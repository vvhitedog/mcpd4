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
