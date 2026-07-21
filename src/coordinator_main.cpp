#include <mcpd4/discovery.h>
#include <mcpd4/runtime.h>
#include <mcpd4/solver_policy.h>
#include <mcpd4/status.h>

#include <decomp/dualdecomp.h>
#include <decomp/partition_coordinator.h>
#include <graph/dimacs.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <poll.h>
#include <sstream>
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

struct ObjectiveScaleStats {
  long arc_saturation_count = 0;
  long terminal_saturation_count = 0;
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

struct Config {
  std::string dimacs_path;
  std::string bind_host = "127.0.0.1";
  std::uint16_t port = 0;
  int worker_count = 1;
  int partition_count = 1;
  mcpd4::SolverPolicy solver_policy;
  long accept_timeout_ms = 24L * 60L * 60L * 1000L;
  int progress_every = 0;
  std::string ready_file;
  std::uint16_t discovery_port = 0;
  std::string discovery_token = mcpd4::kDefaultDiscoveryToken;
  std::string advertise_host;
  std::uint16_t status_port = 0;
  std::string status_token = mcpd4::kDefaultStatusToken;
  std::string status_file;
  std::string telemetry_csv_prefix;
  mcpd4::TransportCompression rpc_compression =
      mcpd4::TransportCompression::NONE;
  bool directed = false;
};

std::string statusValue(std::string value) {
  if (value.empty()) {
    return "-";
  }
  for (auto &ch : value) {
    if (std::isspace(static_cast<unsigned char>(ch)) || ch == ',' ||
        ch == ';') {
      ch = '_';
    }
  }
  return value;
}

std::string joinStatusValues(const std::vector<std::string> &values) {
  if (values.empty()) {
    return "-";
  }
  std::string joined;
  for (const auto &value : values) {
    if (!joined.empty()) {
      joined += ",";
    }
    joined += statusValue(value);
  }
  return joined;
}

std::string joinInts(const std::vector<int> &values) {
  if (values.empty()) {
    return "-";
  }
  std::string joined;
  for (const auto value : values) {
    if (!joined.empty()) {
      joined += ",";
    }
    joined += std::to_string(value);
  }
  return joined;
}

std::string boolString(bool value) { return value ? "1" : "0"; }

std::uint64_t saturatedSubtract(std::uint64_t lhs, std::uint64_t rhs) {
  return lhs > rhs ? lhs - rhs : 0;
}

void addRpcByteStats(mcpd4::RpcByteStats *total,
                     const mcpd4::RpcByteStats &stats) {
  total->tx_bytes_total += stats.tx_bytes_total;
  total->rx_bytes_total += stats.rx_bytes_total;
  total->tx_wire_bytes_total += stats.tx_wire_bytes_total;
  total->rx_wire_bytes_total += stats.rx_wire_bytes_total;
  total->compression_wall_us += stats.compression_wall_us;
  total->decompression_wall_us += stats.decompression_wall_us;
  total->tx_compressed_frame_count += stats.tx_compressed_frame_count;
  total->tx_stored_frame_count += stats.tx_stored_frame_count;
  total->rx_compressed_frame_count += stats.rx_compressed_frame_count;
  total->rx_stored_frame_count += stats.rx_stored_frame_count;
  total->hello_tx_bytes += stats.hello_tx_bytes;
  total->hello_rx_bytes += stats.hello_rx_bytes;
  total->partition_load_tx_bytes += stats.partition_load_tx_bytes;
  total->partition_load_rx_bytes += stats.partition_load_rx_bytes;
  total->partition_load_tx_frame_count +=
      stats.partition_load_tx_frame_count;
  total->partition_load_rx_frame_count +=
      stats.partition_load_rx_frame_count;
  total->solve_request_tx_bytes += stats.solve_request_tx_bytes;
  total->solve_request_rx_bytes += stats.solve_request_rx_bytes;
  total->solve_result_tx_bytes += stats.solve_result_tx_bytes;
  total->solve_result_rx_bytes += stats.solve_result_rx_bytes;
  total->scale_objective_tx_bytes += stats.scale_objective_tx_bytes;
  total->scale_objective_rx_bytes += stats.scale_objective_rx_bytes;
  total->capacity_update_tx_bytes += stats.capacity_update_tx_bytes;
  total->capacity_update_rx_bytes += stats.capacity_update_rx_bytes;
  total->capacity_update_tx_frame_count +=
      stats.capacity_update_tx_frame_count;
  total->capacity_update_rx_frame_count +=
      stats.capacity_update_rx_frame_count;
  total->full_labels_request_tx_bytes += stats.full_labels_request_tx_bytes;
  total->full_labels_request_rx_bytes += stats.full_labels_request_rx_bytes;
  total->full_labels_result_tx_bytes += stats.full_labels_result_tx_bytes;
  total->full_labels_result_rx_bytes += stats.full_labels_result_rx_bytes;
  total->full_labels_result_tx_frame_count +=
      stats.full_labels_result_tx_frame_count;
  total->full_labels_result_rx_frame_count +=
      stats.full_labels_result_rx_frame_count;
  total->ready_tx_bytes += stats.ready_tx_bytes;
  total->ready_rx_bytes += stats.ready_rx_bytes;
  total->stop_tx_bytes += stats.stop_tx_bytes;
  total->stop_rx_bytes += stats.stop_rx_bytes;
  total->error_tx_bytes += stats.error_tx_bytes;
  total->error_rx_bytes += stats.error_rx_bytes;
}

std::string csvValue(const std::string &value) {
  bool quote = value.empty();
  for (const char ch : value) {
    if (ch == ',' || ch == '"' || ch == '\n' || ch == '\r') {
      quote = true;
      break;
    }
  }
  if (!quote) {
    return value;
  }
  std::string escaped;
  escaped.reserve(value.size() + 2);
  escaped.push_back('"');
  for (const char ch : value) {
    escaped.push_back(ch);
    if (ch == '"') {
      escaped.push_back('"');
    }
  }
  escaped.push_back('"');
  return escaped;
}

std::string csvDouble(double value) {
  std::ostringstream out;
  out << std::setprecision(17) << value;
  return out.str();
}

class TelemetryRecorder {
public:
  void open(const Config &config) {
    if (config.telemetry_csv_prefix.empty()) {
      return;
    }
    prefix_ = config.telemetry_csv_prefix;
    openFile(&metadata_, ".metadata.csv");
    openFile(&partitions_, ".partitions.csv");
    openFile(&workers_, ".workers.csv");
    openFile(&iterations_, ".iterations.csv");
    openFile(&worker_iterations_, ".worker_iterations.csv");
    openFile(&worker_rpc_metrics_, ".worker_rpc_metrics.csv");
    openFile(&final_, ".final.csv");
    enabled_ = true;
    writeHeaders();
    const auto created_unix_seconds =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    insertMetadata("schema_version", "1",
                   "Telemetry CSV schema version for mcpd4 coordinator runs.");
    insertMetadata("created_unix_seconds",
                   std::to_string(created_unix_seconds),
                   "Unix timestamp when the telemetry recorder was opened.");
    insertMetadata("dimacs_path", config.dimacs_path,
                   "DIMACS input path read by the coordinator.");
    insertMetadata("directed", boolString(config.directed),
                   "1 when the directed DIMACS reader was used.");
    insertMetadata("worker_count", std::to_string(config.worker_count),
                   "Requested number of worker processes.");
    insertMetadata("partition_count", std::to_string(config.partition_count),
                   "Requested number of graph partitions.");
    insertMetadata("max_iterations", std::to_string(config.solver_policy.max_iteration_count),
                   "Maximum iterations per schedule level.");
    insertMetadata("schedule_levels", std::to_string(config.solver_policy.num_optimization_scales),
                   "Number of base-10 schedule levels.");
    insertMetadata("schedule_start", std::to_string(config.solver_policy.initial_step_size),
                   "Initial dual-decomposition schedule step.");
    insertMetadata("objective_scale", std::to_string(config.solver_policy.objective_scale),
                   "Initial exact objective/regularization quantum.");
    insertMetadata("rpc_compression",
                   mcpd4::transportCompressionName(config.rpc_compression),
                   "Transport compression mode requested for RPC frames.");
  }

  bool enabled() const { return enabled_; }

  void recordGraph(const mcpd3::MinCutGraph &graph,
                   const ObjectiveScaleStats &scale_stats) {
    if (!enabled_) {
      return;
    }
    insertMetadata("graph_node_count", std::to_string(graph.nnode),
                   "Node count reported by the DIMACS reader.");
    insertMetadata("graph_arc_count", std::to_string(graph.narc),
                   "Arc count reported by the DIMACS reader.");
    insertMetadata("objective_scale_arc_saturation_count",
                   std::to_string(scale_stats.arc_saturation_count),
                   "Number of arc capacities clipped by saturating scale mode.");
    insertMetadata(
        "objective_scale_terminal_saturation_count",
        std::to_string(scale_stats.terminal_saturation_count),
        "Number of terminal capacities clipped by saturating scale mode.");
  }

  void recordPackages(const std::vector<mcpd3::PartitionPackage> &packages) {
    if (!enabled_) {
      return;
    }
    for (const auto &package : packages) {
      partitions_ << package.partition_id << "," << package.local_node_count
                  << "," << package.arcs.size() << ","
                  << package.arcs.size() / 2 << ","
                  << package.terminal_capacities.size() << ","
                  << package.constraint_endpoints.size() << "\n";
    }
  }

  void beginSolve(const std::vector<mcpd4::TcpPartitionWorker *>
                      &remote_workers) {
    if (!enabled_) {
      return;
    }
    solve_started_at_ = std::chrono::steady_clock::now();
    previous_progress_at_ = solve_started_at_;
    previous_workers_.clear();
    for (const auto *worker : remote_workers) {
      const auto snapshot = worker->statusSnapshot();
      previous_workers_[snapshot.worker_name] = snapshot.timing;
      workers_ << csvValue(snapshot.worker_name) << ","
               << snapshot.cpu_count << "," << snapshot.ram_gb << ","
               << csvValue(worker->hello().temp_path) << ","
               << csvValue(joinInts(snapshot.partition_ids)) << ","
               << snapshot.partition_ids.size() << "\n";
    }
  }

  void recordProgress(const mcpd3::PartitionWorkerProgressRecord &record,
                      const std::vector<mcpd4::TcpPartitionWorker *>
                          &remote_workers) {
    if (!enabled_) {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto iteration_wall_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now - previous_progress_at_)
            .count());
    const auto solve_elapsed_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now - solve_started_at_)
            .count());
    previous_progress_at_ = now;

    iterations_ << record.total_iteration << "," << record.scale << ","
                << record.iteration << "," << record.max_iteration << ","
                << iteration_wall_us << "," << solve_elapsed_us << ","
                << integerString(record.lower_bound) << ","
                << integerString(record.best_lower_bound) << ","
                << integerString(record.certified_lower_bound) << ","
                << integerString(record.best_certified_lower_bound) << ","
                << integerString(record.regularized_objective) << ","
                << integerString(record.best_regularized_objective) << ","
                << record.disagreement_count << ","
                << csvDouble(record.disagreement_norm_sq) << ","
                << record.step_size << "," << record.effective_step_size
                << "," << integerString(record.regularization_strength) << ","
                << integerString(record.regularization_budget) << ","
                << integerString(record.regularization_contribution) << ","
                << record.regularization_anchor_sink_count << ","
                << record.regularization_active_sink_count << ","
                << record.iterations_since_improvement << "\n";

    for (const auto *worker : remote_workers) {
      recordWorkerProgress(record.total_iteration, *worker);
    }
  }

  void recordFinal(const RuntimeTiming &timing,
                   const mcpd3::PartitionWorkerCoordinatorSolveResult &result,
                   const std::vector<mcpd4::TcpPartitionWorker *>
                       &remote_workers) {
    if (!enabled_) {
      return;
    }
    std::uint64_t solve_rpc_us = 0;
    std::uint64_t worker_solve_us = 0;
    std::uint64_t load_rpc_us = 0;
    std::uint64_t scale_rpc_us = 0;
    mcpd4::RpcByteStats rpc_bytes;
    for (const auto *worker : remote_workers) {
      const auto &stats = worker->timingStats();
      load_rpc_us += stats.load_partition_rpc_wall_us;
      solve_rpc_us += stats.solve_round_rpc_wall_us;
      worker_solve_us += stats.solve_round_worker_wall_us;
      scale_rpc_us += stats.scale_objective_rpc_wall_us;
      addRpcByteStats(&rpc_bytes, stats.rpc_bytes);
    }
    insertFinal("status", std::to_string(static_cast<int>(result.status)),
                "Final coordinator status enum value.");
    insertFinal("stop_reason",
                std::to_string(static_cast<int>(result.stop_reason)),
                "Final coordinator stop reason enum value.");
    insertFinal("final_objective_raw",
                integerString(result.final_objective_raw),
                "Final unscaled objective in raw integer units.");
    insertFinal("final_certified_lower_bound_raw",
                integerString(result.final_certified_lower_bound_raw),
                "Final certified lower bound in raw integer units.");
    insertFinal("final_regularized_objective_raw",
                integerString(result.final_regularized_objective_raw),
                "Final regularized objective in raw integer units.");
    insertFinal("best_lower_bound_raw",
                integerString(result.best_lower_bound_raw),
                "Best raw lower bound observed.");
    insertFinal("best_certified_lower_bound_raw",
                integerString(result.best_certified_lower_bound_raw),
                "Best certified lower bound observed in raw integer units.");
    insertFinal("best_regularized_objective_raw",
                integerString(result.best_regularized_objective_raw),
                "Best regularized objective observed in raw integer units.");
    insertFinal("objective_scale", std::to_string(result.scale),
                "Final objective scale after any promotions.");
    insertFinal("objective_scale_promotions",
                std::to_string(result.objective_scale_promotion_count),
                "Number of objective scale promotions.");
    insertFinal("total_iterations", std::to_string(result.total_iterations),
                "Total optimizer iterations completed.");
    insertFinal("final_disagreement_count",
                std::to_string(result.final_disagreement_count),
                "Boundary disagreement count at termination.");
    insertFinal("final_regularization_budget",
                integerString(result.final_regularization_budget),
                "Final active regularization budget in raw integer units.");
    insertFinal("final_regularization_contribution",
                integerString(result.final_regularization_contribution),
                "Final regularization contribution in raw integer units.");
    insertFinal("final_regularization_anchor_sink_count",
                std::to_string(
                    result.final_regularization_anchor_sink_count),
                "Final count of sink-side regularization anchors.");
    insertFinal("final_regularization_active_sink_count",
                std::to_string(
                    result.final_regularization_active_sink_count),
                "Final count of active sink-side regularized labels.");
    insertFinal("timing_total_wall_us", std::to_string(timing.total_wall_us),
                "Total coordinator process wall time.");
    insertFinal("timing_read_graph_wall_us",
                std::to_string(timing.read_graph_wall_us),
                "Coordinator wall time spent reading the graph.");
    insertFinal("timing_scale_graph_wall_us",
                std::to_string(timing.scale_graph_wall_us),
                "Coordinator wall time spent applying the initial objective scale.");
    insertFinal("timing_partition_wall_us",
                std::to_string(timing.partition_wall_us),
                "Coordinator wall time spent partitioning the graph.");
    insertFinal("timing_accept_workers_wall_us",
                std::to_string(timing.accept_workers_wall_us),
                "Coordinator wall time spent accepting workers.");
    insertFinal("timing_coordinator_setup_wall_us",
                std::to_string(timing.coordinator_setup_wall_us),
                "Coordinator wall time spent constructing coordinator state.");
    insertFinal("timing_solve_wall_us", std::to_string(timing.solve_wall_us),
                "Coordinator wall time spent in the solve segment.");
    insertFinal("timing_coordinator_wait_worker_us",
                std::to_string(solve_rpc_us),
                "Aggregate coordinator wall time waiting on worker solve RPCs.");
    insertFinal("timing_worker_solve_us", std::to_string(worker_solve_us),
                "Aggregate worker-reported solve wall time.");
    insertFinal("timing_worker_rpc_overhead_us",
                std::to_string(saturatedSubtract(solve_rpc_us,
                                                 worker_solve_us)),
                "Aggregate solve RPC wall time not spent in worker solves.");
    insertFinal("timing_load_partition_rpc_us", std::to_string(load_rpc_us),
                "Aggregate partition load RPC wall time.");
    insertFinal("timing_scale_objective_rpc_us", std::to_string(scale_rpc_us),
                "Aggregate objective-scale promotion RPC wall time.");
    insertFinal("timing_stop_workers_wall_us",
                std::to_string(timing.stop_workers_wall_us),
                "Coordinator wall time spent sending stop messages to workers.");
    insertFinal("rpc_tx_bytes_total", std::to_string(rpc_bytes.tx_bytes_total),
                "Total logical bytes transmitted by coordinator RPC handles.");
    insertFinal("rpc_rx_bytes_total", std::to_string(rpc_bytes.rx_bytes_total),
                "Total logical bytes received by coordinator RPC handles.");
    insertFinal("rpc_tx_wire_bytes_total",
                std::to_string(rpc_bytes.tx_wire_bytes_total),
                "Total wire bytes transmitted after compression framing.");
    insertFinal("rpc_rx_wire_bytes_total",
                std::to_string(rpc_bytes.rx_wire_bytes_total),
                "Total wire bytes received after compression framing.");
    insertFinal("rpc_compression_wall_us",
                std::to_string(rpc_bytes.compression_wall_us),
                "Total compression wall time across coordinator RPC handles.");
    insertFinal("rpc_decompression_wall_us",
                std::to_string(rpc_bytes.decompression_wall_us),
                "Total decompression wall time across coordinator RPC handles.");
    insertFinal("rpc_partition_load_tx_bytes",
                std::to_string(rpc_bytes.partition_load_tx_bytes),
                "Logical partition package bytes transmitted.");
    insertFinal("rpc_partition_load_tx_frame_count",
                std::to_string(rpc_bytes.partition_load_tx_frame_count),
                "Partition package frames transmitted.");
    insertFinal("rpc_full_labels_request_tx_bytes",
                std::to_string(rpc_bytes.full_labels_request_tx_bytes),
                "Logical bounded final-label request bytes transmitted.");
    insertFinal("rpc_full_labels_result_rx_bytes",
                std::to_string(rpc_bytes.full_labels_result_rx_bytes),
                "Logical bounded final-label result bytes received.");
    insertFinal("rpc_full_labels_result_rx_frame_count",
                std::to_string(rpc_bytes.full_labels_result_rx_frame_count),
                "Final-label data and end frames received.");
    insertFinal("rpc_capacity_update_tx_bytes",
                std::to_string(rpc_bytes.capacity_update_tx_bytes),
                "Logical persistent capacity-refresh bytes transmitted.");
    insertFinal("rpc_capacity_update_tx_frame_count",
                std::to_string(rpc_bytes.capacity_update_tx_frame_count),
                "Persistent capacity-refresh frames transmitted.");
    insertFinal("rpc_solve_request_tx_bytes",
                std::to_string(rpc_bytes.solve_request_tx_bytes),
                "Logical solve request bytes transmitted.");
    insertFinal("rpc_solve_result_rx_bytes",
                std::to_string(rpc_bytes.solve_result_rx_bytes),
                "Logical solve result bytes received.");
    insertFinal("rpc_scale_objective_tx_bytes",
                std::to_string(rpc_bytes.scale_objective_tx_bytes),
                "Logical objective-scale promotion bytes transmitted.");
  }

  void finish() {
    if (!enabled_ || finished_) {
      return;
    }
    metadata_.close();
    partitions_.close();
    workers_.close();
    iterations_.close();
    worker_iterations_.close();
    worker_rpc_metrics_.close();
    final_.close();
    finished_ = true;
  }

  const std::string &prefix() const { return prefix_; }

private:
  void openFile(std::ofstream *file, const std::string &suffix) {
    const std::string path = prefix_ + suffix;
    file->open(path, std::ios::out | std::ios::trunc);
    if (!*file) {
      throw std::runtime_error("failed to open telemetry CSV file: " + path);
    }
  }

  void writeHeaders() {
    metadata_ << "key,value,description\n";
    partitions_ << "partition_id,local_node_count,arc_int_count,arc_count,"
                   "terminal_capacity_count,constraint_endpoint_count\n";
    workers_ << "worker_name,cpu_count,ram_gb,temp_path,"
                "assigned_partition_ids,assigned_partition_count\n";
    iterations_
        << "total_iteration,schedule_scale,scale_iteration,max_iteration,"
           "iteration_wall_us,solve_elapsed_us,lower_bound,best_lower_bound,"
           "certified_lower_bound,best_certified_lower_bound,"
           "regularized_objective,best_regularized_objective,"
           "disagreement_count,disagreement_norm_sq,schedule_step,"
           "effective_schedule_step,regularization_strength,"
           "regularization_budget,regularization_contribution,"
           "regularization_anchor_sink_count,"
           "regularization_active_sink_count,iterations_since_improvement\n";
    worker_iterations_
        << "total_iteration,worker_name,assigned_partition_ids,"
           "assigned_partition_count,partition_solve_call_count_delta,"
           "solve_batch_rpc_count_delta,solve_rpc_wall_us_delta,"
           "worker_solve_wall_us_delta,worker_rpc_overhead_us_delta,"
           "partition_solve_call_count_cumulative,"
           "solve_batch_rpc_count_cumulative,solve_rpc_wall_us_cumulative,"
           "worker_solve_wall_us_cumulative,load_partition_rpc_count,"
           "load_partition_rpc_wall_us,scale_objective_rpc_count,"
           "scale_objective_rpc_wall_us\n";
    worker_rpc_metrics_
        << "total_iteration,worker_name,metric,delta_value,"
           "cumulative_value\n";
    final_ << "key,value,description\n";
  }

  void insertMetadata(const std::string &key, const std::string &value,
                      const std::string &description) {
    metadata_ << csvValue(key) << "," << csvValue(value) << ","
              << csvValue(description) << "\n";
  }

  void insertFinal(const std::string &key, const std::string &value,
                   const std::string &description) {
    final_ << csvValue(key) << "," << csvValue(value) << ","
           << csvValue(description) << "\n";
  }

  void recordWorkerProgress(long total_iteration,
                            const mcpd4::TcpPartitionWorker &worker) {
    const auto snapshot = worker.statusSnapshot();
    const std::string &name = snapshot.worker_name;
    const auto previous_iter = previous_workers_.find(name);
    const auto previous =
        previous_iter == previous_workers_.end()
            ? mcpd4::TcpPartitionWorkerTimingStats{}
            : previous_iter->second;
    const auto &current = snapshot.timing;
    const auto solve_rpc_delta = saturatedSubtract(
        current.solve_round_rpc_wall_us, previous.solve_round_rpc_wall_us);
    const auto worker_solve_delta = saturatedSubtract(
        current.solve_round_worker_wall_us,
        previous.solve_round_worker_wall_us);
    worker_iterations_
        << total_iteration << "," << csvValue(name) << ","
        << csvValue(joinInts(snapshot.partition_ids)) << ","
        << snapshot.partition_ids.size() << ","
        << current.partition_solve_call_count -
               previous.partition_solve_call_count
        << ","
        << current.solve_batch_rpc_count - previous.solve_batch_rpc_count
        << "," << solve_rpc_delta << "," << worker_solve_delta << ","
        << saturatedSubtract(solve_rpc_delta, worker_solve_delta) << ","
        << current.partition_solve_call_count << ","
        << current.solve_batch_rpc_count << ","
        << current.solve_round_rpc_wall_us << ","
        << current.solve_round_worker_wall_us << ","
        << current.load_partition_rpc_count << ","
        << current.load_partition_rpc_wall_us << ","
        << current.scale_objective_rpc_count << ","
        << current.scale_objective_rpc_wall_us << "\n";
    recordRpcMetrics(total_iteration, name, current.rpc_bytes,
                     previous.rpc_bytes);
    previous_workers_[name] = current;
  }

  void recordRpcMetric(long total_iteration, const std::string &worker_name,
                       const std::string &metric, std::uint64_t current,
                       std::uint64_t previous) {
    worker_rpc_metrics_ << total_iteration << "," << csvValue(worker_name)
                        << "," << csvValue(metric) << ","
                        << saturatedSubtract(current, previous) << ","
                        << current << "\n";
  }

  void recordRpcMetrics(long total_iteration, const std::string &worker_name,
                        const mcpd4::RpcByteStats &current,
                        const mcpd4::RpcByteStats &previous) {
    recordRpcMetric(total_iteration, worker_name, "tx_bytes_total",
                    current.tx_bytes_total, previous.tx_bytes_total);
    recordRpcMetric(total_iteration, worker_name, "rx_bytes_total",
                    current.rx_bytes_total, previous.rx_bytes_total);
    recordRpcMetric(total_iteration, worker_name, "tx_wire_bytes_total",
                    current.tx_wire_bytes_total, previous.tx_wire_bytes_total);
    recordRpcMetric(total_iteration, worker_name, "rx_wire_bytes_total",
                    current.rx_wire_bytes_total, previous.rx_wire_bytes_total);
    recordRpcMetric(total_iteration, worker_name, "compression_wall_us",
                    current.compression_wall_us, previous.compression_wall_us);
    recordRpcMetric(total_iteration, worker_name, "decompression_wall_us",
                    current.decompression_wall_us,
                    previous.decompression_wall_us);
    recordRpcMetric(total_iteration, worker_name,
                    "tx_compressed_frame_count",
                    current.tx_compressed_frame_count,
                    previous.tx_compressed_frame_count);
    recordRpcMetric(total_iteration, worker_name, "tx_stored_frame_count",
                    current.tx_stored_frame_count,
                    previous.tx_stored_frame_count);
    recordRpcMetric(total_iteration, worker_name,
                    "rx_compressed_frame_count",
                    current.rx_compressed_frame_count,
                    previous.rx_compressed_frame_count);
    recordRpcMetric(total_iteration, worker_name, "rx_stored_frame_count",
                    current.rx_stored_frame_count,
                    previous.rx_stored_frame_count);
    recordRpcMetric(total_iteration, worker_name, "hello_tx_bytes",
                    current.hello_tx_bytes, previous.hello_tx_bytes);
    recordRpcMetric(total_iteration, worker_name, "hello_rx_bytes",
                    current.hello_rx_bytes, previous.hello_rx_bytes);
    recordRpcMetric(total_iteration, worker_name, "partition_load_tx_bytes",
                    current.partition_load_tx_bytes,
                    previous.partition_load_tx_bytes);
    recordRpcMetric(total_iteration, worker_name, "partition_load_rx_bytes",
                    current.partition_load_rx_bytes,
                    previous.partition_load_rx_bytes);
    recordRpcMetric(total_iteration, worker_name, "solve_request_tx_bytes",
                    current.solve_request_tx_bytes,
                    previous.solve_request_tx_bytes);
    recordRpcMetric(total_iteration, worker_name, "solve_request_rx_bytes",
                    current.solve_request_rx_bytes,
                    previous.solve_request_rx_bytes);
    recordRpcMetric(total_iteration, worker_name, "solve_result_tx_bytes",
                    current.solve_result_tx_bytes,
                    previous.solve_result_tx_bytes);
    recordRpcMetric(total_iteration, worker_name, "solve_result_rx_bytes",
                    current.solve_result_rx_bytes,
                    previous.solve_result_rx_bytes);
    recordRpcMetric(total_iteration, worker_name, "scale_objective_tx_bytes",
                    current.scale_objective_tx_bytes,
                    previous.scale_objective_tx_bytes);
    recordRpcMetric(total_iteration, worker_name, "scale_objective_rx_bytes",
                    current.scale_objective_rx_bytes,
                    previous.scale_objective_rx_bytes);
    recordRpcMetric(total_iteration, worker_name, "ready_tx_bytes",
                    current.ready_tx_bytes, previous.ready_tx_bytes);
    recordRpcMetric(total_iteration, worker_name, "ready_rx_bytes",
                    current.ready_rx_bytes, previous.ready_rx_bytes);
    recordRpcMetric(total_iteration, worker_name, "stop_tx_bytes",
                    current.stop_tx_bytes, previous.stop_tx_bytes);
    recordRpcMetric(total_iteration, worker_name, "stop_rx_bytes",
                    current.stop_rx_bytes, previous.stop_rx_bytes);
    recordRpcMetric(total_iteration, worker_name, "error_tx_bytes",
                    current.error_tx_bytes, previous.error_tx_bytes);
    recordRpcMetric(total_iteration, worker_name, "error_rx_bytes",
                    current.error_rx_bytes, previous.error_rx_bytes);
  }

  bool enabled_ = false;
  bool finished_ = false;
  std::string prefix_;
  std::ofstream metadata_;
  std::ofstream partitions_;
  std::ofstream workers_;
  std::ofstream iterations_;
  std::ofstream worker_iterations_;
  std::ofstream worker_rpc_metrics_;
  std::ofstream final_;
  std::chrono::steady_clock::time_point solve_started_at_;
  std::chrono::steady_clock::time_point previous_progress_at_;
  std::map<std::string, mcpd4::TcpPartitionWorkerTimingStats>
      previous_workers_;
};

struct CoordinatorStatusState {
  struct SegmentState {
    std::string name;
    bool done = false;
    std::chrono::steady_clock::time_point started_at;
    std::uint64_t elapsed_us = 0;
    bool has_progress = false;
    long progress_current = 0;
    long progress_total = 0;
    bool external_wait = false;
    std::string stats;
  };

  mutable std::mutex mutex;
  std::string phase = "starting";
  std::uint16_t tcp_port = 0;
  std::uint16_t discovery_port = 0;
  std::uint16_t status_port = 0;
  std::string rpc_compression = "none";
  int min_worker_count = 0;
  int worker_count = 0;
  int partition_count = 0;
  long objective_scale = 1;
  long initial_objective_scale = 1;
  long total_iteration = 0;
  long schedule_scale = 0;
  long schedule_step = 0;
  mcpd3::Objective lower_bound = 0;
  mcpd3::Objective best_lower_bound = 0;
  mcpd3::Objective certified_lower_bound = 0;
  mcpd3::Objective best_certified_lower_bound = 0;
  mcpd3::Objective regularized_objective = 0;
  mcpd3::Objective best_regularized_objective = 0;
  long disagreement_count = 0;
  mcpd3::Capacity regularization_strength = 0;
  mcpd3::Objective regularization_budget = 0;
  mcpd3::Objective regularization_contribution = 0;
  long regularization_anchor_sink_count = 0;
  long regularization_active_sink_count = 0;
  long assigned_partition_count = 0;
  long active_worker_count = 0;
  long partition_solve_call_count_total = 0;
  long solve_batch_rpc_count_total = 0;
  mcpd4::RpcByteStats rpc_bytes;
  std::vector<std::string> worker_names;
  std::vector<std::string> worker_resources;
  std::vector<std::string> partition_ownership;
  std::vector<std::string> worker_details;
  std::vector<SegmentState> segments;
  long solve_iteration_budget = 0;
  std::string last_error = "-";
  std::string status_file_path;

  void initialize(const Config &config) {
    std::lock_guard<std::mutex> lock(mutex);
    min_worker_count = config.worker_count;
    partition_count = config.partition_count;
    objective_scale = config.solver_policy.objective_scale;
    initial_objective_scale = config.solver_policy.objective_scale;
    rpc_compression =
        mcpd4::transportCompressionName(config.rpc_compression);
    solve_iteration_budget =
        static_cast<long>(config.solver_policy.max_iteration_count) *
        static_cast<long>(config.solver_policy.num_optimization_scales);
    persistLocked();
  }

  void setStatusFile(const std::string &path) {
    std::lock_guard<std::mutex> lock(mutex);
    status_file_path = path;
    persistLocked();
  }

  void setPhase(const std::string &value) {
    std::lock_guard<std::mutex> lock(mutex);
    phase = value;
    persistLocked();
  }

  void setPorts(std::uint16_t tcp, std::uint16_t discovery,
                std::uint16_t status) {
    std::lock_guard<std::mutex> lock(mutex);
    tcp_port = tcp;
    discovery_port = discovery;
    status_port = status;
    persistLocked();
  }

  void beginSegment(const std::string &name, const std::string &phase_value,
                    const std::string &stats = "",
                    long progress_current = 0, long progress_total = 0,
                    bool external_wait = false) {
    std::lock_guard<std::mutex> lock(mutex);
    phase = phase_value;
    auto &segment = segmentForNameLocked(name);
    segment.done = false;
    segment.started_at = std::chrono::steady_clock::now();
    segment.elapsed_us = 0;
    segment.has_progress = progress_total > 0;
    segment.progress_current = progress_current;
    segment.progress_total = progress_total;
    segment.external_wait = external_wait;
    segment.stats = statusValue(stats);
    persistLocked();
  }

  void updateSegmentProgress(const std::string &name, long progress_current,
                             long progress_total,
                             const std::string &stats = "",
                             bool external_wait = false) {
    std::lock_guard<std::mutex> lock(mutex);
    auto &segment = segmentForNameLocked(name);
    segment.has_progress = progress_total > 0;
    segment.progress_current = progress_current;
    segment.progress_total = progress_total;
    segment.external_wait = external_wait;
    if (!stats.empty()) {
      segment.stats = statusValue(stats);
    }
    persistLocked();
  }

  void finishSegment(const std::string &name, const std::string &stats = "") {
    std::lock_guard<std::mutex> lock(mutex);
    auto &segment = segmentForNameLocked(name);
    segment.done = true;
    segment.elapsed_us = elapsedUs(segment.started_at);
    segment.external_wait = false;
    if (!stats.empty()) {
      segment.stats = statusValue(stats);
    }
    persistLocked();
  }

  void recordWorker(const mcpd4::TcpPartitionWorker &worker) {
    const auto snapshot = worker.statusSnapshot();
    std::lock_guard<std::mutex> lock(mutex);
    const std::string name = statusValue(snapshot.worker_name);
    worker_names.push_back(snapshot.worker_name);
    worker_count = static_cast<int>(worker_names.size());
    worker_resources.push_back(
        name + ":cpu=" + std::to_string(snapshot.cpu_count) +
        ":ram_gb=" + std::to_string(snapshot.ram_gb));
    persistLocked();
  }

  void recordWorkerDetails(const std::vector<mcpd4::TcpPartitionWorker *>
                               &remote_workers) {
    std::lock_guard<std::mutex> lock(mutex);
    worker_resources.clear();
    partition_ownership.clear();
    worker_details.clear();
    assigned_partition_count = 0;
    active_worker_count = 0;
    rpc_bytes = {};
    for (const auto *worker : remote_workers) {
      const auto snapshot = worker->statusSnapshot();
      const std::string name = statusValue(snapshot.worker_name);
      worker_resources.push_back(
          name + ":cpu=" + std::to_string(snapshot.cpu_count) +
          ":ram_gb=" + std::to_string(snapshot.ram_gb));
      partition_ownership.push_back(name + "=" +
                                    joinInts(snapshot.partition_ids));
      assigned_partition_count +=
          snapshot.timing.load_partition_rpc_count;
      if (snapshot.timing.load_partition_rpc_count > 0) {
        ++active_worker_count;
      }
      worker_details.push_back(
          name + ":partitions=" +
          std::to_string(snapshot.timing.load_partition_rpc_count) +
          ":solves=" +
          std::to_string(snapshot.timing.partition_solve_call_count) +
          ":batches=" +
          std::to_string(snapshot.timing.solve_batch_rpc_count) +
          ":rpc_tx_bytes=" +
          std::to_string(snapshot.timing.rpc_bytes.tx_bytes_total) +
          ":rpc_rx_bytes=" +
          std::to_string(snapshot.timing.rpc_bytes.rx_bytes_total) +
          ":rpc_tx_wire_bytes=" +
          std::to_string(snapshot.timing.rpc_bytes.tx_wire_bytes_total) +
          ":rpc_rx_wire_bytes=" +
          std::to_string(snapshot.timing.rpc_bytes.rx_wire_bytes_total) +
          ":solve_rpc_us=" +
          std::to_string(snapshot.timing.solve_round_rpc_wall_us) +
          ":worker_solve_us=" +
          std::to_string(snapshot.timing.solve_round_worker_wall_us));
      addRpcByteStats(&rpc_bytes, snapshot.timing.rpc_bytes);
    }
    persistLocked();
  }

  void recordProgress(const mcpd3::PartitionWorkerProgressRecord &record,
                      const std::vector<mcpd4::TcpPartitionWorker *>
                          &remote_workers) {
    long assigned_partitions = 0;
    long active_workers = 0;
    long partition_solves = 0;
    long batch_rpcs = 0;
    mcpd4::RpcByteStats rpc_totals;
    for (const auto *worker : remote_workers) {
      const auto &stats = worker->timingStats();
      assigned_partitions += stats.load_partition_rpc_count;
      if (stats.load_partition_rpc_count > 0) {
        ++active_workers;
      }
      partition_solves += stats.partition_solve_call_count;
      batch_rpcs += stats.solve_batch_rpc_count;
      addRpcByteStats(&rpc_totals, stats.rpc_bytes);
    }
    std::lock_guard<std::mutex> lock(mutex);
    phase = "solving";
    total_iteration = record.total_iteration;
    schedule_scale = record.scale;
    schedule_step = record.step_size;
    lower_bound = record.lower_bound;
    best_lower_bound = record.best_lower_bound;
    certified_lower_bound = record.certified_lower_bound;
    best_certified_lower_bound = record.best_certified_lower_bound;
    regularized_objective = record.regularized_objective;
    best_regularized_objective = record.best_regularized_objective;
    disagreement_count = record.disagreement_count;
    regularization_strength = record.regularization_strength;
    regularization_budget = record.regularization_budget;
    regularization_contribution = record.regularization_contribution;
    regularization_anchor_sink_count =
        record.regularization_anchor_sink_count;
    regularization_active_sink_count = record.regularization_active_sink_count;
    assigned_partition_count = assigned_partitions;
    active_worker_count = active_workers;
    partition_solve_call_count_total = partition_solves;
    solve_batch_rpc_count_total = batch_rpcs;
    rpc_bytes = rpc_totals;
    updateSolveSegmentLocked(record);
    worker_details.clear();
    for (const auto *worker : remote_workers) {
      const auto snapshot = worker->statusSnapshot();
      const std::string name = statusValue(snapshot.worker_name);
      worker_details.push_back(
          name + ":partitions=" +
          std::to_string(snapshot.timing.load_partition_rpc_count) +
          ":solves=" +
          std::to_string(snapshot.timing.partition_solve_call_count) +
          ":batches=" +
          std::to_string(snapshot.timing.solve_batch_rpc_count) +
          ":rpc_tx_bytes=" +
          std::to_string(snapshot.timing.rpc_bytes.tx_bytes_total) +
          ":rpc_rx_bytes=" +
          std::to_string(snapshot.timing.rpc_bytes.rx_bytes_total) +
          ":rpc_tx_wire_bytes=" +
          std::to_string(snapshot.timing.rpc_bytes.tx_wire_bytes_total) +
          ":rpc_rx_wire_bytes=" +
          std::to_string(snapshot.timing.rpc_bytes.rx_wire_bytes_total) +
          ":solve_rpc_us=" +
          std::to_string(snapshot.timing.solve_round_rpc_wall_us) +
          ":worker_solve_us=" +
          std::to_string(snapshot.timing.solve_round_worker_wall_us));
    }
    persistLocked();
  }

  void recordFinal(
      const mcpd3::PartitionWorkerCoordinatorSolveResult &result) {
    std::lock_guard<std::mutex> lock(mutex);
    phase = "finished";
    objective_scale = result.scale;
    total_iteration = result.total_iterations;
    disagreement_count = result.final_disagreement_count;
    lower_bound = result.final_certified_lower_bound_raw;
    best_lower_bound = result.best_lower_bound_raw;
    certified_lower_bound = result.final_certified_lower_bound_raw;
    best_certified_lower_bound = result.best_certified_lower_bound_raw;
    regularized_objective = result.final_regularized_objective_raw;
    best_regularized_objective = result.best_regularized_objective_raw;
    regularization_budget = result.final_regularization_budget;
    regularization_contribution = result.final_regularization_contribution;
    regularization_anchor_sink_count =
        result.final_regularization_anchor_sink_count;
    regularization_active_sink_count =
        result.final_regularization_active_sink_count;
    finishSegmentLocked(
        "solve",
        "status=" + std::to_string(static_cast<int>(result.status)) +
            ":stop_reason=" +
            std::to_string(static_cast<int>(result.stop_reason)) +
            ":total_iterations=" +
            std::to_string(result.total_iterations) +
            ":final_disagreement_count=" +
            std::to_string(result.final_disagreement_count) +
            ":objective_scale=" + std::to_string(result.scale));
    persistLocked();
  }

  void recordFailure(const std::string &message) {
    std::lock_guard<std::mutex> lock(mutex);
    phase = "error";
    last_error = statusValue(message);
    persistLocked();
  }

  std::string snapshot() const {
    std::lock_guard<std::mutex> lock(mutex);
    return snapshotLocked();
  }

private:
  std::string snapshotLocked() const {
    std::ostringstream out;
    out << "role coordinator"
        << " phase " << phase
        << " tcp_port " << tcp_port
        << " discovery_port " << discovery_port
        << " status_port " << status_port
        << " worker_count " << worker_count
        << " min_worker_count " << min_worker_count
        << " partition_count " << partition_count
        << " objective_scale " << objective_scale
        << " initial_objective_scale " << initial_objective_scale
        << " rpc_compression " << rpc_compression
        << " total_iteration " << total_iteration
        << " schedule_scale " << schedule_scale
        << " schedule_step " << schedule_step
        << " lower_bound " << integerString(lower_bound)
        << " best_lower_bound " << integerString(best_lower_bound)
        << " certified_lower_bound " << integerString(certified_lower_bound)
        << " best_certified_lower_bound "
        << integerString(best_certified_lower_bound)
        << " regularized_objective " << integerString(regularized_objective)
        << " best_regularized_objective "
        << integerString(best_regularized_objective)
        << " disagreement_count " << disagreement_count
        << " regularization_strength " << integerString(regularization_strength)
        << " regularization_budget " << integerString(regularization_budget)
        << " regularization_contribution "
        << integerString(regularization_contribution)
        << " regularization_anchor_sink_count "
        << regularization_anchor_sink_count
        << " regularization_active_sink_count "
        << regularization_active_sink_count
        << " assigned_partition_count " << assigned_partition_count
        << " active_worker_count " << active_worker_count
        << " partition_solve_call_count_total "
        << partition_solve_call_count_total
        << " solve_batch_rpc_count_total " << solve_batch_rpc_count_total
        << " rpc_tx_bytes_total " << rpc_bytes.tx_bytes_total
        << " rpc_rx_bytes_total " << rpc_bytes.rx_bytes_total
        << " rpc_tx_wire_bytes_total " << rpc_bytes.tx_wire_bytes_total
        << " rpc_rx_wire_bytes_total " << rpc_bytes.rx_wire_bytes_total
        << " rpc_compression_wall_us " << rpc_bytes.compression_wall_us
        << " rpc_decompression_wall_us "
        << rpc_bytes.decompression_wall_us
        << " rpc_tx_compressed_frame_count "
        << rpc_bytes.tx_compressed_frame_count
        << " rpc_tx_stored_frame_count "
        << rpc_bytes.tx_stored_frame_count
        << " rpc_rx_compressed_frame_count "
        << rpc_bytes.rx_compressed_frame_count
        << " rpc_rx_stored_frame_count "
        << rpc_bytes.rx_stored_frame_count
        << " rpc_hello_rx_bytes " << rpc_bytes.hello_rx_bytes
        << " rpc_partition_load_tx_bytes "
        << rpc_bytes.partition_load_tx_bytes
        << " rpc_partition_load_tx_frame_count "
        << rpc_bytes.partition_load_tx_frame_count
        << " rpc_full_labels_request_tx_bytes "
        << rpc_bytes.full_labels_request_tx_bytes
        << " rpc_full_labels_result_rx_bytes "
        << rpc_bytes.full_labels_result_rx_bytes
        << " rpc_full_labels_result_rx_frame_count "
        << rpc_bytes.full_labels_result_rx_frame_count
        << " rpc_capacity_update_tx_bytes "
        << rpc_bytes.capacity_update_tx_bytes
        << " rpc_capacity_update_tx_frame_count "
        << rpc_bytes.capacity_update_tx_frame_count
        << " rpc_solve_request_tx_bytes "
        << rpc_bytes.solve_request_tx_bytes
        << " rpc_solve_result_rx_bytes "
        << rpc_bytes.solve_result_rx_bytes
        << " rpc_scale_objective_tx_bytes "
        << rpc_bytes.scale_objective_tx_bytes
        << " rpc_ready_rx_bytes " << rpc_bytes.ready_rx_bytes
        << " rpc_stop_tx_bytes " << rpc_bytes.stop_tx_bytes
        << " rpc_error_rx_bytes " << rpc_bytes.error_rx_bytes
        << " last_error " << statusValue(last_error)
        << " segments " << segmentSummaryLocked()
        << " worker_names " << joinStatusValues(worker_names)
        << " worker_resources " << joinStatusValues(worker_resources)
        << " partition_ownership " << joinStatusValues(partition_ownership)
        << " worker_details " << joinStatusValues(worker_details);
    return out.str();
  }

  void persistLocked() const {
    if (status_file_path.empty()) {
      return;
    }
    const std::string tmp_path = status_file_path + ".tmp";
    {
      std::ofstream out(tmp_path, std::ios::trunc);
      if (!out) {
        return;
      }
      out << snapshotLocked() << "\n";
      out.close();
      if (!out) {
        return;
      }
    }
    (void)std::rename(tmp_path.c_str(), status_file_path.c_str());
  }

  SegmentState &segmentForNameLocked(const std::string &name) {
    for (auto &segment : segments) {
      if (segment.name == name) {
        return segment;
      }
    }
    SegmentState segment;
    segment.name = name;
    segment.started_at = std::chrono::steady_clock::now();
    segments.push_back(std::move(segment));
    return segments.back();
  }

  void finishSegmentLocked(const std::string &name,
                           const std::string &stats = "") {
    auto &segment = segmentForNameLocked(name);
    segment.done = true;
    segment.elapsed_us = elapsedUs(segment.started_at);
    segment.external_wait = false;
    if (!stats.empty()) {
      segment.stats = statusValue(stats);
    }
  }

  void updateSolveSegmentLocked(
      const mcpd3::PartitionWorkerProgressRecord &record) {
    auto &segment = segmentForNameLocked("solve");
    segment.has_progress = solve_iteration_budget > 0;
    segment.progress_current = record.total_iteration;
    segment.progress_total = solve_iteration_budget;
    segment.stats =
        statusValue("total_iteration=" +
                    std::to_string(record.total_iteration) +
                    ":max_total_iterations=" +
                    std::to_string(solve_iteration_budget) +
                    ":scale_iteration=" +
                    std::to_string(record.iteration + 1) +
                    ":scale_max_iteration=" +
                    std::to_string(record.max_iteration) +
                    ":schedule_scale=" +
                    std::to_string(record.scale) +
                    ":schedule_step=" +
                    std::to_string(record.step_size) +
                    ":disagreement_count=" +
                    std::to_string(record.disagreement_count));
  }

  std::string etaRemainingUsLocked(const SegmentState &segment,
                                   std::uint64_t elapsed_us) const {
    if (segment.done) {
      return "0";
    }
    if (segment.external_wait) {
      return "unknown";
    }
    if (!segment.has_progress || segment.progress_total <= 0 ||
        segment.progress_current <= 0) {
      return "unknown";
    }
    if (segment.progress_current >= segment.progress_total) {
      return "0";
    }
    const long remaining_units =
        segment.progress_total - segment.progress_current;
    const long double elapsed = static_cast<long double>(elapsed_us);
    const long double estimate =
        elapsed * static_cast<long double>(remaining_units) /
        static_cast<long double>(segment.progress_current);
    if (estimate >=
        static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
      return "unknown";
    }
    return std::to_string(static_cast<std::uint64_t>(estimate));
  }

  std::string segmentSummaryLocked() const {
    if (segments.empty()) {
      return "-";
    }
    const auto now = std::chrono::steady_clock::now();
    std::string summary;
    for (const auto &segment : segments) {
      if (!summary.empty()) {
        summary += ",";
      }
      const auto segment_elapsed_us =
          segment.done
              ? segment.elapsed_us
              : static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        now - segment.started_at)
                        .count());
      summary += segment.name;
      summary += ":state=";
      summary += segment.done ? "done" : "running";
      summary += ":elapsed_us=" + std::to_string(segment_elapsed_us);
      if (!segment.done) {
        summary += ":eta_remaining_us=" +
                   etaRemainingUsLocked(segment, segment_elapsed_us);
      }
      if (segment.has_progress) {
        summary += ":progress_current=" +
                   std::to_string(segment.progress_current);
        summary += ":progress_total=" +
                   std::to_string(segment.progress_total);
      }
      if (!segment.stats.empty()) {
        summary += ":" + segment.stats;
      }
    }
    return summary;
  }
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
      << "       [--max-iterations N] [--schedule-levels N]\n"
      << "       [--schedule-start N] [--objective-scale N]\n"
      << "       [--max-total-iterations N] [--patience N]\n"
      << "       [--regularization none|scaled-epsilon|plateau-epsilon]\n"
      << "       [--regularization-cutoff N] [--regularization-cap N]\n"
      << "       [--disagreement-patience N] [--regularization-budget N]\n"
      << "       [--max-objective-scale-promotions N]\n"
      << "       [--no-promote-objective-scale-on-overbudget]\n"
      << "       [--no-momentum] [--group-stopping]\n"
      << "       [--retry-unit-step-without-momentum]\n"
      << "       [--random-alpha-radius N] [--random-alpha-seed N]\n"
      << "       [--halo-depth N|infinite]\n"
      << "       [--canonical-cut solver|min|max]\n"
      << "       [--force-full-mincut-recompute]\n"
      << "       [--accept-timeout-ms N]\n"
      << "       [--progress-every N] [--ready-file PATH]\n"
      << "       [--discovery-port PORT] [--discovery-token TOKEN]\n"
      << "       [--advertise-host HOST]\n"
      << "       [--status-port PORT] [--status-token TOKEN]\n"
      << "       [--status-file PATH]\n"
      << "       [--telemetry-csv-prefix PATH]\n"
      << "       [--rpc-compression none|snappy]\n"
      << "       [--exhaust-scale-iterations]\n"
      << "       [--no-exhaust-regularized-scale-iterations]\n"
      << "       [--no-retry-exhaust-regularized-scale-iterations]\n"
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
      config.solver_policy.max_iteration_count =
          parseInt(require_value(arg), arg);
    } else if (arg == "--max-total-iterations") {
      config.solver_policy.max_total_iteration_count =
          parseLong(require_value(arg), arg);
    } else if (arg == "--schedule-levels" || arg == "--num-scales") {
      config.solver_policy.num_optimization_scales =
          parseInt(require_value(arg), arg);
    } else if (arg == "--schedule-start" || arg == "--initial-step") {
      config.solver_policy.initial_step_size = parseLong(require_value(arg), arg);
    } else if (arg == "--objective-scale" || arg == "--capacity-multiplier") {
      config.solver_policy.objective_scale = parseLong(require_value(arg), arg);
    } else if (arg == "--patience") {
      config.solver_policy.patience = parseNonNegativeInt(require_value(arg), arg);
    } else if (arg == "--disagreement-patience") {
      config.solver_policy.disagreement_patience =
          parseNonNegativeInt(require_value(arg), arg);
    } else if (arg == "--regularization") {
      const auto value = require_value(arg);
      if (value == "none") {
        config.solver_policy.regularization_scheme =
            mcpd4::SolverRegularizationScheme::NONE;
      } else if (value == "scaled-epsilon") {
        config.solver_policy.regularization_scheme =
            mcpd4::SolverRegularizationScheme::SCALED_EPSILON;
      } else if (value == "plateau-epsilon") {
        config.solver_policy.regularization_scheme =
            mcpd4::SolverRegularizationScheme::
                DISAGREEMENT_PLATEAU_EPSILON;
      } else {
        throw std::runtime_error("invalid regularization scheme: " + value);
      }
    } else if (arg == "--regularization-cutoff") {
      config.solver_policy.scaled_epsilon_max_step_size =
          parseInt(require_value(arg), arg);
    } else if (arg == "--regularization-cap") {
      config.solver_policy.scaled_epsilon_strength_cap =
          parseNonNegativeInt(require_value(arg), arg);
    } else if (arg == "--regularization-budget") {
      config.solver_policy.regularization_budget_limit =
          parseLong(require_value(arg), arg);
    } else if (arg == "--max-objective-scale-promotions") {
      config.solver_policy.max_objective_scale_promotions =
          parseNonNegativeInt(require_value(arg), arg);
    } else if (arg == "--no-promote-objective-scale-on-overbudget") {
      config.solver_policy.promote_objective_scale_on_overbudget = false;
    } else if (arg == "--promote-objective-scale-on-overbudget") {
      config.solver_policy.promote_objective_scale_on_overbudget = true;
    } else if (arg == "--no-momentum") {
      config.solver_policy.use_momentum = false;
    } else if (arg == "--momentum") {
      config.solver_policy.use_momentum = true;
    } else if (arg == "--group-stopping") {
      config.solver_policy.enable_group_stopping = true;
    } else if (arg == "--no-group-stopping") {
      config.solver_policy.enable_group_stopping = false;
    } else if (arg == "--retry-unit-step-without-momentum") {
      config.solver_policy.retry_unit_step_without_momentum = true;
    } else if (arg == "--no-retry-unit-step-without-momentum") {
      config.solver_policy.retry_unit_step_without_momentum = false;
    } else if (arg == "--random-alpha-radius") {
      config.solver_policy.initial_alpha_random_radius =
          parseLong(require_value(arg), arg);
      config.solver_policy.randomize_initial_alphas =
          config.solver_policy.initial_alpha_random_radius > 0;
    } else if (arg == "--random-alpha-seed") {
      const int seed = parseNonNegativeInt(require_value(arg), arg);
      config.solver_policy.initial_alpha_random_seed =
          static_cast<unsigned int>(seed);
    } else if (arg == "--halo-depth") {
      const auto value = require_value(arg);
      config.solver_policy.halo_depth =
          value == "infinite" ? mcpd3::kInfiniteHaloDepth
                              : parseInt(value, arg);
    } else if (arg == "--canonical-cut") {
      const auto value = require_value(arg);
      if (value == "solver") {
        config.solver_policy.canonical_cut_selection =
            mcpd3::CanonicalCutSelection::SOLVER_DEFAULT;
      } else if (value == "min") {
        config.solver_policy.canonical_cut_selection =
            mcpd3::CanonicalCutSelection::MINIMUM_LABELS;
      } else if (value == "max") {
        config.solver_policy.canonical_cut_selection =
            mcpd3::CanonicalCutSelection::MAXIMUM_LABELS;
      } else {
        throw std::runtime_error("invalid canonical cut selection: " + value);
      }
    } else if (arg == "--force-full-mincut-recompute") {
      config.solver_policy.force_full_mincut_recompute = true;
    } else if (arg == "--accept-timeout-ms") {
      config.accept_timeout_ms = parseLong(require_value(arg), arg);
    } else if (arg == "--progress-every") {
      config.progress_every = parseNonNegativeInt(require_value(arg), arg);
    } else if (arg == "--ready-file") {
      config.ready_file = require_value(arg);
    } else if (arg == "--discovery-port") {
      config.discovery_port = parsePort(require_value(arg));
    } else if (arg == "--discovery-token") {
      config.discovery_token = require_value(arg);
    } else if (arg == "--advertise-host") {
      config.advertise_host = require_value(arg);
    } else if (arg == "--status-port") {
      config.status_port = parsePort(require_value(arg));
    } else if (arg == "--status-token") {
      config.status_token = require_value(arg);
    } else if (arg == "--status-file") {
      config.status_file = require_value(arg);
    } else if (arg == "--telemetry-csv-prefix") {
      config.telemetry_csv_prefix = require_value(arg);
    } else if (arg == "--rpc-compression") {
      config.rpc_compression =
          mcpd4::parseTransportCompression(require_value(arg));
    } else if (arg == "--exhaust-scale-iterations") {
      config.solver_policy.exhaust_scale_iterations = true;
    } else if (arg == "--exhaust-regularized-scale-iterations") {
      config.solver_policy.exhaust_regularized_scale_iterations = true;
    } else if (arg == "--no-exhaust-regularized-scale-iterations") {
      config.solver_policy.exhaust_regularized_scale_iterations = false;
    } else if (arg == "--retry-exhaust-regularized-scale-iterations") {
      config.solver_policy.retry_exhaust_regularized_scale_iterations = true;
    } else if (arg == "--no-retry-exhaust-regularized-scale-iterations") {
      config.solver_policy.retry_exhaust_regularized_scale_iterations = false;
    } else if (arg == "--saturate-capacity-overflow" ||
               arg == "--truncate-capacity-overflow") {
      config.solver_policy.saturate_capacity_overflow = true;
    } else if (arg == "--directed") {
      config.directed = true;
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  if (config.port == 0) {
    throw std::runtime_error("--port is required");
  }
  mcpd4::validateSolverPolicy(config.solver_policy);
  if (config.rpc_compression == mcpd4::TransportCompression::SNAPPY &&
      !mcpd4::snappyCompressionAvailable()) {
    throw std::runtime_error(
        "--rpc-compression snappy requested but this binary was built without "
        "Snappy support");
  }
  return config;
}

mcpd3::Capacity scaleCapacity(const mcpd3::Capacity &value, long factor,
                              bool saturate_overflow,
                              long *saturation_count) {
  try {
    return mcpd3::checked_scale_capacity(value, factor);
  } catch (const std::overflow_error &) {
    if (!saturate_overflow) {
      throw;
    }
    ++(*saturation_count);
    return mcpd3::checked_scale_capacity(value, factor, true);
  }
}

void scaleGraph(mcpd3::MinCutGraph *graph, long factor,
                bool saturate_capacity_overflow,
                ObjectiveScaleStats *stats) {
  if (factor == 1) {
    return;
  }
  for (auto &capacity : graph->arc_capacities) {
    capacity = scaleCapacity(capacity, factor, saturate_capacity_overflow,
                             &stats->arc_saturation_count);
  }
  for (auto &capacity : graph->terminal_capacities) {
    capacity =
        scaleCapacity(capacity, factor, saturate_capacity_overflow,
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

std::vector<std::unique_ptr<mcpd3::PartitionWorker>> acceptFixedWorkers(
    mcpd4::SocketHandle *listener, const Config &config,
    std::vector<mcpd4::TcpPartitionWorker *> *remote_workers,
    CoordinatorStatusState *status_state) {
  std::vector<std::unique_ptr<mcpd3::PartitionWorker>> workers;
  status_state->beginSegment(
      "accept_workers", "accepting_workers",
      "mode=fixed:accepted=0:target=" + std::to_string(config.worker_count),
      0, config.worker_count);
  for (int i = 0; i < config.worker_count; ++i) {
    auto worker = mcpd4::acceptTcpPartitionWorker(
        listener, std::chrono::milliseconds(config.accept_timeout_ms),
        config.rpc_compression);
    std::cout << "accepted worker " << worker->hello().worker_name << "\n";
    std::cout.flush();
    status_state->recordWorker(*worker);
    remote_workers->push_back(worker.get());
    workers.push_back(std::move(worker));
    status_state->updateSegmentProgress(
        "accept_workers", static_cast<long>(workers.size()),
        config.worker_count,
        "mode=fixed:accepted=" + std::to_string(workers.size()) +
            ":target=" + std::to_string(config.worker_count));
  }
  return workers;
}

std::vector<std::unique_ptr<mcpd3::PartitionWorker>> acceptDiscoveredWorkers(
    mcpd4::SocketHandle *listener, const mcpd4::SocketHandle &discovery_socket,
    const Config &config, std::uint16_t tcp_port,
    std::vector<mcpd4::TcpPartitionWorker *> *remote_workers,
    CoordinatorStatusState *status_state) {
  std::vector<std::unique_ptr<mcpd3::PartitionWorker>> workers;
  bool close_requested = false;
  status_state->beginSegment(
      "accept_workers", "discovery_waiting",
      "mode=discovery:accepted=0:target=" +
          std::to_string(config.worker_count) + ":close_requested=0",
      0, config.worker_count, true);
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(config.accept_timeout_ms);

  auto discovery_info = [&]() {
    mcpd4::DiscoveryCoordinatorInfo info;
    info.host = config.advertise_host;
    info.tcp_port = tcp_port;
    info.discovery_port = mcpd4::localPort(discovery_socket);
    info.worker_count = static_cast<int>(workers.size());
    info.min_worker_count = config.worker_count;
    info.closed = close_requested;
    return info;
  };

  while (!close_requested ||
         static_cast<int>(workers.size()) < config.worker_count) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      throw std::runtime_error(
          "timed out waiting for discovery close or worker connections");
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - now);
    const int poll_timeout_ms =
        std::max(1, std::min<int>(250, static_cast<int>(remaining.count())));

    pollfd fds[2]{};
    fds[0].fd = listener->get();
    fds[0].events = POLLIN;
    fds[1].fd = discovery_socket.get();
    fds[1].events = POLLIN;
    const int rc = ::poll(fds, 2, poll_timeout_ms);
    if (rc < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error("poll failed during discovery accept");
    }
    if (rc == 0) {
      continue;
    }

    if ((fds[1].revents & POLLIN) != 0) {
      const auto request =
          mcpd4::receiveDiscoveryRequest(discovery_socket);
      if (request.token != config.discovery_token) {
        continue;
      }
      if (request.type == mcpd4::DiscoveryRequestType::QUERY) {
        mcpd4::sendDiscoveryCoordinatorResponse(discovery_socket, request,
                                                discovery_info());
      } else if (request.type == mcpd4::DiscoveryRequestType::CLOSE) {
        close_requested = true;
        status_state->updateSegmentProgress(
            "accept_workers", static_cast<long>(workers.size()),
            config.worker_count,
            "mode=discovery:accepted=" + std::to_string(workers.size()) +
                ":target=" + std::to_string(config.worker_count) +
                ":close_requested=1",
            false);
        mcpd4::DiscoveryCloseResult result;
        result.accepted = true;
        result.worker_count = static_cast<int>(workers.size());
        result.min_worker_count = config.worker_count;
        mcpd4::sendDiscoveryCloseAck(discovery_socket, request, result);
      }
    }

    if ((fds[0].revents & POLLIN) != 0) {
      auto worker = mcpd4::acceptTcpPartitionWorker(
          listener, std::chrono::milliseconds(1),
          config.rpc_compression);
      std::cout << "accepted worker " << worker->hello().worker_name << "\n";
      std::cout.flush();
      status_state->recordWorker(*worker);
      remote_workers->push_back(worker.get());
      workers.push_back(std::move(worker));
      status_state->updateSegmentProgress(
          "accept_workers", static_cast<long>(workers.size()),
          config.worker_count,
          "mode=discovery:accepted=" + std::to_string(workers.size()) +
              ":target=" + std::to_string(config.worker_count) +
              ":close_requested=" + boolString(close_requested),
          !close_requested);
    }
  }

  status_state->setPhase("discovery_closed");
  std::cout << "discovery_closed worker_count " << workers.size()
            << " min_worker_count " << config.worker_count << "\n";
  std::cout.flush();
  return workers;
}

std::vector<mcpd3::PartitionPackage> makePartitionPackages(
    int partition_count, mcpd3::MinCutGraph graph,
    const mcpd4::SolverPolicy &solver_policy) {
  auto package_options = mcpd4::makePackageOptions(solver_policy);
  mcpd3::DualDecomposition package_source(
      partition_count, graph.nnode, graph.narc, std::move(graph.arcs),
      std::move(graph.arc_capacities), std::move(graph.terminal_capacities),
      package_options);
  return package_source.takePartitionPackages();
}

struct SolveCounterStats {
  long assigned_partition_count = 0;
  long active_worker_count = 0;
  long partition_solve_call_count_total = 0;
  long solve_batch_rpc_count_total = 0;
  long scale_objective_rpc_count = 0;
};

SolveCounterStats gatherSolveCounters(
    const std::vector<mcpd4::TcpPartitionWorker *>
        &remote_workers) {
  SolveCounterStats counters;
  for (const auto *worker : remote_workers) {
    const auto &stats = worker->timingStats();
    counters.assigned_partition_count += stats.load_partition_rpc_count;
    if (stats.load_partition_rpc_count > 0) {
      ++counters.active_worker_count;
    }
    counters.partition_solve_call_count_total +=
        stats.partition_solve_call_count;
    counters.solve_batch_rpc_count_total += stats.solve_batch_rpc_count;
    counters.scale_objective_rpc_count += stats.scale_objective_rpc_count;
  }
  return counters;
}

mcpd4::RpcByteStats gatherRpcByteStats(
    const std::vector<mcpd4::TcpPartitionWorker *>
        &remote_workers) {
  mcpd4::RpcByteStats totals;
  for (const auto *worker : remote_workers) {
    addRpcByteStats(&totals, worker->timingStats().rpc_bytes);
  }
  return totals;
}

void printTiming(const RuntimeTiming &timing,
                 const std::vector<mcpd4::TcpPartitionWorker *>
                     &remote_workers) {
  std::uint64_t load_rpc_us = 0;
  std::uint64_t solve_rpc_us = 0;
  std::uint64_t worker_solve_us = 0;
  std::uint64_t scale_rpc_us = 0;
  for (const auto *worker : remote_workers) {
    const auto &stats = worker->timingStats();
    load_rpc_us += stats.load_partition_rpc_wall_us;
    solve_rpc_us += stats.solve_round_rpc_wall_us;
    worker_solve_us += stats.solve_round_worker_wall_us;
    scale_rpc_us += stats.scale_objective_rpc_wall_us;
  }
  const auto counters = gatherSolveCounters(remote_workers);
  const auto rpc_bytes = gatherRpcByteStats(remote_workers);

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
  std::cout << "assigned_partition_count "
            << counters.assigned_partition_count << "\n";
  std::cout << "active_worker_count " << counters.active_worker_count << "\n";
  std::cout << "partition_solves_per_iteration "
            << counters.assigned_partition_count << "\n";
  std::cout << "solve_batch_rpcs_per_iteration "
            << counters.active_worker_count << "\n";
  std::cout << "load_partition_rpc_count "
            << counters.assigned_partition_count << "\n";
  std::cout << "partition_solve_call_count_total "
            << counters.partition_solve_call_count_total << "\n";
  std::cout << "solve_batch_rpc_count_total "
            << counters.solve_batch_rpc_count_total << "\n";
  std::cout << "scale_objective_rpc_count "
            << counters.scale_objective_rpc_count << "\n";
  std::cout << "rpc_tx_bytes_total " << rpc_bytes.tx_bytes_total << "\n";
  std::cout << "rpc_rx_bytes_total " << rpc_bytes.rx_bytes_total << "\n";
  std::cout << "rpc_tx_wire_bytes_total "
            << rpc_bytes.tx_wire_bytes_total << "\n";
  std::cout << "rpc_rx_wire_bytes_total "
            << rpc_bytes.rx_wire_bytes_total << "\n";
  std::cout << "rpc_compression_wall_us "
            << rpc_bytes.compression_wall_us << "\n";
  std::cout << "rpc_decompression_wall_us "
            << rpc_bytes.decompression_wall_us << "\n";
  std::cout << "rpc_tx_compressed_frame_count "
            << rpc_bytes.tx_compressed_frame_count << "\n";
  std::cout << "rpc_tx_stored_frame_count "
            << rpc_bytes.tx_stored_frame_count << "\n";
  std::cout << "rpc_rx_compressed_frame_count "
            << rpc_bytes.rx_compressed_frame_count << "\n";
  std::cout << "rpc_rx_stored_frame_count "
            << rpc_bytes.rx_stored_frame_count << "\n";
  std::cout << "rpc_hello_rx_bytes " << rpc_bytes.hello_rx_bytes << "\n";
  std::cout << "rpc_partition_load_tx_bytes "
            << rpc_bytes.partition_load_tx_bytes << "\n";
  std::cout << "rpc_partition_load_tx_frame_count "
            << rpc_bytes.partition_load_tx_frame_count << "\n";
  std::cout << "rpc_full_labels_request_tx_bytes "
            << rpc_bytes.full_labels_request_tx_bytes << "\n";
  std::cout << "rpc_full_labels_result_rx_bytes "
            << rpc_bytes.full_labels_result_rx_bytes << "\n";
  std::cout << "rpc_full_labels_result_rx_frame_count "
            << rpc_bytes.full_labels_result_rx_frame_count << "\n";
  std::cout << "rpc_capacity_update_tx_bytes "
            << rpc_bytes.capacity_update_tx_bytes << "\n";
  std::cout << "rpc_capacity_update_tx_frame_count "
            << rpc_bytes.capacity_update_tx_frame_count << "\n";
  std::cout << "rpc_solve_request_tx_bytes "
            << rpc_bytes.solve_request_tx_bytes << "\n";
  std::cout << "rpc_solve_result_rx_bytes "
            << rpc_bytes.solve_result_rx_bytes << "\n";
  std::cout << "rpc_scale_objective_tx_bytes "
            << rpc_bytes.scale_objective_tx_bytes << "\n";
  std::cout << "rpc_ready_rx_bytes " << rpc_bytes.ready_rx_bytes << "\n";
  std::cout << "rpc_stop_tx_bytes " << rpc_bytes.stop_tx_bytes << "\n";
  std::cout << "rpc_error_rx_bytes " << rpc_bytes.error_rx_bytes << "\n";
}

void printObjectiveScaleStats(const Config &config,
                              const ObjectiveScaleStats &stats) {
  const long total_saturation_count =
      stats.arc_saturation_count + stats.terminal_saturation_count;
  std::cout << "initial_objective_scale " << config.solver_policy.objective_scale
            << "\n";
  std::cout << "rpc_compression "
            << mcpd4::transportCompressionName(config.rpc_compression)
            << "\n";
  std::cout << "objective_scale_overflow_mode "
            << (config.solver_policy.saturate_capacity_overflow ? "saturate" : "strict")
            << "\n";
  std::cout << "objective_scale_saturation_count "
            << total_saturation_count << "\n";
  std::cout << "objective_scale_arc_saturation_count "
            << stats.arc_saturation_count << "\n";
  std::cout << "objective_scale_terminal_saturation_count "
            << stats.terminal_saturation_count << "\n";
}

void printProgress(
    const mcpd3::PartitionWorkerProgressRecord &record,
    const std::vector<mcpd4::TcpPartitionWorker *>
        &remote_workers) {
  std::uint64_t solve_rpc_us = 0;
  std::uint64_t worker_solve_us = 0;
  for (const auto *worker : remote_workers) {
    const auto &stats = worker->timingStats();
    solve_rpc_us += stats.solve_round_rpc_wall_us;
    worker_solve_us += stats.solve_round_worker_wall_us;
  }
  const auto counters = gatherSolveCounters(remote_workers);
  const auto rpc_bytes = gatherRpcByteStats(remote_workers);
  const auto worker_rpc_overhead_us =
      saturatedSubtract(solve_rpc_us, worker_solve_us);

  std::cout << "progress"
            << " total_iteration " << record.total_iteration
            << " schedule_scale " << record.scale
            << " iteration " << record.iteration
            << " lower_bound " << integerString(record.lower_bound)
            << " best_lower_bound " << integerString(record.best_lower_bound)
            << " certified_lower_bound "
            << integerString(record.certified_lower_bound)
            << " best_certified_lower_bound "
            << integerString(record.best_certified_lower_bound)
            << " regularized_objective "
            << integerString(record.regularized_objective)
            << " best_regularized_objective "
            << integerString(record.best_regularized_objective)
            << " disagreement_count " << record.disagreement_count
            << " disagreement_norm_sq " << record.disagreement_norm_sq
            << " schedule_step " << record.step_size
            << " effective_schedule_step " << record.effective_step_size
            << " regularization_strength "
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
            << record.iterations_since_improvement
            << " assigned_partition_count "
            << counters.assigned_partition_count
            << " active_worker_count " << counters.active_worker_count
            << " partition_solves_per_iteration "
            << counters.assigned_partition_count
            << " solve_batch_rpcs_per_iteration "
            << counters.active_worker_count
            << " partition_solve_call_count_total "
            << counters.partition_solve_call_count_total
            << " solve_batch_rpc_count_total "
            << counters.solve_batch_rpc_count_total
            << " solve_rpc_wall_us " << solve_rpc_us
            << " worker_solve_wall_us " << worker_solve_us
            << " worker_rpc_overhead_us " << worker_rpc_overhead_us
            << " rpc_tx_bytes_total " << rpc_bytes.tx_bytes_total
            << " rpc_rx_bytes_total " << rpc_bytes.rx_bytes_total
            << " rpc_tx_wire_bytes_total "
            << rpc_bytes.tx_wire_bytes_total
            << " rpc_rx_wire_bytes_total "
            << rpc_bytes.rx_wire_bytes_total
            << " rpc_compression_wall_us "
            << rpc_bytes.compression_wall_us
            << " rpc_decompression_wall_us "
            << rpc_bytes.decompression_wall_us
            << " rpc_tx_compressed_frame_count "
            << rpc_bytes.tx_compressed_frame_count
            << " rpc_tx_stored_frame_count "
            << rpc_bytes.tx_stored_frame_count
            << " rpc_rx_compressed_frame_count "
            << rpc_bytes.rx_compressed_frame_count
            << " rpc_rx_stored_frame_count "
            << rpc_bytes.rx_stored_frame_count
            << " rpc_partition_load_tx_bytes "
            << rpc_bytes.partition_load_tx_bytes
            << " rpc_partition_load_tx_frame_count "
            << rpc_bytes.partition_load_tx_frame_count
            << " rpc_full_labels_request_tx_bytes "
            << rpc_bytes.full_labels_request_tx_bytes
            << " rpc_full_labels_result_rx_bytes "
            << rpc_bytes.full_labels_result_rx_bytes
            << " rpc_full_labels_result_rx_frame_count "
            << rpc_bytes.full_labels_result_rx_frame_count
            << " rpc_capacity_update_tx_bytes "
            << rpc_bytes.capacity_update_tx_bytes
            << " rpc_capacity_update_tx_frame_count "
            << rpc_bytes.capacity_update_tx_frame_count
            << " rpc_solve_request_tx_bytes "
            << rpc_bytes.solve_request_tx_bytes
            << " rpc_solve_result_rx_bytes "
            << rpc_bytes.solve_result_rx_bytes
            << " rpc_scale_objective_tx_bytes "
            << rpc_bytes.scale_objective_tx_bytes
            << " rpc_ready_rx_bytes " << rpc_bytes.ready_rx_bytes
            << "\n";

  for (const auto *worker : remote_workers) {
    const auto &stats = worker->timingStats();
    std::cout << "progress_worker"
              << " total_iteration " << record.total_iteration
              << " name " << worker->hello().worker_name
              << " assigned_partition_count "
              << stats.load_partition_rpc_count
              << " partition_solve_call_count_total "
              << stats.partition_solve_call_count
              << " solve_batch_rpc_count_total "
              << stats.solve_batch_rpc_count
              << " solve_rpc_wall_us " << stats.solve_round_rpc_wall_us
              << " worker_solve_wall_us "
              << stats.solve_round_worker_wall_us
              << " worker_rpc_overhead_us "
              << saturatedSubtract(stats.solve_round_rpc_wall_us,
                                   stats.solve_round_worker_wall_us)
              << " rpc_tx_bytes_total "
              << stats.rpc_bytes.tx_bytes_total
              << " rpc_rx_bytes_total "
              << stats.rpc_bytes.rx_bytes_total
              << " rpc_tx_wire_bytes_total "
              << stats.rpc_bytes.tx_wire_bytes_total
              << " rpc_rx_wire_bytes_total "
              << stats.rpc_bytes.rx_wire_bytes_total
              << " rpc_compression_wall_us "
              << stats.rpc_bytes.compression_wall_us
              << " rpc_decompression_wall_us "
              << stats.rpc_bytes.decompression_wall_us
              << " rpc_tx_compressed_frame_count "
              << stats.rpc_bytes.tx_compressed_frame_count
              << " rpc_rx_compressed_frame_count "
              << stats.rpc_bytes.rx_compressed_frame_count
              << " rpc_partition_load_tx_bytes "
              << stats.rpc_bytes.partition_load_tx_bytes
              << " rpc_partition_load_tx_frame_count "
              << stats.rpc_bytes.partition_load_tx_frame_count
              << " rpc_full_labels_request_tx_bytes "
              << stats.rpc_bytes.full_labels_request_tx_bytes
              << " rpc_full_labels_result_rx_bytes "
              << stats.rpc_bytes.full_labels_result_rx_bytes
              << " rpc_full_labels_result_rx_frame_count "
              << stats.rpc_bytes.full_labels_result_rx_frame_count
              << " rpc_capacity_update_tx_bytes "
              << stats.rpc_bytes.capacity_update_tx_bytes
              << " rpc_capacity_update_tx_frame_count "
              << stats.rpc_bytes.capacity_update_tx_frame_count
              << " rpc_solve_request_tx_bytes "
              << stats.rpc_bytes.solve_request_tx_bytes
              << " rpc_solve_result_rx_bytes "
              << stats.rpc_bytes.solve_result_rx_bytes
              << "\n";
  }
  std::cout.flush();
}

} // namespace

int main(int argc, char **argv) {
  CoordinatorStatusState status_state;
  bool parsed_args = false;
  try {
    const auto total_start = std::chrono::steady_clock::now();
    RuntimeTiming timing;
    const Config config = parseArgs(argc, argv);
    parsed_args = true;
    TelemetryRecorder telemetry;
    telemetry.open(config);
    status_state.initialize(config);
    status_state.setStatusFile(config.status_file);
    std::unique_ptr<mcpd4::StatusServer> status_server;
    if (config.status_port != 0) {
      status_server = std::make_unique<mcpd4::StatusServer>(
          config.bind_host, config.status_port, config.status_token,
          [&status_state] { return status_state.snapshot(); });
      status_state.setPorts(/*tcp=*/0, /*discovery=*/0,
                            status_server->port());
      std::cout << "status_listening port " << status_server->port() << "\n";
      std::cout.flush();
    }

    auto graph_start = std::chrono::steady_clock::now();
    status_state.beginSegment(
        "read_graph", "reading_graph",
        "directed=" + boolString(config.directed));
    auto graph = config.directed
                     ? mcpd3::read_dimacs_directed_streaming(config.dimacs_path)
                     : mcpd3::read_dimacs(config.dimacs_path);
    timing.read_graph_wall_us = elapsedUs(graph_start);
    status_state.finishSegment(
        "read_graph",
        "directed=" + boolString(config.directed) +
            ":nodes=" + std::to_string(graph.nnode) +
            ":arcs=" + std::to_string(graph.narc));

    ObjectiveScaleStats objective_scale_stats;
    const auto scale_graph_start = std::chrono::steady_clock::now();
    status_state.beginSegment(
        "scale_graph", "scaling_graph",
        "objective_scale=" + std::to_string(config.solver_policy.objective_scale));
    scaleGraph(&graph, config.solver_policy.objective_scale,
               config.solver_policy.saturate_capacity_overflow,
               &objective_scale_stats);
    timing.scale_graph_wall_us = elapsedUs(scale_graph_start);
    status_state.finishSegment(
        "scale_graph",
        "objective_scale=" + std::to_string(config.solver_policy.objective_scale) +
            ":arc_saturations=" +
            std::to_string(objective_scale_stats.arc_saturation_count) +
            ":terminal_saturations=" +
            std::to_string(objective_scale_stats.terminal_saturation_count));
    telemetry.recordGraph(graph, objective_scale_stats);
    if (objective_scale_stats.arc_saturation_count +
            objective_scale_stats.terminal_saturation_count >
        0) {
      std::cerr
          << "warning: saturated "
          << objective_scale_stats.arc_saturation_count +
                 objective_scale_stats.terminal_saturation_count
          << " capacities while scaling; results use clipped configured "
             "capacities\n";
    }

    const auto partition_start = std::chrono::steady_clock::now();
    status_state.beginSegment(
        "partitioning", "partitioning",
        "requested_partitions=" + std::to_string(config.partition_count));
    auto packages = makePartitionPackages(
        config.partition_count, std::move(graph), config.solver_policy);
    telemetry.recordPackages(packages);
    timing.partition_wall_us = elapsedUs(partition_start);
    long package_boundary_count = 0;
    long package_node_count = 0;
    long package_arc_count = 0;
    for (const auto &package : packages) {
      package_boundary_count +=
          static_cast<long>(package.constraint_endpoints.size());
      package_node_count += package.local_node_count;
      package_arc_count += static_cast<long>(package.arcs.size());
    }
    status_state.finishSegment(
        "partitioning",
        "requested_partitions=" + std::to_string(config.partition_count) +
            ":packages=" + std::to_string(packages.size()) +
            ":package_nodes=" + std::to_string(package_node_count) +
            ":package_arcs=" + std::to_string(package_arc_count) +
            ":boundary_endpoints=" +
            std::to_string(package_boundary_count));

    status_state.beginSegment("transport_setup", "transport_setup");
    auto listener =
        mcpd4::listenTcp(config.bind_host, config.port);
    std::cout << "listening " << config.bind_host << ":"
              << mcpd4::localPort(listener) << "\n";
    std::cout.flush();
    mcpd4::SocketHandle discovery_socket;
    if (config.discovery_port != 0) {
      discovery_socket =
          mcpd4::bindDiscoveryUdp(config.bind_host, config.discovery_port);
      std::cout << "discovery_listening port "
                << mcpd4::localPort(discovery_socket)
                << " min_worker_count " << config.worker_count << "\n";
      std::cout.flush();
    }
    status_state.setPorts(mcpd4::localPort(listener),
                          discovery_socket.valid()
                              ? mcpd4::localPort(discovery_socket)
                              : 0,
                          status_server ? status_server->port() : 0);
    status_state.finishSegment(
        "transport_setup",
        "tcp_port=" + std::to_string(mcpd4::localPort(listener)) +
            ":discovery_port=" +
            std::to_string(discovery_socket.valid()
                               ? mcpd4::localPort(discovery_socket)
                               : 0) +
            ":status_port=" +
            std::to_string(status_server ? status_server->port() : 0));
    writeReadyFile(config.ready_file, mcpd4::localPort(listener));

    std::vector<std::unique_ptr<mcpd3::PartitionWorker>> workers;
    std::vector<mcpd4::TcpPartitionWorker *> remote_workers;
    const auto accept_start = std::chrono::steady_clock::now();
    if (config.discovery_port == 0) {
      workers =
          acceptFixedWorkers(&listener, config, &remote_workers, &status_state);
    } else {
      workers = acceptDiscoveredWorkers(&listener, discovery_socket, config,
                                        mcpd4::localPort(listener),
                                        &remote_workers, &status_state);
    }
    timing.accept_workers_wall_us = elapsedUs(accept_start);
    status_state.finishSegment(
        "accept_workers",
        "accepted=" + std::to_string(remote_workers.size()) +
            ":target=" + std::to_string(config.worker_count) +
            ":mode=" + (config.discovery_port == 0 ? std::string("fixed")
                                                    : std::string("discovery")));

    auto solve_options = mcpd4::makeCoordinatorOptions(config.solver_policy);
    solve_options.progress_report_interval =
        (config.status_port != 0 || telemetry.enabled()) ? 1
                                                         : config.progress_every;
    solve_options.progress_callback =
        [&](const mcpd3::PartitionWorkerProgressRecord &record) {
          status_state.recordProgress(record, remote_workers);
          telemetry.recordProgress(record, remote_workers);
          if (config.progress_every > 0 &&
              record.total_iteration % config.progress_every == 0) {
            printProgress(record, remote_workers);
          }
        };
    const auto coordinator_setup_start = std::chrono::steady_clock::now();
    status_state.beginSegment(
        "coordinator_setup", "coordinator_setup",
        "packages=" + std::to_string(packages.size()) +
            ":workers=" + std::to_string(remote_workers.size()));
    mcpd3::PartitionWorkerCoordinator coordinator(std::move(packages),
                                                  std::move(workers),
                                                  solve_options);
    status_state.recordWorkerDetails(remote_workers);
    timing.coordinator_setup_wall_us = elapsedUs(coordinator_setup_start);
    status_state.finishSegment(
        "coordinator_setup",
        "assigned_partitions=" + std::to_string(config.partition_count) +
            ":active_workers=" + std::to_string(remote_workers.size()));
    telemetry.beginSolve(remote_workers);
    const auto solve_start = std::chrono::steady_clock::now();
    const long solve_iteration_budget =
        config.solver_policy.max_total_iteration_count > 0
            ? config.solver_policy.max_total_iteration_count
            : static_cast<long>(config.solver_policy.max_iteration_count) *
                  static_cast<long>(
                      config.solver_policy.num_optimization_scales);
    status_state.beginSegment(
        "solve", "solving",
        "max_total_iterations=" + std::to_string(solve_iteration_budget),
        0, solve_iteration_budget);
    auto result = coordinator.solve();
    if (result.status !=
            mcpd3::PartitionWorkerOptimizationStatus::OPTIMAL &&
        config.solver_policy.retry_exhaust_regularized_scale_iterations &&
        mcpd4::usesRegularization(config.solver_policy) &&
        !config.solver_policy.exhaust_regularized_scale_iterations) {
      std::cout << "solve_retry exhaustive_regularized_scales\n";
      std::cout.flush();
      mcpd4::configureCoordinatorSchedule(
          &coordinator, config.solver_policy,
          /*exhaust_regularized_scale_iterations=*/true);
      result = coordinator.solve();
    }
    status_state.recordWorkerDetails(remote_workers);
    status_state.recordFinal(result);
    timing.solve_wall_us = elapsedUs(solve_start);

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
    std::cout << "best_lower_bound " << result.best_lower_bound << "\n";
    std::cout << "best_lower_bound_raw "
              << integerString(result.best_lower_bound_raw) << "\n";
    std::cout << "best_certified_lower_bound "
              << result.best_certified_lower_bound << "\n";
    std::cout << "best_certified_lower_bound_raw "
              << integerString(result.best_certified_lower_bound_raw) << "\n";
    std::cout << "best_regularized_objective "
              << result.best_regularized_objective << "\n";
    std::cout << "best_regularized_objective_raw "
              << integerString(result.best_regularized_objective_raw) << "\n";
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
    printObjectiveScaleStats(config, objective_scale_stats);
    status_state.beginSegment(
        "stop_workers", "stopping_workers",
        "workers=" + std::to_string(remote_workers.size()), 0,
        static_cast<long>(remote_workers.size()));
    const auto stop_start = std::chrono::steady_clock::now();
    long stopped_workers = 0;
    for (auto *worker : remote_workers) {
      worker->stop(/*reason=*/0, "coordinator finished");
      ++stopped_workers;
      status_state.updateSegmentProgress(
          "stop_workers", stopped_workers,
          static_cast<long>(remote_workers.size()),
          "workers=" + std::to_string(remote_workers.size()));
    }
    timing.stop_workers_wall_us = elapsedUs(stop_start);
    status_state.finishSegment(
        "stop_workers",
        "workers=" + std::to_string(remote_workers.size()));
    timing.total_wall_us = elapsedUs(total_start);
    telemetry.recordFinal(timing, result, remote_workers);
    telemetry.finish();
    if (telemetry.enabled()) {
      std::cout << "telemetry_csv_prefix " << telemetry.prefix() << "\n";
      std::cout << "telemetry_csv_iterations "
                << telemetry.prefix() << ".iterations.csv\n";
      std::cout << "telemetry_csv_worker_iterations "
                << telemetry.prefix() << ".worker_iterations.csv\n";
      std::cout << "telemetry_csv_worker_rpc_metrics "
                << telemetry.prefix() << ".worker_rpc_metrics.csv\n";
    }
    printTiming(timing, remote_workers);
  } catch (const std::exception &e) {
    status_state.recordFailure(e.what());
    std::cerr << "mcpd4_coordinator failed: " << e.what() << "\n";
    std::cerr << "mcpd4_coordinator_status "
              << status_state.snapshot() << "\n";
    if (!parsed_args) {
      usage(argv[0]);
    }
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
