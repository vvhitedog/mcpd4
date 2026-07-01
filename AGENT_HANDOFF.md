# Agent Handoff: mcpd4 Product MVP

This document is the starting point for a new Codex/agent session in this
repository. It deliberately condenses the previous experimental context so the
new agent does not need the full chat history.

## Current State: 2026-06-30 21:43 PDT

This file began as the original distributed-MVP plan. The historical sections
below are useful background, but the implementation is far past the original
"no TCP yet" phase. Treat this current-state section, `README.md`,
`MVP_TRACKER.md`, and `PROGRESS_LOG.md` as authoritative.

Current anchors:

- Product repo path: `/home/matt/software/mcpd3-distributed`.
- Product branch: `working`.
- Product remote: `https://github.com/vvhitedog/mcpd4`.
- mcpd3 submodule path: `third_party/mcpd3`.
- mcpd3 submodule branch: `partition-worker-api`.
- mcpd3 submodule checkpoint:
  `8328d73 Document productized solver usage`.

Implemented product surface:

- mcpd4 coordinator/worker TCP runtime with one-shot partition package load.
- Batched per-worker solve RPCs for workers owning multiple partitions.
- Static initial partition assignment weighted by partition estimate and
  worker CPU/RAM handshake data.
- Remote objective-scale promotion via `SCALE_OBJECTIVE`.
- UDP discovery mode plus `mcpd4_discovery list/close`.
- UDP queryable status plus `mcpd4_status`.
- Progress/final/status telemetry for solve counts, timings, regularization
  diagnostics, worker ownership, resources, and RPC byte counters.
- Compact solve-result boundary-label and alpha-update encoding in protocol
  version `4`.
- README runbook for build/test, localhost runs, discovery-mode LAN runs, and
  status inspection.

Current transport telemetry checkpoint:

- Coordinator progress/final/status exposes `rpc_tx_bytes_total`,
  `rpc_rx_bytes_total`, `rpc_partition_load_tx_bytes`,
  `rpc_solve_request_tx_bytes`, `rpc_solve_result_rx_bytes`,
  `rpc_ready_rx_bytes`, `rpc_stop_tx_bytes`, and related counters.
- Worker status exposes transmitted/received totals plus partition-load,
  solve-request, solve-result, ready, stop, and error byte categories.
- Current tiny local fixture run:
  `rpc_tx_bytes_total=1090`, `rpc_rx_bytes_total=1432`,
  `rpc_partition_load_tx_bytes=250`, `rpc_solve_request_tx_bytes=760`,
  `rpc_solve_result_rx_bytes=1232`.
- First concrete payload reductions are complete:
  - solve-result boundary labels now transmit only `constraint_id` and
    `label`;
  - alpha updates now transmit only `constraint_id` and current `alpha`;
  - `global_node_id`, `local_index`, and initial alpha state remain setup
    metadata in partition packages;
  - workers derive `last_alpha` from their local previous alpha, and
    `alpha_momentum` remains coordinator-owned.
- Tiny fixture result traffic dropped from `rpc_solve_result_rx_bytes=1344` to
  `1232`, saving 8 bytes for each of 14 partition-solve result labels.
- Tiny fixture request traffic dropped from `rpc_solve_request_tx_bytes=904`
  to `760`; a full dirty `adhead` alpha sync should save roughly `15.7 MB`
  per iteration.

Suggested next prompt:

```text
We are in /home/matt/software/mcpd3-distributed on branch working.
Read AGENT_HANDOFF.md current-state, MVP_TRACKER.md, PROGRESS_LOG.md, and
README.md. Continue RPC transport optimization. Compact solve-result labels
and compact alpha updates are already implemented in protocol version 4, so
use the existing byte telemetry to evaluate serialization/deserialization
timing, compression of large one-time partition packages, bit-packed boundary
labels, and any remaining repeated solve payload. Preserve correctness tests
and update docs/logs.
```

## Repository Roles

There are three distinct codebases/concepts:

1. `graph-cuts-undirected`
   - Path: `/home/matt/software/graph-cuts-undirected`
   - Role: experimental research/benchmark/proof workspace.
   - Do not turn this into the product.
   - Useful as historical context and benchmark reference.

2. `mcpd3`
   - In this repo as submodule: `third_party/mcpd3`
   - Public branch:
     `https://github.com/vvhitedog/mcpd3/tree/distributed-mvp-start`
   - Role: solver dependency and algorithm core.
   - Should receive only narrow, upstreamable API extraction changes.
   - Should not absorb product deployment/runtime concerns unless unavoidable.

3. `mcpd4`
   - This repository.
   - Role: product layer for distributed mcpd3, branded as mcpd4.
   - Owns coordinator/worker binaries, TCP protocol, config/deployment,
     product docs, integration tests.

## Current Git Anchors

Experimental parent repo:

```text
/home/matt/software/graph-cuts-undirected
HEAD: 6b0dac1 Document distributed mcpd3 MVP plan
```

Relevant context document in the experimental repo:

```text
/home/matt/software/graph-cuts-undirected/docs/mcpd3_distributed_mvp.md
```

mcpd3 public starting branch:

```text
remote: https://github.com/vvhitedog/mcpd3.git
branch: distributed-mvp-start
HEAD: aa49b0c Remove Polyak step policy from distributed start branch
URL: https://github.com/vvhitedog/mcpd3/tree/distributed-mvp-start
```

This branch was created from the local `dd-convergence-fixes` work and then
cleaned by removing the Polyak step-policy experiment, which did not benchmark
well enough to be a good product starting point.

The starting branch keeps useful work:

- corrected patience/no-progress logic;
- progress instrumentation;
- regularization diagnostics;
- mmap-backed BK graph storage options;
- streaming DIMACS support for large symmetric inputs;
- flat CSR METIS partitioning path;
- mcpd3 setup/solve timing instrumentation;
- partition validation/progress logging.

The starting branch intentionally removes:

- `--step-policy polyak`;
- `--theta`;
- `DualDecompositionStepPolicy`;
- `computePolyakStep`.

The exposed schedule is now fixed-scale with bounded step size:

- `--min-step`
- `--max-step`
- `--patience`
- `--max-iterations`
- `--disable-group-stopping`
- `--no-momentum`

## Why This Product Exists

mcpd3 decomposes a min-cut problem into independent partition subproblems plus
clone-equality constraints. The optimization loop already has the shape needed
for distributed execution:

1. solve each partition independently;
2. gather each partition's labels/lower-bound contribution;
3. centrally update Lagrange multipliers;
4. scatter updated multipliers;
5. repeat until agreement or stopping.

The goal is to make this distributed across machines.

The initial target is not a perfect cluster framework. The target is an MVP that
proves:

- a coordinator can assign partitions to worker processes;
- workers can solve local subproblems;
- only clone labels / lower-bound terms / alpha updates need to move during
  optimization;
- results match current in-process mcpd3 on small and medium cases.

## Recommended Architecture

Use server/client first, not MPI.

Server/client advantages for the MVP:

- easier to debug on localhost;
- supports heterogeneous machines;
- clients can report RAM/CPU before assignment;
- coordinator can wait for a dynamic set of workers at setup;
- easy to add logs and failure diagnostics.

MPI is still a good later target:

- rank 0 = coordinator;
- ranks 1..N = workers;
- setup maps to scatter/broadcast;
- solve results map to gather;
- alpha updates map to broadcast or sparse point-to-point sends.

Design the message boundaries so they can later be mapped to MPI.

## Critical Boundary Decision

Do not bake TCP/networking into the solver core.

Instead:

- `mcpd3` should expose a partition-worker API and the minimal data structures
  needed by a distributed runtime.
- this repo should implement the coordinator/worker runtime.

Good split:

```text
mcpd3:
  graph loading/parsing
  partition construction
  local partition solver
  clone constraint metadata
  alpha/momentum update math
  in-process worker API

mcpd4:
  coordinator executable
  worker executable
  binary protocol
  TCP transport
  resource handshake
  product config/logging/deployment
  integration tests
```

## Current mcpd3 Internals To Understand

Read these files first:

```text
third_party/mcpd3/decomp/dualdecomp.h
third_party/mcpd3/decomp/constraint.h
third_party/mcpd3/example/dimacs_dual_decomp.cpp
third_party/mcpd3/primaldual/mcpd3.h
```

Key current objects:

- `DualDecomposition`
  - owns graph partitioning, subgraph construction, solvers, constraints, and
    optimization loop.

- `solvers_[partition]`
  - vector of independent `PrimalDualMinCutSolver` instances.

- `constraint_arc_map_`
  - coordinator-owned global map:
    `global_node_id -> list<DualDecompositionConstraintArc>`.

- `DualDecompositionConstraintArc`
  - stores:
    - `alpha`
    - `last_alpha`
    - `alpha_momentum`
    - `partition_index_source`
    - `partition_index_target`
    - `local_index_source`
    - `local_index_target`

- `runLagrangeMultipliersUpdateStep`
  - current central update path:
    - reads clone labels from partition solvers;
    - computes disagreement count and norm;
    - updates alpha/momentum;
    - records changed/disagreeing global indices.

The distributed coordinator should own the equivalent of `constraint_arc_map_`
and the alpha/momentum state.

Workers should own only local partition solvers and local endpoint bindings.

## MVP Implementation Sequence

### Stage 0: Verify build

From this repo:

```bash
git submodule update --init --recursive
cmake -S third_party/mcpd3 -B third_party/mcpd3/build -DCMAKE_BUILD_TYPE=Release
cmake --build third_party/mcpd3/build --target dimacs_dual_decomp_example -j
```

If METIS is available and desired, use the existing branch support in mcpd3.
The experimental repo has local METIS build artifacts, but this product repo
should not assume they exist.

### Stage 1: Extract network-free partition worker API in mcpd3

Do this in `third_party/mcpd3` first. Keep changes narrow and upstreamable.

Introduce data structures roughly like:

```cpp
struct PartitionPackage {
  int partition_id;
  int local_node_count;
  std::vector<int> arcs;
  std::vector<int> arc_capacities;
  std::vector<int> terminal_capacities;
  std::vector<int> local_to_global;
  std::vector<ConstraintEndpointBinding> constraint_endpoints;
};

struct PartitionSolveRequest {
  long round_id;
  long scale;
  int regularization_strength;
  std::vector<AlphaUpdate> alpha_updates;
};

struct PartitionSolveResult {
  long round_id;
  int partition_id;
  long lower_bound;
  long regularization_budget;
  long regularization_contribution;
  long regularization_anchor_sink_count;
  long regularization_active_sink_count;
  std::vector<ConstraintLabel> constrained_labels;
};

class PartitionWorker {
 public:
  virtual ~PartitionWorker() = default;
  virtual void loadPartition(const PartitionPackage& package) = 0;
  virtual PartitionSolveResult solveRound(
      const PartitionSolveRequest& request) = 0;
};
```

Then implement:

```cpp
class InProcessPartitionWorker final : public PartitionWorker { ... };
```

The first milestone is not TCP. It is proving that current in-process mcpd3 can
run through this worker interface and produce the same result as current
`DualDecomposition`.

### Stage 2: Extract coordinator loop in-process

Add a coordinator/controller that uses `PartitionWorker` objects instead of
directly accessing `solvers_`.

It should:

1. own global constraints and alpha/momentum;
2. send solve requests to workers;
3. gather `PartitionSolveResult`s;
4. update alpha/momentum centrally;
5. check stopping criteria;
6. report the same progress fields as `DualDecomposition`.

The controller should initially be constructed with in-process workers.

Equivalence target:

- same lower-bound sequence where deterministic;
- same final best lower bound;
- same disagreement count;
- same optimal/no-progress status.

Exact per-iteration identity may be too strict if ordering changes; final
equivalence and diagnostics are enough for MVP.

### Stage 3: Add serialization in this product repo

Once the in-process API works, add serialization here.

Use a length-prefixed binary protocol:

```text
uint32 message_type
uint64 payload_bytes
payload
```

Start with explicit little-endian vector encodings. Avoid JSON for hot paths.

Messages:

- `HELLO`
- `PARTITION_PACKAGE`
- `READY`
- `SOLVE_ROUND_REQUEST`
- `SOLVE_ROUND_RESULT`
- `ALPHA_UPDATE`
- `STOP`
- `ERROR`

### Stage 4: TCP loopback coordinator/worker

Add binaries in this repo:

```text
src/mcpd4_coordinator.cpp
src/mcpd4_worker.cpp
```

Suggested CLI:

```bash
mcpd4_coordinator \
  --input graph.max \
  --partitions 4 \
  --listen 127.0.0.1:50051 \
  --workers 2 \
  --wait-timeout-sec 60 \
  --patience 10 \
  --max-iterations 10000 \
  --disable-primal-upper-bound

mcpd4_worker \
  --connect 127.0.0.1:50051 \
  --worker-name w0 \
  --ram-gb 64 \
  --threads 1
```

For MVP, a worker may own multiple partitions.

### Stage 5: Localhost equivalence tests

Use tiny DIMACS graphs first:

- hand bottleneck graph;
- dead-end graph;
- random small graph.

Then use:

```text
/home/matt/software/graph-cuts-undirected/benchmark_results/dimacs/LB07-bunny-sml-undirected.max
```

Small bunny reference:

```text
nodes: 805802
directed arcs in converted undirected DIMACS: 5299184
known maxflow/mincut: 1881077
```

Do not assume that file exists on every machine. It is present locally in the
experimental repo, not in this product repo.

## Setup/Handshake Design

Worker `HELLO` should include:

- protocol version;
- worker name/id;
- CPU count;
- RAM GB;
- supported protocol features;
- optional temporary storage path;
- endianness/build id for debug.

Coordinator assignment policy:

- MVP: equal partitions assigned round-robin.
- Next: assign number of partitions by worker RAM.
- Later: produce skewed partition sizes based on RAM.

RAM-aware goal:

- smallest RAM worker must hold at least one partition;
- larger workers may hold multiple partitions;
- avoid producing partitions too large for the smallest worker.

## Per-Round Data Movement

Hot path should send only:

Worker -> coordinator:

- lower-bound contribution;
- regularization diagnostics;
- constrained clone labels;
- optional full labels only if doing primal upper-bound decode.

Coordinator -> worker:

- changed alpha/momentum values;
- regularization strength;
- scale/round id.

Do not resend graph structure during optimization.

MVP can send all alphas every round for simplicity. First performance upgrade:
send only changed alpha records.

## Stopping/Correctness Semantics

Important: mcpd3 can stop for different reasons.

Strong optimality:

- zero disagreement with zero regularization;
- or lower bound closes primal upper bound with zero regularization.

Not an exact certificate:

- regularized agreement;
- no-progress/patience;
- group stopping;
- timeout.

Regularization diagnostics matter:

- `final_regularization_budget`
- `final_regularization_contribution`
- active/anchor counts

If regularization is active, do not claim exact mincut solely from agreement.

## Current mcpd3 Benchmark Knowledge

The important result from the experimental repo:

- mcpd3 can be very fast on some undirected vision DIMACS conversions,
  especially with a small partition count.
- Partition quality matters a lot.
- More partitions are not always better.
- Large instances may become memory/mmap sensitive.
- METIS can help on some cases but can be expensive or problematic on very
  large graphs.

Small bunny exact reference from native Dinic:

```text
LB07-bunny-sml-undirected.max
maxflow = 1881077
parallel BFS Dinic, 4 threads ~= 1.2s on the local machine
```

mcpd3-specific notes:

- `--disable-primal-upper-bound` avoids expensive primal decoding and is often
  appropriate for raw solve-loop comparisons.
- `--quiet` should be used in benchmarks to avoid printing overhead.
- progress output can be enabled for debugging convergence/ETA.

## Files In The Experimental Repo Worth Reading

```text
/home/matt/software/graph-cuts-undirected/docs/mcpd3_distributed_mvp.md
/home/matt/software/graph-cuts-undirected/docs/mcpd3_integration.md
/home/matt/software/graph-cuts-undirected/scripts/bench_mcpd3.py
/home/matt/software/graph-cuts-undirected/scripts/convert_dimacs_to_undirected_maxsym.py
/home/matt/software/graph-cuts-undirected/scripts/crop_symmetric_dimacs_prefix.py
```

These are reference/benchmark tools, not product code.

## Branching Guidance

For mcpd3 API extraction:

```bash
cd third_party/mcpd3
git checkout distributed-mvp-start
git checkout -b partition-worker-api
```

Keep mcpd3 commits small and upstreamable:

1. expose partition package construction;
2. introduce `PartitionWorker` interface;
3. implement in-process worker;
4. refactor coordinator loop to use worker interface;
5. add tests/examples.

For product repo work:

```bash
cd /home/matt/software/mcpd3-distributed
git checkout -b tcp-loopback-mvp
```

## Suggested First Prompt For A New Agent

Use this prompt when starting a new Codex session in this repository:

```text
We are in /home/matt/software/mcpd3-distributed.

Read AGENT_HANDOFF.md completely first.

This repo is the product layer for distributed mcpd3, branded as mcpd4.
The experimental cut repo is /home/matt/software/graph-cuts-undirected and
should remain experimental.

mcpd3 is a submodule at third_party/mcpd3 on public branch
distributed-mvp-start:
https://github.com/vvhitedog/mcpd3/tree/distributed-mvp-start

Goal for this session:
Start the network-free MVP. Inspect mcpd3's DualDecomposition internals and
propose/implement the first narrow API extraction needed for an
InProcessPartitionWorker equivalence test. Do not add TCP yet. Keep changes
small and upstreamable.
```

## Non-goals For The Next Agent

- Do not implement MPI first.
- Do not add TCP before the in-process worker API is proven.
- Do not port benchmark experiments from `graph-cuts-undirected` wholesale.
- Do not reintroduce Polyak step policy.
- Do not claim exactness on regularized agreement.
- Do not make this repo depend on local data files from the experimental repo.

## Immediate Definition Of Done

The next useful checkpoint is:

1. mcpd3 exposes a `PartitionWorker`-like in-process API.
2. Existing in-process `DualDecomposition` behavior can be reproduced through
   that API on tiny graphs.
3. The product repo has a test or example invoking that API through the
   submodule.
4. No TCP yet.
