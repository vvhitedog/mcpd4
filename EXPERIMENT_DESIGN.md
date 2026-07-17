# Partitioned Maximum-Preflow Design

## Experiment Boundary

This branch tests a partitioned highest-label push-relabel algorithm. It is not
yet a TCP or parallel-worker implementation. The local phase is represented as
a separate operation over arcs whose endpoints share a partition, so it can be
split among workers later without changing the coordinator protocol.

The implementation uses the algorithmic structure and data-layout principles
of the IG Systems `hi_pr` implementation: contiguous residual arcs, current-arc
scans, highest-label active buckets, gap retirement, and reverse-BFS global
relabeling. It does not copy that source into MCPD4 because upstream's copyright
notice says commercial use requires a license.

## Graph Split

Each nonterminal vertex belongs to exactly one partition. A positive-capacity
arc is a boundary arc exactly when both endpoints are nonterminals in different
partitions. Both residual directions of that arc remain coordinator-owned.

A vertex incident to a boundary arc is a boundary vertex. During a local phase:

- all pushes use only non-boundary residual arcs;
- a boundary vertex may push through admissible local arcs;
- a boundary vertex cannot relabel;
- a non-boundary vertex can relabel because every residual arc incident to it
  is local;
- source and sink arcs are local to the nonterminal endpoint.

## Coordination Round

1. The coordinator runs reverse BFS from the sink over the complete residual
   graph. A vertex label is its exact residual distance to the sink, or `n` if
   the sink is unreachable. The source label is fixed to `n`.
2. In descending label order, the coordinator pushes excess over admissible
   boundary arcs. A push from label `k` goes to label `k-1`.
3. The local phase performs highest-label discharge using local arcs. Interior
   vertices use current-arc relabeling and the gap heuristic. Boundary labels
   stay fixed until the next coordinator BFS.
4. If a positive-excess nonterminal still has a finite label, another round is
   required. Otherwise the state is a maximum preflow.

An optional work limit can interrupt local discharge and request an earlier
global BFS. It is off by default because full-graph BFS was more expensive than
the local work saved on the tested Waterloo graph.

## Correctness Argument

After global relabeling, labels satisfy the residual validity condition:

```text
d(t) = 0
d(u) <= d(v) + 1 for every residual arc u -> v
```

Coordinator and local pushes use only admissible arcs, where
`d(u) = d(v) + 1`. Such pushes preserve label validity. A local relabel is
performed only by a non-boundary vertex, so its scan covers every incident
residual arc; assigning one plus the minimum neighboring label is the standard
valid push-relabel operation. Boundary labels never change locally. The gap
heuristic retires only labels above an empty level, as in standard push-relabel.

Run a final reverse BFS and define:

```text
T = vertices that can reach the sink in the residual graph
S = V without T
```

There can be no positive residual arc from `S` to `T`; otherwise its tail could
also reach the sink. Therefore every original arc from `S` to `T` is saturated.
Likewise, no positive flow can remain on an original arc from `T` to `S`, since
that flow would create a positive reverse residual arc from `S` to `T`.

At termination, every nonterminal in `T` has zero excess. Summing the preflow
balance over `T` therefore makes the net flow entering `T` equal the sink
excess. By the two cut observations above, that net flow also equals the
capacity of the cut `(S,T)`. Thus:

```text
sink excess = cut capacity
```

The standard preflow-to-flow conversion can return stranded excess along
positive-flow paths toward the source without changing sink excess. Therefore
a feasible flow with this sink value exists. Its value equals the displayed cut
capacity, so weak duality proves that the flow is maximum and `(S,T)` is a
minimum cut. Performing that conversion is unnecessary for the cut or objective
certificate.

## Remaining Distribution Work

- Store each partition's local residual arcs and node state on its worker.
- Run local discharge concurrently and return only changed boundary excess,
  labels, and residual capacities.
- Replace or distribute the coordinator's full residual BFS. On bunny P8,
  repeated global BFS already dominates serial wall time after local work is
  reduced.
- Define ownership and atomic exchange for reverse residual capacity on every
  boundary arc.
