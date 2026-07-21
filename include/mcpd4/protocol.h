#pragma once

#include <array>
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
  PARTITION_PACKAGE_BEGIN = 25,
  PARTITION_PACKAGE_CHUNK = 26,
  PARTITION_PACKAGE_END = 27,
  FULL_LABELS_REQUEST = 28,
  FULL_LABELS_CHUNK = 29,
  FULL_LABELS_END = 30,
  PARTITION_CAPACITY_UPDATE_BEGIN = 31,
  PARTITION_CAPACITY_UPDATE_CHUNK = 32,
  PARTITION_CAPACITY_UPDATE_END = 33,
};

enum class PartitionPackageSection : std::uint32_t {
  ARCS = 0,
  ARC_CAPACITIES = 1,
  TERMINAL_CAPACITIES = 2,
  LOCAL_TO_GLOBAL = 3,
  CONSTRAINT_ENDPOINTS = 4,
  REFERENCE_CUT_LABELS = 5,
};

inline constexpr std::size_t kPartitionPackageSectionCount = 6;

struct PartitionPackageTransferHeader {
  int partition_id = -1;
  int local_node_count = 0;
  std::array<std::uint64_t, kPartitionPackageSectionCount> section_counts{};
  long objective_multiplier = 1;
  mcpd3::CanonicalCutSelection canonical_cut_selection =
      mcpd3::CanonicalCutSelection::SOLVER_DEFAULT;
  bool force_full_mincut_recompute = false;
  mcpd3::ReferenceCutSelection reference_cut_selection =
      mcpd3::ReferenceCutSelection::CLOSEST_EXACT;
  long reference_cut_check_interval = 1;
};

struct PartitionPackageTransferChunk {
  int partition_id = -1;
  PartitionPackageSection section = PartitionPackageSection::ARCS;
  std::uint64_t offset = 0;
  std::vector<int> int_values;
  std::vector<mcpd3::Capacity> capacity_values;
  std::vector<mcpd3::ConstraintEndpointBinding> constraint_values;

  std::size_t size() const;
};

struct PartitionPackageTransferEnd {
  int partition_id = -1;
};

struct FullLabelsRequest {
  int partition_id = -1;
  std::uint64_t offset = 0;
  std::uint64_t count = 0;
};

struct FullLabelsChunk {
  int partition_id = -1;
  std::uint64_t offset = 0;
  std::vector<mcpd3::NodeLabel> labels;
};

struct FullLabelsEnd {
  int partition_id = -1;
};

class PartitionPackageAssembler {
public:
  PartitionPackageAssembler(PartitionPackageTransferHeader header,
                            mcpd3::SolverStorageOptions storage);
  void append(PartitionPackageTransferChunk chunk);
  bool complete() const;
  mcpd3::PartitionPackage finish();

private:
  PartitionPackageTransferHeader header_;
  std::array<std::uint64_t, kPartitionPackageSectionCount> next_offsets_{};
  mcpd3::PartitionPackage package_;
  bool finished_ = false;
};

enum class PartitionCapacityUpdateSection : std::uint32_t {
  ARC_CAPACITIES = 0,
  TERMINAL_CAPACITIES = 1,
};

inline constexpr std::size_t kPartitionCapacityUpdateSectionCount = 2;

struct PartitionCapacityUpdateTransferHeader {
  int partition_id = -1;
  std::array<std::uint64_t, kPartitionCapacityUpdateSectionCount>
      section_counts{};
  bool preserve_flow_state = true;
  mcpd3::Objective flow_scale_numerator = 1;
  mcpd3::Objective flow_scale_denominator = 1;
};

struct PartitionCapacityUpdateTransferChunk {
  int partition_id = -1;
  PartitionCapacityUpdateSection section =
      PartitionCapacityUpdateSection::ARC_CAPACITIES;
  std::uint64_t offset = 0;
  std::vector<mcpd3::Capacity> values;
};

struct PartitionCapacityUpdateTransferEnd {
  int partition_id = -1;
};

class PartitionCapacityUpdateAssembler {
public:
  PartitionCapacityUpdateAssembler(
      PartitionCapacityUpdateTransferHeader header,
      mcpd3::SolverStorageOptions storage);
  void append(PartitionCapacityUpdateTransferChunk chunk);
  bool complete() const;
  mcpd3::PartitionCapacityUpdate finish();

private:
  PartitionCapacityUpdateTransferHeader header_;
  std::array<std::uint64_t, kPartitionCapacityUpdateSectionCount>
      next_offsets_{};
  mcpd3::PartitionCapacityUpdate update_;
  bool finished_ = false;
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
  // Empty preserves the historical command that scales every loaded partition.
  std::vector<int> partition_ids;
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

PartitionPackageTransferHeader makePartitionPackageTransferHeader(
    const mcpd3::PartitionPackage &message);
std::vector<std::uint8_t> encodePartitionPackageTransferBegin(
    const PartitionPackageTransferHeader &message);
PartitionPackageTransferHeader decodePartitionPackageTransferBegin(
    const std::vector<std::uint8_t> &frame);
std::vector<std::uint8_t> encodePartitionPackageTransferChunk(
    const mcpd3::PartitionPackage &message, PartitionPackageSection section,
    std::uint64_t offset, std::size_t count);
PartitionPackageTransferChunk decodePartitionPackageTransferChunk(
    const std::vector<std::uint8_t> &frame);
std::vector<std::uint8_t> encodePartitionPackageTransferEnd(
    const PartitionPackageTransferEnd &message);
PartitionPackageTransferEnd decodePartitionPackageTransferEnd(
    const std::vector<std::uint8_t> &frame);

std::vector<std::uint8_t> encodeFullLabelsRequest(
    const FullLabelsRequest &message);
FullLabelsRequest decodeFullLabelsRequest(
    const std::vector<std::uint8_t> &frame);
std::vector<std::uint8_t> encodeFullLabelsChunk(
    const FullLabelsChunk &message);
FullLabelsChunk decodeFullLabelsChunk(const std::vector<std::uint8_t> &frame);
std::vector<std::uint8_t> encodeFullLabelsEnd(
    const FullLabelsEnd &message);
FullLabelsEnd decodeFullLabelsEnd(const std::vector<std::uint8_t> &frame);

PartitionCapacityUpdateTransferHeader makePartitionCapacityUpdateTransferHeader(
    const mcpd3::PartitionCapacityUpdate &message);
std::vector<std::uint8_t> encodePartitionCapacityUpdateTransferBegin(
    const PartitionCapacityUpdateTransferHeader &message);
PartitionCapacityUpdateTransferHeader decodePartitionCapacityUpdateTransferBegin(
    const std::vector<std::uint8_t> &frame);
std::vector<std::uint8_t> encodePartitionCapacityUpdateTransferChunk(
    const mcpd3::PartitionCapacityUpdate &message,
    PartitionCapacityUpdateSection section, std::uint64_t offset,
    std::size_t count);
PartitionCapacityUpdateTransferChunk decodePartitionCapacityUpdateTransferChunk(
    const std::vector<std::uint8_t> &frame);
std::vector<std::uint8_t> encodePartitionCapacityUpdateTransferEnd(
    const PartitionCapacityUpdateTransferEnd &message);
PartitionCapacityUpdateTransferEnd decodePartitionCapacityUpdateTransferEnd(
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
