#include <decomp/dualdecomp.h>
#include <decomp/partition_coordinator.h>
#include <graph/dimacs.h>
#include <io/memory.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
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

struct MemorySnapshot {
  double vm_kb = 0;
  double rss_kb = 0;
};

struct Config {
  std::string dimacs_path;
  std::string stop_after;
  int worker_count = 1;
  int partition_count = 10;
  int max_iterations = 10000;
  int schedule_levels = 5;
  long schedule_start = 10000;
  long objective_scale = 1;
  int progress_every = 0;
  std::string streaming_dir;
  std::uint64_t streaming_cache_bytes = 0;
  bool exhaust_scale_iterations = false;
  bool exhaust_regularized_scale_iterations = true;
  bool directed = false;
  bool saturate_capacity_overflow = false;
  bool streaming_workers = false;
};

std::uint64_t elapsedUs(std::chrono::steady_clock::time_point start) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start)
          .count());
}

template <typename Integer>
std::string integerString(const Integer &value) {
  return mcpd3::integer_to_string(value);
}

MemorySnapshot memorySnapshot() {
  MemorySnapshot snapshot;
  mcpd3::process_mem_usage(snapshot.vm_kb, snapshot.rss_kb);
  return snapshot;
}

void printMemory(const std::string &prefix, const MemorySnapshot &snapshot) {
  std::cout << prefix << "_vm_kb " << snapshot.vm_kb << "\n";
  std::cout << prefix << "_rss_kb " << snapshot.rss_kb << "\n";
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

std::uint64_t parsePositiveU64(const std::string &value,
                               const std::string &name) {
  const auto parsed = std::stoull(value);
  if (parsed == 0) {
    throw std::runtime_error(name + " must be positive");
  }
  return parsed;
}

Config parseArgs(int argc, char **argv) {
  Config config;
  if (argc < 2) {
    throw std::runtime_error(
        "usage: mcpd4_inprocess_benchmark DIMACS [--directed] "
        "[--workers N] [--partitions N] [--max-iterations N] "
        "[--schedule-levels N] [--schedule-start N] "
        "[--objective-scale N] [--progress-every N] "
        "[--stop-after read|scale|partition|setup] "
        "[--streaming-workers] [--streaming-dir DIR] "
        "[--streaming-cache-bytes N] "
        "[--exhaust-scale-iterations] "
        "[--no-exhaust-regularized-scale-iterations] "
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
    } else if (arg == "--stop-after") {
      config.stop_after = requireValue(arg);
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
    } else if (arg == "--streaming-workers" ||
               arg == "--streaming-partitions") {
      config.streaming_workers = true;
    } else if (arg == "--streaming-dir") {
      config.streaming_dir = requireValue(arg);
    } else if (arg == "--streaming-cache-bytes") {
      config.streaming_cache_bytes =
          parsePositiveU64(requireValue(arg), arg);
    } else if (arg == "--exhaust-scale-iterations") {
      config.exhaust_scale_iterations = true;
    } else if (arg == "--exhaust-regularized-scale-iterations") {
      config.exhaust_regularized_scale_iterations = true;
    } else if (arg == "--no-exhaust-regularized-scale-iterations") {
      config.exhaust_regularized_scale_iterations = false;
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
  if (!config.stop_after.empty() && config.stop_after != "read" &&
      config.stop_after != "scale" && config.stop_after != "partition" &&
      config.stop_after != "setup") {
    throw std::runtime_error(
        "--stop-after must be one of read, scale, partition, setup");
  }
  return config;
}

bool shouldStopAfter(const Config &config, const std::string &phase) {
  return config.stop_after == phase;
}

void scaleGraph(mcpd3::MinCutGraph *graph, long factor, bool saturate) {
  if (factor == 1) {
    return;
  }
  for (auto &capacity : graph->arc_capacities) {
    capacity = mcpd3::checked_scale_capacity(capacity, factor, saturate);
  }
  for (auto &capacity : graph->terminal_capacities) {
    capacity = mcpd3::checked_scale_capacity(capacity, factor, saturate);
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
  package_options.construct_solvers = false;
  mcpd3::DualDecomposition package_source(
      partition_count, graph.nnode, graph.narc, std::move(graph.arcs),
      std::move(graph.arc_capacities), std::move(graph.terminal_capacities),
      package_options);
  return package_source.getPartitionPackages();
}

void printConfig(const Config &config) {
  std::cout << "benchmark inprocess_partition_worker\n";
  std::cout << "dimacs_path " << config.dimacs_path << "\n";
  std::cout << "worker_count " << config.worker_count << "\n";
  std::cout << "partition_count " << config.partition_count << "\n";
  std::cout << "schedule_start " << config.schedule_start << "\n";
  std::cout << "schedule_levels " << config.schedule_levels << "\n";
  std::cout << "max_iterations " << config.max_iterations << "\n";
  std::cout << "objective_scale " << config.objective_scale << "\n";
  std::cout << "progress_every " << config.progress_every << "\n";
  std::cout << "streaming_workers " << config.streaming_workers << "\n";
  std::cout << "streaming_dir "
            << (config.streaming_dir.empty() ? "auto" : config.streaming_dir)
            << "\n";
  std::cout << "streaming_cache_bytes " << config.streaming_cache_bytes
            << "\n";
  std::cout << "directed " << config.directed << "\n";
  std::cout << "saturate_capacity_overflow "
            << config.saturate_capacity_overflow << "\n";
  std::cout << "stop_after "
            << (config.stop_after.empty() ? "none" : config.stop_after)
            << "\n";
}

void printGraphStats(const mcpd3::MinCutGraph &graph) {
  std::cout << "graph_node_count " << graph.nnode << "\n";
  std::cout << "graph_arc_count " << graph.narc << "\n";
  std::cout << "graph_arc_endpoint_count " << graph.arcs.size() << "\n";
  std::cout << "graph_arc_capacity_count " << graph.arc_capacities.size()
            << "\n";
  std::cout << "graph_terminal_capacity_count "
            << graph.terminal_capacities.size() << "\n";
}

std::uint64_t sumValues(const std::vector<std::uint64_t> &values) {
  std::uint64_t total = 0;
  for (const auto value : values) {
    total += value;
  }
  return total;
}

std::uint64_t percentileValue(std::vector<std::uint64_t> values,
                              double percentile) {
  if (values.empty()) {
    return 0;
  }
  std::sort(values.begin(), values.end());
  const auto index = static_cast<size_t>(
      std::max<double>(0.0, std::ceil(percentile * values.size()) - 1.0));
  return values[std::min(index, values.size() - 1)];
}

std::uint64_t topKSum(std::vector<std::uint64_t> values, size_t count) {
  if (values.empty() || count == 0) {
    return 0;
  }
  std::sort(values.begin(), values.end(), std::greater<std::uint64_t>());
  count = std::min(count, values.size());
  std::uint64_t total = 0;
  for (size_t i = 0; i < count; ++i) {
    total += values[i];
  }
  return total;
}

void printByteSummary(const std::string &prefix,
                      const std::vector<std::uint64_t> &values) {
  const auto total = sumValues(values);
  const auto max_iter = std::max_element(values.begin(), values.end());
  const auto max_value = max_iter == values.end() ? 0 : *max_iter;
  const auto mean = values.empty()
                        ? 0.0
                        : static_cast<double>(total) /
                              static_cast<double>(values.size());
  std::cout << prefix << "_total " << total << "\n";
  std::cout << prefix << "_max " << max_value << "\n";
  std::cout << prefix << "_mean " << mean << "\n";
  std::cout << prefix << "_p95 " << percentileValue(values, 0.95) << "\n";
}

void printPackageStats(const std::vector<mcpd3::PartitionPackage> &packages) {
  std::uint64_t local_node_count = 0;
  std::uint64_t arc_endpoint_count = 0;
  std::uint64_t arc_capacity_count = 0;
  std::uint64_t terminal_capacity_count = 0;
  std::uint64_t local_to_global_count = 0;
  std::uint64_t constraint_endpoint_count = 0;
  std::vector<std::uint64_t> bk_node_bytes;
  std::vector<std::uint64_t> bk_arc_bytes;
  std::vector<std::uint64_t> bk_total_bytes;
  std::vector<std::uint64_t> solver_vector_bytes;
  std::vector<std::uint64_t> loaded_solver_estimated_bytes;
  std::vector<std::uint64_t> partition_package_payload_bytes;
  bk_node_bytes.reserve(packages.size());
  bk_arc_bytes.reserve(packages.size());
  bk_total_bytes.reserve(packages.size());
  solver_vector_bytes.reserve(packages.size());
  loaded_solver_estimated_bytes.reserve(packages.size());
  partition_package_payload_bytes.reserve(packages.size());
  for (const auto &package : packages) {
    local_node_count += static_cast<std::uint64_t>(package.local_node_count);
    arc_endpoint_count += package.arcs.size();
    arc_capacity_count += package.arc_capacities.size();
    terminal_capacity_count += package.terminal_capacities.size();
    local_to_global_count += package.local_to_global.size();
    constraint_endpoint_count += package.constraint_endpoints.size();
    const auto local_arc_count =
        static_cast<int>(package.arcs.size() / 2);
    const auto estimate =
        mcpd3::PrimalDualMinCutSolver::estimateMemoryBytes(
            package.local_node_count, local_arc_count);
    const auto endpoint_bytes =
        static_cast<std::uint64_t>(package.constraint_endpoints.size()) *
        static_cast<std::uint64_t>(
            sizeof(mcpd3::ConstraintEndpointBinding));
    const auto package_int_bytes =
        static_cast<std::uint64_t>(
            package.arcs.size() + package.arc_capacities.size() +
            package.terminal_capacities.size() +
            package.local_to_global.size()) *
        static_cast<std::uint64_t>(sizeof(int));
    const auto package_payload_bytes = package_int_bytes + endpoint_bytes;
    bk_node_bytes.push_back(estimate.bk_node_bytes);
    bk_arc_bytes.push_back(estimate.bk_arc_bytes);
    bk_total_bytes.push_back(estimate.bk_total_bytes);
    solver_vector_bytes.push_back(estimate.solver_vector_bytes);
    loaded_solver_estimated_bytes.push_back(estimate.total_bytes +
                                            endpoint_bytes);
    partition_package_payload_bytes.push_back(package_payload_bytes);
    std::cout << "partition_footprint partition_id " << package.partition_id
              << " local_node_count " << package.local_node_count
              << " local_arc_count " << local_arc_count
              << " constraint_endpoint_count "
              << package.constraint_endpoints.size()
              << " bk_node_bytes " << estimate.bk_node_bytes
              << " bk_arc_bytes " << estimate.bk_arc_bytes
              << " bk_total_bytes " << estimate.bk_total_bytes
              << " solver_vector_bytes " << estimate.solver_vector_bytes
              << " loaded_solver_estimated_bytes "
              << estimate.total_bytes + endpoint_bytes
              << " package_payload_bytes " << package_payload_bytes << "\n";
  }
  const auto int_bytes =
      (arc_endpoint_count + arc_capacity_count + terminal_capacity_count +
       local_to_global_count) *
      static_cast<std::uint64_t>(sizeof(int));
  const auto endpoint_bytes = constraint_endpoint_count *
                              static_cast<std::uint64_t>(
                                  sizeof(mcpd3::ConstraintEndpointBinding));

  std::cout << "package_count " << packages.size() << "\n";
  std::cout << "package_total_local_node_count " << local_node_count << "\n";
  std::cout << "package_total_arc_endpoint_count " << arc_endpoint_count
            << "\n";
  std::cout << "package_total_arc_capacity_count " << arc_capacity_count
            << "\n";
  std::cout << "package_total_terminal_capacity_count "
            << terminal_capacity_count << "\n";
  std::cout << "package_total_local_to_global_count " << local_to_global_count
            << "\n";
  std::cout << "package_total_constraint_endpoint_count "
            << constraint_endpoint_count << "\n";
  std::cout << "package_payload_int_bytes " << int_bytes << "\n";
  std::cout << "package_payload_constraint_endpoint_bytes " << endpoint_bytes
            << "\n";
  printByteSummary("package_bk_node_bytes", bk_node_bytes);
  printByteSummary("package_bk_arc_bytes", bk_arc_bytes);
  printByteSummary("package_bk_total_bytes", bk_total_bytes);
  printByteSummary("package_solver_vector_bytes", solver_vector_bytes);
  printByteSummary("package_loaded_solver_estimated_bytes",
                   loaded_solver_estimated_bytes);
  printByteSummary("package_stream_payload_bytes",
                   partition_package_payload_bytes);
  const std::vector<size_t> windows{1, 2, 4, 8, 16};
  for (const auto window : windows) {
    if (window > packages.size()) {
      continue;
    }
    std::cout << "stream_window_" << window
              << "_bk_total_bytes_worst "
              << topKSum(bk_total_bytes, window) << "\n";
    std::cout << "stream_window_" << window
              << "_loaded_solver_estimated_bytes_worst "
              << topKSum(loaded_solver_estimated_bytes, window) << "\n";
    std::cout << "stream_window_" << window
              << "_package_payload_bytes_worst "
              << topKSum(partition_package_payload_bytes, window) << "\n";
  }
}

std::vector<std::unique_ptr<mcpd3::PartitionWorker>>
makeInProcessWorkers(const Config &config) {
  std::vector<std::unique_ptr<mcpd3::PartitionWorker>> workers;
  workers.reserve(config.worker_count);
  for (int i = 0; i < config.worker_count; ++i) {
    if (config.streaming_workers) {
      mcpd3::StreamingPartitionWorker::Options options;
      options.resident_byte_limit = config.streaming_cache_bytes;
      if (!config.streaming_dir.empty()) {
        options.storage_directory =
            (std::filesystem::path(config.streaming_dir) /
             ("worker_" + std::to_string(i)))
                .string();
        options.remove_storage_on_destroy = false;
      }
      workers.push_back(
          std::make_unique<mcpd3::StreamingPartitionWorker>(std::move(options)));
    } else {
      workers.push_back(std::make_unique<mcpd3::InProcessPartitionWorker>());
    }
  }
  return workers;
}

void printProgress(const mcpd3::PartitionWorkerProgressRecord &record) {
  std::cout << "progress total_iteration " << record.total_iteration
            << " schedule_scale " << record.scale
            << " iteration " << record.iteration << " lower_bound "
            << integerString(record.lower_bound) << " best_lower_bound "
            << integerString(record.best_lower_bound)
            << " certified_lower_bound "
            << integerString(record.certified_lower_bound)
            << " best_certified_lower_bound "
            << integerString(record.best_certified_lower_bound)
            << " regularized_objective "
            << integerString(record.regularized_objective)
            << " best_regularized_objective "
            << integerString(record.best_regularized_objective)
            << " disagreement_count "
            << record.disagreement_count << " schedule_step "
            << record.step_size << " effective_schedule_step "
            << record.effective_step_size << " regularization_strength "
            << integerString(record.regularization_strength)
            << " regularization_budget "
            << integerString(record.regularization_budget)
            << " regularization_contribution "
            << integerString(record.regularization_contribution)
            << " regularization_anchor_sink_count "
            << record.regularization_anchor_sink_count
            << " regularization_active_sink_count "
            << record.regularization_active_sink_count
            << " iterations_since_improvement "
            << record.iterations_since_improvement << "\n";
}

void printFinal(const Timing &timing,
                const mcpd3::PartitionWorkerCoordinatorSolveResult &result,
                const Config &config, double peak_rss_kb) {
  std::cout << "status " << static_cast<int>(result.status) << "\n";
  std::cout << "stop_reason " << static_cast<int>(result.stop_reason) << "\n";
  std::cout << "final_objective " << result.final_objective << "\n";
  std::cout << "final_objective_raw "
            << integerString(result.final_objective_raw) << "\n";
  std::cout << "final_certified_lower_bound "
            << result.final_certified_lower_bound << "\n";
  std::cout << "final_certified_lower_bound_raw "
            << integerString(result.final_certified_lower_bound_raw) << "\n";
  std::cout << "final_regularized_objective "
            << result.final_regularized_objective << "\n";
  std::cout << "final_regularized_objective_raw "
            << integerString(result.final_regularized_objective_raw) << "\n";
  std::cout << "objective_scale " << result.scale << "\n";
  std::cout << "objective_scale_promotions "
            << result.objective_scale_promotion_count << "\n";
  std::cout << "total_iterations " << result.total_iterations << "\n";
  std::cout << "final_disagreement_count "
            << result.final_disagreement_count << "\n";
  std::cout << "final_regularization_budget "
            << integerString(result.final_regularization_budget) << "\n";
  std::cout << "final_regularization_contribution "
            << integerString(result.final_regularization_contribution) << "\n";
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
  std::cout << "timing_coordinator_round_count "
            << result.timing.round_count << "\n";
  std::cout << "timing_coordinator_solve_partitions_wall_us "
            << result.timing.solve_partitions_wall_us << "\n";
  std::cout << "timing_coordinator_prepare_alpha_updates_us "
            << result.timing.prepare_alpha_updates_us << "\n";
  std::cout << "timing_coordinator_build_requests_us "
            << result.timing.build_requests_us << "\n";
  std::cout << "timing_coordinator_dispatch_workers_wall_us "
            << result.timing.dispatch_workers_wall_us << "\n";
  std::cout << "timing_coordinator_worker_batch_wall_us "
            << result.timing.worker_batch_wall_us << "\n";
  std::cout << "timing_coordinator_gather_round_terms_us "
            << result.timing.gather_round_terms_us << "\n";
  std::cout << "timing_coordinator_update_constraints_us "
            << result.timing.update_constraints_from_labels_us << "\n";
  const std::uint64_t accounted_coordinator_us =
      result.timing.solve_partitions_wall_us +
      result.timing.gather_round_terms_us +
      result.timing.update_constraints_from_labels_us;
  std::cout << "timing_coordinator_accounted_wall_us "
            << accounted_coordinator_us << "\n";
  std::cout << "timing_coordinator_unaccounted_wall_us "
            << (timing.solve_wall_us > accounted_coordinator_us
                    ? timing.solve_wall_us - accounted_coordinator_us
                    : 0)
            << "\n";
  std::cout << "memory_peak_observed_rss_kb " << peak_rss_kb << "\n";
}

void printStopped(const Timing &timing, const Config &config,
                  const std::string &phase, double peak_rss_kb) {
  std::cout << "status stopped_after_" << phase << "\n";
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
  std::cout << "memory_peak_observed_rss_kb " << peak_rss_kb << "\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    std::cout << std::unitbuf;
    const auto config = parseArgs(argc, argv);
    printConfig(config);
    Timing timing;
    const auto total_start = std::chrono::steady_clock::now();
    auto peak_rss_kb = memorySnapshot().rss_kb;
    printMemory("memory_initial", memorySnapshot());

    mcpd3::MinCutGraph graph;
    const auto read_start = std::chrono::steady_clock::now();
    graph = config.directed ? mcpd3::read_dimacs_directed_streaming(
                                  config.dimacs_path)
                            : mcpd3::read_dimacs(config.dimacs_path);
    timing.read_graph_wall_us = elapsedUs(read_start);
    const auto after_read_memory = memorySnapshot();
    peak_rss_kb = std::max(peak_rss_kb, after_read_memory.rss_kb);
    printGraphStats(graph);
    printMemory("memory_after_read_graph", after_read_memory);
    if (shouldStopAfter(config, "read")) {
      timing.total_wall_us = elapsedUs(total_start);
      printStopped(timing, config, "read", peak_rss_kb);
      return EXIT_SUCCESS;
    }

    const auto scale_start = std::chrono::steady_clock::now();
    scaleGraph(&graph, config.objective_scale,
               config.saturate_capacity_overflow);
    timing.scale_graph_wall_us = elapsedUs(scale_start);
    const auto after_scale_memory = memorySnapshot();
    peak_rss_kb = std::max(peak_rss_kb, after_scale_memory.rss_kb);
    printMemory("memory_after_scale_graph", after_scale_memory);
    if (shouldStopAfter(config, "scale")) {
      timing.total_wall_us = elapsedUs(total_start);
      printStopped(timing, config, "scale", peak_rss_kb);
      return EXIT_SUCCESS;
    }

    const auto partition_start = std::chrono::steady_clock::now();
    auto packages =
        makePartitionPackages(config.partition_count, std::move(graph),
                              config.objective_scale);
    timing.partition_wall_us = elapsedUs(partition_start);
    const auto after_partition_memory = memorySnapshot();
    peak_rss_kb = std::max(peak_rss_kb, after_partition_memory.rss_kb);
    printPackageStats(packages);
    printMemory("memory_after_partition", after_partition_memory);
    if (shouldStopAfter(config, "partition")) {
      timing.total_wall_us = elapsedUs(total_start);
      printStopped(timing, config, "partition", peak_rss_kb);
      return EXIT_SUCCESS;
    }

    mcpd3::PartitionWorkerCoordinatorOptions solve_options;
    solve_options.max_iteration_count = config.max_iterations;
    solve_options.num_optimization_scales = config.schedule_levels;
    solve_options.initial_step_size = config.schedule_start;
    solve_options.objective_scale = config.objective_scale;
    solve_options.exhaust_scale_iterations = config.exhaust_scale_iterations;
    solve_options.exhaust_regularized_scale_iterations =
        config.exhaust_regularized_scale_iterations;
    solve_options.saturate_capacity_overflow =
        config.saturate_capacity_overflow;
    solve_options.progress_report_interval =
        config.progress_every > 0 ? config.progress_every : 0;
    if (config.progress_every > 0) {
      solve_options.progress_callback = printProgress;
    }

    auto workers = makeInProcessWorkers(config);
    const auto setup_start = std::chrono::steady_clock::now();
    mcpd3::PartitionWorkerCoordinator coordinator(std::move(packages),
                                                  std::move(workers),
                                                  solve_options);
    timing.coordinator_setup_wall_us = elapsedUs(setup_start);
    const auto after_setup_memory = memorySnapshot();
    peak_rss_kb = std::max(peak_rss_kb, after_setup_memory.rss_kb);
    printMemory("memory_after_coordinator_setup", after_setup_memory);
    if (shouldStopAfter(config, "setup")) {
      timing.total_wall_us = elapsedUs(total_start);
      printStopped(timing, config, "setup", peak_rss_kb);
      return EXIT_SUCCESS;
    }

    const auto solve_start = std::chrono::steady_clock::now();
    const auto result = coordinator.solve();
    timing.solve_wall_us = elapsedUs(solve_start);
    timing.total_wall_us = elapsedUs(total_start);
    const auto after_solve_memory = memorySnapshot();
    peak_rss_kb = std::max(peak_rss_kb, after_solve_memory.rss_kb);
    printMemory("memory_after_solve", after_solve_memory);
    printFinal(timing, result, config, peak_rss_kb);
  } catch (const std::exception &e) {
    std::cerr << "mcpd4_inprocess_benchmark failed: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
