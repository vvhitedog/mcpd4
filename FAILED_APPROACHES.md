# Failed Approaches And Taboos

## 2026-06-29 00:24:08 PDT

- Do not add TCP, MPI, or serialization before the in-process partition-worker
  API has an equivalence test.
- Do not move product deployment/runtime concerns into `third_party/mcpd3`.
- Do not reintroduce the removed Polyak step policy.
- Do not depend on local benchmark files from
  `/home/matt/software/graph-cuts-undirected`.
- Do not claim exact min-cut optimality from regularized agreement alone.

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
