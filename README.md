# mcpd4

mcpd4 is the distributed product wrapper around the `mcpd3` min-cut solver.
The product repo owns the coordinator/worker binaries, TCP protocol, run
scripts, integration tests, and deployment docs. The solver dependency remains
the `third_party/mcpd3` submodule and keeps its public `mcpd3::` API.

Use this README as the setup/runbook for another agent or machine. Historical
planning details live in [AGENT_HANDOFF.md](AGENT_HANDOFF.md), progress is in
[PROGRESS_LOG.md](PROGRESS_LOG.md), and known constraints are in
[FAILED_APPROACHES.md](FAILED_APPROACHES.md).

## Repository Layout

- `include/mcpd4`, `src`: mcpd4 protocol, TCP runtime, coordinator, worker.
- `third_party/mcpd3`: pinned solver dependency and partition-worker API.
- `third_party/snappy`: optional RPC compression dependency.
- `tests/fixtures`: small committed DIMACS graphs for smoke tests.
- `scripts/run_local_process_benchmark.sh`: starts one coordinator and local
  workers for a quick localhost distributed run.
- `build/mcpd4_inprocess_benchmark`: optional benchmark binary that runs the
  product `PartitionWorkerCoordinator` fully in-process for local monolithic
  comparisons.
- `third_party/mcpd3/build/mcpd3_native_monolith_benchmark`: standalone
  mcpd3 build target for the clean native `DualDecomposition` path. Build this
  from the mcpd3 submodule when comparing best local mcpd3 against mcpd4.

## Requirements

- Linux/POSIX environment with TCP loopback support.
- CMake 3.16 or newer.
- C++17 compiler.
- `git` with submodule support.
- `python3` only for the local benchmark helper script.

The current runtime is IPv4 TCP. It can optionally compress RPC frames with
Snappy, but it does not provide authentication or encryption, so run it on a
trusted network or behind an SSH/VPN tunnel.

## Get The Code

Clone the product repo and initialize the solver and compression submodules:

```bash
git clone --recurse-submodules https://github.com/vvhitedog/mcpd4.git
cd mcpd4
git submodule update --init --recursive
```

If the checkout already exists, refresh it with:

```bash
git pull
git submodule sync --recursive
git submodule update --init --recursive
```

The expected solver dependency is `third_party/mcpd3`. Do not rename that
submodule or the `mcpd3::` API when working on mcpd4. Snappy support is built
from `third_party/snappy` by default; configure with `-DMCPD4_ENABLE_SNAPPY=OFF`
to build without RPC compression support.

## Build And Test

Configure and build the product binaries:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The important outputs are:

- `build/mcpd4_coordinator`
- `build/mcpd4_worker`
- `build/mcpd4_discovery`
- `build/mcpd4_status`

Run the product test suite:

```bash
ctest --test-dir build --output-on-failure
```

The current suite includes protocol serialization, TCP loopback, worker error
handling, objective-scale promotion, optional Snappy transport coverage, and
localhost process integration tests.

## Native mcpd3 Comparator

mcpd4 also includes `mcpd4_inprocess_benchmark`, but that intentionally uses
the product worker-coordinator abstraction. For a native mcpd3 monolith
baseline, configure the solver submodule independently:

```bash
cmake -S third_party/mcpd3 -B build/mcpd3-native -DCMAKE_BUILD_TYPE=Release
cmake --build build/mcpd3-native -j
```

Example directed run:

```bash
MCPD3_PARTITIONER=basic \
build/mcpd3-native/mcpd3_native_monolith_benchmark /data/adhead.n6c10.max \
  --directed \
  --partitions 10 \
  --objective-scale 1000 \
  --schedule-start 10000 \
  --schedule-levels 5 \
  --max-iterations 10000
```

This path constructs `mcpd3::DualDecomposition` directly and disables
partition-package export by default. Use it to measure the best native local
solver separately from distributed RPC, worker assignment, and wire-format
choices.

## Out-Of-Core Worker Storage

Workers normally keep every assigned partition solver resident in memory. For
large graphs, enable disk-backed streaming storage on workers so nonresident
partition payloads are kept on local disk and only a bounded set of BK solver
instances is materialized at once.

Use this on each process-level worker:

```bash
./build/mcpd4_worker 10.0.0.10 50051 \
  --name worker-a \
  --streaming-partitions \
  --streaming-dir /fast-disk/mcpd4-worker-a \
  --streaming-cache-bytes 4000000000
```

Use the same mode in the local in-process benchmark:

```bash
MCPD3_PARTITIONER=basic \
./build/mcpd4_inprocess_benchmark /data/adhead.n26c100.max \
  --directed \
  --workers 1 \
  --partitions 48 \
  --objective-scale 2000 \
  --schedule-start 10000 \
  --schedule-levels 5 \
  --max-iterations 10000 \
  --streaming-workers \
  --streaming-dir /fast-disk/mcpd4-stream \
  --streaming-cache-bytes 4000000000
```

`--streaming-cache-bytes 0` means no explicit resident-byte limit. A positive
limit is approximate and based on BK array plus solver-vector estimates; one
oversized partition is still allowed to load when the cache is otherwise empty.
The streaming worker preserves alpha state and the previous local cut labels
across eviction, which is required by the current scaled-epsilon
regularization. It also spills warm solver state on cache eviction, including
primal-dual flow vectors and BK residual/tree state, then restores it when the
partition is materialized again. Evicted warm snapshots are invalidated across
objective-scale promotions; resident solvers still scale in place and will
write a fresh warm snapshot on later eviction.

## Quick Localhost Run

Use the helper script for the simplest end-to-end distributed smoke test. It
starts a coordinator, waits until it is listening, starts workers, and forwards
the coordinator output.

```bash
MCPD4_WORKERS=2 \
MCPD4_PARTITIONS=4 \
MCPD4_MAX_ITERATIONS=10000 \
MCPD4_SCHEDULE_LEVELS=5 \
MCPD4_SCHEDULE_START=10000 \
MCPD4_OBJECTIVE_SCALE=10000 \
MCPD4_PROGRESS_EVERY=100 \
scripts/run_local_process_benchmark.sh tests/fixtures/hand_bottleneck.max
```

The helper accepts these environment overrides:

```text
MCPD4_BUILD_DIR                    default: build
MCPD4_WORKERS                      default: 2
MCPD4_PARTITIONS                   default: 2
MCPD4_MAX_ITERATIONS               default: 10000
MCPD4_SCHEDULE_LEVELS              default: 5
MCPD4_SCHEDULE_START               default: 10000
MCPD4_OBJECTIVE_SCALE              default: 10000
MCPD4_ACCEPT_TIMEOUT_MS            default: 30000
MCPD4_READY_TIMEOUT_SEC            default: 300
MCPD4_PROGRESS_EVERY               default: 0
MCPD4_RPC_COMPRESSION              default: none
MCPD4_SATURATE_CAPACITY_OVERFLOW   default: 0
```

Legacy `MCPD4_NUM_SCALES`, `MCPD4_INITIAL_STEP`,
`MCPD4_CAPACITY_MULTIPLIER`, and `MCPD3_*` aliases are still accepted by the
helper for compatibility.

## Manual Localhost Run

Manual runs are useful when debugging process behavior. Start the coordinator
first in terminal 1:

```bash
./build/mcpd4_coordinator tests/fixtures/hand_bottleneck.max \
  --bind 127.0.0.1 \
  --port 50051 \
  --workers 2 \
  --partitions 4 \
  --max-iterations 10000 \
  --schedule-levels 5 \
  --schedule-start 10000 \
  --objective-scale 10000 \
  --accept-timeout-ms 30000 \
  --progress-every 100 \
  --rpc-compression none
```

Then start exactly two workers, matching `--workers 2`:

```bash
./build/mcpd4_worker 127.0.0.1 50051 --name local-a --rpc-compression none
```

```bash
./build/mcpd4_worker 127.0.0.1 50051 --name local-b --rpc-compression none
```

The coordinator waits for all requested workers before solving. If a worker
does not connect before `--accept-timeout-ms`, the coordinator exits with an
error.

## Distributed Run Across Machines

Only the coordinator needs the DIMACS input file. Workers only need the built
`mcpd4_worker` binary and network access to the coordinator.

On the coordinator machine, choose an interface and open the port in any local
firewall. Use `0.0.0.0` to accept connections on all IPv4 interfaces:

```bash
./build/mcpd4_coordinator /data/graph.max \
  --bind 0.0.0.0 \
  --port 50051 \
  --workers 4 \
  --partitions 10 \
  --max-iterations 10000 \
  --schedule-levels 5 \
  --schedule-start 10000 \
  --objective-scale 10000 \
  --accept-timeout-ms 600000 \
  --progress-every 50 \
  --rpc-compression none
```

On each worker machine, use the coordinator machine's reachable IP or DNS name:

```bash
./build/mcpd4_worker 10.0.0.10 50051 --name worker-a --rpc-compression none
```

```bash
./build/mcpd4_worker 10.0.0.10 50051 --name worker-b --rpc-compression none
```

```bash
./build/mcpd4_worker 10.0.0.10 50051 --name worker-c --rpc-compression none
```

```bash
./build/mcpd4_worker 10.0.0.10 50051 --name worker-d --rpc-compression none
```

Use one worker process per machine to start. A worker can own multiple
partitions; the coordinator sends a batched solve request to each active worker
per round. Initial partition ownership is static and weighted by partition size
plus the worker CPU/RAM values reported in the worker handshake.

For directed DIMACS inputs, add `--directed` to the coordinator command:

```bash
./build/mcpd4_coordinator /data/adhead.n6c10.max \
  --directed \
  --bind 0.0.0.0 \
  --port 50051 \
  --workers 4 \
  --partitions 10 \
  --objective-scale 10000 \
  --rpc-compression none
```

## RPC Compression

`--rpc-compression none` is the default. Use `--rpc-compression snappy` on the
coordinator and every worker to compress all RPC frames after the initial
uncompressed `HELLO` handshake. Snappy is useful to test when partition-package
setup traffic or repeated boundary traffic is large enough to offset
compression/decompression CPU time.

Baseline run:

```bash
./build/mcpd4_coordinator /data/adhead.n6c10.max \
  --directed \
  --bind 0.0.0.0 \
  --port 50051 \
  --workers 4 \
  --partitions 10 \
  --max-iterations 10000 \
  --schedule-levels 5 \
  --schedule-start 10000 \
  --objective-scale 100 \
  --accept-timeout-ms 600000 \
  --progress-every 50 \
  --rpc-compression none
```

```bash
./build/mcpd4_worker 10.0.0.10 50051 --name worker-a \
  --rpc-compression none
```

Compressed run:

```bash
./build/mcpd4_coordinator /data/adhead.n6c10.max \
  --directed \
  --bind 0.0.0.0 \
  --port 50051 \
  --workers 4 \
  --partitions 10 \
  --max-iterations 10000 \
  --schedule-levels 5 \
  --schedule-start 10000 \
  --objective-scale 100 \
  --accept-timeout-ms 600000 \
  --progress-every 50 \
  --rpc-compression snappy
```

```bash
./build/mcpd4_worker 10.0.0.10 50051 --name worker-a \
  --rpc-compression snappy
```

Compare these fields between the baseline and compressed logs:

- `timing_total_wall_us`: full process wall time.
- `timing_worker_rpc_overhead_us`: coordinator-observed worker RPC time not
  spent inside worker solve calls.
- `rpc_tx_bytes_total` / `rpc_rx_bytes_total`: logical protocol bytes.
- `rpc_tx_wire_bytes_total` / `rpc_rx_wire_bytes_total`: actual transport
  bytes after compression envelopes.
- `rpc_compression_wall_us` / `rpc_decompression_wall_us`: CPU time spent in
  transport compression and decompression.
- `rpc_tx_compressed_frame_count` / `rpc_rx_compressed_frame_count`: frames
  that Snappy made smaller.
- `rpc_tx_stored_frame_count` / `rpc_rx_stored_frame_count`: frames sent in
  the Snappy envelope without compression because compression would not help.

## Discovery Mode

Discovery mode lets workers find a waiting coordinator without manually typing
the TCP host/port on every machine. The coordinator answers UDP discovery
queries until an operator sends a close command. Closing discovery tells the
coordinator to proceed once at least `--workers N` workers have connected.

Start the coordinator with a UDP discovery port:

```bash
./build/mcpd4_coordinator /data/graph.max \
  --bind 0.0.0.0 \
  --port 50051 \
  --workers 2 \
  --partitions 10 \
  --max-iterations 10000 \
  --schedule-levels 5 \
  --schedule-start 10000 \
  --objective-scale 10000 \
  --accept-timeout-ms 600000 \
  --progress-every 50 \
  --discovery-port 50052 \
  --discovery-token lab-run-1 \
  --status-port 50053 \
  --status-token lab-run-1 \
  --rpc-compression none
```

List visible coordinators from another machine:

```bash
./build/mcpd4_discovery list \
  --host 255.255.255.255 \
  --port 50052 \
  --token lab-run-1
```

If broadcast is blocked, query the coordinator machine directly:

```bash
./build/mcpd4_discovery list \
  --host 10.0.0.10 \
  --port 50052 \
  --token lab-run-1
```

Start workers in discovery mode:

```bash
./build/mcpd4_worker --discover \
  --discovery-host 255.255.255.255 \
  --discovery-port 50052 \
  --discovery-token lab-run-1 \
  --status-port 51053 \
  --status-token lab-run-1 \
  --name worker-a \
  --rpc-compression none
```

Query coordinator or worker status while the run is waiting or solving:

```bash
./build/mcpd4_status 10.0.0.10 50053 --token lab-run-1
```

```bash
./build/mcpd4_status 10.0.0.20 51053 --token lab-run-1
```

After enough workers have connected, close discovery and start the solve:

```bash
./build/mcpd4_discovery close \
  --host 10.0.0.10 \
  --port 50052 \
  --token lab-run-1
```

Use `--advertise-host HOST` on the coordinator if workers should connect to a
specific DNS name or interface address instead of the UDP reply source.

## Coordinator Options

```text
usage: mcpd4_coordinator DIMACS --port PORT [--bind HOST] [--workers N]
       [--partitions N] [--max-iterations N] [--schedule-levels N]
       [--schedule-start N] [--objective-scale N]
       [--accept-timeout-ms N] [--progress-every N] [--ready-file PATH]
       [--discovery-port PORT] [--discovery-token TOKEN]
       [--advertise-host HOST]
       [--status-port PORT] [--status-token TOKEN]
       [--telemetry-csv-prefix PATH]
       [--rpc-compression none|snappy]
       [--saturate-capacity-overflow] [--directed]
```

- `DIMACS`: input graph path. The coordinator reads this locally.
- `--bind HOST`: local bind address. Default is `127.0.0.1`; use `0.0.0.0` or
  an interface IP for remote workers.
- `--port PORT`: required TCP port.
- `--workers N`: number of worker processes the coordinator must accept. In
  discovery mode this is the minimum worker count required before a close
  command can let the solve proceed.
- `--partitions N`: number of local subproblems to build.
- `--max-iterations N`: maximum optimizer iterations.
- `--schedule-levels N`: number of base-10 dual-decomposition schedule levels.
- `--schedule-start N`: first dual-decomposition schedule step.
- `--objective-scale N`: multiplies capacities before partitioning and is
  also the objective scale used by exact scaled-epsilon regularization.
- `--accept-timeout-ms N`: per-worker accept timeout.
- `--progress-every N`: print optimizer health every N total iterations. Use
  `0` to disable progress streaming.
- `--ready-file PATH`: write the listening port after the socket is ready.
- `--discovery-port PORT`: enable UDP discovery mode on this port.
- `--discovery-token TOKEN`: require matching worker/list/close discovery
  tokens. Default is `mcpd4`.
- `--advertise-host HOST`: host or IP workers should use for the TCP
  connection. If omitted, workers use the UDP response source address.
- `--status-port PORT`: enable a UDP status endpoint on this port.
- `--status-token TOKEN`: token required for status queries. Default is
  `mcpd4`.
- `--telemetry-csv-prefix PATH`: write raw post-run telemetry CSVs at
  `PATH.*.csv`. This records every optimizer iteration even when
  `--progress-every 0`.
- `--rpc-compression none|snappy`: transport compression mode. Default is
  `none`. If set to `snappy`, every worker must also use
  `--rpc-compression snappy`.
- `--saturate-capacity-overflow`: opt-in overflow compatibility mode. This
  clips overflowing scaled capacities and solves the clipped problem, not the
  exact original problem. The setting also applies to later objective-scale
  promotions; `--truncate-capacity-overflow` is accepted as an alias.
- `--directed`: use the directed streaming DIMACS reader.

Compatibility aliases accepted by the coordinator:
`--num-scales` for `--schedule-levels`, `--initial-step` for
`--schedule-start`, and `--capacity-multiplier` for `--objective-scale`.

## Worker Options

```text
usage: mcpd4_worker HOST PORT [--name NAME]
       mcpd4_worker --discover [--discovery-host HOST]
       [--discovery-port PORT] [--discovery-token TOKEN]
       [--discovery-timeout-ms N] [--name NAME]
       [--status-port PORT] [--status-token TOKEN]
       [--rpc-compression none|snappy]
       [--streaming-partitions] [--streaming-dir DIR]
       [--streaming-cache-bytes N]
```

- `HOST`: coordinator host or IP.
- `PORT`: coordinator port.
- `--discover`: find one coordinator through UDP discovery, then connect to
  its TCP port.
- `--discovery-host HOST`: UDP destination for discovery. Default is
  `255.255.255.255`; use an explicit coordinator IP if broadcast is blocked.
- `--discovery-port PORT`: UDP discovery port. Default is `50052`.
- `--discovery-token TOKEN`: token that must match the coordinator.
- `--discovery-timeout-ms N`: discovery wait timeout.
- `--status-port PORT`: enable a UDP status endpoint on this worker.
- `--status-token TOKEN`: token required for status queries. Default is
  `mcpd4`.
- `--rpc-compression none|snappy`: transport compression mode. Must match the
  coordinator's setting.
- `--streaming-partitions`: keep partition payloads on disk and materialize
  assigned solvers on demand.
- `--streaming-dir DIR`: directory for disk-backed partition payloads. If
  omitted, the worker uses a temporary directory it owns.
- `--streaming-cache-bytes N`: approximate maximum resident solver bytes for
  materialized partitions. `0` disables eviction.
- `--name NAME`: optional worker name used in logs and progress output.

Workers receive all partition data from the coordinator after connecting. They
do not need the DIMACS file.

## Discovery Tool Options

```text
usage: mcpd4_discovery list [--host HOST] [--port PORT] [--token TOKEN]
       [--timeout-ms N]
       mcpd4_discovery close --host HOST [--port PORT] [--token TOKEN]
       [--timeout-ms N]
```

- `list`: send a UDP discovery query and print visible coordinators.
- `close`: send a UDP close command. A valid close command stops discovery and
  lets the coordinator proceed once the minimum worker count is connected.
- `--host HOST`: UDP destination. `list` defaults to broadcast; `close`
  requires an explicit host.
- `--port PORT`: UDP discovery port. Default is `50052`.
- `--token TOKEN`: discovery token. Default is `mcpd4`.
- `--timeout-ms N`: wait timeout for replies.

## Status Tool Options

```text
usage: mcpd4_status HOST PORT [--token TOKEN] [--timeout-ms N]
```

- `HOST`: coordinator or worker host/IP.
- `PORT`: UDP status port configured with `--status-port`.
- `--token TOKEN`: status token. Default is `mcpd4`.
- `--timeout-ms N`: wait timeout for the status response.

Coordinator status includes the current phase, accepted worker count, worker
names/resources, partition ownership, partition count, objective scale, latest
schedule/iteration state, lower-bound fields, regularization diagnostics,
disagreement count, aggregate solve/RPC counts, RPC byte/wire counters,
compression timing, per-worker solve timing, and a `segments` timing summary.
Worker status includes its phase, CPU/RAM, temp path, coordinator endpoint,
worker storage mode, streaming directory/cache settings, loaded partition ids,
current round/partition ids, solve counts, batch RPC count, worker solve wall
time, RPC byte/wire counters, compression timing, and last error.

The coordinator `segments` field is a comma-separated summary of algorithm
segments that have started. Not-yet-started segments are omitted. Each record
uses this shape:

```text
name:state=running|done:elapsed_us=N[:eta_remaining_us=N|unknown][:progress_current=N:progress_total=N][:stats...]
```

Useful segment names are `read_graph`, `scale_graph`, `partitioning`,
`transport_setup`, `accept_workers`, `coordinator_setup`, `solve`, and
`stop_workers`. Completed segments report total elapsed time and segment
stats. Running segments report elapsed time and, when a useful progress
denominator exists, estimated remaining time. Discovery waits that depend on
an operator close command report `eta_remaining_us=unknown`.

## Interpreting Output

The coordinator prints key-value lines. A successful exact run usually has:

```text
status 0
stop_reason 1
final_disagreement_count 0
objective_scale_saturation_count 0
```

`stop_reason 2` is also an exact agreement path when the scaled-epsilon
regularization budget is valid. The product rejects over-budget regularized
rounds and promotes objective scale before accepting the result.

Status values:

```text
0 OPTIMAL
1 NO_FURTHER_PROGRESS
2 ITERATION_COUNT_EXCEEDED
3 REGULARIZATION_BUDGET_EXCEEDED
```

Stop reason values:

```text
0 NONE
1 NO_DISAGREEMENT
2 REGULARIZED_NO_DISAGREEMENT
3 ITERATION_COUNT_EXCEEDED
4 NO_LOWER_BOUND_IMPROVEMENT
5 LEGACY_PATIENCE
6 GROUP_STOPPING
7 REGULARIZATION_BUDGET_EXCEEDED
```

Important objective fields:

- `final_objective`: selected original objective after dividing by
  `objective_scale`.
- `final_certified_lower_bound`: conservative lower-bound certificate after
  dividing by `objective_scale`.
- `final_regularized_objective`: perturbed objective used by the regularized
  local solve.
- `objective_scale`: final scale after any promotions.
- `objective_scale_promotions`: number of times the coordinator promoted the
  objective scale after a regularization budget overflow.
- `final_regularization_budget`: total active regularization budget in raw
  units.
- `objective_scale_saturation_count`: nonzero means overflow clipping occurred
  and the run solved a clipped-capacity problem.

With `--progress-every`, the coordinator also prints:

- `progress ...`: global optimizer health and cumulative worker timing,
  including `schedule_scale`, `schedule_step`, and
  `effective_schedule_step`.
- `progress_worker ...`: per-worker assigned partition counts, solve counts,
  batch RPC counts, solve wall time, RPC overhead, and RPC byte counters.

RPC byte counters are cumulative. Logical byte counters include encoded
protocol frame headers before transport compression. Wire byte counters report
actual bytes written to the TCP connection, including any Snappy envelope.
Useful fields:

- `rpc_tx_bytes_total` and `rpc_rx_bytes_total`: total bytes sent and received
  from the coordinator perspective before transport compression.
- `rpc_tx_wire_bytes_total` and `rpc_rx_wire_bytes_total`: actual transport
  bytes sent and received after any compression envelope.
- `rpc_compression_wall_us` and `rpc_decompression_wall_us`: cumulative
  Snappy CPU time spent compressing sent frames and decompressing received
  frames.
- `rpc_tx_compressed_frame_count` and `rpc_rx_compressed_frame_count`: frames
  that were actually Snappy-compressed.
- `rpc_tx_stored_frame_count` and `rpc_rx_stored_frame_count`: Snappy-mode
  frames sent uncompressed because the compressed payload was not smaller.
- `rpc_partition_load_tx_bytes`: one-time partition package bytes sent to
  workers.
- `rpc_solve_request_tx_bytes`: repeated solve request bytes sent during the
  optimization loop.
- `rpc_solve_result_rx_bytes`: repeated solve result bytes received during
  the optimization loop.
- `rpc_ready_rx_bytes`, `rpc_stop_tx_bytes`, and `rpc_error_rx_bytes`: control
  traffic.

Protocol version 5 compacts the hot-path boundary exchange and then applies
stateful temporal deltas on live TCP solve frames:

- solve-result boundary labels send only `constraint_id` and `label`;
- alpha updates send only `constraint_id` and current `alpha`.
- after the first sync for a partition, solve requests omit unchanged alpha
  updates and encode changed alphas as temporal varint deltas;
- after the first result for a partition, solve results omit unchanged
  boundary labels and the coordinator reconstructs the full label set before
  handing the result to the mcpd3 coordinator;
- temporal baselines reset on partition load and objective-scale promotion.

`global_node_id`, `local_index`, and initial alpha state are one-time
partition-package metadata. `last_alpha` is maintained by each worker from its
local previous alpha, and `alpha_momentum` remains coordinator-owned.

These fields are useful for detecting stalled workers, partition imbalance,
and whether transport overhead is dominated by setup packages or repeated
solve traffic.

## CSV Telemetry

Use `--telemetry-csv-prefix /tmp/run-name` when you want raw data for offline
analysis instead of live summaries:

```bash
./build/mcpd4_coordinator /data/adhead.n6c10.max \
  --directed \
  --bind 0.0.0.0 \
  --port 50051 \
  --workers 2 \
  --partitions 10 \
  --max-iterations 10000 \
  --schedule-levels 5 \
  --schedule-start 10000 \
  --objective-scale 1000 \
  --telemetry-csv-prefix /tmp/adhead-lan
```

The coordinator writes these files:

- `/tmp/adhead-lan.metadata.csv`: run configuration and graph metadata.
- `/tmp/adhead-lan.partitions.csv`: per-partition node, arc, and boundary
  endpoint counts.
- `/tmp/adhead-lan.workers.csv`: worker resources and assigned partitions.
- `/tmp/adhead-lan.iterations.csv`: one row per optimizer iteration with
  iteration wall time, solve elapsed time, bounds, disagreement, schedule, and
  regularization fields.
- `/tmp/adhead-lan.worker_iterations.csv`: one row per worker per iteration
  with solve RPC wall-time deltas, worker solve-time deltas, RPC overhead
  deltas, and cumulative solve counts.
- `/tmp/adhead-lan.worker_rpc_metrics.csv`: long-form per-worker per-iteration
  RPC counter deltas and cumulative values for byte, wire-byte,
  compression-time, and frame-count fields.
- `/tmp/adhead-lan.final.csv`: final objective, iteration, timing, and
  aggregate RPC summary fields.

For histograms of RPC or solve contribution per iteration, start from
`worker_iterations.csv`: compare `worker_solve_wall_us_delta` and
`worker_rpc_overhead_us_delta` to `solve_rpc_wall_us_delta`, then join to
`iterations.csv` on `total_iteration` when the global iteration state matters.

## Objective Scale And Exactness

The scaled-epsilon regularizer treats each local objective as:

```text
M * F(x) + R(x)
```

where `M` is the objective scale from `--objective-scale`. The point is to
make regularization a lexicographic tie-break rather than a change to the
original optimization problem. Larger objective scales give more regularization
budget but increase the risk of 32-bit capacity overflow.

Practical starting points:

- Small fixtures: `--objective-scale 10000`.
- Large benchmark graphs with bigger capacities: try `100`, then increase if
  the run reports objective-scale promotions or regularization budget pressure.
- If strict scaling overflows, inspect the input capacities before using
  `--saturate-capacity-overflow`; saturation is only a compatibility mode.

## Troubleshooting

- `mcpd4_coordinator failed: --port is required`: pass `--port PORT`.
- Coordinator waits forever or times out: start the exact number of workers
  requested by `--workers`; verify firewall rules and bind address.
- Remote workers cannot connect: bind the coordinator to `0.0.0.0` or the
  correct interface IP, not `127.0.0.1`.
- `objective scale exceeds int range`: reduce
  `--objective-scale` or intentionally use
  `--saturate-capacity-overflow` knowing it clips capacities.
- One worker does most of the work: increase `--partitions`, check
  `progress_worker` timing, and compare assigned partition counts. Dynamic
  work stealing is not implemented yet.
- Nonzero `final_disagreement_count`: the coordinator did not recover an
  agreeing primal solution in that run. Increase scales/iterations, inspect
  progress output, and check whether objective-scale promotions occurred.

## Current MVP Limits

- Worker ownership is static after initial assignment.
- No worker reconnect, heartbeat, or worker replacement during optimization.
- No authentication/encryption on the TCP protocol.
- Coordinator and workers use blocking RPCs, though active workers are
  dispatched concurrently.
- The current capacity storage path is 32-bit for graph capacities; scaling can
  overflow without care.
