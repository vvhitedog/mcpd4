#include <mcpd4/runtime.h>
#include <mcpd4/tcp.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
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
                         mcpd4::TransportCompression::NONE,
                     mcpd4::WorkerRuntimeOptions runtime_options = {})
      : thread_([this, port, hello, compression, runtime_options] {
          try {
            mcpd4::runWorkerClient("127.0.0.1", port, hello, {},
                                   compression, runtime_options);
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
        mcpd4::TransportCompression::NONE,
    mcpd4::WorkerRuntimeOptions runtime_options = {}) {
  const auto port = mcpd4::localPort(*listener);
  auto hello = makeHello(worker_name);
  if (compression == mcpd4::TransportCompression::SNAPPY) {
    hello.feature_bits |= mcpd4::kFeatureSnappyCompression;
  }
  *client = new WorkerClientThread(port, hello, compression,
                                   std::move(runtime_options));
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

void rawWorkerRejectsMultipartSequence(
    const std::vector<std::vector<std::uint8_t>> &frames,
    const std::string &expected_error, const std::string &worker_name) {
  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  WorkerClientThread client(mcpd4::localPort(listener), makeHello(worker_name));
  auto socket = mcpd4::acceptTcp(&listener, 2s);
  (void)mcpd4::decodeHello(mcpd4::receiveFrameBytes(socket));
  for (const auto &frame : frames) {
    mcpd4::sendFrameBytes(socket, frame);
  }
  const auto error =
      mcpd4::decodeError(mcpd4::receiveFrameBytes(socket));
  require(error.message.find(expected_error) != std::string::npos,
          "worker multipart error mismatch: " + error.message);
  mcpd4::sendFrameBytes(
      socket, mcpd4::encodeStop(mcpd4::StopMessage{0, "test complete"}));
  client.joinAndRethrow();
}

void remoteWorkerRejectsMalformedMultipartSequences() {
  mcpd3::PartitionPackage package;
  package.partition_id = 9;
  package.local_node_count = 2;
  package.arcs = {0, 1};
  package.arc_capacities = {3, 3};
  package.terminal_capacities = {5, -5};
  package.local_to_global = {10, 11};
  const auto header = mcpd4::makePartitionPackageTransferHeader(package);
  const auto chunk = mcpd4::encodePartitionPackageTransferChunk(
      package, mcpd4::PartitionPackageSection::ARCS, 0, 1);

  rawWorkerRejectsMultipartSequence(
      {chunk}, "chunk arrived before begin", "chunk-before-begin-worker");
  rawWorkerRejectsMultipartSequence(
      {mcpd4::encodePartitionPackageTransferEnd(
          mcpd4::PartitionPackageTransferEnd{package.partition_id})},
      "end arrived before begin", "end-before-begin-worker");

  mcpd3::PartitionSolveRequest request;
  request.partition_id = package.partition_id;
  rawWorkerRejectsMultipartSequence(
      {mcpd4::encodePartitionPackageTransferBegin(header),
       mcpd4::encodeSolveRoundRequest(request)},
      "transfer was interrupted", "interrupted-package-worker");

  std::vector<std::vector<std::uint8_t>> wrong_end_frames;
  wrong_end_frames.push_back(
      mcpd4::encodePartitionPackageTransferBegin(header));
  for (std::size_t section_index = 0;
       section_index < mcpd4::kPartitionPackageSectionCount;
       ++section_index) {
    const auto count = header.section_counts[section_index];
    if (count == 0) {
      continue;
    }
    wrong_end_frames.push_back(mcpd4::encodePartitionPackageTransferChunk(
        package, static_cast<mcpd4::PartitionPackageSection>(section_index),
        0, static_cast<std::size_t>(count)));
  }
  wrong_end_frames.push_back(mcpd4::encodePartitionPackageTransferEnd(
      mcpd4::PartitionPackageTransferEnd{package.partition_id + 1}));
  rawWorkerRejectsMultipartSequence(
      wrong_end_frames, "end id mismatch", "wrong-package-end-worker");
}

void remoteWorkerRejectsMalformedCapacityUpdateSequences() {
  mcpd3::PartitionCapacityUpdate update;
  update.partition_id = 9;
  update.arc_capacities = {3, 3};
  update.terminal_capacities = {5, -5};
  const auto header =
      mcpd4::makePartitionCapacityUpdateTransferHeader(update);
  const auto chunk = mcpd4::encodePartitionCapacityUpdateTransferChunk(
      update, mcpd4::PartitionCapacityUpdateSection::ARC_CAPACITIES,
      /*offset=*/0, /*count=*/1);
  rawWorkerRejectsMultipartSequence(
      {chunk}, "chunk arrived before begin", "capacity-chunk-before-worker");
  rawWorkerRejectsMultipartSequence(
      {mcpd4::encodePartitionCapacityUpdateTransferEnd(
          mcpd4::PartitionCapacityUpdateTransferEnd{update.partition_id})},
      "end arrived before begin", "capacity-end-before-worker");

  mcpd3::PartitionSolveRequest request;
  request.partition_id = update.partition_id;
  rawWorkerRejectsMultipartSequence(
      {mcpd4::encodePartitionCapacityUpdateTransferBegin(header),
       mcpd4::encodeSolveRoundRequest(request)},
      "transfer was interrupted", "interrupted-capacity-worker");

  std::vector<std::vector<std::uint8_t>> wrong_end_frames;
  wrong_end_frames.push_back(
      mcpd4::encodePartitionCapacityUpdateTransferBegin(header));
  for (std::size_t section_index = 0;
       section_index < mcpd4::kPartitionCapacityUpdateSectionCount;
       ++section_index) {
    const auto count = header.section_counts[section_index];
    wrong_end_frames.push_back(
        mcpd4::encodePartitionCapacityUpdateTransferChunk(
            update,
            static_cast<mcpd4::PartitionCapacityUpdateSection>(section_index),
            /*offset=*/0, static_cast<std::size_t>(count)));
  }
  wrong_end_frames.push_back(
      mcpd4::encodePartitionCapacityUpdateTransferEnd(
          mcpd4::PartitionCapacityUpdateTransferEnd{update.partition_id + 1}));
  rawWorkerRejectsMultipartSequence(
      wrong_end_frames, "end id mismatch", "wrong-capacity-end-worker");
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

void twoRemoteWorkersSolveCollectivePcgSystem() {
  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  WorkerClientThread *left_client = nullptr;
  WorkerClientThread *right_client = nullptr;
  auto left = startRemoteWorker(&listener, &left_client, "linear-left");
  auto right = startRemoteWorker(&listener, &right_client, "linear-right");
  try {
    mcpd4::LinearStructureMessage left_structure;
    left_structure.partition_id = 0;
    left_structure.owned_global_nodes = {0, 1};
    left_structure.ghost_global_nodes = {2};
    left_structure.row_offsets = {0, 2, 5};
    left_structure.column_indices = {0, 1, 0, 1, 2};
    left_structure.boundary_owned_local_indices = {1};
    left->loadLinearStructure(left_structure);

    mcpd4::LinearStructureMessage right_structure;
    right_structure.partition_id = 1;
    right_structure.owned_global_nodes = {2};
    right_structure.ghost_global_nodes = {1};
    right_structure.row_offsets = {0, 2};
    right_structure.column_indices = {0, 1};
    right_structure.boundary_owned_local_indices = {0};
    right->loadLinearStructure(right_structure);

    requireThrows(
        [&] { (void)left->multiplyLinear({/*partition_id=*/0, {0.0}}); },
        "linear operation before numerical system load should fail remotely");

    mcpd4::LinearSystemValuesMessage left_system;
    left_system.partition_id = 0;
    left_system.values = {4.0, -1.0, -1.0, 4.0, -1.0};
    left_system.rhs = {15.0, 10.0};
    left_system.initial_x = {0.0, 0.0};
    left->loadLinearSystem(left_system);

    mcpd4::LinearSystemValuesMessage right_system;
    right_system.partition_id = 1;
    right_system.values = {3.0, -1.0};
    right_system.rhs = {10.0};
    right_system.initial_x = {0.0};
    right->loadLinearSystem(right_system);

    auto left_initial =
        left->initializeLinear({/*partition_id=*/0, {0.0}});
    auto right_initial =
        right->initializeLinear({/*partition_id=*/1, {0.0}});
    const double rhs_norm_squared = left_initial.rhs_norm_squared +
                                    right_initial.rhs_norm_squared;
    double residual_inner = left_initial.residual_preconditioned_inner +
                            right_initial.residual_preconditioned_inner;
    double relative_residual = std::sqrt(
        (left_initial.residual_norm_squared +
         right_initial.residual_norm_squared) /
        rhs_norm_squared);
    std::vector<double> left_boundary = left_initial.boundary_direction;
    std::vector<double> right_boundary = right_initial.boundary_direction;

    int iterations = 0;
    while (relative_residual > 1e-12 && iterations < 10) {
      const auto left_product =
          left->multiplyLinear({/*partition_id=*/0, right_boundary});
      const auto right_product =
          right->multiplyLinear({/*partition_id=*/1, left_boundary});
      const double denominator = left_product.direction_product_inner +
                                 right_product.direction_product_inner;
      const double alpha = residual_inner / denominator;
      const auto left_update =
          left->updateLinearAlpha({/*partition_id=*/0, alpha});
      const auto right_update =
          right->updateLinearAlpha({/*partition_id=*/1, alpha});
      relative_residual = std::sqrt(
          (left_update.residual_norm_squared +
           right_update.residual_norm_squared) /
          rhs_norm_squared);
      ++iterations;
      if (relative_residual <= 1e-12) {
        break;
      }
      const double next_inner =
          left_update.residual_preconditioned_inner +
          right_update.residual_preconditioned_inner;
      const double beta = next_inner / residual_inner;
      left_boundary =
          left->updateLinearBeta({/*partition_id=*/0, beta})
              .boundary_direction;
      right_boundary =
          right->updateLinearBeta({/*partition_id=*/1, beta})
              .boundary_direction;
      residual_inner = next_inner;
    }

    require(iterations <= 3,
            "three-variable remote PCG should converge in at most n steps");
    require(relative_residual <= 1e-12,
            "two remote workers should reach the requested PCG residual");
    const auto left_solution = left->linearSolution(0);
    const auto right_solution = right->linearSolution(1);
    require(left_solution.size() == 2 && right_solution.size() == 1,
            "remote linear solution sizes should match ownership");
    for (double value : left_solution) {
      require(std::abs(value - 5.0) <= 1e-10,
              "left remote worker should recover the known solution");
    }
    require(std::abs(right_solution.at(0) - 5.0) <= 1e-10,
            "right remote worker should recover the known solution");
    const auto &left_timing = left->timingStats();
    require(left_timing.linear_structure_rpc_count == 1 &&
                left_timing.linear_system_rpc_count == 1 &&
                left_timing.linear_initialize_rpc_count == 1 &&
                left_timing.linear_multiply_rpc_count == iterations &&
                left_timing.linear_alpha_rpc_count == iterations &&
                left_timing.linear_beta_rpc_count == iterations - 1 &&
                left_timing.linear_solution_rpc_count == 1,
            "linear RPC telemetry should count each collective PCG phase");
    require(left_timing.linear_rpc_wall_us > 0 &&
                left_timing.rpc_bytes.linear_tx_bytes > 0 &&
                left_timing.rpc_bytes.linear_rx_bytes > 0,
            "linear RPC telemetry should record time and both byte directions");

    stopAndJoin(left.get(), left_client);
    left_client = nullptr;
    stopAndJoin(right.get(), right_client);
    right_client = nullptr;
  } catch (...) {
    stopAndJoin(left.get(), left_client);
    stopAndJoin(right.get(), right_client);
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

void remoteWorkerReplacesCapacitiesAndPreservesWarmState() {
  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  WorkerClientThread *client = nullptr;
  auto worker = startRemoteWorker(&listener, &client, "capacity-worker");
  try {
    mcpd3::PartitionPackage package;
    package.partition_id = 0;
    package.local_node_count = 2;
    package.arcs = {0, 1};
    package.arc_capacities = {5, 0};
    package.terminal_capacities = {9, -9};
    package.local_to_global = {0, 1};
    worker->loadPartition(package);

    mcpd3::PartitionSolveRequest request;
    request.round_id = 1;
    request.partition_id = 0;
    request.return_full_labels = true;
    const auto before = worker->solveRound(request);
    require(before.lower_bound == 5,
            "capacity replacement test needs a nonzero warm flow");

    mcpd3::PartitionCapacityUpdate update;
    update.partition_id = 0;
    update.arc_capacities = {10, 0};
    update.terminal_capacities = {18, -18};
    update.preserve_flow_state = true;
    update.flow_scale_numerator = 2;
    update.flow_scale_denominator = 1;
    worker->replacePartitionCapacities(update);

    request.round_id = 2;
    const auto after = worker->solveRound(request);
    require(after.lower_bound == before.lower_bound * 2,
            "remote capacity replacement should preserve and scale flow");
    require(after.full_labels.size() == before.full_labels.size(),
            "remote capacity replacement should preserve label count");
    for (std::size_t i = 0; i < after.full_labels.size(); ++i) {
      require(after.full_labels[i].global_node_id ==
                  before.full_labels[i].global_node_id &&
                  after.full_labels[i].local_index ==
                      before.full_labels[i].local_index &&
                  after.full_labels[i].label == before.full_labels[i].label,
              "remote capacity replacement should preserve the cut");
    }

    update.preserve_flow_state = false;
    update.flow_scale_numerator = 1;
    update.flow_scale_denominator = 1;
    worker->replacePartitionCapacities(update);
    request.round_id = 3;
    const auto reset = worker->solveRound(request);
    require(reset.lower_bound == after.lower_bound,
            "remote capacity replacement should recompute after flow reset");
    stopAndJoin(worker.get(), client);
  } catch (...) {
    stopAndJoin(worker.get(), client);
    throw;
  }
}

void remoteWorkerFileBacksCompleteSolverState() {
  if constexpr (!(mcpd3::solver_storage_mmap_compatible_v<mcpd3::Capacity> &&
                  mcpd3::solver_storage_mmap_compatible_v<mcpd3::NodeFlow> &&
                  mcpd3::solver_storage_mmap_compatible_v<
                      mcpd3::TerminalResidual> &&
                  mcpd3::solver_storage_mmap_compatible_v<mcpd3::Objective>)) {
    return;
  }
  const auto scratch =
      std::filesystem::temp_directory_path() /
      ("mcpd4-worker-file-backed-test-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()));
  std::filesystem::create_directories(scratch);
  mcpd4::WorkerRuntimeOptions runtime_options;
  runtime_options.solver_storage.mode =
      mcpd3::SolverStorageMode::FILE_BACKED_MMAP;
  runtime_options.solver_storage.directory = scratch.string();

  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  WorkerClientThread *client = nullptr;
  auto worker = startRemoteWorker(
      &listener, &client, "file-backed-worker",
      mcpd4::TransportCompression::NONE, runtime_options);
  try {
    mcpd3::PartitionPackage package;
    package.partition_id = 0;
    package.local_node_count = 2;
    package.arcs = {0, 1};
    package.arc_capacities = {7, 7};
    package.terminal_capacities = {11, -11};
    package.local_to_global = {0, 1};
    worker->loadPartition(package);

    std::size_t mapped_file_count = 0;
    for (const auto &entry :
         std::filesystem::directory_iterator("/proc/self/fd")) {
      std::error_code error;
      const auto target = std::filesystem::read_symlink(entry.path(), error);
      if (!error && target.string().find(scratch.string()) !=
                        std::string::npos) {
        ++mapped_file_count;
      }
    }
    require(mapped_file_count >= 8,
            "remote file-backed worker should map all persistent arrays");

    mcpd3::PartitionSolveRequest request;
    request.round_id = 1;
    request.partition_id = 0;
    require(worker->solveRound(request).lower_bound == 7,
            "remote file-backed worker should solve exactly");
    stopAndJoin(worker.get(), client);
    std::filesystem::remove_all(scratch);
  } catch (...) {
    stopAndJoin(worker.get(), client);
    std::filesystem::remove_all(scratch);
    throw;
  }
}

void legacyStreamingAliasUsesPersistentFileBackedWorker() {
  if constexpr (!(mcpd3::solver_storage_mmap_compatible_v<mcpd3::Capacity> &&
                  mcpd3::solver_storage_mmap_compatible_v<mcpd3::NodeFlow> &&
                  mcpd3::solver_storage_mmap_compatible_v<
                      mcpd3::TerminalResidual> &&
                  mcpd3::solver_storage_mmap_compatible_v<mcpd3::Objective>)) {
    return;
  }
  const auto scratch =
      std::filesystem::temp_directory_path() /
      ("mcpd4-legacy-streaming-alias-test-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()));
  std::filesystem::create_directories(scratch);
  mcpd4::WorkerRuntimeOptions runtime_options;
  runtime_options.streaming_partitions = true;
  runtime_options.streaming_directory = scratch.string();

  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  WorkerClientThread *client = nullptr;
  auto worker = startRemoteWorker(
      &listener, &client, "legacy-streaming-alias-worker",
      mcpd4::TransportCompression::NONE, runtime_options);
  try {
    mcpd3::PartitionPackage package;
    package.partition_id = 0;
    package.local_node_count = 2;
    package.arcs = {0, 1};
    package.arc_capacities = {7, 7};
    package.terminal_capacities = {11, -11};
    package.local_to_global = {0, 1};
    worker->loadPartition(package);

    std::size_t mapped_file_count = 0;
    for (const auto &entry :
         std::filesystem::directory_iterator("/proc/self/fd")) {
      std::error_code error;
      const auto target = std::filesystem::read_symlink(entry.path(), error);
      if (!error && target.string().find(scratch.string()) !=
                        std::string::npos) {
        ++mapped_file_count;
      }
    }
    require(mapped_file_count >= 8,
            "legacy streaming alias must map complete persistent state");

    mcpd3::PartitionSolveRequest request;
    request.round_id = 1;
    request.partition_id = 0;
    require(worker->solveRound(request).lower_bound == 7,
            "legacy streaming alias should solve exactly");
    stopAndJoin(worker.get(), client);
    std::filesystem::remove_all(scratch);
  } catch (...) {
    stopAndJoin(worker.get(), client);
    std::filesystem::remove_all(scratch);
    throw;
  }
}

void remoteWorkerChunksLargePartitionPayload(
    mcpd4::TransportCompression compression,
    const std::string &worker_name) {
  if (compression == mcpd4::TransportCompression::SNAPPY &&
      !mcpd4::snappyCompressionAvailable()) {
    return;
  }
  const auto scratch =
      std::filesystem::temp_directory_path() /
      ("mcpd4-multipart-worker-test-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()));
  std::filesystem::create_directories(scratch);
  mcpd4::WorkerRuntimeOptions runtime_options;
  runtime_options.solver_storage.mode =
      mcpd3::SolverStorageMode::FILE_BACKED_MMAP;
  runtime_options.solver_storage.directory = scratch.string();

  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  WorkerClientThread *client = nullptr;
  auto worker = startRemoteWorker(
      &listener, &client, worker_name, compression, runtime_options);
  try {
    constexpr std::size_t edge_count = 70000;
    mcpd3::PartitionPackage package;
    package.partition_id = 0;
    package.local_node_count = 2;
    package.arcs.resize(2 * edge_count);
    package.arc_capacities.resize(2 * edge_count);
    for (std::size_t edge = 0; edge < edge_count; ++edge) {
      package.arcs[2 * edge] = 0;
      package.arcs[2 * edge + 1] = 1;
      package.arc_capacities[2 * edge] = 1;
      package.arc_capacities[2 * edge + 1] = 1;
    }
    package.terminal_capacities = {1, -1};
    package.local_to_global = {0, 1};
    worker->loadPartition(package);
    require(worker->timingStats().rpc_bytes.partition_load_tx_frame_count >= 10,
            "large package should use multiple bounded transport chunks");
    if (compression == mcpd4::TransportCompression::SNAPPY) {
      require(worker->timingStats().rpc_bytes.tx_compressed_frame_count > 0,
              "large multipart package should compress individual chunks");
    }

    mcpd3::PartitionSolveRequest request;
    request.round_id = 1;
    request.partition_id = 0;
    require(worker->solveRound(request).lower_bound == 1,
            "multipart worker should preserve the exact local problem");

    mcpd3::SolverStorageOptions update_storage;
    update_storage.mode = mcpd3::SolverStorageMode::FILE_BACKED_MMAP;
    update_storage.directory = scratch.string();
    mcpd3::PartitionCapacityUpdate update;
    update.partition_id = package.partition_id;
    update.arc_capacities = mcpd3::SolverArray<mcpd3::Capacity>(
        package.arc_capacities.size(), mcpd3::Capacity(2), update_storage,
        "large_update_arcs");
    update.terminal_capacities = mcpd3::SolverArray<mcpd3::Capacity>(
        /*count=*/2, update_storage, "large_update_terminals");
    update.terminal_capacities[0] = 2;
    update.terminal_capacities[1] = -2;
    update.flow_scale_numerator = 2;
    update.flow_scale_denominator = 1;
    worker->replacePartitionCapacities(update);
    require(worker->timingStats().rpc_bytes.capacity_update_tx_frame_count == 6,
            "large capacity refresh should use bounded multipart frames");
    request.round_id = 2;
    require(worker->solveRound(request).lower_bound == 2,
            "multipart capacity refresh should preserve and scale warm flow");
    stopAndJoin(worker.get(), client);
    std::filesystem::remove_all(scratch);
  } catch (...) {
    stopAndJoin(worker.get(), client);
    std::filesystem::remove_all(scratch);
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

void remoteWorkerStreamsLargeFinalLabelsIntoMappedStorage() {
  constexpr std::size_t node_count = 150000;
  const auto scratch =
      std::filesystem::temp_directory_path() /
      ("mcpd4-full-label-stream-test-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()));
  std::filesystem::create_directories(scratch);
  mcpd4::WorkerRuntimeOptions runtime_options;
  runtime_options.solver_storage.mode =
      mcpd3::SolverStorageMode::FILE_BACKED_MMAP;
  runtime_options.solver_storage.directory = scratch.string();

  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  WorkerClientThread *client = nullptr;
  auto worker = startRemoteWorker(
      &listener, &client, "full-label-stream-worker",
      mcpd4::TransportCompression::NONE, runtime_options);
  try {
    mcpd3::PartitionPackage package;
    package.partition_id = 4;
    package.local_node_count = static_cast<int>(node_count);
    package.terminal_capacities.resize(node_count);
    package.local_to_global.resize(node_count);
    for (std::size_t node = 0; node < node_count; ++node) {
      package.terminal_capacities[node] = node % 2 == 0 ? 1 : -1;
      package.local_to_global[node] = static_cast<int>(1000000 + node);
    }
    worker->loadPartition(package);

    mcpd3::PartitionSolveRequest request;
    request.round_id = 1;
    request.partition_id = package.partition_id;
    const auto solve_result = worker->solveRound(request);
    require(solve_result.full_labels.empty(),
            "bounded recovery setup must not return resident full labels");

    mcpd3::SolverStorageOptions label_storage;
    label_storage.mode = mcpd3::SolverStorageMode::FILE_BACKED_MMAP;
    label_storage.directory = scratch.string();
    mcpd3::SolverArray<mcpd3::NodeLabel> labels(
        node_count, label_storage, "tcp_full_labels");
    worker->copyFullLabels(package.partition_id, /*offset=*/0,
                           labels.data(), labels.size());
    require(labels.isFileBacked(),
            "large remote labels should land directly in mapped storage");
    require(labels[0].global_node_id == 1000000 &&
                labels[0].local_index == 0 &&
                (labels[0].label == 0 || labels[0].label == 1) &&
                labels[node_count - 1].global_node_id ==
                    static_cast<int>(1000000 + node_count - 1) &&
                labels[node_count - 1].local_index ==
                    static_cast<int>(node_count - 1),
            "streamed full labels should preserve endpoint metadata");
    const auto &stats = worker->timingStats();
    require(stats.full_labels_rpc_count == 1,
            "large full label recovery should use one streaming request");
    require(stats.rpc_bytes.full_labels_result_rx_frame_count == 4,
            "150,000 labels should arrive as three chunks and one end frame");

    std::vector<mcpd3::NodeLabel> partial(3);
    worker->copyFullLabels(package.partition_id, /*offset=*/65535,
                           partial.data(), partial.size());
    require(partial[0].local_index == 65535 &&
                partial[2].local_index == 65537,
            "remote full label recovery should preserve partial ranges");
    requireThrowsContaining(
        [&] {
          mcpd3::NodeLabel label;
          worker->copyFullLabels(package.partition_id, node_count, &label, 1);
        },
        "outside partition",
        "remote worker should reject an out-of-range full label request");
    requireThrowsContaining(
        [&] {
          worker->copyFullLabels(/*partition_id=*/999, /*offset=*/0, nullptr,
                                 /*count=*/0);
        },
        "unknown partition",
        "remote worker should reject an unknown full label partition");
    requireThrows(
        [&] {
          worker->copyFullLabels(package.partition_id, /*offset=*/0, nullptr,
                                 /*count=*/1);
        },
        "remote label recovery should reject a null destination locally");
    stopAndJoin(worker.get(), client);
    std::filesystem::remove_all(scratch);
  } catch (...) {
    stopAndJoin(worker.get(), client);
    std::filesystem::remove_all(scratch);
    throw;
  }
}

void remoteLabelResponseFails(
    const std::function<std::vector<std::vector<std::uint8_t>>(
        const mcpd4::FullLabelsRequest &)> &response_frames,
    const std::string &expected_error, const std::string &worker_name) {
  auto listener = mcpd4::listenTcpLoopback(/*port=*/0);
  const auto port = mcpd4::localPort(listener);
  std::exception_ptr client_exception;
  std::thread client([&] {
    try {
      auto socket = mcpd4::connectTcp("127.0.0.1", port);
      mcpd4::sendFrameBytes(socket, mcpd4::encodeHello(makeHello(worker_name)));
      const auto request = mcpd4::decodeFullLabelsRequest(
          mcpd4::receiveFrameBytes(socket));
      for (const auto &frame : response_frames(request)) {
        mcpd4::sendFrameBytes(socket, frame);
      }
      (void)mcpd4::decodeStop(mcpd4::receiveFrameBytes(socket));
    } catch (...) {
      client_exception = std::current_exception();
    }
  });
  auto worker = mcpd4::acceptTcpPartitionWorker(&listener, 2s);
  try {
    mcpd3::NodeLabel label;
    requireThrowsContaining(
        [&] {
          worker->copyFullLabels(/*partition_id=*/4, /*offset=*/0, &label,
                                 /*count=*/1);
        },
        expected_error, "malformed full label stream should fail");
    worker->stop(/*reason=*/0, "test complete");
    client.join();
    if (client_exception) {
      std::rethrow_exception(client_exception);
    }
  } catch (...) {
    worker->stop(/*reason=*/1, "test failed");
    if (client.joinable()) {
      client.join();
    }
    throw;
  }
}

void tcpWorkerRejectsMalformedFullLabelResponses() {
  const auto one_label = [] {
    return mcpd3::NodeLabel{/*global_node_id=*/10,
                            /*local_index=*/0,
                            /*label=*/1};
  };
  remoteLabelResponseFails(
      [&](const auto &request) {
        mcpd4::FullLabelsChunk chunk;
        chunk.partition_id = request.partition_id + 1;
        chunk.offset = request.offset;
        chunk.labels = {one_label()};
        return std::vector<std::vector<std::uint8_t>>{
            mcpd4::encodeFullLabelsChunk(chunk)};
      },
      "chunk partition id mismatch", "wrong-label-partition-worker");
  remoteLabelResponseFails(
      [&](const auto &request) {
        mcpd4::FullLabelsChunk chunk;
        chunk.partition_id = request.partition_id;
        chunk.offset = request.offset + 1;
        chunk.labels = {one_label()};
        return std::vector<std::vector<std::uint8_t>>{
            mcpd4::encodeFullLabelsChunk(chunk)};
      },
      "offset is not contiguous", "wrong-label-offset-worker");
  remoteLabelResponseFails(
      [&](const auto &request) {
        mcpd4::FullLabelsChunk chunk;
        chunk.partition_id = request.partition_id;
        chunk.offset = request.offset;
        chunk.labels = {one_label(), one_label()};
        return std::vector<std::vector<std::uint8_t>>{
            mcpd4::encodeFullLabelsChunk(chunk)};
      },
      "exceeds requested range", "oversized-label-chunk-worker");
  remoteLabelResponseFails(
      [&](const auto &request) {
        return std::vector<std::vector<std::uint8_t>>{
            mcpd4::encodeFullLabelsEnd(
                mcpd4::FullLabelsEnd{request.partition_id})};
      },
      "ended early", "early-label-end-worker");
  remoteLabelResponseFails(
      [&](const auto &request) {
        return std::vector<std::vector<std::uint8_t>>{
            mcpd4::encodeFullLabelsEnd(
                mcpd4::FullLabelsEnd{request.partition_id + 1})};
      },
      "end partition id mismatch", "wrong-label-end-worker");
  remoteLabelResponseFails(
      [&](const auto &) {
        return std::vector<std::vector<std::uint8_t>>{
            mcpd4::encodeReady(mcpd4::ReadyMessage{"unexpected"})};
      },
      "expected FULL_LABELS_CHUNK", "unexpected-label-frame-worker");
}

void remoteWorkerCoordinatorSolvesRegularizedAgreement() {
  const auto scratch =
      std::filesystem::temp_directory_path() /
      ("mcpd4-coordinator-label-test-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()));
  std::filesystem::create_directories(scratch);
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
    options.collect_final_labels = true;
    options.final_label_storage.mode =
        mcpd3::SolverStorageMode::FILE_BACKED_MMAP;
    options.final_label_storage.directory = scratch.string();
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
    require(result.final_labels.isFileBacked() &&
                result.final_labels.size() == 2 &&
                result.final_labels[0].label == result.final_labels[1].label,
            "remote coordinator should recover agreeing labels into mapping");
    require(remote_ptr->timingStats().solve_batch_rpc_count ==
                result.total_iterations + 1,
            "final label recovery should perform the same final local solve");
    require(remote_ptr->timingStats().partition_solve_call_count ==
                result.total_iterations * 2 + 2,
            "single remote worker should solve both partitions in each batch");
    require(remote_ptr->timingStats().full_labels_rpc_count == 2,
            "coordinator should stream each partition labeling separately");
    remote_ptr->stop(/*reason=*/0, "coordinator test complete");
    client->joinAndRethrow();
    delete client;
    std::filesystem::remove_all(scratch);
  } catch (...) {
    if (remote_ptr != nullptr) {
      remote_ptr->stop(/*reason=*/1, "coordinator test failed");
    }
    if (client != nullptr) {
      client->joinAndRethrow();
      delete client;
    }
    std::filesystem::remove_all(scratch);
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
    twoRemoteWorkersSolveCollectivePcgSystem();
    loadPartitionDisconnectReportsWorkerAndPartitionContext();
    remoteWorkerScalesLoadedObjective();
    remoteWorkerReplacesCapacitiesAndPreservesWarmState();
    remoteWorkerFileBacksCompleteSolverState();
    legacyStreamingAliasUsesPersistentFileBackedWorker();
    remoteWorkerRejectsMalformedMultipartSequences();
    remoteWorkerRejectsMalformedCapacityUpdateSequences();
    remoteWorkerChunksLargePartitionPayload(
        mcpd4::TransportCompression::NONE, "multipart-worker");
    remoteWorkerChunksLargePartitionPayload(
        mcpd4::TransportCompression::SNAPPY,
        "multipart-snappy-worker");
    remoteWorkerSolvesConfiguredPrecisionExtreme();
    remoteWorkerSaturatesScaleObjectiveOverflow();
    remoteWorkerSolvesExplicitBatch();
    remoteWorkerDeltaEncodingReducesRepeatedSolveBytes();
    remoteWorkerUsesSnappyCompression();
    remoteWorkerStreamsLargeFinalLabelsIntoMappedStorage();
    tcpWorkerRejectsMalformedFullLabelResponses();
    remoteWorkerCoordinatorSolvesRegularizedAgreement();
    remoteWorkerCoordinatorPromotesObjectiveScale();
  } catch (const std::exception &e) {
    std::cerr << "tcp_loopback_test failed: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
