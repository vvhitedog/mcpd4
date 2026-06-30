#include <mcpd3_distributed/tcp.h>

#include <decomp/dualdecomp.h>
#include <decomp/partition_coordinator.h>
#include <graph/dimacs.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using namespace std::chrono_literals;

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
  int num_scales = 5;
  long initial_step_size = 10000;
  long capacity_multiplier = 10000;
};

struct SolveSummary {
  long status = -1;
  long stop_reason = -1;
  long final_objective_raw = 0;
  long final_certified_lower_bound_raw = 0;
  long final_regularized_objective_raw = 0;
  long best_selected_objective_raw = 0;
  long best_lower_bound_raw = 0;
  long best_certified_lower_bound_raw = 0;
  long best_regularized_objective_raw = 0;
  long objective_scale = 1;
  long objective_scale_promotions = 0;
  long total_iterations = 0;
  long final_disagreement_count = 0;
  long final_regularization_budget = 0;
  long final_regularization_contribution = 0;
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
  auto socket = mcpd3_distributed::listenTcpLoopback(/*port=*/0);
  return mcpd3_distributed::localPort(socket);
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

int checkedScaleInt(int value, long factor) {
  const long scaled = static_cast<long>(value) * factor;
  if (scaled > std::numeric_limits<int>::max() ||
      scaled < std::numeric_limits<int>::min()) {
    throw std::overflow_error("test capacity multiplier exceeds int range");
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
  summary.final_objective_raw = r.final_selected_objective_raw;
  summary.final_certified_lower_bound_raw =
      r.final_certified_lower_bound_raw;
  summary.final_regularized_objective_raw =
      r.final_regularized_objective_raw;
  summary.best_selected_objective_raw = r.best_selected_objective_raw;
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

  mcpd3::PartitionWorkerCoordinatorOptions solve_options;
  solve_options.max_iteration_count = config.max_iterations;
  solve_options.num_optimization_scales = config.num_scales;
  solve_options.initial_step_size = config.initial_step_size;
  solve_options.objective_scale = config.capacity_multiplier;

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

SolveSummary parseCoordinatorOutput(const std::string &output) {
  const std::vector<std::string> keys{
      "status",
      "stop_reason",
      "final_objective_raw",
      "final_certified_lower_bound_raw",
      "final_regularized_objective_raw",
      "best_selected_objective_raw",
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
  summary.final_objective_raw = parseLongField(fields, "final_objective_raw");
  summary.final_certified_lower_bound_raw =
      parseLongField(fields, "final_certified_lower_bound_raw");
  summary.final_regularized_objective_raw =
      parseLongField(fields, "final_regularized_objective_raw");
  summary.best_selected_objective_raw =
      parseLongField(fields, "best_selected_objective_raw");
  summary.best_lower_bound_raw =
      parseLongField(fields, "best_lower_bound_raw");
  summary.best_certified_lower_bound_raw =
      parseLongField(fields, "best_certified_lower_bound_raw");
  summary.best_regularized_objective_raw =
      parseLongField(fields, "best_regularized_objective_raw");
  summary.objective_scale = parseLongField(fields, "objective_scale");
  summary.objective_scale_promotions =
      parseLongField(fields, "objective_scale_promotions");
  summary.total_iterations = parseLongField(fields, "total_iterations");
  summary.final_disagreement_count =
      parseLongField(fields, "final_disagreement_count");
  summary.final_regularization_budget =
      parseLongField(fields, "final_regularization_budget");
  summary.final_regularization_contribution =
      parseLongField(fields, "final_regularization_contribution");
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
                                     std::vector<std::string> extra_args = {}) {
  const auto port = reservePort();
  const std::string port_string = std::to_string(port);
  const std::string ready_file =
      "/tmp/mcpd3-stage5-" + std::to_string(::getpid()) + "-" + config.name +
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
        "--num-scales",
        std::to_string(config.num_scales),
        "--initial-step",
        std::to_string(config.initial_step_size),
        "--capacity-multiplier",
        std::to_string(config.capacity_multiplier),
        "--accept-timeout-ms",
        "5000",
        "--ready-file",
        ready_file};
    coordinator_args.insert(coordinator_args.end(), extra_args.begin(),
                            extra_args.end());
    coordinator = spawnProcess(std::move(coordinator_args));

    waitForReadyFile(ready_file, 5s);

    for (int i = 0; i < config.worker_count; ++i) {
      workers.push_back(spawnProcess({worker_bin, "127.0.0.1", port_string,
                                      "--name",
                                      config.name + "-worker-" +
                                          std::to_string(i)}));
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
  auto check = [&](long lhs, long rhs, const std::string &field) {
    require(lhs == rhs, case_name + " mismatch for " + field +
                         ": distributed=" + std::to_string(lhs) +
                         " reference=" + std::to_string(rhs));
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
  check(distributed.best_selected_objective_raw,
        reference.best_selected_objective_raw,
        "best_selected_objective_raw");
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
      "/tmp/mcpd3-stage5-timeout-" + std::to_string(::getpid()) + ".ready";
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

void capacityOverflowSaturationIsOptIn(const std::string &coordinator_bin,
                                       const std::string &worker_bin,
                                       const std::string &fixture_dir) {
  const auto port = reservePort();
  auto rejected = spawnProcess({coordinator_bin,
                                fixture_dir + "/overflow_saturate.max",
                                "--port",
                                std::to_string(port),
                                "--workers",
                                "1",
                                "--partitions",
                                "1",
                                "--capacity-multiplier",
                                "10000",
                                "--accept-timeout-ms",
                                "50"});
  const int rejected_exit = waitForExit(&rejected, 5s);
  require(rejected_exit != 0,
          "capacity overflow should fail in strict mode");
  require(rejected.output.find("capacity multiplier exceeds int range") !=
              std::string::npos,
          "strict overflow should explain int range failure\n" +
              rejected.output);

  const auto saturated = runDistributedProcess(
      coordinator_bin, worker_bin, fixture_dir,
      CaseConfig{/*name=*/"overflow_saturate",
                 /*fixture=*/"overflow_saturate.max",
                 /*worker_count=*/1,
                 /*partition_count=*/1,
                 /*max_iterations=*/5,
                 /*num_scales=*/1,
                 /*initial_step_size=*/10000,
                 /*capacity_multiplier=*/10000},
      {"--saturate-capacity-overflow"});
  require(saturated.output.find("capacity_scale_overflow_mode saturate") !=
              std::string::npos,
          "saturated run should report saturate mode\n" + saturated.output);
  require(saturated.output.find("capacity_scale_saturation_count 1") !=
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
                 /*num_scales=*/1,
                 /*initial_step_size=*/10000,
                 /*capacity_multiplier=*/10000},
      {"--progress-every", "1"});

  require(run.output.find("progress total_iteration ") != std::string::npos,
          "progress telemetry should include coordinator progress\n" +
              run.output);
  require(run.output.find(" disagreement_count ") != std::string::npos,
          "progress telemetry should include disagreement count\n" +
              run.output);
  require(run.output.find(" regularized_objective ") != std::string::npos,
          "progress telemetry should include regularized objective\n" +
              run.output);
  require(run.output.find(" selected_objective ") != std::string::npos,
          "progress telemetry should include selected objective\n" +
              run.output);
  require(run.output.find(" certified_lower_bound ") != std::string::npos,
          "progress telemetry should include certified lower bound\n" +
              run.output);
  require(run.output.find(" worker_solve_wall_us ") != std::string::npos,
          "progress telemetry should include worker solve timing\n" +
              run.output);
  require(run.output.find(" solve_round_batch_count ") != std::string::npos,
          "progress telemetry should include batch solve count\n" +
              run.output);
  require(run.output.find("progress_worker total_iteration ") !=
              std::string::npos,
          "progress telemetry should include per-worker timing lines\n" +
              run.output);
  require(run.output.find(" name progress-worker-0 ") != std::string::npos,
          "progress telemetry should include worker names\n" + run.output);
  require(run.output.find("timing_solve_round_batch_count ") !=
              std::string::npos,
          "final timing should include batch solve count\n" + run.output);
}

} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 4,
            "usage: process_integration_test COORDINATOR_BIN WORKER_BIN "
            "FIXTURE_DIR");
    setenv("MCPD3_PARTITIONER", "basic", /*overwrite=*/1);

    const std::string coordinator_bin = argv[1];
    const std::string worker_bin = argv[2];
    const std::string fixture_dir = argv[3];

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
    capacityOverflowSaturationIsOptIn(coordinator_bin, worker_bin,
                                      fixture_dir);
    progressTelemetryIsStreamed(coordinator_bin, worker_bin, fixture_dir);
  } catch (const std::exception &e) {
    std::cerr << "process_integration_test failed: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
