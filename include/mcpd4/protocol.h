#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <decomp/partition_worker.h>
#include <mcpd4/linear_worker.h>

namespace mcpd4 {

enum class CapacityMode : std::uint32_t {
  BITS_32 = 1,
  BITS_64 = 2,
  BITS_128 = 3,
  GMP = 4,
};

inline constexpr CapacityMode configuredCapacityMode() {
#if defined(MCPD_CAPACITY_MODE_32)
  return CapacityMode::BITS_32;
#elif defined(MCPD_CAPACITY_MODE_64)
  return CapacityMode::BITS_64;
#elif defined(MCPD_CAPACITY_MODE_128)
  return CapacityMode::BITS_128;
#else
  return CapacityMode::GMP;
#endif
}

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
  LINEAR_STRUCTURE = 12,
  LINEAR_SYSTEM_VALUES = 13,
  LINEAR_INITIALIZE_REQUEST = 14,
  LINEAR_INITIALIZE_RESULT = 15,
  LINEAR_MULTIPLY_REQUEST = 16,
  LINEAR_MULTIPLY_RESULT = 17,
  LINEAR_ALPHA_REQUEST = 18,
  LINEAR_ALPHA_RESULT = 19,
  LINEAR_BETA_REQUEST = 20,
  LINEAR_BETA_RESULT = 21,
  LINEAR_SOLUTION_REQUEST = 22,
  LINEAR_SOLUTION_RESULT = 23,
  REPLACE_PARTITION_CAPACITIES = 24,
};

struct Frame {
  MessageType type = MessageType::ERROR;
  std::vector<std::uint8_t> payload;
};

struct HelloMessage {
  std::uint32_t protocol_version = 0;
  CapacityMode capacity_mode = configuredCapacityMode();
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

struct LinearStructureMessage {
  int partition_id = -1;
  std::vector<int> owned_global_nodes;
  std::vector<int> ghost_global_nodes;
  std::vector<std::uint64_t> row_offsets;
  std::vector<int> column_indices;
  std::vector<int> boundary_owned_local_indices;
};

struct LinearSystemValuesMessage {
  int partition_id = -1;
  std::vector<double> values;
  std::vector<double> rhs;
  std::vector<double> initial_x;
};

struct LinearVectorRequest {
  int partition_id = -1;
  std::vector<double> values;
};

struct LinearScalarRequest {
  int partition_id = -1;
  double value = 0.0;
};

struct LinearPartitionRequest {
  int partition_id = -1;
};

struct LinearInitializeResultMessage {
  int partition_id = -1;
  LinearInitializeResult result;
};

struct LinearMultiplyResultMessage {
  int partition_id = -1;
  LinearMultiplyResult result;
};

struct LinearAlphaResultMessage {
  int partition_id = -1;
  LinearAlphaUpdateResult result;
};

struct LinearBetaResultMessage {
  int partition_id = -1;
  LinearBetaUpdateResult result;
};

struct LinearSolutionResultMessage {
  int partition_id = -1;
  std::vector<double> solution;
};

std::vector<std::uint8_t> encodeFrame(MessageType type,
                                      const std::vector<std::uint8_t> &payload);
Frame decodeFrame(const std::vector<std::uint8_t> &bytes);

std::vector<std::uint8_t> encodeHello(const HelloMessage &message);
HelloMessage decodeHello(const std::vector<std::uint8_t> &frame);

std::vector<std::uint8_t> encodePartitionPackage(
    const mcpd3::PartitionPackage &message);
mcpd3::PartitionPackage decodePartitionPackage(
    const std::vector<std::uint8_t> &frame);

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

std::vector<std::uint8_t> encodePartitionCapacityUpdate(
    const mcpd3::PartitionCapacityUpdate &message);
mcpd3::PartitionCapacityUpdate decodePartitionCapacityUpdate(
    const std::vector<std::uint8_t> &frame);

std::vector<std::uint8_t> encodeAlphaUpdate(
    const AlphaUpdateMessage &message);
AlphaUpdateMessage decodeAlphaUpdate(const std::vector<std::uint8_t> &frame);

std::vector<std::uint8_t> encodeStop(const StopMessage &message);
StopMessage decodeStop(const std::vector<std::uint8_t> &frame);

std::vector<std::uint8_t> encodeError(const ErrorMessage &message);
ErrorMessage decodeError(const std::vector<std::uint8_t> &frame);

std::vector<std::uint8_t> encodeLinearStructure(
    const LinearStructureMessage &message);
LinearStructureMessage decodeLinearStructure(
    const std::vector<std::uint8_t> &frame);
std::vector<std::uint8_t> encodeLinearSystemValues(
    const LinearSystemValuesMessage &message);
LinearSystemValuesMessage decodeLinearSystemValues(
    const std::vector<std::uint8_t> &frame);

std::vector<std::uint8_t> encodeLinearInitializeRequest(
    const LinearVectorRequest &message);
LinearVectorRequest decodeLinearInitializeRequest(
    const std::vector<std::uint8_t> &frame);
std::vector<std::uint8_t> encodeLinearInitializeResult(
    const LinearInitializeResultMessage &message);
LinearInitializeResultMessage decodeLinearInitializeResult(
    const std::vector<std::uint8_t> &frame);

std::vector<std::uint8_t> encodeLinearMultiplyRequest(
    const LinearVectorRequest &message);
LinearVectorRequest decodeLinearMultiplyRequest(
    const std::vector<std::uint8_t> &frame);
std::vector<std::uint8_t> encodeLinearMultiplyResult(
    const LinearMultiplyResultMessage &message);
LinearMultiplyResultMessage decodeLinearMultiplyResult(
    const std::vector<std::uint8_t> &frame);

std::vector<std::uint8_t> encodeLinearAlphaRequest(
    const LinearScalarRequest &message);
LinearScalarRequest decodeLinearAlphaRequest(
    const std::vector<std::uint8_t> &frame);
std::vector<std::uint8_t> encodeLinearAlphaResult(
    const LinearAlphaResultMessage &message);
LinearAlphaResultMessage decodeLinearAlphaResult(
    const std::vector<std::uint8_t> &frame);

std::vector<std::uint8_t> encodeLinearBetaRequest(
    const LinearScalarRequest &message);
LinearScalarRequest decodeLinearBetaRequest(
    const std::vector<std::uint8_t> &frame);
std::vector<std::uint8_t> encodeLinearBetaResult(
    const LinearBetaResultMessage &message);
LinearBetaResultMessage decodeLinearBetaResult(
    const std::vector<std::uint8_t> &frame);

std::vector<std::uint8_t> encodeLinearSolutionRequest(
    const LinearPartitionRequest &message);
LinearPartitionRequest decodeLinearSolutionRequest(
    const std::vector<std::uint8_t> &frame);
std::vector<std::uint8_t> encodeLinearSolutionResult(
    const LinearSolutionResultMessage &message);
LinearSolutionResultMessage decodeLinearSolutionResult(
    const std::vector<std::uint8_t> &frame);

} // namespace mcpd4
