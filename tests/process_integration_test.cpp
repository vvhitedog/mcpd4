#include <mcpd4/tcp.h>

#include <decomp/dualdecomp.h>
#include <decomp/partition_coordinator.h>
#include <graph/dimacs.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <netinet/in.h>
#include <sys/vfs.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using namespace std::chrono_literals;

#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC 0x01021994
#endif

#ifndef RAMFS_MAGIC
#define RAMFS_MAGIC 0x858458f6
#endif

#ifndef HUGETLBFS_MAGIC
#define HUGETLBFS_MAGIC 0x958458f6
#endif

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct CaseConfig {
  std::string name;
  std::string fixture;
  int worker_count = 2;
  int partition_count = 2;
  int max_iterations = 80;
  int schedule_levels = 5;
  long schedule_start = 10000;
  long objective_scale = 10000;
};

struct SolveSummary {
  long status = -1;
  long stop_reason = -1;
  mcpd3::Objective final_objective_raw = 0;
  mcpd3::Objective final_certified_lower_bound_raw = 0;
  mcpd3::Objective final_regularized_objective_raw = 0;
  mcpd3::Objective best_lower_bound_raw = 0;
  mcpd3::Objective best_certified_lower_bound_raw = 0;
  mcpd3::Objective best_regularized_objective_raw = 0;
  long objective_scale = 1;
  long objective_scale_promotions = 0;
  long total_iterations = 0;
  long final_disagreement_count = 0;
  mcpd3::Objective final_regularization_budget = 0;
  mcpd3::Objective final_regularization_contribution = 0;
  long final_regularization_anchor_sink_count = 0;
  long final_regularization_active_sink_count = 0;
};

struct DistributedRun {
  SolveSummary summary;
  std::string output;
};

struct ChildProcess {
  pid_t pid = -1;
  int output_fd = -1;
  std::vector<std::string> args;
  std::string output;
};

std::string commandString(const std::vector<std::string> &args) {
  std::string command;
  for (const auto &arg : args) {
    if (!command.empty()) {
      command += " ";
    }
    command += arg;
  }
  return command;
}

ChildProcess spawnProcess(std::vector<std::string> args) {
  int pipefd[2] = {-1, -1};
  if (::pipe(pipefd) != 0) {
    throw std::runtime_error("pipe failed: " + std::string(std::strerror(errno)));
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    ::close(pipefd[0]);
    ::close(pipefd[1]);
    throw std::runtime_error("fork failed: " + std::string(std::strerror(errno)));
  }

  if (pid == 0) {
    ::dup2(pipefd[1], STDOUT_FILENO);
    ::dup2(pipefd[1], STDERR_FILENO);
    ::close(pipefd[0]);
    ::close(pipefd[1]);

    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (auto &arg : args) {
      argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);
    ::execv(argv[0], argv.data());
    std::perror("execv");
    _exit(127);
  }

  ::close(pipefd[1]);
  ChildProcess child;
  child.pid = pid;
  child.output_fd = pipefd[0];
  child.args = std::move(args);
  return child;
}

std::string readOutput(ChildProcess *child) {
  if (child->output_fd < 0) {
    return child->output;
  }
  char buffer[4096];
  while (true) {
    const ssize_t n = ::read(child->output_fd, buffer, sizeof(buffer));
    if (n > 0) {
      child->output.append(buffer, buffer + n);
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    break;
  }
  ::close(child->output_fd);
  child->output_fd = -1;
  return child->output;
}

int exitCodeFromStatus(int status) {
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  }
  return -1;
}

int waitForExit(ChildProcess *child, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  int status = 0;
  while (true) {
    const pid_t rc = ::waitpid(child->pid, &status, WNOHANG);
    if (rc == child->pid) {
      child->pid = -1;
      (void)readOutput(child);
      return exitCodeFromStatus(status);
    }
    if (rc < 0 && errno != EINTR) {
      throw std::runtime_error("waitpid failed for " +
                               commandString(child->args));
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      ::kill(child->pid, SIGKILL);
      (void)::waitpid(child->pid, &status, 0);
      child->pid = -1;
      (void)readOutput(child);
      throw std::runtime_error("process timed out: " +
                               commandString(child->args) + "\n" +
                               child->output);
    }
    std::this_thread::sleep_for(10ms);
  }
}

void killIfRunning(ChildProcess *child) {
  if (child == nullptr || child->pid <= 0) {
    return;
  }
  int status = 0;
  const pid_t rc = ::waitpid(child->pid, &status, WNOHANG);
  if (rc == 0) {
    ::kill(child->pid, SIGKILL);
    (void)::waitpid(child->pid, &status, 0);
  }
  child->pid = -1;
  (void)readOutput(child);
}

std::uint16_t reservePort() {
  auto socket = mcpd4::listenTcpLoopback(/*port=*/0);
  return mcpd4::localPort(socket);
}

std::uint16_t reserveUdpPort() {
  const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    throw std::runtime_error("failed to create UDP reserve socket: " +
                             std::string(std::strerror(errno)));
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    const std::string message =
        "failed to bind UDP reserve socket: " + std::string(std::strerror(errno));
    ::close(fd);
    throw std::runtime_error(message);
  }
  socklen_t len = sizeof(addr);
  if (::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) != 0) {
    const std::string message =
        "failed to inspect UDP reserve socket: " +
        std::string(std::strerror(errno));
    ::close(fd);
    throw std::runtime_error(message);
  }
  const auto port = ntohs(addr.sin_port);
  ::close(fd);
  return port;
}

void waitForReadyFile(const std::string &path,
                      std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    std::ifstream in(path);
    if (in.good()) {
      return;
    }
    std::this_thread::sleep_for(10ms);
  }
  throw std::runtime_error("coordinator did not write ready file: " + path);
}

std::vector<std::string> telemetryCsvSuffixes() {
  return {".metadata.csv",
          ".partitions.csv",
          ".workers.csv",
          ".iterations.csv",
          ".worker_iterations.csv",
          ".worker_rpc_metrics.csv",
          ".final.csv"};
}

void removeTelemetryCsvFiles(const std::string &prefix) {
  for (const auto &suffix : telemetryCsvSuffixes()) {
    std::remove((prefix + suffix).c_str());
  }
}

std::string readTextFile(const std::string &path) {
  std::ifstream in(path);
  require(in.good(), "failed to open expected file: " + path);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

void scaleGraph(mcpd3::MinCutGraph *graph, long factor) {
  if (factor == 1) {
    return;
  }
  for (auto &capacity : graph->arc_capacities) {
    capacity = mcpd3::checked_scale_capacity(capacity, factor);
  }
  for (auto &capacity : graph->terminal_capacities) {
    capacity = mcpd3::checked_scale_capacity(capacity, factor);
  }
}

std::vector<std::unique_ptr<mcpd3::PartitionWorker>> makeInProcessWorkers(
    int count) {
  std::vector<std::unique_ptr<mcpd3::PartitionWorker>> workers;
  for (int i = 0; i < count; ++i) {
    workers.push_back(std::make_unique<mcpd3::InProcessPartitionWorker>());
  }
  return workers;
}

SolveSummary summarize(const mcpd3::PartitionWorkerCoordinatorSolveResult &r) {
  SolveSummary summary;
  summary.status = static_cast<long>(r.status);
  summary.stop_reason = static_cast<long>(r.stop_reason);
  summary.final_objective_raw = r.final_objective_raw;
  summary.final_certified_lower_bound_raw =
      r.final_certified_lower_bound_raw;
  summary.final_regularized_objective_raw =
      r.final_regularized_objective_raw;
  summary.best_lower_bound_raw = r.best_lower_bound_raw;
  summary.best_certified_lower_bound_raw = r.best_certified_lower_bound_raw;
  summary.best_regularized_objective_raw = r.best_regularized_objective_raw;
  summary.objective_scale = r.scale;
  summary.objective_scale_promotions = r.objective_scale_promotion_count;
  summary.total_iterations = r.total_iterations;
  summary.final_disagreement_count = r.final_disagreement_count;
  summary.final_regularization_budget = r.final_regularization_budget;
  summary.final_regularization_contribution =
      r.final_regularization_contribution;
  summary.final_regularization_anchor_sink_count =
      r.final_regularization_anchor_sink_count;
  summary.final_regularization_active_sink_count =
      r.final_regularization_active_sink_count;
  return summary;
}

SolveSummary runInProcessReference(const std::string &fixture_dir,
                                   const CaseConfig &config) {
  auto graph = mcpd3::read_dimacs(fixture_dir + "/" + config.fixture);
  scaleGraph(&graph, config.objective_scale);

  mcpd3::DualDecompositionOptions package_options;
  package_options.track_primal_upper_bound = false;
  package_options.verbose = false;
  package_options.thread_count = 1;
  package_options.objective_scale = config.objective_scale;
  mcpd3::DualDecomposition package_source(
      config.partition_count, graph.nnode, graph.narc, std::move(graph.arcs),
      std::move(graph.arc_capacities), std::move(graph.terminal_capacities),
      package_options);

  mcpd3::PartitionWorkerCoordinatorOptions solve_options;
  solve_options.max_iteration_count = config.max_iterations;
  solve_options.num_optimization_scales = config.schedule_levels;
  solve_options.initial_step_size = config.schedule_start;
  solve_options.objective_scale = config.objective_scale;

  mcpd3::PartitionWorkerCoordinator coordinator(
      package_source.getPartitionPackages(),
      makeInProcessWorkers(config.worker_count), solve_options);
  return summarize(coordinator.solve());
}

long parseLongField(const std::map<std::string, std::string> &fields,
                    const std::string &key) {
  const auto iter = fields.find(key);
  if (iter == fields.end()) {
    throw std::runtime_error("missing coordinator output field: " + key);
  }
  return std::stol(iter->second);
}

mcpd3::Objective parseObjectiveField(
    const std::map<std::string, std::string> &fields,
    const std::string &key) {
  const auto iter = fields.find(key);
  if (iter == fields.end()) {
    throw std::runtime_error("missing coordinator output field: " + key);
  }
  return mcpd3::parse_objective(iter->second);
}

SolveSummary parseCoordinatorOutput(const std::string &output) {
  const std::vector<std::string> keys{
      "status",
      "stop_reason",
      "final_objective_raw",
      "final_certified_lower_bound_raw",
      "final_regularized_objective_raw",
      "best_lower_bound_raw",
      "best_certified_lower_bound_raw",
      "best_regularized_objective_raw",
      "objective_scale",
      "objective_scale_promotions",
      "total_iterations",
      "final_disagreement_count",
      "final_regularization_budget",
      "final_regularization_contribution",
      "final_regularization_anchor_sink_count",
      "final_regularization_active_sink_count"};

  std::map<std::string, bool> wanted;
  for (const auto &key : keys) {
    wanted[key] = true;
  }

  std::map<std::string, std::string> fields;
  std::string::size_type offset = 0;
  while (offset < output.size()) {
    auto end = output.find('\n', offset);
    if (end == std::string::npos) {
      end = output.size();
    }
    const std::string line = output.substr(offset, end - offset);
    const auto space = line.find(' ');
    if (space != std::string::npos) {
      const auto key = line.substr(0, space);
      if (wanted.find(key) != wanted.end()) {
        fields[key] = line.substr(space + 1);
      }
    }
    offset = end + 1;
  }

  SolveSummary summary;
  summary.status = parseLongField(fields, "status");
  summary.stop_reason = parseLongField(fields, "stop_reason");
  summary.final_objective_raw =
      parseObjectiveField(fields, "final_objective_raw");
  summary.final_certified_lower_bound_raw =
      parseObjectiveField(fields, "final_certified_lower_bound_raw");
  summary.final_regularized_objective_raw =
      parseObjectiveField(fields, "final_regularized_objective_raw");
  summary.best_lower_bound_raw =
      parseObjectiveField(fields, "best_lower_bound_raw");
  summary.best_certified_lower_bound_raw =
      parseObjectiveField(fields, "best_certified_lower_bound_raw");
  summary.best_regularized_objective_raw =
      parseObjectiveField(fields, "best_regularized_objective_raw");
  summary.objective_scale = parseLongField(fields, "objective_scale");
  summary.objective_scale_promotions =
      parseLongField(fields, "objective_scale_promotions");
  summary.total_iterations = parseLongField(fields, "total_iterations");
  summary.final_disagreement_count =
      parseLongField(fields, "final_disagreement_count");
  summary.final_regularization_budget =
      parseObjectiveField(fields, "final_regularization_budget");
  summary.final_regularization_contribution =
      parseObjectiveField(fields, "final_regularization_contribution");
  summary.final_regularization_anchor_sink_count =
      parseLongField(fields, "final_regularization_anchor_sink_count");
  summary.final_regularization_active_sink_count =
      parseLongField(fields, "final_regularization_active_sink_count");
  return summary;
}

DistributedRun runDistributedProcess(const std::string &coordinator_bin,
                                     const std::string &worker_bin,
                                     const std::string &fixture_dir,
                                     const CaseConfig &config,
                                     std::vector<std::string> extra_args = {},
                                     std::vector<std::string>
                                         worker_extra_args = {}) {
  const auto port = reservePort();
  const std::string port_string = std::to_string(port);
  const std::string ready_file =
      "/tmp/mcpd4-stage5-" + std::to_string(::getpid()) + "-" + config.name +
      ".ready";
  std::remove(ready_file.c_str());

  std::vector<ChildProcess> workers;
  ChildProcess coordinator;
  try {
    std::vector<std::string> coordinator_args{
        coordinator_bin,
        fixture_dir + "/" + config.fixture,
        "--port",
        port_string,
        "--workers",
        std::to_string(config.worker_count),
        "--partitions",
        std::to_string(config.partition_count),
        "--max-iterations",
        std::to_string(config.max_iterations),
        "--schedule-levels",
        std::to_string(config.schedule_levels),
        "--schedule-start",
        std::to_string(config.schedule_start),
        "--objective-scale",
        std::to_string(config.objective_scale),
        "--accept-timeout-ms",
        "5000",
        "--ready-file",
        ready_file};
    coordinator_args.insert(coordinator_args.end(), extra_args.begin(),
                            extra_args.end());
    coordinator = spawnProcess(std::move(coordinator_args));

    waitForReadyFile(ready_file, 5s);

    for (int i = 0; i < config.worker_count; ++i) {
      std::vector<std::string> worker_args{
          worker_bin,
          "127.0.0.1",
          port_string,
          "--name",
          config.name + "-worker-" + std::to_string(i)};
      worker_args.insert(worker_args.end(), worker_extra_args.begin(),
                         worker_extra_args.end());
      workers.push_back(spawnProcess(std::move(worker_args)));
    }

    const int coordinator_exit = waitForExit(&coordinator, 20s);
    require(coordinator_exit == 0,
            "coordinator failed for " + config.name + "\n" +
                coordinator.output);
    for (auto &worker : workers) {
      const int worker_exit = waitForExit(&worker, 5s);
      require(worker_exit == 0,
              "worker failed for " + config.name + "\n" + worker.output);
    }
    std::remove(ready_file.c_str());
    DistributedRun run;
    run.summary = parseCoordinatorOutput(coordinator.output);
    run.output = coordinator.output;
    return run;
  } catch (...) {
    killIfRunning(&coordinator);
    for (auto &worker : workers) {
      killIfRunning(&worker);
    }
    std::remove(ready_file.c_str());
    throw;
  }
}

void requireEqual(const SolveSummary &distributed,
                  const SolveSummary &reference,
                  const std::string &case_name) {
  auto check = [&](const auto &lhs, const auto &rhs,
                   const std::string &field) {
    require(lhs == rhs, case_name + " mismatch for " + field +
                         ": distributed=" +
                         mcpd3::integer_to_string(lhs) + " reference=" +
                         mcpd3::integer_to_string(rhs));
  };

  check(distributed.status, reference.status, "status");
  check(distributed.stop_reason, reference.stop_reason, "stop_reason");
  check(distributed.final_objective_raw, reference.final_objective_raw,
        "final_objective_raw");
  check(distributed.final_certified_lower_bound_raw,
        reference.final_certified_lower_bound_raw,
        "final_certified_lower_bound_raw");
  check(distributed.final_regularized_objective_raw,
        reference.final_regularized_objective_raw,
        "final_regularized_objective_raw");
  check(distributed.best_lower_bound_raw, reference.best_lower_bound_raw,
        "best_lower_bound_raw");
  check(distributed.best_certified_lower_bound_raw,
        reference.best_certified_lower_bound_raw,
        "best_certified_lower_bound_raw");
  check(distributed.best_regularized_objective_raw,
        reference.best_regularized_objective_raw,
        "best_regularized_objective_raw");
  check(distributed.objective_scale, reference.objective_scale,
        "objective_scale");
  check(distributed.objective_scale_promotions,
        reference.objective_scale_promotions, "objective_scale_promotions");
  check(distributed.final_disagreement_count,
        reference.final_disagreement_count, "final_disagreement_count");
  check(distributed.final_regularization_budget,
        reference.final_regularization_budget, "final_regularization_budget");
  check(distributed.final_regularization_contribution,
        reference.final_regularization_contribution,
        "final_regularization_contribution");
  check(distributed.final_regularization_anchor_sink_count,
        reference.final_regularization_anchor_sink_count,
        "final_regularization_anchor_sink_count");
  check(distributed.final_regularization_active_sink_count,
        reference.final_regularization_active_sink_count,
        "final_regularization_active_sink_count");
  require(distributed.total_iterations > 0,
          case_name + " should run at least one optimization iteration");
}

void fixtureProcessMatchesInProcessReference(
    const std::string &coordinator_bin, const std::string &worker_bin,
    const std::string &fixture_dir, const CaseConfig &config) {
  const auto reference = runInProcessReference(fixture_dir, config);
  const auto distributed_run =
      runDistributedProcess(coordinator_bin, worker_bin, fixture_dir, config);
  requireEqual(distributed_run.summary, reference, config.name);
}

void coordinatorAcceptTimeoutIsExposed(const std::string &coordinator_bin,
                                       const std::string &fixture_dir) {
  const auto port = reservePort();
  const std::string ready_file =
      "/tmp/mcpd4-stage5-timeout-" + std::to_string(::getpid()) + ".ready";
  std::remove(ready_file.c_str());

  auto coordinator = spawnProcess({coordinator_bin,
                                   fixture_dir + "/hand_bottleneck.max",
                                   "--port",
                                   std::to_string(port),
                                   "--workers",
                                   "1",
                                   "--partitions",
                                   "1",
                                   "--accept-timeout-ms",
                                   "50",
                                   "--ready-file",
                                   ready_file});
  try {
    waitForReadyFile(ready_file, 5s);
    const int exit_code = waitForExit(&coordinator, 5s);
    require(exit_code != 0,
            "coordinator without workers should fail after accept timeout");
    require(coordinator.output.find("timed out waiting for worker connection") !=
                std::string::npos,
            "accept timeout should produce a clear error message\n" +
                coordinator.output);
    std::remove(ready_file.c_str());
  } catch (...) {
    killIfRunning(&coordinator);
    std::remove(ready_file.c_str());
    throw;
  }
}

void coordinatorDurableStatusSurvivesFailure(
    const std::string &coordinator_bin, const std::string &status_bin,
    const std::string &fixture_dir) {
  const auto port = reservePort();
  const std::string status_file =
      "/tmp/mcpd4-stage5-status-" + std::to_string(::getpid()) + ".txt";
  std::remove(status_file.c_str());

  auto coordinator = spawnProcess({coordinator_bin,
                                   fixture_dir + "/missing-input.max",
                                   "--port",
                                   std::to_string(port),
                                   "--workers",
                                   "1",
                                   "--partitions",
                                   "1",
                                   "--status-file",
                                   status_file});
  try {
    const int exit_code = waitForExit(&coordinator, 5s);
    require(exit_code != 0,
            "coordinator should fail for a missing DIMACS input");
    require(coordinator.output.find("mcpd4_coordinator_status ") !=
                std::string::npos,
            "coordinator failure should print a compact status snapshot\n" +
                coordinator.output);

    auto status = spawnProcess({status_bin, "--file", status_file});
    const int status_exit = waitForExit(&status, 5s);
    require(status_exit == 0,
            "mcpd4_status --file should read durable coordinator status\n" +
                status.output);
    require(status.output.find("role coordinator") != std::string::npos,
            "durable status should identify the coordinator\n" + status.output);
    require(status.output.find("phase error") != std::string::npos,
            "durable status should preserve failed phase\n" + status.output);
    require(status.output.find("last_error ") != std::string::npos,
            "durable status should preserve the failure message\n" +
                status.output);
    std::remove(status_file.c_str());
  } catch (...) {
    killIfRunning(&coordinator);
    std::remove(status_file.c_str());
    throw;
  }
}

void capacityOverflowSaturationIsOptIn(const std::string &coordinator_bin,
                                       const std::string &worker_bin,
                                       const std::string &fixture_dir) {
  if (mcpd3::capacity_storage_bits() != 32) {
    return;
  }
  const auto port = reservePort();
  auto rejected = spawnProcess({coordinator_bin,
                                fixture_dir + "/overflow_saturate.max",
                                "--port",
                                std::to_string(port),
                                "--workers",
                                "1",
                                "--partitions",
                                "1",
                                "--objective-scale",
                                "10000",
                                "--accept-timeout-ms",
                                "50"});
  const int rejected_exit = waitForExit(&rejected, 5s);
  require(rejected_exit != 0,
          "capacity overflow should fail in strict mode");
  require(rejected.output.find("capacity multiplication overflow") !=
              std::string::npos,
          "strict overflow should explain configured capacity failure\n" +
              rejected.output);

  const auto saturated = runDistributedProcess(
      coordinator_bin, worker_bin, fixture_dir,
      CaseConfig{/*name=*/"overflow_saturate",
                 /*fixture=*/"overflow_saturate.max",
                 /*worker_count=*/1,
                 /*partition_count=*/1,
                 /*max_iterations=*/5,
                 /*schedule_levels=*/1,
                 /*schedule_start=*/10000,
                 /*objective_scale=*/10000},
      {"--saturate-capacity-overflow"});
  require(saturated.output.find("objective_scale_overflow_mode saturate") !=
              std::string::npos,
          "saturated run should report saturate mode\n" + saturated.output);
  require(saturated.output.find("objective_scale_saturation_count 1") !=
              std::string::npos,
          "saturated run should report one clipped capacity\n" +
              saturated.output);
  require(saturated.summary.final_disagreement_count == 0,
          "one-partition saturated fixture should finish with agreement");
}

void progressTelemetryIsStreamed(const std::string &coordinator_bin,
                                 const std::string &worker_bin,
                                 const std::string &fixture_dir) {
  const auto run = runDistributedProcess(
      coordinator_bin, worker_bin, fixture_dir,
      CaseConfig{/*name=*/"progress",
                 /*fixture=*/"hand_bottleneck.max",
                 /*worker_count=*/2,
                 /*partition_count=*/2,
                 /*max_iterations=*/10,
                 /*schedule_levels=*/1,
                 /*schedule_start=*/10000,
                 /*objective_scale=*/10000},
      {"--progress-every", "1"});

  require(run.output.find("progress total_iteration ") != std::string::npos,
          "progress telemetry should include coordinator progress\n" +
              run.output);
  require(run.output.find(" schedule_scale ") != std::string::npos,
          "progress telemetry should use schedule_scale\n" + run.output);
  require(run.output.find(" schedule_step ") != std::string::npos,
          "progress telemetry should use schedule_step\n" + run.output);
  require(run.output.find(" effective_schedule_step ") != std::string::npos,
          "progress telemetry should use effective_schedule_step\n" +
              run.output);
  require(run.output.find(" scale ") == std::string::npos,
          "progress telemetry should not use ambiguous scale field\n" +
              run.output);
  require(run.output.find(" step_size ") == std::string::npos,
          "progress telemetry should not use step_size field\n" + run.output);
  require(run.output.find(" effective_step_size ") == std::string::npos,
          "progress telemetry should not use effective_step_size field\n" +
              run.output);
  require(run.output.find(" disagreement_count ") != std::string::npos,
          "progress telemetry should include disagreement count\n" +
              run.output);
  require(run.output.find(" regularized_objective ") != std::string::npos,
          "progress telemetry should include regularized objective\n" +
              run.output);
  require(run.output.find(" selected_objective ") == std::string::npos,
          "progress telemetry should not include selected objective\n" +
              run.output);
  require(run.output.find(" certified_lower_bound ") != std::string::npos,
          "progress telemetry should include certified lower bound\n" +
              run.output);
  require(run.output.find(" worker_solve_wall_us ") != std::string::npos,
          "progress telemetry should include worker solve timing\n" +
              run.output);
  require(run.output.find(" rpc_tx_bytes_total ") != std::string::npos,
          "progress telemetry should include total transmitted RPC bytes\n" +
              run.output);
  require(run.output.find(" rpc_rx_bytes_total ") != std::string::npos,
          "progress telemetry should include total received RPC bytes\n" +
              run.output);
  require(run.output.find(" rpc_tx_wire_bytes_total ") !=
              std::string::npos,
          "progress telemetry should include total transmitted wire bytes\n" +
              run.output);
  require(run.output.find(" rpc_rx_wire_bytes_total ") !=
              std::string::npos,
          "progress telemetry should include total received wire bytes\n" +
              run.output);
  require(run.output.find(" rpc_compression_wall_us ") !=
              std::string::npos,
          "progress telemetry should include compression timing\n" +
              run.output);
  require(run.output.find(" rpc_solve_request_tx_bytes ") !=
              std::string::npos,
          "progress telemetry should include solve request bytes\n" +
              run.output);
  require(run.output.find(" rpc_solve_result_rx_bytes ") !=
              std::string::npos,
          "progress telemetry should include solve result bytes\n" +
              run.output);
  require(run.output.find(" solve_batch_rpc_count_total ") !=
              std::string::npos,
          "progress telemetry should include batch RPC count\n" + run.output);
  require(run.output.find(" partition_solves_per_iteration ") !=
              std::string::npos,
          "progress telemetry should include partition solve factor\n" +
              run.output);
  require(run.output.find(" solve_batch_rpcs_per_iteration ") !=
              std::string::npos,
          "progress telemetry should include batch RPC factor\n" +
              run.output);
  require(run.output.find("progress_worker total_iteration ") !=
              std::string::npos,
          "progress telemetry should include per-worker timing lines\n" +
              run.output);
  require(run.output.find(" name progress-worker-0 ") != std::string::npos,
          "progress telemetry should include worker names\n" + run.output);
  require(run.output.find("solve_batch_rpc_count_total ") !=
              std::string::npos,
          "final telemetry should include batch RPC count\n" + run.output);
  require(run.output.find("rpc_partition_load_tx_bytes ") !=
              std::string::npos,
          "final telemetry should include partition load bytes\n" +
              run.output);
  require(run.output.find("rpc_tx_bytes_total ") != std::string::npos,
          "final telemetry should include total transmitted bytes\n" +
              run.output);
  require(run.output.find("rpc_rx_bytes_total ") != std::string::npos,
          "final telemetry should include total received bytes\n" +
              run.output);
  require(run.output.find("rpc_tx_wire_bytes_total ") !=
              std::string::npos,
          "final telemetry should include total transmitted wire bytes\n" +
              run.output);
  require(run.output.find("rpc_rx_wire_bytes_total ") !=
              std::string::npos,
          "final telemetry should include total received wire bytes\n" +
              run.output);
  require(run.output.find("timing_solve_round_batch_count ") ==
              std::string::npos,
          "final telemetry should not use timing prefix for counts\n" +
              run.output);
}

void telemetryCsvIsWritten(const std::string &coordinator_bin,
                           const std::string &worker_bin,
                           const std::string &fixture_dir) {
  const std::string prefix =
      "/tmp/mcpd4-telemetry-" + std::to_string(::getpid());
  removeTelemetryCsvFiles(prefix);
  try {
    const auto run = runDistributedProcess(
        coordinator_bin, worker_bin, fixture_dir,
        CaseConfig{/*name=*/"telemetry",
                   /*fixture=*/"hand_bottleneck.max",
                   /*worker_count=*/2,
                   /*partition_count=*/2,
                   /*max_iterations=*/10,
                   /*schedule_levels=*/1,
                   /*schedule_start=*/10000,
                   /*objective_scale=*/10000},
        {"--telemetry-csv-prefix", prefix});

    require(run.output.find("telemetry_csv_prefix " + prefix) !=
                std::string::npos,
            "coordinator should report telemetry CSV prefix\n" + run.output);
    require(run.output.find("telemetry_csv_worker_rpc_metrics " + prefix +
                            ".worker_rpc_metrics.csv") != std::string::npos,
            "coordinator should report RPC telemetry CSV path\n" +
                run.output);

    const auto metadata = readTextFile(prefix + ".metadata.csv");
    const auto partitions = readTextFile(prefix + ".partitions.csv");
    const auto workers = readTextFile(prefix + ".workers.csv");
    const auto iterations = readTextFile(prefix + ".iterations.csv");
    const auto worker_iterations =
        readTextFile(prefix + ".worker_iterations.csv");
    const auto worker_rpc_metrics =
        readTextFile(prefix + ".worker_rpc_metrics.csv");
    const auto final = readTextFile(prefix + ".final.csv");

    require(metadata.find("schema_version,1,") != std::string::npos,
            "metadata CSV should include schema version\n" + metadata);
    require(metadata.find("rpc_compression,none,") != std::string::npos,
            "metadata CSV should include compression mode\n" + metadata);
    require(partitions.find("partition_id,local_node_count") == 0,
            "partition CSV should include documented header\n" + partitions);
    require(partitions.find("\n0,") != std::string::npos,
            "partition CSV should include partition rows\n" + partitions);
    require(workers.find("telemetry-worker-0") != std::string::npos,
            "worker CSV should include worker names\n" + workers);
    require(iterations.find("iteration_wall_us") != std::string::npos,
            "iteration CSV should include wall-time delta column\n" +
                iterations);
    require(iterations.find("solve_elapsed_us") != std::string::npos,
            "iteration CSV should include solve elapsed column\n" +
                iterations);
    require(iterations.find("\n1,") != std::string::npos,
            "iteration CSV should include per-iteration rows\n" + iterations);
    require(worker_iterations.find("solve_rpc_wall_us_delta") !=
                std::string::npos,
            "worker iteration CSV should include RPC timing deltas\n" +
                worker_iterations);
    require(worker_iterations.find("worker_solve_wall_us_delta") !=
                std::string::npos,
            "worker iteration CSV should include solve timing deltas\n" +
                worker_iterations);
    require(worker_iterations.find("worker_rpc_overhead_us_delta") !=
                std::string::npos,
            "worker iteration CSV should include overhead timing deltas\n" +
                worker_iterations);
    require(worker_iterations.find("telemetry-worker-0") !=
                std::string::npos,
            "worker iteration CSV should include worker rows\n" +
                worker_iterations);
    require(worker_rpc_metrics.find("solve_request_tx_bytes") !=
                std::string::npos,
            "worker RPC metric CSV should include solve request bytes\n" +
                worker_rpc_metrics);
    require(worker_rpc_metrics.find("solve_result_rx_bytes") !=
                std::string::npos,
            "worker RPC metric CSV should include solve result bytes\n" +
                worker_rpc_metrics);
    require(final.find("timing_worker_rpc_overhead_us") != std::string::npos,
            "final CSV should include aggregate RPC overhead timing\n" +
                final);
    require(final.find("total_iterations") != std::string::npos,
            "final CSV should include total iterations\n" + final);
  } catch (...) {
    removeTelemetryCsvFiles(prefix);
    throw;
  }
  removeTelemetryCsvFiles(prefix);
}

void snappyCompressionProcessMatchesReference(
    const std::string &coordinator_bin, const std::string &worker_bin,
    const std::string &fixture_dir) {
  if (!mcpd4::snappyCompressionAvailable()) {
    return;
  }
  const CaseConfig config{/*name=*/"snappy",
                          /*fixture=*/"hand_bottleneck.max",
                          /*worker_count=*/2,
                          /*partition_count=*/2,
                          /*max_iterations=*/80,
                          /*schedule_levels=*/5,
                          /*schedule_start=*/10000,
                          /*objective_scale=*/10000};
  const auto reference = runInProcessReference(fixture_dir, config);
  const auto compressed = runDistributedProcess(
      coordinator_bin, worker_bin, fixture_dir, config,
      {"--rpc-compression", "snappy"},
      {"--rpc-compression", "snappy"});
  requireEqual(compressed.summary, reference, "snappy");
  require(compressed.output.find("rpc_compression snappy") !=
              std::string::npos,
          "snappy run should report compression mode\n" + compressed.output);
  require(compressed.output.find("rpc_tx_wire_bytes_total ") !=
              std::string::npos,
          "snappy run should report transmitted wire bytes\n" +
              compressed.output);
  require(compressed.output.find("rpc_rx_wire_bytes_total ") !=
              std::string::npos,
          "snappy run should report received wire bytes\n" +
              compressed.output);
  require(compressed.output.find("rpc_compression_wall_us ") !=
              std::string::npos,
          "snappy run should report compression timing\n" +
              compressed.output);
  require(compressed.output.find("rpc_decompression_wall_us ") !=
              std::string::npos,
          "snappy run should report decompression timing\n" +
              compressed.output);
  require(compressed.output.find("rpc_tx_compressed_frame_count ") !=
              std::string::npos,
          "snappy run should report compressed send frame count\n" +
              compressed.output);
  require(compressed.output.find("rpc_rx_compressed_frame_count ") !=
              std::string::npos,
          "snappy run should report compressed receive frame count\n" +
              compressed.output);
}

void streamingWorkerProcessMatchesReference(
    const std::string &coordinator_bin, const std::string &worker_bin,
    const std::string &fixture_dir) {
  const CaseConfig config{/*name=*/"streaming",
                          /*fixture=*/"random_small.max",
                          /*worker_count=*/2,
                          /*partition_count=*/3,
                          /*max_iterations=*/80,
                          /*schedule_levels=*/5,
                          /*schedule_start=*/10000,
                          /*objective_scale=*/10000};
  const auto reference = runInProcessReference(fixture_dir, config);
  const auto streaming = runDistributedProcess(
      coordinator_bin, worker_bin, fixture_dir, config, {},
      {"--streaming-partitions", "--streaming-cache-bytes", "1"});
  requireEqual(streaming.summary, reference, "streaming");
}

void discoveryModeAcceptsDiscoveredWorkersAndClose(
    const std::string &coordinator_bin, const std::string &worker_bin,
    const std::string &discovery_bin, const std::string &status_bin,
    const std::string &fixture_dir) {
  const CaseConfig config{/*name=*/"discovery",
                          /*fixture=*/"hand_bottleneck.max",
                          /*worker_count=*/1,
                          /*partition_count=*/2,
                          /*max_iterations=*/80,
                          /*schedule_levels=*/5,
                          /*schedule_start=*/10000,
                          /*objective_scale=*/10000};
  const auto reference = runInProcessReference(fixture_dir, config);
  const auto tcp_port = reservePort();
  const auto discovery_port = reserveUdpPort();
  const auto coordinator_status_port = reserveUdpPort();
  const auto worker_status_port = reserveUdpPort();
  const std::string token =
      "process-test-" + std::to_string(::getpid()) + "-discovery";
  const std::string status_token =
      "process-test-" + std::to_string(::getpid()) + "-status";
  const std::string ready_file =
      "/tmp/mcpd4-discovery-" + std::to_string(::getpid()) + ".ready";
  std::remove(ready_file.c_str());

  ChildProcess coordinator;
  ChildProcess worker;
  try {
    coordinator = spawnProcess({coordinator_bin,
                                fixture_dir + "/" + config.fixture,
                                "--port",
                                std::to_string(tcp_port),
                                "--workers",
                                std::to_string(config.worker_count),
                                "--partitions",
                                std::to_string(config.partition_count),
                                "--max-iterations",
                                std::to_string(config.max_iterations),
                                "--schedule-levels",
                                std::to_string(config.schedule_levels),
                                "--schedule-start",
                                std::to_string(config.schedule_start),
                                "--objective-scale",
                                std::to_string(config.objective_scale),
                                "--accept-timeout-ms",
                                "5000",
                                "--ready-file",
                                ready_file,
                                "--discovery-port",
                                std::to_string(discovery_port),
                                "--discovery-token",
                                token,
                                "--status-port",
                                std::to_string(coordinator_status_port),
                                "--status-token",
                                status_token});
    waitForReadyFile(ready_file, 5s);

    auto list = spawnProcess({discovery_bin,
                              "list",
                              "--host",
                              "127.0.0.1",
                              "--port",
                              std::to_string(discovery_port),
                              "--token",
                              token,
                              "--timeout-ms",
                              "2000"});
    const int list_exit = waitForExit(&list, 5s);
    require(list_exit == 0, "discovery list failed\n" + list.output);
    require(list.output.find("coordinator host 127.0.0.1") !=
                std::string::npos,
            "discovery list should report coordinator host\n" + list.output);
    require(list.output.find(" tcp_port " + std::to_string(tcp_port)) !=
                std::string::npos,
            "discovery list should report coordinator TCP port\n" +
                list.output);

    worker = spawnProcess({worker_bin,
                           "--discover",
                           "--discovery-host",
                           "127.0.0.1",
                           "--discovery-port",
                           std::to_string(discovery_port),
                           "--discovery-token",
                           token,
                           "--discovery-timeout-ms",
                           "2000",
                           "--status-port",
                           std::to_string(worker_status_port),
                           "--status-token",
                           status_token,
                           "--max-active-partition-solves",
                           "1",
                           "--name",
                           "discovered-worker"});

    auto query_status = [&](std::uint16_t port) {
      auto status = spawnProcess({status_bin,
                                  "127.0.0.1",
                                  std::to_string(port),
                                  "--token",
                                  status_token,
                                  "--timeout-ms",
                                  "2000"});
      const int exit_code = waitForExit(&status, 5s);
      require(exit_code == 0, "status query failed\n" + status.output);
      return status.output;
    };

    std::string coordinator_status_output;
    const auto status_deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < status_deadline) {
      coordinator_status_output = query_status(coordinator_status_port);
      if (coordinator_status_output.find("worker_count 1") !=
              std::string::npos &&
          coordinator_status_output.find("worker_names discovered-worker") !=
              std::string::npos) {
        break;
      }
      std::this_thread::sleep_for(50ms);
    }
    require(coordinator_status_output.find("role coordinator") !=
                std::string::npos,
            "coordinator status should report role\n" +
                coordinator_status_output);
    require(coordinator_status_output.find("phase discovery_waiting") !=
                std::string::npos,
            "coordinator status should report discovery waiting phase\n" +
                coordinator_status_output);
    require(coordinator_status_output.find("worker_count 1") !=
                std::string::npos,
            "coordinator status should report accepted worker count\n" +
                coordinator_status_output);
    require(coordinator_status_output.find("worker_names discovered-worker") !=
                std::string::npos,
            "coordinator status should report accepted worker name\n" +
                coordinator_status_output);
    require(coordinator_status_output.find("worker_resources discovered-worker:cpu=") !=
                std::string::npos,
            "coordinator status should report accepted worker resources\n" +
                coordinator_status_output);
    require(coordinator_status_output.find("initial_objective_scale 10000") !=
                std::string::npos,
            "coordinator status should report initial objective scale\n" +
                coordinator_status_output);
    require(coordinator_status_output.find("schedule_scale 0") !=
                std::string::npos,
            "coordinator status should use schedule_scale naming\n" +
                coordinator_status_output);
    require(coordinator_status_output.find("rpc_tx_bytes_total ") !=
                std::string::npos,
            "coordinator status should report transmitted RPC bytes\n" +
                coordinator_status_output);
    require(coordinator_status_output.find("rpc_rx_bytes_total ") !=
                std::string::npos,
            "coordinator status should report received RPC bytes\n" +
                coordinator_status_output);
    require(coordinator_status_output.find(" segments ") !=
                std::string::npos,
            "coordinator status should include segment timing summary\n" +
                coordinator_status_output);
    require(coordinator_status_output.find(
                "read_graph:state=done:elapsed_us=") != std::string::npos,
            "coordinator status should report completed graph read segment\n" +
                coordinator_status_output);
    require(coordinator_status_output.find(
                "scale_graph:state=done:elapsed_us=") != std::string::npos,
            "coordinator status should report completed graph scale segment\n" +
                coordinator_status_output);
    require(coordinator_status_output.find(
                "partitioning:state=done:elapsed_us=") != std::string::npos,
            "coordinator status should report completed partition segment\n" +
                coordinator_status_output);
    require(coordinator_status_output.find(
                "transport_setup:state=done:elapsed_us=") != std::string::npos,
            "coordinator status should report completed transport setup "
            "segment\n" +
                coordinator_status_output);
    require(coordinator_status_output.find(
                "accept_workers:state=running:elapsed_us=") !=
                std::string::npos,
            "coordinator status should report running worker accept segment\n" +
                coordinator_status_output);
    require(coordinator_status_output.find("eta_remaining_us=") !=
                std::string::npos,
            "running segment status should include ETA/remaining field\n" +
                coordinator_status_output);
    require(coordinator_status_output.find("mode=discovery:accepted=1:target=1") !=
                std::string::npos,
            "accept segment should report discovery worker progress\n" +
                coordinator_status_output);
    require(coordinator_status_output.find("solve:state=") ==
                std::string::npos,
            "coordinator status should skip not-yet-started solve segment\n" +
                coordinator_status_output);

    std::string worker_status_output;
    while (std::chrono::steady_clock::now() < status_deadline) {
      worker_status_output = query_status(worker_status_port);
      if (worker_status_output.find("phase connected") != std::string::npos) {
        break;
      }
      std::this_thread::sleep_for(50ms);
    }
    require(worker_status_output.find("role worker") != std::string::npos,
            "worker status should report role\n" + worker_status_output);
    require(worker_status_output.find("name discovered-worker") !=
                std::string::npos,
            "worker status should report worker name\n" +
                worker_status_output);
    require(worker_status_output.find("phase connected") != std::string::npos,
            "worker status should report connected phase\n" +
                worker_status_output);
    require(worker_status_output.find("cpu_count ") != std::string::npos,
            "worker status should report CPU count\n" + worker_status_output);
    require(worker_status_output.find("ram_gb ") != std::string::npos,
            "worker status should report RAM GB\n" + worker_status_output);
    require(worker_status_output.find("rpc_rx_bytes_total ") !=
                std::string::npos,
            "worker status should report received RPC bytes\n" +
                worker_status_output);
    require(worker_status_output.find("rpc_tx_bytes_total ") !=
                std::string::npos,
            "worker status should report transmitted RPC bytes\n" +
                worker_status_output);
    require(worker_status_output.find("bk_storage file_mmap") !=
                std::string::npos,
            "worker should default to file-backed BK storage\n" +
                worker_status_output);
    require(worker_status_output.find("bk_mmap_dir /var/tmp/mcpd4-bk-mmap-") !=
                std::string::npos,
            "worker should report its default BK mmap directory\n" +
                worker_status_output);
    require(worker_status_output.find("bk_mmap_advise -") !=
                std::string::npos,
            "worker should not apply BK mmap advice unless requested\n" +
                worker_status_output);
    require(worker_status_output.find(
                "max_active_partition_solves 1") != std::string::npos,
            "worker status should report its active partition limit\n" +
                worker_status_output);
    require(worker_status_output.find("active_partition_ids -") !=
                std::string::npos,
            "idle worker status should report no active partition solve\n" +
                worker_status_output);

    auto close = spawnProcess({discovery_bin,
                               "close",
                               "--host",
                               "127.0.0.1",
                               "--port",
                               std::to_string(discovery_port),
                               "--token",
                               token,
                               "--timeout-ms",
                               "2000"});
    const int close_exit = waitForExit(&close, 5s);
    require(close_exit == 0, "discovery close failed\n" + close.output);
    require(close.output.find("closed accepted 1") != std::string::npos,
            "discovery close should be accepted\n" + close.output);

    const int coordinator_exit = waitForExit(&coordinator, 20s);
    require(coordinator_exit == 0,
            "discovery coordinator failed\n" + coordinator.output);
    const int worker_exit = waitForExit(&worker, 5s);
    require(worker_exit == 0, "discovery worker failed\n" + worker.output);
    require(coordinator.output.find("discovery_listening port " +
                                    std::to_string(discovery_port)) !=
                std::string::npos,
            "coordinator should report discovery listener\n" +
                coordinator.output);
    require(coordinator.output.find("accepted worker discovered-worker") !=
                std::string::npos,
            "coordinator should accept discovered worker\n" +
                coordinator.output);
    require(coordinator.output.find("discovery_closed worker_count 1") !=
                std::string::npos,
            "coordinator should report discovery close\n" +
                coordinator.output);

    const auto distributed = parseCoordinatorOutput(coordinator.output);
    requireEqual(distributed, reference, "discovery");
    std::remove(ready_file.c_str());
  } catch (...) {
    killIfRunning(&coordinator);
    killIfRunning(&worker);
    std::remove(ready_file.c_str());
    throw;
  }
}

bool pathIsMemoryBackedFilesystem(const std::string &path) {
  struct statfs fs {};
  if (::statfs(path.c_str(), &fs) != 0) {
    return false;
  }
  const auto type = static_cast<unsigned long>(fs.f_type);
  return type == TMPFS_MAGIC || type == RAMFS_MAGIC ||
         type == HUGETLBFS_MAGIC;
}

void workerRejectsMemoryBackedBkMmapDir(const std::string &worker_bin) {
  const std::string memory_fs = "/dev/shm";
  if (!pathIsMemoryBackedFilesystem(memory_fs)) {
    return;
  }

  const std::string bk_mmap_dir =
      memory_fs + "/mcpd4-process-test-bk-mmap-" +
      std::to_string(::getpid());
  std::filesystem::remove_all(bk_mmap_dir);

  auto worker = spawnProcess({worker_bin,
                              "127.0.0.1",
                              "1",
                              "--name",
                              "memory-backed-worker",
                              "--bk-storage",
                              "file_mmap",
                              "--bk-mmap-dir",
                              bk_mmap_dir});
  const int worker_exit = waitForExit(&worker, 5s);
  std::filesystem::remove_all(bk_mmap_dir);
  require(worker_exit != 0,
          "worker should reject memory-backed BK mmap directory\n" +
              worker.output);
  require(worker.output.find("memory-backed filesystem") != std::string::npos,
          "worker should explain memory-backed BK mmap rejection\n" +
              worker.output);
}

} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 6,
            "usage: process_integration_test COORDINATOR_BIN WORKER_BIN "
            "DISCOVERY_BIN STATUS_BIN FIXTURE_DIR");
    setenv("MCPD3_PARTITIONER", "basic", /*overwrite=*/1);

    const std::string coordinator_bin = argv[1];
    const std::string worker_bin = argv[2];
    const std::string discovery_bin = argv[3];
    const std::string status_bin = argv[4];
    const std::string fixture_dir = argv[5];

    fixtureProcessMatchesInProcessReference(
        coordinator_bin, worker_bin, fixture_dir,
        CaseConfig{/*name=*/"hand_bottleneck",
                   /*fixture=*/"hand_bottleneck.max"});
    fixtureProcessMatchesInProcessReference(
        coordinator_bin, worker_bin, fixture_dir,
        CaseConfig{/*name=*/"dead_end", /*fixture=*/"dead_end.max"});
    fixtureProcessMatchesInProcessReference(
        coordinator_bin, worker_bin, fixture_dir,
        CaseConfig{/*name=*/"random_small",
                   /*fixture=*/"random_small.max",
                   /*worker_count=*/2,
                   /*partition_count=*/3});
    coordinatorAcceptTimeoutIsExposed(coordinator_bin, fixture_dir);
    coordinatorDurableStatusSurvivesFailure(coordinator_bin, status_bin,
                                            fixture_dir);
    capacityOverflowSaturationIsOptIn(coordinator_bin, worker_bin,
                                      fixture_dir);
    progressTelemetryIsStreamed(coordinator_bin, worker_bin, fixture_dir);
    telemetryCsvIsWritten(coordinator_bin, worker_bin, fixture_dir);
    snappyCompressionProcessMatchesReference(coordinator_bin, worker_bin,
                                             fixture_dir);
    streamingWorkerProcessMatchesReference(coordinator_bin, worker_bin,
                                           fixture_dir);
    discoveryModeAcceptsDiscoveredWorkersAndClose(
        coordinator_bin, worker_bin, discovery_bin, status_bin, fixture_dir);
    workerRejectsMemoryBackedBkMmapDir(worker_bin);
  } catch (const std::exception &e) {
    std::cerr << "process_integration_test failed: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
