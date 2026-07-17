# Partitioned hi_pr Failed Approaches

## 2026-07-17

- Do not add a second phase that returns all excess to the source. The desired
  result is a maximum preflow and its minimum-cut certificate; flow conversion
  adds work without changing the cut.
- Do not copy the upstream `hi_pr` source into the product unnoticed. Its
  copyright notice states that commercial use requires a license. This branch
  uses the algorithm and data-layout ideas as an experimental basis and keeps
  the provenance explicit.
- Periodic full-graph relabeling at the original `hi_pr`-style work factors is
  not a good default for the partition protocol. On the 805,800-node Waterloo
  bunny graph, factors 0.5, 1, 2, and 4 all lost to waiting until local work
  blocked. The full residual BFS cost outweighed the saved local relabels. The
  option remains available for graph-specific experiments.
