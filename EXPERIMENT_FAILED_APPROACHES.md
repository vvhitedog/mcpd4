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
- Do not assume that adding more separated partitionings monotonically helps.
  On grid 128x128 P4, M2 solved in 17.778 ms while M4 took 20.087 ms because the
  extra local phases repeated node activation and arc scans.
- Do not assume that eliminating intermediate global BFS is sufficient for a
  speedup. M2 with only initial/final BFS increased local arc scans by 1.35x to
  1.87x on the Ghiglia-Pritt cuts and was slower on all three. A work-triggered
  compromise helped Spiral and Head but not IFSAR, so no sampled relabel factor
  is a safe universal default.
- Do not run the partition-cover solver with an incomplete family and interpret
  stagnation as a cut. If a node remains boundary or an arc remains crossing in
  every partitioning, the local schedule is not fair. The implementation now
  rejects that input before preflow initialization.
