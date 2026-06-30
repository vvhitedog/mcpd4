# Distributed mcpd3 MVP Tracker

Last updated: 2026-06-29 19:57 PDT

This document tracks the path from the current network-free checkpoint to a
usable localhost distributed MVP. Chronological implementation notes live in
`PROGRESS_LOG.md`; known failures, constraints, and taboos live in
`FAILED_APPROACHES.md`.

## Current Branches

- Product repo: `network-free-worker-api`
- mcpd3 submodule: `partition-worker-api`
- Current submodule checkpoint: `c398487 Decouple objective scale from DD step size`

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
  lexicographic regularized agreement, and non-certificate no-progress stops.

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
- [ ] Find and test at least one real convergence case where low-scale
  lexicographic regularization succeeds and the same low-scale unregularized
  schedule does not. Current source-biased placement has not demonstrated
  this.
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
- [ ] Implement a hardened OG-style regularization mode with an explicit
  summed global budget check:
  `global_regularization_budget < objective_scale`.
- [ ] Decide whether symmetric alpha-shift should replace local
  lexicographic regularization as the coordinator default, and whether
  randomized initial alphas should remain diagnostic-only, after broader
  stress testing.
- [ ] Decide and implement how optional primal upper-bound decoding maps to
  workers. MVP may keep it disabled, but behavior must be explicit.
- [ ] Support one worker object/process owning multiple partition packages.
- [ ] Add tests for multi-package worker ownership.

### Stage 3: Product Serialization

- [ ] Add product-repo serialization module.
- [ ] Implement length-prefixed framing:
  `uint32 message_type`, `uint64 payload_bytes`, `payload`.
- [ ] Use explicit little-endian encodings.
- [ ] Serialize/deserialize:
  - `HELLO`;
  - `PARTITION_PACKAGE`;
  - `READY`;
  - `SOLVE_ROUND_REQUEST`;
  - `SOLVE_ROUND_RESULT`;
  - `ALPHA_UPDATE`;
  - `STOP`;
  - `ERROR`.
- [ ] Add round-trip unit tests for every message type.
- [ ] Add malformed-frame and truncated-payload tests.

### Stage 4: TCP Loopback Runtime

- [ ] Add `mcpd3_coordinator` binary in this product repo.
- [ ] Add `mcpd3_worker` binary in this product repo.
- [ ] Implement worker `HELLO` with protocol version, worker name, CPU count,
  RAM GB, feature bits, temp path, and debug build/endianness fields.
- [ ] Implement coordinator wait-for-workers and timeout behavior.
- [ ] Implement MVP partition assignment, initially round-robin.
- [ ] Send partition packages once during setup.
- [ ] Implement per-round solve request, result gather, alpha update, and stop
  broadcast.
- [ ] Ensure graph structure is not resent during optimization rounds.
- [ ] Add clear worker-side and coordinator-side error messages.

### Stage 5: Correctness And Integration Tests

- [ ] Add committed tiny DIMACS fixtures:
  - hand bottleneck graph;
  - dead-end graph;
  - random small graph.
- [ ] Add in-process reference tests for each fixture.
- [ ] Add localhost coordinator/worker integration tests for each fixture.
- [ ] Compare distributed and in-process behavior:
  - final best lower bound;
  - final disagreement count;
  - stopping status;
  - regularization diagnostics.
- [ ] Add optional local benchmark hook for bunny data without making the repo
  depend on local experimental files.

### Stage 6: Failure Handling And Operational Readiness

- [ ] Handle worker disconnect before setup completes.
- [ ] Handle worker disconnect during optimization.
- [ ] Handle malformed protocol messages.
- [ ] Handle worker-reported solver errors.
- [ ] Add coordinator timeout behavior with tests.
- [ ] Add logging that identifies worker name, partition ids, round id, and
  message type for failures.

### Stage 7: Docs And Runbook

- [ ] Document build steps for product repo and mcpd3 submodule.
- [ ] Document localhost coordinator/worker invocation.
- [ ] Document test commands.
- [ ] Document current MVP limitations.
- [ ] Document exactness semantics:
  - zero disagreement with zero regularization can be exact;
  - lexicographic regularized agreement can be exact when local workers solve
    `M * F(x) + R(x)` and report `F(x)`;
  - heuristic/additive regularized agreement is not an exact certificate;
  - patience, group stopping, timeout, and no-progress stops are not exact
    certificates.

## Current Limitations

- No TCP transport exists yet.
- No product serialization exists yet.
- `PartitionWorkerCoordinator` currently sends all alpha records every round.
- `PartitionWorkerCoordinator` currently has one package per worker object.
- Source-side lexicographic regularization can be redundant in simple local
  ties because the current maxflow implementation already chooses source in
  those cases.
- A hand-derived one-node cycle (`source=-10`, `target=+8`, step `10`) is a
  scale-schedule case: step `10` cycles, but continuing to step `1` reaches
  agreement. This is not a strict regularization-required example because a
  forced-unregularized step `1` run also reaches agreement.
- Experimental `SYMMETRIC_ALPHA_SHIFT` resolves fixed step-`10` variants of
  that cycle by making `+/-10` alpha updates into `+/-9` updates. It has only
  been tested on committed tiny synthetic cases so far.
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
  original behavior in the checked window.
- The OG regularization idea is exact under the lattice proof when agreement
  holds and the summed effective regularization range is below the objective
  scale. The OG implementation has the right shape but does not fully enforce
  this condition because the budget check is local, warning-only, and can miss
  stale incremental perturbations.
- `adhead.n6c10` is available locally but has not been solved because the
  current machine is memory constrained for a graph that size.
- Primal upper-bound decoding is not yet mapped into the worker-coordinator
  path.

## Non-Goals Until This MVP Is Done

- Do not add MPI first.
- Do not reintroduce Polyak step policy.
- Do not move product TCP/runtime concerns into `third_party/mcpd3`.
- Do not make committed tests depend on local data under
  `/home/matt/software/graph-cuts-undirected`.
