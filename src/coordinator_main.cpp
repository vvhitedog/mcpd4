#include <mcpd3_distributed/runtime.h>

#include <decomp/dualdecomp.h>
#include <decomp/partition_coordinator.h>
#include <graph/dimacs.h>

#include <chrono>
#include <cstdint>
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

struct RuntimeTiming {
  std::uint64_t total_wall_us = 0;
  std::uint64_t read_graph_wall_us = 0;
  std::uint64_t scale_graph_wall_us = 0;
  std::uint64_t partition_wall_us = 0;
  std::uint64_t accept_workers_wall_us = 0;
  std::uint64_t coordinator_setup_wall_us = 0;
  std::uint64_t solve_wall_us = 0;
  std::uint64_t stop_workers_wall_us = 0;
};

struct CapacityScaleStats {
  long arc_saturation_count = 0;
  long terminal_saturation_count = 0;
};

std::uint64_t elapsedUs(std::chrono::steady_clock::time_point start) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start)
          .count());
}

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
  int progress_every = 0;
  std::string ready_file;
  bool saturate_capacity_overflow = false;
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

int parseNonNegativeInt(const std::string &value, const std::string &name) {
  const long parsed = std::stol(value);
  if (parsed < 0) {
    throw std::runtime_error(name + " must be non-negative");
  }
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
      << "       [--progress-every N] [--ready-file PATH]\n"
      << "       [--saturate-capacity-overflow]\n"
      << "       [--directed]\n";
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
    } else if (arg == "--progress-every") {
      config.progress_every = parseNonNegativeInt(require_value(arg), arg);
    } else if (arg == "--ready-file") {
      config.ready_file = require_value(arg);
    } else if (arg == "--saturate-capacity-overflow" ||
               arg == "--truncate-capacity-overflow") {
      config.saturate_capacity_overflow = true;
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

bool wouldOverflowIntScale(int value, long factor) {
  if (value > 0 &&
      value > std::numeric_limits<int>::max() / factor) {
    return true;
  }
  if (value < 0 &&
      value < std::numeric_limits<int>::min() / factor) {
    return true;
  }
  return false;
}

int scaleIntCapacity(int value, long factor, bool saturate_overflow,
                     long *saturation_count) {
  if (wouldOverflowIntScale(value, factor)) {
    if (!saturate_overflow) {
      throw std::overflow_error("capacity multiplier exceeds int range");
    }
    ++(*saturation_count);
    return value < 0 ? std::numeric_limits<int>::min()
                     : std::numeric_limits<int>::max();
  }
  const long scaled = static_cast<long>(value) * factor;
  if (scaled > std::numeric_limits<int>::max() ||
      scaled < std::numeric_limits<int>::min()) {
    throw std::overflow_error("capacity multiplier exceeds int range");
  }
  return static_cast<int>(scaled);
}

void scaleGraph(mcpd3::MinCutGraph *graph, long factor,
                bool saturate_capacity_overflow,
                CapacityScaleStats *stats) {
  if (factor == 1) {
    return;
  }
  for (auto &capacity : graph->arc_capacities) {
    capacity = scaleIntCapacity(capacity, factor, saturate_capacity_overflow,
                                &stats->arc_saturation_count);
  }
  for (auto &capacity : graph->terminal_capacities) {
    capacity =
        scaleIntCapacity(capacity, factor, saturate_capacity_overflow,
                         &stats->terminal_saturation_count);
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

std::vector<mcpd3::PartitionPackage> makePartitionPackages(
    int partition_count, mcpd3::MinCutGraph graph, long objective_scale) {
  mcpd3::DualDecompositionOptions package_options;
  package_options.track_primal_upper_bound = false;
  package_options.verbose = false;
  package_options.thread_count = 1;
  package_options.objective_scale = objective_scale;
  mcpd3::DualDecomposition package_source(
      partition_count, graph.nnode, graph.narc, std::move(graph.arcs),
      std::move(graph.arc_capacities), std::move(graph.terminal_capacities),
      package_options);
  return package_source.getPartitionPackages();
}

std::uint64_t saturatedSubtract(std::uint64_t lhs, std::uint64_t rhs) {
  return lhs > rhs ? lhs - rhs : 0;
}

void printTiming(const RuntimeTiming &timing,
                 const std::vector<mcpd3_distributed::TcpPartitionWorker *>
                     &remote_workers) {
  std::uint64_t load_rpc_us = 0;
  std::uint64_t solve_rpc_us = 0;
  std::uint64_t worker_solve_us = 0;
  std::uint64_t scale_rpc_us = 0;
  long load_count = 0;
  long solve_count = 0;
  long scale_count = 0;
  for (const auto *worker : remote_workers) {
    const auto &stats = worker->timingStats();
    load_rpc_us += stats.load_partition_rpc_wall_us;
    solve_rpc_us += stats.solve_round_rpc_wall_us;
    worker_solve_us += stats.solve_round_worker_wall_us;
    scale_rpc_us += stats.scale_objective_rpc_wall_us;
    load_count += stats.load_partition_count;
    solve_count += stats.solve_round_count;
    scale_count += stats.scale_objective_count;
  }

  const auto coordinator_compute_us =
      saturatedSubtract(timing.solve_wall_us, solve_rpc_us);
  const auto worker_rpc_overhead_us =
      saturatedSubtract(solve_rpc_us, worker_solve_us);

  std::cout << "timing_total_wall_us " << timing.total_wall_us << "\n";
  std::cout << "timing_read_graph_wall_us " << timing.read_graph_wall_us
            << "\n";
  std::cout << "timing_scale_graph_wall_us " << timing.scale_graph_wall_us
            << "\n";
  std::cout << "timing_partition_wall_us " << timing.partition_wall_us
            << "\n";
  std::cout << "timing_accept_workers_wall_us "
            << timing.accept_workers_wall_us << "\n";
  std::cout << "timing_coordinator_setup_wall_us "
            << timing.coordinator_setup_wall_us << "\n";
  std::cout << "timing_solve_wall_us " << timing.solve_wall_us << "\n";
  std::cout << "timing_coordinator_compute_us " << coordinator_compute_us
            << "\n";
  std::cout << "timing_coordinator_wait_worker_us " << solve_rpc_us << "\n";
  std::cout << "timing_worker_solve_us " << worker_solve_us << "\n";
  std::cout << "timing_worker_rpc_overhead_us " << worker_rpc_overhead_us
            << "\n";
  std::cout << "timing_load_partition_rpc_us " << load_rpc_us << "\n";
  std::cout << "timing_scale_objective_rpc_us " << scale_rpc_us << "\n";
  std::cout << "timing_stop_workers_wall_us " << timing.stop_workers_wall_us
            << "\n";
  std::cout << "timing_load_partition_count " << load_count << "\n";
  std::cout << "timing_solve_round_count " << solve_count << "\n";
  std::cout << "timing_scale_objective_count " << scale_count << "\n";
}

void printCapacityScaleStats(const Config &config,
                             const CapacityScaleStats &stats) {
  const long total_saturation_count =
      stats.arc_saturation_count + stats.terminal_saturation_count;
  std::cout << "capacity_scale_multiplier " << config.capacity_multiplier
            << "\n";
  std::cout << "capacity_scale_overflow_mode "
            << (config.saturate_capacity_overflow ? "saturate" : "strict")
            << "\n";
  std::cout << "capacity_scale_saturation_count "
            << total_saturation_count << "\n";
  std::cout << "capacity_scale_arc_saturation_count "
            << stats.arc_saturation_count << "\n";
  std::cout << "capacity_scale_terminal_saturation_count "
            << stats.terminal_saturation_count << "\n";
}

void printProgress(
    const mcpd3::PartitionWorkerProgressRecord &record,
    const std::vector<mcpd3_distributed::TcpPartitionWorker *>
        &remote_workers) {
  std::uint64_t solve_rpc_us = 0;
  std::uint64_t worker_solve_us = 0;
  long solve_count = 0;
  for (const auto *worker : remote_workers) {
    const auto &stats = worker->timingStats();
    solve_rpc_us += stats.solve_round_rpc_wall_us;
    worker_solve_us += stats.solve_round_worker_wall_us;
    solve_count += stats.solve_round_count;
  }
  const auto worker_rpc_overhead_us =
      saturatedSubtract(solve_rpc_us, worker_solve_us);

  std::cout << "progress"
            << " total_iteration " << record.total_iteration
            << " scale " << record.scale
            << " iteration " << record.iteration
            << " lower_bound " << record.lower_bound
            << " best_lower_bound " << record.best_lower_bound
            << " disagreement_count " << record.disagreement_count
            << " disagreement_norm_sq " << record.disagreement_norm_sq
            << " step_size " << record.step_size
            << " effective_step_size " << record.effective_step_size
            << " regularization_strength "
            << record.regularization_strength
            << " regularization_budget " << record.regularization_budget
            << " regularization_contribution "
            << record.regularization_contribution
            << " regularization_anchor_sink_count "
            << record.regularization_anchor_sink_count
            << " regularization_active_sink_count "
            << record.regularization_active_sink_count
            << " iterations_since_improvement "
            << record.iterations_since_improvement
            << " solve_round_count " << solve_count
            << " solve_rpc_wall_us " << solve_rpc_us
            << " worker_solve_wall_us " << worker_solve_us
            << " worker_rpc_overhead_us " << worker_rpc_overhead_us << "\n";

  for (const auto *worker : remote_workers) {
    const auto &stats = worker->timingStats();
    std::cout << "progress_worker"
              << " total_iteration " << record.total_iteration
              << " name " << worker->hello().worker_name
              << " solve_round_count " << stats.solve_round_count
              << " solve_rpc_wall_us " << stats.solve_round_rpc_wall_us
              << " worker_solve_wall_us "
              << stats.solve_round_worker_wall_us
              << " worker_rpc_overhead_us "
              << saturatedSubtract(stats.solve_round_rpc_wall_us,
                                   stats.solve_round_worker_wall_us)
              << "\n";
  }
  std::cout.flush();
}

} // namespace

int main(int argc, char **argv) {
  try {
    const auto total_start = std::chrono::steady_clock::now();
    RuntimeTiming timing;
    const Config config = parseArgs(argc, argv);

    auto graph_start = std::chrono::steady_clock::now();
    auto graph = config.directed
                     ? mcpd3::read_dimacs_directed_streaming(config.dimacs_path)
                     : mcpd3::read_dimacs(config.dimacs_path);
    timing.read_graph_wall_us = elapsedUs(graph_start);

    CapacityScaleStats capacity_scale_stats;
    const auto scale_graph_start = std::chrono::steady_clock::now();
    scaleGraph(&graph, config.capacity_multiplier,
               config.saturate_capacity_overflow, &capacity_scale_stats);
    timing.scale_graph_wall_us = elapsedUs(scale_graph_start);
    if (capacity_scale_stats.arc_saturation_count +
            capacity_scale_stats.terminal_saturation_count >
        0) {
      std::cerr
          << "warning: saturated "
          << capacity_scale_stats.arc_saturation_count +
                 capacity_scale_stats.terminal_saturation_count
          << " capacities while scaling; results use clipped int capacities\n";
    }

    const auto partition_start = std::chrono::steady_clock::now();
    const auto packages = makePartitionPackages(
        config.partition_count, std::move(graph), config.capacity_multiplier);
    timing.partition_wall_us = elapsedUs(partition_start);

    auto listener =
        mcpd3_distributed::listenTcp(config.bind_host, config.port);
    std::cout << "listening " << config.bind_host << ":"
              << mcpd3_distributed::localPort(listener) << "\n";
    writeReadyFile(config.ready_file, mcpd3_distributed::localPort(listener));

    std::vector<std::unique_ptr<mcpd3::PartitionWorker>> workers;
    std::vector<mcpd3_distributed::TcpPartitionWorker *> remote_workers;
    const auto accept_start = std::chrono::steady_clock::now();
    for (int i = 0; i < config.worker_count; ++i) {
      auto worker = mcpd3_distributed::acceptTcpPartitionWorker(
          &listener, std::chrono::milliseconds(config.accept_timeout_ms));
      std::cout << "accepted worker " << worker->hello().worker_name << "\n";
      remote_workers.push_back(worker.get());
      workers.push_back(std::move(worker));
    }
    timing.accept_workers_wall_us = elapsedUs(accept_start);

    mcpd3::PartitionWorkerCoordinatorOptions solve_options;
    solve_options.max_iteration_count = config.max_iterations;
    solve_options.num_optimization_scales = config.num_scales;
    solve_options.initial_step_size = config.initial_step_size;
    solve_options.objective_scale = config.capacity_multiplier;
    solve_options.progress_report_interval = config.progress_every;
    solve_options.progress_callback =
        [&](const mcpd3::PartitionWorkerProgressRecord &record) {
          printProgress(record, remote_workers);
        };
    const auto coordinator_setup_start = std::chrono::steady_clock::now();
    mcpd3::PartitionWorkerCoordinator coordinator(packages, std::move(workers),
                                                  solve_options);
    timing.coordinator_setup_wall_us = elapsedUs(coordinator_setup_start);
    const auto solve_start = std::chrono::steady_clock::now();
    const auto result = coordinator.solve();
    timing.solve_wall_us = elapsedUs(solve_start);

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
    printCapacityScaleStats(config, capacity_scale_stats);
    const auto stop_start = std::chrono::steady_clock::now();
    for (auto *worker : remote_workers) {
      worker->stop(/*reason=*/0, "coordinator finished");
    }
    timing.stop_workers_wall_us = elapsedUs(stop_start);
    timing.total_wall_us = elapsedUs(total_start);
    printTiming(timing, remote_workers);
  } catch (const std::exception &e) {
    std::cerr << "mcpd3_coordinator failed: " << e.what() << "\n";
    usage(argv[0]);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
