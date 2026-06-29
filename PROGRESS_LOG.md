# Progress Log

## 2026-06-29 00:24:08 PDT

- Created product branch `network-free-worker-api` from `main`.
- Created `third_party/mcpd3` branch `partition-worker-api` from
  `distributed-mvp-start`.
- Confirmed the next implementation checkpoint is network-free: introduce and
  test an in-process partition-worker API before any TCP, MPI, or serialization
  work.
- Project management constraints for this branch:
  - use test-driven development;
  - keep work in logical commits in both this product repo and the upstreamable
    `mcpd3` submodule branch;
  - maintain this progress log and the failed approaches log.

## 2026-06-29 00:29:46 PDT

- Added a CTest suite entry in `third_party/mcpd3` for an
  `InProcessPartitionWorker` API.
- Implemented `decomp/partition_worker.h` with:
  - `PartitionPackage`;
  - `ConstraintEndpointBinding`;
  - `PartitionSolveRequest`;
  - `PartitionSolveResult`;
  - `PartitionWorker`;
  - `InProcessPartitionWorker`.
- The submodule test compares the worker path with direct
  `PrimalDualMinCutSolver` behavior across two rounds, including an alpha
  update.
- Committed the upstreamable mcpd3 unit on branch `partition-worker-api`:
  `02c28fc Add in-process partition worker API`.
- Added a top-level product CTest smoke test that compiles against
  `third_party/mcpd3` and invokes the new worker API through the submodule.
- Verified:
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `ctest --test-dir build --output-on-failure`.

## 2026-06-29 00:36:04 PDT

- Added a tiny-graph equivalence test in `third_party/mcpd3`:
  exported `DualDecomposition` partition packages are solved through
  `InProcessPartitionWorker` and compared against the existing
  `DualDecomposition` one-iteration lower bound and disagreement count.
- Exposed `DualDecomposition::getPartitionPackages()` and populated
  `PartitionPackage` graph data before local solver construction.
- Added stable per-constraint endpoint IDs while constructing existing
  `DualDecompositionConstraintArc` records.
- Committed the mcpd3 equivalence/export unit on branch `partition-worker-api`:
  `aebb31a Export dual decomposition partition packages`.
- Fixed the documented Stage 0 example build by removing an unnecessary Boost
  hash dependency from `graph/csrgraph.h`, adding the missing `io/workdir.h`,
  and removing the stale CMake Boost lookup.
- Committed the mcpd3 build-fix unit:
  `d7dfbe1 Make CSR graph build without Boost hash`.
- Verified:
  - `cmake --build third_party/mcpd3/build --target dimacs_dual_decomp_example -j`;
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `ctest --test-dir build --output-on-failure`.

## 2026-06-29 00:39:30 PDT

- Started Stage 2 without adding networking.
- Added `PartitionWorkerCoordinator`, a network-free controller that:
  - owns coordinator-side constraint alpha/momentum state;
  - sends alpha records to partition workers each round;
  - gathers lower-bound and regularization terms;
  - gathers constrained labels;
  - computes disagreement diagnostics;
  - applies the same fixed-step alpha update math as `DualDecomposition`.
- Added a two-round tiny-graph test comparing `PartitionWorkerCoordinator`
  against the existing `DualDecomposition` loop for best lower bound,
  disagreement count, and disagreement norm.
- Committed the mcpd3 coordinator unit:
  `e65eaa4 Add partition worker coordinator round loop`.
- Verified:
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `cmake --build third_party/mcpd3/build --target dimacs_dual_decomp_example -j`;
  - `ctest --test-dir build --output-on-failure`.

## 2026-06-29 00:50:30 PDT

- Added `MVP_TRACKER.md` to track the overall path from the current
  network-free checkpoint to a localhost distributed MVP.
- Linked the tracker from `README.md`.

## 2026-06-29 00:57:34 PDT

- Implemented the first unchecked Stage 2 tracker item: extended
  `PartitionWorkerCoordinator` from a single-round primitive to a full
  network-free solve loop.
- Added coordinator result/status types for:
  - overall optimization status;
  - stop reason;
  - per-iteration progress records;
  - per-scale results;
  - final lower-bound, disagreement, and regularization diagnostics.
- Added scripted-worker tests covering solve-loop branches:
  - unregularized agreement as exact optimality;
  - regularized agreement as non-exact no-further-progress;
  - iteration cap;
  - modern patience/no-improvement stopping;
  - legacy patience stopping;
  - group stopping;
  - continuation across scales.
- Committed the mcpd3 unit:
  `9e2d530 Add full partition coordinator solve loop`.
- Marked the corresponding `MVP_TRACKER.md` checklist item complete.
- Verified:
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `cmake --build third_party/mcpd3/build --target dimacs_dual_decomp_example -j`;
  - `ctest --test-dir build --output-on-failure`.

## 2026-06-29 02:47 PDT

- Reworked low-scale regularization into an exact lexicographic local solve:
  each regularized local subproblem now solves `M * F(x) + R(x)` with
  `M = regularization_budget + 1`, then reports the unregularized local
  optimum `F(x)` as the lower-bound term.
- Preserved strict local optima under regularization and limited the
  one-sided term to tie-breaking among existing local optima.
- Updated coordinator and legacy `DualDecomposition` stop semantics so
  lexicographic regularized agreement reports `OPTIMAL` instead of
  `NO_FURTHER_PROGRESS`.
- Added tests for:
  - no-anchor regularization behavior;
  - strict-optimum preservation;
  - local tie-breaking with unregularized lower-bound reporting;
  - low-scale schedule activation at step sizes `10` and `1`;
  - coordinator optimal stop on lexicographic regularized agreement.
- Committed the mcpd3 unit:
  `9eda49c Add exact lexicographic regularization`.
- Verified:
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `cmake --build third_party/mcpd3/build --target dimacs_dual_decomp_example -j`;
  - `ctest --test-dir build --output-on-failure`.

## 2026-06-29 02:58 PDT

- Tried to find cases where the committed one-sided lexicographic
  regularization is required for convergence.
- Search results found no strict-need cases:
  - 500k random one-boundary, two-node partition-pair traces;
  - 1M random two-boundary coupled partition-pair traces;
  - 1M random local source-endpoint subproblems checking whether
    lexicographic regularization changes labels;
  - 1M random mixed source/target local subproblems checking whether
    lexicographic regularization changes labels.
- Found a hand-derived low-scale cycle:
  source terminal `-10`, target terminal `+8`, step size `10`.
  Scale `10` cycles, but scale `1` resolves it.
- Corrected follow-up: this case validates the scaling schedule, not
  regularization necessity. A forced-unregularized `10 -> 1` schedule also
  reaches agreement.
- The previous additive regularization scheme at commit `9e2d530` was tested
  in a temporary worktree on this same case. With the coordinator's immediate
  low-scale regularization, it did not reach agreement for checked
  full-schedule iteration budgets `12`, `20`, or `30`.
- Delayed old-additive variants confirm the nuance: if scale `1`
  regularization is delayed until after the unregularized scale-`1` path has
  already agreed, the case succeeds, but regularization was not needed for
  that success.
- Conclusion: the current `M * F(x) + R(x)` machinery avoids the old additive
  scheme's failure on this case, but this case is still not a strict
  regularization-required example.

## 2026-06-29 08:57 PDT

- Added a second coordinator regularization scheme:
  `SYMMETRIC_ALPHA_SHIFT`.
- The scheme leaves local solver regularization disabled and instead applies a
  symmetric DD alpha pullback to each nonzero alpha update. With
  `symmetric_alpha_shift = 1`, a step-`10` disagreement update of `+/-10`
  becomes `+/-9`; step-`1` updates are unchanged because no smaller positive
  integer shift exists.
- Added tests on three one-node cycle variants:
  source terminal `-10`, target terminals `{2, 5, 8}`.
  At fixed step `10`, the default local lexicographic scheme remains
  disagreeing, while symmetric alpha shift reaches agreement in the second
  round with zero local regularization budget.
- This is the first committed test evidence that a symmetric DD-style
  regularization can resolve fixed-scale cycling while avoiding the local
  `M * F(x) + R(x)` solve path.
