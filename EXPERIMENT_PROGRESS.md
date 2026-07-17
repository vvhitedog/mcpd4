# Partitioned hi_pr Experiment Progress

## 2026-07-17

- Created isolated worktree `exp/partitioned-hi-pr` from `b2226af`.
- Reviewed the upstream highest-label push-relabel implementation and its
  commercial-use licensing restriction.
- Fixed the experiment contract around maximum preflow rather than a
  flow-conversion stage: stranded nonterminal excess is permitted.
- Added the initial failing test suite for deterministic, randomized,
  partition-boundary, and input-validation behavior.
