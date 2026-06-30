# Distributed mcpd3 MVP Tracker

Last updated: 2026-06-30 00:12 PDT

This document tracks the path from the current network-free checkpoint to a
usable localhost distributed MVP. Chronological implementation notes live in
`PROGRESS_LOG.md`; known failures, constraints, and taboos live in
`FAILED_APPROACHES.md`.

## Current Branches

- Product repo: `network-free-worker-api`
- mcpd3 submodule: `partition-worker-api`
- Current submodule checkpoint:
  `f58cf42 Dispatch worker solves concurrently`

## MVP Definition

The MVP is complete when:

- a coordinator process can read and partition a DIMACS graph;
- worker processes can connect over TCP and receive one or more partition
  packages;
- workers can solve assigned local subproblems through the extracted mcpd3
  worker API;
- the coordinator can run the repeated solve/gather/update/scatter loop without
  resending graph structure;
- localhost integration tests show distributed results match current
  in-process mcpd3 behavior on committed tiny and small fixtures;
- stopping output distinguishes exact unregularized agreement, exact
  budget-safe scaled-epsilon regularized agreement, over-budget
  promotion/rejection, and non-certificate no-progress stops.

## Progress Checklist

### Stage 0: Build And Project Baseline

- [x] Create product branch for network-free/distributed MVP work.
- [x] Create upstreamable mcpd3 branch from `distributed-mvp-start`.
- [x] Add durable progress and failed-approaches logs.
- [x] Verify `dimacs_dual_decomp_example` builds from this repo.
- [x] Remove stale Boost hash dependency that blocked the example build.

### Stage 1: Network-Free Partition Worker API

- [x] Add mcpd3 CTest harness.
- [x] Add `PartitionPackage`, `ConstraintEndpointBinding`,
  `PartitionSolveRequest`, `PartitionSolveResult`, and `ConstraintLabel`.
- [x] Add `PartitionWorker` interface.
- [x] Add `InProcessPartitionWorker`.
- [x] Test worker results against direct `PrimalDualMinCutSolver` behavior
  across an alpha update.
- [x] Expose `DualDecomposition::getPartitionPackages()`.
- [x] Test exported packages against one existing `DualDecomposition` round on
  a tiny graph.
- [x] Add product-level smoke test that compiles against the submodule worker
  API.

### Stage 2: Network-Free Coordinator Loop

- [x] Add `PartitionWorkerCoordinator`.
- [x] Coordinator owns alpha and momentum state.
- [x] Coordinator gathers lower-bound, regularization, and constrained-label
  results.
- [x] Coordinator applies fixed-step alpha updates matching
  `DualDecomposition`.
- [x] Test two coordinator rounds against existing `DualDecomposition` on a
  tiny graph.
- [x] Extend coordinator from round primitive to full solve loop:
  - scale loop;
  - best lower bound tracking;
  - iteration caps;
  - patience and no-progress stopping;
  - group stopping;
  - regularization diagnostics;
  - progress output parity.
- [x] Harden low-scale regularization as an exact lexicographic tie-break:
  - no regularization above step size `10`;
  - solve regularized local subproblems as `M * F(x) + R(x)`;
  - preserve strict local optima;
  - report unregularized lower-bound terms;
  - treat lexicographic regularized agreement as optimal.
- [x] Resolve the local-lexicographic convergence search for this MVP slice:
  no strict-need case was found, and the productized default moved to OG-style
  scaled epsilon with explicit exactness/budget handling.
- [x] Add an experimental symmetric DD alpha-shift scheme and test it on
  fixed-scale cycle cases where local lexicographic regularization remains
  disagreeing.
- [x] Add an experimental seedable randomized initial-alpha option and test
  both a hit seed and a miss seed on fixed-scale cycle cases.
- [x] Add benchmark controls for the legacy `DualDecomposition` path:
  selectable regularization scheme, symmetric alpha shift size, randomized
  initial-alpha radius/seed, and directed streaming DIMACS input.
- [x] Run local directed Waterloo `BL06-gargoyle-med` checks for no-reg,
  randomized initial alpha, symmetric alpha shift, and local lexicographic
  modes. In the checked 10-partition basic setup, no-reg already reached
  exact agreement and matched the `.sol` value after adding the reader's
  terminal imbalance offset.
- [x] Remove default benchmark capacity premultiplication and add objective
  scale tests so reported values are independent of DD step size.
- [x] Screen Waterloo `babyface.n6c10` under unscaled and compatibility scaled
  settings. It did not produce a clean regularization-required case in the
  checked runs.
- [x] Compare `babyface.n6c10` against the original `early_experiments`
  branch. The original branch converges with zero disagreement, so the current
  exact regularization path needs follow-up.
- [x] Write the exactness proof for the original scaled epsilon regularization
  idea and review the OG implementation against it.
- [x] Implement a hardened OG-style scaled-epsilon regularization mode with an
  explicit summed global active-budget diagnostic:
  `global_regularization_budget < objective_scale`.
- [x] Replace warning-only over-budget behavior in the current monolithic
  `DualDecomposition` benchmark path with objective-scale promotion: reject
  the over-budget iteration's lower bound, scale the objective by `10x`,
  preserve residual graphs and alphas, and restart the capacity-scaling
  schedule from the promoted scale.
- [x] Add equivalent over-budget handling to `PartitionWorkerCoordinator` and
  `InProcessPartitionWorker`: reject over-budget lower bounds, skip alpha
  updates for those rounds, rescale coordinator state plus live worker
  solvers, and restart from the promoted objective scale.
- [x] Add an in-process worker rescale hook for objective-scale promotion.
- [x] Benchmark the hardened scaled-epsilon mode on local
  `adhead.n6c10` against the OG `early_experiments` scheme. Current
  productized code and OG both reached raw lower bound `483730000` with zero
  disagreement; current wall time was `1:24.18`, OG wall time was `1:26.19`.
- [x] Decide regularization default for this MVP slice: use OG-style
  scaled-epsilon regularization, remove symmetric alpha-shift from the
  productized path, and keep randomized initial alphas diagnostic-only.
- [x] Decide how optional primal upper-bound decoding maps to workers for this
  MVP slice: workers do not persist or track a global upper bound. The
  coordinator may add explicit worker compute requests later when it needs
  primal information.
- [x] Support one worker object/process owning multiple partition packages.
- [x] Add tests for multi-package worker ownership.

### Stage 3: Product Serialization

- [x] Add product-repo serialization module.
- [x] Implement length-prefixed framing:
  `uint32 message_type`, `uint64 payload_bytes`, `payload`.
- [x] Use explicit little-endian encodings.
- [x] Serialize/deserialize:
  - `HELLO`;
  - `PARTITION_PACKAGE`;
  - `READY`;
  - `SOLVE_ROUND_REQUEST`;
  - `SOLVE_ROUND_RESULT`;
  - `SCALE_OBJECTIVE`;
  - `ALPHA_UPDATE`;
  - `STOP`;
  - `ERROR`.
- [x] Add round-trip unit tests for every message type.
- [x] Add malformed-frame and truncated-payload tests.

### Stage 4: TCP Loopback Runtime

- [x] Add `mcpd3_coordinator` binary in this product repo.
- [x] Add `mcpd3_worker` binary in this product repo.
- [x] Implement worker `HELLO` with protocol version, worker name, CPU count,
  RAM GB, feature bits, temp path, and debug build/endianness fields.
- [x] Implement coordinator wait-for-workers and accept-timeout behavior.
- [x] Implement MVP partition assignment, initially round-robin.
- [x] Send partition packages once during setup.
- [x] Implement per-round solve request, result gather, alpha update, and stop
  broadcast.
- [x] Implement remote objective-scale promotion with `SCALE_OBJECTIVE`.
- [x] Ensure graph structure is not resent during optimization rounds.
- [x] Add clear worker-side and coordinator-side error messages.
- [x] Add TCP loopback tests for framing, invalid handshakes, worker errors,
  objective scaling, regularized agreement, and objective-scale promotion.

### Stage 5: Correctness And Integration Tests

- [x] Add committed tiny DIMACS fixtures:
  - hand bottleneck graph;
  - dead-end graph;
  - random small graph.
- [x] Add in-process reference tests for each fixture.
- [x] Add localhost coordinator/worker integration tests for each fixture.
- [x] Compare distributed and in-process behavior:
  - final best lower bound;
  - final disagreement count;
  - stopping status;
  - regularization diagnostics.
- [x] Add optional local benchmark hook for bunny data without making the repo
  depend on local experimental files.

### Stage 6: Failure Handling And Operational Readiness

- [ ] Handle worker disconnect before setup completes.
- [ ] Handle worker disconnect during optimization.
- [ ] Handle malformed protocol messages.
- [x] Handle worker-reported solver errors.
- [x] Add coordinator timeout behavior with tests.
- [x] Add streaming optimizer-health telemetry with worker names, round counts,
  per-worker solve timing, lower-bound progress, disagreement count, and
  regularization diagnostics.
- [ ] Add failure logging that identifies worker name, partition ids, round id,
  and message type for protocol/solver failures.

### Stage 7: Docs And Runbook

- [ ] Document build steps for product repo and mcpd3 submodule.
- [ ] Document localhost coordinator/worker invocation.
- [ ] Document test commands.
- [ ] Document current MVP limitations.
- [ ] Document exactness semantics:
  - zero disagreement with zero regularization can be exact;
  - scaled-epsilon regularized agreement can be exact when the summed active
    regularization budget is strictly below the objective scale;
  - over-budget regularized rounds are not certificates; current in-process
    paths reject those lower bounds and either promote objective scale or
    return `REGULARIZATION_BUDGET_EXCEEDED`;
  - patience, group stopping, timeout, and no-progress stops are not exact
    certificates.

## Current Limitations

- TCP runtime exists for the localhost/IPv4 MVP and exposes an explicit bind
  address, but it is still sequential and blocking. It does not yet implement
  reconnects, heartbeats, worker replacement, or partial-progress recovery.
- Process-level coordinator/worker tests now cover three committed tiny
  DIMACS fixtures, but no committed medium benchmark fixture exists. Local
  benchmark runs should use `scripts/run_local_process_benchmark.sh` with a
  user-supplied DIMACS path.
- `mcpd3_coordinator` now exposes `--accept-timeout-ms`, but it still has no
  worker reconnect, replacement, heartbeat, or partial-progress recovery.
- `PartitionWorkerCoordinator` currently sends all alpha records every round.
- `PartitionWorkerCoordinator`, `InProcessPartitionWorker`, and
  `TcpPartitionWorker` support one worker object/process owning multiple
  partition packages.
- Product coordinator solves are dispatched concurrently across active worker
  processes. If one worker owns multiple packages, that worker still solves
  its own package stream sequentially.
- `mcpd3_coordinator --progress-every N` streams per-round optimizer health
  and per-worker timing. On `adhead.n6c10`, this exposed severe static
  partition load imbalance: the first saturated telemetry-smoke round spent
  about `69.7 s` on one worker and `35.5 s` on another, while two workers were
  under `1 s`.
- `--saturate-capacity-overflow` is an opt-in benchmark/compatibility mode for
  the current 32-bit capacity path. Runs with nonzero
  `capacity_scale_saturation_count` solve a clipped-capacity problem, not the
  exact original capacities.
- Source-side lexicographic regularization can be redundant in simple local
  ties because the current maxflow implementation already chooses source in
  those cases.
- A hand-derived one-node cycle (`source=-10`, `target=+8`, step `10`) is a
  scale-schedule case: step `10` cycles, but continuing to step `1` reaches
  agreement. This is not a strict regularization-required example because a
  forced-unregularized step `1` run also reaches agreement.
- `SYMMETRIC_ALPHA_SHIFT` was useful experimental evidence, but it is no
  longer part of the productized regularization path. The productized path is
  now OG-style scaled epsilon plus optional randomized initial alphas.
- Experimental randomized initial alphas can also resolve those fixed
  step-`10` variants when the seed lands in a useful multiplier region, but a
  tested miss seed remains disagreeing. This is a one-shot perturbation, not a
  replacement for an adaptive regularizer.
- Waterloo `BL06-gargoyle-med` did not reproduce a regularization-required
  failure under the checked directed-streaming, 10-partition basic setup:
  no-reg reached gap zero and zero disagreement; randomized initial alpha and
  symmetric alpha shift were worse in the checked runs.
- Waterloo `babyface.n6c10` is now confirmed as an original-code convergence
  case: `early_experiments` reaches `19448` with zero disagreement. Current
  no-reg gets near the lower bound but leaves disagreement, while the current
  exact lexicographic regularizer is too slow and did not reproduce the
  original behavior in the checked window. The hardened scaled-epsilon path
  improves the productized behavior but still leaves disagreement under the
  default patience window (`best_lower_bound_raw=194479904`,
  `final_disagreement_count=187`).
- The OG regularization idea is exact under the lattice proof when agreement
  holds and the summed effective regularization range is strictly below the
  objective scale. The hardened in-process implementations now report the
  summed active budget, reject over-budget lower bounds, and promote objective
  scale when allowed.
- `adhead.n6c10` is the current benchmark comparison target. With
  `--capacity-multiplier 10000`, current scaled epsilon reached the known
  optimum `48373` with zero disagreement and active budget `40 < 10000`.
- Dynamic objective-scale promotion lets the monolithic benchmark path start
  `adhead.n6c10` at `--capacity-multiplier 100`, detect
  `budget=9840 >= 100`, promote to objective scale `1000`, and finish with
  `best_lower_bound_unscaled=48373`, `final_disagreement_count=0`, and
  `final_regularization_budget_raw=180 < 1000`. This was correct but slower
  than starting at `10000`.
- Dynamic objective-scale promotion is implemented in
  `PartitionWorkerCoordinator` for both in-process workers and TCP workers via
  the `SCALE_OBJECTIVE` message, preserving remote live solver state across
  promotion.
- The opposite-direction cycle has explicit coverage for starting at low
  objective scale: with `M=10`, enough promoted schedule depth, and sufficient
  unit-scale iterations, the coordinator promotes once to `M=100` and reaches
  agreement under budget.
- Primal upper-bound decoding is intentionally not persistent worker state for
  this MVP slice. If the coordinator needs primal information later, it should
  request explicit worker computation rather than enabling always-on worker
  upper-bound tracking.

## Non-Goals Until This MVP Is Done

- Do not add MPI first.
- Do not reintroduce Polyak step policy.
- Do not move product TCP/runtime concerns into `third_party/mcpd3`.
- Do not make committed tests depend on local data under
  `/home/matt/software/graph-cuts-undirected`.
