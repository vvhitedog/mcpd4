# Failed Approaches And Taboos

## 2026-06-29 00:24:08 PDT

- Do not add TCP, MPI, or serialization before the in-process partition-worker
  API has an equivalence test.
- Do not move product deployment/runtime concerns into `third_party/mcpd3`.
- Do not reintroduce the removed Polyak step policy.
- Do not depend on local benchmark files from
  `/home/matt/software/graph-cuts-undirected`.
- Do not claim exact min-cut optimality from heuristic/non-lexicographic
  regularized agreement. Exact claims require local subproblems to report the
  unregularized optimum after a lexicographic `M * F(x) + R(x)` tie-break.

## 2026-06-29 00:29:46 PDT

- Expected TDD red state: `partition_worker_test` initially failed to compile
  because `decomp/partition_worker.h` did not exist. This was resolved by
  mcpd3 commit `02c28fc`.

## 2026-06-29 00:36:04 PDT

- Expected TDD red state: the tiny-graph equivalence test initially failed to
  compile because `DualDecomposition` did not expose partition packages. This
  was resolved by mcpd3 commit `aebb31a`.
- Stage 0 build failure: `dimacs_dual_decomp_example` initially failed on a
  missing `boost/functional/hash.hpp` include. That exposed an unnecessary
  Boost dependency for hashing `std::pair`; resolved by mcpd3 commit
  `d7dfbe1`.
- Stage 0 build failure: after removing the Boost hash dependency, the example
  build failed on missing `io/workdir.h`. The helper existed only in the
  experimental compatibility tree; resolved by adding it to mcpd3 in commit
  `d7dfbe1`.

## 2026-06-29 00:39:30 PDT

- Expected TDD red state: the two-round coordinator test initially failed to
  compile because `decomp/partition_coordinator.h` did not exist. This was
  resolved by mcpd3 commit `e65eaa4`.
- Current coordinator limitation: `PartitionWorkerCoordinator` supports the
  MVP shape of one package per worker object and sends all alpha records each
  round. Multi-package workers and changed-alpha deltas remain future work.

## 2026-06-29 00:57:34 PDT

- Expected TDD red state: full-solve tests initially failed to compile because
  `PartitionWorkerCoordinator` had no `solve()` API, no solve-loop options, no
  optimization status enum, and no stop-reason enum. This was resolved by
  mcpd3 commit `9e2d530`.
- Still out of scope for this step: primal upper-bound decoding is not mapped
  to the worker-coordinator path. That remains the next tracker item.

## 2026-06-29 02:47 PDT

- Discarded the unregularized-confirmation idea for lexicographic
  regularization: a plain unregularized re-solve can select a different
  optimizer among tied local optima, so it is not the right certificate for
  the chosen regularized labels.
- Important caveat found while testing: simple source-side local ties already
  choose source under the current maxflow implementation, so one-sided
  lexicographic regularization may be redundant in those cases. Tests now
  cover the exactness contract rather than claiming every small tie case needs
  the regularizer.
- Do not restore the old additive local regularization path as a certificate:
  it can perturb strict local optima. The hardened path must keep
  `M > max(R)` and report the unregularized lower-bound term.

## 2026-06-29 02:58 PDT

- The committed exact one-sided lexicographic regularizer has not produced a
  case where it is required for convergence. Random searches over simple local
  and two-partition traces found zero strict-need cases.
- Do not test regularization convergence only at scale `10`. A concrete cycle
  with source terminal `-10`, target terminal `+8`, and step size `10` is
  resolved by continuing to scale `1`.
- Do not treat that concrete cycle as a strict regularization-required case:
  forced-unregularized scale `1` also reaches agreement.
- The previous additive regularization scheme at commit `9e2d530` still fails
  this full-schedule case for checked iteration budgets `12`, `20`, and `30`
  when low-scale regularization is active immediately.
- If old additive regularization is delayed until after the unregularized
  scale-`1` path has already agreed, this case succeeds, but that does not
  validate the old regularizer.

## 2026-06-29 08:57 PDT

- Symmetric DD-style regularization should not be lumped in with local unary
  regularization failures. It changes alpha updates symmetrically and cancels
  on agreement.
- Early positive result: `SYMMETRIC_ALPHA_SHIFT` resolves fixed step-`10`
  one-node cycle variants where local lexicographic regularization remains
  disagreeing.
- Still not validated broadly: do not make symmetric alpha-shift the default
  without stress tests beyond the committed tiny synthetic cases.

## 2026-06-29 14:50 PDT

- Randomized initial Lagrange multipliers should not be treated as a stronger
  regularization scheme. Committed tests show both outcomes on the same fixed
  step-`10` cycle family: one seed reaches first-round unregularized
  agreement, while another seed misses and remains disagreeing.
- Use randomized initial alphas as diagnostic/experimental evidence for
  alpha-offset behavior, not as an MVP default or an optimality argument.

## 2026-06-29 17:15 PDT

- Do not cite Waterloo `BL06-gargoyle-med` as a current reproduced
  regularization-required case under the checked setup. With directed
  streaming input, 10 basic partitions, 4 threads, and regularization disabled,
  it reached `best_gap=0` and `final_disagreement_count=0`.
- The GARG-med objective needs the reader's terminal imbalance offset when
  comparing against the `.sol` file. The checked no-reg value was
  `68173681`; adding the reported imbalance `29806257` gives the provided
  solution value `97979938`.
- Randomized initial alpha is not automatically beneficial on GARG-med:
  radius `9999`, seed `1`, with no regularization ended with
  `best_gap=1024` and `final_disagreement_count=10`.
- Symmetric alpha shift is not automatically beneficial on GARG-med:
  shift `1` ended with `best_gap=1024` and
  `final_disagreement_count=7`.
- The local lexicographic run did not test final recovery on GARG-med because
  it closed at step size `100` with zero regularization budget. This is a
  useful regression check, not evidence that lexicographic recovery is needed
  on this instance.
