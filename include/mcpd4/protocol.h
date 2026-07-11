#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <mcpd4/buffer_view.h>

#include <decomp/partition_worker.h>

namespace mcpd4 {

enum class MessageType : std::uint32_t {
  HELLO = 1,
  PARTITION_PACKAGE = 2,
  READY = 3,
  SOLVE_ROUND_REQUEST = 4,
  SOLVE_ROUND_RESULT = 5,
  SCALE_OBJECTIVE = 6,
  ALPHA_UPDATE = 7,
  STOP = 8,
  ERROR = 9,
  SOLVE_ROUND_BATCH_REQUEST = 10,
  SOLVE_ROUND_BATCH_RESULT = 11,
};

struct Frame {
  MessageType type = MessageType::ERROR;
  std::vector<std::uint8_t> payload;
};

struct HelloMessage {
  std::uint32_t protocol_version = 0;
  std::string worker_name;
  std::uint32_t cpu_count = 0;
  std::uint64_t ram_gb = 0;
  std::uint64_t feature_bits = 0;
  std::string temp_path;
  bool debug_build = false;
  bool little_endian = true;
};

struct ReadyMessage {
  std::string worker_name;
};

struct ScaleObjectiveMessage {
  std::int64_t factor = 1;
  bool saturate_capacity_overflow = false;
};

struct AlphaUpdateMessage {
  int partition_id = -1;
  std::vector<mcpd3::AlphaUpdate> alpha_updates;
};

struct StopMessage {
  std::uint32_t reason = 0;
  std::string message;
};

struct ErrorMessage {
  std::uint32_t code = 0;
  std::string message;
};

struct TimedSolveRoundResult {
  mcpd3::PartitionSolveResult result;
  std::uint64_t worker_solve_wall_us = 0;
};

struct TimedSolveRoundBatchResult {
  std::vector<mcpd3::PartitionSolveResult> results;
  std::uint64_t worker_solve_wall_us = 0;
};

std::vector<std::uint8_t> encodeFrame(MessageType type,
                                      const std::vector<std::uint8_t> &payload);
Frame decodeFrame(const std::vector<std::uint8_t> &bytes);
MessageType decodeFrameType(const std::vector<std::uint8_t> &bytes);

std::vector<std::uint8_t> encodeHello(const HelloMessage &message);
HelloMessage decodeHello(const std::vector<std::uint8_t> &frame);

std::vector<std::uint8_t> encodePartitionPackage(
    const mcpd3::PartitionPackage &message);
mcpd3::PartitionPackage decodePartitionPackage(
    const std::vector<std::uint8_t> &frame);
bool partitionPackageFrameBuffersSupported();

class PartitionPackageFrameBuffers {
public:
  enum class Mode {
    FULL,
    WORKER_LOAD,
  };

  explicit PartitionPackageFrameBuffers(
      const mcpd3::PartitionPackage &message,
      Mode mode = Mode::FULL);

  std::size_t totalSize() const;
  std::vector<ByteBufferView> buffers() const;

private:
  const mcpd3::PartitionPackage *message_ = nullptr;
  Mode mode_ = Mode::FULL;
  std::size_t total_size_ = 0;
  std::array<std::uint8_t, 12> frame_header_{};
  std::array<std::uint8_t, 8> scalar_header_{};
  std::array<std::uint8_t, 4> arcs_size_{};
  std::array<std::uint8_t, 4> arc_capacities_size_{};
  std::array<std::uint8_t, 4> terminal_capacities_size_{};
  std::array<std::uint8_t, 4> local_to_global_size_{};
  std::array<std::uint8_t, 4> constraint_endpoints_size_{};
  bool use_compact_arc_capacities_ = false;
  std::vector<int> compact_arc_capacities_;
  std::vector<std::uint8_t> constraint_endpoint_bytes_;
};

std::vector<std::uint8_t> encodeReady(const ReadyMessage &message);
ReadyMessage decodeReady(const std::vector<std::uint8_t> &frame);

std::vector<std::uint8_t> encodeSolveRoundRequest(
    const mcpd3::PartitionSolveRequest &message);
mcpd3::PartitionSolveRequest decodeSolveRoundRequest(
    const std::vector<std::uint8_t> &frame);
std::vector<std::uint8_t> encodeSolveRoundBatchRequest(
    const std::vector<mcpd3::PartitionSolveRequest> &messages);
std::vector<mcpd3::PartitionSolveRequest> decodeSolveRoundBatchRequest(
    const std::vector<std::uint8_t> &frame);

std::vector<std::uint8_t> encodeSolveRoundResult(
    const mcpd3::PartitionSolveResult &message);
std::vector<std::uint8_t> encodeSolveRoundResultWithTiming(
    const mcpd3::PartitionSolveResult &message,
    std::uint64_t worker_solve_wall_us);
mcpd3::PartitionSolveResult decodeSolveRoundResult(
    const std::vector<std::uint8_t> &frame);
TimedSolveRoundResult decodeTimedSolveRoundResult(
    const std::vector<std::uint8_t> &frame);
std::vector<std::uint8_t> encodeSolveRoundBatchResult(
    const std::vector<mcpd3::PartitionSolveResult> &messages);
std::vector<std::uint8_t> encodeSolveRoundBatchResultWithTiming(
    const std::vector<mcpd3::PartitionSolveResult> &messages,
    std::uint64_t worker_solve_wall_us);
std::vector<mcpd3::PartitionSolveResult> decodeSolveRoundBatchResult(
    const std::vector<std::uint8_t> &frame);
TimedSolveRoundBatchResult decodeTimedSolveRoundBatchResult(
    const std::vector<std::uint8_t> &frame);

std::vector<std::uint8_t> encodeScaleObjective(
    const ScaleObjectiveMessage &message);
ScaleObjectiveMessage decodeScaleObjective(
    const std::vector<std::uint8_t> &frame);

std::vector<std::uint8_t> encodeAlphaUpdate(
    const AlphaUpdateMessage &message);
AlphaUpdateMessage decodeAlphaUpdate(const std::vector<std::uint8_t> &frame);

std::vector<std::uint8_t> encodeStop(const StopMessage &message);
StopMessage decodeStop(const std::vector<std::uint8_t> &frame);

std::vector<std::uint8_t> encodeError(const ErrorMessage &message);
ErrorMessage decodeError(const std::vector<std::uint8_t> &frame);

} // namespace mcpd4
