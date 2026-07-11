#include <mcpd4/runtime.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace mcpd4 {
namespace {

constexpr std::size_t kProtocolFrameHeaderBytes = 12;
constexpr std::size_t kConstraintEndpointWireBytes =
    4 + 4 + 4 + 1 + 8 + 8 + 4;
constexpr std::size_t kCompactConstraintEndpointWireBytes =
    4 + 4 + 1 + 8 + 8;

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

std::runtime_error socketError(const std::string &message) {
  return std::runtime_error(message + ": " + std::strerror(errno));
}

void readAllFromSocket(const SocketHandle &socket, std::uint8_t *data,
                       std::size_t size) {
  std::size_t read = 0;
  while (read < size) {
    const auto n = ::recv(socket.get(), data + read, size - read, 0);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw socketError("socket read failed");
    }
    if (n == 0) {
      throw std::runtime_error("socket closed during read");
    }
    read += static_cast<std::size_t>(n);
  }
}

std::uint32_t readLittleU32(const std::uint8_t *data) {
  std::uint32_t value = 0;
  for (int shift = 0; shift < 32; shift += 8) {
    value |= static_cast<std::uint32_t>(*data++) << shift;
  }
  return value;
}

std::uint64_t readLittleU64(const std::uint8_t *data) {
  std::uint64_t value = 0;
  for (int shift = 0; shift < 64; shift += 8) {
    value |= static_cast<std::uint64_t>(*data++) << shift;
  }
  return value;
}

MessageType checkedMessageType(std::uint32_t raw_type) {
  switch (static_cast<MessageType>(raw_type)) {
  case MessageType::HELLO:
  case MessageType::PARTITION_PACKAGE:
  case MessageType::READY:
  case MessageType::SOLVE_ROUND_REQUEST:
  case MessageType::SOLVE_ROUND_RESULT:
  case MessageType::SCALE_OBJECTIVE:
  case MessageType::ALPHA_UPDATE:
  case MessageType::STOP:
  case MessageType::ERROR:
  case MessageType::SOLVE_ROUND_BATCH_REQUEST:
  case MessageType::SOLVE_ROUND_BATCH_RESULT:
    return static_cast<MessageType>(raw_type);
  }
  throw std::runtime_error("unknown message type");
}

void requireFramePayloadWithinLimit(std::uint64_t payload_size,
                                    std::size_t max_frame_bytes) {
  if (max_frame_bytes < kProtocolFrameHeaderBytes ||
      payload_size >
          static_cast<std::uint64_t>(max_frame_bytes -
                                     kProtocolFrameHeaderBytes)) {
    throw std::runtime_error("frame payload exceeds maximum size");
  }
}

long checkedLongCast(std::int64_t value) {
  if (value < static_cast<std::int64_t>(std::numeric_limits<long>::min()) ||
      value > static_cast<std::int64_t>(std::numeric_limits<long>::max())) {
    throw std::runtime_error("integer value is outside target range");
  }
  return static_cast<long>(value);
}

struct UncompressedFrameHeader {
  std::array<std::uint8_t, kProtocolFrameHeaderBytes> bytes{};
  MessageType type = MessageType::ERROR;
  std::uint64_t payload_size = 0;
};

UncompressedFrameHeader receiveUncompressedFrameHeader(
    const SocketHandle &socket, std::size_t max_frame_bytes) {
  UncompressedFrameHeader header;
  readAllFromSocket(socket, header.bytes.data(), header.bytes.size());
  header.type = checkedMessageType(readLittleU32(header.bytes.data()));
  header.payload_size = readLittleU64(header.bytes.data() + 4);
  requireFramePayloadWithinLimit(header.payload_size, max_frame_bytes);
  return header;
}

std::vector<std::uint8_t> receiveUncompressedFrameBody(
    const SocketHandle &socket, const UncompressedFrameHeader &header,
    FrameTransferStats *stats) {
  std::vector<std::uint8_t> frame(
      kProtocolFrameHeaderBytes + static_cast<std::size_t>(
                                      header.payload_size));
  std::copy(header.bytes.begin(), header.bytes.end(), frame.begin());
  readAllFromSocket(socket, frame.data() + kProtocolFrameHeaderBytes,
                    static_cast<std::size_t>(header.payload_size));
  (void)decodeFrameType(frame);
  if (stats != nullptr) {
    *stats = {};
    stats->logical_bytes = static_cast<std::uint64_t>(frame.size());
    stats->wire_bytes = static_cast<std::uint64_t>(frame.size());
  }
  return frame;
}

class SocketPayloadReader {
public:
  SocketPayloadReader(const SocketHandle &socket, std::uint64_t payload_size)
      : socket_(socket), payload_size_(payload_size) {}

  int readI32() {
    std::array<std::uint8_t, 4> bytes{};
    readBytes(bytes.data(), bytes.size());
    std::int32_t value = 0;
    const auto bits = readLittleU32(bytes.data());
    std::memcpy(&value, &bits, sizeof(value));
    return static_cast<int>(value);
  }

  std::uint32_t readU32() {
    std::array<std::uint8_t, 4> bytes{};
    readBytes(bytes.data(), bytes.size());
    return readLittleU32(bytes.data());
  }

  std::vector<int> readI32Vector() {
    const auto size = readU32();
    std::vector<int> values(static_cast<std::size_t>(size));
    if (values.empty()) {
      return values;
    }
    const auto byte_count =
        static_cast<std::uint64_t>(size) * sizeof(std::int32_t);
    readBytes(reinterpret_cast<std::uint8_t *>(values.data()),
              static_cast<std::size_t>(byte_count));
    return values;
  }

  std::vector<mcpd3::ConstraintEndpointBinding> readConstraintEndpoints() {
    const auto size = readU32();
    const auto full_byte_count =
        static_cast<std::uint64_t>(size) * kConstraintEndpointWireBytes;
    const auto compact_byte_count =
        static_cast<std::uint64_t>(size) * kCompactConstraintEndpointWireBytes;
    const bool compact = payload_size_ - offset_ == compact_byte_count;
    const auto byte_count = compact ? compact_byte_count : full_byte_count;
    if (!compact && payload_size_ - offset_ < full_byte_count) {
      throw std::runtime_error("truncated endpoint payload");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(byte_count));
    if (!bytes.empty()) {
      readBytes(bytes.data(), bytes.size());
    }
    std::vector<mcpd3::ConstraintEndpointBinding> endpoints;
    endpoints.reserve(static_cast<std::size_t>(size));
    std::size_t offset = 0;
    const auto read_u8 = [&]() {
      if (offset >= bytes.size()) {
        throw std::runtime_error("truncated payload");
      }
      return bytes[offset++];
    };
    const auto read_u32 = [&]() {
      if (4 > bytes.size() - offset) {
        throw std::runtime_error("truncated payload");
      }
      const auto value = readLittleU32(bytes.data() + offset);
      offset += 4;
      return value;
    };
    const auto read_i32 = [&]() {
      const auto bits = read_u32();
      std::int32_t value = 0;
      std::memcpy(&value, &bits, sizeof(value));
      return static_cast<int>(value);
    };
    const auto read_i64 = [&]() {
      if (8 > bytes.size() - offset) {
        throw std::runtime_error("truncated payload");
      }
      const auto bits = readLittleU64(bytes.data() + offset);
      offset += 8;
      std::int64_t value = 0;
      std::memcpy(&value, &bits, sizeof(value));
      return value;
    };
    const auto read_float = [&]() {
      const auto bits = read_u32();
      float value = 0;
      std::memcpy(&value, &bits, sizeof(value));
      return value;
    };
    for (std::uint32_t i = 0; i < size; ++i) {
      mcpd3::ConstraintEndpointBinding binding;
      binding.constraint_id = read_i32();
      binding.global_node_id = compact ? -1 : read_i32();
      binding.local_index = read_i32();
      const auto side = read_u8();
      if (side != 0 && side != 1) {
        throw std::runtime_error("invalid boolean value");
      }
      binding.is_source = side != 0;
      binding.alpha = checkedLongCast(read_i64());
      binding.last_alpha = checkedLongCast(read_i64());
      binding.alpha_momentum = compact ? 0 : read_float();
      endpoints.push_back(binding);
    }
    if (offset != bytes.size()) {
      throw std::runtime_error("payload has trailing bytes");
    }
    return endpoints;
  }

  void requireDone() const {
    if (offset_ != payload_size_) {
      throw std::runtime_error("payload has trailing bytes");
    }
  }

private:
  void readBytes(std::uint8_t *data, std::size_t size) {
    if (size > payload_size_ - offset_) {
      throw std::runtime_error("truncated payload");
    }
    readAllFromSocket(socket_, data, size);
    offset_ += size;
  }

  const SocketHandle &socket_;
  std::uint64_t payload_size_ = 0;
  std::uint64_t offset_ = 0;
};

mcpd3::PartitionPackage receivePartitionPackagePayload(
    const SocketHandle &socket, std::uint64_t payload_size) {
  SocketPayloadReader reader(socket, payload_size);
  mcpd3::PartitionPackage message;
  message.partition_id = reader.readI32();
  message.local_node_count = reader.readI32();
  message.arcs = reader.readI32Vector();
  message.arc_capacities = reader.readI32Vector();
  message.terminal_capacities = reader.readI32Vector();
  message.local_to_global = reader.readI32Vector();
  message.constraint_endpoints = reader.readConstraintEndpoints();
  reader.requireDone();
  return message;
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
      receiveFrameBytes(socket, kDefaultMaxFrameBytes, compression,
                        &transfer);
  Frame frame;
  frame.type = decodeFrameType(*frame_bytes);
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
  std::uint64_t frame_logical_bytes = 0;
  FrameTransferStats transfer;
  try {
    const bool use_buffered_send = partitionPackageFrameBuffersSupported();
    if (use_buffered_send) {
      PartitionPackageFrameBuffers frame_buffers(
          package, PartitionPackageFrameBuffers::Mode::WORKER_LOAD);
      frame_logical_bytes =
          static_cast<std::uint64_t>(frame_buffers.totalSize());
      std::cerr << "mcpd4_load_partition_begin worker "
                << hello_.worker_name << " " << package_summary
                << " frame_logical_bytes " << frame_logical_bytes << "\n";
      sendFrameByteBuffers(socket_, frame_buffers.buffers(), compression_,
                           &transfer);
    } else {
      const auto frame = encodePartitionPackage(package);
      frame_logical_bytes = static_cast<std::uint64_t>(frame.size());
      std::cerr << "mcpd4_load_partition_begin worker "
                << hello_.worker_name << " " << package_summary
                << " frame_logical_bytes " << frame_logical_bytes << "\n";
      sendFrameBytes(socket_, frame, compression_, &transfer);
    }
    recordFrameSent(&timing_stats_.rpc_bytes, MessageType::PARTITION_PACKAGE,
                    transfer);
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
              << " frame_logical_bytes " << transfer.logical_bytes
              << " frame_wire_bytes " << transfer.wire_bytes << "\n";
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

  std::unique_ptr<mcpd3::PartitionWorker> worker;
  if (runtime_options.streaming_partitions) {
    mcpd3::StreamingPartitionWorker::Options options;
    options.storage_directory = runtime_options.streaming_directory;
    options.resident_byte_limit = runtime_options.streaming_resident_bytes;
    worker = std::make_unique<mcpd3::StreamingPartitionWorker>(
        std::move(options));
  } else {
    worker = std::make_unique<mcpd3::InProcessPartitionWorker>();
  }
  TemporalSolveCodecState temporal_state;
  while (true) {
    try {
      FrameTransferStats receive_transfer;
      std::vector<std::uint8_t> frame_bytes;
      mcpd3::PartitionPackage direct_package;
      bool has_direct_package = false;
      std::uint64_t frame_logical_bytes = 0;
      MessageType frame_type = MessageType::ERROR;
      if (compression == TransportCompression::NONE &&
          partitionPackageFrameBuffersSupported()) {
        const auto header =
            receiveUncompressedFrameHeader(socket, kDefaultMaxFrameBytes);
        frame_type = header.type;
        frame_logical_bytes =
            static_cast<std::uint64_t>(kProtocolFrameHeaderBytes) +
            header.payload_size;
        if (frame_type == MessageType::PARTITION_PACKAGE) {
          direct_package =
              receivePartitionPackagePayload(socket, header.payload_size);
          receive_transfer.logical_bytes = frame_logical_bytes;
          receive_transfer.wire_bytes = frame_logical_bytes;
          has_direct_package = true;
        } else {
          frame_bytes =
              receiveUncompressedFrameBody(socket, header, &receive_transfer);
        }
      } else {
        frame_bytes =
            receiveFrameBytes(socket, kDefaultMaxFrameBytes, compression,
                              &receive_transfer);
        frame_type = decodeFrameType(frame_bytes);
        frame_logical_bytes = static_cast<std::uint64_t>(frame_bytes.size());
      }
      if (status_hooks.on_frame_received) {
        status_hooks.on_frame_received(frame_type, receive_transfer);
      }
      switch (frame_type) {
      case MessageType::PARTITION_PACKAGE:
        {
          if (status_hooks.on_phase) {
            status_hooks.on_phase("loading_partition");
          }
          auto package = has_direct_package
                             ? std::move(direct_package)
                             : decodePartitionPackage(frame_bytes);
          const int partition_id = package.partition_id;
          if (status_hooks.on_partition_loading) {
            status_hooks.on_partition_loading(
                package, frame_logical_bytes);
          }
          std::cerr << "mcpd4_worker_load_partition_begin worker "
                    << hello.worker_name << " "
                    << partitionPackageSummary(package)
                    << " frame_logical_bytes " << frame_logical_bytes << "\n";
          worker->loadPartition(std::move(package));
          temporal_state.resetPartition(partition_id);
          if (status_hooks.on_partition_loaded) {
            status_hooks.on_partition_loaded(partition_id);
          }
          if (status_hooks.on_phase) {
            status_hooks.on_phase("connected");
          }
          const auto transfer =
              sendReady(socket, hello.worker_name, compression);
          if (status_hooks.on_frame_sent) {
            status_hooks.on_frame_sent(MessageType::READY, transfer);
          }
          std::cerr << "mcpd4_worker_load_partition_done worker "
                    << hello.worker_name << " partition_id "
                    << partition_id << "\n";
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
  const auto frame_type = decodeFrameType(frame_bytes);
  if (frame_type != MessageType::HELLO) {
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
