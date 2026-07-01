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

Frame receiveTypedFrame(const SocketHandle &socket,
                        std::vector<std::uint8_t> *frame_bytes) {
  *frame_bytes = receiveFrameBytes(socket);
  return decodeFrame(*frame_bytes);
}

ReadyMessage receiveReadyOrThrow(const SocketHandle &socket) {
  std::vector<std::uint8_t> frame_bytes;
  const auto frame = receiveTypedFrame(socket, &frame_bytes);
  if (frame.type == MessageType::ERROR) {
    throw remoteError(frame_bytes);
  }
  if (frame.type != MessageType::READY) {
    throw std::runtime_error("expected READY response from worker");
  }
  return decodeReady(frame_bytes);
}

void sendReady(const SocketHandle &socket, const std::string &worker_name) {
  ReadyMessage ready;
  ready.worker_name = worker_name;
  sendFrameBytes(socket, encodeReady(ready));
}

void sendErrorBestEffort(const SocketHandle &socket, std::uint32_t code,
                         const std::string &message) {
  try {
    ErrorMessage error;
    error.code = code;
    error.message = message;
    sendFrameBytes(socket, encodeError(error));
  } catch (...) {
  }
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

TcpPartitionWorker::TcpPartitionWorker(SocketHandle socket, HelloMessage hello)
    : socket_(std::move(socket)), hello_(std::move(hello)) {
  validateHello(hello_);
}

void TcpPartitionWorker::loadPartition(
    const mcpd3::PartitionPackage &package) {
  const auto start = std::chrono::steady_clock::now();
  sendFrameBytes(socket_, encodePartitionPackage(package));
  (void)receiveReadyOrThrow(socket_);
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
  sendFrameBytes(socket_, encodeSolveRoundRequest(request));
  std::vector<std::uint8_t> frame_bytes;
  const auto frame = receiveTypedFrame(socket_, &frame_bytes);
  if (frame.type == MessageType::ERROR) {
    throw remoteError(frame_bytes);
  }
  if (frame.type != MessageType::SOLVE_ROUND_RESULT) {
    throw std::runtime_error("expected SOLVE_ROUND_RESULT from worker");
  }
  const auto timed = decodeTimedSolveRoundResult(frame_bytes);
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
  sendFrameBytes(socket_, encodeSolveRoundBatchRequest(requests));
  std::vector<std::uint8_t> frame_bytes;
  const auto frame = receiveTypedFrame(socket_, &frame_bytes);
  if (frame.type == MessageType::ERROR) {
    throw remoteError(frame_bytes);
  }
  if (frame.type != MessageType::SOLVE_ROUND_BATCH_RESULT) {
    throw std::runtime_error("expected SOLVE_ROUND_BATCH_RESULT from worker");
  }
  const auto timed = decodeTimedSolveRoundBatchResult(frame_bytes);
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
  sendFrameBytes(socket_, encodeScaleObjective(message));
  (void)receiveReadyOrThrow(socket_);
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
    sendFrameBytes(socket_, encodeStop(stop));
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
                     const WorkerRuntimeStatusHooks &status_hooks) {
  validateHello(hello);
  SocketHandle socket = connectTcp(host, port);
  sendFrameBytes(socket, encodeHello(hello));
  if (status_hooks.on_phase) {
    status_hooks.on_phase("connected");
  }

  mcpd3::InProcessPartitionWorker worker;
  while (true) {
    const auto frame_bytes = receiveFrameBytes(socket);
    const auto frame = decodeFrame(frame_bytes);
    try {
      switch (frame.type) {
      case MessageType::PARTITION_PACKAGE:
        {
          if (status_hooks.on_phase) {
            status_hooks.on_phase("loading_partition");
          }
          const auto package = decodePartitionPackage(frame_bytes);
          worker.loadPartition(package);
          if (status_hooks.on_partition_loaded) {
            status_hooks.on_partition_loaded(package.partition_id);
          }
          if (status_hooks.on_phase) {
            status_hooks.on_phase("connected");
          }
          sendReady(socket, hello.worker_name);
        }
        break;
      case MessageType::SOLVE_ROUND_REQUEST:
        {
          const auto request = decodeSolveRoundRequest(frame_bytes);
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
          sendFrameBytes(
              socket, encodeSolveRoundResultWithTiming(result, elapsed));
        }
        break;
      case MessageType::SOLVE_ROUND_BATCH_REQUEST:
        {
          const auto requests = decodeSolveRoundBatchRequest(frame_bytes);
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
          sendFrameBytes(
              socket, encodeSolveRoundBatchResultWithTiming(results, elapsed));
        }
        break;
      case MessageType::SCALE_OBJECTIVE:
        {
          const auto factor = decodeScaleObjective(frame_bytes).factor;
          if (status_hooks.on_scale_objective) {
            status_hooks.on_scale_objective(factor);
          }
          worker.scaleObjective(factor);
          if (status_hooks.on_phase) {
            status_hooks.on_phase("connected");
          }
          sendReady(socket, hello.worker_name);
        }
        break;
      case MessageType::STOP:
        (void)decodeStop(frame_bytes);
        if (status_hooks.on_phase) {
          status_hooks.on_phase("stopped");
        }
        return;
      default:
        sendErrorBestEffort(socket, 1,
                            "worker received unexpected message type");
        break;
      }
    } catch (const std::exception &e) {
      if (status_hooks.on_error) {
        status_hooks.on_error(e.what());
      }
      sendErrorBestEffort(socket, 2, e.what());
    }
  }
}

std::unique_ptr<TcpPartitionWorker> acceptTcpPartitionWorker(
    SocketHandle *listener, std::chrono::milliseconds timeout) {
  SocketHandle socket = acceptTcp(listener, timeout);
  const auto frame_bytes = receiveFrameBytes(socket);
  const auto frame = decodeFrame(frame_bytes);
  if (frame.type != MessageType::HELLO) {
    throw std::runtime_error("expected HELLO from worker");
  }
  auto hello = decodeHello(frame_bytes);
  validateHello(hello);
  return std::make_unique<TcpPartitionWorker>(std::move(socket),
                                              std::move(hello));
}

} // namespace mcpd4
