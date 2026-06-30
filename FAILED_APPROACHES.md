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

## 2026-06-29 18:14 PDT

- Do not leave `dimacs_dual_decomp_example` premultiplying all DIMACS
  capacities by `10000` by default. That was historical compatibility for the
  old approximate regularization scheme and also changed alpha resolution.
  The benchmark binary now defaults to `capacity_multiplier=1`.
- Do not compare unscaled benchmark logs until objective/reporting scale is
  decoupled from DD step size. Before commit `c398487`, reported
  `*_unscaled` values were only meaningful because the example also
  premultiplied capacities by the same value as the initial step.
- `babyface.n6c10` is not currently a clean regularization-required example:
  unscaled no-reg runs stalled at lower bound `1373` versus `.sol=19448`;
  compatibility scaled symmetric alpha shift was worse than scaled no-reg;
  compatibility scaled local-search partitioning was also worse than scaled
  no-reg.
- Do not use local lexicographic regularization as a broad benchmark
  screening mode on `babyface.n6c10` without tighter controls. The checked
  compatibility scaled run reached step size `10` but each local solve took
  about 19-20 seconds and still had hundreds of disagreements when stopped.
- Do not launch `adhead.n6c10` blindly in the current 15GB RAM environment.
  It is much larger than babyface, swap is already full, and babyface used
  about `5.3GB` RSS.

## 2026-06-29 19:31 PDT

- Do not conclude that `babyface.n6c10` is not a regularization/convergence
  case. The original `early_experiments` branch converges to
  `lower_bound=19448` with zero disagreement on this instance.
- Do not treat the current exact lexicographic regularizer as a drop-in
  replacement for the old additive low-scale regularizer. On
  `babyface.n6c10`, the current exact path becomes orders of magnitude slower
  at step size `10` and did not reach agreement in the checked window.
- Do not compare current branch regularization experiments only through
  `--stream-directed-input`. The original branch used the general
  `read_dimacs()` path. Directed streaming changes the decomposition surface
  and should be validated separately before using it as benchmark evidence.

## 2026-06-29 19:57 PDT

- Do not treat the OG regularization warning as an exactness certificate. The
  proof requires a summed global effective regularization range below the
  objective scale; the OG check is per local solver and warning-only.
- Do not use final regularization contribution as the budget for the proof.
  The required budget is the maximum possible regularization swing in the
  local solves whose lower bounds are summed.
- Do not assume the OG incremental budget variable measures the actual
  perturbation in the maxflow graph. Skipped constrained nodes can retain stale
  terminal perturbations, and changing low-scale regularization strength does
  not force a full terminal recomputation.

## 2026-06-29 20:45 PDT

- Do not anchor every previous sink boundary copy on every low-scale solve.
  That over-broad productized variant over-regularized `babyface.n6c10`:
  `final_regularization_budget_raw=876603`, far above the `10000` objective
  scale, and it still ended with hundreds of disagreements. The useful OG
  shape refreshes anchors only when the local DD alpha term changes.
- Do not hide stale or persistent epsilon terms from diagnostics. The hardened
  implementation treats active epsilon terms as explicit state and reports the
  full active budget, including terms that persist across unchanged-alpha
  solves.
- Superseded by later objective-scale promotion work: at this point the
  over-budget behavior was intentionally warning-only. If
  `regularization_budget >= regularization_budget_limit`, the run may still
  stop on regularized agreement for now, but that result must be treated as
  potentially non-certifying until future code rejects, rescales, or otherwise
  handles the over-budget condition.
- Do not use `babyface.n6c10` as the primary benchmark for this step. It
  remains a useful sanity/regression case, but the benchmark comparison
  requested here is `adhead.n6c10` against the OG scheme.
- The OG `early_experiments` benchmark run on this machine required a local
  worktree-only shim to remove unused CSR/primal-decoding code that depended
  on missing Boost headers. That shim did not modify the OG DD solver or
  regularizer, but benchmark notes should mention it.

## 2026-06-29 21:43 PDT

- Do not use `--capacity-multiplier 100` without objective-scale promotion for
  the scaled-epsilon `adhead.n6c10` benchmark. It reached zero disagreement,
  but the active regularization budget exceeded the strict limit by orders of
  magnitude (`11036 >= 100`), so the run was non-certifying and the reported
  lower bound rose above the known optimum.
- Lowering the multiplier from `10000` to `100` without handling over-budget
  regularization did not improve performance on `adhead.n6c10`; wall time
  increased from `1:24.18` to `3:31.91`.

## 2026-06-29 22:43 PDT

- Do not accept a lower bound from an iteration whose scaled-epsilon active
  regularization budget is greater than or equal to the objective scale. The
  dynamic-promotion implementation now detects this before updating the best
  lower bound, then promotes the objective scale and restarts the schedule.
- Do not assume a smaller initial multiplier is faster. With dynamic
  promotion, `adhead.n6c10 --capacity-multiplier 100` became certifying by
  promoting to `1000`, but still took `3:26.77` versus `1:24.18` for starting
  directly at `10000`.
- The first dynamic-promotion commit only covered the current monolithic
  `DualDecomposition` benchmark path. That was incomplete for productization;
  the productized `PartitionWorkerCoordinator` also needs promotion/rescale
  handling.

## 2026-06-29 22:58 PDT

- Do not describe the current monolithic `DualDecomposition` path as a
  separate old branch. It is current code, but it is not the whole
  productized worker-coordinator path.
- Do not let worker-coordinator over-budget rounds mutate alpha state or enter
  accepted progress. The coordinator now computes disagreement diagnostics for
  the over-budget round, rejects its lower bound, skips alpha updates, and
  then either promotes or returns `REGULARIZATION_BUDGET_EXCEEDED`.
- Remaining distributed-protocol note: the in-process worker API has
  `scaleObjective(long factor)`, but the future TCP protocol still needs an
  explicit rescale message before remote workers can preserve live solver
  state across promotion.

## 2026-06-29 23:12 PDT

- Do not interpret the low-`M` opposite-direction cycle failure with
  `num_optimization_scales=2` as a regularization failure. That setup was
  artificially under-provisioned after promotion: starting at `M=10` promotes
  to `M=100`, and the restarted schedule needs enough scale levels and
  low-scale iterations to reach agreement.
- The corrected low-`M` cycle regression uses `num_optimization_scales=3` and
  `max_iteration_count=100`, and it passes with one promotion to `M=100`.

## 2026-06-29 23:30 PDT

- Do not model primal upper-bound decoding as persistent worker state for the
  MVP. The coordinator can request explicit worker-side computation later if
  it needs primal information.
- Do not assume one package per worker object. The in-process worker API now
  supports multiple loaded packages per worker, and solve requests must carry
  a `partition_id` when a worker owns more than one partition.

## 2026-06-29 23:38 PDT

- Do not add TCP behavior into the Stage 3 serialization module. Stage 3 is
  bytes-in/bytes-out only; socket lifecycle and partial network reads belong
  to Stage 4.
- The current frame decoder intentionally expects a complete frame buffer.
  Stage 4 can add a stream/framing reader that accumulates bytes from sockets
  before calling the strict decoder.

## 2026-06-29 23:56 PDT

- Do not test remote objective scaling with an unconstrained one-node local
  partition. That local problem can report a valid zero lower bound, so it is
  not a useful witness that `SCALE_OBJECTIVE` rescaled the live remote solver.
  The Stage 4 test now includes a source-side boundary constraint so the bound
  is nonzero before scaling and must scale by the requested factor.
- Do not treat Stage 4 loopback runtime as operational fault tolerance. It now
  propagates worker `ERROR` frames and rejects invalid handshakes, but worker
  disconnect/reconnect, heartbeat, replacement, and process-level fixture
  tests remain Stage 6/Stage 5 work.

## 2026-06-30 00:12 PDT

- Do not start process-level workers after an arbitrary sleep and assume the
  coordinator is listening. The Stage 5 test and local benchmark hook use the
  coordinator's `--ready-file` signal before launching workers to avoid
  nondeterministic connection-refused failures.
- Do not commit local Waterloo/bunny/adhead benchmark data paths into the test
  suite. Keep committed Stage 5 fixtures tiny and deterministic; use
  `scripts/run_local_process_benchmark.sh` for user-supplied local datasets.

## 2026-06-30 00:45 PDT

- Do not assume `adhead.n6c10 --capacity-multiplier 10000` is safe in the
  product/distributed path. The real max DIMACS arc capacity is `999999`, so
  scaling by `10000` exceeds the 32-bit package/solver capacity type.
- Do not treat saturating capacity overflow as exact. It is an explicit
  benchmark/compatibility mode for running the current 32-bit implementation
  when a requested multiplier is too large. Any run with
  `capacity_scale_saturation_count > 0` solved a clipped-capacity problem.
- Do not hide the distinction between modulo overflow and saturation. The old
  monolithic benchmark path multiplied `int` capacities unchecked. The product
  compatibility mode clamps to `INT_MAX`/`INT_MIN`, which is safer and
  diagnosable but not bit-for-bit old-overflow behavior.

## 2026-06-30 00:58 PDT

- Do not treat process-level coordinator/worker tests as evidence of parallel
  solve throughput by themselves. Before the `PartitionWorkerCoordinator`
  dispatch fix, the coordinator contacted workers serially, so a 4-worker
  `adhead` run showed one worker at 100% CPU while the coordinator and the
  other workers were idle.

## 2026-06-30 01:23 PDT

- Do not diagnose a one-hot-worker snapshot as a deadlock without checking
  round-level progress. After concurrent dispatch, `adhead.n6c10` still shows
  barrier tails where one worker finishes a much more expensive partition set
  while other workers wait for the next coordinator round.
- Do not rely on `ps %CPU` alone for solver health. It is lifetime-averaged
  and can miss short round-boundary changes. Use `--progress-every N` for
  optimizer fields and `pidstat` only as a process-level liveness/imbalance
  supplement.
- Do not treat the current `M=10000` saturated adhead telemetry smoke as an
  exact benchmark. It clipped `328844` capacities before solving; the smoke was
  only to validate health telemetry and identify load imbalance.

## 2026-06-30 09:33 PDT

- Do not use the old monolithic `adhead.n6c10 --capacity-multiplier 10000`
  result as an exact overflow-safe benchmark. It happened to reach the correct
  cut because `999999 * 10000` wrapped to a large positive value
  (`1410055408`) and those arcs still behaved as effectively infinite on this
  instance.
- Do not compare distributed/TCP with fewer workers than partitions and expect
  monolithic-like timing. With 4 workers and 10 partitions, multiple expensive
  partitions can be pinned behind one worker connection and solved
  sequentially. The exact local comparison should use 10 workers for 10
  partitions until there is a real dynamic work-stealing or batched
  multi-partition worker protocol.
- Do not resend full alpha state every round. Most alpha records are unchanged
  after early iterations; dirty-only alpha sync reduced exact adhead cumulative
  worker RPC overhead from `87912105 us` to `28968042 us`.

## 2026-06-30 10:09 PDT

- The prior one-worker-per-partition comparison rule is obsolete after the
  batched worker RPC implementation. Do not use older 4-worker/10-partition
  results from the per-partition RPC path as representative of current
  performance.
- Do not confuse fixed ownership batching with dynamic load balancing. A
  worker can now receive one batch and solve its owned partitions concurrently,
  but partition ownership is still static for the run; work stealing and
  repartitioning remain future work.
- Do not interpret summed `timing_coordinator_wait_worker_us` as elapsed wall
  time. It is accumulated across parallel worker RPCs and can exceed
  `timing_solve_wall_us`; use the wall-time fields plus per-worker progress
  timing to diagnose imbalance.

## 2026-06-30 10:10 PDT

- Do not treat the selected unregularized value from a regularized local solve
  as a certified original-problem lower bound. The local solver selects
  `argmin(F + r)` but reports `F(x_selected)`, which can be above the true
  local minimum of `F`.
- The conservative certificate for an accepted regularized round is:
  `F(x_selected) + r(x_selected) - R`, where `R` is the active regularization
  budget and `r(x_selected)` is the actual regularization contribution paid by
  the selected solution. Subtracting `R` directly from `F(x_selected)` is safe
  but unnecessarily pessimistic; not subtracting the slack at all can overstate
  the lower bound.

## 2026-06-30 10:31 PDT

- Do not revert to round-robin as the default static partition map. It ignores
  both package-size skew and worker resources. The current setup path uses a
  deterministic largest-partition-first weighted assignment from package node,
  arc, and boundary counts plus worker CPU/RAM estimates.
- Do not treat static weighted assignment as dynamic load balancing. Packages
  are still sent once and remain owned by the assigned worker for the solve;
  moving packages later would require explicit migration/reload semantics.
- Do not compare the pre-balancing 4-worker batched adhead run against the
  post-balancing run without noting the ownership change. On the checked
  `adhead.n6c10` run, static weighted assignment improved wall time from
  `3:45.06` to `3:22.74`.

## 2026-06-30 10:46 PDT

- Do not use the conservative certified lower bound as the user-facing solved
  objective after budget-safe regularized agreement. For adhead, the certificate
  is `48372.9` while the selected/original final objective is `48373`.
- Report selected/original objective, certified lower bound, and regularized
  objective as separate fields. The certificate remains the valid lower bound;
  the selected/original objective is the final value once agreement certifies
  primal recovery.
