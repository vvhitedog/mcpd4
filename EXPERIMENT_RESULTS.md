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
```
