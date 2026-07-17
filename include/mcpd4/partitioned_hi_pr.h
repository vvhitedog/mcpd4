#pragma once

#include <capacity.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mcpd4::experimental {

struct DirectedArc {
  int tail = -1;
  int head = -1;
  mcpd3::Capacity capacity = 0;
};

struct PartitionedHiPrOptions {
  std::size_t max_coordination_rounds = 100000;
  // Set above zero to interrupt local work for periodic global relabels.
  // hi_pr uses roughly 2 * (6n + m), but a distributed full-graph BFS can
  // cost more than the local work it saves, so this experiment defaults off.
  double global_relabel_work_factor = 0.0;
  bool validate_invariants = false;
};

struct PartitionedHiPrStats {
  std::size_t residual_arc_count = 0;
  std::size_t boundary_directed_arc_count = 0;
  std::size_t boundary_node_count = 0;
  std::size_t coordination_rounds = 0;
  std::size_t global_relabels = 0;
  std::size_t local_pushes = 0;
  std::size_t boundary_pushes = 0;
  std::size_t local_relabels = 0;
  std::size_t local_arc_scans = 0;
  std::size_t work_global_relabel_interruptions = 0;
  std::size_t gap_events = 0;
  std::size_t retired_by_gap = 0;
  std::uint64_t global_relabel_wall_us = 0;
  std::uint64_t boundary_push_wall_us = 0;
  std::uint64_t local_discharge_wall_us = 0;
};

struct PartitionedHiPrResult {
  bool converged = false;
  mcpd3::Objective maximum_preflow = 0;
  mcpd3::Objective cut_capacity = 0;
  std::vector<std::uint8_t> source_side;
  std::vector<mcpd3::Objective> excess;
  std::vector<int> heights;
  PartitionedHiPrStats stats;
};

// partition_of_node has one entry per node. Source and sink entries are
// ignored; every other entry must be nonnegative.
PartitionedHiPrResult partitionedHiPr(
    int node_count, int source, int sink,
    const std::vector<DirectedArc> &arcs,
    const std::vector<int> &partition_of_node,
    const PartitionedHiPrOptions &options = {});

} // namespace mcpd4::experimental
