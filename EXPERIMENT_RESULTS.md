# Partitioned hi_pr Experiment Results

## Scope

These are serial CPU prototype measurements. Local partition discharge is
separated algorithmically but is not yet run concurrently. Times include each
solver's residual-graph construction and solve, but exclude DIMACS parsing and
graph partitioning. All reported partitioned results matched BK's objective and
returned cut value.

The implementation follows the maximum-preflow stage: nonterminal excess that
cannot reach the sink is retained. A final full residual reverse BFS defines
the weak-source side and certifies the cut.

## Release Benchmark Results

Measurements were collected on 2026-07-17 with 32-bit capacities. Values below
are median wall times unless a single run is identified.

| Graph | Partitioning | Parts | Partitioned PR | BK | PR / BK |
|---|---:|---:|---:|---:|---:|
| grid 128x128 | contiguous | 2 | 34.729 ms | 6.392 ms | 5.43x |
| grid 128x128 | contiguous | 4 | 33.431 ms | 6.016 ms | 5.56x |
| grid 128x128 | contiguous | 8 | 29.370 ms | 6.092 ms | 4.82x |
| grid 128x128 | METIS | 2 | 38.110 ms | 6.251 ms | 6.10x |
| grid 128x128 | METIS | 4 | 34.681 ms | 6.032 ms | 5.75x |
| grid 128x128 | METIS | 8 | 33.150 ms | 6.486 ms | 5.11x |
| wide layered | contiguous | 2 | 2.065 ms | 8.924 ms | 0.23x |
| wide layered | contiguous | 4 | 2.888 ms | 8.870 ms | 0.33x |
| wide layered | contiguous | 8 | 3.223 ms | 9.778 ms | 0.33x |
| wide layered | METIS | 2 | 2.107 ms | 8.733 ms | 0.24x |
| wide layered | METIS | 4 | 3.392 ms | 9.646 ms | 0.35x |
| wide layered | METIS | 8 | 3.266 ms | 8.810 ms | 0.37x |
| Waterloo LB07 bunny sml | contiguous | 2 | 2.391 s | 0.244 s | 9.80x |
| Waterloo LB07 bunny sml | contiguous | 4 | 2.165 s | 0.247 s | 8.76x |
| Waterloo LB07 bunny sml | contiguous | 8 | 1.853 s | 0.247 s | 7.49x |
| Waterloo LB07 bunny sml | METIS | 4 | 2.751 s | 0.241 s | 11.41x |
| Waterloo LB07 bunny sml | METIS | 8 | 2.177 s | 0.240 s | 9.09x |

The grid reader reports a 65,152 terminal imbalance offset. Both implementations
were compared on the same normalized `MinCutGraph`; its exact reported value is
40,941 and the omitted offset does not affect the minimizing cut.

## Native Dual-Decomposition Comparison

A follow-up comparison used the same direct grid min-cut for partitioned PR and
native `mcpd3::DualDecomposition` (`mcpd3-n`). The 256x256 graph was generated
with the same `make_grid_graph` generator and seed 24 as the 128x128 fixture.
Both solvers used two basic partitions and 32-bit capacities. The mcpd3-n run
used two local threads and the current phase-solver defaults:
`objective_scale=10350`, initial step 690, five scales, patience 250, momentum
enabled, and group stopping disabled.

Parsing is excluded. The PR time includes residual construction and solve; its
separate partitioning time is shown in parentheses. The mcpd3-n time is the
median of DD construction plus solve. Each median is over five runs.

| Graph | Partitioned PR | mcpd3-n DD | PR / DD | Exact value |
|---|---:|---:|---:|---:|
| grid 128x128 | 19.393 ms (+0.077 ms partitioning) | 13.620 ms | 1.43x | 40,941 |
| grid 256x256 | 109.345 ms (+0.254 ms partitioning) | 23.152 ms | 4.73x | 163,756 |

Both methods reached agreement/certification and matched the exact objective.
The PR local phases are serial in this prototype, whereas mcpd3-n executes its
two local subproblems concurrently. These numbers therefore compare the current
CPU implementations, not a projected parallel PR implementation.

For context only, the separate physical phase-unwrapping benchmark records
whole-PU mcpd3-n medians near 270 ms at 128x128 (11 cut attempts) and 990 ms at
256x256 (21 cut attempts). Those whole-PU values must not be compared directly
with the one-cut PR values above until PR is integrated as a PU cut backend.

## Ghiglia-Pritt Book Cuts

The next comparison exports the first UP binary-move cut from a zero ambiguity
state on three published Ghiglia-Pritt datasets. Spiral, Head, and IFSAR are
classified as easy, medium, and hard from their prior complete four-partition
mcpd3-n PU walls (3.003 s, 5.927 s, and 43.457 s respectively). This label
describes the complete PU problem, not necessarily the first cut alone.

Both direct-cut solvers used two basic partitions and 32-bit capacities. PR
used its default serial local phase. Mcpd3-n used two local threads and the
phase defaults: objective scale 10,350, initial step 690, five scales, patience
250, momentum enabled, and group stopping disabled. Parsing is excluded. PR
time includes residual construction and solve plus the separately measured
partition setup; mcpd3-n is DD construction plus solve. Values are five-run
medians.

| Dataset | Variables | Exact cut | Partitioned PR | mcpd3-n DD | PR / DD |
|---|---:|---:|---:|---:|---:|
| Spiral (easy) | 66,049 | 2,028 | 99.301 ms | 38.980 ms | 2.55x |
| Head (medium) | 65,536 | 4,294 | 72.676 ms | 21.046 ms | 3.45x |
| IFSAR (hard) | 262,144 | 20,578 | 1,022.094 ms | 249.103 ms | 4.10x |

PR, its internal BK reference, and mcpd3-n returned the same objective on every
fixture. Additional invariant-enabled PR runs also converged with matching
preflow and cut certificates. These are single-cut measurements; PR still has
not been integrated into the full PU iteration.

## Multiple-Partitioning Cover Results

The cover extension was measured with four contiguous parts. `M1` is the
preserved coordinator-boundary implementation. `M2` uses two graph-separated
partitionings over shared residual state and performs no coordinator boundary
pushes. Times below exclude DIMACS parsing and one-time partition-cover
construction. They are three-run Release medians.

| Graph | M1 baseline | M2, initial/final BFS only | Change | Global BFS, M1 -> M2 |
|---|---:|---:|---:|---:|
| grid 128x128 | 21.899 ms | 18.636 ms | 14.9% faster | 11 -> 2 |
| Spiral first UP cut | 99.116 ms | 119.601 ms | 20.7% slower | 12 -> 2 |
| Head first UP cut | 69.541 ms | 92.553 ms | 33.1% slower | 7 -> 2 |
| IFSAR first UP cut | 846.980 ms | 935.839 ms | 10.5% slower | 10 -> 2 |

The grid cover had 768 boundary nodes per partitioning, no uncovered nodes or
arcs, and minimum boundary-set distance 13. Cover construction took roughly
3-6 ms and is expected to be amortized across repeated PU cuts. Repeated timing
batches placed the M2 improvement between 9% and 22%; the table uses a
representative same-session three-run pair. Four partitionings converged in one
cycle but took 20.1-22.6 ms in sampled batches, so more covers are not
automatically better.

The book regressions came from extra local work rather than BFS. M2 increased
local arc scans from 11.29M to 19.51M on Spiral, 6.20M to 11.63M on Head, and
120.09M to 161.41M on IFSAR. Work-triggered global relabeling recovered some of
that loss:

| Graph | Work factor | M2 cover | Change from M1 | Global BFS |
|---|---:|---:|---:|---:|
| Spiral | 4 | 95.338 ms | 3.8% faster | 10 |
| Head | 8 | 59.226 ms | 14.8% faster | 4 |
| IFSAR | 8 | 885.077 ms | 4.5% slower | 12 |

Spiral and Head covers had minimum boundary-set distance 27; IFSAR's was 56.
All had zero uncovered nodes/arcs, zero coordinator boundary pushes, and exact
objectives matching BK. There is no universal sampled work factor: the cover is
a valid scheduling mechanism and can reduce BFS cost, but it is not a general
performance win in its current serial form.

## Key Findings

- Bucketed gap retirement is essential. On bunny P1, replacing a full vertex
  scan per gap reduced wall time from 14.895 s to 1.157 s, a 12.9x improvement.
- The wide-layered graph is favorable to highest-label PR: this prototype is
  2.7x to 4.3x faster than BK even before local partition parallelism.
- Bunny is unfavorable. At contiguous P8, about 1.02 s is spent in 17 global
  BFS passes and 0.54 s in serial local discharge. Full-graph BFS is the main
  scaling bottleneck after parallelizing local work.
- Fewer crossing arcs did not predict better time. Bunny METIS P8 has fewer
  boundary arcs than contiguous P8 but more local pushes/relabels and is slower.
- Original-style periodic global relabel work limits did not transfer well to
  this coordinator design. On bunny P1, factors 0.5, 1, 2, and 4 all lost to
  waiting until local work blocked; repeated full-graph BFS dominated.
- Multiple separated partitionings remove the need for coordinator boundary
  pushes and can reduce global BFS frequency while preserving exactness. Their
  benefit depends on whether saved BFS work exceeds repeated local scans.

## Commands

```text
cmake -S . -B /home/matt/software/experiments/build-mcpd4-partitioned-hi-pr-release \
  -DCMAKE_BUILD_TYPE=Release -DMCPD4_ENABLE_SNAPPY=OFF \
  -DMCPD_BOOST_INCLUDE_DIR=/home/matt/.local/boost-dev/usr/include \
  -DMETIS_INCLUDE_DIRS=/home/matt/.local/metis-dev/usr/include \
  -DMETIS_LIBRARIES=/lib/x86_64-linux-gnu/libmetis.so.5.1.0
cmake --build /home/matt/software/experiments/build-mcpd4-partitioned-hi-pr-release \
  --target mcpd4_partitioned_hi_pr_benchmark -j 8
./mcpd4_partitioned_hi_pr_benchmark GRAPH.max --symmetric-streaming \
  --partitions 8 --partitioner basic --repeats 3
./mcpd4_partitioned_hi_pr_benchmark GRAPH.max --directed \
  --partitions 4 --partitionings 2 --repeats 3 \
  --global-relabel-work-factor 8
```
