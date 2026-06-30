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

## 2026-06-29 14:50 PDT

- Added an experimental seedable randomized initial-alpha option to
  `PartitionWorkerCoordinator`.
- Added `PartitionWorkerRegularizationScheme::NONE` so tests can isolate
  unregularized coordinator behavior from both local lexicographic
  regularization and symmetric alpha-shift updates.
- Added tests on the same fixed step-`10` one-node cycle variants:
  source terminal `-10`, target terminals `{2, 5, 8}`.
  - With no regularization and no randomized alpha, the fixed-scale schedule
    remains disagreeing.
  - With a seed that misses the useful multiplier region, randomized
    initialization also remains disagreeing.
  - With a seed that starts in the useful multiplier region, randomized
    initialization reaches unregularized agreement in the first round with
    zero local regularization budget.
- Conclusion: randomized initialization is useful diagnostic evidence for the
  alpha-offset interpretation, but it is weaker than an adaptive regularizer
  because it is a one-shot start perturbation.
- Committed the mcpd3 unit:
  `fb311f4 Add randomized alpha initialization option`.
- Verified:
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `cmake --build third_party/mcpd3/build --target dimacs_dual_decomp_example -j`.

## 2026-06-29 17:15 PDT

- Added benchmark-facing regularization controls to the legacy
  `DualDecomposition` path and `dimacs_dual_decomp_example`:
  - `--regularization local-lexicographic|symmetric-alpha-shift|none`;
  - `--disable-regularization`;
  - `--symmetric-alpha-shift`;
  - `--random-initial-alpha-radius`;
  - `--random-initial-alpha-seed`.
- Added tests that verify:
  - low-scale regularization strength is controlled by the selected scheme;
  - randomized initial alphas are exported through partition packages.
- Committed the mcpd3 unit:
  `599d206 Add dual decomposition benchmark regularization controls`.
- Added a streaming directed DIMACS reader for large directed benchmark files
  and exposed it through `dimacs_dual_decomp_example --stream-directed-input`.
  The reader preserves each directed nonterminal arc with zero reverse
  capacity instead of merging reverse arcs through the general reader's
  unordered-map path.
- Added a tiny DIMACS test comparing the streaming directed reader against the
  general reader on maxflow value and terminal capacities.
- Committed the mcpd3 unit:
  `5147815 Add directed streaming DIMACS reader`.
- Downloaded and extracted Waterloo `BL06-gargoyle-med` locally under ignored
  `data/maxflow/`.
- Ran directed GARG-med benchmarks with 10 basic partitions, 4 threads,
  progress enabled, and logs under ignored `benchmark_results/gargoyle-med/`:
  - no regularization:
    `best_lower_bound_unscaled=68173681`,
    `best_upper_bound_unscaled=68173681`, `best_gap=0`,
    `final_disagreement_count=0`, `iteration_count=416`,
    wall time `2:09.44`;
  - local lexicographic regularization:
    same final value and disagreement as no-reg, with
    `final_regularization_budget=0`, because the run closes at step size
    `100` before low-scale regularization activates;
  - random initial alpha with radius `9999`, seed `1`, and no regularization:
    `best_gap=1024`, `final_disagreement_count=10`,
    `iteration_count=549`, wall time `2:41.37`;
  - symmetric alpha shift `1`:
    `best_gap=1024`, `final_disagreement_count=7`,
    `iteration_count=515`, wall time `2:30.44`.
- The DIMACS `.sol` value is `97979938`; the reader reports terminal
  imbalance `29806257`, and `68173681 + 29806257 = 97979938`.
  Under this setup the no-regularization baseline therefore reaches an exact
  primal agreement and matches the provided solution after the reader's
  imbalance offset.
- Conclusion: `BL06-gargoyle-med` with this directed reader, basic 10-way
  partitioning, and default fixed-step schedule is not a reproduced
  regularization-required case. Randomized initial alpha and symmetric alpha
  shift were worse than no-reg in the checked runs.

## 2026-06-29 18:14 PDT

- Removed the benchmark binary's unconditional DIMACS capacity premultiply:
  `dimacs_dual_decomp_example` now defaults to `capacity_multiplier=1`.
  Historical behavior can still be requested explicitly with
  `--capacity-multiplier 10000`.
- Decoupled objective/reporting scale from the DD step-size schedule:
  - `DualDecompositionOptions::objective_scale`;
  - `PartitionWorkerCoordinatorOptions::objective_scale`;
  - positive-scale validation in both paths;
  - `getScale()` now reports the objective scale instead of
    `initial_step_size`.
- Added tests proving that objective reporting scale is independent of DD
  step size and that explicit objective scales control reported lower bounds
  in both `DualDecomposition` and `PartitionWorkerCoordinator`.
- Committed the mcpd3 unit:
  `c398487 Decouple objective scale from DD step size`.
- Downloaded and extracted Waterloo `babyface.n6c10` and `adhead.n6c10`
  locally under ignored `data/maxflow/`.
- Ran directed `babyface.n6c10` checks with 10 partitions:
  - unscaled, no regularization, `--max-step 10`:
    `best_lower_bound_raw=1373`, `.sol=19448`,
    `best_gap=1.785e+06`, `final_disagreement_count=325411`;
  - unscaled, no regularization, `--max-step 1`:
    `best_lower_bound_raw=1373`, `.sol=19448`,
    `best_gap=1.7218e+06`, `final_disagreement_count=339274`;
  - compatibility scaled no-reg from the earlier run:
    `best_lower_bound_raw=194479585`,
    `best_lower_bound_unscaled=19447`, `best_gap=1049.04`,
    `final_disagreement_count=510`;
  - compatibility scaled symmetric alpha shift:
    `best_lower_bound_raw=194479315`,
    `best_lower_bound_unscaled=19447`, `best_gap=1589.07`,
    `final_disagreement_count=615`;
  - compatibility scaled local-search partitioner with no regularization:
    `best_lower_bound_raw=194475049`,
    `best_lower_bound_unscaled=19447`, `best_gap=2388.5`,
    `final_disagreement_count=814`.
- A compatibility scaled local-lexicographic run was stopped after it became a
  slow screening path: at step size `10`, local solves were taking about
  19-20 seconds per iteration and after 26 low-scale iterations it still had
  `best_lower_bound=19447.442`, `gap=3228.558`, and
  `num_disagreeing=569`.
- Conclusion: `babyface.n6c10` did not provide the clean target case under
  checked settings. Without capacity premultiplication it stalls far below the
  known optimum; with compatibility scaling it approaches the optimum but
  neither symmetric alpha shift nor local partitioning improved over the
  basic no-reg baseline.
- Did not run `adhead.n6c10` DD solve in this environment: babyface used about
  `5.3GB` RSS, adhead has roughly `2.5x` the nodes/arcs, and the machine had
  about `7.4GB` available with swap full. A full adhead solve is likely to
  OOM unless memory is reduced, for example by disabling upper-bound tracking
  or running on a larger machine.

## 2026-06-29 19:31 PDT

- Checked the original `early_experiments` branch at commit
  `e5d48b2 Introduce regularization on constrainied nodes` in a detached
  worktree. To build it on this machine, added temporary uncommitted compile
  shims for the removed Boost hash dependency and missing `io/workdir.h`.
- Ran the original `dimacs_dual_decomp_example` on `babyface.n6c10` with
  10 partitions. The original branch uses the general `read_dimacs()` path,
  premultiplies capacities by `10000`, and has the old additive low-scale
  regularizer always enabled.
- Original branch result:
  - reached `lower_bound=19448.000000`;
  - reached `num_disagreeing=0`;
  - stopped on no disagreement;
  - max raw lower bound `194480000`;
  - wall time `2:31.91`;
  - max RSS `3283160 kbytes`.
- Re-ran the current branch with the same general-reader input path,
  `--capacity-multiplier 10000`, and no regularization:
  - `best_lower_bound_raw=194479585`;
  - `best_lower_bound_unscaled=19447`;
  - `best_gap=1049.04`;
  - `final_disagreement_count=510`;
  - wall time `2:07.59`.
- Re-ran the current branch with the same general-reader input path and exact
  local lexicographic regularization. This did not reproduce the original
  behavior within the same runtime window: at step size `10`, iterations were
  taking about `13-14s` each and still had hundreds of disagreements. The run
  was stopped after iteration `10` of the step-`10` scale with
  `best_lower_bound=19446.812`, `gap=3381.188`, and
  `num_disagreeing=625`.
- Conclusion: the original branch does converge on `babyface.n6c10`. The
  current exact lexicographic regularization path is a practical regression
  for this case relative to the old additive regularizer. The earlier
  `babyface` note was incomplete because it did not compare against
  `early_experiments`.

## 2026-06-29 19:57 PDT

- Added `REGULARIZATION_EXACTNESS_PROOF.md` with the lattice proof for the
  original scaled low-strength regularization scheme.
- Formalized the exactness condition as:
  `agreement + global_regularization_budget < objective_scale => optimal`.
- Clarified that "below the scale" means the total regularization range over
  all local subproblem copies in the summed DD solve, not the final paid
  contribution and not a per-partition budget.
- Reviewed the OG `early_experiments` implementation at `e5d48b2` against the
  proof obligations. It has the intended mathematical shape: capacities are
  scaled by `10000`, low-scale regularization is nonnegative and one-sided,
  reported lower bounds exclude regularization, and alpha terms cancel under
  agreement.
- Found that the OG implementation does not fully enforce the proof condition:
  the budget check is per local solver, warning-only, hardcoded to `10000`,
  uses `>` instead of rejecting `>=`, and does not export a summed global
  effective perturbation budget.
- Also found that incremental local terminal updates can leave stale
  regularization in the maxflow graph when alpha is unchanged or when the
  regularization strength changes between step sizes. A hardened version must
  track the actual effective perturbation used in the solve or force a full
  terminal recomputation when regularization state changes.

## 2026-06-29 20:45 PDT

- Replaced the productized local regularization experiments with a hardened
  OG-style scaled-epsilon scheme:
  - low-scale only: regularization strength is `10` at step `10`, `1` at
    step `1`, and `0` above step `10`;
  - anchors are refreshed from previous sink labels only when the local DD
    alpha term changes;
  - active epsilon terms persist across unchanged-alpha solves, matching the
    useful OG incremental behavior;
  - diagnostics count the full active regularization budget and contribution;
  - `regularization_budget_limit` is configurable, defaulting to
    `objective_scale`;
  - if the active budget is not strictly below the limit, the solver prints a
    warning that the result may not certify optimality, but it does not stop
    the run yet.
- Removed the symmetric alpha-shift scheme from the productized coordinator
  and legacy benchmark path. Randomized initial alphas remain available as a
  diagnostic/preconditioning option.
- Added/updated tests for:
  - scaled-epsilon activation only when a previous sink label has a changed
    alpha term;
  - persistence of active epsilon terms until a later alpha change clears
    them;
  - low-scale tie handling;
  - the synthetic opposite-direction cycle case;
  - over-budget warning diagnostics where regularized agreement still stops
    for now.
- Committed and pushed the mcpd3 unit on branch `partition-worker-api`:
  `7e5caea Harden scaled epsilon regularization`.
- Benchmarked current productized scaled-epsilon on `adhead.n6c10` with
  10 basic partitions, 4 threads, and `--capacity-multiplier 10000`:
  - known `.sol` value: `48373`;
  - `best_lower_bound_raw=483730000`;
  - `final_disagreement_count=0`;
  - `final_regularization_budget_raw=40`, below the default strict limit
    `10000`;
  - wall time `1:24.18`, max RSS `6007596 KB`.
- Compared against the OG `origin/early_experiments` branch in a separate
  worktree. The old example needed a local benchmark-only shim to remove an
  unused CSR/primal-decoding Boost dependency; the DD solver and OG
  regularizer were left unchanged. OG on the same `adhead.n6c10` setup
  reached:
  - `=== MAX === lower_bound : 483730000`;
  - final printed `num_disagreeing : 0`;
  - wall time `1:26.19`, max RSS `5848172 KB`.
- Sanity-checked `babyface.n6c10` with the hardened scaled-epsilon path. It
  improved the prior productized run but did not reach agreement under the
  default patience window:
  - `best_lower_bound_raw=194479904` versus `.sol=19448`;
  - `final_disagreement_count=187`;
  - `final_regularization_budget_raw=1048`, below the default strict limit
    `10000`.

## 2026-06-29 21:43 PDT

- Tested whether `adhead.n6c10` can use a smaller compatibility multiplier:
  `--capacity-multiplier 100`.
- Result: not enough for the current scaled-epsilon method.
  - The run printed the expected warning:
    `regularization budget 9840 is not below limit 100`.
  - Final agreement was reached, but it was non-certifying:
    `final_disagreement_count=0`,
    `final_regularization_budget_raw=11036`, and
    `final_regularization_contribution_raw=10205`.
  - The reported bound exceeded the known scaled optimum:
    `best_lower_bound_raw=4855590` versus expected `4837300`.
  - It was slower than the `10000` multiplier run:
    wall time `3:31.91` versus `1:24.18`.

## 2026-06-29 22:43 PDT

- Implemented dynamic objective-scale promotion for the legacy
  `DualDecomposition` path when scaled-epsilon regularization exceeds the
  strict active-budget condition.
- New behavior:
  - detects `regularization_budget >= objective_scale` before accepting a
    regularized lower bound;
  - scales the current objective by `10x`;
  - preserves local residual graphs, DD alphas, local capacities, and tracked
    bounds by scaling them in place;
  - restarts the capacity-scaling schedule from the promoted objective scale;
  - limits promotion count through `max_objective_scale_promotions`;
  - exposes benchmark flags `--disable-scale-promotion` and
    `--max-scale-promotions`.
- Added a regression test where a tiny scaled-epsilon solve exceeds the
  initial budget, promotes from scale `10` to `100`, reaches agreement, and
  proves that the over-budget regularized lower bound was not accepted.
- Committed and pushed the mcpd3 unit on branch `partition-worker-api`:
  `8290cd8 Promote objective scale on reg overbudget`.
- Re-ran `adhead.n6c10` with the previously non-certifying
  `--capacity-multiplier 100` setup and default scale promotion:
  - first low-scale regularized iteration exceeded the budget:
    `budget=9840`, `limit=100`;
  - promoted once from objective scale `100` to `1000`;
  - reached `best_lower_bound_raw=48373000`,
    `best_lower_bound_unscaled=48373`;
  - reached `final_disagreement_count=0`;
  - final active budget was certifying:
    `final_regularization_budget_raw=180 < 1000`;
  - wall time `3:26.77`, max RSS `6049328 KB`.
- Verified:
  - `cmake --build third_party/mcpd3/build --target partition_worker_test -j`;
  - `cmake --build third_party/mcpd3/build --target dimacs_dual_decomp_example -j`;
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `ctest --test-dir build --output-on-failure`.

## 2026-06-29 22:58 PDT

- Corrected the scope of objective-scale promotion: the previous commit
  covered the current monolithic `DualDecomposition` benchmark path, but the
  productized `PartitionWorkerCoordinator` path also needs the same behavior.
- Added a worker rescale API:
  `PartitionWorker::scaleObjective(long factor)`.
- Implemented `InProcessPartitionWorker::scaleObjective()` by scaling the
  already-loaded `PrimalDualMinCutSolver`, package capacities, and local
  alpha/last-alpha state in place.
- Updated `PartitionWorkerCoordinator` so over-budget scaled-epsilon rounds:
  - compute disagreement diagnostics;
  - do not update coordinator alpha state;
  - do not record the lower bound as accepted progress;
  - return an explicit `REGULARIZATION_BUDGET_EXCEEDED` status/stop reason;
  - promote the objective scale by `10x` when promotion is enabled;
  - scale coordinator alphas, accepted aggregate bounds, packages, and live
    workers;
  - restart the schedule from the promoted objective scale.
- Added coordinator tests for:
  - promotion success with scripted workers, including worker rescale calls
    and rejection of the over-budget lower bound;
  - disabled-promotion behavior, where over-budget is reported and no lower
    bound is accepted;
  - promotion through real `InProcessPartitionWorker` instances, exercising
    live solver/residual-graph scaling.
- Committed and pushed the mcpd3 unit on branch `partition-worker-api`:
  `0d699c8 Promote coordinator scale on reg overbudget`.
- Verified:
  - `cmake --build third_party/mcpd3/build --target partition_worker_test -j`;
  - `cmake --build third_party/mcpd3/build --target dimacs_dual_decomp_example -j`;
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `ctest --test-dir build --output-on-failure`.

## 2026-06-29 23:12 PDT

- Rechecked the opposite-direction cycle with a low objective scale
  (`M=10`).
- Confirmed the earlier failing temporary variant was under-provisioned: after
  promotion from `M=10` to `M=100`, the schedule also needs enough scale
  levels and unit-scale iterations to finish.
- Preserved the original high-`M` cycle test and added a separate regression
  where:
  - `initial_step_size=10`;
  - `objective_scale=10`;
  - `num_optimization_scales=3`;
  - `max_iteration_count=100`;
  - promotion is enabled;
  - the cycle promotes once to `M=100`, reaches agreement, and finishes with
    active budget below the promoted scale.
- Committed and pushed the mcpd3 test unit on branch `partition-worker-api`:
  `cae055f Test low scale cycle promotion`.
- Verified:
  - `cmake --build third_party/mcpd3/build --target partition_worker_test -j`;
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `ctest --test-dir build --output-on-failure`.
