#include <mcpd4/partitioned_hi_pr.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>
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
                        const std::vector<std::vector<int>> &partitionings,
                        const PartitionedHiPrOptions &options,
                        bool partition_cover_mode)
      : node_count_(node_count), source_(source), sink_(sink),
        input_arcs_(input_arcs), partitionings_(partitionings),
        options_(options), partition_cover_mode_(partition_cover_mode),
        nodes_(node_count), height_count_(node_count + 1),
        active_heads_(node_count, -1), level_heads_(node_count, -1),
        coordinator_heads_(node_count, -1),
        coordinator_next_(node_count, -1),
        coordinator_queued_(node_count, false), bfs_queue_(node_count) {
    validateInput();
    buildResidualGraph();
  }

  PartitionedHiPrResult solve() {
    initializePreflow();

    bool converged = partition_cover_mode_ ? solvePartitionCover()
                                           : solveSinglePartitioning();

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
  };

  bool solveSinglePartitioning() {
    for (std::size_t round = 0; round < options_.max_coordination_rounds;
         ++round) {
      ++stats_.coordination_rounds;
      globalRelabel();
      coordinatorDischarge();
      localDischarge();
      if (!hasFiniteActiveVertex()) {
        return true;
      }
    }
    return false;
  }

  bool solvePartitionCover() {
    globalRelabel();
    for (std::size_t cycle = 0; cycle < options_.max_coordination_rounds;
         ++cycle) {
      ++stats_.coordination_rounds;
      ++stats_.partition_cover_cycles;
      for (std::size_t cover = 0; cover < partitionings_.size(); ++cover) {
        current_partitioning_ = cover;
        ++stats_.partition_local_phases;
        localDischarge();
        if (globalRelabelWorkLimitReached()) {
          globalRelabel();
        }
      }
      if (!hasFiniteActiveVertex()) {
        return true;
      }
    }
    return false;
  }

  void validateInput() const {
    if (node_count_ < 2) {
      throw std::invalid_argument("partitioned hi_pr requires at least two nodes");
    }
    if (source_ < 0 || source_ >= node_count_ || sink_ < 0 ||
        sink_ >= node_count_ || source_ == sink_) {
      throw std::invalid_argument("invalid source or sink");
    }
    if (partitionings_.empty()) {
      throw std::invalid_argument("at least one partitioning is required");
    }
    if (!partition_cover_mode_ && partitionings_.size() != 1) {
      throw std::invalid_argument(
          "single-partition mode requires exactly one partitioning");
    }
    for (const auto &partitioning : partitionings_) {
      if (partitioning.size() != static_cast<std::size_t>(node_count_)) {
        throw std::invalid_argument(
            "partition vector must have one entry per node");
      }
      for (int node = 0; node < node_count_; ++node) {
        if (node != source_ && node != sink_ && partitioning[node] < 0) {
          throw std::invalid_argument(
              "nonterminal partition ids must be nonnegative");
        }
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

  bool isBoundaryArc(const DirectedArc &arc,
                     std::size_t partitioning) const {
    if (arc.capacity == 0 || arc.tail == arc.head || arc.tail == source_ ||
        arc.tail == sink_ || arc.head == source_ || arc.head == sink_) {
      return false;
    }
    return partitionings_[partitioning][arc.tail] !=
           partitionings_[partitioning][arc.head];
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
    boundary_arcs_.assign(
        partitionings_.size(),
        std::vector<std::uint8_t>(residual_arc_count, 0));
    boundary_nodes_.assign(
        partitionings_.size(),
        std::vector<std::uint8_t>(static_cast<std::size_t>(node_count_), 0));
    stats_.partitioning_count = partitionings_.size();
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
      arcs_[forward] = {input.head, reverse, input.capacity};
      arcs_[reverse] = {input.tail, forward, 0};
      if (options_.validate_invariants) {
        pair_capacity_[forward] = input.capacity;
        pair_capacity_[reverse] = input.capacity;
      }
      for (std::size_t partitioning = 0;
           partitioning < partitionings_.size(); ++partitioning) {
        if (!isBoundaryArc(input, partitioning)) {
          continue;
        }
        boundary_arcs_[partitioning][forward] = 1;
        boundary_arcs_[partitioning][reverse] = 1;
        boundary_nodes_[partitioning][input.tail] = 1;
        boundary_nodes_[partitioning][input.head] = 1;
        ++stats_.boundary_directed_arc_count;
      }
    }

    stats_.minimum_boundary_node_count =
        std::numeric_limits<std::size_t>::max();
    for (const auto &boundary_nodes : boundary_nodes_) {
      const std::size_t count = static_cast<std::size_t>(std::count(
          boundary_nodes.begin(), boundary_nodes.end(), std::uint8_t{1}));
      stats_.boundary_node_count += count;
      stats_.minimum_boundary_node_count =
          std::min(stats_.minimum_boundary_node_count, count);
      stats_.maximum_boundary_node_count =
          std::max(stats_.maximum_boundary_node_count, count);
    }

    if (partition_cover_mode_) {
      validatePartitionCover();
    }
  }

  void validatePartitionCover() const {
    for (int node = 0; node < node_count_; ++node) {
      if (node == source_ || node == sink_) {
        continue;
      }
      bool interior_somewhere = false;
      for (const auto &boundary_nodes : boundary_nodes_) {
        if (boundary_nodes[node] == 0) {
          interior_somewhere = true;
          break;
        }
      }
      if (!interior_somewhere) {
        throw std::invalid_argument(
            "partition cover leaves a nonterminal boundary in every "
            "partitioning");
      }
    }

    for (const auto &input : input_arcs_) {
      if (input.capacity == 0 || input.tail == input.head ||
          input.tail == source_ || input.tail == sink_ ||
          input.head == source_ || input.head == sink_) {
        continue;
      }
      bool local_somewhere = false;
      for (std::size_t partitioning = 0;
           partitioning < partitionings_.size(); ++partitioning) {
        if (!isBoundaryArc(input, partitioning)) {
          local_somewhere = true;
          break;
        }
      }
      if (!local_somewhere) {
        throw std::invalid_argument(
            "partition cover leaves an arc crossing every partitioning");
      }
    }
  }

  bool isCurrentBoundaryArc(std::size_t arc) const {
    return boundary_arcs_[current_partitioning_][arc] != 0;
  }

  bool isCurrentBoundaryNode(int node) const {
    return boundary_nodes_[current_partitioning_][node] != 0;
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
          if (!isCurrentBoundaryArc(index) || arc.residual <= 0 ||
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
      if (!isCurrentBoundaryArc(index) && arc.residual > 0 &&
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
    if (isCurrentBoundaryNode(node) && !hasAdmissibleInteriorArc(node)) {
      return;
    }
    if (isCurrentBoundaryNode(node)) {
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
      if (isCurrentBoundaryNode(neighbor) && nodes_[neighbor].excess > 0) {
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
      if (isCurrentBoundaryArc(index) || arc.residual <= 0 ||
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
        if (isCurrentBoundaryArc(index) || arc.residual <= 0 ||
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
      if (isCurrentBoundaryNode(tail)) {
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
      // A different cover exposes a different arc subset, so a current-arc
      // cursor from the previous phase cannot be reused safely.
      node.current = node.first;
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
  std::vector<std::vector<int>> partitionings_;
  PartitionedHiPrOptions options_;
  bool partition_cover_mode_ = false;
  std::size_t current_partitioning_ = 0;
  std::vector<Node> nodes_;
  std::vector<ResidualArc> arcs_;
  std::vector<std::vector<std::uint8_t>> boundary_arcs_;
  std::vector<std::vector<std::uint8_t>> boundary_nodes_;
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
                               {partition_of_node}, options, false)
      .solve();
}

PartitionedHiPrResult partitionCoverHiPr(
    int node_count, int source, int sink,
    const std::vector<DirectedArc> &arcs,
    const std::vector<std::vector<int>> &partitionings,
    const PartitionedHiPrOptions &options) {
  return PartitionedHiPrSolver(node_count, source, sink, arcs, partitionings,
                               options, true)
      .solve();
}

std::vector<std::vector<int>> makeSeparatedContiguousPartitionCover(
    int node_count, int source, int sink,
    const std::vector<DirectedArc> &arcs, int partition_count,
    int partitioning_count) {
  if (node_count < 2 || source < 0 || source >= node_count || sink < 0 ||
      sink >= node_count || source == sink) {
    throw std::invalid_argument("invalid graph terminals for partition cover");
  }
  if (partition_count <= 0 || partitioning_count <= 0) {
    throw std::invalid_argument(
        "partition and partitioning counts must be positive");
  }

  std::vector<int> ordered_nodes;
  ordered_nodes.reserve(static_cast<std::size_t>(node_count - 2));
  for (int node = 0; node < node_count; ++node) {
    if (node != source && node != sink) {
      ordered_nodes.push_back(node);
    }
  }
  if (partition_count > static_cast<int>(ordered_nodes.size())) {
    throw std::invalid_argument(
        "partition count exceeds the number of nonterminal nodes");
  }
  for (const auto &arc : arcs) {
    if (arc.tail < 0 || arc.tail >= node_count || arc.head < 0 ||
        arc.head >= node_count || arc.capacity < 0) {
      throw std::invalid_argument("invalid arc in partition-cover graph");
    }
  }

  struct Candidate {
    std::vector<int> partitions;
    std::vector<std::uint8_t> boundary;
  };

  const int candidate_count = std::max(9, 4 * partitioning_count + 1);
  std::vector<Candidate> candidates;
  candidates.reserve(static_cast<std::size_t>(candidate_count));
  const long double chunk =
      static_cast<long double>(ordered_nodes.size()) / partition_count;
  for (int candidate_index = 0; candidate_index < candidate_count;
       ++candidate_index) {
    const long double normalized =
        candidate_count == 1
            ? 0.0L
            : (2.0L * candidate_index / (candidate_count - 1) - 1.0L);
    const long double shift = normalized * 0.45L * chunk;

    std::vector<int> cuts;
    cuts.reserve(static_cast<std::size_t>(partition_count - 1));
    int previous = 0;
    for (int partition = 1; partition < partition_count; ++partition) {
      const int minimum = previous + 1;
      const int maximum = static_cast<int>(ordered_nodes.size()) -
                          (partition_count - partition);
      const long double ideal =
          static_cast<long double>(partition) * ordered_nodes.size() /
              partition_count +
          shift;
      const int cut = std::clamp(static_cast<int>(std::llround(ideal)),
                                 minimum, maximum);
      cuts.push_back(cut);
      previous = cut;
    }

    Candidate candidate;
    candidate.partitions.assign(static_cast<std::size_t>(node_count), -1);
    candidate.boundary.assign(static_cast<std::size_t>(node_count), 0);
    int partition = 0;
    std::size_t next_cut = 0;
    for (std::size_t rank = 0; rank < ordered_nodes.size(); ++rank) {
      while (next_cut < cuts.size() &&
             rank >= static_cast<std::size_t>(cuts[next_cut])) {
        ++partition;
        ++next_cut;
      }
      candidate.partitions[ordered_nodes[rank]] = partition;
    }
    for (const auto &arc : arcs) {
      if (arc.capacity == 0 || arc.tail == arc.head || arc.tail == source ||
          arc.tail == sink || arc.head == source || arc.head == sink) {
        continue;
      }
      if (candidate.partitions[arc.tail] != candidate.partitions[arc.head]) {
        candidate.boundary[arc.tail] = 1;
        candidate.boundary[arc.head] = 1;
      }
    }
    candidates.push_back(std::move(candidate));
  }

  std::vector<std::size_t> degree(static_cast<std::size_t>(node_count), 0);
  for (const auto &arc : arcs) {
    if (arc.capacity > 0 && arc.tail != arc.head && arc.tail != source &&
        arc.tail != sink && arc.head != source && arc.head != sink) {
      ++degree[arc.tail];
      ++degree[arc.head];
    }
  }
  std::vector<std::size_t> offsets(static_cast<std::size_t>(node_count) + 1,
                                   0);
  for (int node = 0; node < node_count; ++node) {
    offsets[node + 1] = offsets[node] + degree[node];
  }
  std::vector<int> neighbors(offsets.back());
  std::vector<std::size_t> cursors = offsets;
  for (const auto &arc : arcs) {
    if (arc.capacity > 0 && arc.tail != arc.head && arc.tail != source &&
        arc.tail != sink && arc.head != source && arc.head != sink) {
      neighbors[cursors[arc.tail]++] = arc.head;
      neighbors[cursors[arc.head]++] = arc.tail;
    }
  }

  std::vector<bool> used(candidates.size(), false);
  std::vector<std::uint8_t> persistent_boundary(
      static_cast<std::size_t>(node_count), 0);
  std::vector<std::uint8_t> boundary_union(static_cast<std::size_t>(node_count),
                                           0);
  std::vector<std::vector<int>> result;
  result.reserve(static_cast<std::size_t>(partitioning_count));

  for (int selection = 0; selection < partitioning_count; ++selection) {
    std::vector<int> distance(static_cast<std::size_t>(node_count),
                              std::numeric_limits<int>::max());
    std::queue<int> queue;
    if (selection > 0) {
      for (int node = 0; node < node_count; ++node) {
        if (boundary_union[node] != 0) {
          distance[node] = 0;
          queue.push(node);
        }
      }
      while (!queue.empty()) {
        const int tail = queue.front();
        queue.pop();
        for (std::size_t index = offsets[tail]; index < offsets[tail + 1];
             ++index) {
          const int head = neighbors[index];
          if (distance[head] == std::numeric_limits<int>::max()) {
            distance[head] = distance[tail] + 1;
            queue.push(head);
          }
        }
      }
    }

    int best = -1;
    std::size_t best_persistent_overlap =
        std::numeric_limits<std::size_t>::max();
    int best_distance = -1;
    for (int candidate_index = 0;
         candidate_index < static_cast<int>(candidates.size());
         ++candidate_index) {
      if (used[candidate_index]) {
        continue;
      }
      const auto &boundary = candidates[candidate_index].boundary;
      std::size_t overlap = 0;
      int minimum_distance = std::numeric_limits<int>::max();
      for (int node = 0; node < node_count; ++node) {
        if (boundary[node] == 0) {
          continue;
        }
        if (selection > 0 && persistent_boundary[node] != 0) {
          ++overlap;
        }
        minimum_distance = std::min(minimum_distance, distance[node]);
      }
      if (selection == 0) {
        const int center = candidate_count / 2;
        if (best == -1 || std::abs(candidate_index - center) <
                              std::abs(best - center)) {
          best = candidate_index;
        }
      } else if (best == -1 || overlap < best_persistent_overlap ||
                 (overlap == best_persistent_overlap &&
                  minimum_distance > best_distance)) {
        best = candidate_index;
        best_persistent_overlap = overlap;
        best_distance = minimum_distance;
      }
    }
    if (best < 0) {
      throw std::logic_error("failed to select a partition-cover candidate");
    }
    used[best] = true;
    result.push_back(candidates[best].partitions);
    for (int node = 0; node < node_count; ++node) {
      if (selection == 0) {
        persistent_boundary[node] = candidates[best].boundary[node];
      } else {
        persistent_boundary[node] = static_cast<std::uint8_t>(
            persistent_boundary[node] != 0 &&
            candidates[best].boundary[node] != 0);
      }
      boundary_union[node] = static_cast<std::uint8_t>(
          boundary_union[node] != 0 || candidates[best].boundary[node] != 0);
    }
  }

  for (int node = 0; node < node_count; ++node) {
    if (node != source && node != sink && persistent_boundary[node] != 0) {
      throw std::runtime_error(
          "candidate partitionings cannot provide complete interior coverage");
    }
  }
  return result;
}

PartitionCoverGeometry analyzePartitionCoverGeometry(
    int node_count, int source, int sink,
    const std::vector<DirectedArc> &arcs,
    const std::vector<std::vector<int>> &partitionings) {
  if (node_count < 2 || source < 0 || source >= node_count || sink < 0 ||
      sink >= node_count || source == sink || partitionings.empty()) {
    throw std::invalid_argument("invalid partition-cover geometry input");
  }
  for (const auto &partitioning : partitionings) {
    if (partitioning.size() != static_cast<std::size_t>(node_count)) {
      throw std::invalid_argument(
          "partition-cover geometry vector has the wrong size");
    }
  }

  std::vector<std::vector<std::uint8_t>> boundaries(
      partitionings.size(),
      std::vector<std::uint8_t>(static_cast<std::size_t>(node_count), 0));
  PartitionCoverGeometry geometry;
  geometry.boundary_node_counts.resize(partitionings.size(), 0);
  for (const auto &arc : arcs) {
    if (arc.tail < 0 || arc.tail >= node_count || arc.head < 0 ||
        arc.head >= node_count || arc.capacity < 0) {
      throw std::invalid_argument("invalid arc in partition-cover geometry");
    }
    if (arc.capacity == 0 || arc.tail == arc.head || arc.tail == source ||
        arc.tail == sink || arc.head == source || arc.head == sink) {
      continue;
    }
    bool boundary_everywhere = true;
    for (std::size_t cover = 0; cover < partitionings.size(); ++cover) {
      if (partitionings[cover][arc.tail] ==
          partitionings[cover][arc.head]) {
        boundary_everywhere = false;
      } else {
        boundaries[cover][arc.tail] = 1;
        boundaries[cover][arc.head] = 1;
      }
    }
    if (boundary_everywhere) {
      ++geometry.uncovered_directed_arc_count;
    }
  }
  for (std::size_t cover = 0; cover < boundaries.size(); ++cover) {
    geometry.boundary_node_counts[cover] = static_cast<std::size_t>(std::count(
        boundaries[cover].begin(), boundaries[cover].end(), std::uint8_t{1}));
  }
  for (int node = 0; node < node_count; ++node) {
    if (node == source || node == sink) {
      continue;
    }
    bool boundary_everywhere = true;
    for (const auto &boundary : boundaries) {
      boundary_everywhere = boundary_everywhere && boundary[node] != 0;
    }
    if (boundary_everywhere) {
      ++geometry.uncovered_node_count;
    }
  }

  std::vector<std::vector<int>> adjacency(static_cast<std::size_t>(node_count));
  for (const auto &arc : arcs) {
    if (arc.capacity > 0 && arc.tail != arc.head && arc.tail != source &&
        arc.tail != sink && arc.head != source && arc.head != sink) {
      adjacency[arc.tail].push_back(arc.head);
      adjacency[arc.head].push_back(arc.tail);
    }
  }
  int minimum_pair_distance = std::numeric_limits<int>::max();
  for (std::size_t first = 0; first < boundaries.size(); ++first) {
    if (geometry.boundary_node_counts[first] == 0) {
      continue;
    }
    std::vector<int> distance(static_cast<std::size_t>(node_count), -1);
    std::queue<int> queue;
    for (int node = 0; node < node_count; ++node) {
      if (boundaries[first][node] != 0) {
        distance[node] = 0;
        queue.push(node);
      }
    }
    while (!queue.empty()) {
      const int tail = queue.front();
      queue.pop();
      for (const int head : adjacency[tail]) {
        if (distance[head] == -1) {
          distance[head] = distance[tail] + 1;
          queue.push(head);
        }
      }
    }
    for (std::size_t second = first + 1; second < boundaries.size(); ++second) {
      if (geometry.boundary_node_counts[second] == 0) {
        continue;
      }
      int pair_distance = std::numeric_limits<int>::max();
      for (int node = 0; node < node_count; ++node) {
        if (boundaries[second][node] != 0 && distance[node] >= 0) {
          pair_distance = std::min(pair_distance, distance[node]);
        }
      }
      minimum_pair_distance = std::min(minimum_pair_distance, pair_distance);
    }
  }
  if (minimum_pair_distance != std::numeric_limits<int>::max()) {
    geometry.minimum_pairwise_boundary_distance = minimum_pair_distance;
  }
  return geometry;
}

} // namespace mcpd4::experimental
