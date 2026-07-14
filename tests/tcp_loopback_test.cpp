#include <mcpd4/runtime.h>
#include <mcpd4/tcp.h>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
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

template <typename Fn>
void requireThrowsContaining(Fn fn, const std::string &needle,
                             const std::string &message) {
  try {
    fn();
  } catch (const std::runtime_error &e) {
    require(std::string(e.what()).find(needle) != std::string::npos,
            message + "\nactual: " + e.what());
    return;
  }
  throw std::runtime_error(message + "\nactual: no exception");
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

void connectedSocketsDisableNagleDelay() {
  auto pair = makeConnectedPair();
  require(mcpd4::tcpNoDelayEnabled(pair.client),
          "connected client socket should enable TCP_NODELAY");
  require(mcpd4::tcpNoDelayEnabled(pair.server),
          "accepted server socket should enable TCP_NODELAY");
}

class WorkerClientThread {
public:
  WorkerClientThread(std::uint16_t port,
                     mcpd4::HelloMessage hello,
                     mcpd4::TransportCompression compression =
                         mcpd4::TransportCompression::NONE)
      : thread_([this, port, hello, compression] {
          try {
            mcpd4::runWorkerClient("127.0.0.1", port, hello, {},
                                   compression);
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

mcpd3::PartitionPackage makeManyBoundaryLabelsPackage(int partition_id,
                                                      int count) {
  mcpd3::PartitionPackage package;
  package.partition_id = partition_id;
  package.local_node_count = count;
  package.terminal_capacities.reserve(count);
  package.local_to_global.reserve(count);
  package.constraint_endpoints.reserve(count);
  for (int i = 0; i < count; ++i) {
    package.terminal_capacities.push_back((i % 2 == 0) ? -3 : 3);
    package.local_to_global.push_back(10000 + i);
    package.constraint_endpoints.push_back(
        mcpd3::ConstraintEndpointBinding{/*constraint_id=*/20000 + i,
                                          /*global_node_id=*/10000 + i,
                                          /*local_index=*/i,
                                          /*is_source=*/true,
                                          /*alpha=*/0,
                                          /*last_alpha=*/0,
                                          /*alpha_momentum=*/0});
  }
  return package;
}

std::unique_ptr<mcpd4::TcpPartitionWorker> startRemoteWorker(
    mcpd4::SocketHandle *listener, WorkerClientThread **client,
    const std::string &worker_name,
    mcpd4::TransportCompression compression =
        mcpd4::TransportCompression::NONE) {
  const auto port = mcpd4::localPort(*listener);
  auto hello = makeHello(worker_name);
  if (compression == mcpd4::TransportCompression::SNAPPY) {
    hello.feature_bits |= mcpd4::kFeatureSnappyCompression;
  }
  *client = new WorkerClientThread(port, hello, compression);
  return mcpd4::acceptTcpPartitionWorker(listener, 2s, compression);
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

void rejectsOversizedPayloadBeforeWritingBody() {
  auto pair = makeConnectedPair();
  const std::vector<std::uint8_t> payload{1, 2, 3, 4};
  const auto frame = mcpd4::encodeFrame(
      mcpd4::MessageType::READY, payload);
  requireThrows(
      [&] {
        mcpd4::sendFrameBytes(pair.client, frame,
                              mcpd4::TransportCompression::NONE, nullptr,
                              frame.size() - 1);
      },
      "oversized TCP frame should be rejected before send");
}

void defaultFrameLimitCoversObservedLargeAdheadP16Package() {
  constexpr std::size_t kLargestObservedP16PackageFrame =
      358612992ULL + 12ULL;
  require(mcpd4::kDefaultMaxFrameBytes >=
              kLargestObservedP16PackageFrame,
          "default frame limit should cover the observed p16 adhead package");
}

void rejectsOversizedSnappyLogicalFrameBeforeReadingBody() {
  if (!mcpd4::snappyCompressionAvailable()) {
    return;
  }
  auto pair = makeConnectedPair();
  mcpd4::ReadyMessage ready;
  ready.worker_name = std::string(1024, 'a');
  const auto frame = mcpd4::encodeReady(ready);

  mcpd4::sendFrameBytes(pair.client, frame,
                        mcpd4::TransportCompression::SNAPPY);
  requireThrows(
      [&] {
        (void)mcpd4::receiveFrameBytes(
            pair.server, frame.size() - 1,
            mcpd4::TransportCompression::SNAPPY);
      },
      "oversized snappy logical frame should be rejected");
}

void snappyCompressedFrameRoundTrips() {
  if (!mcpd4::snappyCompressionAvailable()) {
    return;
  }
  auto pair = makeConnectedPair();
  mcpd4::ReadyMessage ready;
  ready.worker_name = std::string(1024 * 1024, 'a');
  const auto frame = mcpd4::encodeReady(ready);

  mcpd4::FrameTransferStats sent;
  mcpd4::sendFrameBytes(pair.client, frame,
                        mcpd4::TransportCompression::SNAPPY, &sent);
  mcpd4::FrameTransferStats received;
  const auto decoded_frame = mcpd4::receiveFrameBytes(
      pair.server, 2ULL * 1024ULL * 1024ULL,
      mcpd4::TransportCompression::SNAPPY, &received);
  const auto decoded = mcpd4::decodeReady(decoded_frame);
  require(decoded.worker_name == ready.worker_name,
          "snappy transport frame should decode after round trip");
  require(sent.logical_bytes == frame.size(),
          "snappy send stats should preserve logical frame size");
  require(received.logical_bytes == frame.size(),
          "snappy receive stats should preserve logical frame size");
  require(sent.compression_requested && received.compression_requested,
          "snappy transfer stats should record compression mode");
  require(sent.compressed && received.compressed,
          "compressible frame should use snappy payload");
  require(sent.wire_bytes < sent.logical_bytes,
          "compressible frame should use fewer wire bytes than logical bytes");
  require(received.wire_bytes == sent.wire_bytes,
          "receiver should report the same compressed wire byte count");
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

void rejectsMismatchedWorkerCapacityMode() {
  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  const auto port = mcpd4::localPort(listener);
  std::thread client([&] {
    auto socket = mcpd4::connectTcp("127.0.0.1", port);
    auto hello = makeHello("wrong-capacity-mode");
    hello.capacity_mode =
        mcpd4::configuredCapacityMode() == mcpd4::CapacityMode::BITS_32
            ? mcpd4::CapacityMode::BITS_64
            : mcpd4::CapacityMode::BITS_32;
    mcpd4::sendFrameBytes(socket, mcpd4::encodeHello(hello));
  });
  requireThrows(
      [&] { (void)mcpd4::acceptTcpPartitionWorker(&listener, 2s); },
      "coordinator should reject a worker built for another capacity mode");
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

void loadPartitionDisconnectReportsWorkerAndPartitionContext() {
  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  const auto port = mcpd4::localPort(listener);
  std::thread client([&] {
    auto socket = mcpd4::connectTcp("127.0.0.1", port);
    mcpd4::sendFrameBytes(socket,
                          mcpd4::encodeHello(makeHello("closing-worker")));
  });
  auto worker = mcpd4::acceptTcpPartitionWorker(&listener, 2s);
  client.join();

  mcpd3::PartitionPackage package;
  package.partition_id = 42;
  package.local_node_count = 1;
  package.terminal_capacities = {0};
  package.local_to_global = {7};

  requireThrowsContaining(
      [&] { worker->loadPartition(package); },
      "worker closing-worker failed loading partition 42",
      "load partition disconnect should include worker and partition context");
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

void remoteWorkerSolvesConfiguredPrecisionExtreme() {
  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  WorkerClientThread *client = nullptr;
  auto worker = startRemoteWorker(&listener, &client, "extreme-worker");
  try {
    const mcpd3::Capacity capacity = mcpd3::capacity_test_extreme_value();
    mcpd3::PartitionPackage package;
    package.partition_id = 0;
    package.local_node_count = 2;
    package.arcs = {0, 1};
    package.arc_capacities = {capacity, 0};
    package.terminal_capacities = {capacity, -capacity};
    package.local_to_global = {5, 6};
    worker->loadPartition(package);

    mcpd3::PartitionSolveRequest request;
    request.round_id = 1;
    request.partition_id = 0;
    const auto result = worker->solveRound(request);
    require(result.lower_bound == mcpd3::widen_capacity(capacity),
            "remote solve must preserve the configured precision extreme");
    stopAndJoin(worker.get(), client);
  } catch (...) {
    stopAndJoin(worker.get(), client);
    throw;
  }
}

void remoteWorkerSaturatesScaleObjectiveOverflow() {
#if defined(MCPD_CAPACITY_MODE_GMP)
  return;
#else
  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  WorkerClientThread *client = nullptr;
  auto worker = startRemoteWorker(&listener, &client, "scale-saturate-worker");
  try {
    mcpd3::PartitionPackage package;
    package.partition_id = 0;
    package.local_node_count = 1;
    package.terminal_capacities = {
        std::numeric_limits<mcpd3::Capacity>::max() / 2 + 1};
    package.local_to_global = {5};
    worker->loadPartition(package);

    worker->scaleObjective(2, /*saturate_capacity_overflow=*/true);
    mcpd3::PartitionSolveRequest request;
    request.round_id = 1;
    request.partition_id = 0;
    (void)worker->solveRound(request);
    require(worker->timingStats().scale_objective_rpc_count == 1,
            "saturated remote scale should count objective scaling");
    stopAndJoin(worker.get(), client);
  } catch (...) {
    stopAndJoin(worker.get(), client);
    throw;
  }
#endif
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
    first.return_full_labels = true;

    mcpd3::PartitionSolveRequest second = first;
    second.partition_id = 1;

    const auto results = worker->solveRoundBatch({first, second});
    require(results.size() == 2,
            "remote batch should return one result per request");
    require(results[0].partition_id == 0 && results[1].partition_id == 1,
            "remote batch should preserve partition result order");
    require(results[0].round_id == 3 && results[1].round_id == 3,
            "remote batch should preserve round ids");
    require(results[0].full_labels.size() == 1 &&
                results[1].full_labels.size() == 1,
            "remote batch should return full labels when requested");
    require(results[0].full_labels[0].global_node_id == 11 &&
                results[1].full_labels[0].global_node_id == 11,
            "remote batch full labels should preserve global node ids");
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

void remoteWorkerDeltaEncodingReducesRepeatedSolveBytes() {
  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  WorkerClientThread *client = nullptr;
  auto worker = startRemoteWorker(&listener, &client, "delta-worker");
  try {
    constexpr int kBoundaryLabelCount = 128;
    const auto package =
        makeManyBoundaryLabelsPackage(/*partition_id=*/5, kBoundaryLabelCount);
    worker->loadPartition(package);

    mcpd3::PartitionSolveRequest request;
    request.round_id = 1;
    request.partition_id = package.partition_id;
    request.scale = 1000;
    request.regularization_strength = 0;
    request.alpha_updates.reserve(package.constraint_endpoints.size());
    for (const auto &endpoint : package.constraint_endpoints) {
      request.alpha_updates.push_back(mcpd3::AlphaUpdate{
          /*constraint_id=*/endpoint.constraint_id,
          /*alpha=*/1,
          /*last_alpha=*/0,
          /*alpha_momentum=*/0});
    }

    const auto before = worker->timingStats().rpc_bytes;
    const auto first = worker->solveRound(request);
    const auto after_first = worker->timingStats().rpc_bytes;
    request.round_id = 2;
    const auto second = worker->solveRound(request);
    const auto after_second = worker->timingStats().rpc_bytes;

    require(first.constrained_labels.size() == kBoundaryLabelCount,
            "first delta loopback solve should return all labels");
    require(second.constrained_labels.size() == kBoundaryLabelCount,
            "second delta loopback solve should reconstruct all labels");
    for (int i = 0; i < kBoundaryLabelCount; ++i) {
      require(first.constrained_labels[i].constraint_id ==
                  second.constrained_labels[i].constraint_id,
              "delta loopback should preserve label order");
      require(first.constrained_labels[i].label ==
                  second.constrained_labels[i].label,
              "delta loopback should preserve label values");
    }

    const auto first_request_bytes =
        after_first.solve_request_tx_bytes - before.solve_request_tx_bytes;
    const auto second_request_bytes = after_second.solve_request_tx_bytes -
                                      after_first.solve_request_tx_bytes;
    const auto first_result_bytes =
        after_first.solve_result_rx_bytes - before.solve_result_rx_bytes;
    const auto second_result_bytes = after_second.solve_result_rx_bytes -
                                     after_first.solve_result_rx_bytes;
    require(second_request_bytes * 2 < first_request_bytes,
            "repeated solve request should omit unchanged alpha payload");
    require(second_result_bytes * 2 < first_result_bytes,
            "repeated solve result should omit unchanged label payload");

    stopAndJoin(worker.get(), client);
  } catch (...) {
    stopAndJoin(worker.get(), client);
    throw;
  }
}

void remoteWorkerUsesSnappyCompression() {
  if (!mcpd4::snappyCompressionAvailable()) {
    return;
  }
  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  WorkerClientThread *client = nullptr;
  auto worker = startRemoteWorker(&listener, &client, "snappy-worker",
                                  mcpd4::TransportCompression::SNAPPY);
  try {
    mcpd3::PartitionPackage package;
    package.partition_id = 0;
    package.local_node_count = 8192;
    package.terminal_capacities.assign(package.local_node_count, 0);
    package.local_to_global.reserve(package.local_node_count);
    for (int i = 0; i < package.local_node_count; ++i) {
      package.local_to_global.push_back(i);
    }

    worker->loadPartition(package);
    const auto &bytes = worker->timingStats().rpc_bytes;
    require(bytes.partition_load_tx_bytes > 0,
            "snappy worker test should send a partition package");
    require(bytes.tx_wire_bytes_total > 0,
            "snappy worker test should record wire bytes");
    require(bytes.tx_compressed_frame_count > 0,
            "large package should be transmitted as a compressed frame");
    require(bytes.tx_wire_bytes_total < bytes.tx_bytes_total,
            "compressed remote load should reduce coordinator wire bytes");
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
    connectedSocketsDisableNagleDelay();
    receivesFrameSplitAcrossTcpPackets();
    rejectsOversizedPayloadBeforeReadingBody();
    rejectsOversizedPayloadBeforeWritingBody();
    defaultFrameLimitCoversObservedLargeAdheadP16Package();
    rejectsOversizedSnappyLogicalFrameBeforeReadingBody();
    snappyCompressedFrameRoundTrips();
    rejectsInvalidWorkerHello();
    rejectsMismatchedWorkerCapacityMode();
    remoteWorkerExposesHandshakeResources();
    remoteWorkerReportsErrorsAsExceptions();
    loadPartitionDisconnectReportsWorkerAndPartitionContext();
    remoteWorkerScalesLoadedObjective();
    remoteWorkerSolvesConfiguredPrecisionExtreme();
    remoteWorkerSaturatesScaleObjectiveOverflow();
    remoteWorkerSolvesExplicitBatch();
    remoteWorkerDeltaEncodingReducesRepeatedSolveBytes();
    remoteWorkerUsesSnappyCompression();
    remoteWorkerCoordinatorSolvesRegularizedAgreement();
    remoteWorkerCoordinatorPromotesObjectiveScale();
  } catch (const std::exception &e) {
    std::cerr << "tcp_loopback_test failed: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
