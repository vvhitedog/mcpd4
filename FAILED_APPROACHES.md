# Failed Approaches And Taboos

## 2026-07-03 02:28 PDT

- Do not use a BK mmap directory on a memory-backed filesystem. Paths under
  `/tmp` are not portable: this laptop has `/tmp` on ext4, but many Linux
  systems mount `/tmp` or `/dev/shm` as `tmpfs`. If BK `file_mmap` lands on
  tmpfs, it is effectively RAM-backed and can kill the worker during partition
  loading. mcpd4 workers now reject `tmpfs`, `ramfs`, and `hugetlbfs` for BK
  mmap dirs.
- The p32 large-adhead distributed restart connected both workers, then the
  remote worker disconnected while loading partition `6` after completing
  partitions `0`, `2`, and `4`. Do not interpret the coordinator-side
  `socket closed during read` as a network root cause without first checking
  the remote worker log, mount point for `--bk-mmap-dir`, free disk, and kernel
  OOM messages. Remote BK mmap directory disk exhaustion is the leading
  suspicion for this run.

## 2026-07-03 02:08 PDT

- Do not assume BK arrays are file-backed unless worker status says
  `bk_storage file_mmap`. The p24/p32/p48 large-adhead resident runs were
  launched before mcpd4 defaulted workers to file-backed BK storage, and the
  logged worker commands lacked `--bk-storage file_mmap`/`--bk-mmap-dir`.
  Kernel OOM logs showed `anon-rss` around `7.8 GB` and `file-rss` near zero,
  confirming heap pressure rather than file-backed page-cache pressure.
- Do not rely on an implicit BK mmap directory for large production benchmarks
  on a nearly full root filesystem. The default is `/var/tmp`, but large runs
  should pass `--bk-mmap-dir` on a known disk-backed fast filesystem with
  enough free space for that worker's assigned BK node/arc arrays.

## 2026-07-03 01:54 PDT

- Do not rely on the coordinator UDP status endpoint for post-mortem errors.
  It is an in-process thread and disappears when the coordinator exits. Use
  `mcpd4_coordinator --status-file PATH` and inspect it with
  `mcpd4_status --file PATH` after a crash/failure.
- Do not treat a bare `socket write failed: Broken pipe` or
  `socket closed during read` as sufficient distributed-run diagnostics.
  That error only says the peer disconnected. The runtime now logs partition
  load begin/done/failed lines with worker and partition context so the failing
  package can be identified.

## 2026-07-03 01:30 PDT

- Do not benchmark large adhead resident distributed workers on the 15 GB
  laptop with default malloc-backed BK storage. p24 and p32 killed the local
  worker during package load; p48 also failed during package send/load. Use
  BK `file_mmap` with a fast directory that has enough free space, use more
  worker memory, or keep the laptop as coordinator-only.

## 2026-07-03 01:04 PDT

- Large adhead p16 resident distributed run failed under the old TCP frame cap:
  workers reported `frame payload exceeds maximum size` because p16 partition
  packages are roughly 320-359 MiB logical, while the runtime still had a
  256 MiB receive limit. Snappy does not avoid this because the cap applies to
  the logical protocol frame before compression. Fix: use the shared 1 GiB
  frame cap and sender-side enforcement; if a future graph exceeds that cap,
  increase the partition count or implement multipart partition-package RPC.

## 2026-07-02 00:53 PDT

- Do not evict and cold-reload a regularized partition while preserving only
  capacities and alpha metadata. The scaled-epsilon regularizer anchors from
  the previous local cut labels; losing that label vector made a forced
  streaming reload disagree with the resident in-process worker on
  regularization budget diagnostics.
- Historical pre-warm-state limitation: disk-backed streaming workers were
  not initially a warm-solver performance optimization. That version preserved
  correctness-critical alpha and label state across eviction, but rebuilt BK
  solver/residual state on reload.

## 2026-06-29 00:24:08 PDT

- Do not add TCP, MPI, or serialization before the in-process partition-worker
  API has an equivalence test.
- Do not move product deployment/runtime concerns into `third_party/mcpd3`.
- Do not rename the `third_party/mcpd3` solver, its public `mcpd3::` API
  namespace, or upstream branch as part of the product rebrand. The product
  wrapper is `mcpd4`; the solver dependency remains `mcpd3`.
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
- Do not expose a separate `selected_objective` /
  `best_selected_objective` user-facing field. It duplicates `final_objective`
  once agreement certifies primal recovery and makes the reporting semantics
  harder to reason about. Keep `final_objective` for the solved value, and keep
  certified lower-bound plus regularized-objective fields as diagnostics.

## 2026-07-01 22:44 PDT

- Do not treat the first native mcpd3 adhead run as a completed benchmark.
  Command:
  `MCPD3_PARTITIONER=basic build/mcpd3-native/mcpd3_native_monolith_benchmark data/maxflow/adhead.n6c10/adhead.n6c10.max --directed --partitions 10 --objective-scale 1000 --schedule-start 10000 --schedule-levels 5 --max-iterations 10000`.
  It was terminated by SIGTERM after `552.78s` with no final result.
- Do not compare that aborted native run against mcpd4 distributed timings.
  It showed about `8.1 GB` RSS and roughly one-core CPU use before termination,
  so the next investigation should compare direct `DualDecomposition::solve()`
  stopping/scheduling against the worker-coordinator path before drawing
  performance conclusions.
- Do not rely on fully buffered benchmark stdout for long runs. The native
  benchmark now sets unbuffered stdout, but the aborted run happened before
  that change, leaving an empty `.out` file.

## 2026-07-01 23:39 PDT

- Do not treat the `adhead.n6c10 p16 objective_scale=1000` failure as proof
  that p16 has an inherent algorithmic convergence bug. The observed failure
  was: regularization budget exceeded `1000`, the coordinator attempted to
  promote by `10x`, and 32-bit capacity promotion overflowed.
- Exact p16 does converge when the objective scale is chosen high enough but
  still within int32 input capacity limits: `p16 objective_scale=2000` reached
  agreement with objective `48373`.
- `--truncate-capacity-overflow` now lets p16/os1000 promotion proceed to
  scale `10000`, but that run may clip promoted capacities. Use it only as
  explicit compatibility mode, not as the exact local baseline.

## 2026-07-02 00:17 PDT

- Do not run large-adhead distributed package generation through solver-backed
  `DualDecomposition` export. Before `construct_solvers=false`, p32
  partition-only on `adhead.n26c100` climbed to about `14.1 GB` RSS with swap
  full and only about `7.5 GB` disk free before it was stopped. The issue was
  double construction: local native solvers were being built only to export
  packages, then worker solvers would be built again.
- Do not assume fewer partitions are automatically safer for the large local
  setup path. p24 has lower boundary/package overhead than p32, but with BK
  `file_mmap` it drove free disk down to about `3.7 GB` before setup completed,
  so it was manually terminated. On this laptop, p32 completed setup while p24
  did not.
- Do not treat p40/p48 as better just because local subproblems are smaller.
  Partition-only probes increased boundary endpoint counts from p32's
  `4,194,304` to `5,275,972` and `6,291,456`, respectively, with no partition
  wall-time improvement.

## 2026-07-11 01:44 PDT

- Do not treat `babyface.n6c10` local TCP p10/w10 `objective_scale=1000` as an
  exact completed benchmark. Run
  `benchmark_results/local_tcp_optimized_full_babyface_p10_w10_none_20260711_013605`
  reached iteration `96`, best certified lower bound raw `18,599,400`, then
  exceeded the regularization budget and failed during objective-scale
  promotion with `objective scale promotion exceeds int`.
- Do not treat `babyface.n6c10` `objective_scale=10000` strict mode as runnable
  with the current int32 capacity representation. Run
  `benchmark_results/local_tcp_optimized_full_babyface_p10_w10_os10000_none_20260711_014141`
  failed during the initial scale pass with `objective scale exceeds int
  range`.
- If using `--saturate-capacity-overflow` for these local TCP performance
  sweeps, keep the result labeled as clipped-capacity compatibility data. The
  saturated `babyface.n6c10` scale-10000 run clipped `11,370` terminal
  capacities during initial scaling, so it is useful for transport/runtime
  performance but not an exact strict-capacity proof.

## 2026-07-11 02:06 PDT

- Do not use one `send()` per partition-package buffer as the scatter/gather
  implementation. Run
  `benchmark_results/local_tcp_scatter_package_defaultbk_babyface_p10_w2_none_20260711_020321`
  was slower than the contiguous move-return copy-drop run on the comparable
  mmap-backed p10/w2 one-iteration setup point:
  - per-buffer send: setup `17,758,739us`, total `24,141,107us`;
  - contiguous move-return copy drop:
    `benchmark_results/local_tcp_sendrecv_copydrop_defaultbk_babyface_p10_w2_none_20260711_015836`
    setup `17,300,658us`, total `23,832,448us`.
- Scatter/gather should use `sendmsg` or another batched write path. The
  `sendmsg` run
  `benchmark_results/local_tcp_sendmsg_package_defaultbk_babyface_p10_w2_none_20260711_020445`
  recovered the per-buffer-send loss while preserving the coordinator memory
  pressure benefit.

## 2026-07-11 02:24 PDT

- Do not claim the mcpd3 boundary-map reserve change as a proven standalone
  speedup from the p10/w10 local TCP benchmark. The first run improved setup
  substantially (`7,951,065us`), but the repeat was close to the previous
  p10/w10 baseline (`9,117,897us` versus `9,250,669us`). Keep the change as
  allocation hygiene with test coverage, and use the worker-count sweep as the
  stronger result.

## 2026-07-11 02:32 PDT

- Do not enable `TCP_NODELAY` as a claimed local TCP setup optimization based
  on current evidence. A temporary implementation with loopback coverage was
  benchmarked on p10/w10/file-backed mmap with `willneed`, then reverted:
  `benchmark_results/local_tcp_nodelay_willneed_filemmap_babyface_p10_w10_none_20260711_023104`
  setup `9,150,496us`, total `15,845,669us`, worse than the `willneed` runs
  without TCP_NODELAY.
- Do not default to snappy compression for localhost p10/w10 setup. It reduced
  coordinator wire TX from `572,751,240` to `278,055,791` bytes in
  `benchmark_results/local_tcp_snappy_filemmap_babyface_p10_w10_20260711_022738`,
  but setup `8,391,191us` and total `14,641,085us` did not beat no-compression
  with BK mmap `willneed`.

## 2026-07-11 02:42 PDT

- Do not compare local TCP benchmark runs launched concurrently as if they were
  independent points. The first p5/w5 and p7/w7 after-memory-free probes were
  started at the same time:
  - `benchmark_results/local_tcp_willneed_after_memfree_babyface_p5_w5_none_20260711_024001`;
  - `benchmark_results/local_tcp_willneed_after_memfree_babyface_p7_w7_none_20260711_024001`.
  They share CPU, disk, and page cache pressure, so use the later sequential
  p5/w5 and p7/w7 runs for comparisons.
- Do not run benchmarks from this worktree with a relative `data/...` DIMACS
  path unless the data directory has been linked into the worktree. Run
  `benchmark_results/local_tcp_willneed_after_memfree_babyface_p10_w10_none_20260711_023518`
  failed before graph load because `data/maxflow/babyface.n6c10/babyface.n6c10.max`
  was not present under `.worktrees/local-tcp-opt`; the valid rerun used the
  absolute path from the main checkout.

## 2026-07-11 02:53 PDT

- Do not switch localhost p6/w6 local TCP to Snappy by default after the compact
  worker-load package change. Run
  `benchmark_results/local_tcp_omit_l2g_snappy_filemmap_babyface_p6_w6_20260711_025317`
  reduced coordinator wire TX to `249,634,612` bytes, but took
  `9,425,194us` total versus `8,778,549us` for no-compression in
  `benchmark_results/local_tcp_omit_l2g_filemmap_babyface_p6_w6_none_20260711_024927`.
  Compression alone cost `1,100,580us`.

## 2026-07-11 03:01 PDT

- Do not claim compact endpoint records as a proven localhost wall-time speedup
  on p6/w6. The change reduced partition-load TX from `532,500,240` to
  `526,500,240` bytes and preserved outputs, but the measured file-backed run
  was `8,887,600us` versus `8,778,549us` before endpoint compaction, and the
  measured malloc run was `8,376,554us` versus `8,290,466us`. Treat it as a
  transport-size optimization, not a localhost timing win.

## 2026-07-11 03:07 PDT

- Do not assume fewer partitions are faster once malloc-backed BK storage fits.
  With `13Gi` available, p2/w2 through p7/w7 all fit on `babyface.n6c10`, but
  the one-iteration local TCP totals still favored p6/w6:
  - p2/w2: `12,078,335us`;
  - p3/w3: `8,478,821us`;
  - p4/w4: `8,522,239us`;
  - p5/w5: `10,385,090us`;
  - p6/w6 repeat: `8,324,249us`;
  - p7/w7: `9,580,102us`.
- p2 has the smallest partition-load transfer in this sweep, but its solve time
  (`4,773,711us`) dominates. Use p6/w6 as the current local TCP comparison
  point until a larger/more complete convergence benchmark says otherwise.

## 2026-07-11 03:18 PDT

- Do not replace `DualDecomposition::initializeDecomposition` constrained-node
  partition sets with per-node `std::vector<int>` de-duplication based on the
  current evidence. The experiment preserved results but did not improve the
  matched p6/w6 malloc/no-compression `babyface.n6c10` one-iteration probe:
  - post-reader baseline repeat
    `benchmark_results/local_tcp_presized_dimacs_repeat_malloc_babyface_p6_w6_none_20260711_031335`:
    total `7,993,139us`, partition `1,327,133us`, setup `3,260,440us`;
  - vector boundary-set run
    `benchmark_results/local_tcp_vector_boundary_sets_malloc_babyface_p6_w6_none_20260711_031732`:
    total `8,105,027us`, partition `1,388,056us`, setup `3,288,289us`;
  - vector boundary-set repeat
    `benchmark_results/local_tcp_vector_boundary_sets_repeat_malloc_babyface_p6_w6_none_20260711_031757`:
    total `8,076,785us`, partition `1,380,602us`, setup `3,279,833us`.
- The uncommitted code was reverted. If this area is revisited, first add
  finer-grained package-build timing so we can see whether boundary map/set
  maintenance is actually material on larger partition counts.
