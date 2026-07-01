#include <mcpd4/discovery.h>
#include <mcpd4/runtime.h>
#include <mcpd4/status.h>

#include <decomp/dualdecomp.h>
#include <decomp/partition_coordinator.h>
#include <graph/dimacs.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
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

struct Config {
  std::string dimacs_path;
  std::string bind_host = "127.0.0.1";
  std::uint16_t port = 0;
  int worker_count = 1;
  int partition_count = 1;
  int max_iterations = 10000;
  int schedule_levels = 5;
  long schedule_start = 10000;
  long objective_scale = 1;
  long accept_timeout_ms = 24L * 60L * 60L * 1000L;
  int progress_every = 0;
  std::string ready_file;
  std::uint16_t discovery_port = 0;
  std::string discovery_token = mcpd4::kDefaultDiscoveryToken;
  std::string advertise_host;
  std::uint16_t status_port = 0;
  std::string status_token = mcpd4::kDefaultStatusToken;
  bool saturate_capacity_overflow = false;
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

struct CoordinatorStatusState {
  mutable std::mutex mutex;
  std::string phase = "starting";
  std::uint16_t tcp_port = 0;
  std::uint16_t discovery_port = 0;
  std::uint16_t status_port = 0;
  int min_worker_count = 0;
  int worker_count = 0;
  int partition_count = 0;
  long objective_scale = 1;
  long initial_objective_scale = 1;
  long total_iteration = 0;
  long schedule_scale = 0;
  long schedule_step = 0;
  long lower_bound = 0;
  long best_lower_bound = 0;
  long certified_lower_bound = 0;
  long best_certified_lower_bound = 0;
  long regularized_objective = 0;
  long best_regularized_objective = 0;
  long disagreement_count = 0;
  long regularization_strength = 0;
  long regularization_budget = 0;
  long regularization_contribution = 0;
  long regularization_anchor_sink_count = 0;
  long regularization_active_sink_count = 0;
  long assigned_partition_count = 0;
  long active_worker_count = 0;
  long partition_solve_call_count_total = 0;
  long solve_batch_rpc_count_total = 0;
  std::vector<std::string> worker_names;
  std::vector<std::string> worker_resources;
  std::vector<std::string> partition_ownership;
  std::vector<std::string> worker_details;

  void initialize(const Config &config) {
    std::lock_guard<std::mutex> lock(mutex);
    min_worker_count = config.worker_count;
    partition_count = config.partition_count;
    objective_scale = config.objective_scale;
    initial_objective_scale = config.objective_scale;
  }

  void setPhase(const std::string &value) {
    std::lock_guard<std::mutex> lock(mutex);
    phase = value;
  }

  void setPorts(std::uint16_t tcp, std::uint16_t discovery,
                std::uint16_t status) {
    std::lock_guard<std::mutex> lock(mutex);
    tcp_port = tcp;
    discovery_port = discovery;
    status_port = status;
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
  }

  void recordWorkerDetails(const std::vector<mcpd4::TcpPartitionWorker *>
                               &remote_workers) {
    std::lock_guard<std::mutex> lock(mutex);
    worker_resources.clear();
    partition_ownership.clear();
    worker_details.clear();
    assigned_partition_count = 0;
    active_worker_count = 0;
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
          ":solve_rpc_us=" +
          std::to_string(snapshot.timing.solve_round_rpc_wall_us) +
          ":worker_solve_us=" +
          std::to_string(snapshot.timing.solve_round_worker_wall_us));
    }
  }

  void recordProgress(const mcpd3::PartitionWorkerProgressRecord &record,
                      const std::vector<mcpd4::TcpPartitionWorker *>
                          &remote_workers) {
    long assigned_partitions = 0;
    long active_workers = 0;
    long partition_solves = 0;
    long batch_rpcs = 0;
    for (const auto *worker : remote_workers) {
      const auto &stats = worker->timingStats();
      assigned_partitions += stats.load_partition_rpc_count;
      if (stats.load_partition_rpc_count > 0) {
        ++active_workers;
      }
      partition_solves += stats.partition_solve_call_count;
      batch_rpcs += stats.solve_batch_rpc_count;
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
          ":solve_rpc_us=" +
          std::to_string(snapshot.timing.solve_round_rpc_wall_us) +
          ":worker_solve_us=" +
          std::to_string(snapshot.timing.solve_round_worker_wall_us));
    }
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
  }

  std::string snapshot() const {
    std::lock_guard<std::mutex> lock(mutex);
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
        << " total_iteration " << total_iteration
        << " schedule_scale " << schedule_scale
        << " schedule_step " << schedule_step
        << " lower_bound " << lower_bound
        << " best_lower_bound " << best_lower_bound
        << " certified_lower_bound " << certified_lower_bound
        << " best_certified_lower_bound " << best_certified_lower_bound
        << " regularized_objective " << regularized_objective
        << " best_regularized_objective " << best_regularized_objective
        << " disagreement_count " << disagreement_count
        << " regularization_strength " << regularization_strength
        << " regularization_budget " << regularization_budget
        << " regularization_contribution " << regularization_contribution
        << " regularization_anchor_sink_count "
        << regularization_anchor_sink_count
        << " regularization_active_sink_count "
        << regularization_active_sink_count
        << " assigned_partition_count " << assigned_partition_count
        << " active_worker_count " << active_worker_count
        << " partition_solve_call_count_total "
        << partition_solve_call_count_total
        << " solve_batch_rpc_count_total " << solve_batch_rpc_count_total
        << " worker_names " << joinStatusValues(worker_names)
        << " worker_resources " << joinStatusValues(worker_resources)
        << " partition_ownership " << joinStatusValues(partition_ownership)
        << " worker_details " << joinStatusValues(worker_details);
    return out.str();
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
      << "       [--accept-timeout-ms N]\n"
      << "       [--progress-every N] [--ready-file PATH]\n"
      << "       [--discovery-port PORT] [--discovery-token TOKEN]\n"
      << "       [--advertise-host HOST]\n"
      << "       [--status-port PORT] [--status-token TOKEN]\n"
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
    } else if (arg == "--schedule-levels" || arg == "--num-scales") {
      config.schedule_levels = parseInt(require_value(arg), arg);
    } else if (arg == "--schedule-start" || arg == "--initial-step") {
      config.schedule_start = parseLong(require_value(arg), arg);
    } else if (arg == "--objective-scale" || arg == "--capacity-multiplier") {
      config.objective_scale = parseLong(require_value(arg), arg);
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
      throw std::overflow_error("objective scale exceeds int range");
    }
    ++(*saturation_count);
    return value < 0 ? std::numeric_limits<int>::min()
                     : std::numeric_limits<int>::max();
  }
  const long scaled = static_cast<long>(value) * factor;
  if (scaled > std::numeric_limits<int>::max() ||
      scaled < std::numeric_limits<int>::min()) {
    throw std::overflow_error("objective scale exceeds int range");
  }
  return static_cast<int>(scaled);
}

void scaleGraph(mcpd3::MinCutGraph *graph, long factor,
                bool saturate_capacity_overflow,
                ObjectiveScaleStats *stats) {
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

std::vector<std::unique_ptr<mcpd3::PartitionWorker>> acceptFixedWorkers(
    mcpd4::SocketHandle *listener, const Config &config,
    std::vector<mcpd4::TcpPartitionWorker *> *remote_workers,
    CoordinatorStatusState *status_state) {
  std::vector<std::unique_ptr<mcpd3::PartitionWorker>> workers;
  status_state->setPhase("accepting_workers");
  for (int i = 0; i < config.worker_count; ++i) {
    auto worker = mcpd4::acceptTcpPartitionWorker(
        listener, std::chrono::milliseconds(config.accept_timeout_ms));
    std::cout << "accepted worker " << worker->hello().worker_name << "\n";
    std::cout.flush();
    status_state->recordWorker(*worker);
    remote_workers->push_back(worker.get());
    workers.push_back(std::move(worker));
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
  status_state->setPhase("discovery_waiting");
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
        mcpd4::DiscoveryCloseResult result;
        result.accepted = true;
        result.worker_count = static_cast<int>(workers.size());
        result.min_worker_count = config.worker_count;
        mcpd4::sendDiscoveryCloseAck(discovery_socket, request, result);
      }
    }

    if ((fds[0].revents & POLLIN) != 0) {
      auto worker = mcpd4::acceptTcpPartitionWorker(
          listener, std::chrono::milliseconds(1));
      std::cout << "accepted worker " << worker->hello().worker_name << "\n";
      std::cout.flush();
      status_state->recordWorker(*worker);
      remote_workers->push_back(worker.get());
      workers.push_back(std::move(worker));
    }
  }

  status_state->setPhase("discovery_closed");
  std::cout << "discovery_closed worker_count " << workers.size()
            << " min_worker_count " << config.worker_count << "\n";
  std::cout.flush();
  return workers;
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
}

void printObjectiveScaleStats(const Config &config,
                              const ObjectiveScaleStats &stats) {
  const long total_saturation_count =
      stats.arc_saturation_count + stats.terminal_saturation_count;
  std::cout << "initial_objective_scale " << config.objective_scale
            << "\n";
  std::cout << "objective_scale_overflow_mode "
            << (config.saturate_capacity_overflow ? "saturate" : "strict")
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
  const auto worker_rpc_overhead_us =
      saturatedSubtract(solve_rpc_us, worker_solve_us);

  std::cout << "progress"
            << " total_iteration " << record.total_iteration
            << " schedule_scale " << record.scale
            << " iteration " << record.iteration
            << " lower_bound " << record.lower_bound
            << " best_lower_bound " << record.best_lower_bound
            << " certified_lower_bound "
            << record.certified_lower_bound
            << " best_certified_lower_bound "
            << record.best_certified_lower_bound
            << " regularized_objective " << record.regularized_objective
            << " best_regularized_objective "
            << record.best_regularized_objective
            << " disagreement_count " << record.disagreement_count
            << " disagreement_norm_sq " << record.disagreement_norm_sq
            << " schedule_step " << record.step_size
            << " effective_schedule_step " << record.effective_step_size
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
            << " worker_rpc_overhead_us " << worker_rpc_overhead_us << "\n";

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
    CoordinatorStatusState status_state;
    status_state.initialize(config);

    auto graph_start = std::chrono::steady_clock::now();
    status_state.setPhase("reading_graph");
    auto graph = config.directed
                     ? mcpd3::read_dimacs_directed_streaming(config.dimacs_path)
                     : mcpd3::read_dimacs(config.dimacs_path);
    timing.read_graph_wall_us = elapsedUs(graph_start);

    ObjectiveScaleStats objective_scale_stats;
    const auto scale_graph_start = std::chrono::steady_clock::now();
    status_state.setPhase("scaling_graph");
    scaleGraph(&graph, config.objective_scale,
               config.saturate_capacity_overflow, &objective_scale_stats);
    timing.scale_graph_wall_us = elapsedUs(scale_graph_start);
    if (objective_scale_stats.arc_saturation_count +
            objective_scale_stats.terminal_saturation_count >
        0) {
      std::cerr
          << "warning: saturated "
          << objective_scale_stats.arc_saturation_count +
                 objective_scale_stats.terminal_saturation_count
          << " capacities while scaling; results use clipped int capacities\n";
    }

    const auto partition_start = std::chrono::steady_clock::now();
    status_state.setPhase("partitioning");
    auto packages = makePartitionPackages(
        config.partition_count, std::move(graph), config.objective_scale);
    timing.partition_wall_us = elapsedUs(partition_start);

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
    std::unique_ptr<mcpd4::StatusServer> status_server;
    if (config.status_port != 0) {
      status_server = std::make_unique<mcpd4::StatusServer>(
          config.bind_host, config.status_port, config.status_token,
          [&status_state] { return status_state.snapshot(); });
      std::cout << "status_listening port " << status_server->port() << "\n";
      std::cout.flush();
    }
    status_state.setPorts(mcpd4::localPort(listener),
                          discovery_socket.valid()
                              ? mcpd4::localPort(discovery_socket)
                              : 0,
                          status_server ? status_server->port() : 0);
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

    mcpd3::PartitionWorkerCoordinatorOptions solve_options;
    solve_options.max_iteration_count = config.max_iterations;
    solve_options.num_optimization_scales = config.schedule_levels;
    solve_options.initial_step_size = config.schedule_start;
    solve_options.objective_scale = config.objective_scale;
    solve_options.progress_report_interval = config.progress_every;
    solve_options.progress_callback =
        [&](const mcpd3::PartitionWorkerProgressRecord &record) {
          status_state.recordProgress(record, remote_workers);
          printProgress(record, remote_workers);
        };
    const auto coordinator_setup_start = std::chrono::steady_clock::now();
    status_state.setPhase("coordinator_setup");
    mcpd3::PartitionWorkerCoordinator coordinator(std::move(packages),
                                                  std::move(workers),
                                                  solve_options);
    status_state.recordWorkerDetails(remote_workers);
    timing.coordinator_setup_wall_us = elapsedUs(coordinator_setup_start);
    const auto solve_start = std::chrono::steady_clock::now();
    status_state.setPhase("solving");
    const auto result = coordinator.solve();
    status_state.recordWorkerDetails(remote_workers);
    status_state.recordFinal(result);
    timing.solve_wall_us = elapsedUs(solve_start);

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
    std::cout << "best_lower_bound " << result.best_lower_bound << "\n";
    std::cout << "best_lower_bound_raw " << result.best_lower_bound_raw << "\n";
    std::cout << "best_certified_lower_bound "
              << result.best_certified_lower_bound << "\n";
    std::cout << "best_certified_lower_bound_raw "
              << result.best_certified_lower_bound_raw << "\n";
    std::cout << "best_regularized_objective "
              << result.best_regularized_objective << "\n";
    std::cout << "best_regularized_objective_raw "
              << result.best_regularized_objective_raw << "\n";
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
    printObjectiveScaleStats(config, objective_scale_stats);
    const auto stop_start = std::chrono::steady_clock::now();
    for (auto *worker : remote_workers) {
      worker->stop(/*reason=*/0, "coordinator finished");
    }
    timing.stop_workers_wall_us = elapsedUs(stop_start);
    timing.total_wall_us = elapsedUs(total_start);
    printTiming(timing, remote_workers);
  } catch (const std::exception &e) {
    std::cerr << "mcpd4_coordinator failed: " << e.what() << "\n";
    usage(argv[0]);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
