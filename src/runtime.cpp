#include <mcpd4/runtime.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

namespace mcpd4 {
namespace {

bool hostIsLittleEndian() {
  const std::uint16_t value = 1;
  return *reinterpret_cast<const std::uint8_t *>(&value) == 1;
}

void validateHello(const HelloMessage &hello) {
  if (hello.protocol_version != kProtocolVersion) {
    throw std::runtime_error("unsupported worker protocol version");
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
    stats->partition_load_tx_bytes += bytes;
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
    stats->partition_load_rx_bytes += bytes;
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
  }
}

Frame receiveTypedFrame(const SocketHandle &socket,
                        std::vector<std::uint8_t> *frame_bytes,
                        RpcByteStats *stats = nullptr,
                        TransportCompression compression =
                            TransportCompression::NONE) {
  FrameTransferStats transfer;
  *frame_bytes =
      receiveFrameBytes(socket, 256ULL * 1024ULL * 1024ULL, compression,
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
  const auto frame = encodePartitionPackage(package);
  FrameTransferStats transfer;
  sendFrameBytes(socket_, frame, compression_, &transfer);
  recordFrameSent(&timing_stats_.rpc_bytes, MessageType::PARTITION_PACKAGE,
                  transfer);
  (void)receiveReadyOrThrow(socket_, &timing_stats_.rpc_bytes,
                            compression_);
  temporal_state_.resetPartition(package.partition_id);
  timing_stats_.load_partition_rpc_wall_us += elapsedUs(start);
  ++timing_stats_.load_partition_rpc_count;
  partition_ids_.push_back(package.partition_id);
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

void TcpPartitionWorker::scaleObjective(long factor) {
  const auto start = std::chrono::steady_clock::now();
  ScaleObjectiveMessage message;
  message.factor = factor;
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
                     TransportCompression compression) {
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

  mcpd3::InProcessPartitionWorker worker;
  TemporalSolveCodecState temporal_state;
  while (true) {
    FrameTransferStats receive_transfer;
    const auto frame_bytes =
        receiveFrameBytes(socket, 256ULL * 1024ULL * 1024ULL, compression,
                          &receive_transfer);
    const auto frame = decodeFrame(frame_bytes);
    if (status_hooks.on_frame_received) {
      status_hooks.on_frame_received(frame.type, receive_transfer);
    }
    try {
      switch (frame.type) {
      case MessageType::PARTITION_PACKAGE:
        {
          if (status_hooks.on_phase) {
            status_hooks.on_phase("loading_partition");
          }
          const auto package = decodePartitionPackage(frame_bytes);
          worker.loadPartition(package);
          temporal_state.resetPartition(package.partition_id);
          if (status_hooks.on_partition_loaded) {
            status_hooks.on_partition_loaded(package.partition_id);
          }
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
      case MessageType::SOLVE_ROUND_REQUEST:
        {
          const auto request =
              decodeDeltaSolveRoundRequest(frame_bytes, &temporal_state);
          if (status_hooks.on_solve_start) {
            status_hooks.on_solve_start(request.round_id,
                                        {request.partition_id});
          }
          const auto start = std::chrono::steady_clock::now();
          const auto result = worker.solveRound(request);
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
          const auto results = worker.solveRoundBatch(requests);
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
          const auto factor = decodeScaleObjective(frame_bytes).factor;
          if (status_hooks.on_scale_objective) {
            status_hooks.on_scale_objective(factor);
          }
          worker.scaleObjective(factor);
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
      if (status_hooks.on_error) {
        status_hooks.on_error(e.what());
      }
      const auto transfer =
          sendErrorBestEffort(socket, 2, e.what(), compression);
      if (transfer.logical_bytes != 0 && status_hooks.on_frame_sent) {
        status_hooks.on_frame_sent(MessageType::ERROR, transfer);
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
      receiveFrameBytes(socket, 256ULL * 1024ULL * 1024ULL,
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
