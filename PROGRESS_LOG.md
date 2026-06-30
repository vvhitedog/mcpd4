# Progress Log

## 2026-06-30 10:31 PDT

- Confirmed branch state before starting:
  - product branch `network-free-worker-api` was at
    `d89cc4e Merge certified lower bound diagnostics`;
  - `third_party/mcpd3` branch `partition-worker-api` was at
    `5805c53 Merge certified lower bound accounting`.
- Implemented static initial partition-to-worker balancing.
- In `third_party/mcpd3`:
  - added `PartitionWorkerResourceEstimate` with `cpu_count` and `ram_gb`;
  - added `PartitionWorker::resourceEstimate()` with a default one-CPU,
    unknown-RAM estimate;
  - changed `PartitionWorkerCoordinator` setup from round-robin assignment to
    deterministic largest-partition-first packing;
  - partition work estimate uses local node count, arc count, and boundary
    endpoint count;
  - worker capacity uses CPU count as the primary scale and RAM as a mild
    tie-break/modifier;
  - assignment remains static after packages are loaded.
- Added submodule tests proving:
  - higher-CPU workers receive the largest package and more total estimated
    work;
  - for equal CPU counts, the higher-RAM worker receives the largest package.
- Committed and pushed mcpd3 submodule branch `partition-worker-api`:
  `6415ec7 Balance initial partition worker assignment`.
- In the product repo:
  - `TcpPartitionWorker::resourceEstimate()` now exposes CPU/RAM from the
    worker `HELLO`;
  - TCP loopback coverage verifies custom handshake resources propagate to the
    coordinator-side worker object.
- Exact `adhead.n6c10` distributed/TCP benchmark with 4 workers and 10
  partitions improved versus the previous batched run:
  - output:
    `benchmark_results/adhead-distributed-exact-w4-balanced-20260630-102800.out`;
  - `best_lower_bound 48372.9`;
  - `best_lower_bound_raw 48372930`;
  - `best_regularized_objective 48373.1`;
  - `best_regularized_objective_raw 48373110`;
  - `objective_scale 1000`;
  - `objective_scale_promotions 1`;
  - `final_disagreement_count 0`;
  - `final_regularization_budget 180`;
  - `capacity_scale_saturation_count 0`;
  - `timing_solve_wall_us 140040416`;
  - `timing_worker_rpc_overhead_us 19525976`;
  - `timing_solve_round_count 1820`;
  - `timing_solve_round_batch_count 728`;
  - wall time `3:22.74`;
  - max RSS `6342740 kB`.
- The lower-bound value now reflects the merged certified-LB accounting:
  certified original lower bound is conservative relative to the regularized
  objective. This was not changed in this balancing unit.
- Verified:
  - `cmake --build third_party/mcpd3/build -j`;
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`;
  - exact distributed/TCP adhead run with 4 workers, 10 partitions, and no
    saturation.

## 2026-06-30 10:09 PDT

- Implemented batched multi-partition worker solves.
- In `third_party/mcpd3`:
  - added `PartitionWorker::solveRoundBatch()` with a default single-request
    fallback;
  - added concurrent distinct-partition batch solving to
    `InProcessPartitionWorker`;
  - changed `PartitionWorkerCoordinator::runRound()` to send one batch per
    worker per round and scatter results by returned partition id;
  - added guards for wrong batch result counts and results for unowned
    partitions.
- Added submodule tests for:
  - coordinator batching when one worker owns multiple partitions;
  - malformed batch responses;
  - in-process batch solving of distinct loaded partitions;
  - duplicate partition rejection in one batch.
- Committed and pushed mcpd3 submodule branch `partition-worker-api`:
  `d19b319 Batch partition worker solve requests`.
- In the product repo:
  - added protocol v2 batch frames:
    `SOLVE_ROUND_BATCH_REQUEST` and `SOLVE_ROUND_BATCH_RESULT`;
  - added TCP `solveRoundBatch()` RPC support;
  - taught worker processes to execute a batch request;
  - added `solve_round_batch_count` timing/progress telemetry.
- Added product tests for:
  - batch protocol serialization round trips;
  - explicit TCP batch solve RPCs;
  - coordinator use of batch RPCs through a remote worker;
  - process-level progress/final telemetry for batch counts.
- Exact `adhead.n6c10` distributed/TCP benchmark with 4 workers and 10
  partitions now completes correctly:
  - output:
    `benchmark_results/adhead-distributed-exact-w4-batch-20260630-100447.out`;
  - `best_lower_bound 48373`;
  - `best_lower_bound_raw 48373000`;
  - `objective_scale 1000`;
  - `objective_scale_promotions 1`;
  - `final_disagreement_count 0`;
  - `final_regularization_budget 180`;
  - `capacity_scale_saturation_count 0`;
  - `timing_solve_round_count 1820`;
  - `timing_solve_round_batch_count 728`;
  - wall time `3:45.06`;
  - max RSS `6342852 kB`.
- The 4-worker run initially exceeded the `M=100` regularization budget during
  the schedule, promoted to `M=1000`, and finished with final budget below the
  active objective scale.
- Verified:
  - `cmake --build third_party/mcpd3/build -j`;
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`;
  - exact distributed/TCP adhead run with 4 workers, 10 partitions, and no
    saturation.

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

## 2026-06-29 23:30 PDT

- Finished the remaining network-free Stage 2 ownership work.
- Decided the MVP worker path will not keep persistent primal upper-bound
  state. If the coordinator needs primal information later, it should issue an
  explicit worker compute request rather than enabling always-on worker
  tracking.
- Extended the in-process worker API so a solve request names its target
  `partition_id`.
- Reworked `InProcessPartitionWorker` to own multiple loaded partitions, each
  with its own live `PrimalDualMinCutSolver`, constraint arcs, and constraint
  map.
- Updated `PartitionWorkerCoordinator` to assign packages round-robin across
  the supplied worker objects and issue one solve request per package per
  round. This keeps graph structure loaded once while allowing one worker
  object/process to own several partitions.
- Kept objective-scale promotion compatible with multi-package ownership by
  scaling only workers that actually received packages.
- Added tests for:
  - scripted one-worker/two-package routing, including per-partition alpha
    update delivery;
  - real `InProcessPartitionWorker` solving both partition packages from one
    worker object.
- Committed and pushed the mcpd3 unit on branch `partition-worker-api`:
  `27ff756 Support multi-package partition workers`.
- Verified:
  - `cmake --build third_party/mcpd3/build --target partition_worker_test -j`;
  - `cmake --build third_party/mcpd3/build --target dimacs_dual_decomp_example -j`;
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `ctest --test-dir build --output-on-failure`.

## 2026-06-29 23:38 PDT

- Completed Stage 3 product serialization without adding sockets.
- Added `mcpd3_distributed_protocol` with:
  - length-prefixed frames:
    `uint32 message_type`, `uint64 payload_bytes`, `payload`;
  - explicit little-endian integer encodings;
  - strict complete-frame parsing;
  - typed encode/decode helpers for `HELLO`, `PARTITION_PACKAGE`, `READY`,
    `SOLVE_ROUND_REQUEST`, `SOLVE_ROUND_RESULT`, `SCALE_OBJECTIVE`,
    `ALPHA_UPDATE`, `STOP`, and `ERROR`.
- Added `protocol_serialization_test` covering:
  - header little-endian layout;
  - round trips for every Stage 3 message type;
  - unknown message type rejection;
  - truncated header rejection;
  - payload-size mismatch rejection;
  - wrong expected message type rejection;
  - truncated typed payload rejection;
  - trailing typed payload byte rejection.
- Verified:
  - `cmake -S . -B build`;
  - `cmake --build build --target protocol_serialization_test -j`;
  - `./build/protocol_serialization_test`;
  - `ctest --test-dir build --output-on-failure`;
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`.

## 2026-06-29 23:56 PDT

- Completed Stage 4 TCP loopback runtime in the product repo.
- Added `mcpd3_distributed_runtime` with:
  - POSIX TCP socket RAII;
  - explicit complete-frame send/receive helpers;
  - loopback and explicit IPv4 bind helpers;
  - `TcpPartitionWorker`, implementing the existing
    `mcpd3::PartitionWorker` interface over the serialized protocol;
  - worker-side request loop backed by `InProcessPartitionWorker`;
  - coordinator-side `HELLO` accept/validation.
- Added product binaries:
  - `mcpd3_worker HOST PORT [--name NAME]`;
  - `mcpd3_coordinator DIMACS --port PORT ...`, which reads/partitions a graph,
    accepts workers, sends packages once, runs the worker coordinator, reports
    solve diagnostics, and sends `STOP`.
- Implemented remote handling for `PARTITION_PACKAGE`, `SOLVE_ROUND_REQUEST`,
  `SOLVE_ROUND_RESULT`, `SCALE_OBJECTIVE`, `READY`, `STOP`, and `ERROR`.
- Added `tcp_loopback_test` covering:
  - partial TCP frame receive;
  - oversized payload rejection;
  - invalid worker protocol version rejection;
  - worker-side solver errors returned as `ERROR` frames;
  - remote objective scaling of a loaded partition;
  - full coordinator solve over one TCP worker owning both partitions;
  - objective-scale promotion over TCP via `SCALE_OBJECTIVE`.
- Verified:
  - `cmake -S . -B build`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`;
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - manual process smoke test with `mcpd3_coordinator` and `mcpd3_worker`
    on a temporary DIMACS graph.

## 2026-06-30 00:12 PDT

- Completed Stage 5 correctness and process integration coverage.
- Added committed DIMACS fixtures:
  - `tests/fixtures/hand_bottleneck.max`;
  - `tests/fixtures/dead_end.max`;
  - `tests/fixtures/random_small.max`.
- Added `process_integration_test`, which:
  - runs the in-process `PartitionWorkerCoordinator` reference on each
    fixture;
  - launches real `mcpd3_coordinator` and `mcpd3_worker` processes on the same
    fixture;
  - compares status, stop reason, raw best lower bound, final disagreement
    count, objective-scale promotions, and regularization diagnostics;
  - verifies coordinator accept timeout behavior and error text.
- Added coordinator CLI support for:
  - `--ready-file PATH`, used by process tests and scripts to avoid startup
    races;
  - `--accept-timeout-ms N`, making worker-accept timeout behavior testable;
  - final regularization diagnostic output fields.
- Added `scripts/run_local_process_benchmark.sh`, an optional local hook for
  user-supplied bunny/adhead-style DIMACS files without committing external
  data paths.
- Verified:
  - `cmake -S . -B build`;
  - `cmake --build build -j`;
  - `./build/process_integration_test ./build/mcpd3_coordinator ./build/mcpd3_worker tests/fixtures`;
  - `ctest --test-dir build --output-on-failure`;
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `MCPD3_WORKERS=1 MCPD3_PARTITIONS=1 MCPD3_MAX_ITERATIONS=2 scripts/run_local_process_benchmark.sh tests/fixtures/hand_bottleneck.max`.

## 2026-06-30 00:45 PDT

- Added benchmark timing telemetry to the product TCP runtime:
  - coordinator total wall time;
  - graph read/scale/partition times;
  - worker accept/setup/stop times;
  - solve wall time;
  - coordinator compute time outside remote solve RPCs;
  - coordinator wait-for-worker RPC time;
  - worker-reported local solve time;
  - remote RPC overhead;
  - load/solve/scale request counts.
- Extended `SOLVE_ROUND_RESULT` serialization with an optional trailing
  `worker_solve_wall_us` field. The decoder accepts older no-timing frames as
  zero timing.
- Added non-default saturating capacity scaling for the product coordinator:
  `--saturate-capacity-overflow` and alias `--truncate-capacity-overflow`.
  Strict checked overflow remains the default.
- Added saturation diagnostics:
  - `capacity_scale_overflow_mode`;
  - `capacity_scale_saturation_count`;
  - arc and terminal saturation counts.
- Added `tests/fixtures/overflow_saturate.max` and process coverage proving:
  - strict mode rejects 32-bit overflow;
  - opt-in saturation clamps overflowing capacities and completes a process
    solve.
- Scanned local `adhead.n6c10.max`:
  - `arc_count=75826316`;
  - `max_cap=999999`;
  - safe per-capacity limit for `M=10000` is `214748`;
  - `328844` DIMACS arc records exceed that limit and would be clipped by
    saturation mode.
- Verified:
  - `cmake -S . -B build`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`;
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `MCPD3_WORKERS=1 MCPD3_PARTITIONS=1 MCPD3_MAX_ITERATIONS=2 MCPD3_CAPACITY_MULTIPLIER=10000 MCPD3_SATURATE_CAPACITY_OVERFLOW=1 scripts/run_local_process_benchmark.sh tests/fixtures/overflow_saturate.max`.

## 2026-06-30 00:58 PDT

- Started a saturated process-architecture `adhead.n6c10` run with
  4 worker processes, 10 partitions, and `M=10000`.
- Observed via process CPU sampling that the coordinator was issuing remote
  `solveRound` requests serially: one worker consumed CPU while the
  coordinator and other workers were idle.
- Stopped the run because it was not a valid distributed performance
  benchmark.
- Updated upstream `PartitionWorkerCoordinator::runRound()` so package solves
  are dispatched concurrently across active workers. Multiple packages owned
  by the same worker are still solved sequentially on that worker connection.
- Added a submodule regression test with probe workers that verifies two
  independent workers are inside `solveRound()` concurrently.
- Verified:
  - `cmake --build third_party/mcpd3/build --target partition_worker_test -j`;
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`.

## 2026-06-30 01:23 PDT

- Added streaming optimizer-health telemetry to the productized coordinator:
  - new `PartitionWorkerCoordinatorOptions::progress_report_interval`;
  - new `progress_callback` hook carrying `PartitionWorkerProgressRecord`;
  - `mcpd3_coordinator --progress-every N`;
  - `MCPD3_PROGRESS_EVERY` support in
    `scripts/run_local_process_benchmark.sh`.
- Product progress output now reports, per interval:
  - total iteration, scale, lower bound, best lower bound, disagreement count
    and norm;
  - step size and effective step size;
  - regularization strength, budget, contribution, and active anchor counts;
  - cumulative solve RPC wall time, worker-reported solve wall time, and RPC
    overhead;
  - one `progress_worker` line per worker with worker name, solve count, solve
    RPC wall time, worker solve wall time, and RPC overhead.
- Added tests:
  - submodule callback enabled/disabled/invalid-interval coverage;
  - product process integration coverage proving TCP coordinator progress and
    per-worker timing lines are emitted.
- Fixed a submodule include hygiene issue: `graph/dimacs.h` now includes
  `<stdexcept>` because it throws `std::runtime_error` when included directly.
- Ran an `adhead.n6c10` saturated telemetry smoke with 4 workers, 10
  partitions, `M=10000`, `--progress-every 1`, and opt-in saturation. The run
  was intentionally stopped after the first progress record.
- The first adhead progress record showed the health issue directly:
  - `disagreement_count=456270`;
  - `solve_round_count=10`;
  - total solve RPC wall `107233556 us`;
  - worker solve wall `106982322 us`;
  - worker 3 solve wall about `69.7 s`;
  - worker 4 solve wall about `35.5 s`;
  - workers 1 and 2 were each under `1 s`.
- Conclusion from the smoke: the current run is not wedged, but static
  partition ownership plus the round barrier creates severe per-round load
  imbalance on `adhead`.
- Verified:
  - `cmake --build third_party/mcpd3/build -j`;
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`;
  - `MCPD3_PROGRESS_EVERY=1` adhead saturated smoke via
    `scripts/run_local_process_benchmark.sh`.

## 2026-06-30 09:33 PDT

- Compared monolithic and distributed/TCP `adhead.n6c10` behavior.
- Confirmed why the prior monolithic `M=10000` result was correct despite
  unchecked overflow:
  - adhead has `328844` arc records above the safe `M=10000` 32-bit limit;
  - those records all have raw capacity `999999`;
  - unchecked 32-bit-style multiplication maps `999999 * 10000` to the large
    positive value `1410055408`, not a negative value;
  - saturation maps those arcs to `2147483647`;
  - both values are larger than the scaled optimum, so on this instance those
    arcs behave as effectively infinite either way. This is not a correctness
    guarantee.
- Fixed the monolithic benchmark driver to reject initial capacity multiplier
  overflow instead of relying on unchecked `int` wraparound. Verified
  `adhead.n6c10 --capacity-multiplier 10000` now exits with:
  `capacity multiplier exceeds int range`.
- Established the exact mono/distributed comparison path:
  `--capacity-multiplier 100` with objective-scale promotion to `1000`.
- Baseline exact distributed/TCP run before the optimization:
  - command used 10 workers for 10 partitions;
  - `capacity_scale_overflow_mode strict`;
  - `capacity_scale_saturation_count 0`;
  - `best_lower_bound 48373`;
  - `best_lower_bound_raw 48373000`;
  - `objective_scale 1000`;
  - `objective_scale_promotions 1`;
  - `final_disagreement_count 0`;
  - wall time `3:57.62`;
  - `timing_solve_wall_us 150154294`;
  - `timing_worker_rpc_overhead_us 87912105`.
- Fixed two distributed overhead sources:
  - `PartitionWorkerCoordinator` now keeps only coordinator metadata instead
    of retaining full graph payloads after workers load partitions;
  - `PartitionWorkerCoordinator` now sends alpha updates only for dirty
    constraints, while still sending the one-round `last_alpha` catch-up
    needed by scaled-epsilon regularization.
- Added regression coverage proving dirty-alpha requests:
  - do not resend initial zero alpha state;
  - send changed alpha on the next request;
  - send the `last_alpha` catch-up exactly once after agreement;
  - stop sending once synchronized.
- Exact distributed/TCP run after the optimization:
  - output:
    `benchmark_results/adhead-distributed-exact-w10-moved-packages-20260630-092747.out`;
  - `best_lower_bound 48373`;
  - `best_lower_bound_raw 48373000`;
  - `objective_scale 1000`;
  - `objective_scale_promotions 1`;
  - `final_disagreement_count 0`;
  - `capacity_scale_saturation_count 0`;
  - wall time `3:31.71`;
  - `timing_solve_wall_us 137512497`;
  - `timing_worker_rpc_overhead_us 28968042`.
- The optimized distributed/TCP result is now close to the monolithic exact
  `M=100` promoted run (`3:26.77`) and remains exact.
- Verified:
  - `cmake --build third_party/mcpd3/build -j`;
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`;
  - monolithic overflow guard on adhead `M=10000`;
  - exact distributed/TCP adhead run with 10 workers and no saturation.

## 2026-06-30 10:10 PDT

- Fixed regularized lower-bound accounting in an isolated worktree
  (`certified-lb-lower-bound`).
- Clarified and implemented the certificate arithmetic used by both
  monolithic `DualDecomposition` and distributed `PartitionWorkerCoordinator`:
  - local solvers report the selected solution's unregularized value;
  - accepted regularized objective is `selected + contribution`;
  - certified original-problem lower bound is
    `selected + contribution - budget`.
- `best_lower_bound_raw` and progress `lower_bound` now store/report the
  certified original-problem lower bound instead of the selected value from
  the regularized solve.
- Added `regularized_objective` / `best_regularized_objective` diagnostics to
  coordinator progress and final distributed output, plus matching monolithic
  example/getter diagnostics.
- Added tests for:
  - certificate arithmetic, including overflow/underflow detection;
  - regularized round accounting with nonzero contribution and budget;
  - unregularized round accounting remaining unchanged;
  - progress callback fields;
  - regularized agreement storing certified LB and regularized objective
    separately;
  - objective-scale promotion preserving the certified bound and diagnostic;
  - process/TCP output comparing the new diagnostic against the in-process
    reference.
- Verified:
  - `cmake --build third_party/mcpd3/build -j`;
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure` with loopback permission.
