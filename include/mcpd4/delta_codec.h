#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <decomp/partition_worker.h>
#include <mcpd4/protocol.h>

namespace mcpd4 {

struct TemporalPartitionState {
  std::unordered_map<int, mcpd3::Capacity> alpha_by_constraint;
  std::unordered_map<int, int> label_by_constraint;
  std::vector<int> label_order;
  bool labels_initialized = false;
};

struct TemporalSolveCodecState {
  std::unordered_map<int, TemporalPartitionState> partitions;

  void reset();
  void resetPartition(int partition_id);
};

std::vector<std::uint8_t> encodeDeltaSolveRoundRequest(
    const mcpd3::PartitionSolveRequest &message,
    TemporalSolveCodecState *state);
mcpd3::PartitionSolveRequest decodeDeltaSolveRoundRequest(
    const std::vector<std::uint8_t> &frame, TemporalSolveCodecState *state);

std::vector<std::uint8_t> encodeDeltaSolveRoundBatchRequest(
    const std::vector<mcpd3::PartitionSolveRequest> &messages,
    TemporalSolveCodecState *state);
std::vector<mcpd3::PartitionSolveRequest> decodeDeltaSolveRoundBatchRequest(
    const std::vector<std::uint8_t> &frame, TemporalSolveCodecState *state);

std::vector<std::uint8_t> encodeDeltaSolveRoundResultWithTiming(
    const mcpd3::PartitionSolveResult &message,
    std::uint64_t worker_solve_wall_us, TemporalSolveCodecState *state);
TimedSolveRoundResult decodeDeltaTimedSolveRoundResult(
    const std::vector<std::uint8_t> &frame, TemporalSolveCodecState *state);

std::vector<std::uint8_t> encodeDeltaSolveRoundBatchResultWithTiming(
    const std::vector<mcpd3::PartitionSolveResult> &messages,
    std::uint64_t worker_solve_wall_us, TemporalSolveCodecState *state);
TimedSolveRoundBatchResult decodeDeltaTimedSolveRoundBatchResult(
    const std::vector<std::uint8_t> &frame, TemporalSolveCodecState *state);

} // namespace mcpd4
