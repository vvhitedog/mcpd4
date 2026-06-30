#include <mcpd3_distributed/runtime.h>

#include <decomp/dualdecomp.h>
#include <decomp/partition_coordinator.h>
#include <graph/dimacs.h>

#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Config {
  std::string dimacs_path;
  std::string bind_host = "127.0.0.1";
  std::uint16_t port = 0;
  int worker_count = 1;
  int partition_count = 1;
  int max_iterations = 10000;
  int num_scales = 5;
  long initial_step_size = 10000;
  long capacity_multiplier = 1;
  long accept_timeout_ms = 24L * 60L * 60L * 1000L;
  std::string ready_file;
  bool directed = false;
};

long parseLong(const std::string &value, const std::string &name) {
  const long parsed = std::stol(value);
  if (parsed <= 0) {
    throw std::runtime_error(name + " must be positive");
  }
  return parsed;
}

int parseInt(const std::string &value, const std::string &name) {
  const long parsed = parseLong(value, name);
  if (parsed > std::numeric_limits<int>::max()) {
    throw std::runtime_error(name + " exceeds int range");
  }
  return static_cast<int>(parsed);
}

std::uint16_t parsePort(const std::string &value) {
  const long parsed = parseLong(value, "port");
  if (parsed > std::numeric_limits<std::uint16_t>::max()) {
    throw std::runtime_error("port exceeds uint16 range");
  }
  return static_cast<std::uint16_t>(parsed);
}

void usage(const char *argv0) {
  std::cerr
      << "usage: " << argv0
      << " DIMACS --port PORT [--bind HOST] [--workers N] [--partitions N]\n"
      << "       [--max-iterations N] [--num-scales N] [--initial-step N]\n"
      << "       [--capacity-multiplier N] [--accept-timeout-ms N]\n"
      << "       [--ready-file PATH] [--directed]\n";
}

Config parseArgs(int argc, char **argv) {
  if (argc < 2) {
    throw std::runtime_error("DIMACS path is required");
  }
  Config config;
  config.dimacs_path = argv[1];
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    auto require_value = [&](const std::string &name) -> std::string {
      if (i + 1 >= argc) {
        throw std::runtime_error(name + " requires a value");
      }
      return argv[++i];
    };

    if (arg == "--port") {
      config.port = parsePort(require_value(arg));
    } else if (arg == "--bind") {
      config.bind_host = require_value(arg);
    } else if (arg == "--workers") {
      config.worker_count = parseInt(require_value(arg), arg);
    } else if (arg == "--partitions") {
      config.partition_count = parseInt(require_value(arg), arg);
    } else if (arg == "--max-iterations") {
      config.max_iterations = parseInt(require_value(arg), arg);
    } else if (arg == "--num-scales") {
      config.num_scales = parseInt(require_value(arg), arg);
    } else if (arg == "--initial-step") {
      config.initial_step_size = parseLong(require_value(arg), arg);
    } else if (arg == "--capacity-multiplier") {
      config.capacity_multiplier = parseLong(require_value(arg), arg);
    } else if (arg == "--accept-timeout-ms") {
      config.accept_timeout_ms = parseLong(require_value(arg), arg);
    } else if (arg == "--ready-file") {
      config.ready_file = require_value(arg);
    } else if (arg == "--directed") {
      config.directed = true;
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  if (config.port == 0) {
    throw std::runtime_error("--port is required");
  }
  return config;
}

int checkedScaleInt(int value, long factor) {
  const long scaled = static_cast<long>(value) * factor;
  if (scaled > std::numeric_limits<int>::max() ||
      scaled < std::numeric_limits<int>::min()) {
    throw std::overflow_error("capacity multiplier exceeds int range");
  }
  return static_cast<int>(scaled);
}

void scaleGraph(mcpd3::MinCutGraph *graph, long factor) {
  if (factor == 1) {
    return;
  }
  for (auto &capacity : graph->arc_capacities) {
    capacity = checkedScaleInt(capacity, factor);
  }
  for (auto &capacity : graph->terminal_capacities) {
    capacity = checkedScaleInt(capacity, factor);
  }
}

void writeReadyFile(const std::string &path, std::uint16_t port) {
  if (path.empty()) {
    return;
  }
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("failed to open ready file for writing: " + path);
  }
  out << port << "\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    const Config config = parseArgs(argc, argv);

    auto graph = config.directed
                     ? mcpd3::read_dimacs_directed_streaming(config.dimacs_path)
                     : mcpd3::read_dimacs(config.dimacs_path);
    scaleGraph(&graph, config.capacity_multiplier);

    mcpd3::DualDecompositionOptions package_options;
    package_options.track_primal_upper_bound = false;
    package_options.verbose = false;
    package_options.thread_count = 1;
    package_options.objective_scale = config.capacity_multiplier;
    mcpd3::DualDecomposition package_source(
        config.partition_count, graph.nnode, graph.narc, std::move(graph.arcs),
        std::move(graph.arc_capacities), std::move(graph.terminal_capacities),
        package_options);
    const auto packages = package_source.getPartitionPackages();

    auto listener =
        mcpd3_distributed::listenTcp(config.bind_host, config.port);
    std::cout << "listening " << config.bind_host << ":"
              << mcpd3_distributed::localPort(listener) << "\n";
    writeReadyFile(config.ready_file, mcpd3_distributed::localPort(listener));

    std::vector<std::unique_ptr<mcpd3::PartitionWorker>> workers;
    std::vector<mcpd3_distributed::TcpPartitionWorker *> remote_workers;
    for (int i = 0; i < config.worker_count; ++i) {
      auto worker = mcpd3_distributed::acceptTcpPartitionWorker(
          &listener, std::chrono::milliseconds(config.accept_timeout_ms));
      std::cout << "accepted worker " << worker->hello().worker_name << "\n";
      remote_workers.push_back(worker.get());
      workers.push_back(std::move(worker));
    }

    mcpd3::PartitionWorkerCoordinatorOptions solve_options;
    solve_options.max_iteration_count = config.max_iterations;
    solve_options.num_optimization_scales = config.num_scales;
    solve_options.initial_step_size = config.initial_step_size;
    solve_options.objective_scale = config.capacity_multiplier;
    mcpd3::PartitionWorkerCoordinator coordinator(packages, std::move(workers),
                                                  solve_options);
    const auto result = coordinator.solve();

    std::cout << "status " << static_cast<int>(result.status) << "\n";
    std::cout << "stop_reason " << static_cast<int>(result.stop_reason) << "\n";
    std::cout << "best_lower_bound " << result.best_lower_bound << "\n";
    std::cout << "best_lower_bound_raw " << result.best_lower_bound_raw << "\n";
    std::cout << "objective_scale " << result.scale << "\n";
    std::cout << "objective_scale_promotions "
              << result.objective_scale_promotion_count << "\n";
    std::cout << "total_iterations " << result.total_iterations << "\n";
    std::cout << "final_disagreement_count "
              << result.final_disagreement_count << "\n";
    std::cout << "final_regularization_budget "
              << result.final_regularization_budget << "\n";
    std::cout << "final_regularization_contribution "
              << result.final_regularization_contribution << "\n";
    std::cout << "final_regularization_anchor_sink_count "
              << result.final_regularization_anchor_sink_count << "\n";
    std::cout << "final_regularization_active_sink_count "
              << result.final_regularization_active_sink_count << "\n";
    for (auto *worker : remote_workers) {
      worker->stop(/*reason=*/0, "coordinator finished");
    }
  } catch (const std::exception &e) {
    std::cerr << "mcpd3_coordinator failed: " << e.what() << "\n";
    usage(argv[0]);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
