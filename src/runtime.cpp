#include <mcpd4/runtime.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unistd.h>

namespace mcpd4 {
namespace {

constexpr std::size_t kPartitionPackageChunkElements = 64 * 1024;

bool hostIsLittleEndian() {
  const std::uint16_t value = 1;
  return *reinterpret_cast<const std::uint8_t *>(&value) == 1;
}

void validateHello(const HelloMessage &hello) {
  if (hello.protocol_version != kProtocolVersion) {
    throw std::runtime_error("unsupported worker protocol version");
  }
  if (hello.capacity_mode != configuredCapacityMode()) {
    throw std::runtime_error("worker capacity precision does not match coordinator");
  }
  if (!hello.little_endian) {
    throw std::runtime_error("worker must use little-endian protocol encoding");
  }
}

std::runtime_error remoteError(const std::vector<std::uint8_t> &frame) {
  const auto error = decodeError(frame);
  return std::runtime_error("remote worker error: " + error.message);
}

bool compressionFeatureEnabled(const HelloMessage &hello,
                               std::uint64_t feature) {
  return (hello.feature_bits & feature) != 0;
}

std::string partitionPackageSummary(const mcpd3::PartitionPackage &package) {
  std::ostringstream out;
  out << "partition_id " << package.partition_id
      << " local_nodes " << package.local_node_count
      << " arc_int_count " << package.arcs.size()
      << " arc_capacity_count " << package.arc_capacities.size()
      << " terminal_count " << package.terminal_capacities.size()
      << " local_to_global_count " << package.local_to_global.size()
      << " constraint_endpoint_count "
      << package.constraint_endpoints.size();
  return out.str();
}

void recordFrameSent(RpcByteStats *stats, MessageType type,
                     const FrameTransferStats &transfer) {
  if (stats == nullptr) {
    return;
  }
  const auto bytes = transfer.logical_bytes;
  stats->tx_bytes_total += bytes;
  stats->tx_wire_bytes_total += transfer.wire_bytes;
  stats->compression_wall_us += transfer.compression_wall_us;
  if (transfer.compression_requested) {
    if (transfer.compressed) {
      ++stats->tx_compressed_frame_count;
    } else {
      ++stats->tx_stored_frame_count;
    }
  }
  switch (type) {
  case MessageType::HELLO:
    stats->hello_tx_bytes += bytes;
    break;
  case MessageType::PARTITION_PACKAGE:
  case MessageType::PARTITION_PACKAGE_BEGIN:
  case MessageType::PARTITION_PACKAGE_CHUNK:
  case MessageType::PARTITION_PACKAGE_END:
    stats->partition_load_tx_bytes += bytes;
    ++stats->partition_load_tx_frame_count;
    break;
  case MessageType::SOLVE_ROUND_REQUEST:
  case MessageType::SOLVE_ROUND_BATCH_REQUEST:
    stats->solve_request_tx_bytes += bytes;
    break;
  case MessageType::SOLVE_ROUND_RESULT:
  case MessageType::SOLVE_ROUND_BATCH_RESULT:
    stats->solve_result_tx_bytes += bytes;
    break;
  case MessageType::SCALE_OBJECTIVE:
    stats->scale_objective_tx_bytes += bytes;
    break;
  case MessageType::REPLACE_PARTITION_CAPACITIES:
    stats->capacity_update_tx_bytes += bytes;
    break;
  case MessageType::READY:
    stats->ready_tx_bytes += bytes;
    break;
  case MessageType::STOP:
    stats->stop_tx_bytes += bytes;
    break;
  case MessageType::ERROR:
    stats->error_tx_bytes += bytes;
    break;
  case MessageType::ALPHA_UPDATE:
    break;
  case MessageType::LINEAR_STRUCTURE:
  case MessageType::LINEAR_SYSTEM_VALUES:
  case MessageType::LINEAR_INITIALIZE_REQUEST:
  case MessageType::LINEAR_INITIALIZE_RESULT:
  case MessageType::LINEAR_MULTIPLY_REQUEST:
  case MessageType::LINEAR_MULTIPLY_RESULT:
  case MessageType::LINEAR_ALPHA_REQUEST:
  case MessageType::LINEAR_ALPHA_RESULT:
  case MessageType::LINEAR_BETA_REQUEST:
  case MessageType::LINEAR_BETA_RESULT:
  case MessageType::LINEAR_SOLUTION_REQUEST:
  case MessageType::LINEAR_SOLUTION_RESULT:
    stats->linear_tx_bytes += bytes;
    break;
  }
}

void recordFrameReceived(RpcByteStats *stats, MessageType type,
                         const FrameTransferStats &transfer) {
  if (stats == nullptr) {
    return;
  }
  const auto bytes = transfer.logical_bytes;
  stats->rx_bytes_total += bytes;
  stats->rx_wire_bytes_total += transfer.wire_bytes;
  stats->decompression_wall_us += transfer.decompression_wall_us;
  if (transfer.compression_requested) {
    if (transfer.compressed) {
      ++stats->rx_compressed_frame_count;
    } else {
      ++stats->rx_stored_frame_count;
    }
  }
  switch (type) {
  case MessageType::HELLO:
    stats->hello_rx_bytes += bytes;
    break;
  case MessageType::PARTITION_PACKAGE:
  case MessageType::PARTITION_PACKAGE_BEGIN:
  case MessageType::PARTITION_PACKAGE_CHUNK:
  case MessageType::PARTITION_PACKAGE_END:
    stats->partition_load_rx_bytes += bytes;
    ++stats->partition_load_rx_frame_count;
    break;
  case MessageType::SOLVE_ROUND_REQUEST:
  case MessageType::SOLVE_ROUND_BATCH_REQUEST:
    stats->solve_request_rx_bytes += bytes;
    break;
  case MessageType::SOLVE_ROUND_RESULT:
  case MessageType::SOLVE_ROUND_BATCH_RESULT:
    stats->solve_result_rx_bytes += bytes;
    break;
  case MessageType::SCALE_OBJECTIVE:
    stats->scale_objective_rx_bytes += bytes;
    break;
  case MessageType::REPLACE_PARTITION_CAPACITIES:
    stats->capacity_update_rx_bytes += bytes;
    break;
  case MessageType::READY:
    stats->ready_rx_bytes += bytes;
    break;
  case MessageType::STOP:
    stats->stop_rx_bytes += bytes;
    break;
  case MessageType::ERROR:
    stats->error_rx_bytes += bytes;
    break;
  case MessageType::ALPHA_UPDATE:
    break;
  case MessageType::LINEAR_STRUCTURE:
  case MessageType::LINEAR_SYSTEM_VALUES:
  case MessageType::LINEAR_INITIALIZE_REQUEST:
  case MessageType::LINEAR_INITIALIZE_RESULT:
  case MessageType::LINEAR_MULTIPLY_REQUEST:
  case MessageType::LINEAR_MULTIPLY_RESULT:
  case MessageType::LINEAR_ALPHA_REQUEST:
  case MessageType::LINEAR_ALPHA_RESULT:
  case MessageType::LINEAR_BETA_REQUEST:
  case MessageType::LINEAR_BETA_RESULT:
  case MessageType::LINEAR_SOLUTION_REQUEST:
  case MessageType::LINEAR_SOLUTION_RESULT:
    stats->linear_rx_bytes += bytes;
    break;
  }
}

Frame receiveTypedFrame(const SocketHandle &socket,
                        std::vector<std::uint8_t> *frame_bytes,
                        RpcByteStats *stats = nullptr,
                        TransportCompression compression =
                            TransportCompression::NONE) {
  FrameTransferStats transfer;
  *frame_bytes =
      receiveFrameBytes(socket, kDefaultMaxFrameBytes, compression,
                        &transfer);
  const auto frame = decodeFrame(*frame_bytes);
  recordFrameReceived(stats, frame.type, transfer);
  return frame;
}

ReadyMessage receiveReadyOrThrow(const SocketHandle &socket,
                                 RpcByteStats *stats = nullptr,
                                 TransportCompression compression =
                                     TransportCompression::NONE) {
  std::vector<std::uint8_t> frame_bytes;
  const auto frame =
      receiveTypedFrame(socket, &frame_bytes, stats, compression);
  if (frame.type == MessageType::ERROR) {
    throw remoteError(frame_bytes);
  }
  if (frame.type != MessageType::READY) {
    throw std::runtime_error("expected READY response from worker");
  }
  return decodeReady(frame_bytes);
}

FrameTransferStats sendReady(const SocketHandle &socket,
                             const std::string &worker_name,
                             TransportCompression compression) {
  ReadyMessage ready;
  ready.worker_name = worker_name;
  const auto frame = encodeReady(ready);
  FrameTransferStats transfer;
  sendFrameBytes(socket, frame, compression, &transfer);
  return transfer;
}

FrameTransferStats sendErrorBestEffort(const SocketHandle &socket,
                                       std::uint32_t code,
                                       const std::string &message,
                                       TransportCompression compression) {
  FrameTransferStats transfer;
  try {
    ErrorMessage error;
    error.code = code;
    error.message = message;
    const auto frame = encodeError(error);
    sendFrameBytes(socket, frame, compression, &transfer);
  } catch (...) {
    transfer = {};
  }
  return transfer;
}

std::uint64_t hostRamGb() {
  const long pages = ::sysconf(_SC_PHYS_PAGES);
  const long page_size = ::sysconf(_SC_PAGE_SIZE);
  if (pages <= 0 || page_size <= 0) {
    return 0;
  }
  const auto bytes =
      static_cast<long double>(pages) * static_cast<long double>(page_size);
  return static_cast<std::uint64_t>(bytes / (1024.0L * 1024.0L * 1024.0L));
}

std::uint32_t hostCpuCount() {
  return std::max(1u, std::thread::hardware_concurrency());
}

std::string defaultTempPath() {
  const char *tmpdir = std::getenv("TMPDIR");
  return tmpdir != nullptr && tmpdir[0] != '\0' ? std::string(tmpdir)
                                                 : std::string("/tmp");
}

std::uint64_t elapsedUs(std::chrono::steady_clock::time_point start) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start)
          .count());
}

} // namespace

TcpPartitionWorker::TcpPartitionWorker(SocketHandle socket, HelloMessage hello,
                                       std::uint64_t hello_rx_bytes,
                                       TransportCompression compression)
    : socket_(std::move(socket)), hello_(std::move(hello)),
      compression_(compression) {
  validateHello(hello_);
  FrameTransferStats transfer;
  transfer.logical_bytes = hello_rx_bytes;
  transfer.wire_bytes = hello_rx_bytes;
  recordFrameReceived(&timing_stats_.rpc_bytes, MessageType::HELLO, transfer);
}

void TcpPartitionWorker::loadPartition(
    const mcpd3::PartitionPackage &package) {
  const auto start = std::chrono::steady_clock::now();
  const auto package_summary = partitionPackageSummary(package);
  const auto starting_frame_count =
      timing_stats_.rpc_bytes.partition_load_tx_frame_count;
  std::uint64_t frame_logical_bytes = 0;
  FrameTransferStats aggregate_transfer;
  try {
    auto send_package_frame = [&](MessageType type,
                                  const std::vector<std::uint8_t> &frame) {
      FrameTransferStats transfer;
      sendFrameBytes(socket_, frame, compression_, &transfer);
      recordFrameSent(&timing_stats_.rpc_bytes, type, transfer);
      aggregate_transfer.logical_bytes += transfer.logical_bytes;
      aggregate_transfer.wire_bytes += transfer.wire_bytes;
      aggregate_transfer.compression_wall_us +=
          transfer.compression_wall_us;
    };
    const auto header = makePartitionPackageTransferHeader(package);
    send_package_frame(
        MessageType::PARTITION_PACKAGE_BEGIN,
        encodePartitionPackageTransferBegin(header));
    for (std::size_t section_index = 0;
         section_index < kPartitionPackageSectionCount; ++section_index) {
      const auto section =
          static_cast<PartitionPackageSection>(section_index);
      const auto total = header.section_counts[section_index];
      for (std::uint64_t offset = 0; offset < total;) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
            kPartitionPackageChunkElements, total - offset));
        send_package_frame(
            MessageType::PARTITION_PACKAGE_CHUNK,
            encodePartitionPackageTransferChunk(package, section, offset,
                                                count));
        offset += count;
      }
    }
    send_package_frame(
        MessageType::PARTITION_PACKAGE_END,
        encodePartitionPackageTransferEnd(
            PartitionPackageTransferEnd{package.partition_id}));
    frame_logical_bytes = aggregate_transfer.logical_bytes;
    std::cerr << "mcpd4_load_partition_begin worker "
              << hello_.worker_name << " " << package_summary
              << " frame_logical_bytes " << frame_logical_bytes << "\n";
    (void)receiveReadyOrThrow(socket_, &timing_stats_.rpc_bytes,
                              compression_);
    temporal_state_.resetPartition(package.partition_id);
    const auto elapsed = elapsedUs(start);
    timing_stats_.load_partition_rpc_wall_us += elapsed;
    ++timing_stats_.load_partition_rpc_count;
    partition_ids_.push_back(package.partition_id);
    std::cerr << "mcpd4_load_partition_done worker "
              << hello_.worker_name << " partition_id "
              << package.partition_id << " elapsed_us " << elapsed
              << " frame_logical_bytes " << aggregate_transfer.logical_bytes
              << " frame_wire_bytes " << aggregate_transfer.wire_bytes
              << " frame_count "
              << timing_stats_.rpc_bytes.partition_load_tx_frame_count -
                     starting_frame_count
              << "\n";
  } catch (const std::exception &e) {
    std::cerr << "mcpd4_load_partition_failed worker "
              << hello_.worker_name << " " << package_summary
              << " elapsed_us " << elapsedUs(start)
              << " frame_logical_bytes " << frame_logical_bytes
              << " error " << e.what() << "\n";
    std::ostringstream message;
    message << "worker " << hello_.worker_name
            << " failed loading partition " << package.partition_id
            << " (" << package_summary
            << " frame_logical_bytes " << frame_logical_bytes
            << "): " << e.what();
    throw std::runtime_error(message.str());
  }
}

mcpd3::PartitionWorkerResourceEstimate
TcpPartitionWorker::resourceEstimate() const {
  mcpd3::PartitionWorkerResourceEstimate resources;
  resources.cpu_count =
      hello_.cpu_count >
              static_cast<std::uint32_t>(std::numeric_limits<int>::max())
          ? std::numeric_limits<int>::max()
          : static_cast<int>(hello_.cpu_count);
  resources.ram_gb =
      hello_.ram_gb >
              static_cast<std::uint64_t>(std::numeric_limits<long>::max())
          ? std::numeric_limits<long>::max()
          : static_cast<long>(hello_.ram_gb);
  return resources;
}

mcpd3::PartitionSolveResult TcpPartitionWorker::solveRound(
    const mcpd3::PartitionSolveRequest &request) {
  const auto start = std::chrono::steady_clock::now();
  const auto request_frame =
      encodeDeltaSolveRoundRequest(request, &temporal_state_);
  FrameTransferStats transfer;
  sendFrameBytes(socket_, request_frame, compression_, &transfer);
  recordFrameSent(&timing_stats_.rpc_bytes,
                  MessageType::SOLVE_ROUND_REQUEST,
                  transfer);
  std::vector<std::uint8_t> frame_bytes;
  const auto frame =
      receiveTypedFrame(socket_, &frame_bytes, &timing_stats_.rpc_bytes,
                        compression_);
  if (frame.type == MessageType::ERROR) {
    throw remoteError(frame_bytes);
  }
  if (frame.type != MessageType::SOLVE_ROUND_RESULT) {
    throw std::runtime_error("expected SOLVE_ROUND_RESULT from worker");
  }
  const auto timed =
      decodeDeltaTimedSolveRoundResult(frame_bytes, &temporal_state_);
  timing_stats_.solve_round_rpc_wall_us += elapsedUs(start);
  timing_stats_.solve_round_worker_wall_us += timed.worker_solve_wall_us;
  ++timing_stats_.partition_solve_call_count;
  return timed.result;
}

std::vector<mcpd3::PartitionSolveResult> TcpPartitionWorker::solveRoundBatch(
    const std::vector<mcpd3::PartitionSolveRequest> &requests) {
  if (requests.empty()) {
    return {};
  }
  const auto start = std::chrono::steady_clock::now();
  const auto request_frame =
      encodeDeltaSolveRoundBatchRequest(requests, &temporal_state_);
  FrameTransferStats transfer;
  sendFrameBytes(socket_, request_frame, compression_, &transfer);
  recordFrameSent(&timing_stats_.rpc_bytes,
                  MessageType::SOLVE_ROUND_BATCH_REQUEST,
                  transfer);
  std::vector<std::uint8_t> frame_bytes;
  const auto frame =
      receiveTypedFrame(socket_, &frame_bytes, &timing_stats_.rpc_bytes,
                        compression_);
  if (frame.type == MessageType::ERROR) {
    throw remoteError(frame_bytes);
  }
  if (frame.type != MessageType::SOLVE_ROUND_BATCH_RESULT) {
    throw std::runtime_error("expected SOLVE_ROUND_BATCH_RESULT from worker");
  }
  const auto timed =
      decodeDeltaTimedSolveRoundBatchResult(frame_bytes, &temporal_state_);
  timing_stats_.solve_round_rpc_wall_us += elapsedUs(start);
  timing_stats_.solve_round_worker_wall_us += timed.worker_solve_wall_us;
  timing_stats_.partition_solve_call_count +=
      static_cast<long>(timed.results.size());
  ++timing_stats_.solve_batch_rpc_count;
  return timed.results;
}

void TcpPartitionWorker::scaleObjective(long factor,
                                        bool saturate_capacity_overflow) {
  const auto start = std::chrono::steady_clock::now();
  ScaleObjectiveMessage message;
  message.factor = factor;
  message.saturate_capacity_overflow = saturate_capacity_overflow;
  const auto frame = encodeScaleObjective(message);
  FrameTransferStats transfer;
  sendFrameBytes(socket_, frame, compression_, &transfer);
  recordFrameSent(&timing_stats_.rpc_bytes, MessageType::SCALE_OBJECTIVE,
                  transfer);
  (void)receiveReadyOrThrow(socket_, &timing_stats_.rpc_bytes,
                            compression_);
  temporal_state_.reset();
  timing_stats_.scale_objective_rpc_wall_us += elapsedUs(start);
  ++timing_stats_.scale_objective_rpc_count;
}

void TcpPartitionWorker::replacePartitionCapacities(
    const mcpd3::PartitionCapacityUpdate &update) {
  const auto start = std::chrono::steady_clock::now();
  const auto frame = encodePartitionCapacityUpdate(update);
  FrameTransferStats transfer;
  sendFrameBytes(socket_, frame, compression_, &transfer);
  recordFrameSent(&timing_stats_.rpc_bytes,
                  MessageType::REPLACE_PARTITION_CAPACITIES, transfer);
  (void)receiveReadyOrThrow(socket_, &timing_stats_.rpc_bytes, compression_);
  temporal_state_.resetPartition(update.partition_id);
  timing_stats_.capacity_update_rpc_wall_us += elapsedUs(start);
  ++timing_stats_.capacity_update_rpc_count;
}

void TcpPartitionWorker::loadLinearStructure(
    const LinearStructureMessage &message) {
  const auto start = std::chrono::steady_clock::now();
  const auto frame = encodeLinearStructure(message);
  FrameTransferStats transfer;
  sendFrameBytes(socket_, frame, compression_, &transfer);
  recordFrameSent(&timing_stats_.rpc_bytes, MessageType::LINEAR_STRUCTURE,
                  transfer);
  (void)receiveReadyOrThrow(socket_, &timing_stats_.rpc_bytes, compression_);
  timing_stats_.linear_rpc_wall_us += elapsedUs(start);
  ++timing_stats_.linear_structure_rpc_count;
}

void TcpPartitionWorker::loadLinearSystem(
    const LinearSystemValuesMessage &message) {
  const auto start = std::chrono::steady_clock::now();
  const auto frame = encodeLinearSystemValues(message);
  FrameTransferStats transfer;
  sendFrameBytes(socket_, frame, compression_, &transfer);
  recordFrameSent(&timing_stats_.rpc_bytes, MessageType::LINEAR_SYSTEM_VALUES,
                  transfer);
  (void)receiveReadyOrThrow(socket_, &timing_stats_.rpc_bytes, compression_);
  timing_stats_.linear_rpc_wall_us += elapsedUs(start);
  ++timing_stats_.linear_system_rpc_count;
}

LinearInitializeResult TcpPartitionWorker::initializeLinear(
    const LinearVectorRequest &request) {
  const auto start = std::chrono::steady_clock::now();
  const auto request_frame = encodeLinearInitializeRequest(request);
  FrameTransferStats transfer;
  sendFrameBytes(socket_, request_frame, compression_, &transfer);
  recordFrameSent(&timing_stats_.rpc_bytes,
                  MessageType::LINEAR_INITIALIZE_REQUEST, transfer);
  std::vector<std::uint8_t> frame_bytes;
  const auto frame = receiveTypedFrame(socket_, &frame_bytes,
                                       &timing_stats_.rpc_bytes, compression_);
  if (frame.type == MessageType::ERROR) {
    throw remoteError(frame_bytes);
  }
  if (frame.type != MessageType::LINEAR_INITIALIZE_RESULT) {
    throw std::runtime_error("expected LINEAR_INITIALIZE_RESULT from worker");
  }
  const auto message = decodeLinearInitializeResult(frame_bytes);
  if (message.partition_id != request.partition_id) {
    throw std::runtime_error("linear initialize partition id mismatch");
  }
  timing_stats_.linear_rpc_wall_us += elapsedUs(start);
  ++timing_stats_.linear_initialize_rpc_count;
  return message.result;
}

LinearMultiplyResult TcpPartitionWorker::multiplyLinear(
    const LinearVectorRequest &request) {
  const auto start = std::chrono::steady_clock::now();
  const auto request_frame = encodeLinearMultiplyRequest(request);
  FrameTransferStats transfer;
  sendFrameBytes(socket_, request_frame, compression_, &transfer);
  recordFrameSent(&timing_stats_.rpc_bytes,
                  MessageType::LINEAR_MULTIPLY_REQUEST, transfer);
  std::vector<std::uint8_t> frame_bytes;
  const auto frame = receiveTypedFrame(socket_, &frame_bytes,
                                       &timing_stats_.rpc_bytes, compression_);
  if (frame.type == MessageType::ERROR) {
    throw remoteError(frame_bytes);
  }
  if (frame.type != MessageType::LINEAR_MULTIPLY_RESULT) {
    throw std::runtime_error("expected LINEAR_MULTIPLY_RESULT from worker");
  }
  const auto message = decodeLinearMultiplyResult(frame_bytes);
  if (message.partition_id != request.partition_id) {
    throw std::runtime_error("linear multiply partition id mismatch");
  }
  timing_stats_.linear_rpc_wall_us += elapsedUs(start);
  ++timing_stats_.linear_multiply_rpc_count;
  return message.result;
}

LinearAlphaUpdateResult TcpPartitionWorker::updateLinearAlpha(
    const LinearScalarRequest &request) {
  const auto start = std::chrono::steady_clock::now();
  const auto request_frame = encodeLinearAlphaRequest(request);
  FrameTransferStats transfer;
  sendFrameBytes(socket_, request_frame, compression_, &transfer);
  recordFrameSent(&timing_stats_.rpc_bytes, MessageType::LINEAR_ALPHA_REQUEST,
                  transfer);
  std::vector<std::uint8_t> frame_bytes;
  const auto frame = receiveTypedFrame(socket_, &frame_bytes,
                                       &timing_stats_.rpc_bytes, compression_);
  if (frame.type == MessageType::ERROR) {
    throw remoteError(frame_bytes);
  }
  if (frame.type != MessageType::LINEAR_ALPHA_RESULT) {
    throw std::runtime_error("expected LINEAR_ALPHA_RESULT from worker");
  }
  const auto message = decodeLinearAlphaResult(frame_bytes);
  if (message.partition_id != request.partition_id) {
    throw std::runtime_error("linear alpha partition id mismatch");
  }
  timing_stats_.linear_rpc_wall_us += elapsedUs(start);
  ++timing_stats_.linear_alpha_rpc_count;
  return message.result;
}

LinearBetaUpdateResult TcpPartitionWorker::updateLinearBeta(
    const LinearScalarRequest &request) {
  const auto start = std::chrono::steady_clock::now();
  const auto request_frame = encodeLinearBetaRequest(request);
  FrameTransferStats transfer;
  sendFrameBytes(socket_, request_frame, compression_, &transfer);
  recordFrameSent(&timing_stats_.rpc_bytes, MessageType::LINEAR_BETA_REQUEST,
                  transfer);
  std::vector<std::uint8_t> frame_bytes;
  const auto frame = receiveTypedFrame(socket_, &frame_bytes,
                                       &timing_stats_.rpc_bytes, compression_);
  if (frame.type == MessageType::ERROR) {
    throw remoteError(frame_bytes);
  }
  if (frame.type != MessageType::LINEAR_BETA_RESULT) {
    throw std::runtime_error("expected LINEAR_BETA_RESULT from worker");
  }
  const auto message = decodeLinearBetaResult(frame_bytes);
  if (message.partition_id != request.partition_id) {
    throw std::runtime_error("linear beta partition id mismatch");
  }
  timing_stats_.linear_rpc_wall_us += elapsedUs(start);
  ++timing_stats_.linear_beta_rpc_count;
  return message.result;
}

std::vector<double> TcpPartitionWorker::linearSolution(int partition_id) {
  const auto start = std::chrono::steady_clock::now();
  const auto request_frame =
      encodeLinearSolutionRequest(LinearPartitionRequest{partition_id});
  FrameTransferStats transfer;
  sendFrameBytes(socket_, request_frame, compression_, &transfer);
  recordFrameSent(&timing_stats_.rpc_bytes,
                  MessageType::LINEAR_SOLUTION_REQUEST, transfer);
  std::vector<std::uint8_t> frame_bytes;
  const auto frame = receiveTypedFrame(socket_, &frame_bytes,
                                       &timing_stats_.rpc_bytes, compression_);
  if (frame.type == MessageType::ERROR) {
    throw remoteError(frame_bytes);
  }
  if (frame.type != MessageType::LINEAR_SOLUTION_RESULT) {
    throw std::runtime_error("expected LINEAR_SOLUTION_RESULT from worker");
  }
  auto message = decodeLinearSolutionResult(frame_bytes);
  if (message.partition_id != partition_id) {
    throw std::runtime_error("linear solution partition id mismatch");
  }
  timing_stats_.linear_rpc_wall_us += elapsedUs(start);
  ++timing_stats_.linear_solution_rpc_count;
  return std::move(message.solution);
}

void TcpPartitionWorker::stop(std::uint32_t reason,
                              const std::string &message) {
  if (!socket_.valid()) {
    return;
  }
  try {
    StopMessage stop;
    stop.reason = reason;
    stop.message = message;
    const auto frame = encodeStop(stop);
    FrameTransferStats transfer;
    sendFrameBytes(socket_, frame, compression_, &transfer);
    recordFrameSent(&timing_stats_.rpc_bytes, MessageType::STOP,
                    transfer);
  } catch (...) {
  }
  socket_.reset();
}

TcpPartitionWorkerStatusSnapshot TcpPartitionWorker::statusSnapshot() const {
  TcpPartitionWorkerStatusSnapshot snapshot;
  snapshot.worker_name = hello_.worker_name;
  snapshot.cpu_count = hello_.cpu_count;
  snapshot.ram_gb = hello_.ram_gb;
  snapshot.partition_ids = partition_ids_;
  snapshot.timing = timing_stats_;
  return snapshot;
}

HelloMessage makeDefaultHello(const std::string &worker_name) {
  HelloMessage hello;
  hello.protocol_version = kProtocolVersion;
  hello.capacity_mode = configuredCapacityMode();
  hello.worker_name = worker_name.empty() ? "mcpd4-worker" : worker_name;
  hello.cpu_count = hostCpuCount();
  hello.ram_gb = hostRamGb();
  hello.feature_bits = 0;
  hello.temp_path = defaultTempPath();
#ifndef NDEBUG
  hello.debug_build = true;
#else
  hello.debug_build = false;
#endif
  hello.little_endian = hostIsLittleEndian();
  return hello;
}

void runWorkerClient(const std::string &host, std::uint16_t port,
                     const HelloMessage &hello,
                     const WorkerRuntimeStatusHooks &status_hooks,
                     TransportCompression compression,
                     WorkerRuntimeOptions runtime_options) {
  validateHello(hello);
  if (compression == TransportCompression::SNAPPY) {
    if (!snappyCompressionAvailable()) {
      throw std::runtime_error(
          "snappy compression requested but this binary was built without "
          "MCPD4_ENABLE_SNAPPY");
    }
    if (!compressionFeatureEnabled(hello, kFeatureSnappyCompression)) {
      throw std::runtime_error(
          "snappy compression requested but worker HELLO does not advertise "
          "the snappy feature bit");
    }
  }
  SocketHandle socket = connectTcp(host, port);
  const auto hello_frame = encodeHello(hello);
  FrameTransferStats hello_transfer;
  sendFrameBytes(socket, hello_frame, TransportCompression::NONE,
                 &hello_transfer);
  if (status_hooks.on_frame_sent) {
    status_hooks.on_frame_sent(MessageType::HELLO, hello_transfer);
  }
  if (status_hooks.on_phase) {
    status_hooks.on_phase("connected");
  }

  // The historical streaming worker evicted and reconstructed solvers, which
  // changes warm-state execution. Keep legacy runtime options as a storage
  // alias, but always execute the same persistent worker algorithm.
  if (runtime_options.streaming_partitions) {
    if (runtime_options.solver_storage.mode ==
        mcpd3::SolverStorageMode::RESIDENT) {
      runtime_options.solver_storage.mode =
          mcpd3::SolverStorageMode::FILE_BACKED_MMAP;
    }
    if (runtime_options.solver_storage.mode ==
            mcpd3::SolverStorageMode::FILE_BACKED_MMAP &&
        runtime_options.solver_storage.directory.empty()) {
      runtime_options.solver_storage.directory =
          runtime_options.streaming_directory;
    }
    if (runtime_options.solver_storage.mode ==
            mcpd3::SolverStorageMode::FILE_BACKED_MMAP &&
        runtime_options.solver_storage.directory.empty()) {
      throw std::runtime_error(
          "legacy streaming alias requires a file-backed storage directory");
    }
  }
  auto worker = std::make_unique<mcpd3::InProcessPartitionWorker>(
      runtime_options.solver_storage);
  TemporalSolveCodecState temporal_state;
  std::unordered_map<int, LinearStructureMessage> linear_structures;
  std::unordered_map<int, std::unique_ptr<ResidentLinearPartition>>
      linear_partitions;
  std::optional<PartitionPackageAssembler> package_assembler;
  std::uint64_t package_transfer_logical_bytes = 0;
  auto load_complete_package = [&](mcpd3::PartitionPackage package,
                                   std::uint64_t logical_bytes) {
    const int partition_id = package.partition_id;
    if (status_hooks.on_partition_loading) {
      status_hooks.on_partition_loading(package, logical_bytes);
    }
    std::cerr << "mcpd4_worker_load_partition_begin worker "
              << hello.worker_name << " "
              << partitionPackageSummary(package)
              << " frame_logical_bytes " << logical_bytes << "\n";
    worker->loadPartition(std::move(package));
    temporal_state.resetPartition(partition_id);
    if (status_hooks.on_partition_loaded) {
      status_hooks.on_partition_loaded(partition_id);
    }
    if (status_hooks.on_phase) {
      status_hooks.on_phase("connected");
    }
    const auto transfer = sendReady(socket, hello.worker_name, compression);
    if (status_hooks.on_frame_sent) {
      status_hooks.on_frame_sent(MessageType::READY, transfer);
    }
    std::cerr << "mcpd4_worker_load_partition_done worker "
              << hello.worker_name << " partition_id "
              << partition_id << "\n";
  };
  while (true) {
    try {
      FrameTransferStats receive_transfer;
      const auto frame_bytes =
          receiveFrameBytes(socket, kDefaultMaxFrameBytes, compression,
                            &receive_transfer);
      const auto frame = decodeFrame(frame_bytes);
      if (status_hooks.on_frame_received) {
        status_hooks.on_frame_received(frame.type, receive_transfer);
      }
      if (package_assembler.has_value() &&
          frame.type != MessageType::PARTITION_PACKAGE_CHUNK &&
          frame.type != MessageType::PARTITION_PACKAGE_END) {
        throw std::runtime_error(
            "partition package transfer was interrupted");
      }
      switch (frame.type) {
      case MessageType::PARTITION_PACKAGE:
        {
          if (status_hooks.on_phase) {
            status_hooks.on_phase("loading_partition");
          }
          auto package = decodePartitionPackage(frame_bytes);
          load_complete_package(
              std::move(package),
              static_cast<std::uint64_t>(frame_bytes.size()));
        }
        break;
      case MessageType::PARTITION_PACKAGE_BEGIN:
        {
          if (status_hooks.on_phase) {
            status_hooks.on_phase("loading_partition");
          }
          const auto header = decodePartitionPackageTransferBegin(frame_bytes);
          package_assembler.emplace(header,
                                    runtime_options.solver_storage);
          package_transfer_logical_bytes = receive_transfer.logical_bytes;
        }
        break;
      case MessageType::PARTITION_PACKAGE_CHUNK:
        if (!package_assembler.has_value()) {
          throw std::runtime_error(
              "partition package chunk arrived before begin");
        }
        package_assembler->append(
            decodePartitionPackageTransferChunk(frame_bytes));
        package_transfer_logical_bytes += receive_transfer.logical_bytes;
        break;
      case MessageType::PARTITION_PACKAGE_END:
        {
          if (!package_assembler.has_value()) {
            throw std::runtime_error(
                "partition package end arrived before begin");
          }
          const auto end = decodePartitionPackageTransferEnd(frame_bytes);
          package_transfer_logical_bytes += receive_transfer.logical_bytes;
          auto package = package_assembler->finish();
          if (end.partition_id != package.partition_id) {
            throw std::runtime_error(
                "partition package end id mismatch");
          }
          package_assembler.reset();
          load_complete_package(std::move(package),
                                package_transfer_logical_bytes);
          package_transfer_logical_bytes = 0;
        }
        break;
      case MessageType::SOLVE_ROUND_REQUEST:
        {
          const auto request =
              decodeDeltaSolveRoundRequest(frame_bytes, &temporal_state);
          if (status_hooks.on_solve_start) {
            status_hooks.on_solve_start(request.round_id,
                                        {request.partition_id});
          }
          const auto start = std::chrono::steady_clock::now();
          const auto result = worker->solveRound(request);
          const auto elapsed = elapsedUs(start);
          if (status_hooks.on_solve_done) {
            status_hooks.on_solve_done(elapsed, 1, false);
          }
          const auto result_frame =
              encodeDeltaSolveRoundResultWithTiming(result, elapsed,
                                                    &temporal_state);
          FrameTransferStats transfer;
          sendFrameBytes(socket, result_frame, compression, &transfer);
          if (status_hooks.on_frame_sent) {
            status_hooks.on_frame_sent(
                MessageType::SOLVE_ROUND_RESULT, transfer);
          }
        }
        break;
      case MessageType::SOLVE_ROUND_BATCH_REQUEST:
        {
          const auto requests =
              decodeDeltaSolveRoundBatchRequest(frame_bytes, &temporal_state);
          if (status_hooks.on_solve_start) {
            std::vector<int> partition_ids;
            partition_ids.reserve(requests.size());
            long round_id = 0;
            for (const auto &request : requests) {
              partition_ids.push_back(request.partition_id);
              round_id = request.round_id;
            }
            status_hooks.on_solve_start(round_id, partition_ids);
          }
          const auto start = std::chrono::steady_clock::now();
          const auto results = worker->solveRoundBatch(requests);
          const auto elapsed = elapsedUs(start);
          if (status_hooks.on_solve_done) {
            status_hooks.on_solve_done(
                elapsed, static_cast<long>(results.size()), true);
          }
          const auto result_frame =
              encodeDeltaSolveRoundBatchResultWithTiming(results, elapsed,
                                                         &temporal_state);
          FrameTransferStats transfer;
          sendFrameBytes(socket, result_frame, compression, &transfer);
          if (status_hooks.on_frame_sent) {
            status_hooks.on_frame_sent(
                MessageType::SOLVE_ROUND_BATCH_RESULT, transfer);
          }
        }
        break;
      case MessageType::SCALE_OBJECTIVE:
        {
          const auto message = decodeScaleObjective(frame_bytes);
          if (status_hooks.on_scale_objective) {
            status_hooks.on_scale_objective(
                message.factor, message.saturate_capacity_overflow);
          }
          worker->scaleObjective(message.factor,
                                 message.saturate_capacity_overflow);
          temporal_state.reset();
          if (status_hooks.on_phase) {
            status_hooks.on_phase("connected");
          }
          const auto transfer =
              sendReady(socket, hello.worker_name, compression);
          if (status_hooks.on_frame_sent) {
            status_hooks.on_frame_sent(MessageType::READY, transfer);
          }
        }
        break;
      case MessageType::REPLACE_PARTITION_CAPACITIES:
        {
          const auto update = decodePartitionCapacityUpdate(frame_bytes);
          worker->replacePartitionCapacities(update);
          temporal_state.resetPartition(update.partition_id);
          if (status_hooks.on_phase) {
            status_hooks.on_phase("connected");
          }
          const auto transfer =
              sendReady(socket, hello.worker_name, compression);
          if (status_hooks.on_frame_sent) {
            status_hooks.on_frame_sent(MessageType::READY, transfer);
          }
        }
        break;
      case MessageType::LINEAR_STRUCTURE:
        {
          auto message = decodeLinearStructure(frame_bytes);
          const int partition_id = message.partition_id;
          linear_structures[partition_id] = std::move(message);
          linear_partitions.erase(partition_id);
          const auto transfer = sendReady(socket, hello.worker_name, compression);
          if (status_hooks.on_frame_sent) {
            status_hooks.on_frame_sent(MessageType::READY, transfer);
          }
        }
        break;
      case MessageType::LINEAR_SYSTEM_VALUES:
        {
          auto values = decodeLinearSystemValues(frame_bytes);
          const auto structure_it = linear_structures.find(values.partition_id);
          if (structure_it == linear_structures.end()) {
            throw std::runtime_error(
                "linear structure must be loaded before numerical system");
          }
          const auto &structure = structure_it->second;
          LinearSystemPartition partition;
          partition.partition_id = structure.partition_id;
          partition.owned_global_nodes = structure.owned_global_nodes;
          partition.ghost_global_nodes = structure.ghost_global_nodes;
          partition.row_offsets = structure.row_offsets;
          partition.column_indices = structure.column_indices;
          partition.boundary_owned_local_indices =
              structure.boundary_owned_local_indices;
          partition.values = std::move(values.values);
          partition.rhs = std::move(values.rhs);
          partition.initial_x = std::move(values.initial_x);
          linear_partitions[partition.partition_id] =
              std::make_unique<ResidentLinearPartition>(std::move(partition));
          const auto transfer = sendReady(socket, hello.worker_name, compression);
          if (status_hooks.on_frame_sent) {
            status_hooks.on_frame_sent(MessageType::READY, transfer);
          }
        }
        break;
      case MessageType::LINEAR_INITIALIZE_REQUEST:
        {
          auto request = decodeLinearInitializeRequest(frame_bytes);
          const auto partition_it = linear_partitions.find(request.partition_id);
          if (partition_it == linear_partitions.end()) {
            throw std::runtime_error(
                "linear system must be loaded before initialize");
          }
          LinearInitializeResultMessage message;
          message.partition_id = request.partition_id;
          message.result = partition_it->second->initialize(request.values);
          const auto result_frame = encodeLinearInitializeResult(message);
          FrameTransferStats transfer;
          sendFrameBytes(socket, result_frame, compression, &transfer);
          if (status_hooks.on_frame_sent) {
            status_hooks.on_frame_sent(MessageType::LINEAR_INITIALIZE_RESULT,
                                       transfer);
          }
        }
        break;
      case MessageType::LINEAR_MULTIPLY_REQUEST:
        {
          auto request = decodeLinearMultiplyRequest(frame_bytes);
          const auto partition_it = linear_partitions.find(request.partition_id);
          if (partition_it == linear_partitions.end()) {
            throw std::runtime_error(
                "linear system must be loaded before multiply");
          }
          LinearMultiplyResultMessage message;
          message.partition_id = request.partition_id;
          message.result = partition_it->second->multiply(request.values);
          const auto result_frame = encodeLinearMultiplyResult(message);
          FrameTransferStats transfer;
          sendFrameBytes(socket, result_frame, compression, &transfer);
          if (status_hooks.on_frame_sent) {
            status_hooks.on_frame_sent(MessageType::LINEAR_MULTIPLY_RESULT,
                                       transfer);
          }
        }
        break;
      case MessageType::LINEAR_ALPHA_REQUEST:
        {
          const auto request = decodeLinearAlphaRequest(frame_bytes);
          const auto partition_it = linear_partitions.find(request.partition_id);
          if (partition_it == linear_partitions.end()) {
            throw std::runtime_error(
                "linear system must be loaded before alpha update");
          }
          LinearAlphaResultMessage message;
          message.partition_id = request.partition_id;
          message.result = partition_it->second->updateAlpha(request.value);
          const auto result_frame = encodeLinearAlphaResult(message);
          FrameTransferStats transfer;
          sendFrameBytes(socket, result_frame, compression, &transfer);
          if (status_hooks.on_frame_sent) {
            status_hooks.on_frame_sent(MessageType::LINEAR_ALPHA_RESULT,
                                       transfer);
          }
        }
        break;
      case MessageType::LINEAR_BETA_REQUEST:
        {
          const auto request = decodeLinearBetaRequest(frame_bytes);
          const auto partition_it = linear_partitions.find(request.partition_id);
          if (partition_it == linear_partitions.end()) {
            throw std::runtime_error(
                "linear system must be loaded before beta update");
          }
          LinearBetaResultMessage message;
          message.partition_id = request.partition_id;
          message.result = partition_it->second->updateBeta(request.value);
          const auto result_frame = encodeLinearBetaResult(message);
          FrameTransferStats transfer;
          sendFrameBytes(socket, result_frame, compression, &transfer);
          if (status_hooks.on_frame_sent) {
            status_hooks.on_frame_sent(MessageType::LINEAR_BETA_RESULT,
                                       transfer);
          }
        }
        break;
      case MessageType::LINEAR_SOLUTION_REQUEST:
        {
          const auto request = decodeLinearSolutionRequest(frame_bytes);
          const auto partition_it = linear_partitions.find(request.partition_id);
          if (partition_it == linear_partitions.end()) {
            throw std::runtime_error(
                "linear system must be loaded before solution request");
          }
          LinearSolutionResultMessage message;
          message.partition_id = request.partition_id;
          message.solution = partition_it->second->solution();
          const auto result_frame = encodeLinearSolutionResult(message);
          FrameTransferStats transfer;
          sendFrameBytes(socket, result_frame, compression, &transfer);
          if (status_hooks.on_frame_sent) {
            status_hooks.on_frame_sent(MessageType::LINEAR_SOLUTION_RESULT,
                                       transfer);
          }
        }
        break;
      case MessageType::STOP:
        (void)decodeStop(frame_bytes);
        if (status_hooks.on_phase) {
          status_hooks.on_phase("stopped");
        }
        return;
      default:
        {
          const auto transfer = sendErrorBestEffort(
              socket, 1, "worker received unexpected message type",
              compression);
          if (transfer.logical_bytes != 0 && status_hooks.on_frame_sent) {
            status_hooks.on_frame_sent(MessageType::ERROR, transfer);
          }
        }
        break;
      }
    } catch (const std::exception &e) {
      package_assembler.reset();
      package_transfer_logical_bytes = 0;
      if (status_hooks.on_error) {
        status_hooks.on_error(e.what());
      }
      std::cerr << "mcpd4_worker_runtime_error worker "
                << hello.worker_name << " error " << e.what() << "\n";
      const auto transfer =
          sendErrorBestEffort(socket, 2, e.what(), compression);
      if (transfer.logical_bytes != 0 && status_hooks.on_frame_sent) {
        status_hooks.on_frame_sent(MessageType::ERROR, transfer);
      }
      if (std::string(e.what()).find("socket ") != std::string::npos ||
          std::string(e.what()).find("compressed transport frame") !=
              std::string::npos) {
        throw;
      }
    }
  }
}

std::unique_ptr<TcpPartitionWorker> acceptTcpPartitionWorker(
    SocketHandle *listener, std::chrono::milliseconds timeout,
    TransportCompression compression) {
  SocketHandle socket = acceptTcp(listener, timeout);
  FrameTransferStats transfer;
  const auto frame_bytes =
      receiveFrameBytes(socket, kDefaultMaxFrameBytes,
                        TransportCompression::NONE, &transfer);
  const auto frame = decodeFrame(frame_bytes);
  if (frame.type != MessageType::HELLO) {
    throw std::runtime_error("expected HELLO from worker");
  }
  auto hello = decodeHello(frame_bytes);
  validateHello(hello);
  if (compression == TransportCompression::SNAPPY) {
    if (!snappyCompressionAvailable()) {
      throw std::runtime_error(
          "snappy compression requested but this binary was built without "
          "MCPD4_ENABLE_SNAPPY");
    }
    if (!compressionFeatureEnabled(hello, kFeatureSnappyCompression)) {
      throw std::runtime_error(
          "worker did not advertise snappy compression support");
    }
  }
  return std::make_unique<TcpPartitionWorker>(std::move(socket),
                                              std::move(hello),
                                              transfer.logical_bytes,
                                              compression);
}

} // namespace mcpd4
