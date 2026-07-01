#include <mcpd4/runtime.h>
#include <mcpd4/tcp.h>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <vector>

#include <decomp/partition_coordinator.h>

namespace {

using namespace std::chrono_literals;

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Fn> void requireThrows(Fn fn, const std::string &message) {
  bool threw = false;
  try {
    fn();
  } catch (const std::runtime_error &) {
    threw = true;
  }
  require(threw, message);
}

void sendRawAll(int fd, const std::uint8_t *data, std::size_t size) {
  std::size_t sent = 0;
  while (sent < size) {
    const auto n = ::send(fd, data + sent, size - sent, MSG_NOSIGNAL);
    if (n <= 0) {
      throw std::runtime_error("raw test send failed");
    }
    sent += static_cast<std::size_t>(n);
  }
}

struct ConnectedPair {
  mcpd4::SocketHandle server;
  mcpd4::SocketHandle client;
};

ConnectedPair makeConnectedPair() {
  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  const auto port = mcpd4::localPort(listener);
  mcpd4::SocketHandle client;
  std::thread connector(
      [&] { client = mcpd4::connectTcp("127.0.0.1", port); });
  auto server = mcpd4::acceptTcp(&listener, 2s);
  connector.join();
  return ConnectedPair{std::move(server), std::move(client)};
}

class WorkerClientThread {
public:
  WorkerClientThread(std::uint16_t port,
                     mcpd4::HelloMessage hello)
      : thread_([this, port, hello] {
          try {
            mcpd4::runWorkerClient("127.0.0.1", port, hello);
          } catch (...) {
            exception_ = std::current_exception();
          }
        }) {}

  ~WorkerClientThread() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  void joinAndRethrow() {
    if (thread_.joinable()) {
      thread_.join();
    }
    if (exception_) {
      std::rethrow_exception(exception_);
    }
  }

private:
  std::thread thread_;
  std::exception_ptr exception_;
};

mcpd4::HelloMessage makeHello(const std::string &name) {
  auto hello = mcpd4::makeDefaultHello(name);
  hello.cpu_count = 1;
  hello.ram_gb = 1;
  hello.temp_path = "/tmp";
  return hello;
}

std::vector<mcpd3::PartitionPackage> makeTieBreakRegularizationPackages() {
  mcpd3::PartitionPackage source;
  source.partition_id = 0;
  source.local_node_count = 1;
  source.terminal_capacities = {-10};
  source.local_to_global = {11};
  source.constraint_endpoints.push_back(
      mcpd3::ConstraintEndpointBinding{/*constraint_id=*/7,
                                        /*global_node_id=*/11,
                                        /*local_index=*/0,
                                        /*is_source=*/true,
                                        /*alpha=*/0,
                                        /*last_alpha=*/0,
                                        /*alpha_momentum=*/0});

  mcpd3::PartitionPackage target;
  target.partition_id = 1;
  target.local_node_count = 1;
  target.terminal_capacities = {10};
  target.local_to_global = {11};
  target.constraint_endpoints.push_back(
      mcpd3::ConstraintEndpointBinding{/*constraint_id=*/7,
                                        /*global_node_id=*/11,
                                        /*local_index=*/0,
                                        /*is_source=*/false,
                                        /*alpha=*/0,
                                        /*last_alpha=*/0,
                                        /*alpha_momentum=*/0});

  return {source, target};
}

std::unique_ptr<mcpd4::TcpPartitionWorker> startRemoteWorker(
    mcpd4::SocketHandle *listener, WorkerClientThread **client,
    const std::string &worker_name) {
  const auto port = mcpd4::localPort(*listener);
  auto hello = makeHello(worker_name);
  *client = new WorkerClientThread(port, hello);
  return mcpd4::acceptTcpPartitionWorker(listener, 2s);
}

void stopAndJoin(mcpd4::TcpPartitionWorker *worker,
                 WorkerClientThread *client) {
  if (worker != nullptr) {
    worker->stop(/*reason=*/0, "test complete");
  }
  if (client != nullptr) {
    client->joinAndRethrow();
    delete client;
  }
}

void receivesFrameSplitAcrossTcpPackets() {
  auto pair = makeConnectedPair();
  mcpd4::ReadyMessage ready;
  ready.worker_name = "partial";
  const auto frame = mcpd4::encodeReady(ready);

  sendRawAll(pair.client.get(), frame.data(), 5);
  sendRawAll(pair.client.get(), frame.data() + 5, frame.size() - 5);

  const auto received = mcpd4::receiveFrameBytes(pair.server);
  const auto decoded = mcpd4::decodeReady(received);
  require(decoded.worker_name == ready.worker_name,
          "split TCP frame should decode after full receive");
}

void rejectsOversizedPayloadBeforeReadingBody() {
  auto pair = makeConnectedPair();
  const std::vector<std::uint8_t> payload{1, 2, 3, 4};
  const auto frame = mcpd4::encodeFrame(
      mcpd4::MessageType::READY, payload);
  mcpd4::sendFrameBytes(pair.client, frame);
  requireThrows(
      [&] { (void)mcpd4::receiveFrameBytes(pair.server, 3); },
      "oversized TCP frame should be rejected");
}

void rejectsInvalidWorkerHello() {
  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  const auto port = mcpd4::localPort(listener);
  std::thread client([&] {
    auto socket = mcpd4::connectTcp("127.0.0.1", port);
    auto hello = makeHello("bad-hello");
    hello.protocol_version = mcpd4::kProtocolVersion + 1;
    mcpd4::sendFrameBytes(socket,
                                      mcpd4::encodeHello(hello));
  });
  requireThrows(
      [&] {
        (void)mcpd4::acceptTcpPartitionWorker(&listener, 2s);
      },
      "coordinator should reject unsupported worker protocol versions");
  client.join();
}

void remoteWorkerExposesHandshakeResources() {
  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  const auto port = mcpd4::localPort(listener);
  auto hello = makeHello("resource-worker");
  hello.cpu_count = 7;
  hello.ram_gb = 48;
  auto client = std::make_unique<WorkerClientThread>(port, hello);
  auto worker = mcpd4::acceptTcpPartitionWorker(&listener, 2s);
  try {
    const auto resources = worker->resourceEstimate();
    require(resources.cpu_count == 7,
            "remote worker should expose HELLO cpu count");
    require(resources.ram_gb == 48,
            "remote worker should expose HELLO RAM estimate");
    stopAndJoin(worker.get(), client.release());
  } catch (...) {
    stopAndJoin(worker.get(), client.release());
    throw;
  }
}

void remoteWorkerReportsErrorsAsExceptions() {
  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  WorkerClientThread *client = nullptr;
  auto worker = startRemoteWorker(&listener, &client, "error-worker");
  try {
    mcpd3::PartitionSolveRequest request;
    request.round_id = 1;
    request.partition_id = 0;
    requireThrows([&] { (void)worker->solveRound(request); },
                  "remote solve before load should throw ERROR frame");
    stopAndJoin(worker.get(), client);
  } catch (...) {
    stopAndJoin(worker.get(), client);
    throw;
  }
}

void remoteWorkerScalesLoadedObjective() {
  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  WorkerClientThread *client = nullptr;
  auto worker = startRemoteWorker(&listener, &client, "scale-worker");
  try {
    mcpd3::PartitionPackage package;
    package.partition_id = 0;
    package.local_node_count = 1;
    package.terminal_capacities = {-7};
    package.local_to_global = {5};
    package.constraint_endpoints.push_back(
        mcpd3::ConstraintEndpointBinding{/*constraint_id=*/7,
                                          /*global_node_id=*/5,
                                          /*local_index=*/0,
                                          /*is_source=*/true,
                                          /*alpha=*/-7,
                                          /*last_alpha=*/-7,
                                          /*alpha_momentum=*/0});
    worker->loadPartition(package);

    mcpd3::PartitionSolveRequest request;
    request.round_id = 1;
    request.partition_id = 0;
    auto before = worker->solveRound(request);
    worker->scaleObjective(10);
    request.round_id = 2;
    auto after = worker->solveRound(request);
    require(before.lower_bound != 0,
            "scale test needs a nonzero lower bound before scaling");
    require(after.lower_bound == before.lower_bound * 10,
            "remote SCALE_OBJECTIVE should rescale loaded capacities");
    const auto &stats = worker->timingStats();
    require(stats.load_partition_rpc_count == 1,
            "remote timing should count partition loads");
    require(stats.partition_solve_call_count == 2,
            "remote timing should count partition solve calls");
    require(stats.solve_batch_rpc_count == 0,
            "direct single solve should not count as a batch RPC");
    require(stats.scale_objective_rpc_count == 1,
            "remote timing should count objective scaling");
    require(stats.rpc_bytes.hello_rx_bytes > 0,
            "remote telemetry should count worker hello bytes");
    require(stats.rpc_bytes.partition_load_tx_bytes > 0,
            "remote telemetry should count partition load bytes");
    require(stats.rpc_bytes.solve_request_tx_bytes > 0,
            "remote telemetry should count solve request bytes");
    require(stats.rpc_bytes.solve_result_rx_bytes > 0,
            "remote telemetry should count solve result bytes");
    require(stats.rpc_bytes.scale_objective_tx_bytes > 0,
            "remote telemetry should count objective scaling bytes");
    require(stats.rpc_bytes.tx_bytes_total >
                stats.rpc_bytes.partition_load_tx_bytes,
            "remote telemetry should aggregate transmitted bytes");
    require(stats.rpc_bytes.rx_bytes_total >
                stats.rpc_bytes.solve_result_rx_bytes,
            "remote telemetry should aggregate received bytes");
    require(stats.solve_round_rpc_wall_us >=
                stats.solve_round_worker_wall_us,
            "remote timing should split RPC and worker solve time");
    stopAndJoin(worker.get(), client);
  } catch (...) {
    stopAndJoin(worker.get(), client);
    throw;
  }
}

void remoteWorkerSolvesExplicitBatch() {
  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  WorkerClientThread *client = nullptr;
  auto worker = startRemoteWorker(&listener, &client, "batch-worker");
  try {
    for (const auto &package : makeTieBreakRegularizationPackages()) {
      worker->loadPartition(package);
    }

    mcpd3::PartitionSolveRequest first;
    first.round_id = 3;
    first.partition_id = 0;
    first.scale = 100;
    first.regularization_strength = 0;

    mcpd3::PartitionSolveRequest second = first;
    second.partition_id = 1;

    const auto results = worker->solveRoundBatch({first, second});
    require(results.size() == 2,
            "remote batch should return one result per request");
    require(results[0].partition_id == 0 && results[1].partition_id == 1,
            "remote batch should preserve partition result order");
    require(results[0].round_id == 3 && results[1].round_id == 3,
            "remote batch should preserve round ids");
    require(worker->timingStats().partition_solve_call_count == 2,
            "remote batch timing should count partition solve calls");
    require(worker->timingStats().solve_batch_rpc_count == 1,
            "remote batch timing should count batch RPCs");
    require(worker->timingStats().rpc_bytes.solve_request_tx_bytes > 0,
            "remote batch telemetry should count request bytes");
    require(worker->timingStats().rpc_bytes.solve_result_rx_bytes > 0,
            "remote batch telemetry should count result bytes");
    require(worker->timingStats().solve_round_rpc_wall_us >=
                worker->timingStats().solve_round_worker_wall_us,
            "remote batch timing should split RPC and worker solve time");
    stopAndJoin(worker.get(), client);
  } catch (...) {
    stopAndJoin(worker.get(), client);
    throw;
  }
}

void remoteWorkerCoordinatorSolvesRegularizedAgreement() {
  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  WorkerClientThread *client = nullptr;
  auto remote = startRemoteWorker(&listener, &client, "coordinator-worker");
  auto *remote_ptr = remote.get();
  try {
    std::vector<std::unique_ptr<mcpd3::PartitionWorker>> workers;
    workers.push_back(std::move(remote));

    mcpd3::PartitionWorkerCoordinatorOptions options;
    options.initial_step_size = 10;
    options.max_iteration_count = 5;
    options.num_optimization_scales = 1;
    options.patience = 99;
    options.enable_group_stopping = false;
    options.use_momentum = false;
    options.objective_scale = 10000;
    mcpd3::PartitionWorkerCoordinator coordinator(
        makeTieBreakRegularizationPackages(), std::move(workers), options);

    const auto result = coordinator.solve();
    require(result.status == mcpd3::PartitionWorkerOptimizationStatus::OPTIMAL,
            "remote worker coordinator should solve to agreement");
    require(result.stop_reason ==
                mcpd3::PartitionWorkerStopReason::REGULARIZED_NO_DISAGREEMENT,
            "remote worker coordinator should stop on regularized agreement");
    require(result.final_disagreement_count == 0,
            "remote worker coordinator should finish with no disagreement");
    require(result.final_regularization_budget == 10,
            "remote worker should preserve regularization diagnostics");
    require(remote_ptr->timingStats().solve_batch_rpc_count ==
                result.total_iterations,
            "single remote worker should receive one solve batch per round");
    require(remote_ptr->timingStats().partition_solve_call_count ==
                result.total_iterations * 2,
            "single remote worker should solve both partitions in each batch");
    remote_ptr->stop(/*reason=*/0, "coordinator test complete");
    client->joinAndRethrow();
    delete client;
  } catch (...) {
    if (remote_ptr != nullptr) {
      remote_ptr->stop(/*reason=*/1, "coordinator test failed");
    }
    if (client != nullptr) {
      client->joinAndRethrow();
      delete client;
    }
    throw;
  }
}

void remoteWorkerCoordinatorPromotesObjectiveScale() {
  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  WorkerClientThread *client = nullptr;
  auto remote = startRemoteWorker(&listener, &client, "promotion-worker");
  auto *remote_ptr = remote.get();
  try {
    std::vector<std::unique_ptr<mcpd3::PartitionWorker>> workers;
    workers.push_back(std::move(remote));

    mcpd3::PartitionWorkerCoordinatorOptions options;
    options.initial_step_size = 10;
    options.max_iteration_count = 20;
    options.num_optimization_scales = 3;
    options.patience = 99;
    options.enable_group_stopping = false;
    options.use_momentum = false;
    options.objective_scale = 10;
    options.max_objective_scale_promotions = 2;
    mcpd3::PartitionWorkerCoordinator coordinator(
        makeTieBreakRegularizationPackages(), std::move(workers), options);

    const auto result = coordinator.solve();
    require(result.status == mcpd3::PartitionWorkerOptimizationStatus::OPTIMAL,
            "remote promoted solve should reach agreement");
    require(result.objective_scale_promotion_count == 1,
            "remote solve should promote objective scale once");
    require(result.scale == 100,
            "remote promoted solve should report promoted objective scale");
    require(result.final_regularization_budget < result.scale,
            "remote promoted solve should finish under budget");
    require(remote_ptr->timingStats().scale_objective_rpc_count == 1,
            "remote promotion should record objective scaling timing");
    remote_ptr->stop(/*reason=*/0, "promotion test complete");
    client->joinAndRethrow();
    delete client;
  } catch (...) {
    if (remote_ptr != nullptr) {
      remote_ptr->stop(/*reason=*/1, "promotion test failed");
    }
    if (client != nullptr) {
      client->joinAndRethrow();
      delete client;
    }
    throw;
  }
}

} // namespace

int main() {
  try {
    receivesFrameSplitAcrossTcpPackets();
    rejectsOversizedPayloadBeforeReadingBody();
    rejectsInvalidWorkerHello();
    remoteWorkerExposesHandshakeResources();
    remoteWorkerReportsErrorsAsExceptions();
    remoteWorkerScalesLoadedObjective();
    remoteWorkerSolvesExplicitBatch();
    remoteWorkerCoordinatorSolvesRegularizedAgreement();
    remoteWorkerCoordinatorPromotesObjectiveScale();
  } catch (const std::exception &e) {
    std::cerr << "tcp_loopback_test failed: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
