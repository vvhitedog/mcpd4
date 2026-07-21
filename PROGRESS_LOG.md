# Progress Log

## 2026-07-17 18:46 PDT - Promoted schedule parity

- Advanced the pinned mcpd3 dependency with the final-unit-scale promotion
  invariant used by native DD: package/coordinator solves now promote on
  exhausted unit-scale disagreement and recompute enough schedule levels to
  return to unit scale after every promotion.
- This removes the previous transport-path divergence where mcpd4 could stop
  or restart above unit scale while the corrected local solver continued.
- The complete five-test suite passes in the default, 64-bit, 128-bit, and GMP
  builds after the submodule update.

## 2026-07-13 22:00 PDT - Widened DD state transport

- Advanced mcpd3 to keep compact source capacities and arc residuals while
  widening node balances, terminal residuals, and Lagrange multipliers.
- Updated stateless partition packages, solve requests, and temporal delta
  state to encode/decode alpha in the widened `Lagrange` domain.
- Added a regression that sends alpha values twice the configured source
  capacity maximum through initial package sync, stateless requests, temporal
  initial sync, and sign-changing delta reconstruction.
- Bumped the mcpd4 transport protocol to version 9 so mixed old/new binaries
  fail at handshake instead of failing later on a widened alpha value.
- Passed the full five-test 32-bit transport/integration suite and the widened
  protocol test in 64-bit, 128-bit, and GMP modes.

## 2026-07-13 15:47 PDT - Ratio-aware mcpd3 flow dependency

- Advanced the mcpd3 submodule to `2b52bc0`, which adds checked rational
  scaling for persistent local flow across proportional capacity changes.
- The mcpd3 32/64/128/GMP test matrix passes at this revision (8/8 tests).
- Rebuilt every mcpd4 target and passed all four transport/integration tests
  in 32/64/128/GMP modes (16/16 tests).
- This is a shared solver capability only. The current distributed PU adapter
  does not retain quantum workspaces across calls, so mcpd4 transport and
  runtime behavior are unchanged by this pointer update.

## 2026-07-12 15:09 PDT

- Added native mcpd3 reference-guided exact local mincut selection for the
  phase-unwrapping preconditioner experiment. The full mode minimizes Hamming
  distance over the residual mincut lattice; the cheap mode uses the exact
  reference only when it is itself a current local optimum.
- Added configurable reference-check cadence and aggregate counters for checks,
  direct hits, closure solves, and decoder time. The feature remains native
  mcpd3-only and does not change mcpd4 transport or worker behavior.
- Exhaustive random graph tests preserve local mincut objectives. On 64x64 p2
  phase fixtures, the cheap oracle mode every five local solves reduced
  conditioned native-DD wall time by 5.9-8.0% across three seeds.

## 2026-07-12 03:24 PDT

- Prototyped and tested native-only colored Gauss-Seidel DD scheduling in the
  mcpd3 submodule. Partition-pair constraint blocks were edge-colored into
  conflict-free matching phases; each phase updated alphas and immediately
  re-solved both endpoint partitions.
- The 64x64 p2 PU benchmark was effectively unchanged, while corrected p4 runs
  failed to reach agreement under both plain and momentum alpha updates. The
  implementation was removed completely; only this result and its taboo remain.

## 2026-07-11 19:18:28 PDT

- Optimized the shared mcpd3 `PartitionWorkerCoordinator`, used by mcpd4:
  - replaced per-round worker `std::async` fanout with a persistent
    `ThreadPool<void>`;
  - added explicit task exception capture/rethrow so worker failures are still
    reported at the round that triggered them;
  - cached static package-to-worker grouping and compact-ID lookup tables for
    worker, package, and constraint IDs.
- Verified no state drift:
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `ctest --test-dir build --output-on-failure`;
  - phase repo `ctest --test-dir build-mcpd4 --output-on-failure`.
- Phase 64x64 seed-1 diagnosis:
  - direct phase `mcpd3` backend is a whole-graph mincut backend, not native
    partitioned DD;
  - new phase `mcpd3-n` backend showed native partitioned DD at `14323.5 ms`;
  - mcpd4 after package/pool fixes was `21078.8 ms`;
  - all rows had objective `9383`, 22 optimizer iterations, and `optimal=1`.
- Interpretation: no evidence of mcpd4 algorithm/state divergence here. The
  remaining small-problem gap is distributed worker request/result and boundary
  label bookkeeping overhead.

## 2026-07-11 18:52 PDT

- Continued the `mcpd3-n` versus mcpd4 runtime diagnosis using only local
  native DD and coordinated DD comparators.
- Added a stronger mcpd3 submodule parity test:
  - `partitionWorkerCoordinatorMatchesDualDecompositionRegularizedRounds`;
  - drives a multi-partition fixture through native
    `mcpd3::DualDecomposition` and `PartitionWorkerCoordinator` one round at a
    time;
  - compares raw objective, certified lower bound, regularized objective,
    disagreement count/norm, regularization budget/contribution, full local
    labels, and every constraint's `alpha`, `last_alpha`, and
    `alpha_momentum`;
  - exercises both momentum off/on and step sizes above and inside the
    scaled-epsilon regularization window (`1000`, `100`, `10`, `1`).
- UB checks:
  - native DD Valgrind on `tests/fixtures/random_small.max`: `0` errors,
    all heap blocks freed;
  - mcpd4 in-process coordinator Valgrind on the same fixture: `0` errors,
    all heap blocks freed;
  - process/TCP coordinator plus two worker processes under Valgrind on the
    same fixture: coordinator and both workers reported `0` errors and all
    heap blocks freed.
- Aligned adhead.n6c10 p10 objective-scale-1000 timings:
  - native DD:
    `benchmark_results/adhead-mcpd3-native-dd-p10-os1000-exhaustreg-20260711-183532.out`,
    wall `123.00s`, total `122.470s`, construct `31.226s`, solve
    `83.768s`, inner partition-solve `82.186s`;
  - mcpd4 in-process:
    `benchmark_results/adhead-mcpd4-inprocess-w1-p10-os1000-exhaustreg-20260711-183752.out`,
    wall `126.66s`, total `126.090s`, partition+setup `32.311s`, solve
    `86.887s`;
  - mcpd4 local TCP, two worker processes:
    `benchmark_results/adhead-mcpd4-localtcp-w2-p10-os1000-20260711-184853`,
    wall `149.83s`, total `149.730s`, partition+setup `45.087s`, solve
    `97.191s`.
- All three aligned adhead runs matched algorithmic state:
  - certified lower bound raw `48372380`;
  - regularized objective raw `48373110`;
  - total DD iterations `108`;
  - final disagreement count `0`;
  - regularization budget/contribution `730/110`;
  - objective-scale promotions `0`.
- Local TCP overhead evidence:
  - partition package load transmitted `1,357,119,888` logical bytes over
    loopback and took `41.561s` aggregate load RPC wall time;
  - solve RPC sent `23.046 MB` of requests and received `24.878 MB` of
    results;
  - aggregate worker-reported solve time was `142.415s` across the two
    workers, with critical worker solve time `85.068s`;
  - aggregate solve RPC overhead was `8.346s`;
  - worker assignment was balanced by count but not by solve cost:
    worker 1 partitions `0,2,6,8,9` solved in `57.346s`, worker 2 partitions
    `1,3,4,5,7` solved in `85.068s`.
- Current attribution:
  - no evidence of alpha/regularization algorithm drift in the local-DD versus
    coordinator core;
  - no Valgrind evidence of UB on small native, in-process coordinator, or
    process/TCP runs;
  - the remaining adhead p10 gap is measured coordinator/worker execution
    overhead: package serialization/load/setup, per-round TCP RPC overhead,
    and static load imbalance.

## 2026-07-11 18:40 PDT

- Corrected the native mcpd3 comparator terminology: `mcpd3-n` refers to the
  native `mcpd3::DualDecomposition` benchmark, not the phase direct-cut backend
  and not the mcpd4 in-process worker/coordinator benchmark.
- Added native benchmark flags for low-scale exhaustion so native DD can be run
  with the same schedule defaults as mcpd4:
  - `--exhaust-scale-iterations`;
  - `--exhaust-regularized-scale-iterations`;
  - `--no-exhaust-regularized-scale-iterations`.
- Ran an aligned adhead.n6c10 p10 objective-scale-1000 benchmark:
  - native DD Release:
    `benchmark_results/adhead-mcpd3-native-dd-p10-os1000-exhaustreg-20260711-183532.out`;
    wall `123.00s`, total `122.470s`, solve `83.768s`, inner solve
    `82.186s`, certified raw `48372380`, regularized raw `48373110`,
    iterations `108`, disagreements `0`;
  - mcpd4 in-process Release:
    `benchmark_results/adhead-mcpd4-inprocess-w1-p10-os1000-exhaustreg-20260711-183752.out`;
    wall `126.66s`, total `126.090s`, solve `86.887s`, certified raw
    `48372380`, regularized raw `48373110`, iterations `108`,
    disagreements `0`.
- Interpretation: after aligning build type and schedule, mcpd4 in-process is
  about `3.6s` slower than native DD on this run, not the previously suspected
  large gap. The earlier native output was an empty killed run and should not be
  used as a completed baseline.

## 2026-07-03 02:51 PDT

- Completed a full distributed large-adhead resident run across this laptop plus
  the remote worker:
  - run directory:
    `benchmark_results/large_adhead_distributed_p32_resident_20260703_022001_restart_20260703_023441`;
  - DIMACS: `data/maxflow/adhead.n26c100/adhead.n26c100.max`;
  - configuration: directed, `32` partitions, `2` workers, objective scale
    `1000`, schedule start `1000`, schedule levels `4`, Snappy RPC,
    saturated overflow mode;
  - worker assignment: local worker `15` partitions, remote worker `17`
    partitions;
  - final objective: `734905`, matching the known optimum;
  - final certified lower bound: `734905`;
  - total iterations: `550`;
  - final disagreement count: `0`;
  - regularization cleanup occurred at schedule scale `10` with budget `120`,
    contribution `120`, and `12` active sink-side anchors;
  - total coordinator wall time: `985.08s` (`16.42 min`);
  - solve segment wall time: `498.45s` (`8.31 min`);
  - one-time partition load RPC wall time: `187.27s`;
  - logical partition package bytes: `5.49 GB`; compressed wire bytes total:
    `2.06 GB`;
  - solve request bytes: `93.90 MB`; solve result bytes: `125.53 MB`;
  - aggregate worker solve time: `532.22s`; aggregate solve RPC overhead:
    `186.87s`.
- Important observations:
  - resident distributed solving avoided the minute-scale per-iteration behavior
    seen in local streaming probes;
  - transport was not dominant after setup, although remote worker batches were
    slower and owned more partitions;
  - the disk-backed remote BK mmap path fixed the earlier partition-6 worker
    death.

## 2026-07-03 02:28 PDT

- Added worker-side validation that BK `file_mmap` directories are not on
  memory-backed filesystems (`tmpfs`, `ramfs`, `hugetlbfs`). This prevents a
  worker from silently placing BK mmap arrays in RAM when a path such as `/tmp`
  is memory-backed on that machine.
- Added process integration coverage using `/dev/shm` when available: a worker
  configured with `--bk-storage file_mmap --bk-mmap-dir /dev/shm/...` must fail
  before connecting and report a memory-backed filesystem error.
- The p32 large-adhead distributed restart reached two workers and began
  loading partitions. The remote worker successfully loaded partitions `0`,
  `2`, and `4`, then disconnected during partition `6`; coordinator failed with
  `worker remote-worker failed loading partition 6 ... socket closed during
  read`. The leading suspicion is remote BK mmap directory disk exhaustion,
  especially if the remote worker used a small `/tmp`/tmpfs-backed path.
- Local `/tmp` on this laptop is ext4-backed, but remote `/tmp` may be tmpfs.
  The implicit worker-owned BK mmap directory was moved to `/var/tmp`, and
  future large distributed worker commands should still use an explicit
  verified disk-backed BK mmap directory, not `/tmp` by habit.

## 2026-07-03 02:08 PDT

- Changed mcpd4 worker BK storage default from heap-backed `malloc` to
  file-backed `file_mmap`:
  - when no `--bk-storage`/`MCPD3_BK_STORAGE` override is present, the worker
    now sets `MCPD3_BK_STORAGE=file_mmap`;
  - when no mmap directory is supplied, the worker creates an owned
    `/var/tmp/mcpd4-bk-mmap-<pid>` directory;
  - explicit `--bk-mmap-dir` directories are created if missing;
  - explicit `--bk-storage malloc` and existing `MCPD3_BK_*` env overrides
    remain supported.
- Updated process integration coverage so a worker with no BK flags must report
  `bk_storage file_mmap` and a default mmap directory in status.
- Updated README worker examples to pass `--bk-mmap-dir` explicitly for
  repeatable large-run behavior on a known filesystem.
- Verified:
  - `cmake --build build -j`;
  - `./build/process_integration_test ./build/mcpd4_coordinator ./build/mcpd4_worker ./build/mcpd4_discovery ./build/mcpd4_status tests/fixtures`;
  - `ctest --test-dir build --output-on-failure`;
  - `cmake --build build/no-snappy -j`;
  - `ctest --test-dir build/no-snappy --output-on-failure`.

## 2026-07-03 01:54 PDT

- Added durable coordinator status snapshots:
  - `mcpd4_coordinator --status-file PATH` atomically writes the latest status
    line to disk;
  - `mcpd4_status --file PATH` prints that snapshot after the coordinator has
    exited or failed;
  - fatal coordinator errors now record `phase error`, `last_error`, and print
    a compact `mcpd4_coordinator_status ...` line instead of dumping usage for
    runtime failures.
- Added partition-load diagnostics on both sides of the TCP runtime:
  - coordinator logs `mcpd4_load_partition_begin|done|failed` with worker name,
    partition id, local node count, arc/capacity/vector counts, endpoint count,
    logical frame bytes, wire bytes, and elapsed time;
  - worker logs `mcpd4_worker_load_partition_begin|done` and records live
    `current_load_*` fields in worker status while loading a partition.
- Added regression coverage:
  - coordinator-side load disconnect errors must include worker and partition
    context;
  - durable status files remain queryable with `mcpd4_status --file` after a
    coordinator failure.
- Verified:
  - `cmake --build build -j`;
  - `./build/tcp_loopback_test`;
  - `./build/process_integration_test ./build/mcpd4_coordinator ./build/mcpd4_worker ./build/mcpd4_discovery ./build/mcpd4_status tests/fixtures`;
  - `ctest --test-dir build --output-on-failure`;
  - `cmake --build build/no-snappy -j`;
  - `ctest --test-dir build/no-snappy --output-on-failure`.

## 2026-07-03 01:30 PDT

- Added first-class mcpd4 worker controls for mcpd3 BK graph storage:
  - `--bk-storage malloc|file_mmap|anon_mmap`;
  - `--bk-mmap-dir DIR`;
  - `--bk-mmap-advise ADVISE`.
- Worker CLI now preserves existing `MCPD3_BK_*` environment configuration
  when these flags are omitted, while explicit flags set the environment before
  any local solver/BK graph is constructed.
- Worker UDP status now reports `bk_storage`, `bk_mmap_dir`, and
  `bk_mmap_advise`, making distributed runs inspectable for mmap correctness.
- Process integration coverage now starts a worker with `file_mmap` BK storage
  and asserts status reports the mmap configuration.

## 2026-07-03 01:04 PDT

- Raised mcpd4's default TCP logical frame cap from 256 MiB to 1 GiB and
  centralized it as `mcpd4::kDefaultMaxFrameBytes`.
- Added sender-side frame-size enforcement so oversized partition packages
  fail before the coordinator writes a partial frame and resets the worker
  connection.
- Updated all runtime receive sites to use the shared frame cap instead of a
  hard-coded 256 MiB literal.
- Added TCP loopback coverage for:
  - receiver-side oversize rejection;
  - sender-side oversize rejection;
  - Snappy logical-frame oversize rejection;
  - the observed large adhead p16 package size fitting under the new default
    cap.

## 2026-07-02 22:17 PDT

- Added warm-state preservation for streaming partition eviction:
  - BK `Graph` now exposes pointer-safe reusable state using arc/node indices
    rather than raw pointers;
  - `PrimalDualMinCutSolver` can capture/restore primal-dual vectors, labels,
    cached multipliers, regularization anchors, mincut value, iteration flags,
    and BK residual/tree state;
  - `InProcessPartitionWorker` exposes solver warm-state capture/restore by
    partition id;
  - `StreamingPartitionWorker` writes warm state to disk when cache eviction
    drops a resident solver and restores it when the partition is reloaded.
- Objective-scale handling:
  - resident streaming solvers still scale in place;
  - evicted warm snapshots are invalidated on objective-scale promotion, then
    the worker falls back to persisted labels for correctness until a fresh
    resident solver is solved and evicted again.
- Added test coverage:
  - streaming eviction test now asserts a warm-state write on eviction and a
    warm-state restore on reload, while still matching the resident
    in-process worker result.
- Verified:
  - `cmake --build build/mcpd3-native -j`;
  - `ctest --test-dir build/mcpd3-native --output-on-failure`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`;
  - `cmake --build build/no-snappy -j`;
  - `ctest --test-dir build/no-snappy --output-on-failure`.

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

## 2026-07-11 19:39 PDT

- Added native/coordinator timing attribution for the `mcpd3-n` versus mcpd4
  performance investigation:
  - native `DualDecomposition` now reports total lagrange-update wall time
    alongside inner partition-solve wall time;
  - `PartitionWorkerCoordinator` now returns per-solve timing counters for
    round count, solve-partitions wall time, alpha-update preparation,
    request construction, worker dispatch, worker batch execution, round-term
    gathering, and constraint updates;
  - `mcpd4_inprocess_benchmark` prints these counters and accounted versus
    unaccounted solve wall time.
- Re-ran aligned `adhead.n6c10` p10 objective-scale-1000 benchmarks with the
  basic partitioner. Native DD and mcpd4 in-process both converged in 108
  iterations with zero disagreements, certified lower bound raw `48372380`,
  regularized objective raw `48373110`, regularization budget `730`, and
  regularization contribution `110`.
- Runtime attribution for that run:
  - native solve wall `81.593 s`, partition-solve wall `79.963 s`,
    lagrange-update wall `1.630 s`;
  - mcpd4 in-process solve wall `83.636 s`, solve-partitions wall `82.110 s`,
    alpha-update preparation `0.266 s`, constraint update `1.525 s`;
  - local TCP matched the same result but took `156.58 s` wall, with about
    `41.6 s` spent loading partitions over TCP and about `7.2 s` of solve-RPC
    overhead above worker solve time.
- Current conclusion: no algorithmic/state difference was observed between
  native DD and mcpd4 for these aligned runs; the remaining difference is
  worker/coordinator interface and transport overhead.
- Verified:
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `ctest --test-dir build --output-on-failure`.

## 2026-07-11 21:30 PDT

- Added the shared core enum/support for opt-in `cycle-epsilon`
  regularization, but left it unexposed in the mcpd4 coordinator CLI/parser for
  now. The active productized experiment is mcpd3 native DD only.
- Rebuilt and verified the distributed repo after disabling the mcpd4 public
  entry point:
  - `cmake --build build -j$(nproc)`;
  - `ctest --test-dir build --output-on-failure`.

## 2026-07-11 22:08 PDT

- Added mcpd3 native-DD-only support for an opt-in
  `disagreement-plateau-epsilon` regularization scheme. It activates
  configured epsilon regularization within a scale after the disagreement count
  has failed to decrease for `disagreement_patience` iterations, then resets
  the lower-bound patience clock so the regularized scale gets a full patience
  window.
- Tightened the paired-label alternation tracker so cycle recognition happens
  only at full windows (`5, 10, 15, ...` flips for threshold `5`) rather than
  every subsequent flip after the first threshold crossing.
- Kept the new plateau scheme out of the mcpd4 public CLI path; this remains a
  native DD experiment while phase benchmarks compare trigger behavior.
- Verified:
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure`;
  - `ctest --test-dir build --output-on-failure`.

## 2026-07-11 22:21 PDT

- Removed the experimental `cycle-epsilon` and
  `disagreement-plateau-epsilon` implementations, options, diagnostics, and
  tests from mcpd3/mcpd4 after the phase-unwrapping benchmarks showed they were
  dead ends. The validated original `scaled-epsilon` scheme remains unchanged
  and is again the sole epsilon-regularization implementation.
- Retained the historical experiment and failure notes so future work does not
  repeat these approaches. A more selective regularization design will be
  developed separately.
- Verified the cleanup with all mcpd3 and mcpd4 tests:
  - `ctest --test-dir third_party/mcpd3/build --output-on-failure` (`1/1`);
  - `ctest --test-dir build --output-on-failure` (`4/4`).

## 2026-07-11 22:57 PDT

- Added generic monotone flow/label/alpha warm-start snapshots to the mcpd3
  submodule for use by PU quantum continuation. The API contains no PU-specific
  objective logic and rejects topology changes, capacity decreases, malformed
  partition state, malformed constraint state, and internally regularized
  flows.
- Confirmed and regression-tested that a terminal on a duplicated boundary
  node is assigned exactly once to the node's home partition; other local
  clones receive zero terminal capacity plus their DD alpha terms.
- The phase library now owns dense-unary quantum scheduling and only passes
  ordinary constructed mincut graphs plus a generic objective-magnitude hint
  to mcpd3-native. mcpd4 support remains deferred until the native experiment
  is mature.

## 2026-07-12 23:34:36 PDT

- Fixed non-power-of-ten optimization schedules in both native
  `DualDecomposition` and `PartitionWorkerCoordinator`. A positive schedule
  now reaches a unit step before terminating; the default balanced start now
  runs `690 -> 69 -> 6 -> 1` instead of stopping after step 6.
- Added a coordinator regression that exercises every transition and verifies
  that the unit scale runs exactly once even when more scale slots are
  configured.
- Reproduced the issue with the 16x16 seed-1, radius-8 Ishikawa cut. The old
  schedule remained at 16 disagreements for 500,000 final-scale iterations.
  The fixed schedule reached agreement after 863 total DD iterations and
  returned bounded objective 576, matching BK. The mcpd4 path returned the
  same objective.
- Verified all mcpd3 (`1/1`), mcpd4 (`4/4`), and phase worktree (`7/7`) CTest
  targets.

## 2026-07-13 11:54 PDT - Build-time capacity precision in mcpd4

- Added shared `MCPD_CAPACITY_MODE=32|64|128|gmp` configuration from mcpd3.
- Bumped the RPC protocol to v7, advertised precision in worker `HELLO`, and
  reject coordinator/worker precision mismatches before partition transfer.
- Replaced fixed int32/int64 capacity and objective payloads with canonical
  signed arbitrary-width varints. Temporal alpha deltas use the widened
  objective type so extreme sign changes remain representable.
- Removed int-specific graph scaling and objective telemetry narrowing.
- Added configured-extreme stateless, temporal-delta, malformed-wire, and TCP
  remote-solve tests. GMP tests exercise values above 521 bits.
- Built all mcpd4 targets and passed all four CTest tests in 32-, 64-, 128-bit,
  and GMP modes.
- Corrected the user-facing MVP limits to describe the selectable precision
  modes instead of the obsolete fixed 32-bit capacity path.

## 2026-07-13 13:02 PDT - Checked mcpd3 backend integration

- Advanced the mcpd3 backend to the checked precision-arithmetic revision.
- Rebuilt every mcpd4 target and passed all four CTest tests in 32-, 64-,
  128-bit, and GMP modes: 16/16 mode/test combinations.
## 2026-07-13 19:26:12 PDT - Resident collective-PCG transport

- Added `TCP_NODELAY` to connected and accepted worker sockets, with loopback
  verification on both ends.
- Added a generic resident sparse linear partition using owned/ghost CSR rows,
  IC(0) preconditioning, and coordinator-driven PCG phases. Validation covers
  malformed ownership, CSR, finite values, SPD factorization, ghost sizes, and
  invalid state transitions.
- Added protocol-version-8 messages for one-time linear structure loading,
  repeatable numerical-system loading, initialize/multiply/alpha/beta phases,
  and owned-solution retrieval. Logical linear RPC bytes and per-operation
  counts are retained in worker telemetry.
- Added a two-worker TCP loopback test that recovers the known solution of a
  split SPD system in at most three iterations and verifies remote error
  recovery and telemetry.
- Corrected a collective-PCG edge case: a partition with zero local residual
  must continue participating when the global residual is nonzero. The
  regression starts one side locally exact and proves cross-boundary updates
  activate it and converge globally.

## 2026-07-13 23:40 PDT - MCPD3 historical replay integration

- Advanced the MCPD3 submodule to the revision that adds an opt-in,
  deterministic 32-bit historical DD replay policy. Normal MCPD4 builds do not
  enable that policy and retain checked, widened production arithmetic.
- Rebuilt all MCPD4 targets against the updated submodule and passed all five
  CTest targets, including TCP loopback and process integration.

## 2026-07-14 12:05 PDT - Cumulative scaled-epsilon backend

- Advanced the mcpd3 backend to persistent per-boundary cumulative epsilon
  weights. Continued disagreement now forces monotone budget growth until
  agreement or strict-budget promotion; objective promotion keeps this
  secondary regularizer unscaled.
- Worker warm-state version 4 serializes cumulative weights and remains able to
  read version 3 binary-anchor state. The distributed protocol is unchanged
  because regularization state remains resident inside each worker.
- Rebuilt mcpd4 and passed all five CTest targets in 32-, 64-, 128-bit, and GMP
  modes: 20/20 mode/test combinations, including TCP loopback and process
  integration.

## 2026-07-14 13:12 PDT - Plateau scheduler backend revision

- Advanced the mcpd3 submodule to the opt-in native disagreement-plateau
  scheduler. MCPD4 behavior and protocol remain unchanged because the new mode
  is not yet exposed through the distributed coordinator.
- Rebuilt mcpd4 and passed all five CTest targets in both 32- and 64-bit modes,
  including TCP loopback and process integration.

## 2026-07-15 - Allocation-free worker backend revision

- Advanced the mcpd3 submodule to allocation-free incremental cut and flow
  maintenance. The worker protocol and resident partition mapping are
  unchanged; each local solver now uses dense generation marks and reusable
  change buffers.
- Rebuilding exposed a pre-existing intermittent TCP saturation failure. The
  mcpd3 solver objective was uninitialized before its first solve, although
  objective promotion can occur in that state. A deterministic poisoned-memory
  regression now covers the initialized-zero invariant.
- The fixed TCP loopback test passed 100 consecutive runs. The complete mcpd4
  Release suite passes 5/5, including process integration.

## 2026-07-20 20:17 PDT - Persistent MCPD3-N algorithm parity over TCP

- Advanced the MCPD3 backend to the shared native/distributed scheduling and
  regularization policy, complete file-backed worker state, and exact
  flow-preserving capacity refresh API.
- Extended protocol version 10 with all behavior-affecting package policy and
  `REPLACE_PARTITION_CAPACITIES`. The new request carries capacities, flow
  preservation, and an exact rational flow scale; malformed and truncated
  branches are covered.
- Wired `--bk-storage file_mmap` through the entire worker solver state rather
  than only BK internals. A remote TCP test verifies the live unlinked backing
  mappings through `/proc/self/fd` and compares the exact result.
- Added live TCP tests for capacity replacement with and without warm-flow
  preservation and temporal-state reset. The full MCPD4 suite passes 5/5,
  including protocol, loopback, and process integration tests.

## 2026-07-20 21:35 PDT - Product-policy and branch-level parity

- Added one shared MCPD4 solver-policy adapter for package generation,
  in-process workers, and TCP coordinator execution. The validated product
  defaults are now `objective_scale=500`, `initial_step=5000`, five scales,
  patience 10, momentum enabled, group stopping disabled, and scaled-epsilon
  cutoff/cap 12/2.
- Forwarded every common schedule, regularization, promotion, retry,
  randomized-alpha, halo, canonical-cut, and overflow option instead of the
  former six-field subset. Both standalone front ends now perform the same
  preserved-state exhaustive-regularization retry as MCPD3-N.
- Package construction now materializes isolated nodes and carries canonical
  local-solve behavior, so worker execution sees the same local problems as
  native execution.
- Added policy translation tests covering all regularization branches and
  invalid domains. Added end-to-end native/coordinator trajectory checks that
  exercise unregularized random starts, scaled epsilon, plateau activation,
  group/exhaustion policy, and objective-scale promotion.
- Aligned progress best-bound semantics across schedule levels and corrected
  native plateau telemetry to report the regularization actually used in the
  completed round. MCPD4 passes 6/6 CTest targets; the Phase integration passes
  58/58 tests.

## 2026-07-20 23:04 PDT - Backing-preserving package handoff

- Advanced MCPD3 package topology, capacities, local/global maps, reference
  labels, and capacity-refresh data to backing-preserving arrays.
- MCPD4 package builders now consume the generated packages by move rather
  than retaining and copying a second partition payload.
- Generalized protocol serialization over contiguous package containers while
  preserving the existing wire representation and exact arithmetic.
- A remote file-backed worker now rehomes decoded resident payloads into its
  configured mappings before constructing the solver. MCPD4 passes all 6 CTest
  targets, including TCP loopback and process integration.

## 2026-07-20 23:13 PDT - Removed the algorithmically distinct worker mode

- Removed MCPD4's runtime and in-process benchmark selection of
  `StreamingPartitionWorker`, which evicted and reconstructed local solvers.
- Historical streaming flags now select complete file-backed
  `InProcessPartitionWorker` storage. Alpha, momentum, primal-dual flow, local
  labels, and BK residual/search-tree state remain live exactly as in MCPD3-N.
- Added a TCP regression proving the compatibility alias maps complete
  persistent state and still solves exactly. Updated user documentation to
  describe operating-system paging rather than solver eviction.
- MCPD4 passes all 6 CTest targets, including the process-level compatibility
  command and remote mmap diagnostics.

## 2026-07-20 23:35 PDT - Bounded multipart partition transport

- Advanced the wire protocol to version 11 and replaced whole-package TCP
  loads with metadata, bounded 64K-element section chunks, and a validated end
  marker. The worker assembles topology, capacities, local/global maps, and
  reference labels directly into its configured final backing.
- Kept legacy single-package decoding for compatibility while making all new
  coordinator loads multipart. Malformed order, interruption, ID, offset,
  value-type, size, and header-shape branches reject the transfer and discard
  partial state.
- Added package frame counts to coordinator/worker status and final telemetry.
  Snappy now compresses each bounded chunk independently.
- A 70,000-parallel-edge TCP regression forces ten package frames, solves the
  exact local problem, and passes in both uncompressed and Snappy modes. The
  700,375 logical bytes compressed to 33,407 wire bytes in the test run.
- MCPD4 passes all 6 CTest targets, including protocol serialization, TCP
  loopback, and process integration.

## 2026-07-20 23:56 PDT - Bounded mapped final-label recovery

- Advanced MCPD3 to recover final local labels by validated offset/count
  copies into a configurable mapped coordinator result without changing the
  final local solve, alpha state, regularization, or package ordering.
- Advanced the wire protocol to version 12 with one final-label request and a
  bounded stream of 64K-label chunks plus an end marker. Full labels no longer
  inflate a solve-result frame or require a complete worker/coordinator staging
  vector.
- Added dedicated label-recovery timing, byte, and frame telemetry to worker,
  coordinator, status, and final output.
- A 150,000-node remote regression arrives as three chunks plus one end frame
  directly into mapped storage. Partial ranges, unknown partitions, invalid
  ranges, null destinations, wrong IDs/offsets, oversized chunks, early ends,
  and unexpected response frames are covered.
- MCPD4 passes 6/6 and the Phase integration passes 58/58 with explicit mapped
  final-label diagnostics.
