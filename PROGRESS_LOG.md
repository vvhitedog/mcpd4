# Progress Log

## 2026-07-02 00:53 PDT

- Implemented explicit disk-backed streaming partition workers:
  - `mcpd3::StreamingPartitionWorker` stores partition arc/capacity payloads
    on disk and materializes `InProcessPartitionWorker` solvers on demand;
  - supports an approximate resident-byte cache limit with LRU eviction;
  - keeps alpha metadata in memory and persists the previous local min-cut
    labels across eviction so scaled-epsilon regularization anchors match the
    resident worker path;
  - supports objective-scale promotion by scaling both resident solvers and
    evicted disk payloads.
- Added mcpd3 regression coverage:
  - streaming worker matches an in-process worker across forced eviction,
    alpha updates, and regularized solves;
  - evicted disk payloads scale correctly before a later reload.
- Exposed streaming storage in mcpd4:
  - worker CLI flags: `--streaming-partitions`, `--streaming-dir DIR`, and
    `--streaming-cache-bytes N`;
  - in-process benchmark flags: `--streaming-workers`/`--streaming-partitions`,
    `--streaming-dir DIR`, and `--streaming-cache-bytes N`;
  - worker UDP status reports storage mode, streaming directory, and cache
    bytes.
- Added process integration coverage that runs real `mcpd4_worker` processes
  with `--streaming-partitions --streaming-cache-bytes 1` and compares the
  result against the in-memory worker reference.
- Added README documentation for out-of-core worker storage with concrete
  process-worker and in-process benchmark examples.
- Verified:
  - `cmake --build build/mcpd3-native -j`;
  - `ctest --test-dir build/mcpd3-native --output-on-failure`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`;
  - `cmake --build build/no-snappy -j`;
  - `ctest --test-dir build/no-snappy --output-on-failure`;
  - streaming CLI smoke:
    `MCPD3_PARTITIONER=basic build/mcpd4_inprocess_benchmark tests/fixtures/random_small.max --directed --workers 1 --partitions 2 --objective-scale 10 --schedule-start 10 --schedule-levels 2 --max-iterations 50 --streaming-workers --streaming-cache-bytes 1 --progress-every 25`.

## 2026-07-01 16:37 PDT

- Added `mcpd4_inprocess_benchmark`, a local benchmark executable that uses
  the product `mcpd3::PartitionWorkerCoordinator` path with
  `InProcessPartitionWorker` instances instead of TCP workers.
- Ran a one-worker in-process `adhead.n6c10` baseline with the same 10
  partitions, objective scale `1000`, schedule start `10000`, five schedule
  levels, and directed input used by the two-machine delta/no-Snappy run:
  - output prefix:
    `benchmark_results/adhead-local-inprocess-w1-p10-os1000-20260701-163403`;
  - `final_objective_raw=48373000`, `total_iterations=108`,
    `final_disagreement_count=0`;
  - in-process wall `158.95s`;
  - two-machine delta/no-Snappy fair wall excluding discovery was `158.28s`;
  - in-process solve wall `87.91s` versus distributed solve wall `90.51s`.
- Interpretation: with this partition count and laptop CPU count, the
  two-machine run did not produce a meaningful end-to-end speedup over one
  local in-process worker. The remote worker mainly replaces local maxflow
  work but adds setup/transport overhead; future gains likely require reducing
  partition upload/setup cost, changing partition/worker balance, or using a
  larger case where local in-process parallelism is saturated.

## 2026-07-01 01:55 PDT

- Added temporal delta encoding for live TCP solve traffic:
  - introduced `mcpd4/delta_codec.h` and `src/delta_codec.cpp`;
  - protocol version is now `5`;
  - solve requests omit unchanged alpha updates and encode changed alpha
    values as temporal varint deltas relative to the previous sent value for
    that partition/constraint;
  - solve results omit unchanged boundary labels while the coordinator-side
    decoder reconstructs the full label list required by mcpd3;
  - temporal baselines reset on partition load and objective-scale promotion;
  - the existing CSV and progress telemetry counters are unchanged and remain
    the comparison surface for before/after runs.
- Added serialization coverage for:
  - first alpha sync, unchanged-alpha omission, changed-alpha reconstruction,
    partition reset, and per-partition batch state;
  - first label sync, unchanged-label reconstruction, changed-label
    reconstruction, same-size label-id full resync, stale-delta rejection, and
    large repeated batch shrinkage.
- Added loopback runtime coverage proving repeated solve requests/results
  shrink in the actual `TcpPartitionWorker` byte counters while decoded solve
  results still contain every boundary label.
- Preserved the existing LAN baseline telemetry under
  `benchmark_results/adhead-lan-w2-p10-os1000-snappy-20260701-011533.*` for
  comparison against future delta-enabled runs.
- Verified:
  - `cmake --build build -j`;
  - `./build/protocol_serialization_test`;
  - `./build/tcp_loopback_test`;
  - `./build/process_integration_test ./build/mcpd4_coordinator ./build/mcpd4_worker ./build/mcpd4_discovery ./build/mcpd4_status tests/fixtures`;
  - `ctest --test-dir build --output-on-failure`;
  - `cmake --build build/no-snappy -j`;
  - `ctest --test-dir build/no-snappy --output-on-failure`;
  - `git diff --check`.

## 2026-07-01 01:08 PDT

- Added optional raw CSV telemetry for coordinator runs:
  - `--telemetry-csv-prefix PATH` writes `PATH.metadata.csv`,
    `PATH.partitions.csv`, `PATH.workers.csv`, `PATH.iterations.csv`,
    `PATH.worker_iterations.csv`, `PATH.worker_rpc_metrics.csv`, and
    `PATH.final.csv`;
  - enabling CSV telemetry forces per-iteration progress callbacks internally
    so every optimizer iteration is recorded, while stdout progress still
    follows `--progress-every`;
  - `iterations.csv` records global optimizer state, iteration wall time,
    solve elapsed time, bounds, disagreement, schedule, and regularization
    fields;
  - `worker_iterations.csv` records per-worker per-iteration solve RPC wall
    deltas, worker solve-time deltas, derived RPC overhead deltas, assignment,
    and cumulative solve counts;
  - `worker_rpc_metrics.csv` records long-form per-worker per-iteration deltas
    and cumulative values for every RPC byte, wire-byte, compression-time, and
    frame-count counter.
- Updated README runbook docs with the CSV flag, emitted files, and the basic
  join path for histogram analysis.
- Added process integration coverage proving a coordinator run writes the CSV
  files and includes the expected timing, RPC, metadata, partition, worker, and
  final-summary fields.
- Verified so far:
  - `cmake --build build -j`;
  - `./build/process_integration_test ./build/mcpd4_coordinator ./build/mcpd4_worker ./build/mcpd4_discovery ./build/mcpd4_status tests/fixtures`;
  - `ctest --test-dir build --output-on-failure`;
  - `cmake --build build/no-snappy -j`;
  - `ctest --test-dir build/no-snappy --output-on-failure`.

## 2026-06-30 23:57 PDT

- Added live coordinator algorithm segment tracking to the UDP status
  endpoint.
- Coordinator status now includes a `segments` field containing started
  logical algorithm segments:
  - `read_graph`;
  - `scale_graph`;
  - `partitioning`;
  - `transport_setup`;
  - `accept_workers`;
  - `coordinator_setup`;
  - `solve`;
  - `stop_workers`.
- Each segment reports:
  - `state=running|done`;
  - `elapsed_us`;
  - `eta_remaining_us` for running segments;
  - progress counters when a meaningful denominator exists;
  - segment-specific stats such as node/arc counts, saturation counts,
    worker accept progress, iteration progress, final solve status, and stop
    reason.
- Status server startup now happens before graph reading when
  `--status-port` is provided, so long graph reads and partitioning can be
  queried too.
- Solve status updates are now decoupled from `--progress-every`: when status
  is enabled, the coordinator records every progress callback for live status
  ETA, while stdout progress still honors `--progress-every`.
- Process integration coverage now queries a live discovery-mode coordinator
  and verifies:
  - completed `read_graph`, `scale_graph`, `partitioning`, and
    `transport_setup` segments;
  - running `accept_workers` with ETA/remaining field and discovery progress;
  - not-yet-started `solve` segment is skipped.
- Verified:
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`;
  - `cmake --build build/no-snappy -j`;
  - `ctest --test-dir build/no-snappy --output-on-failure`.

## 2026-06-30 22:46 PDT

- Added optional Snappy RPC compression as a product transport feature:
  - added `third_party/snappy` as a submodule;
  - added `MCPD4_ENABLE_SNAPPY` CMake option, default `ON`;
  - coordinator and worker now accept `--rpc-compression none|snappy`;
  - workers advertise Snappy support with a `HELLO` feature bit;
  - `HELLO` remains uncompressed and all subsequent frames use the selected
    transport mode.
- Added a Snappy transport envelope that compresses complete protocol frames:
  - compressed frames carry logical byte count and compressed payload count;
  - frames that do not shrink are sent stored inside the Snappy envelope;
  - uncompressed mode keeps the original protocol framing exactly.
- Extended coordinator/worker telemetry:
  - existing `rpc_*_bytes` fields remain logical protocol bytes;
  - new `rpc_*_wire_bytes` fields report actual TCP bytes;
  - added compression/decompression wall time and compressed/stored frame
    counters.
- Added test coverage:
  - TCP loopback test for a large compressible Snappy frame;
  - TCP loopback test proving a remote worker can negotiate Snappy and reduce
    wire bytes for a large partition package;
  - process integration test proving `--rpc-compression snappy` preserves
    fixture solve results and emits compression telemetry.
- Added runbook docs for two-machine Snappy A/B testing and comparison
  fields.
- Added `MCPD4_RPC_COMPRESSION` to the local benchmark helper.
- Local tiny fixture A/B sanity check:
  - no compression:
    `final_objective_raw=40000`, `final_disagreement_count=0`,
    `rpc_tx_wire_bytes_total=1090`, `rpc_rx_wire_bytes_total=1432`,
    `timing_total_wall_us=20743`;
  - Snappy:
    `final_objective_raw=40000`, `final_disagreement_count=0`,
    `rpc_tx_wire_bytes_total=1409`, `rpc_rx_wire_bytes_total=1154`,
    `rpc_compression_wall_us=8`, `rpc_decompression_wall_us=2`,
    `timing_total_wall_us=21273`;
  - total wire bytes were slightly worse on this tiny fixture because many
    coordinator-to-worker frames were too small and were stored in the
    compression envelope. Larger two-machine cases should be compared with the
    new wire/timing counters before deciding whether to enable Snappy.
- Verified:
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`;
  - `cmake -S . -B build/no-snappy -DCMAKE_BUILD_TYPE=Release -DMCPD4_ENABLE_SNAPPY=OFF`;
  - `cmake --build build/no-snappy -j`;
  - `ctest --test-dir build/no-snappy --output-on-failure`;
  - local benchmark helper A/B commands with `MCPD4_RPC_COMPRESSION=none` and
    `MCPD4_RPC_COMPRESSION=snappy`.

## 2026-06-30 11:18 PDT

- Removed the separate public `selected_objective` /
  `best_selected_objective` reporting surface.
- `final_objective[_raw]` is now the user-facing original objective for the
  final agreed solution.
- Kept the distinct certificate diagnostics:
  - `final_certified_lower_bound[_raw]`;
  - `final_regularized_objective[_raw]`;
  - `best_lower_bound[_raw]` / `best_certified_lower_bound[_raw]`;
  - `best_regularized_objective[_raw]`.
- Progress telemetry now reports certified lower-bound and regularized
  objective diagnostics without duplicating the final objective as a selected
  objective.
- Tests were updated to keep checking the internal original-objective
  arithmetic needed for the certificate while rejecting `selected_objective` in
  product progress output.

## 2026-06-30 11:02 PDT

- Renamed solve-count telemetry so counters are not emitted with the
  `timing_` prefix.
- New final/progress counter names:
  - `assigned_partition_count`;
  - `active_worker_count`;
  - `partition_solves_per_iteration`;
  - `solve_batch_rpcs_per_iteration`;
  - `partition_solve_call_count_total`;
  - `solve_batch_rpc_count_total`;
  - `load_partition_rpc_count`;
  - `scale_objective_rpc_count`.
- Removed current output of the confusing names
  `timing_solve_round_count`, `timing_solve_round_batch_count`,
  `solve_round_count`, and `solve_round_batch_count`.
- Product tests now assert the clearer names and verify that final telemetry no
  longer reports batch counters under a `timing_` prefix.
- Verified:
  - `ctest --test-dir build -R tcp_loopback_test --output-on-failure`;
  - `git diff --check`;
  - `git -C third_party/mcpd3 diff --check`.
- Full product build is currently blocked by an unrelated dirty submodule
  reporting refactor that removed `selected_objective` /
  `best_selected_objective` fields while the product coordinator still targets
  the committed submodule API.

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
  - `assigned_partition_count 10`;
  - `active_worker_count 4`;
  - `partition_solves_per_iteration 10`;
  - `solve_batch_rpcs_per_iteration 4`;
  - `partition_solve_call_count_total 1820`;
  - `solve_batch_rpc_count_total 728`;
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
  - added batch RPC count telemetry.
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
  - old counter names at the time:
    `timing_solve_round_count 1820` and
    `timing_solve_round_batch_count 728`;
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
  - old counter name at the time: `solve_round_count=10`;
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

## 2026-06-30 10:46 PDT

- Split regularized reporting so selected/original objective, certified lower
  bound, and regularized objective are distinct fields.
- `PartitionWorkerCoordinator` and `DualDecomposition` now track selected
  objective separately from the conservative certificate. Progress telemetry
  exposes `selected_objective`, `certified_lower_bound`, and
  `regularized_objective`.
- Distributed final output now includes:
  - `final_objective[_raw]` for the final selected/original objective;
  - `best_selected_objective[_raw]` for the best selected/original objective
    diagnostic;
  - `best_certified_lower_bound[_raw]` for the conservative certificate;
  - `best_regularized_objective[_raw]` for the perturbed objective.
- Re-ran the balanced exact `adhead.n6c10` case with 4 workers, 10
  partitions, capacity multiplier `100`, and promotion to scale `1000`.
  Verified:
  - `final_objective 48373`;
  - `final_objective_raw 48373000`;
  - `best_selected_objective 48373`;
  - `best_selected_objective_raw 48373000`;
  - `best_certified_lower_bound 48372.9`;
  - `best_certified_lower_bound_raw 48372930`;
  - `best_regularized_objective 48373.1`;
  - `best_regularized_objective_raw 48373110`;
  - `final_disagreement_count 0`.
- Verified:
  - `cmake --build third_party/mcpd3/build -j`;
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure` with loopback permission;
  - exact balanced adhead local process benchmark.

## 2026-06-30 11:46 PDT

- Rebranded the product wrapper from `mcpd3-distributed` to `mcpd4` while
  keeping the solver dependency and API namespace as `mcpd3`.
- Renamed product CMake targets, public include path, protocol/runtime
  namespace, coordinator/worker binaries, temp-file prefixes, and docs to use
  `mcpd4`.
- Updated the local benchmark helper to prefer `MCPD4_*` environment variables
  while accepting legacy `MCPD3_*` aliases for existing local run scripts.

## 2026-06-30 11:57 PDT

- Expanded `README.md` into a concrete setup and distributed-run guide for a
  new agent or client machine.
- Documented clone/submodule setup, build/test commands, localhost helper
  runs, manual coordinator/worker runs, remote worker setup, coordinator and
  worker options, output status codes, exactness checks, capacity scaling, and
  troubleshooting.
- Verified:
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`;
  - README quick-start helper command on `tests/fixtures/hand_bottleneck.max`
    with two workers and four partitions.

## 2026-06-30 12:49 PDT

- Renamed the product remote/default branch from `network-free-worker-api` to
  `working`.
- Preserved internal project-management Markdown on `working`:
  `AGENT_HANDOFF.md`, `MVP_TRACKER.md`, `PROGRESS_LOG.md`,
  `FAILED_APPROACHES.md`, and `REGULARIZATION_EXACTNESS_PROOF.md`.
- Created remote `main` at the same product checkpoint as the working branch.
- Created cleanup branch `main-cleanup` for the user-facing main PR:
  - removed non-user-facing Markdown;
  - kept `README.md` as the single user-facing Markdown runbook;
  - removed README links to the internal Markdown files.
- Opened PR: `https://github.com/vvhitedog/mcpd4/pull/1`.
- Verified cleanup branch before opening PR:
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`;
  - Markdown scan showed only `README.md` outside ignored build/benchmark and
    submodule paths.

## 2026-06-30 14:38 PDT

- Corrected the GitHub default branch to `main`.
- Updated local `origin/HEAD` to point at `origin/main`.
- Fast-forwarded local `main` to track `origin/main`.
- Confirmed PR `https://github.com/vvhitedog/mcpd4/pull/1` is merged as
  `682da28 Clean user-facing documentation set (#1)`.
- Confirmed clean `origin/main` has only `README.md` as Markdown outside
  ignored build/benchmark and submodule paths.

## 2026-06-30 14:43 PDT

- Updated the mcpd3 productized branch `partition-worker-api` with a
  user-facing README explaining:
  - standalone min-cut/max-flow use;
  - the dual-decomposition approach;
  - the partition-worker API;
  - how mcpd3 is used independently and as the solver core for mcpd4.
- Pushed mcpd3 commit `8328d73 Document productized solver usage`.
- Opened mcpd3 PR: `https://github.com/vvhitedog/mcpd3/pull/1`.
- Verified in `third_party/mcpd3`:
  - `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`;
  - `./build/simple_example`;
  - `./build/dimacs_example ../../tests/fixtures/hand_bottleneck.max`;
  - `./build/dimacs_dual_decomp_example ../../tests/fixtures/hand_bottleneck.max --partitions 2 --max-iterations 100 --threads 2 --capacity-multiplier 10000 --disable-primal-upper-bound --quiet`.

## 2026-06-30 16:13 PDT

- Ran the first two-machine LAN `adhead.n6c10` trial with the coordinator on
  `192.168.1.87` and one remote worker `wifi-worker-1` on `192.168.1.175`.
- Confirmed the distributed TCP path solved across machines:
  - `final_objective 48373`;
  - `final_disagreement_count 0`;
  - `objective_scale 1000`;
  - `objective_scale_promotions 1`;
  - `partition_solve_call_count_total 1820`.
- Captured the post-trial follow-up plan in `MVP_TRACKER.md`, covering
  queryable coordinator/worker status, terminology cleanup, RPC transfer
  telemetry, coordinator-host worker participation, worker wait policy, and
  load-balancing improvements.

## 2026-06-30 16:34 PDT

- Implemented first-pass coordinator discovery tooling:
  - coordinator `--discovery-port PORT` and `--discovery-token TOKEN` start a
    UDP discovery listener while waiting for workers;
  - workers can use `mcpd4_worker --discover` with discovery host/port/token
    instead of explicit TCP host/port;
  - new `mcpd4_discovery list` command lists visible waiting coordinators;
  - new `mcpd4_discovery close` command tells the coordinator to stop
    discovery and proceed once the minimum `--workers` count is connected.
- Added process-level TDD coverage for discovery mode: list coordinator,
  connect a discovered worker, close discovery, solve `hand_bottleneck.max`,
  and compare against the in-process reference.
- Updated `README.md` with the discovery-mode LAN workflow and CLI options.

## 2026-06-30 17:06 PDT

- Implemented first-pass queryable status tooling:
  - coordinator and worker support `--status-port PORT` and
    `--status-token TOKEN`;
  - new `mcpd4_status HOST PORT` command queries a UDP status endpoint;
  - coordinator status reports phase, TCP/discovery/status ports, accepted
    workers, worker names, partition count, objective scale, latest progress
    fields, disagreement, and aggregate solve/RPC counts;
  - worker status reports phase, worker name, coordinator endpoint, loaded
    partitions, current round/partitions, solve counts, batch RPC count, worker
    solve wall time, and last error.
- Extended the process integration test to query coordinator and worker status
  while discovery mode is open and a discovered worker is connected, then close
  discovery and solve the fixture against the in-process reference.
- Verified:
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`.

## 2026-06-30 17:59 PDT

- Cleaned up product terminology:
  - new coordinator flags are `--objective-scale`, `--schedule-start`, and
    `--schedule-levels`;
  - old `--capacity-multiplier`, `--initial-step`, and `--num-scales` flags
    remain compatibility aliases;
  - progress output now uses `schedule_scale`, `schedule_step`, and
    `effective_schedule_step` instead of overloaded `scale`/`step_size`;
  - objective-scale saturation output now uses `objective_scale_*` field names.
- Improved queryable status snapshots:
  - coordinator status now reports worker resources, partition ownership,
    regularization diagnostics, per-worker solve/RPC counts, and per-worker
    solve timing;
  - worker status now reports CPU/RAM, temp path, loaded partitions, active
    round/partition ids, solve counts, batch RPC count, and solve time.
- Updated the local benchmark helper and README to prefer the new terminology
  while keeping legacy aliases for existing scripts.

## 2026-06-30 21:43 PDT

- Added first-pass RPC byte telemetry for the TCP runtime:
  - coordinator-side `TcpPartitionWorker` now records encoded frame bytes for
    worker `HELLO`, partition packages, solve requests, solve results,
    objective-scale messages, ready/error/stop control traffic, and totals;
  - worker status now records encoded bytes sent/received by the same major
    traffic classes;
  - coordinator progress, final output, and UDP status snapshots now include
    `rpc_tx_bytes_total`, `rpc_rx_bytes_total`,
    `rpc_partition_load_tx_bytes`, `rpc_solve_request_tx_bytes`,
    `rpc_solve_result_rx_bytes`, and related control counters;
  - worker UDP status snapshots now include `rpc_rx_bytes_total`,
    `rpc_tx_bytes_total`, partition-load receive bytes, solve-request receive
    bytes, solve-result transmit bytes, ready transmit bytes, stop receive
    bytes, and error transmit bytes.
- Added TDD coverage:
  - TCP loopback tests assert runtime byte counters are populated for hello,
    partition load, solve request/result, batch request/result, and objective
    scaling;
  - process integration tests assert progress/final output and live
    coordinator/worker status expose RPC byte counters.
- Local fixture measurement:
  - command:
    `MCPD4_WORKERS=2 MCPD4_PARTITIONS=2 MCPD4_MAX_ITERATIONS=20 MCPD4_SCHEDULE_LEVELS=1 MCPD4_OBJECTIVE_SCALE=10000 MCPD4_PROGRESS_EVERY=1 scripts/run_local_process_benchmark.sh tests/fixtures/hand_bottleneck.max`;
  - final counters: `rpc_tx_bytes_total=1234`,
    `rpc_rx_bytes_total=1544`, `rpc_partition_load_tx_bytes=250`,
    `rpc_solve_request_tx_bytes=904`, `rpc_solve_result_rx_bytes=1344`,
    `rpc_stop_tx_bytes=80`;
  - even on the tiny fixture, repeated solve-result traffic dominates setup
    traffic.
- Next transport optimization target: compact solve-result boundary labels.
  The coordinator currently only uses `constraint_id` and `label` from each
  repeated constrained label; `global_node_id` and `local_index` are already
  known from partition setup, so dropping them from solve-result frames should
  reduce repeated label payloads before considering compression.
- Verified:
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`;
  - local benchmark command listed above.

## 2026-06-30 21:48 PDT

- Implemented the first transport payload reduction:
  - `SOLVE_ROUND_RESULT` and `SOLVE_ROUND_BATCH_RESULT` now encode each
    repeated constrained label as `constraint_id` plus `label` only;
  - `global_node_id` and `local_index` remain in the one-time partition
    packages and are left unset on decoded result labels;
  - bumped the mcpd4 worker protocol version to `3` so old/new binaries fail
    the handshake instead of silently disagreeing on result-frame layout.
- Added protocol serialization coverage:
  - single solve-result frames with two labels are now 92 bytes;
  - batch solve-result frames with two one-label results are now 152 bytes;
  - decoded compact labels preserve `constraint_id` and `label` and omit
    setup-only endpoint metadata.
- Re-ran the same local fixture benchmark:
  - before compact labels: `rpc_solve_result_rx_bytes=1344`,
    `rpc_rx_bytes_total=1544`;
  - after compact labels: `rpc_solve_result_rx_bytes=1232`,
    `rpc_rx_bytes_total=1432`;
  - saved 112 bytes on the tiny run, matching 8 bytes saved for each of 14
    partition-solve result labels.
- Verified:
  - `cmake --build build -j`;
  - `./build/protocol_serialization_test`;
  - `./build/process_integration_test ./build/mcpd4_coordinator ./build/mcpd4_worker ./build/mcpd4_discovery ./build/mcpd4_status tests/fixtures`;
  - `ctest --test-dir build --output-on-failure`;
  - local benchmark command listed in the prior entry.

## 2026-06-30 22:27 PDT

- Implemented compact per-round alpha updates:
  - `SOLVE_ROUND_REQUEST`, `SOLVE_ROUND_BATCH_REQUEST`, and standalone
    `ALPHA_UPDATE` frames now encode each alpha update as `constraint_id` plus
    current `alpha` only;
  - `last_alpha` is now worker-local state: workers set it from their
    persisted previous alpha when an update arrives, then catch it up to
    `alpha` after the local solve completes;
  - `alpha_momentum` remains coordinator-owned and is no longer transmitted to
    workers;
  - bumped the mcpd4 worker protocol version to `4`.
- Added test coverage:
  - protocol serialization asserts compact 12-byte alpha update records in
    single solve requests, batched solve requests, and standalone alpha update
    messages;
  - partition-worker smoke coverage sends bogus `last_alpha` and
    `alpha_momentum` metadata and verifies the worker result matches the
    result from correct metadata.
- Re-ran the same local fixture benchmark:
  - final objective stayed `4`, final raw objective stayed `40000`, and
    `final_disagreement_count` stayed `0`;
  - after compact result labels but before compact alpha updates:
    `rpc_solve_request_tx_bytes=904`;
  - after compact alpha updates: `rpc_solve_request_tx_bytes=760`;
  - for `adhead`, the full-sync estimate drops from about `31.5 MB` of alpha
    updates per iteration to about `15.7 MB`, saving roughly `15.7 MB` per
    full dirty sync iteration.
- Verified:
  - `cmake --build build -j`;
  - `./build/protocol_serialization_test`;
  - `./build/distributed_partition_worker_smoke_test`;
  - `ctest --test-dir build --output-on-failure`;
  - local benchmark command listed in the 21:43 entry.

## 2026-07-01 22:44 PDT

- Added a clean native mcpd3 monolith benchmark in the solver submodule:
  - target: `mcpd3_native_monolith_benchmark`;
  - source: `third_party/mcpd3/benchmark/native_monolith.cpp`;
  - it constructs `mcpd3::DualDecomposition` directly and does not use
    mcpd4 RPC, worker coordination, or partition packages by default.
- Added `DualDecompositionOptions::emit_partition_packages`, defaulting to
  `true` so distributed/package-export behavior remains unchanged. The native
  benchmark sets it to `false` to avoid copying every local subproblem into
  `PartitionPackage` data.
- Added regression coverage:
  - `disabledPartitionPackageExportPreservesNativeSolve()` verifies package
    access throws when export is disabled;
  - the same tiny decomposition with package export on/off produces matching
    lower-bound and disagreement results after a local optimization round.
- Cleaned native benchmark stdout:
  - benchmark output is unbuffered key/value text;
  - old solver first-iteration timing output now requires
    `MCPD3_SOLVER_TIMING`;
  - dual-decomposition partition constraint-count prints are now gated by
    `DualDecompositionOptions::verbose`.
- Smoke result:
  - command:
    `MCPD3_PARTITIONER=basic build/mcpd3-native/mcpd3_native_monolith_benchmark tests/fixtures/random_small.max --directed --partitions 2 --objective-scale 10 --schedule-start 10 --schedule-levels 2 --max-iterations 20 --threads 1 --regularization none`;
  - status `agreement`, final objective raw `90`, total iterations `7`.
- Attempted a native adhead baseline:
  - command:
    `MCPD3_PARTITIONER=basic build/mcpd3-native/mcpd3_native_monolith_benchmark data/maxflow/adhead.n6c10/adhead.n6c10.max --directed --partitions 10 --objective-scale 1000 --schedule-start 10000 --schedule-levels 5 --max-iterations 10000`;
  - terminated manually after `552.78s` without a final result;
  - process reached roughly `8.1 GB` RSS and effectively used about one core,
    suggesting direct native solve/scheduling needs investigation before this
    is a fair best-local comparator on adhead.
- Verified:
  - `cmake -S third_party/mcpd3 -B build/mcpd3-native`;
  - `cmake --build build/mcpd3-native -j`;
  - `ctest --test-dir build/mcpd3-native --output-on-failure`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`;
  - `cmake --build build/no-snappy -j`;
  - `ctest --test-dir build/no-snappy --output-on-failure`.

## 2026-07-01 23:39 PDT

- Hardened opt-in capacity truncation/saturation so it applies to later
  objective-scale promotions, not only the initial graph scaling step:
  - `DualDecompositionOptions` and `PartitionWorkerCoordinatorOptions` now
    carry `saturate_capacity_overflow`;
  - native `DualDecomposition::scaleProblem()` and
    `PrimalDualMinCutSolver::scaleProblem()` clamp promoted int capacities
    when the option is enabled;
  - `PartitionWorker::scaleObjective()` now receives the saturation flag, and
    `InProcessPartitionWorker` passes it through to loaded solvers;
  - mcpd4 `SCALE_OBJECTIVE` frames now encode the saturation flag and the
    worker protocol version is bumped to `6`;
  - `mcpd4_coordinator`, `mcpd4_inprocess_benchmark`, and the native mcpd3
    benchmark pass `--saturate-capacity-overflow` /
    `--truncate-capacity-overflow` through to promotion scaling.
- Added regression coverage:
  - scripted coordinator promotion forwards the saturation flag to workers;
  - strict in-process worker promotion rejects int overflow while saturated
    promotion accepts it;
  - protocol serialization round-trips the scale-objective saturation flag;
  - TCP remote worker promotion overflows in strict mode pre-fix, and now
    succeeds when the transmitted saturation flag is true.
- adhead n6c10 partition-count notes from the local productized path:
  - `p=10, objective_scale=1000`: exact success, objective `48373`, wall
    `161.39s`, total iterations `108`;
  - `p=8, objective_scale=2000`: exact success, objective `48373`, wall
    `139.74s`, total iterations `110`;
  - `p=16, objective_scale=2000`: exact success, objective `48373`, wall
    `206.27s`, total iterations `130`;
  - `p=16, objective_scale=1000, --truncate-capacity-overflow`: promoted to
    objective scale `10000`, agreement, objective `48373`, wall `285.24s`.
    This verifies truncation is respected through promotion, but it is a
    clipped-capacity compatibility run, not the best exact local baseline.
- Verified:
  - `cmake --build build/mcpd3-native -j`;
  - `ctest --test-dir build/mcpd3-native --output-on-failure`;
  - `cmake --build build -j`;
  - `./build/protocol_serialization_test`;
  - `./build/tcp_loopback_test`;
  - `ctest --test-dir build --output-on-failure`;
  - `cmake --build build/no-snappy -j`;
  - `ctest --test-dir build/no-snappy --output-on-failure`.

## 2026-07-02 00:17 PDT

- Added large-graph phase instrumentation to `mcpd4_inprocess_benchmark`:
  - unbuffered key/value output;
  - memory snapshots after read, scale, partition, setup, and solve;
  - `--stop-after read|scale|partition|setup` for safe out-of-core probes;
  - graph/package payload summaries, including boundary constraint endpoint
    counts.
- Reduced package/setup duplication for distributed export:
  - `DualDecompositionOptions::construct_solvers` allows package-only export;
  - mcpd4 package generation now disables native solver construction and moves
    package payloads instead of copying subgraph vectors;
  - `PartitionWorker::loadPartition(PartitionPackage&&)` lets in-process
    workers move package vectors into solver state;
  - `InProcessPartitionWorker` no longer retains arcs/capacity/local mapping
    payloads after loading a solver; it keeps only partition id and boundary
    endpoint metadata.
- Added mcpd3 regression coverage:
  - package-only export matches solver-backed package export on a tiny
    decomposition;
  - package-only `DualDecomposition::solve()` rejects misuse because no native
    solvers exist.
- Large adhead probe target:
  - DIMACS: `data/maxflow/adhead.n26c100/adhead.n26c100.max`;
  - solution file reports optimum `734905`;
  - directed reader reports `12,582,912` nodes and `327,155,712` graph arcs.
- Large adhead phase probes, objective scale `2000`, directed, basic
  partitioner:
  - read-only: wall `30.07s`, settled RSS `5.16 GB`, `/usr/bin/time` max RSS
    `7.72 GB`;
  - scale-only: scale pass `1.04s`, no material RSS increase;
  - p32 partition-only after package-only export: wall `45.88s`, partition
    phase `14.54s`, package endpoint count `4,194,304`, package int payload
    `5,351,931,904` bytes, package endpoint payload `167,772,160` bytes, max
    RSS `12.29 GB`;
  - p32 setup-only with BK `file_mmap`: completed, wall `158.21s`, setup
    phase `112.27s`, final setup RSS `13.99 GB`, max RSS `14.16 GB`;
  - p24 partition-only: wall `44.82s`, endpoint count `3,145,728`, max RSS
    `11.78 GB`;
  - p24 setup-only with BK `file_mmap`: manually terminated at disk limit
    after `136.04s`; max RSS `14.21 GB`;
  - p40 partition-only: wall `45.61s`, endpoint count `5,275,972`, max RSS
    `12.75 GB`;
  - p48 partition-only: wall `46.39s`, endpoint count `6,291,456`, max RSS
    `13.21 GB`.
- Current interpretation:
  - package-only export fixes the earlier p32 partition memory blow-up;
  - p24 reduces boundary/package overhead but stresses BK mmap disk during
    setup more than p32 on this laptop;
  - p40/p48 add boundary overhead without improving partition construction;
  - p32 is the best first large-adhead full-solve candidate so far, but all
    local setup is near RAM/disk limits and should preferably be split across
    machines.
- Verified:
  - `cmake --build build/mcpd3-native -j`;
  - `ctest --test-dir build/mcpd3-native --output-on-failure`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`;
  - `cmake --build build/no-snappy -j`;
  - `ctest --test-dir build/no-snappy --output-on-failure`.

## 2026-07-02 00:33 PDT

- Added explicit BK and loaded-solver footprint estimates:
  - BK `Graph` now exposes estimated node-array, arc-array, and total storage
    bytes using the actual private `node`/`arc` struct sizes and constructor
    minimum capacities;
  - `PrimalDualMinCutSolver::estimateMemoryBytes()` reports BK node bytes, BK
    arc bytes, BK total bytes, solver vector bytes, and combined lower-bound
    loaded-solver bytes;
  - `mcpd4_inprocess_benchmark` now prints one `partition_footprint` line per
    package plus aggregate max/p95/mean totals and worst-case top-K active
    streaming windows.
- Important interpretation correction:
  - previous p40/p48 comments were about the current all-loaded local setup;
  - for a streamed out-of-core worker, higher partition counts reduce the max
    live BK graph size and the top-K active BK working set, at the cost of more
    boundary/package overhead.
- Large adhead BK footprint sweep:
  - output directory:
    `benchmark_results/large_adhead_bk_footprint_20260702_002920`;
  - command shape:
    `MCPD3_PARTITIONER=basic build/mcpd4_inprocess_benchmark data/maxflow/adhead.n26c100/adhead.n26c100.max --directed --workers 1 --partitions P --objective-scale 2000 --schedule-start 10000 --schedule-levels 5 --max-iterations 10000 --stop-after partition`.
- Streaming-relevant results:
  - p24: max BK partition `979,369,984` bytes, p95 BK `900,726,784`, worst
    4-active BK `3,681,550,336`, worst 8-active BK `7,284,457,472`, endpoints
    `3,145,728`;
  - p32: max BK partition `754,974,720` bytes, p95 BK `676,331,520`, worst
    4-active BK `2,783,969,280`, worst 8-active BK `5,489,295,360`, endpoints
    `4,194,304`;
  - p40: max BK partition `620,461,008` bytes, p95 BK `541,816,912`, worst
    4-active BK `2,245,911,744`, worst 8-active BK `4,413,174,016`, endpoints
    `5,275,972`;
  - p48: max BK partition `530,579,456` bytes, p95 BK `451,936,256`, worst
    4-active BK `1,886,388,224`, worst 8-active BK `3,694,133,248`, endpoints
    `6,291,456`.
- Loaded-solver lower-bound max estimates, including BK arrays, solver vectors,
  and endpoint metadata but not STL/list/unordered-map allocator overhead:
  - p24: `1,288,699,904` bytes;
  - p32: `994,574,336` bytes;
  - p40: `818,280,036` bytes;
  - p48: `700,448,768` bytes.
- Verified:
  - `cmake --build build/mcpd3-native -j`;
  - `ctest --test-dir build/mcpd3-native --output-on-failure`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`;
  - `cmake --build build/no-snappy -j`;
  - `ctest --test-dir build/no-snappy --output-on-failure`.
