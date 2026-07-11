# Progress Log

## 2026-07-11 05:50 PDT

- Ran full local TCP schedule probes for the current best single-scale point
  (`babyface.n6c10`, p15/w15, malloc-backed BK storage, no compression,
  schedule start `10000`, five schedule levels, max `20` iterations per
  scale).
- Strict overflow mode:
  `benchmark_results/local_tcp_fullschedule_p15_w15_malloc_babyface_none_20260711_054117`
  reached total_iteration `51`, then attempted objective-scale promotion after
  regularization budget pressure and failed with
  `objective scale promotion exceeds int`. Last status snapshot reported
  schedule scale `100`, objective scale `1000`, `398,414` disagreements, and
  solve elapsed `113,980,430us`.
- Saturating overflow mode:
  `benchmark_results/local_tcp_fullschedule_p15_w15_malloc_babyface_saturate_20260711_054457`
  completed without crashing:
  - status `2` / stop_reason `3`, both `ITERATION_COUNT_EXCEEDED`;
  - final objective scale `100000` after `2` promotions;
  - total iterations `195`;
  - final disagreements `3,099`;
  - best certified lower bound `19,369.2`
    (`best_certified_lower_bound_raw=1,936,919,910`);
  - total wall `293,768,552us`, coordinator setup `4,939,864us`, solve wall
    `285,146,898us`;
  - aggregate worker solve `2,043,549,031us`, worker RPC overhead
    `67,877,700us`, partition-load RPC `62,990,600us`;
  - solve request TX `311,366,120` bytes, solve result RX `298,942,225`
    bytes.
- Conclusion: freeing memory makes the malloc-backed p15/w15 full-schedule run
  stable, and `--saturate-capacity-overflow` correctly keeps objective-scale
  promotion alive, but the current `20`-iteration-per-scale cap does not reach
  agreement. The next convergence experiment should raise the per-scale
  iteration cap or adjust the schedule; the current run is a useful baseline,
  not a solved local TCP target.

## 2026-07-11 05:40 PDT

- Increased the coordinator TCP listen backlog from the default `16` to
  `max(16, configured_worker_count)`. This matters for local runs with more
  than 16 workers, where all workers can connect while the coordinator is still
  reading/partitioning after the early-listen change.
- Verified:
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`.
- Benchmarked high-worker-count local TCP runs after the backlog fix:
  - p17/w17
    `benchmark_results/local_tcp_backlog_worker_count_10iter_p17_w17_malloc_babyface_none_20260711_053820`:
    total `29,037,329us`, setup `5,429,100us`, solve `19,841,111us`;
    previous p17/w17 was total `30,475,038us`, setup `6,077,330us`;
  - p18/w18
    `benchmark_results/local_tcp_backlog_worker_count_10iter_p18_w18_malloc_babyface_none_20260711_053933`:
    total `30,636,137us`, setup `5,715,576us`, solve `21,134,055us`;
    previous p18/w18 was total `31,844,764us`, setup `6,469,179us`;
  - p20/w20
    `benchmark_results/local_tcp_backlog_worker_count_10iter_p20_w20_malloc_babyface_none_20260711_053850`:
    total `31,438,987us`, setup `7,110,401us`, solve `20,426,273us`;
    previous p20/w20 was total `31,504,549us`, setup `7,108,028us`.
- Conclusion: keep the backlog fix. It does not change the current best
  p15/w15 point, but it removes an artificial listener bottleneck for runs with
  more than 16 workers and improves p17/p18 materially.

## 2026-07-11 05:29 PDT

- Refined the 10-iteration local TCP partition/worker count around the p16
  knee. Settings: `babyface.n6c10`, one objective scale, malloc-backed BK
  storage, no compression.
- Narrow partition-count sweep:
  - p15/w15
    `benchmark_results/local_tcp_early_listen_10iter_p15_w15_malloc_babyface_none_20260711_052715`:
    total `27,571,126us`, setup `5,024,530us`, solve `18,848,132us`,
    final disagreements `443,273`;
  - p15/w15 repeat
    `benchmark_results/local_tcp_early_listen_10iter_repeat_p15_w15_malloc_babyface_none_20260711_052830`:
    total `27,700,308us`, setup `4,867,008us`, solve `19,180,430us`,
    final disagreements `443,273`;
  - p17/w17
    `benchmark_results/local_tcp_early_listen_10iter_p17_w17_malloc_babyface_none_20260711_052743`:
    total `30,475,038us`, setup `6,077,330us`, solve `20,628,333us`,
    final disagreements `467,502`.
- Tested more partitions while keeping 16 worker processes:
  - p18/w16
    `benchmark_results/local_tcp_early_listen_10iter_p18_w16_malloc_babyface_none_20260711_052527`:
    total `30,903,147us`, setup `6,457,761us`, solve `20,632,537us`;
  - p20/w16
    `benchmark_results/local_tcp_early_listen_10iter_p20_w16_malloc_babyface_none_20260711_052558`:
    total `31,567,904us`, setup `7,453,603us`, solve `20,220,629us`;
  - p24/w16
    `benchmark_results/local_tcp_early_listen_10iter_p24_w16_malloc_babyface_none_20260711_052630`:
    total `32,799,228us`, setup `8,691,633us`, solve `20,067,984us`.
- Conclusion: p15/w15 is now the best observed local TCP point for this
  10-iteration benchmark. It slightly beats p16/w16 by lowering setup while
  keeping solve wall in the same band, and it also has fewer final
  disagreements on this probe (`443,273` vs `499,015`). Splitting into more
  than 16 partitions while keeping 16 workers is not helpful; setup and RPC
  overhead dominate any solve-wall reduction.
- Checked p15/w15 against p16/w16 with `--max-iterations 20`; both stopped
  after 11 total iterations with `stop_reason 4` under the existing schedule
  stopping logic:
  - p15/w15
    `benchmark_results/local_tcp_early_listen_20iter_p15_w15_malloc_babyface_none_20260711_053009`:
    total `29,852,306us`, setup `4,945,809us`, solve `21,232,761us`,
    final disagreements `439,228`;
  - p16/w16
    `benchmark_results/local_tcp_early_listen_20iter_p16_w16_malloc_babyface_none_20260711_053040`:
    total `31,001,332us`, setup `5,750,316us`, solve `21,534,772us`,
    final disagreements `490,724`.
- This longer run still favors p15/w15.

## 2026-07-11 05:22 PDT

- Swept worker count at fixed p16 for the 10-iteration local TCP
  `babyface.n6c10` run. Settings: one objective scale, malloc-backed BK
  storage, no compression.
- Results:
  - p16/w4
    `benchmark_results/local_tcp_early_listen_10iter_p16_w4_malloc_babyface_none_20260711_052209`:
    total `29,934,232us`, setup `6,808,441us`, solve `19,442,707us`,
    active workers `4`, solve batch RPCs `40`;
  - p16/w6
    `benchmark_results/local_tcp_early_listen_10iter_p16_w6_malloc_babyface_none_20260711_052031`:
    total `28,529,368us`, setup `5,495,884us`, solve `19,313,608us`,
    active workers `6`, solve batch RPCs `60`;
  - p16/w8
    `benchmark_results/local_tcp_early_listen_10iter_p16_w8_malloc_babyface_none_20260711_051852`:
    total `28,307,002us`, setup `5,486,728us`, solve `19,074,716us`,
    active workers `8`, solve batch RPCs `80`;
  - p16/w8 repeat
    `benchmark_results/local_tcp_early_listen_10iter_p16_w8_malloc_babyface_none_20260711_052059`:
    total `29,249,490us`, setup `6,115,888us`, solve `19,418,493us`;
  - p16/w10
    `benchmark_results/local_tcp_early_listen_10iter_p16_w10_malloc_babyface_none_20260711_052129`:
    total `29,273,367us`, setup `5,946,224us`, solve `19,635,765us`;
  - p16/w12
    `benchmark_results/local_tcp_early_listen_10iter_p16_w12_malloc_babyface_none_20260711_051920`:
    total `29,444,918us`, setup `6,000,226us`, solve `19,718,980us`;
  - p16/w14
    `benchmark_results/local_tcp_early_listen_10iter_p16_w14_malloc_babyface_none_20260711_051950`:
    total `29,620,057us`, setup `6,490,634us`, solve `19,420,748us`.
- Baseline p16/w16 clean-source validation remained faster:
  `benchmark_results/local_tcp_clean_rebuild_10iter_p16_w16_malloc_babyface_none_20260711_051706`
  total `27,822,050us`, setup `5,164,564us`, solve `18,954,568us`.
- Conclusion: p16/w16 remains the best observed local TCP wall time for this
  10-iteration probe. p16/w6 and p16/w8 are close alternatives when reducing
  process count or memory pressure matters, because batched worker RPCs preserve
  most of the per-round parallelism.

## 2026-07-11 05:13 PDT

- Ran a 10-iteration local TCP sweep on `babyface.n6c10` to avoid optimizing
  only for setup time. Settings: one objective scale, malloc-backed BK storage,
  no compression, one worker per partition.
- Key result: p3/w3 is best for a one-iteration setup-dominated probe, but it
  is much worse once solve rounds are repeated. p16/w16 is the best observed
  10-iteration point on this machine.
- Results:
  - p3/w3
    `benchmark_results/local_tcp_early_listen_10iter_p3_w3_malloc_babyface_none_20260711_050447`:
    total `121,345,303us`, setup `2,037,059us`, solve
    `116,126,091us`, final disagreements `162,685`;
  - p6/w6
    `benchmark_results/local_tcp_early_listen_10iter_p6_w6_malloc_babyface_none_20260711_050648`:
    total `65,678,122us`, setup `2,233,575us`, solve
    `60,148,854us`, final disagreements `230,773`;
  - p8/w8
    `benchmark_results/local_tcp_early_listen_10iter_p8_w8_malloc_babyface_none_20260711_050812`:
    total `51,423,203us`, setup `2,985,054us`, solve
    `45,073,636us`, final disagreements `312,976`;
  - p10/w10
    `benchmark_results/local_tcp_early_listen_10iter_p10_w10_malloc_babyface_none_20260711_050903`:
    total `34,394,785us`, setup `3,756,691us`, solve
    `27,171,299us`, final disagreements `436,053`;
  - p12/w12
    `benchmark_results/local_tcp_early_listen_10iter_p12_w12_malloc_babyface_none_20260711_050938`:
    total `30,284,165us`, setup `4,323,281us`, solve
    `22,402,263us`, final disagreements `455,590`;
  - p14/w14
    `benchmark_results/local_tcp_early_listen_10iter_p14_w14_malloc_babyface_none_20260711_051141`:
    total `30,269,646us`, setup `4,582,792us`, solve
    `22,044,017us`, final disagreements `346,570`;
  - p16/w16
    `benchmark_results/local_tcp_early_listen_10iter_p16_w16_malloc_babyface_none_20260711_051029`:
    total `27,878,130us`, setup `5,153,077us`, solve
    `19,015,852us`, final disagreements `499,015`;
  - p16/w16 repeat
    `benchmark_results/local_tcp_early_listen_10iter_repeat_p16_w16_malloc_babyface_none_20260711_051254`:
    total `28,369,133us`, setup `5,185,741us`, solve
    `19,487,864us`, final disagreements `499,015`;
  - p16/w16 clean-source rebuild validation
    `benchmark_results/local_tcp_clean_rebuild_10iter_p16_w16_malloc_babyface_none_20260711_051706`:
    total `27,822,050us`, setup `5,164,564us`, solve
    `18,954,568us`, final disagreements `499,015`;
  - p18/w18
    `benchmark_results/local_tcp_early_listen_10iter_p18_w18_malloc_babyface_none_20260711_051211`:
    total `31,844,764us`, setup `6,469,179us`, solve
    `21,275,144us`, final disagreements `793,515`;
  - p20/w20
    `benchmark_results/local_tcp_early_listen_10iter_p20_w20_malloc_babyface_none_20260711_051058`:
    total `31,504,549us`, setup `7,108,028us`, solve
    `20,236,423us`, final disagreements `729,405`.
- Conclusion: for setup microbenchmarks, use p3/w3; for actual iterative local
  TCP solves on this machine, use roughly p16/w16 as the current best point.
  More partitions reduce per-round wall time until around p16, after which RPC
  and worker-load overhead dominate.
- Compared against the in-process worker path at the same p16/w16, 10-iteration
  settings:
  `benchmark_results/inprocess_10iter_p16_w16_malloc_babyface_20260711_051521`
  reported total `30,898,554us`, setup `8,526,738us`, solve `18,742,807us`,
  final disagreements `499,015`.
- Interpretation: local TCP is not losing to in-process here. Solve wall is
  similar, and local TCP setup is faster, likely because worker process
  isolation lets partition loading/allocation proceed with less same-process
  memory pressure.

## 2026-07-11 04:54 PDT

- After freeing memory, reran the current early-listen local TCP partition
  sweep on `babyface.n6c10`: one iteration, malloc-backed BK storage, no
  compression, one worker per partition. System memory at the time was about
  `12GiB` available.
- Results:
  - p1/w1
    `benchmark_results/local_tcp_early_listen_p1_w1_malloc_babyface_none_20260711_045149`:
    total `10,172,368us`, setup `1,681,198us`, solve `5,334,753us`,
    partition-load TX `384,750,040` bytes;
  - p2/w2
    `benchmark_results/local_tcp_early_listen_sweep_p2_w2_malloc_babyface_none_20260711_045251`:
    total `10,226,668us`, setup `2,253,123us`, solve `4,817,758us`;
  - p3/w3
    `benchmark_results/local_tcp_early_listen_sweep_p3_w3_malloc_babyface_none_20260711_045302`:
    total `6,414,628us`, setup `2,048,162us`, solve `1,185,389us`,
    partition-load TX `394,875,120` bytes;
  - p4/w4
    `benchmark_results/local_tcp_early_listen_sweep_p4_w4_malloc_babyface_none_20260711_045308`:
    total `6,599,280us`, setup `1,979,474us`, solve `1,393,322us`;
  - p5/w5
    `benchmark_results/local_tcp_early_listen_sweep_p5_w5_malloc_babyface_none_20260711_045315`:
    total `8,516,844us`, setup `2,080,898us`, solve `3,178,352us`;
  - p6/w6
    `benchmark_results/local_tcp_early_listen_sweep_p6_w6_malloc_babyface_none_20260711_045324`:
    total `6,430,783us`, setup `2,213,397us`, solve `895,838us`;
  - p7/w7
    `benchmark_results/local_tcp_early_listen_sweep_p7_w7_malloc_babyface_none_20260711_045330`:
    total `7,022,121us`, setup `2,898,015us`, solve `768,757us`;
  - p8/w8
    `benchmark_results/local_tcp_early_listen_p8_w8_malloc_babyface_none_20260711_045215`:
    total `7,639,330us`, setup `2,975,196us`, solve `1,286,668us`;
  - p10/w10
    `benchmark_results/local_tcp_early_listen_p10_w10_malloc_babyface_none_20260711_045223`:
    total `7,708,537us`, setup `3,522,568us`, solve `727,304us`;
  - p12/w12
    `benchmark_results/local_tcp_early_listen_p12_w12_malloc_babyface_none_20260711_045231`:
    total `8,251,951us`, setup `4,006,850us`, solve `738,640us`.
- Repeated p3/w3 and p6/w6 because they were close:
  - p3 repeats:
    `benchmark_results/local_tcp_early_listen_repeat_p3_w3_malloc_babyface_none_20260711_045348`
    total `6,438,462us`;
    `benchmark_results/local_tcp_early_listen_repeat_p3_w3_malloc_babyface_none_20260711_045355`
    total `6,394,721us`;
    `benchmark_results/local_tcp_early_listen_repeat_p3_w3_malloc_babyface_none_20260711_045401`
    total `6,389,020us`;
  - p6 repeats:
    `benchmark_results/local_tcp_early_listen_repeat_p6_w6_malloc_babyface_none_20260711_045408`
    total `6,470,651us`;
    `benchmark_results/local_tcp_early_listen_repeat_p6_w6_malloc_babyface_none_20260711_045414`
    total `6,458,023us`;
    `benchmark_results/local_tcp_early_listen_repeat_p6_w6_malloc_babyface_none_20260711_045421`
    total `6,444,772us`.
- Conclusion: with the current memory state and early-listen coordinator, p3/w3
  is the best observed local TCP point for this one-iteration babyface probe.
  It wins by keeping package-load/setup lower; p6 solves faster but pays more
  worker-load wall time. The margin is small, so p3 and p6 should both remain
  candidates for larger or multi-iteration runs.

## 2026-07-11 04:48 PDT

- Moved fixed-worker TCP listener setup before graph read/partitioning in the
  coordinator. This lets `scripts/run_local_process_benchmark.sh` start local
  worker processes while the coordinator is still reading and partitioning the
  input. Discovery mode still opens TCP/discovery after partitioning, preserving
  the existing discovery-ready behavior.
- Added process integration coverage with a FIFO graph input that blocks graph
  read. The fixed-worker ready file must be written before graph data is
  available, so this test fails on the old ordering.
- Verified:
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`.
- Benchmarked `babyface.n6c10`, p6/w6, one iteration, malloc-backed BK storage,
  no compression:
  - baseline restored p6/w6
    `benchmark_results/local_tcp_worker_sweep_restored_p6_w6_malloc_babyface_none_20260711_044246`:
    total `6,541,356us`, read graph `2,296,423us`, partition
    `1,128,552us`, setup `2,208,413us`, solve `888,097us`, partition-load TX
    `405,000,240` bytes;
  - early-listen run
    `benchmark_results/local_tcp_early_listen_p6_w6_malloc_babyface_none_20260711_044754`:
    total `6,364,644us`, read graph `2,208,089us`, partition
    `1,089,446us`, setup `2,171,553us`, solve `895,095us`, partition-load TX
    `405,000,240` bytes;
  - early-listen repeat
    `benchmark_results/local_tcp_early_listen_repeat_p6_w6_malloc_babyface_none_20260711_044813`:
    total `6,435,136us`, read graph `2,220,880us`, partition
    `1,080,986us`, setup `2,229,359us`, solve `903,463us`, partition-load TX
    `405,000,240` bytes.
- Conclusion: keep this as a small but repeatable wall-time win. It does not
  reduce bytes or worker solve time; it overlaps worker startup/connect with
  coordinator graph preparation in fixed-worker mode.

## 2026-07-11 04:43 PDT

- Ran a fixed-partition worker-count sweep on the restored product binary:
  `babyface.n6c10`, p6, one iteration, malloc-backed BK storage,
  no compression.
- Important process note: an earlier sweep immediately after reverting the
  24-bit packed-arc prototype used stale binaries and reported the packed
  `344,250,240` byte payload. Those directories are invalid for product
  comparison. The valid restored sweep below reports the expected
  `405,000,240` byte payload.
- Results:
  - w2
    `benchmark_results/local_tcp_worker_sweep_restored_p6_w2_malloc_babyface_none_20260711_044212`:
    total `9,399,596us`, setup `5,004,025us`, solve `933,072us`;
  - w3
    `benchmark_results/local_tcp_worker_sweep_restored_p6_w3_malloc_babyface_none_20260711_044221`:
    total `7,849,642us`, setup `3,475,420us`, solve `913,428us`;
  - w4
    `benchmark_results/local_tcp_worker_sweep_restored_p6_w4_malloc_babyface_none_20260711_044229`:
    total `7,831,989us`, setup `3,461,129us`, solve `926,388us`;
  - w5
    `benchmark_results/local_tcp_worker_sweep_restored_p6_w5_malloc_babyface_none_20260711_044237`:
    total `7,928,203us`, setup `3,564,479us`, solve `897,545us`;
  - w6
    `benchmark_results/local_tcp_worker_sweep_restored_p6_w6_malloc_babyface_none_20260711_044246`:
    total `6,541,356us`, setup `2,208,413us`, solve `888,097us`.
- Conclusion: for p6 local TCP on this machine, using one worker per partition
  remains best. Reducing workers makes each worker load multiple partitions
  serially and increases setup wall time.

## 2026-07-11 04:39 PDT

- Tested 24-bit worker-load arc endpoint packing as a partition-load byte
  reduction experiment:
  - p6/w6 package arc endpoint payload is about `243 MB`;
  - all p6 local endpoints fit within 24 bits, so fixed 24-bit packing reduced
    partition-load TX from `405,000,240` bytes to `344,250,240` bytes.
- Reverted the prototype because it was slower despite lower bytes:
  - packed run
    `benchmark_results/local_tcp_packed_u24_arcs_p6_w6_malloc_babyface_none_20260711_043730`:
    total `7,777,341us`, setup `3,457,282us`;
  - packed repeat
    `benchmark_results/local_tcp_packed_u24_arcs_repeat_p6_w6_malloc_babyface_none_20260711_043737`:
    total `7,741,303us`, setup `3,449,981us`.
- Recorded the result in `FAILED_APPROACHES.md`; no product code was kept.

## 2026-07-11 04:32 PDT

- Refactored mcpd3 `PartitionWorkerCoordinator` construction to avoid copying
  each package's boundary endpoint vector into coordinator package storage:
  - constraints are now built directly from the input package vector before
    packages are moved to workers;
  - coordinator package records retain only the partition ids needed during
    solve rounds;
  - removed the now-dead coordinator payload drop and empty package scaling
    loops.
- Verified:
  - `cmake --build build/mcpd3-native -j`;
  - `ctest --test-dir build/mcpd3-native --output-on-failure`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`.
- Benchmarked `babyface.n6c10`, p6/w6, one iteration, malloc-backed BK storage,
  no compression:
  - baseline mmap-reader run
    `benchmark_results/local_tcp_mmap_reader_p6_w6_malloc_babyface_none_20260711_042215`:
    total `6,553,760us`, setup `2,221,805us`;
  - no-endpoint-copy run
    `benchmark_results/local_tcp_no_endpoint_copy_p6_w6_malloc_babyface_none_20260711_043133`:
    total `6,619,059us`, setup `2,230,088us`;
  - no-endpoint-copy repeat
    `benchmark_results/local_tcp_no_endpoint_copy_repeat_p6_w6_malloc_babyface_none_20260711_043140`:
    total `6,578,808us`, setup `2,222,026us`.
- This is not a measurable p6 throughput win, but it removes transient
  coordinator endpoint-copy memory and simplifies setup state.

## 2026-07-11 04:28 PDT

- Added mcpd4 TCP loopback coverage for worker-load partition packages whose
  arc capacity vectors must remain full because forward and reverse capacities
  are both nonzero:
  - verifies no directed-capacity compaction is applied in that case;
  - verifies the package still omits worker-redundant `local_to_global`,
    endpoint global id, and endpoint momentum fields;
  - checks both no-compression direct receive and Snappy fallback paths against
    an in-process worker result.
- The attempted chunked direct compact-capacity expansion was benchmarked and
  reverted because it was slower; see `FAILED_APPROACHES.md`.

## 2026-07-11 04:22 PDT

- Replaced the scaled directed DIMACS reader's `fgets` line-buffer parse with
  a bounded read-only mmap scanner in mcpd3:
  - preserves the existing one-pass directed streaming semantics;
  - keeps arc capacities scaled during parse and terminal capacities scaled
    after aggregation;
  - avoids line-buffer copies while still reserving graph vectors from the
    `p max` declaration.
- Added mcpd3 coverage for a final DIMACS arc line without a trailing newline,
  validating node count, internal arcs, scaled capacities, aggregated terminal
  capacities, and saturation counters.
- TDD/debug note: the first mcpd3 test run failed because the rewritten reader
  initially missed the old explicit `g.nnode = 0` initialization. Fixed before
  benchmarking.
- Verified:
  - `cmake --build build/mcpd3-native -j`;
  - `ctest --test-dir build/mcpd3-native --output-on-failure`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`.
- Benchmarked `babyface.n6c10`, p6/w6, one iteration, malloc-backed BK storage,
  no compression:
  - previous best nearby run
    `benchmark_results/local_tcp_arc_reserve_memfree_p6_w6_malloc_babyface_none_20260711_041414`:
    total `6,714,937us`, read `2,421,733us`, partition `1,129,446us`,
    setup `2,255,429us`, solve `894,133us`;
  - mmap-reader run
    `benchmark_results/local_tcp_mmap_reader_p6_w6_malloc_babyface_none_20260711_042215`:
    total `6,553,760us`, read `2,284,160us`, partition `1,134,063us`,
    setup `2,221,805us`, solve `901,214us`;
  - mmap-reader repeat
    `benchmark_results/local_tcp_mmap_reader_repeat_p6_w6_malloc_babyface_none_20260711_042221`:
    total `6,582,526us`, read `2,303,363us`, partition `1,135,770us`,
    setup `2,232,413us`, solve `896,437us`.
- Output and transport size were unchanged:
  `final_objective_raw=1,970,000`, `final_disagreement_count=134,985`,
  partition-load TX `405,000,240` bytes.

## 2026-07-11 04:14 PDT

- Confirmed the laptop has substantially more free memory after cleanup:
  `15Gi` total, `12Gi` available, and no active `mcpd3`/`mcpd4` processes.
- Ran a fresh local TCP malloc-backed `babyface.n6c10` p4/p5/p6 sweep on the
  arc-reserve code path, one iteration, directed input, no compression,
  `objective_scale=1000`:
  - p4/w4
    `benchmark_results/local_tcp_arc_reserve_memfree_p4_w4_malloc_babyface_none_20260711_041358`:
    total `6,855,313us`, read `2,417,210us`, partition `1,043,072us`,
    setup `1,986,944us`, solve `1,393,395us`;
  - p5/w5
    `benchmark_results/local_tcp_arc_reserve_memfree_p5_w5_malloc_babyface_none_20260711_041405`:
    total `8,849,802us`, read `2,439,000us`, partition `1,095,174us`,
    setup `2,074,469us`, solve `3,222,825us`;
  - p6/w6
    `benchmark_results/local_tcp_arc_reserve_memfree_p6_w6_malloc_babyface_none_20260711_041414`:
    total `6,714,937us`, read `2,421,733us`, partition `1,129,446us`,
    setup `2,255,429us`, solve `894,133us`.
- p6/w6 remains the best local TCP setting among these nearby malloc-backed
  choices. p4/w4 is close but loses on solve time; p5/w5 is clearly worse on
  solve time.

## 2026-07-11 04:10 PDT

- Added mcpd3 arc-vector pre-reservation during dual-decomposition package
  construction:
  - after partition labels are computed, mcpd3 now counts how many original
    arcs each subproblem will own;
  - each `MinCutSubGraph` reserves exact endpoint/capacity vector capacity
    before distributing arcs;
  - this avoids repeated large vector reallocations while preserving package
    contents and wire format.
- Added mcpd3 coverage for a many-arc package-only export path that validates
  exported packages and loads/solves them through `PartitionWorkerCoordinator`.
- Benchmarked `babyface.n6c10`, p6/w6, one iteration, malloc-backed BK storage,
  no compression:
  - previous current baseline
    `benchmark_results/local_tcp_after_timing_revert_p6_w6_malloc_babyface_none_20260711_040230`:
    total `6,910,844us`, read `2,439,633us`, partition `1,343,227us`,
    setup `2,212,085us`, solve `902,907us`;
  - arc-reserve run
    `benchmark_results/local_tcp_arc_reserve_p6_w6_malloc_babyface_none_20260711_041007`:
    total `6,736,326us`, read `2,456,462us`, partition `1,149,368us`,
    setup `2,214,534us`, solve `903,222us`;
  - arc-reserve repeat
    `benchmark_results/local_tcp_arc_reserve_repeat_p6_w6_malloc_babyface_none_20260711_041024`:
    total `6,732,454us`, read `2,441,456us`, partition `1,157,074us`,
    setup `2,212,578us`, solve `907,623us`.
- Output and transport size were unchanged:
  `final_objective_raw=1,970,000`, `final_disagreement_count=134,985`,
  partition-load TX `405,000,240` bytes.
- Verified:
  - `cmake --build build/mcpd3-native -j`;
  - `ctest --test-dir build/mcpd3-native --output-on-failure`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`.

## 2026-07-11 03:44 PDT

- Added a scaled directed DIMACS reader in mcpd3 so directed inputs can apply
  the objective scale during streaming load instead of doing a second full
  `scale_graph` pass in mcpd4.
- The reader preserves exact post-load scaling semantics:
  - internal directed arc capacities are scaled as they are parsed;
  - terminal capacities are accumulated unscaled first, then scaled after
    aggregation;
  - strict overflow mode throws;
  - saturating overflow mode clips by sign and reports separate arc/terminal
    saturation counts.
- mcpd4 now uses this reader only for `--directed`; non-directed inputs still
  use the existing read-then-scale path.
- mcpd4 reports `objective_scale_applied_during_read 1` for directed runs so
  users and tests can identify the single-pass scaling path.
- Added test coverage:
  - mcpd3 unit tests for exact scaled directed loading and strict vs saturating
    overflow handling;
  - mcpd4 process integration test for a directed, scaled distributed run that
    matches the in-process reference and reports the single-pass scaling marker.
- Benchmarked `babyface.n6c10`, p6/w6, one iteration, malloc-backed BK storage,
  no compression:
  - baseline
    `benchmark_results/local_tcp_rebuilt_compact_caps_malloc_babyface_p6_w6_none_20260711_033648`:
    total `7,047,881us`, read `2,159,047us`, scale `389,203us`,
    partition `1,344,933us`, setup `2,241,004us`, solve `900,614us`;
  - scaled-reader
    `benchmark_results/local_tcp_scaled_directed_reader_malloc_babyface_p6_w6_none_20260711_034359`:
    total `6,906,757us`, read `2,407,992us`, scale `1us`,
    partition `1,354,420us`, setup `2,229,763us`, solve `903,164us`;
  - repeat
    `benchmark_results/local_tcp_scaled_directed_reader_repeat_malloc_babyface_p6_w6_none_20260711_034426`:
    total `6,931,610us`, read `2,424,360us`, scale `1us`,
    partition `1,356,529us`, setup `2,227,187us`, solve `905,142us`.
- Output remained unchanged across runs:
  `final_objective_raw=1,970,000`, `final_disagreement_count=134,985`,
  `objective_scale_saturation_count=0`, and partition-load TX
  `405,000,240` bytes.
- Verified:
  - `cmake --build build/mcpd3-native -j`;
  - `ctest --test-dir build/mcpd3-native --output-on-failure`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`.

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

## 2026-07-11 01:25 PDT

- Started local TCP optimization work in worktree `.worktrees/local-tcp-opt`
  on branch `local-tcp-opt`; mcpd3 submodule work is on branch
  `partition-load-parallel`.
- Baseline observation on `babyface.n6c10`, directed, p10, 2 localhost TCP
  workers, objective scale `1000`, no RPC compression:
  - run directory:
    `benchmark_results/local_tcp_baseline_babyface_p10_w2_none_20260711_011746`;
  - stopped early after collecting setup and early solve telemetry because it
    was consuming about 5 GB RSS while swap was full;
  - sequential package loading produced `timing_coordinator_setup_wall_us =
    34,548,028`;
  - each ~55-60 MB partition package load took about `3.3-3.6s`, and package
    loads were serialized across workers;
  - early solve telemetry at iteration 50 showed `timing_worker_rpc_overhead`
    around `11.31s` over `517.87s` worker solve aggregate, so setup/package
    loading was the first higher-impact target.
- Implemented mcpd3 commit `e621711`:
  - `PartitionWorkerCoordinator` now groups assigned packages by worker and
    loads one group per active worker concurrently;
  - per-worker package order is still sequential, preserving one in-flight
    load per worker/socket.
- Added regression coverage in `partition_worker_test`:
  - `coordinatorLoadsPartitionsAcrossWorkersConcurrently` uses two sleeping
    probe workers and asserts constructor load calls overlap across workers.
- Optimized setup-only comparison on the same graph/config with a one-iteration
  solve:
  - run directory:
    `benchmark_results/local_tcp_parallel_load_babyface_p10_w2_none_20260711_012422`;
  - `timing_coordinator_setup_wall_us = 19,534,584`;
  - `timing_load_partition_rpc_us = 38,387,094` remains an aggregate sum across
    workers, confirming the improvement is overlap rather than faster
    individual package transfer;
  - setup wall improvement from the collected baseline is about `43.5%`
    (`34.55s -> 19.53s`).
- Added mcpd4 protocol fast path for partition packages:
  - pre-reserve the final package frame size;
  - encode/decode large `int` vectors in bulk on little-endian 32-bit-int
    hosts while preserving the existing wire format;
  - added a fixed package wire-size assertion to
    `protocol_serialization_test`.
- Bulk-encoding benchmark, same p10/w2/file-mmap/no-compression setup:
  - run directory:
    `benchmark_results/local_tcp_parallel_bulk_babyface_p10_w2_none_20260711_012809`;
  - `timing_coordinator_setup_wall_us = 18,017,156`;
  - `timing_load_partition_rpc_us = 35,146,645`;
  - additional setup wall improvement after parallel loading: about `7.8%`
    (`19.53s -> 18.02s`).
- Local TCP configuration sweep on the same graph/config with worker
  `--bk-storage malloc` and no compression:
  - p10/w2:
    `benchmark_results/local_tcp_parallel_bulk_malloc_babyface_p10_w2_none_20260711_012938`,
    setup `16,163,886us`, total `21,853,028us`;
  - p10/w4:
    `benchmark_results/local_tcp_parallel_bulk_malloc_babyface_p10_w4_none_20260711_013018`,
    setup `12,028,587us`, total `17,758,103us`;
  - p10/w8:
    `benchmark_results/local_tcp_parallel_bulk_malloc_babyface_p10_w8_none_20260711_013050`,
    setup `10,868,068us`, total `16,577,284us`;
  - p10/w10:
    `benchmark_results/local_tcp_parallel_bulk_malloc_babyface_p10_w10_none_20260711_013120`,
    setup `8,085,850us`, total `13,860,731us`;
  - for this one-iteration setup-heavy benchmark, one local worker per
    partition was best among the tested local TCP points;
  - important caveat: worker `malloc` storage is substantially more memory
    heavy than file-backed BK mmap, so treat these as fits-in-RAM speed
    measurements, not the recommended large/out-of-core configuration.
- No-TCP in-process comparators after the same code changes:
  - p10/w2:
    `benchmark_results/inprocess_parallel_bulk_babyface_p10_w2_20260711_012845`,
    setup `9,607,807us`, total `14,912,483us`;
  - p10/w10:
    `benchmark_results/inprocess_parallel_bulk_babyface_p10_w10_20260711_013142`,
    setup `3,764,435us`, total `9,232,235us`;
  - remaining local TCP overhead for p10/w10 is about `4.32s` in setup
    (`8.09s - 3.76s`) on this run.
- Added a protocol frame-type peek and zero-copy payload view for
  partition-package decode:
  - public `decodeFrame` still preserves the copied-payload API;
  - runtime dispatch now uses `decodeFrameType` to avoid constructing a copied
    payload just to switch on message type;
  - `decodePartitionPackage` now reads directly from the original frame buffer
    rather than a copied frame payload.
- Type-peek/zero-copy decode benchmark repeats on p10/w10/malloc/no-compression:
  - `benchmark_results/local_tcp_typepeek_babyface_p10_w10_none_20260711_013455`,
    setup `8,649,702us`, total `14,206,731us`;
  - `benchmark_results/local_tcp_typepeek_repeat_babyface_p10_w10_none_20260711_013525`,
    setup `7,670,352us`, total `13,506,131us`;
  - result is noisy but roughly neutral-to-positive relative to the previous
    p10/w10 best setup `8,085,850us`; keep watching on larger runs.
- Added reproducibility knobs to `scripts/run_local_process_benchmark.sh`:
  - `MCPD4_TELEMETRY_CSV_PREFIX`;
  - `MCPD4_WORKER_BK_STORAGE`;
  - `MCPD4_WORKER_BK_MMAP_DIR_PREFIX`;
  - `MCPD4_WORKER_BK_MMAP_ADVISE`.
- Updated README local run docs with optimized localhost examples:
  - malloc-backed workers for in-memory local TCP performance;
  - file-backed worker BK mmap directories for larger graphs.
- Full local TCP `babyface.n6c10` attempts:
  - strict p10/w10/malloc/objective-scale-1000 run
    `benchmark_results/local_tcp_optimized_full_babyface_p10_w10_none_20260711_013605`
    failed at iteration `96` after regularization budget exceeded and
    objective-scale promotion overflowed int32;
  - strict p10/w10/malloc/objective-scale-10000 run
    `benchmark_results/local_tcp_optimized_full_babyface_p10_w10_os10000_none_20260711_014141`
    failed during initial scaling with `objective scale exceeds int range`;
  - saturated p10/w10/malloc/objective-scale-10000 run
    `benchmark_results/local_tcp_optimized_full_babyface_p10_w10_os10000_saturate_none_20260711_014340`
    started successfully and clipped `11,370` terminal capacities; after a
    promotion it was manually stopped at total iteration `66` because it was no
    longer a clean exactness or fast-validation run; treat this as
    compatibility/performance data, not exact strict-capacity data.
- Helper-script smoke for the new reproducibility knobs:
  - run directory:
    `benchmark_results/helper_smoke_local_tcp_20260711_014926`;
  - command used `MCPD4_WORKER_BK_STORAGE=malloc` and
    `MCPD4_TELEMETRY_CSV_PREFIX`;
  - completed through local TCP with `status=0`, final objective `4`, zero
    disagreement, and telemetry files written.
- Verified:
  - `cmake -S third_party/mcpd3 -B build/mcpd3-native -DCMAKE_BUILD_TYPE=Release`;
  - `cmake --build build/mcpd3-native -j`;
  - `ctest --test-dir build/mcpd3-native --output-on-failure`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`.

## 2026-07-11 02:06 PDT

- Continued local TCP setup optimization in worktree
  `/home/matt/software/mcpd3-distributed/.worktrees/local-tcp-opt` on branch
  `local-tcp-opt`.
- Removed another receive-side full-payload copy:
  - `receiveFrameBytes` now validates received frames with `decodeFrameType`
    instead of `decodeFrame`;
  - this preserves message-type validation without constructing a copied
    payload that is immediately discarded;
  - added TCP regression tests for unknown message-type rejection in
    uncompressed and snappy receive paths.
- Removed the completed-frame return copy in `encodePartitionPackage`:
  - added `Writer::takeBytes()`;
  - `encodePartitionPackage` now returns the filled frame vector by move.
- Added uncompressed partition-package scatter/gather send:
  - introduced `ByteBufferView`;
  - added `PartitionPackageFrameBuffers`, which exposes the same package wire
    bytes as small headers plus views over existing `PartitionPackage` vectors;
  - added protocol coverage that concatenating the buffers exactly matches
    `encodePartitionPackage`;
  - added TCP coverage for sending one frame from multiple buffers;
  - `TcpPartitionWorker::loadPartition` uses the buffer path only for
    uncompressed transports on little-endian 32-bit-int hosts, otherwise it
    falls back to the existing encoded-frame path;
  - buffer-list writes use `sendmsg` with `MSG_NOSIGNAL`.
- Comparable mmap-backed `babyface.n6c10` p10/w2/no-compression/one-iteration
  setup runs:
  - previous bulk-encode baseline
    `benchmark_results/local_tcp_parallel_bulk_babyface_p10_w2_none_20260711_012809`:
    setup `18,017,156us`, total `25,706,023us`,
    load RPC aggregate `35,146,645us`;
  - receive type-peek only, default worker BK mmap path
    `benchmark_results/local_tcp_recv_typepeek_defaultbk_babyface_p10_w2_none_20260711_015654`:
    setup `18,397,708us`, total `25,257,601us`,
    load RPC aggregate `36,082,765us`;
  - receive type-peek plus move-return copy drop
    `benchmark_results/local_tcp_sendrecv_copydrop_defaultbk_babyface_p10_w2_none_20260711_015836`:
    setup `17,300,658us`, total `23,832,448us`,
    load RPC aggregate `34,043,835us`;
  - scatter/gather package send with per-buffer writes
    `benchmark_results/local_tcp_scatter_package_defaultbk_babyface_p10_w2_none_20260711_020321`:
    setup `17,758,739us`, total `24,141,107us`,
    load RPC aggregate `34,832,544us`;
  - scatter/gather package send with `sendmsg`
    `benchmark_results/local_tcp_sendmsg_package_defaultbk_babyface_p10_w2_none_20260711_020445`:
    setup `17,341,089us`, total `23,767,172us`,
    load RPC aggregate `34,056,841us`.
- Interpretation:
  - receive type-peek alone was noisy and not a setup-time win on this point;
  - the partition-package move-return copy drop is the measured setup win
    (`18.02s -> 17.30s`, about `4.0%`);
  - `sendmsg` scatter/gather is roughly setup-neutral versus the move-return
    run but lowers coordinator memory pressure by avoiding a full serialized
    package frame allocation for uncompressed local TCP;
  - this is more relevant to file-backed mmap and larger local runs than the
    earlier `malloc` speed probes.
- Verified:
  - `cmake --build build -j`;
  - `./build/protocol_serialization_test`;
  - `./build/tcp_loopback_test`;
  - `ctest --test-dir build --output-on-failure`;
  - `ctest --test-dir build/mcpd3-native --output-on-failure`.

## 2026-07-11 02:12 PDT

- Added direct worker-side receive/decode for uncompressed
  `PARTITION_PACKAGE` frames:
  - the worker now reads the 12-byte uncompressed frame header first;
  - when the frame is a package and the host supports the raw int-vector
    layout, it fills `PartitionPackage` vectors directly from the socket
    payload;
  - non-package uncompressed frames, snappy frames, and unsupported host layouts
    continue through the existing full-frame receive/decode path.
- This removes the worker-side full-frame buffer for uncompressed package
  loads. The package vectors are still materialized because mcpd3 workers own
  their partition packages, but the intermediate serialized frame allocation is
  skipped.
- Comparable mmap-backed `babyface.n6c10` p10/w2/no-compression/one-iteration
  benchmark:
  - previous sendmsg package-transfer run
    `benchmark_results/local_tcp_sendmsg_package_defaultbk_babyface_p10_w2_none_20260711_020445`:
    setup `17,341,089us`, total `23,767,172us`,
    load RPC aggregate `34,056,841us`;
  - direct worker package receive run
    `benchmark_results/local_tcp_direct_recv_package_defaultbk_babyface_p10_w2_none_20260711_021113`:
    setup `17,150,205us`, total `23,620,803us`,
    load RPC aggregate `33,759,335us`;
  - versus the earlier bulk-encode baseline
    `benchmark_results/local_tcp_parallel_bulk_babyface_p10_w2_none_20260711_012809`,
    setup moved from `18,017,156us` to `17,150,205us` and total wall moved
    from `25,706,023us` to `23,620,803us` on this comparable point.
- Verified:
  - `./build/tcp_loopback_test`;
  - `./build/process_integration_test ./build/mcpd4_coordinator ./build/mcpd4_worker ./build/mcpd4_discovery ./build/mcpd4_status tests/fixtures`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`;
  - `ctest --test-dir build/mcpd3-native --output-on-failure`.

## 2026-07-11 02:24 PDT

- Ran a file-backed mmap local TCP worker-count sweep on
  `babyface.n6c10`, p10/no-compression/one-iteration, after the direct package
  receive optimization:
  - w2:
    `benchmark_results/local_tcp_direct_recv_package_defaultbk_babyface_p10_w2_none_20260711_021113`,
    setup `17,150,205us`, total `23,620,803us`;
  - w4:
    `benchmark_results/local_tcp_direct_recv_filemmap_babyface_p10_w4_none_20260711_021806`,
    setup `12,245,282us`, total `18,651,406us`;
  - w8:
    `benchmark_results/local_tcp_direct_recv_filemmap_babyface_p10_w8_none_20260711_021825`,
    setup `10,906,116us`, total `17,563,935us`;
  - w10:
    `benchmark_results/local_tcp_direct_recv_filemmap_babyface_p10_w10_none_20260711_021843`,
    setup `9,250,669us`, total `16,294,229us`.
- Current best tested local TCP point on this p10 setup-heavy benchmark is one
  local worker per partition with file-backed BK mmap. The result is materially
  better than w2 even though aggregate worker load time rises, because package
  loading and solver construction overlap across more worker processes.
- Ran an in-process comparator with matching file-backed BK mmap:
  - `benchmark_results/inprocess_filemmap_babyface_p10_w10_20260711_022024`,
    setup `5,843,418us`, total `11,916,924us`;
  - remaining local TCP setup gap at p10/w10 is roughly `3.4s` on this point
    (`9.25s - 5.84s`), mostly attributable to local package transfer and
    worker-process setup/coordination.
- Added a mcpd3 setup hygiene optimization:
  - `InProcessPartitionWorker::loadPartition` now reserves
    `constraint_arc_by_id` to the boundary endpoint count before inserting
    per-endpoint constraint arcs;
  - added `inProcessPartitionWorkerLoadsManyBoundaryEndpoints` to cover the
    many-boundary load path.
- Boundary-map reserve benchmark repeats on p10/w10/file-backed mmap were
  noisy:
  - first run
    `benchmark_results/local_tcp_reserved_boundary_map_filemmap_babyface_p10_w10_none_20260711_022322`:
    setup `7,951,065us`, total `14,323,513us`;
  - repeat
    `benchmark_results/local_tcp_reserved_boundary_map_repeat_filemmap_babyface_p10_w10_none_20260711_022357`:
    setup `9,117,897us`, total `15,714,525us`;
  - keep this as low-risk allocation hygiene, not a standalone proven
    performance win.
- Verified:
  - `cmake --build build/mcpd3-native -j`;
  - `ctest --test-dir build/mcpd3-native --output-on-failure`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`.

## 2026-07-11 02:32 PDT

- Tested local TCP configuration levers on current p10/w10/file-backed mmap
  `babyface.n6c10` one-iteration setup:
  - snappy:
    `benchmark_results/local_tcp_snappy_filemmap_babyface_p10_w10_20260711_022738`,
    setup `8,391,191us`, total `14,641,085us`, wire TX
    `278,055,791` bytes versus logical TX `572,751,240` bytes, compression
    wall `1,633,765us`;
  - BK mmap `populate`:
    `benchmark_results/local_tcp_populate_filemmap_babyface_p10_w10_none_20260711_022806`,
    setup `8,135,957us`, total `14,374,983us`;
  - BK mmap `willneed`:
    `benchmark_results/local_tcp_willneed_filemmap_babyface_p10_w10_none_20260711_022833`,
    setup `7,824,183us`, total `14,034,584us`;
  - BK mmap `willneed` repeat:
    `benchmark_results/local_tcp_willneed_repeat_filemmap_babyface_p10_w10_none_20260711_022913`,
    setup `8,146,948us`, total `14,246,683us`.
- `willneed` is the best tested file-backed local TCP setting so far on this
  machine for the p10/w10 setup-heavy point. Updated the README large-graph
  local benchmark example to use `MCPD4_WORKER_BK_MMAP_ADVISE=willneed`
  instead of `populate`.
- Tried a `TCP_NODELAY` implementation on accepted and connected sockets with
  loopback coverage, then reverted it after benchmark evidence:
  - run
    `benchmark_results/local_tcp_nodelay_willneed_filemmap_babyface_p10_w10_none_20260711_023104`
    setup `9,150,496us`, total `15,845,669us`;
  - this was worse than both `willneed` repeats, so it was not kept.
- Verified after the temporary TCP_NODELAY change before reverting:
  - `cmake --build build -j`;
  - `./build/tcp_loopback_test`;
  - `ctest --test-dir build --output-on-failure`;
  - `ctest --test-dir build/mcpd3-native --output-on-failure`.

## 2026-07-11 02:42 PDT

- After freeing memory on the laptop, reran local TCP `babyface.n6c10`
  one-iteration setup sweeps with file-backed BK mmap, `willneed`, no
  compression, `objective_scale=1000`, and one local worker per partition.
- The cleaned-memory p10/w10 repeat improved substantially:
  - `benchmark_results/local_tcp_willneed_after_memfree_babyface_p10_w10_none_20260711_023544`;
  - coordinator total `11,076,306us`, setup `5,948,891us`, solve
    `911,493us`, aggregate partition-load RPC `51,338,181us`;
  - wrapper wall `0:11.33`, max RSS `1,419,540 KiB`.
- Swept partition/worker counts under the same settings:
  - p3/w3:
    `benchmark_results/local_tcp_willneed_after_memfree_babyface_p3_w3_none_20260711_024211`,
    total `9,221,119us`, setup `3,492,283us`, solve `1,533,910us`;
  - p4/w4:
    `benchmark_results/local_tcp_willneed_after_memfree_babyface_p4_w4_none_20260711_023913`,
    total `9,139,006us`, setup `3,356,574us`, solve `1,661,378us`;
  - p5/w5:
    `benchmark_results/local_tcp_willneed_after_memfree_seq_babyface_p5_w5_none_20260711_024039`,
    total `11,488,365us`, setup `3,459,302us`, solve `3,900,836us`;
  - p6/w6:
    `benchmark_results/local_tcp_willneed_after_memfree_babyface_p6_w6_none_20260711_023831`,
    total `8,868,286us`, setup `3,608,384us`, solve `1,094,849us`;
  - p6/w6 repeat:
    `benchmark_results/local_tcp_willneed_after_memfree_repeat_babyface_p6_w6_none_20260711_024150`,
    total `9,077,283us`, setup `3,804,701us`, solve `1,114,780us`;
  - p7/w7:
    `benchmark_results/local_tcp_willneed_after_memfree_seq_babyface_p7_w7_none_20260711_024104`,
    total `9,975,008us`, setup `4,810,056us`, solve `882,539us`;
  - p8/w8:
    `benchmark_results/local_tcp_willneed_after_memfree_babyface_p8_w8_none_20260711_023754`,
    total `10,634,266us`, setup `5,008,980us`, solve `1,483,682us`;
  - p12/w12:
    `benchmark_results/local_tcp_willneed_after_memfree_babyface_p12_w12_none_20260711_023712`,
    total `12,306,096us`, setup `7,098,151us`, solve `877,886us`;
  - p16/w16:
    `benchmark_results/local_tcp_willneed_after_memfree_babyface_p16_w16_none_20260711_023633`,
    total `14,435,133us`, setup `9,130,204us`, solve `1,016,999us`.
- Current best measured local TCP one-iteration point on this machine is
  p6/w6, repeatable around `8.9s-9.1s` coordinator wall. Higher p/w counts
  reduce some solve time but lose more to package/load contention; lower p/w
  counts reduce load cost but lose solve parallelism.
- Ran a matched p6/w6 `malloc` worker-storage probe now that RAM was available:
  - `benchmark_results/local_tcp_malloc_after_memfree_babyface_p6_w6_none_20260711_024358`;
  - coordinator total `8,420,003us`, setup `3,269,665us`, solve
    `917,688us`, aggregate partition-load RPC `18,638,094us`;
  - this beats the file-backed p6/w6 repeats on the smaller benchmark, but
    remains a memory-heavy speed probe rather than the recommended large or
    out-of-core default.

## 2026-07-11 02:50 PDT

- Added a compact worker-load package mode for the no-compression TCP fast
  path. Canonical `encodePartitionPackage` remains unchanged, but
  `TcpPartitionWorker::loadPartition` now omits `local_to_global` from the
  scatter/gather worker-load frame because remote workers do not use that field
  for solver construction or label reporting.
- Added coverage:
  - protocol test verifies the compact worker-load frame decodes as the same
    package with `local_to_global` empty and has exactly the expected byte
    reduction;
  - TCP loopback test verifies remote load telemetry omits
    `local_to_global` bytes and the loaded worker can still solve and return
    boundary labels.
- Benchmarked `babyface.n6c10`, p6/w6, one iteration, no compression:
  - file-backed BK mmap + `willneed`:
    `benchmark_results/local_tcp_omit_l2g_filemmap_babyface_p6_w6_none_20260711_024927`,
    total `8,778,549us`, setup `3,606,458us`, solve `1,058,663us`,
    partition-load TX `532,500,240` bytes;
  - matched prior file-backed p6/w6 runs transmitted `554,250,240` partition
    bytes and took `8,868,286us` and `9,077,283us`;
  - `malloc` speed probe:
    `benchmark_results/local_tcp_omit_l2g_malloc_babyface_p6_w6_none_20260711_025013`,
    total `8,290,466us`, setup `3,254,290us`, solve `907,569us`,
    partition-load TX `532,500,240` bytes.
- The optimization removes `21,750,000` bytes from this p6/w6 package load.
  Wall-time improvement is modest on this setup, but it is a strictly smaller
  local TCP transfer and keeps results unchanged (`final_objective_raw`
  `1,970,000`, `final_disagreement_count` `134,985` in all matched p6 runs).
- Extended the compact worker-load package path to Snappy transport as well.
  The compact Snappy p6/w6 file-backed run
  `benchmark_results/local_tcp_omit_l2g_snappy_filemmap_babyface_p6_w6_20260711_025317`
  transmitted the same `532,500,240` logical partition-load bytes, reduced
  total coordinator wire TX to `249,634,612` bytes, and spent `1,100,580us`
  in compression. Local wall time was `9,425,194us`, so no-compression remains
  faster for localhost.
- Verified:
  - `cmake --build build -j`;
  - `./build/protocol_serialization_test`;
  - `./build/tcp_loopback_test`;
  - `ctest --test-dir build --output-on-failure`;
  - `ctest --test-dir build/mcpd3-native --output-on-failure`.

## 2026-07-11 03:01 PDT

- Further compacted worker-load package endpoint records. Canonical package
  serialization remains full-fidelity, but worker-load frames now omit endpoint
  `global_node_id` and `alpha_momentum`; remote TCP solve results already carry
  compact labels and the coordinator uses its own constraint metadata. Kept
  `local_index`, `alpha`, and `last_alpha` because those are worker-side solve
  inputs. Bumped the runtime protocol version from `6` to `7` so mixed old/new
  coordinator-worker pairs fail at handshake instead of failing mid-load.
- Added/updated coverage:
  - protocol test checks the compact frame byte count and compact endpoint
    defaults (`global_node_id=-1`, `alpha_momentum=0`);
  - TCP loopback test checks the larger package-load byte reduction through
    both uncompressed and Snappy transports.
- Benchmarked `babyface.n6c10`, p6/w6, one iteration:
  - file-backed BK mmap + `willneed` + no compression:
    `benchmark_results/local_tcp_omit_l2g_endpoint_filemmap_babyface_p6_w6_none_20260711_025903`,
    total `8,887,600us`, setup `3,580,232us`, solve `1,094,004us`,
    partition-load TX `526,500,240` bytes;
  - `malloc` speed probe:
    `benchmark_results/local_tcp_omit_l2g_endpoint_malloc_babyface_p6_w6_none_20260711_025944`,
    total `8,376,554us`, setup `3,280,384us`, solve `899,287us`,
    partition-load TX `526,500,240` bytes;
  - Snappy file-backed:
    `benchmark_results/local_tcp_omit_l2g_endpoint_snappy_filemmap_babyface_p6_w6_20260711_030035`,
    total `9,565,564us`, setup `4,158,200us`, solve `1,235,490us`,
    logical partition-load TX `526,500,240` bytes, total coordinator wire TX
    `246,629,494` bytes, compression wall `1,069,437us`.
- Endpoint compaction removes another `6,000,000` logical bytes on p6/w6
  (`750,000` boundary endpoints times `8` bytes) and preserves the matched
  one-iteration objective/disagreement (`final_objective_raw=1,970,000`,
  `final_disagreement_count=134,985`). On localhost this did not produce a
  clear wall-time speedup, but it is a smaller transport payload and should
  matter more when network transfer is the bottleneck.
- Verified:
  - `cmake --build build -j`;
  - `./build/protocol_serialization_test`;
  - `./build/tcp_loopback_test`;
  - `ctest --test-dir build --output-on-failure`;
  - `ctest --test-dir build/mcpd3-native --output-on-failure`.

## 2026-07-11 03:07 PDT

- After freeing system memory, reran the `babyface.n6c10` one-iteration local
  TCP malloc-backed partition-count sweep around the p6/w6 candidate:
  - p2/w2:
    `benchmark_results/local_tcp_endpoint_malloc_babyface_p2_w2_none_20260711_030557`,
    total `12,078,335us`, setup `3,219,063us`, solve `4,773,711us`,
    partition-load TX `513,000,080` bytes;
  - p3/w3:
    `benchmark_results/local_tcp_endpoint_malloc_babyface_p3_w3_none_20260711_030515`,
    total `8,478,821us`, setup `3,069,810us`, solve `1,184,767us`,
    partition-load TX `516,375,120` bytes;
  - p4/w4:
    `benchmark_results/local_tcp_endpoint_malloc_babyface_p4_w4_none_20260711_030451`,
    total `8,522,239us`, setup `3,030,707us`, solve `1,400,977us`,
    partition-load TX `519,750,160` bytes;
  - p5/w5:
    `benchmark_results/local_tcp_endpoint_malloc_babyface_p5_w5_none_20260711_030634`,
    total `10,385,090us`, setup `3,061,779us`, solve `3,182,755us`,
    partition-load TX `523,125,200` bytes;
  - p7/w7:
    `benchmark_results/local_tcp_endpoint_malloc_babyface_p7_w7_none_20260711_030645`,
    total `9,580,102us`, setup `4,517,509us`, solve `767,693us`,
    partition-load TX `529,875,280` bytes;
  - p6/w6 repeat:
    `benchmark_results/local_tcp_endpoint_malloc_repeat_babyface_p6_w6_none_20260711_030710`,
    total `8,324,249us`, setup `3,252,579us`, solve `900,272us`,
    partition-load TX `526,500,240` bytes.
- Current best one-iteration local TCP point on this machine remains p6/w6 with
  malloc-backed BK storage and no transport compression. The lower partition
  counts now fit in memory, but p2 and p5 lose badly on local solve time, and
  p3/p4 are close but still slower than p6.

## 2026-07-11 03:14 PDT

- Optimized the mcpd3 directed DIMACS streaming reader used by mcpd4
  `--directed` coordinator runs:
  - scan the DIMACS header before the existing streaming parse;
  - pre-size `terminal_capacities` from the declared node count;
  - reserve directed arc/capacity vectors from the declared arc count;
  - use a larger stdio buffer for DIMACS reads.
- Added mcpd3 regression coverage for directed streaming inputs with declared
  internal nodes that have no incident arcs, so the reader preserves
  `p max` node count instead of only sizing from touched nodes.
- Benchmarked the current local TCP p6/w6 malloc/no-compression
  `babyface.n6c10` one-iteration probe after the reader change:
  - `benchmark_results/local_tcp_presized_dimacs_malloc_babyface_p6_w6_none_20260711_031310`:
    total `8,042,428us`, read graph `2,104,099us`, setup `3,278,394us`,
    solve `901,577us`;
  - `benchmark_results/local_tcp_presized_dimacs_repeat_malloc_babyface_p6_w6_none_20260711_031335`:
    total `7,993,139us`, read graph `2,107,230us`, setup `3,260,440us`,
    solve `894,638us`.
- Previous matched p6/w6 malloc repeat
  `benchmark_results/local_tcp_endpoint_malloc_repeat_babyface_p6_w6_none_20260711_030710`
  was total `8,324,249us`, read graph `2,493,347us`, setup `3,252,579us`,
  solve `900,272us`. The reader change saves roughly `386-389ms` on graph
  read and about `281-331ms` on total local TCP wall time for this probe, with
  unchanged output (`final_objective_raw=1,970,000`,
  `final_disagreement_count=134,985`).
- Verified:
  - `cmake --build build/mcpd3-native -j`;
  - `ctest --test-dir build/mcpd3-native --output-on-failure`;
  - `cmake --build build -j`;
  - `ctest --test-dir build --output-on-failure`.

## 2026-07-11 03:28 PDT

- Added worker-load package compaction for single-direction arc capacities and
  bumped the runtime protocol to `8`. Canonical package serialization remains
  full-fidelity. Worker-load frames may now send one signed capacity per arc
  when every arc has at most one nonzero direction:
  - positive value: forward capacity, implicit zero backward capacity;
  - negative value: backward capacity, implicit zero forward capacity;
  - zero: both directions zero.
- The worker decode path expands the compact signed vector back to the normal
  full `arc_capacities` vector before loading the mcpd3 partition worker.
- Added/updated coverage:
  - protocol serialization test verifies directed worker-load capacity
    compaction byte counts and full-vector decode;
  - TCP loopback test verifies uncompressed and Snappy remote worker loads
    preserve solve behavior with compact directed capacities;
  - existing non-directed package tests still cover the non-compacting branch
    when both arc directions have nonzero capacity.
- Benchmarked `babyface.n6c10`, p6/w6, one iteration, malloc-backed BK storage,
  no compression:
  - `benchmark_results/local_tcp_compact_single_dir_caps_malloc_babyface_p6_w6_none_20260711_032553`:
    total `7,041,625us`, setup `2,232,970us`, load RPC aggregate
    `12,320,753us`, partition-load TX `405,000,240` bytes;
  - `benchmark_results/local_tcp_compact_single_dir_caps_repeat_malloc_babyface_p6_w6_none_20260711_032615`:
    total `7,087,993us`, setup `2,212,913us`, load RPC aggregate
    `12,215,734us`, partition-load TX `405,000,240` bytes;
  - after caching the computed frame size:
    `benchmark_results/local_tcp_compact_single_dir_caps_cached_malloc_babyface_p6_w6_none_20260711_032828`,
    total `7,090,183us`, setup `2,250,509us`, load RPC aggregate
    `12,318,888us`, partition-load TX `405,000,240` bytes.
- Compared with the post-DIMACS-reader p6/w6 baseline
  `benchmark_results/local_tcp_presized_dimacs_repeat_malloc_babyface_p6_w6_none_20260711_031335`
  (`7,993,139us` total, `3,260,440us` setup, `526,500,240` partition-load TX),
  this saves `121,500,000` logical package-load bytes and roughly `0.9s` total
  wall time on this probe, with unchanged output
  (`final_objective_raw=1,970,000`, `final_disagreement_count=134,985`).
- Retested lower partition counts after the compaction:
  - p3/w3:
    `benchmark_results/local_tcp_compact_single_dir_caps_malloc_babyface_p3_w3_none_20260711_032646`,
    total `7,217,562us`, setup `2,086,650us`, solve `1,201,909us`,
    partition-load TX `394,875,120` bytes;
  - p4/w4:
    `benchmark_results/local_tcp_compact_single_dir_caps_malloc_babyface_p4_w4_none_20260711_032653`,
    total `7,215,526us`, setup `2,014,924us`, solve `1,416,190us`,
    partition-load TX `398,250,160` bytes.
- p6/w6 remains the best current one-iteration local TCP point despite higher
  package bytes, because the p3/p4 solve cost is higher.
- Verified:
  - `cmake --build build -j`;
  - `./build/protocol_serialization_test`;
  - `./build/tcp_loopback_test`;
  - `ctest --test-dir build --output-on-failure`;
  - `ctest --test-dir build/mcpd3-native --output-on-failure`.
