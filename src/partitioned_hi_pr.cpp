#include <mcpd4/partitioned_hi_pr.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace mcpd4::experimental {
namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t elapsedUs(Clock::time_point start) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start)
          .count());
}

class PartitionedHiPrSolver {
public:
  PartitionedHiPrSolver(int node_count, int source, int sink,
                        const std::vector<DirectedArc> &input_arcs,
                        const std::vector<int> &partition_of_node,
                        const PartitionedHiPrOptions &options)
      : node_count_(node_count), source_(source), sink_(sink),
        input_arcs_(input_arcs), partitions_(partition_of_node),
        options_(options), nodes_(node_count), height_count_(node_count + 1),
        active_heads_(node_count, -1), level_heads_(node_count, -1),
        coordinator_heads_(node_count, -1),
        coordinator_next_(node_count, -1),
        coordinator_queued_(node_count, false), bfs_queue_(node_count) {
    validateInput();
    buildResidualGraph();
  }

  PartitionedHiPrResult solve() {
    initializePreflow();

    bool converged = false;
    for (std::size_t round = 0; round < options_.max_coordination_rounds;
         ++round) {
      ++stats_.coordination_rounds;
      globalRelabel();
      coordinatorDischarge();
      localDischarge();
      if (!hasFiniteActiveVertex()) {
        converged = true;
        break;
      }
    }

    // Local relabels are valid but not necessarily exact shortest distances.
    // One final BFS gives the canonical weak-source cut classification.
    globalRelabel();
    if (!hasFiniteActiveVertex()) {
      converged = true;
    }

    if (options_.validate_invariants) {
      validatePreflow("final state");
      validateLabels("final global relabel");
    }
    return makeResult(converged);
  }

private:
  struct ResidualArc {
    int head = -1;
    int reverse = -1;
    mcpd3::Capacity residual = 0;
    bool boundary = false;
  };

  struct Node {
    std::size_t first = 0;
    std::size_t end = 0;
    std::size_t current = 0;
    mcpd3::Objective excess = 0;
    int height = 0;
    int next_active = -1;
    int next_at_level = -1;
    int previous_at_level = -1;
    bool in_active_bucket = false;
    bool in_level_bucket = false;
    bool boundary = false;
  };

  void validateInput() const {
    if (node_count_ < 2) {
      throw std::invalid_argument("partitioned hi_pr requires at least two nodes");
    }
    if (source_ < 0 || source_ >= node_count_ || sink_ < 0 ||
        sink_ >= node_count_ || source_ == sink_) {
      throw std::invalid_argument("invalid source or sink");
    }
    if (partitions_.size() != static_cast<std::size_t>(node_count_)) {
      throw std::invalid_argument("partition vector must have one entry per node");
    }
    for (int node = 0; node < node_count_; ++node) {
      if (node != source_ && node != sink_ && partitions_[node] < 0) {
        throw std::invalid_argument("nonterminal partition ids must be nonnegative");
      }
    }
    if (options_.max_coordination_rounds == 0) {
      throw std::invalid_argument("maximum coordination rounds must be positive");
    }
    if (!std::isfinite(options_.global_relabel_work_factor) ||
        options_.global_relabel_work_factor < 0) {
      throw std::invalid_argument(
          "global relabel work factor must be finite and nonnegative");
    }
    for (const auto &arc : input_arcs_) {
      if (arc.tail < 0 || arc.tail >= node_count_ || arc.head < 0 ||
          arc.head >= node_count_) {
        throw std::invalid_argument("arc endpoint is outside the graph");
      }
      if (arc.capacity < 0) {
        throw std::invalid_argument("push-relabel capacities must be nonnegative");
      }
    }
  }

  bool isBoundaryArc(const DirectedArc &arc) const {
    if (arc.capacity == 0 || arc.tail == arc.head || arc.tail == source_ ||
        arc.tail == sink_ || arc.head == source_ || arc.head == sink_) {
      return false;
    }
    return partitions_[arc.tail] != partitions_[arc.head];
  }

  void buildResidualGraph() {
    std::vector<std::size_t> degree(node_count_, 0);
    std::size_t residual_arc_count = 0;
    for (const auto &arc : input_arcs_) {
      if (arc.capacity == 0 || arc.tail == arc.head) {
        continue;
      }
      ++degree[arc.tail];
      ++degree[arc.head];
      residual_arc_count += 2;
    }

    std::size_t offset = 0;
    for (int node = 0; node < node_count_; ++node) {
      nodes_[node].first = offset;
      nodes_[node].end = offset + degree[node];
      nodes_[node].current = offset;
      offset += degree[node];
    }

    arcs_.resize(residual_arc_count);
    stats_.residual_arc_count = residual_arc_count;
    if (options_.validate_invariants) {
      pair_capacity_.resize(residual_arc_count);
    }
    std::vector<std::size_t> cursor(node_count_);
    for (int node = 0; node < node_count_; ++node) {
      cursor[node] = nodes_[node].first;
    }

    for (const auto &input : input_arcs_) {
      if (input.capacity == 0 || input.tail == input.head) {
        continue;
      }
      const int forward = static_cast<int>(cursor[input.tail]++);
      const int reverse = static_cast<int>(cursor[input.head]++);
      const bool boundary = isBoundaryArc(input);
      arcs_[forward] = {input.head, reverse, input.capacity, boundary};
      arcs_[reverse] = {input.tail, forward, 0, boundary};
      if (options_.validate_invariants) {
        pair_capacity_[forward] = input.capacity;
        pair_capacity_[reverse] = input.capacity;
      }
      if (boundary) {
        ++stats_.boundary_directed_arc_count;
        nodes_[input.tail].boundary = true;
        nodes_[input.head].boundary = true;
      }
    }
    stats_.boundary_node_count = static_cast<std::size_t>(std::count_if(
        nodes_.begin(), nodes_.end(), [](const Node &node) {
          return node.boundary;
        }));
  }

  void initializePreflow() {
    for (auto &node : nodes_) {
      node.excess = 0;
      node.height = 0;
      node.current = node.first;
      node.next_active = -1;
      node.next_at_level = -1;
      node.previous_at_level = -1;
      node.in_active_bucket = false;
      node.in_level_bucket = false;
    }
    nodes_[source_].height = node_count_;

    for (std::size_t index = nodes_[source_].first;
         index < nodes_[source_].end; ++index) {
      auto &arc = arcs_[index];
      if (arc.residual <= 0 || arc.head == source_) {
        continue;
      }
      const mcpd3::Capacity delta = arc.residual;
      arc.residual = 0;
      auto &reverse = arcs_[arc.reverse];
      reverse.residual = mcpd3::checked_add(
          reverse.residual, delta, "residual capacity overflow at initialization");
      nodes_[arc.head].excess = mcpd3::checked_add(
          nodes_[arc.head].excess, mcpd3::widen_capacity(delta),
          "node excess overflow at initialization");
    }
    if (options_.validate_invariants) {
      validatePreflow("initial saturation");
    }
  }

  void globalRelabel() {
    const auto start = Clock::now();
    ++stats_.global_relabels;
    std::fill(height_count_.begin(), height_count_.end(), 0);
    std::fill(level_heads_.begin(), level_heads_.end(), -1);
    maximum_finite_height_ = 0;
    for (auto &node : nodes_) {
      node.height = node_count_;
      node.current = node.first;
      node.in_active_bucket = false;
      node.next_active = -1;
      node.next_at_level = -1;
      node.previous_at_level = -1;
      node.in_level_bucket = false;
    }

    std::size_t head = 0;
    std::size_t tail = 0;
    nodes_[sink_].height = 0;
    bfs_queue_[tail++] = sink_;
    while (head < tail) {
      const int node = bfs_queue_[head++];
      const int next_height = nodes_[node].height + 1;
      for (std::size_t index = nodes_[node].first;
           index < nodes_[node].end; ++index) {
        const auto &outgoing = arcs_[index];
        const int predecessor = outgoing.head;
        if (predecessor == source_ || nodes_[predecessor].height != node_count_ ||
            arcs_[outgoing.reverse].residual <= 0) {
          continue;
        }
        nodes_[predecessor].height = next_height;
        bfs_queue_[tail++] = predecessor;
      }
    }
    nodes_[source_].height = node_count_;
    for (int node = 0; node < node_count_; ++node) {
      if (nodes_[node].height < node_count_) {
        addToLevel(node);
      }
    }
    stats_.global_relabel_wall_us += elapsedUs(start);
    local_work_since_global_relabel_ = 0;
    if (options_.validate_invariants) {
      validateLabels("global relabel");
    }
  }

  mcpd3::Capacity pushAmount(int tail, const ResidualArc &arc) const {
    const mcpd3::Objective residual = mcpd3::widen_capacity(arc.residual);
    if (nodes_[tail].excess < residual) {
      return mcpd3::narrow_objective_to_capacity(nodes_[tail].excess);
    }
    return arc.residual;
  }

  void pushResidual(int tail, int arc_index, mcpd3::Capacity delta) {
    auto &arc = arcs_[arc_index];
    auto &reverse = arcs_[arc.reverse];
    arc.residual = mcpd3::checked_subtract(
        arc.residual, delta, "forward residual capacity underflow");
    reverse.residual = mcpd3::checked_add(
        reverse.residual, delta, "reverse residual capacity overflow");
    const mcpd3::Objective widened = mcpd3::widen_capacity(delta);
    nodes_[tail].excess = mcpd3::checked_subtract(
        nodes_[tail].excess, widened, "tail excess underflow");
    nodes_[arc.head].excess = mcpd3::checked_add(
        nodes_[arc.head].excess, widened, "head excess overflow");
  }

  void enqueueCoordinator(int node) {
    if (node == source_ || node == sink_ || coordinator_queued_[node] ||
        nodes_[node].excess <= 0 || nodes_[node].height >= node_count_) {
      return;
    }
    const int height = nodes_[node].height;
    coordinator_next_[node] = coordinator_heads_[height];
    coordinator_heads_[height] = node;
    coordinator_queued_[node] = true;
  }

  void coordinatorDischarge() {
    const auto start = Clock::now();
    std::fill(coordinator_heads_.begin(), coordinator_heads_.end(), -1);
    std::fill(coordinator_queued_.begin(), coordinator_queued_.end(), false);
    std::fill(coordinator_next_.begin(), coordinator_next_.end(), -1);

    int maximum_height = -1;
    for (int node = 0; node < node_count_; ++node) {
      enqueueCoordinator(node);
      if (coordinator_queued_[node]) {
        maximum_height = std::max(maximum_height, nodes_[node].height);
      }
    }

    for (int height = maximum_height; height > 0; --height) {
      while (coordinator_heads_[height] != -1) {
        const int tail = coordinator_heads_[height];
        coordinator_heads_[height] = coordinator_next_[tail];
        coordinator_next_[tail] = -1;
        coordinator_queued_[tail] = false;
        if (nodes_[tail].excess <= 0 || nodes_[tail].height != height) {
          continue;
        }
        for (std::size_t index = nodes_[tail].first;
             index < nodes_[tail].end && nodes_[tail].excess > 0; ++index) {
          const auto &arc = arcs_[index];
          if (!arc.boundary || arc.residual <= 0 ||
              nodes_[arc.head].height + 1 != height) {
            continue;
          }
          const bool head_was_inactive = nodes_[arc.head].excess == 0;
          const mcpd3::Capacity delta = pushAmount(tail, arc);
          pushResidual(tail, static_cast<int>(index), delta);
          ++stats_.boundary_pushes;
          if (head_was_inactive) {
            enqueueCoordinator(arc.head);
          }
        }
      }
    }
    stats_.boundary_push_wall_us += elapsedUs(start);
    if (options_.validate_invariants) {
      validatePreflow("coordinator discharge");
      validateLabels("coordinator discharge");
    }
  }

  bool hasAdmissibleInteriorArc(int tail) const {
    const int target_height = nodes_[tail].height - 1;
    for (std::size_t index = nodes_[tail].first;
         index < nodes_[tail].end; ++index) {
      const auto &arc = arcs_[index];
      if (!arc.boundary && arc.residual > 0 &&
          nodes_[arc.head].height == target_height) {
        return true;
      }
    }
    return false;
  }

  void activateLocal(int node) {
    if (node == source_ || node == sink_ || nodes_[node].excess <= 0 ||
        nodes_[node].height >= node_count_ || nodes_[node].in_active_bucket) {
      return;
    }
    if (nodes_[node].boundary && !hasAdmissibleInteriorArc(node)) {
      return;
    }
    if (nodes_[node].boundary) {
      nodes_[node].current = nodes_[node].first;
    }
    const int height = nodes_[node].height;
    nodes_[node].next_active = active_heads_[height];
    active_heads_[height] = node;
    nodes_[node].in_active_bucket = true;
    maximum_active_height_ = std::max(maximum_active_height_, height);
  }

  int popHighestLocal() {
    while (maximum_active_height_ >= 0) {
      while (active_heads_[maximum_active_height_] != -1) {
        const int node = active_heads_[maximum_active_height_];
        active_heads_[maximum_active_height_] = nodes_[node].next_active;
        nodes_[node].next_active = -1;
        if (!nodes_[node].in_active_bucket ||
            nodes_[node].height != maximum_active_height_ ||
            nodes_[node].excess <= 0) {
          continue;
        }
        nodes_[node].in_active_bucket = false;
        return node;
      }
      --maximum_active_height_;
    }
    return -1;
  }

  void addToLevel(int node) {
    const int height = nodes_[node].height;
    if (height < 0 || height >= node_count_ || nodes_[node].in_level_bucket) {
      throw std::logic_error("invalid insertion into height bucket");
    }
    const int old_head = level_heads_[height];
    nodes_[node].previous_at_level = -1;
    nodes_[node].next_at_level = old_head;
    if (old_head != -1) {
      nodes_[old_head].previous_at_level = node;
    }
    level_heads_[height] = node;
    nodes_[node].in_level_bucket = true;
    ++height_count_[height];
    maximum_finite_height_ = std::max(maximum_finite_height_, height);
  }

  void removeFromLevel(int node) {
    if (!nodes_[node].in_level_bucket) {
      throw std::logic_error("node is missing from its height bucket");
    }
    const int height = nodes_[node].height;
    const int previous = nodes_[node].previous_at_level;
    const int next = nodes_[node].next_at_level;
    if (previous == -1) {
      level_heads_[height] = next;
    } else {
      nodes_[previous].next_at_level = next;
    }
    if (next != -1) {
      nodes_[next].previous_at_level = previous;
    }
    nodes_[node].previous_at_level = -1;
    nodes_[node].next_at_level = -1;
    nodes_[node].in_level_bucket = false;
    --height_count_[height];
  }

  void applyGap(int empty_height) {
    ++stats_.gap_events;
    for (int height = empty_height + 1;
         height <= maximum_finite_height_; ++height) {
      active_heads_[height] = -1;
      while (level_heads_[height] != -1) {
        const int node = level_heads_[height];
        removeFromLevel(node);
        nodes_[node].height = node_count_;
        nodes_[node].current = nodes_[node].first;
        nodes_[node].in_active_bucket = false;
        nodes_[node].next_active = -1;
        ++stats_.retired_by_gap;
      }
    }
    maximum_finite_height_ = empty_height - 1;
  }

  void notifyBoundaryNeighbors(int node) {
    for (std::size_t index = nodes_[node].first;
         index < nodes_[node].end; ++index) {
      const int neighbor = arcs_[index].head;
      if (nodes_[neighbor].boundary && nodes_[neighbor].excess > 0) {
        activateLocal(neighbor);
      }
    }
  }

  void relabelLocal(int node) {
    const int old_height = nodes_[node].height;
    int minimum_height = node_count_;
    std::size_t minimum_arc = nodes_[node].end;
    for (std::size_t index = nodes_[node].first;
         index < nodes_[node].end; ++index) {
      const auto &arc = arcs_[index];
      ++stats_.local_arc_scans;
      ++local_work_since_global_relabel_;
      if (arc.boundary || arc.residual <= 0 ||
          nodes_[arc.head].height >= minimum_height) {
        continue;
      }
      minimum_height = nodes_[arc.head].height;
      minimum_arc = index;
    }

    removeFromLevel(node);
    const int new_height = minimum_height < node_count_
                               ? minimum_height + 1
                               : node_count_;
    nodes_[node].height = new_height;
    nodes_[node].current = minimum_arc;
    if (new_height < node_count_) {
      addToLevel(node);
    }
    ++stats_.local_relabels;
    local_work_since_global_relabel_ += 12;

    if (old_height > 0 && height_count_[old_height] == 0) {
      applyGap(old_height);
    }
    if (nodes_[node].height < node_count_) {
      notifyBoundaryNeighbors(node);
    }
  }

  void dischargeLocal(int tail) {
    while (nodes_[tail].excess > 0 && nodes_[tail].height < node_count_) {
      const int target_height = nodes_[tail].height - 1;
      std::size_t index = nodes_[tail].current;
      for (; index < nodes_[tail].end && nodes_[tail].excess > 0; ++index) {
        const auto &arc = arcs_[index];
        ++stats_.local_arc_scans;
        ++local_work_since_global_relabel_;
        if (arc.boundary || arc.residual <= 0 ||
            nodes_[arc.head].height != target_height) {
          continue;
        }
        const bool head_was_inactive = nodes_[arc.head].excess == 0;
        const mcpd3::Capacity delta = pushAmount(tail, arc);
        pushResidual(tail, static_cast<int>(index), delta);
        ++stats_.local_pushes;
        if (head_was_inactive) {
          activateLocal(arc.head);
        }
      }
      nodes_[tail].current = index;
      if (nodes_[tail].excess == 0) {
        break;
      }
      if (nodes_[tail].boundary) {
        break;
      }
      relabelLocal(tail);
    }
  }

  void localDischarge() {
    const auto start = Clock::now();
    std::fill(active_heads_.begin(), active_heads_.end(), -1);
    maximum_active_height_ = -1;
    for (auto &node : nodes_) {
      node.in_active_bucket = false;
      node.next_active = -1;
    }
    for (int node = 0; node < node_count_; ++node) {
      activateLocal(node);
    }

    while (true) {
      const int node = popHighestLocal();
      if (node == -1) {
        break;
      }
      dischargeLocal(node);
      if (globalRelabelWorkLimitReached()) {
        ++stats_.work_global_relabel_interruptions;
        break;
      }
    }
    stats_.local_discharge_wall_us += elapsedUs(start);
    if (options_.validate_invariants) {
      validatePreflow("local discharge");
      validateLabels("local discharge");
    }
  }

  bool globalRelabelWorkLimitReached() const {
    if (options_.global_relabel_work_factor == 0) {
      return false;
    }
    const long double base =
        6.0L * static_cast<long double>(node_count_) +
        static_cast<long double>(input_arcs_.size());
    const long double limit =
        static_cast<long double>(options_.global_relabel_work_factor) * base;
    return static_cast<long double>(local_work_since_global_relabel_) > limit;
  }

  bool hasFiniteActiveVertex() const {
    for (int node = 0; node < node_count_; ++node) {
      if (node != source_ && node != sink_ && nodes_[node].excess > 0 &&
          nodes_[node].height < node_count_) {
        return true;
      }
    }
    return false;
  }

  void validatePreflow(const char *stage) const {
    for (int node = 0; node < node_count_; ++node) {
      if (nodes_[node].excess < 0) {
        throw std::logic_error(std::string(stage) +
                               ": negative non-source excess");
      }
    }
    for (std::size_t index = 0; index < arcs_.size(); ++index) {
      const auto &arc = arcs_[index];
      if (arc.residual < 0) {
        throw std::logic_error(std::string(stage) +
                               ": negative residual capacity");
      }
      const auto sum = mcpd3::checked_add(
          arc.residual, arcs_[arc.reverse].residual,
          "residual pair capacity overflow during validation");
      if (sum != pair_capacity_[index]) {
        throw std::logic_error(std::string(stage) +
                               ": residual pair lost capacity");
      }
    }
  }

  void validateLabels(const char *stage) const {
    if (nodes_[sink_].height != 0 || nodes_[source_].height != node_count_) {
      throw std::logic_error(std::string(stage) +
                             ": terminal height invariant failed");
    }
    for (int tail = 0; tail < node_count_; ++tail) {
      if (tail == source_ || nodes_[tail].height >= node_count_) {
        continue;
      }
      for (std::size_t index = nodes_[tail].first;
           index < nodes_[tail].end; ++index) {
        const auto &arc = arcs_[index];
        if (arc.residual > 0 &&
            nodes_[tail].height > nodes_[arc.head].height + 1) {
          throw std::logic_error(std::string(stage) +
                                 ": residual labeling is invalid");
        }
      }
    }

    std::vector<int> observed_count(node_count_, 0);
    std::vector<bool> observed_node(node_count_, false);
    for (int height = 0; height < node_count_; ++height) {
      int previous = -1;
      int traversed = 0;
      for (int node = level_heads_[height]; node != -1;
           node = nodes_[node].next_at_level) {
        if (++traversed > node_count_ || node < 0 || node >= node_count_ ||
            observed_node[node] || !nodes_[node].in_level_bucket ||
            nodes_[node].height != height ||
            nodes_[node].previous_at_level != previous) {
          throw std::logic_error(std::string(stage) +
                                 ": corrupt height bucket");
        }
        observed_node[node] = true;
        ++observed_count[height];
        previous = node;
      }
      if (observed_count[height] != height_count_[height]) {
        throw std::logic_error(std::string(stage) +
                               ": height bucket count mismatch");
      }
    }
    for (int node = 0; node < node_count_; ++node) {
      const bool should_be_bucketed = nodes_[node].height < node_count_;
      if (observed_node[node] != should_be_bucketed ||
          nodes_[node].in_level_bucket != should_be_bucketed) {
        throw std::logic_error(std::string(stage) +
                               ": node height-bucket membership mismatch");
      }
    }
  }

  PartitionedHiPrResult makeResult(bool converged) const {
    PartitionedHiPrResult result;
    result.converged = converged;
    result.maximum_preflow = nodes_[sink_].excess;
    result.source_side.resize(node_count_);
    result.excess.resize(node_count_);
    result.heights.resize(node_count_);
    for (int node = 0; node < node_count_; ++node) {
      result.source_side[node] = nodes_[node].height >= node_count_ ? 1 : 0;
      result.excess[node] = nodes_[node].excess;
      result.heights[node] = nodes_[node].height;
    }
    result.source_side[source_] = 1;
    result.source_side[sink_] = 0;
    for (const auto &arc : input_arcs_) {
      if (result.source_side[arc.tail] && !result.source_side[arc.head]) {
        result.cut_capacity = mcpd3::checked_add(
            result.cut_capacity, mcpd3::widen_capacity(arc.capacity),
            "cut capacity overflow");
      }
    }
    result.stats = stats_;
    if (converged && result.maximum_preflow != result.cut_capacity) {
      throw std::logic_error(
          "maximum preflow value does not equal the residual cut capacity");
    }
    return result;
  }

  int node_count_;
  int source_;
  int sink_;
  const std::vector<DirectedArc> &input_arcs_;
  const std::vector<int> &partitions_;
  PartitionedHiPrOptions options_;
  std::vector<Node> nodes_;
  std::vector<ResidualArc> arcs_;
  std::vector<mcpd3::Capacity> pair_capacity_;
  std::vector<int> height_count_;
  std::vector<int> active_heads_;
  std::vector<int> level_heads_;
  int maximum_active_height_ = -1;
  int maximum_finite_height_ = 0;
  std::size_t local_work_since_global_relabel_ = 0;
  std::vector<int> coordinator_heads_;
  std::vector<int> coordinator_next_;
  std::vector<bool> coordinator_queued_;
  std::vector<int> bfs_queue_;
  PartitionedHiPrStats stats_;
};

} // namespace

PartitionedHiPrResult partitionedHiPr(
    int node_count, int source, int sink,
    const std::vector<DirectedArc> &arcs,
    const std::vector<int> &partition_of_node,
    const PartitionedHiPrOptions &options) {
  return PartitionedHiPrSolver(node_count, source, sink, arcs,
                               partition_of_node, options)
      .solve();
}

} // namespace mcpd4::experimental
