#include <decomp/dualdecomp.h>
#include <decomp/partition_coordinator.h>
#include <graph/dimacs.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Timing {
  std::uint64_t total_wall_us = 0;
  std::uint64_t read_graph_wall_us = 0;
  std::uint64_t scale_graph_wall_us = 0;
  std::uint64_t partition_wall_us = 0;
  std::uint64_t coordinator_setup_wall_us = 0;
  std::uint64_t solve_wall_us = 0;
};

struct Config {
  std::string dimacs_path;
  int worker_count = 1;
  int partition_count = 10;
  int max_iterations = 10000;
  int schedule_levels = 5;
  long schedule_start = 10000;
  long objective_scale = 1;
  int progress_every = 0;
  bool directed = false;
  bool saturate_capacity_overflow = false;
};

std::uint64_t elapsedUs(std::chrono::steady_clock::time_point start) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start)
          .count());
}

long parseLong(const std::string &value, const std::string &name) {
  char *end = nullptr;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0') {
    throw std::runtime_error("invalid integer for " + name + ": " + value);
  }
  return parsed;
}

int parseInt(const std::string &value, const std::string &name) {
  const long parsed = parseLong(value, name);
  if (parsed < std::numeric_limits<int>::min() ||
      parsed > std::numeric_limits<int>::max()) {
    throw std::runtime_error("integer out of range for " + name + ": " +
                             value);
  }
  return static_cast<int>(parsed);
}

Config parseArgs(int argc, char **argv) {
  Config config;
  if (argc < 2) {
    throw std::runtime_error(
        "usage: mcpd4_inprocess_benchmark DIMACS [--directed] "
        "[--workers N] [--partitions N] [--max-iterations N] "
        "[--schedule-levels N] [--schedule-start N] "
        "[--objective-scale N] [--progress-every N] "
        "[--saturate-capacity-overflow]");
  }
  config.dimacs_path = argv[1];
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    auto requireValue = [&](const std::string &name) -> std::string {
      if (i + 1 >= argc) {
        throw std::runtime_error(name + " requires a value");
      }
      return argv[++i];
    };
    if (arg == "--workers") {
      config.worker_count = parseInt(requireValue(arg), arg);
    } else if (arg == "--partitions") {
      config.partition_count = parseInt(requireValue(arg), arg);
    } else if (arg == "--max-iterations") {
      config.max_iterations = parseInt(requireValue(arg), arg);
    } else if (arg == "--schedule-levels" || arg == "--num-scales") {
      config.schedule_levels = parseInt(requireValue(arg), arg);
    } else if (arg == "--schedule-start" || arg == "--initial-step") {
      config.schedule_start = parseLong(requireValue(arg), arg);
    } else if (arg == "--objective-scale" ||
               arg == "--capacity-multiplier") {
      config.objective_scale = parseLong(requireValue(arg), arg);
    } else if (arg == "--progress-every") {
      config.progress_every = parseInt(requireValue(arg), arg);
    } else if (arg == "--directed") {
      config.directed = true;
    } else if (arg == "--saturate-capacity-overflow" ||
               arg == "--truncate-capacity-overflow") {
      config.saturate_capacity_overflow = true;
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  if (config.worker_count <= 0) {
    throw std::runtime_error("--workers must be positive");
  }
  if (config.partition_count <= 0) {
    throw std::runtime_error("--partitions must be positive");
  }
  if (config.max_iterations <= 0) {
    throw std::runtime_error("--max-iterations must be positive");
  }
  if (config.schedule_levels <= 0) {
    throw std::runtime_error("--schedule-levels must be positive");
  }
  if (config.schedule_start <= 0) {
    throw std::runtime_error("--schedule-start must be positive");
  }
  if (config.objective_scale <= 0) {
    throw std::runtime_error("--objective-scale must be positive");
  }
  return config;
}

int scaleIntCapacity(int value, long factor, bool saturate) {
  const long scaled = static_cast<long>(value) * factor;
  if (scaled > std::numeric_limits<int>::max()) {
    if (saturate) {
      return std::numeric_limits<int>::max();
    }
    throw std::overflow_error("objective scale exceeds int range");
  }
  if (scaled < std::numeric_limits<int>::min()) {
    if (saturate) {
      return std::numeric_limits<int>::min();
    }
    throw std::overflow_error("objective scale exceeds int range");
  }
  return static_cast<int>(scaled);
}

void scaleGraph(mcpd3::MinCutGraph *graph, long factor, bool saturate) {
  if (factor == 1) {
    return;
  }
  for (auto &capacity : graph->arc_capacities) {
    capacity = scaleIntCapacity(capacity, factor, saturate);
  }
  for (auto &capacity : graph->terminal_capacities) {
    capacity = scaleIntCapacity(capacity, factor, saturate);
  }
}

std::vector<mcpd3::PartitionPackage>
makePartitionPackages(int partition_count, mcpd3::MinCutGraph graph,
                      long objective_scale) {
  mcpd3::DualDecompositionOptions package_options;
  package_options.track_primal_upper_bound = false;
  package_options.verbose = false;
  package_options.thread_count = 1;
  package_options.objective_scale = objective_scale;
  package_options.saturate_capacity_overflow = false;
  mcpd3::DualDecomposition package_source(
      partition_count, graph.nnode, graph.narc, std::move(graph.arcs),
      std::move(graph.arc_capacities), std::move(graph.terminal_capacities),
      package_options);
  return package_source.getPartitionPackages();
}

std::vector<std::unique_ptr<mcpd3::PartitionWorker>>
makeInProcessWorkers(int worker_count) {
  std::vector<std::unique_ptr<mcpd3::PartitionWorker>> workers;
  workers.reserve(worker_count);
  for (int i = 0; i < worker_count; ++i) {
    workers.push_back(std::make_unique<mcpd3::InProcessPartitionWorker>());
  }
  return workers;
}

void printProgress(const mcpd3::PartitionWorkerProgressRecord &record) {
  std::cout << "progress total_iteration " << record.total_iteration
            << " schedule_scale " << record.scale
            << " iteration " << record.iteration << " lower_bound "
            << record.lower_bound << " best_lower_bound "
            << record.best_lower_bound << " certified_lower_bound "
            << record.certified_lower_bound << " best_certified_lower_bound "
            << record.best_certified_lower_bound << " regularized_objective "
            << record.regularized_objective << " best_regularized_objective "
            << record.best_regularized_objective << " disagreement_count "
            << record.disagreement_count << " schedule_step "
            << record.step_size << " effective_schedule_step "
            << record.effective_step_size << " regularization_strength "
            << record.regularization_strength << " regularization_budget "
            << record.regularization_budget
            << " regularization_contribution "
            << record.regularization_contribution
            << " regularization_anchor_sink_count "
            << record.regularization_anchor_sink_count
            << " regularization_active_sink_count "
            << record.regularization_active_sink_count
            << " iterations_since_improvement "
            << record.iterations_since_improvement << "\n";
}

void printFinal(const Timing &timing,
                const mcpd3::PartitionWorkerCoordinatorSolveResult &result,
                const Config &config) {
  std::cout << "status " << static_cast<int>(result.status) << "\n";
  std::cout << "stop_reason " << static_cast<int>(result.stop_reason) << "\n";
  std::cout << "final_objective " << result.final_objective << "\n";
  std::cout << "final_objective_raw " << result.final_objective_raw << "\n";
  std::cout << "final_certified_lower_bound "
            << result.final_certified_lower_bound << "\n";
  std::cout << "final_certified_lower_bound_raw "
            << result.final_certified_lower_bound_raw << "\n";
  std::cout << "final_regularized_objective "
            << result.final_regularized_objective << "\n";
  std::cout << "final_regularized_objective_raw "
            << result.final_regularized_objective_raw << "\n";
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
  std::cout << "worker_count " << config.worker_count << "\n";
  std::cout << "partition_count " << config.partition_count << "\n";
  std::cout << "timing_total_wall_us " << timing.total_wall_us << "\n";
  std::cout << "timing_read_graph_wall_us " << timing.read_graph_wall_us
            << "\n";
  std::cout << "timing_scale_graph_wall_us " << timing.scale_graph_wall_us
            << "\n";
  std::cout << "timing_partition_wall_us " << timing.partition_wall_us << "\n";
  std::cout << "timing_coordinator_setup_wall_us "
            << timing.coordinator_setup_wall_us << "\n";
  std::cout << "timing_solve_wall_us " << timing.solve_wall_us << "\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    const auto config = parseArgs(argc, argv);
    Timing timing;
    const auto total_start = std::chrono::steady_clock::now();

    mcpd3::MinCutGraph graph;
    const auto read_start = std::chrono::steady_clock::now();
    graph = config.directed ? mcpd3::read_dimacs_directed_streaming(
                                  config.dimacs_path)
                            : mcpd3::read_dimacs(config.dimacs_path);
    timing.read_graph_wall_us = elapsedUs(read_start);

    const auto scale_start = std::chrono::steady_clock::now();
    scaleGraph(&graph, config.objective_scale,
               config.saturate_capacity_overflow);
    timing.scale_graph_wall_us = elapsedUs(scale_start);

    const auto partition_start = std::chrono::steady_clock::now();
    auto packages =
        makePartitionPackages(config.partition_count, std::move(graph),
                              config.objective_scale);
    timing.partition_wall_us = elapsedUs(partition_start);

    mcpd3::PartitionWorkerCoordinatorOptions solve_options;
    solve_options.max_iteration_count = config.max_iterations;
    solve_options.num_optimization_scales = config.schedule_levels;
    solve_options.initial_step_size = config.schedule_start;
    solve_options.objective_scale = config.objective_scale;
    solve_options.saturate_capacity_overflow =
        config.saturate_capacity_overflow;
    solve_options.progress_report_interval =
        config.progress_every > 0 ? config.progress_every : 0;
    if (config.progress_every > 0) {
      solve_options.progress_callback = printProgress;
    }

    auto workers = makeInProcessWorkers(config.worker_count);
    const auto setup_start = std::chrono::steady_clock::now();
    mcpd3::PartitionWorkerCoordinator coordinator(std::move(packages),
                                                  std::move(workers),
                                                  solve_options);
    timing.coordinator_setup_wall_us = elapsedUs(setup_start);

    const auto solve_start = std::chrono::steady_clock::now();
    const auto result = coordinator.solve();
    timing.solve_wall_us = elapsedUs(solve_start);
    timing.total_wall_us = elapsedUs(total_start);
    printFinal(timing, result, config);
  } catch (const std::exception &e) {
    std::cerr << "mcpd4_inprocess_benchmark failed: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
