#include <mcpd4/partitioned_hi_pr.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <queue>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using mcpd3::Objective;
using mcpd4::experimental::DirectedArc;
using mcpd4::experimental::PartitionedHiPrOptions;
using mcpd4::experimental::PartitionedHiPrResult;

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

DirectedArc arc(int tail, int head, long capacity) {
  return {tail, head, mcpd3::capacity_from_integer(capacity)};
}

class Dinic {
public:
  explicit Dinic(int node_count) : graph_(node_count), level_(node_count),
                                  next_(node_count) {}

  void addArc(int tail, int head, Objective capacity) {
    const int forward = static_cast<int>(graph_[tail].size());
    const int reverse = static_cast<int>(graph_[head].size());
    graph_[tail].push_back({head, reverse, capacity});
    graph_[head].push_back({tail, forward, 0});
  }

  Objective maxflow(int source, int sink) {
    Objective result = 0;
    while (bfs(source, sink)) {
      std::fill(next_.begin(), next_.end(), 0);
      while (const Objective pushed =
                 dfs(source, sink, std::numeric_limits<std::int64_t>::max())) {
        result += pushed;
      }
    }
    return result;
  }

private:
  struct Edge {
    int head;
    int reverse;
    Objective residual;
  };

  bool bfs(int source, int sink) {
    std::fill(level_.begin(), level_.end(), -1);
    std::queue<int> queue;
    level_[source] = 0;
    queue.push(source);
    while (!queue.empty()) {
      const int tail = queue.front();
      queue.pop();
      for (const auto &edge : graph_[tail]) {
        if (edge.residual > 0 && level_[edge.head] == -1) {
          level_[edge.head] = level_[tail] + 1;
          queue.push(edge.head);
        }
      }
    }
    return level_[sink] != -1;
  }

  Objective dfs(int tail, int sink, Objective available) {
    if (tail == sink) {
      return available;
    }
    for (int &index = next_[tail]; index < static_cast<int>(graph_[tail].size());
         ++index) {
      auto &edge = graph_[tail][index];
      if (edge.residual <= 0 || level_[edge.head] != level_[tail] + 1) {
        continue;
      }
      const Objective pushed =
          dfs(edge.head, sink, std::min(available, edge.residual));
      if (pushed > 0) {
        edge.residual -= pushed;
        graph_[edge.head][edge.reverse].residual += pushed;
        return pushed;
      }
    }
    return 0;
  }

  std::vector<std::vector<Edge>> graph_;
  std::vector<int> level_;
  std::vector<int> next_;
};

Objective exactMaxflow(int node_count, int source, int sink,
                       const std::vector<DirectedArc> &arcs) {
  Dinic dinic(node_count);
  for (const auto &edge : arcs) {
    if (edge.tail != edge.head && edge.capacity > 0) {
      dinic.addArc(edge.tail, edge.head,
                   mcpd3::widen_capacity(edge.capacity));
    }
  }
  return dinic.maxflow(source, sink);
}

Objective cutCapacity(const std::vector<DirectedArc> &arcs,
                      const std::vector<std::uint8_t> &source_side) {
  Objective result = 0;
  for (const auto &edge : arcs) {
    if (source_side[edge.tail] && !source_side[edge.head]) {
      result += mcpd3::widen_capacity(edge.capacity);
    }
  }
  return result;
}

PartitionedHiPrResult solve(int node_count, int source, int sink,
                            const std::vector<DirectedArc> &arcs,
                            std::vector<int> partitions,
                            PartitionedHiPrOptions options = {}) {
  options.validate_invariants = true;
  return mcpd4::experimental::partitionedHiPr(
      node_count, source, sink, arcs, partitions, options);
}

void verifyCertified(const PartitionedHiPrResult &result,
                     const std::vector<DirectedArc> &arcs,
                     Objective expected, int source, int sink,
                     const std::string &context) {
  require(result.converged, context + ": solver did not converge");
  require(result.maximum_preflow == expected,
          context + ": maximum preflow value mismatch");
  require(result.cut_capacity == expected,
          context + ": reported cut value mismatch");
  require(cutCapacity(arcs, result.source_side) == expected,
          context + ": returned partition is not a minimum cut");
  require(result.source_side[source] != 0,
          context + ": source is not on source side");
  require(result.source_side[sink] == 0,
          context + ": sink is not on sink side");
  for (const auto &value : result.excess) {
    require(value >= 0, context + ": preflow contains negative excess");
  }
}

void deterministicCases() {
  {
    const std::vector<DirectedArc> arcs = {arc(0, 1, 7)};
    const auto result = solve(2, 0, 1, arcs, {-1, -1});
    verifyCertified(result, arcs, 7, 0, 1, "single edge");
  }
  {
    const std::vector<DirectedArc> arcs = {
        arc(0, 1, 5), arc(1, 2, 3), arc(2, 3, 4)};
    const auto result = solve(4, 0, 3, arcs, {-1, 0, 1, -1});
    verifyCertified(result, arcs, 3, 0, 3, "cross-partition path");
    require(result.stats.boundary_pushes > 0,
            "cross-partition path did not exercise coordinator pushes");
    require(result.stats.coordination_rounds >= 1,
            "cross-partition path did not coordinate");
  }
  {
    const std::vector<DirectedArc> arcs = {
        arc(0, 1, 7), arc(1, 3, 2), arc(1, 2, 5)};
    const auto result = solve(4, 0, 3, arcs, {-1, 0, 0, -1});
    verifyCertified(result, arcs, 2, 0, 3, "maximum preflow");
    require(result.excess[1] + result.excess[2] == 5,
            "maximum-preflow test unexpectedly returned stranded excess");
  }
  {
    const std::vector<DirectedArc> arcs = {
        arc(0, 1, 4), arc(0, 1, 6), arc(1, 0, 3), arc(1, 2, 8),
        arc(0, 2, 1), arc(1, 1, 99), arc(0, 1, 0)};
    const auto result = solve(3, 0, 2, arcs, {-1, 0, -1});
    verifyCertified(result, arcs, 9, 0, 2,
                    "parallel antiparallel zero and self arcs");
  }
  {
    const std::vector<DirectedArc> arcs = {
        arc(0, 1, 4), arc(2, 3, 9)};
    const auto result = solve(4, 0, 3, arcs, {-1, 0, 1, -1});
    verifyCertified(result, arcs, 0, 0, 3, "disconnected terminals");
  }
  {
    const std::vector<DirectedArc> arcs = {
        arc(0, 1, 3), arc(1, 4, 3), arc(0, 2, 4), arc(2, 4, 4),
        arc(0, 3, 5), arc(3, 4, 5)};
    const auto result = solve(5, 0, 4, arcs, {-1, 0, 1, 2, -1});
    verifyCertified(result, arcs, 12, 0, 4, "disjoint paths");
  }
  {
    // A coordinator push reaches node 2, local discharge moves it to node 3,
    // and only the next coordinator round can cross from node 3 to node 4.
    const std::vector<DirectedArc> arcs = {
        arc(0, 1, 6), arc(1, 2, 6), arc(2, 3, 6),
        arc(3, 4, 6), arc(4, 5, 6)};
    const std::vector<int> partitions = {-1, 0, 1, 1, 2, -1};
    const auto result = solve(6, 0, 5, arcs, partitions);
    verifyCertified(result, arcs, 6, 0, 5,
                    "alternating local and boundary phases");
    require(result.stats.coordination_rounds == 2,
            "alternating path should require exactly two rounds");

    PartitionedHiPrOptions one_round;
    one_round.max_coordination_rounds = 1;
    const auto truncated = solve(6, 0, 5, arcs, partitions, one_round);
    require(!truncated.converged,
            "one-round alternating path incorrectly reported convergence");
    require(truncated.maximum_preflow < 6,
            "one-round alternating path unexpectedly reached the sink");
  }
  {
    const std::vector<DirectedArc> arcs = {
        arc(2, 0, 100), arc(0, 1, 7), arc(1, 2, 7), arc(2, 1, 100)};
    const auto result = solve(3, 0, 2, arcs, {-1, 7, -1});
    verifyCertified(result, arcs, 7, 0, 2,
                    "incoming source and outgoing sink arcs");
    require(result.stats.boundary_pushes == 0,
            "terminal arcs were incorrectly classified as boundary arcs");
  }
  {
    const std::vector<DirectedArc> arcs = {
        arc(0, 1, 20), arc(1, 2, 7), arc(1, 3, 13), arc(3, 4, 13),
        arc(2, 4, 7)};
    PartitionedHiPrOptions frequent_updates;
    frequent_updates.global_relabel_work_factor = 0.000001;
    const auto result =
        solve(5, 0, 4, arcs, {-1, 0, 0, 0, -1}, frequent_updates);
    verifyCertified(result, arcs, 20, 0, 4,
                    "work-triggered global relabel");
    require(result.stats.work_global_relabel_interruptions > 0,
            "small work limit did not interrupt local discharge");
  }
  {
    const long maximum = std::numeric_limits<std::int32_t>::max();
    const std::vector<DirectedArc> arcs = {
        arc(0, 1, maximum), arc(0, 2, maximum),
        arc(1, 3, maximum), arc(2, 3, maximum)};
    const Objective expected =
        2 * mcpd3::widen_capacity(mcpd3::capacity_from_integer(maximum));
    const auto result = solve(4, 0, 3, arcs, {-1, 0, 1, -1});
    verifyCertified(result, arcs, expected, 0, 3,
                    "widened total preflow capacity");
  }
}

void randomizedDifferentialCases() {
  std::mt19937 generator(0x51A7C0DEu);
  for (int trial = 0; trial < 5000; ++trial) {
    const int node_count = 2 + static_cast<int>(generator() % 9);
    const int source = 0;
    const int sink = node_count - 1;
    const int partition_count = 1 + static_cast<int>(generator() % 4);
    std::vector<int> partitions(node_count, -1);
    for (int node = 1; node < sink; ++node) {
      partitions[node] = static_cast<int>(generator() % partition_count);
    }

    std::vector<DirectedArc> arcs;
    const int arc_count = static_cast<int>(generator() % (node_count * 4 + 1));
    for (int index = 0; index < arc_count; ++index) {
      const int tail = static_cast<int>(generator() % node_count);
      const int head = static_cast<int>(generator() % node_count);
      const long capacity = static_cast<long>(generator() % 12);
      arcs.push_back(arc(tail, head, capacity));
    }

    const Objective expected = exactMaxflow(node_count, source, sink, arcs);
    const auto result = solve(node_count, source, sink, arcs, partitions);
    verifyCertified(result, arcs, expected, source, sink,
                    "random trial " + std::to_string(trial));
  }
}

template <typename Operation>
void requireThrows(Operation operation, const std::string &context) {
  try {
    operation();
  } catch (const std::exception &) {
    return;
  }
  throw std::runtime_error(context + ": expected an exception");
}

void validationCases() {
  const std::vector<DirectedArc> path = {arc(0, 1, 1), arc(1, 2, 1)};
  requireThrows([&] { solve(0, 0, 1, path, {}); }, "empty graph");
  requireThrows([&] { solve(3, 0, 0, path, {-1, 0, -1}); },
                "same terminal");
  requireThrows([&] { solve(3, 0, 2, path, {0, 0}); },
                "partition vector size");
  requireThrows(
      [&] { solve(3, 0, 2, {arc(0, 3, 1)}, {-1, 0, -1}); },
      "arc endpoint");
  requireThrows(
      [&] { solve(3, 0, 2, {arc(0, 1, -1)}, {-1, 0, -1}); },
      "negative capacity");
  requireThrows([&] { solve(3, 0, 2, path, {-1, -1, -1}); },
                "negative internal partition");

  PartitionedHiPrOptions options;
  options.max_coordination_rounds = 0;
  requireThrows([&] { solve(3, 0, 2, path, {-1, 0, -1}, options); },
                "zero round limit");

  options.max_coordination_rounds = 10;
  options.global_relabel_work_factor = -1;
  requireThrows([&] { solve(3, 0, 2, path, {-1, 0, -1}, options); },
                "negative global relabel work factor");
}

} // namespace

int main() {
  try {
    deterministicCases();
    randomizedDifferentialCases();
    validationCases();
  } catch (const std::exception &error) {
    std::cerr << "partitioned_hi_pr_test failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
