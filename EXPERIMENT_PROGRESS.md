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
