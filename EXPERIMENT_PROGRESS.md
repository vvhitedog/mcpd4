# Partitioned hi_pr Experiment Progress

## 2026-07-17

- Created isolated worktree `exp/partitioned-hi-pr` from `b2226af`.
- Reviewed the upstream highest-label push-relabel implementation and its
  commercial-use licensing restriction.
- Fixed the experiment contract around maximum preflow rather than a
  flow-conversion stage: stranded nonterminal excess is permitted.
- Added the initial failing test suite for deterministic, randomized,
  partition-boundary, and input-validation behavior.

### 2026-07-17 00:42:12 PDT

- Implemented a contiguous residual graph with current-arc scans,
  highest-label local discharge, global gap retirement, and widened excess.
- Implemented coordinator reverse-BFS global relabeling and admissible pushes
  over boundary arcs only.
- Boundary vertices can discharge through local arcs but never relabel during a
  local phase. A later coordinator BFS updates their labels.
- The solver terminates at a maximum preflow: positive excess at vertices that
  cannot reach the sink is retained, and the weak-source set from the final
  reverse BFS provides the minimum-cut certificate.
- Passed deterministic tests, 600 randomized directed multigraph comparisons
  against an independent Dinic oracle, a forced two-round local/boundary case,
  and a one-round non-certification case.
- Passed ASan/UBSan and 32-, 64-, and 128-bit capacity builds.

### 2026-07-17 Benchmark And Optimization Pass

- Added a release benchmark adapter using the existing BK solver as the exact
  value and timing reference, with contiguous and METIS partitioning.
- Replaced full-vertex gap scans with per-height linked buckets. On Waterloo
  LB07 bunny sml P1, this reduced wall time from 14.895 s to 1.157 s.
- Added optional `hi_pr`-style work-triggered global relabeling. It remained
  exact but was slower on bunny because repeated full-graph BFS dominated, so
  it is retained behind an option and disabled by default.
- Verified exact objectives on a 128x128 grid, a wide-layered graph, and the
  805,800-node Waterloo bunny graph across multiple partition counts and both
  partitioners. Full measurements are in `EXPERIMENT_RESULTS.md`.
- Expanded differential testing to 5,000 randomized directed multigraphs and
  added a total-flow test above 32-bit capacity range.
- Passed the final suite under ASan/UBSan and 32-, 64-, 128-bit, and GMP
  capacity modes.

### 2026-07-17 01:13 PDT - Native DD Comparison

- Compared partitioned PR against the actual native mcpd3 dual-decomposition
  solver on identical 128x128 and 256x256 seed-24 direct grid cuts.
- Used two basic partitions, 32-bit capacities, five repetitions, and the
  current phase-level mcpd3-n schedule defaults. Both solvers returned the same
  exact objectives.
- Partitioned PR took 19.393 ms versus 13.620 ms for mcpd3-n at 128x128, and
  109.345 ms versus 23.152 ms at 256x256, before adding PR's negligible
  0.077/0.254 ms partition setup. The current serial PR prototype is therefore
  1.43x and 4.73x slower at P2.
- Kept the single-cut comparison separate from recorded whole-PU mcpd3-n wall
  times, which include 11 cuts at 128x128 and 21 cuts at 256x256.

### 2026-07-17 01:25 PDT - Book Cut Comparison

- Exported the zero-state first-UP binary cut from the Ghiglia-Pritt Spiral,
  Head, and IFSAR datasets using a tested phase-side DIMACS writer.
- Compared five-run Release medians with two basic partitions. Partitioned PR
  was 2.55x, 3.45x, and 4.10x slower than native mcpd3 DD respectively.
- Every PR, BK, and mcpd3-n run matched exact cut values 2,028, 4,294, and
  20,578. Invariant-enabled PR validation runs also passed on all three cuts.
- The measurements remain single-cut results. Full-PU PR integration and
  timing have not been performed.
