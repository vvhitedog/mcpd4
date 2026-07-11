# Failed Approaches And Taboos

## 2026-07-11 07:47 PDT

- Do not assume more partitions improve the tuned local TCP schedule. With
  start `50`, objective scale `1000`, malloc-backed BK, and no compression,
  p3/w3 and p4/w4 are both slower and fail to produce clean agreement:
  - p3/w3
    `benchmark_results/local_tcp_fullschedule_p3_w3_iter60_start50_malloc_babyface_saturate_20260711_073837`:
    `39,695,478us`, stop_reason `4`, final disagreements `2`;
  - p4/w4
    `benchmark_results/local_tcp_fullschedule_p4_w4_iter60_start50_malloc_babyface_saturate_20260711_074631`:
    `41,636,468us`, stop_reason `4`, final disagreements `64`, objective
    scale promoted to `10000`.
- Do not enable Snappy for localhost local TCP by default. It cuts wire bytes
  but increases wall time and partition-load RPC cost:
  - p1/w1 objective scale `1` no compression:
    `10,043,781us` wall, `1,670,064us` load RPC;
  - p1/w1 objective scale `1` Snappy:
    `11,295,920us` wall, `3,028,873us` load RPC;
  - p2/w2 start `50` no compression repeat:
    `18,210,424us` wall, `4,438,282us` load RPC;
  - p2/w2 start `50` Snappy:
    `18,962,377us` wall, `5,837,053us` load RPC.
- Do not lower objective scale to `1` for the p2/w2 distributed start `50`
  schedule. Run
  `benchmark_results/local_tcp_fullschedule_p2_w2_iter60_start50_os1_malloc_babyface_saturate_20260711_074313`
  had not reached the first progress checkpoint after roughly `48s`, while the
  objective-scale `1000` tuned run finishes cleanly in about `18.2s`; it was
  stopped early. The p1/w1 no-boundary control can use objective scale `1`,
  but the distributed alpha-resolution path cannot.

## 2026-07-11 08:07 PDT

- Do not use the aggressive `adhead.n6c10` p8/w8 schedule start `50` as the
  local TCP target. Run
  `benchmark_results/local_tcp_adhead_n6c10_p8_w8_os2000_start50_iter60_malloc_20260711_075929`
  reached iteration `60` at scale `50` with best raw lower bound `95,877,300`
  and `418` disagreements, then entered an expensive low-scale round. It was
  already past the matched start-10000 control time and emitted
  `regularization budget 2190 is not below limit 2000`, so the run was both
  slower and non-certifying under the strict budget condition. It was stopped
  manually after about `3:42`.
- Do not use `adhead.n6c10` p8/w8 schedule start `500` as a presumed midpoint
  improvement. Run
  `benchmark_results/local_tcp_adhead_n6c10_p8_w8_os2000_start500_iter60_malloc_20260711_080553`
  was stopped after `54.7s`: at the first progress checkpoint it had only
  reached iteration `10`, so it could not beat the clean start-1000 result
  (`50.2s` total wall).
- Do not increase the same tuned adhead local TCP shape to p10/w10 by default.
  Run
  `benchmark_results/local_tcp_adhead_n6c10_p10_w10_os2000_start1000_iter60_malloc_20260711_080822`
  fit in memory but was slower immediately: at `46s` elapsed it had only
  reached iteration `10`, with best raw lower bound `80,680,000` and
  `35,684` disagreements. The p8/w8 start-1000 run had reached iteration `30`
  by about the same time and completed in `50.2s`, so p10/w10 was stopped.
- Do not lower the tuned adhead local TCP shape to p7/w7. Run
  `benchmark_results/local_tcp_adhead_n6c10_p7_w7_os2000_start1000_iter60_malloc_20260711_081056`
  fit in memory, but at `44s` elapsed it had only reached iteration `10`,
  with best raw lower bound `87,378,000` and `55,689` disagreements. It could
  not beat p8/w8 start-1000, and p9/w9 later proved faster still.
- At this point in the sweep, p8/w8 was the best local TCP adhead schedule:
  start `1000`, four schedule levels, max `60`, `objective_scale=2000`,
  malloc BK, no compression:
  `benchmark_results/local_tcp_adhead_n6c10_p8_w8_os2000_start1000_iter60_malloc_20260711_080335`.

## 2026-07-11 08:15 PDT

- Supersede the 08:07 "current best" p8/w8 note: p9/w9 with the same start
  `1000`, max `60`, `objective_scale=2000`, malloc BK, and no compression is
  now the best observed adhead local TCP point. Runs
  `benchmark_results/local_tcp_adhead_n6c10_p9_w9_os2000_start1000_iter60_malloc_20260711_081212`
  and
  `benchmark_results/local_tcp_adhead_n6c10_p9_w9_os2000_start1000_iter60_malloc_repeat_20260711_081347`
  completed in `44.48s` and `44.70s`, versus p8/w8 at `50.22s`.
- Do not skip directly to p9/w9 schedule start `100` for this adhead target.
  Run
  `benchmark_results/local_tcp_adhead_n6c10_p9_w9_os2000_start100_iter60_malloc_20260711_081644`
  was stopped after `51.2s`: at the first progress checkpoint it had only
  reached iteration `10`, with best raw lower bound `72,358,000` and `70,155`
  disagreements. The p9/w9 start-1000 run completes in about `44.6s`, so
  start `100` is a clear regression despite avoiding the scale-1000 warmup.
- Do not reduce the p9/w9 start-1000 per-scale cap to `30`. Run
  `benchmark_results/local_tcp_adhead_n6c10_p9_w9_os2000_start1000_iter30_malloc_20260711_081931`
  finished in `46.4s` coordinator wall with status `1`, stop_reason `4`, and
  `11` final disagreements. It also ran `106` total iterations, more than the
  p9/w9 max-60 baseline's `79`, because the early cap forced work down to
  scale `1` without recovering agreement.

## 2026-07-11 07:36 PDT

- Do not use the current `MCPD3_PARTITIONER=local` region-grow partitioner for
  this `babyface.n6c10` p2/w2 local TCP target. A one-iteration probe,
  `benchmark_results/local_tcp_partitioner_local_p2_w2_iter1_pass0_malloc_babyface_saturate_20260711_071901`,
  reduced boundary endpoints from `125,000` to `103,469`, but the full
  schedule probe
  `benchmark_results/local_tcp_partitioner_local_p2_w2_iter23_pass0_malloc_babyface_saturate_20260711_071957`
  was already worse than the basic-partitioner baseline by total iteration
  `20`: aggregate solve RPC `272,450,865us` versus `192,078,882us` for the
  basic p2/w2 start `10000` run. It was stopped early.
- Do not lower this schedule all the way to start `10` under the current
  regularization budget. Run
  `benchmark_results/local_tcp_fullschedule_p2_w2_iter23_start10_malloc_babyface_saturate_20260711_072755`
  emitted `regularization budget 507200 is not below limit 1000`, promoted the
  objective scale, and had already spent `94,434,400us` aggregate solve RPC by
  total iteration `10`; it was stopped early.
- Do not use start `50` with max `23` iterations per scale as the final tuned
  setting. Run
  `benchmark_results/local_tcp_fullschedule_p2_w2_iter23_start50_malloc_babyface_saturate_20260711_072953`
  was fast at `18,295,812us`, but ended with status `2`, stop_reason `3`, and
  `22` final disagreements. Raising the cap to `60` fixes agreement and keeps
  the wall time essentially unchanged.
- Nearby clean starts `25`, `40`, `75`, `100`, and `1000` are all slower than
  start `50` max `60` on this benchmark. They are useful controls, not the
  current target.

## 2026-07-11 07:16 PDT

- Do not lower the p2/w2 local TCP full-schedule cap to `21` for
  `babyface.n6c10`. Run
  `benchmark_results/local_tcp_fullschedule_p2_w2_iter21_malloc_babyface_saturate_20260711_071146`
  ended with status `2`, stop_reason `3`, final disagreements `9`, total
  iterations `95`, wall `194,974,638us`, and final certified lower bound
  `19447.6`; it did not produce primal agreement.
- Do not prefer p2/w2 max `22` over max `23` even though it reaches agreement.
  Run
  `benchmark_results/local_tcp_fullschedule_p2_w2_iter22_malloc_babyface_saturate_20260711_070710`
  took `196,501,411us` versus `193,613,915us` for max `23`, and reported
  final objective `19448` with final certified lower bound `19447.7`.
- Current best clean point for this benchmark remains p2/w2 max `23`:
  `benchmark_results/local_tcp_fullschedule_p2_w2_iter23_malloc_babyface_saturate_20260711_070323`.

## 2026-07-11 06:56 PDT

- Do not use p5/w5 max `60` as a full local TCP solve despite its relatively
  low wall time. Run
  `benchmark_results/local_tcp_fullschedule_p5_w5_iter60_malloc_babyface_saturate_20260711_063018`
  ended with status `2`, stop_reason `3`, final disagreements `186`, total
  iterations `310`, and wall `212,537,857us`.
- Do not lower the p2/w2 per-scale cap to `20` for this schedule. Run
  `benchmark_results/local_tcp_fullschedule_p2_w2_iter20_malloc_babyface_saturate_20260711_064902`
  was faster at `190,365,239us`, but ended with status `2`, stop_reason `3`,
  final disagreements `6`, and best certified lower bound `19445.3`; it did
  not produce primal agreement.
- p3/w3 max `40` is no longer the target even though it reaches agreement.
  It took `273,475,096us` and emitted a regularization budget warning, while
  p2/w2 max `25` reached agreement in `197,034,265us` without that warning.

## 2026-07-11 06:28 PDT

- Do not pick full-schedule local TCP parameters from the short single-scale
  benchmark alone. The p15/w15 point was best for the 10-iteration probe, but
  it did not reach agreement in full schedule even with larger caps:
  - max `40`: final disagreements `482`, wall `317,670,112us`;
  - max `60`: final disagreements `125`, wall `357,928,457us`;
  - max `80`: final disagreements `213`, stop_reason `4`
    (`NO_LOWER_BOUND_IMPROVEMENT`), wall `359,631,211us`.
- p6/w6 and p4/w4 are tempting because they are faster than the clean p3/w3
  solve, but under the tested settings they still leave boundary
  disagreements:
  - p6/w6 max `60`: `40` disagreements, `224,366,623us`;
  - p6/w6 max `80`: `191` disagreements, stop_reason `4`,
    `228,433,826us`;
  - p4/w4 max `60`: `93` disagreements, `232,498,630us`.
- Current clean full-schedule local TCP point is p3/w3 max `60`, not p15, p6,
  or p4:
  `benchmark_results/local_tcp_fullschedule_p3_w3_iter60_malloc_babyface_saturate_20260711_061859`
  reached status `0`, stop_reason `2`, and final disagreements `0` in
  `277,151,276us`.

## 2026-07-11 05:50 PDT

- Do not treat the p15/w15 full schedule with max `20` iterations per scale as
  a clean local TCP solve for `babyface.n6c10`.
- Strict overflow mode failed at total_iteration `51`:
  `benchmark_results/local_tcp_fullschedule_p15_w15_malloc_babyface_none_20260711_054117`.
  The regularization budget warning forced objective-scale promotion, and
  scaling the existing 32-bit capacities by another decade exceeded the int
  range.
- Saturating overflow mode completed but still did not reach agreement:
  `benchmark_results/local_tcp_fullschedule_p15_w15_malloc_babyface_saturate_20260711_054457`
  ended with status `2` / stop_reason `3` (`ITERATION_COUNT_EXCEEDED`), final
  objective scale `100000`, `2` promotions, `195` total iterations, and
  `3,099` remaining disagreements after `293,768,552us` wall time.
- Interpretation: saturation is useful for continuing past overflow in
  exploratory runs, but it is not itself a convergence fix. Future full-schedule
  probes should increase the per-scale iteration cap or improve the schedule
  logic rather than assuming truncation will finish the primal recovery.

## 2026-07-11 05:38 PDT

- `perf stat` is not usable for this optimization pass under the current
  system permissions. The kernel has `perf_event_paranoid=4`, and the process
  lacks the needed profiling capabilities.
- Command attempted:
  `perf stat -e task-clock,context-switches,cpu-migrations,cycles,instructions,cache-misses -- ...`
- Future profiling should either lower `perf_event_paranoid`, run with the
  required capability, or use a userspace/instrumented profiler. Do not spend
  time trying normal unprivileged `perf` commands in this environment.

## 2026-07-11 05:35 PDT

- Do not add a second vector/index-map cache to the temporal label delta codec
  as a presumed CPU fast path. The prototype kept wire bytes identical but
  regressed p15/w15 local TCP wall time badly and was reverted.
- Benchmark evidence, `babyface.n6c10`, p15/w15, `--max-iterations 20`
  stopping after 11 iterations, malloc-backed BK storage, no compression:
  - baseline p15/w15
    `benchmark_results/local_tcp_early_listen_20iter_p15_w15_malloc_babyface_none_20260711_053009`:
    total `29,852,306us`, setup `4,945,809us`, solve `21,232,761us`,
    aggregate partition-load RPC `63,945,975us`;
  - label-vector fast path
    `benchmark_results/local_tcp_label_vector_fastpath_p15_w15_malloc_babyface_none_20260711_053359`:
    total `33,110,423us`, setup `8,016,959us`, solve `21,458,399us`,
    aggregate partition-load RPC `108,246,485us`;
  - label-vector fast path repeat
    `benchmark_results/local_tcp_label_vector_fastpath_p15_w15_malloc_babyface_none_20260711_053432`:
    total `34,632,108us`, setup `9,284,759us`, solve `21,714,667us`,
    aggregate partition-load RPC `127,515,380us`.
- Interpretation: the extra label state likely worsens memory/cache pressure
  enough to dominate any avoided hash lookups. Keep the simpler map-only codec
  until profiling shows a more precise CPU hotspot.

## 2026-07-11 05:14 PDT

- For the current in-memory-sized `babyface.n6c10` local TCP p16/w16
  10-iteration probe, do not prefer file-backed BK mmap over malloc when RAM is
  available.
- Evidence:
  - malloc p16/w16
    `benchmark_results/local_tcp_early_listen_10iter_p16_w16_malloc_babyface_none_20260711_051029`:
    total `27,878,130us`, setup `5,153,077us`, solve `19,015,852us`;
  - malloc repeat
    `benchmark_results/local_tcp_early_listen_10iter_repeat_p16_w16_malloc_babyface_none_20260711_051254`:
    total `28,369,133us`, setup `5,185,741us`, solve `19,487,864us`;
  - file-backed mmap with sequential advice
    `benchmark_results/local_tcp_early_listen_10iter_p16_w16_filemmap_babyface_none_20260711_051409`:
    total `28,865,596us`, setup `5,461,839us`, solve `19,692,961us`.
- File-backed BK mmap remains important for out-of-core runs, but malloc is
  still faster for this memory-resident local benchmark.

## 2026-07-11 05:03 PDT

- Do not use `MSG_WAITALL` in the blocking TCP receive loops as a localhost
  optimization. It passed the test suite, but did not improve the current p3/w3
  local TCP point and was reverted.
- Benchmark evidence, `babyface.n6c10`, p3/w3, one iteration,
  malloc-backed BK storage, no compression:
  - `benchmark_results/local_tcp_msg_waitall_p3_w3_malloc_babyface_none_20260711_050304`:
    total `6,426,036us`, setup `2,062,099us`, aggregate partition-load RPC
    `5,856,389us`;
  - `benchmark_results/local_tcp_msg_waitall_p3_w3_malloc_babyface_none_20260711_050311`:
    total `6,427,339us`, setup `2,055,062us`, aggregate partition-load RPC
    `5,864,840us`;
  - `benchmark_results/local_tcp_msg_waitall_p3_w3_malloc_babyface_none_20260711_050317`:
    total `6,398,423us`, setup `2,024,147us`, aggregate partition-load RPC
    `5,772,121us`.
- These are in the same noise band as the normal p3 repeats
  (`6,389,020us` to `6,438,462us`), with no clear upside.

## 2026-07-11 04:59 PDT

- Do not switch the normal local TCP benchmark build to ad hoc
  `-march=native` flags as currently attempted.
- `build-nativeopt` with Snappy enabled failed to compile Snappy after CMake
  detected AVX paths; `snappy.cc` referenced `__m256i` without the required
  intrinsic type being available in that translation unit.
- `build-nativeopt-nosnappy` compiled with
  `-DMCPD4_ENABLE_SNAPPY=OFF -DCMAKE_CXX_FLAGS_RELEASE='-O3 -DNDEBUG -march=native'`,
  but it regressed the current p3/w3 local TCP benchmark:
  - normal Release p3 repeats are around `6,389,020us` to `6,438,462us`;
  - native/no-Snappy
    `benchmark_results/local_tcp_native_nosnappy_p3_w3_malloc_babyface_none_20260711_045919`:
    total `7,672,341us`, setup `3,136,764us`, aggregate partition-load RPC
    `9,137,059us`;
  - native/no-Snappy repeat
    `benchmark_results/local_tcp_native_nosnappy_p3_w3_malloc_babyface_none_20260711_045927`:
    total `7,683,460us`, setup `3,144,745us`, aggregate partition-load RPC
    `9,150,974us`.
- Interpretation: any build-profile work needs a more controlled CMake option
  and a Snappy-compatible configuration. The quick native/no-Snappy profile is
  slower for the current localhost transfer workload.

## 2026-07-11 04:57 PDT

- Explicitly setting `MCPD3_PARTITIONER=basic` is not a meaningful speed win
  for the current local TCP `babyface.n6c10` p3/p6 probes. It removes the
  `unknown=metis fallback=basic` warning, but timings stayed in the same band:
  - p3/w3
    `benchmark_results/local_tcp_early_listen_basic_partitioner_p3_w3_malloc_babyface_none_20260711_045747`:
    total `6,435,543us`, partition `970,816us`, setup `2,060,543us`;
  - p6/w6
    `benchmark_results/local_tcp_early_listen_basic_partitioner_p6_w6_malloc_babyface_none_20260711_045753`:
    total `6,448,699us`, partition `1,083,978us`, setup `2,241,770us`.
- Snappy compression is still slower on localhost at the current p3/w3 point:
  `benchmark_results/local_tcp_early_listen_snappy_p3_w3_malloc_babyface_20260711_050019`
  cut coordinator TX wire bytes from about `395MB` to `187MB`, but total wall
  rose to `6,960,255us` and compression took `669,749us`.
- Interpretation: for local TCP, keep `--rpc-compression none`; Snappy is more
  plausible for slower real networks than for localhost.

## 2026-07-11 04:43 PDT

- Do not reduce the local TCP worker count below the partition count for the
  current p6 `babyface.n6c10` configuration when enough memory is available.
  Fewer workers reduce process count but force each worker to load multiple
  partitions serially, increasing setup wall time.
- Valid restored-binary sweep, p6 fixed partitions, one iteration,
  malloc-backed BK storage, no compression:
  - w2: total `9,399,596us`, setup `5,004,025us`;
  - w3: total `7,849,642us`, setup `3,475,420us`;
  - w4: total `7,831,989us`, setup `3,461,129us`;
  - w5: total `7,928,203us`, setup `3,564,479us`;
  - w6: total `6,541,356us`, setup `2,208,413us`.
- Ignore the earlier `local_tcp_worker_sweep_p6_w*_...` directories from this
  timestamp range: they were accidentally run against stale packed-arc
  binaries after the source revert. Use only `local_tcp_worker_sweep_restored`
  for product comparisons.

## 2026-07-11 04:39 PDT

- Do not use eager 24-bit arc-endpoint packing for worker-load partition
  packages on localhost. It reduced partition-load bytes by the expected
  amount, but the extra coordinator packing copy plus worker unpack cost made
  setup much slower.
- Benchmark evidence on `babyface.n6c10`, p6/w6, one iteration,
  malloc-backed BK storage, no compression:
  - current mmap-reader baseline
    `benchmark_results/local_tcp_mmap_reader_p6_w6_malloc_babyface_none_20260711_042215`:
    total `6,553,760us`, setup `2,221,805us`, aggregate partition-load RPC
    `12,205,983us`, partition-load TX `405,000,240` bytes;
  - packed 24-bit arcs
    `benchmark_results/local_tcp_packed_u24_arcs_p6_w6_malloc_babyface_none_20260711_043730`:
    total `7,777,341us`, setup `3,457,282us`, aggregate partition-load RPC
    `19,499,694us`, partition-load TX `344,250,240` bytes;
  - packed 24-bit arcs repeat
    `benchmark_results/local_tcp_packed_u24_arcs_repeat_p6_w6_malloc_babyface_none_20260711_043737`:
    total `7,741,303us`, setup `3,449,981us`, aggregate partition-load RPC
    `19,497,700us`, partition-load TX `344,250,240` bytes.
- Interpretation: saving `60,750,000` wire bytes was not enough to offset the
  CPU and memory-copy cost of eager packing/unpacking. Any future arc endpoint
  compression should avoid materializing a separate packed copy, or be reserved
  for slower networks where bandwidth dominates.

## 2026-07-11 04:28 PDT

- Do not replace the worker's direct compact directed-capacity receive path
  with chunked socket reads that expand directly into the final full capacity
  vector. The idea was to avoid the temporary compact capacity vector and a
  second expansion pass, but it made local TCP setup substantially slower.
- Benchmark evidence on `babyface.n6c10`, p6/w6, one iteration,
  malloc-backed BK storage, no compression:
  - current mmap-reader baseline
    `benchmark_results/local_tcp_mmap_reader_p6_w6_malloc_babyface_none_20260711_042215`:
    total `6,553,760us`, setup `2,221,805us`, aggregate partition-load RPC
    `12,205,983us`;
  - chunked direct expansion
    `benchmark_results/local_tcp_direct_cap_expand_p6_w6_malloc_babyface_none_20260711_042654`:
    total `7,789,046us`, setup `3,332,625us`, aggregate partition-load RPC
    `18,788,815us`;
  - chunked direct expansion repeat
    `benchmark_results/local_tcp_direct_cap_expand_repeat_p6_w6_malloc_babyface_none_20260711_042702`:
    total `7,733,168us`, setup `3,331,078us`, aggregate partition-load RPC
    `18,929,735us`.
- Interpretation: large contiguous `recv` into the compact vector followed by
  in-memory expansion is faster than many chunked socket reads on localhost,
  even though it temporarily holds the compact vector.

## 2026-07-03 02:28 PDT

- Do not use a BK mmap directory on a memory-backed filesystem. Paths under
  `/tmp` are not portable: this laptop has `/tmp` on ext4, but many Linux
  systems mount `/tmp` or `/dev/shm` as `tmpfs`. If BK `file_mmap` lands on
  tmpfs, it is effectively RAM-backed and can kill the worker during partition
  loading. mcpd4 workers now reject `tmpfs`, `ramfs`, and `hugetlbfs` for BK
  mmap dirs.
- The p32 large-adhead distributed restart connected both workers, then the
  remote worker disconnected while loading partition `6` after completing
  partitions `0`, `2`, and `4`. Do not interpret the coordinator-side
  `socket closed during read` as a network root cause without first checking
  the remote worker log, mount point for `--bk-mmap-dir`, free disk, and kernel
  OOM messages. Remote BK mmap directory disk exhaustion is the leading
  suspicion for this run.

## 2026-07-03 02:08 PDT

- Do not assume BK arrays are file-backed unless worker status says
  `bk_storage file_mmap`. The p24/p32/p48 large-adhead resident runs were
  launched before mcpd4 defaulted workers to file-backed BK storage, and the
  logged worker commands lacked `--bk-storage file_mmap`/`--bk-mmap-dir`.
  Kernel OOM logs showed `anon-rss` around `7.8 GB` and `file-rss` near zero,
  confirming heap pressure rather than file-backed page-cache pressure.
- Do not rely on an implicit BK mmap directory for large production benchmarks
  on a nearly full root filesystem. The default is `/var/tmp`, but large runs
  should pass `--bk-mmap-dir` on a known disk-backed fast filesystem with
  enough free space for that worker's assigned BK node/arc arrays.

## 2026-07-03 01:54 PDT

- Do not rely on the coordinator UDP status endpoint for post-mortem errors.
  It is an in-process thread and disappears when the coordinator exits. Use
  `mcpd4_coordinator --status-file PATH` and inspect it with
  `mcpd4_status --file PATH` after a crash/failure.
- Do not treat a bare `socket write failed: Broken pipe` or
  `socket closed during read` as sufficient distributed-run diagnostics.
  That error only says the peer disconnected. The runtime now logs partition
  load begin/done/failed lines with worker and partition context so the failing
  package can be identified.

## 2026-07-03 01:30 PDT

- Do not benchmark large adhead resident distributed workers on the 15 GB
  laptop with default malloc-backed BK storage. p24 and p32 killed the local
  worker during package load; p48 also failed during package send/load. Use
  BK `file_mmap` with a fast directory that has enough free space, use more
  worker memory, or keep the laptop as coordinator-only.

## 2026-07-03 01:04 PDT

- Large adhead p16 resident distributed run failed under the old TCP frame cap:
  workers reported `frame payload exceeds maximum size` because p16 partition
  packages are roughly 320-359 MiB logical, while the runtime still had a
  256 MiB receive limit. Snappy does not avoid this because the cap applies to
  the logical protocol frame before compression. Fix: use the shared 1 GiB
  frame cap and sender-side enforcement; if a future graph exceeds that cap,
  increase the partition count or implement multipart partition-package RPC.

## 2026-07-02 00:53 PDT

- Do not evict and cold-reload a regularized partition while preserving only
  capacities and alpha metadata. The scaled-epsilon regularizer anchors from
  the previous local cut labels; losing that label vector made a forced
  streaming reload disagree with the resident in-process worker on
  regularization budget diagnostics.
- Historical pre-warm-state limitation: disk-backed streaming workers were
  not initially a warm-solver performance optimization. That version preserved
  correctness-critical alpha and label state across eviction, but rebuilt BK
  solver/residual state on reload.

## 2026-06-29 00:24:08 PDT

- Do not add TCP, MPI, or serialization before the in-process partition-worker
  API has an equivalence test.
- Do not move product deployment/runtime concerns into `third_party/mcpd3`.
- Do not rename the `third_party/mcpd3` solver, its public `mcpd3::` API
  namespace, or upstream branch as part of the product rebrand. The product
  wrapper is `mcpd4`; the solver dependency remains `mcpd3`.
- Do not reintroduce the removed Polyak step policy.
- Do not depend on local benchmark files from
  `/home/matt/software/graph-cuts-undirected`.
- Do not claim exact min-cut optimality from heuristic/non-lexicographic
  regularized agreement. Exact claims require local subproblems to report the
  unregularized optimum after a lexicographic `M * F(x) + R(x)` tie-break.

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

## 2026-06-29 00:39:30 PDT

- Expected TDD red state: the two-round coordinator test initially failed to
  compile because `decomp/partition_coordinator.h` did not exist. This was
  resolved by mcpd3 commit `e65eaa4`.
- Current coordinator limitation: `PartitionWorkerCoordinator` supports the
  MVP shape of one package per worker object and sends all alpha records each
  round. Multi-package workers and changed-alpha deltas remain future work.

## 2026-06-29 00:57:34 PDT

- Expected TDD red state: full-solve tests initially failed to compile because
  `PartitionWorkerCoordinator` had no `solve()` API, no solve-loop options, no
  optimization status enum, and no stop-reason enum. This was resolved by
  mcpd3 commit `9e2d530`.
- Still out of scope for this step: primal upper-bound decoding is not mapped
  to the worker-coordinator path. That remains the next tracker item.

## 2026-06-29 02:47 PDT

- Discarded the unregularized-confirmation idea for lexicographic
  regularization: a plain unregularized re-solve can select a different
  optimizer among tied local optima, so it is not the right certificate for
  the chosen regularized labels.
- Important caveat found while testing: simple source-side local ties already
  choose source under the current maxflow implementation, so one-sided
  lexicographic regularization may be redundant in those cases. Tests now
  cover the exactness contract rather than claiming every small tie case needs
  the regularizer.
- Do not restore the old additive local regularization path as a certificate:
  it can perturb strict local optima. The hardened path must keep
  `M > max(R)` and report the unregularized lower-bound term.

## 2026-06-29 02:58 PDT

- The committed exact one-sided lexicographic regularizer has not produced a
  case where it is required for convergence. Random searches over simple local
  and two-partition traces found zero strict-need cases.
- Do not test regularization convergence only at scale `10`. A concrete cycle
  with source terminal `-10`, target terminal `+8`, and step size `10` is
  resolved by continuing to scale `1`.
- Do not treat that concrete cycle as a strict regularization-required case:
  forced-unregularized scale `1` also reaches agreement.
- The previous additive regularization scheme at commit `9e2d530` still fails
  this full-schedule case for checked iteration budgets `12`, `20`, and `30`
  when low-scale regularization is active immediately.
- If old additive regularization is delayed until after the unregularized
  scale-`1` path has already agreed, this case succeeds, but that does not
  validate the old regularizer.

## 2026-06-29 08:57 PDT

- Symmetric DD-style regularization should not be lumped in with local unary
  regularization failures. It changes alpha updates symmetrically and cancels
  on agreement.
- Early positive result: `SYMMETRIC_ALPHA_SHIFT` resolves fixed step-`10`
  one-node cycle variants where local lexicographic regularization remains
  disagreeing.
- Still not validated broadly: do not make symmetric alpha-shift the default
  without stress tests beyond the committed tiny synthetic cases.

## 2026-06-29 14:50 PDT

- Randomized initial Lagrange multipliers should not be treated as a stronger
  regularization scheme. Committed tests show both outcomes on the same fixed
  step-`10` cycle family: one seed reaches first-round unregularized
  agreement, while another seed misses and remains disagreeing.
- Use randomized initial alphas as diagnostic/experimental evidence for
  alpha-offset behavior, not as an MVP default or an optimality argument.

## 2026-06-29 17:15 PDT

- Do not cite Waterloo `BL06-gargoyle-med` as a current reproduced
  regularization-required case under the checked setup. With directed
  streaming input, 10 basic partitions, 4 threads, and regularization disabled,
  it reached `best_gap=0` and `final_disagreement_count=0`.
- The GARG-med objective needs the reader's terminal imbalance offset when
  comparing against the `.sol` file. The checked no-reg value was
  `68173681`; adding the reported imbalance `29806257` gives the provided
  solution value `97979938`.
- Randomized initial alpha is not automatically beneficial on GARG-med:
  radius `9999`, seed `1`, with no regularization ended with
  `best_gap=1024` and `final_disagreement_count=10`.
- Symmetric alpha shift is not automatically beneficial on GARG-med:
  shift `1` ended with `best_gap=1024` and
  `final_disagreement_count=7`.
- The local lexicographic run did not test final recovery on GARG-med because
  it closed at step size `100` with zero regularization budget. This is a
  useful regression check, not evidence that lexicographic recovery is needed
  on this instance.

## 2026-06-29 18:14 PDT

- Do not leave `dimacs_dual_decomp_example` premultiplying all DIMACS
  capacities by `10000` by default. That was historical compatibility for the
  old approximate regularization scheme and also changed alpha resolution.
  The benchmark binary now defaults to `capacity_multiplier=1`.
- Do not compare unscaled benchmark logs until objective/reporting scale is
  decoupled from DD step size. Before commit `c398487`, reported
  `*_unscaled` values were only meaningful because the example also
  premultiplied capacities by the same value as the initial step.
- `babyface.n6c10` is not currently a clean regularization-required example:
  unscaled no-reg runs stalled at lower bound `1373` versus `.sol=19448`;
  compatibility scaled symmetric alpha shift was worse than scaled no-reg;
  compatibility scaled local-search partitioning was also worse than scaled
  no-reg.
- Do not use local lexicographic regularization as a broad benchmark
  screening mode on `babyface.n6c10` without tighter controls. The checked
  compatibility scaled run reached step size `10` but each local solve took
  about 19-20 seconds and still had hundreds of disagreements when stopped.
- Do not launch `adhead.n6c10` blindly in the current 15GB RAM environment.
  It is much larger than babyface, swap is already full, and babyface used
  about `5.3GB` RSS.

## 2026-06-29 19:31 PDT

- Do not conclude that `babyface.n6c10` is not a regularization/convergence
  case. The original `early_experiments` branch converges to
  `lower_bound=19448` with zero disagreement on this instance.
- Do not treat the current exact lexicographic regularizer as a drop-in
  replacement for the old additive low-scale regularizer. On
  `babyface.n6c10`, the current exact path becomes orders of magnitude slower
  at step size `10` and did not reach agreement in the checked window.
- Do not compare current branch regularization experiments only through
  `--stream-directed-input`. The original branch used the general
  `read_dimacs()` path. Directed streaming changes the decomposition surface
  and should be validated separately before using it as benchmark evidence.

## 2026-06-29 19:57 PDT

- Do not treat the OG regularization warning as an exactness certificate. The
  proof requires a summed global effective regularization range below the
  objective scale; the OG check is per local solver and warning-only.
- Do not use final regularization contribution as the budget for the proof.
  The required budget is the maximum possible regularization swing in the
  local solves whose lower bounds are summed.
- Do not assume the OG incremental budget variable measures the actual
  perturbation in the maxflow graph. Skipped constrained nodes can retain stale
  terminal perturbations, and changing low-scale regularization strength does
  not force a full terminal recomputation.

## 2026-06-29 20:45 PDT

- Do not anchor every previous sink boundary copy on every low-scale solve.
  That over-broad productized variant over-regularized `babyface.n6c10`:
  `final_regularization_budget_raw=876603`, far above the `10000` objective
  scale, and it still ended with hundreds of disagreements. The useful OG
  shape refreshes anchors only when the local DD alpha term changes.
- Do not hide stale or persistent epsilon terms from diagnostics. The hardened
  implementation treats active epsilon terms as explicit state and reports the
  full active budget, including terms that persist across unchanged-alpha
  solves.
- Superseded by later objective-scale promotion work: at this point the
  over-budget behavior was intentionally warning-only. If
  `regularization_budget >= regularization_budget_limit`, the run may still
  stop on regularized agreement for now, but that result must be treated as
  potentially non-certifying until future code rejects, rescales, or otherwise
  handles the over-budget condition.
- Do not use `babyface.n6c10` as the primary benchmark for this step. It
  remains a useful sanity/regression case, but the benchmark comparison
  requested here is `adhead.n6c10` against the OG scheme.
- The OG `early_experiments` benchmark run on this machine required a local
  worktree-only shim to remove unused CSR/primal-decoding code that depended
  on missing Boost headers. That shim did not modify the OG DD solver or
  regularizer, but benchmark notes should mention it.

## 2026-06-29 21:43 PDT

- Do not use `--capacity-multiplier 100` without objective-scale promotion for
  the scaled-epsilon `adhead.n6c10` benchmark. It reached zero disagreement,
  but the active regularization budget exceeded the strict limit by orders of
  magnitude (`11036 >= 100`), so the run was non-certifying and the reported
  lower bound rose above the known optimum.
- Lowering the multiplier from `10000` to `100` without handling over-budget
  regularization did not improve performance on `adhead.n6c10`; wall time
  increased from `1:24.18` to `3:31.91`.

## 2026-06-29 22:43 PDT

- Do not accept a lower bound from an iteration whose scaled-epsilon active
  regularization budget is greater than or equal to the objective scale. The
  dynamic-promotion implementation now detects this before updating the best
  lower bound, then promotes the objective scale and restarts the schedule.
- Do not assume a smaller initial multiplier is faster. With dynamic
  promotion, `adhead.n6c10 --capacity-multiplier 100` became certifying by
  promoting to `1000`, but still took `3:26.77` versus `1:24.18` for starting
  directly at `10000`.
- The first dynamic-promotion commit only covered the current monolithic
  `DualDecomposition` benchmark path. That was incomplete for productization;
  the productized `PartitionWorkerCoordinator` also needs promotion/rescale
  handling.

## 2026-06-29 22:58 PDT

- Do not describe the current monolithic `DualDecomposition` path as a
  separate old branch. It is current code, but it is not the whole
  productized worker-coordinator path.
- Do not let worker-coordinator over-budget rounds mutate alpha state or enter
  accepted progress. The coordinator now computes disagreement diagnostics for
  the over-budget round, rejects its lower bound, skips alpha updates, and
  then either promotes or returns `REGULARIZATION_BUDGET_EXCEEDED`.
- Remaining distributed-protocol note: the in-process worker API has
  `scaleObjective(long factor)`, but the future TCP protocol still needs an
  explicit rescale message before remote workers can preserve live solver
  state across promotion.

## 2026-06-29 23:12 PDT

- Do not interpret the low-`M` opposite-direction cycle failure with
  `num_optimization_scales=2` as a regularization failure. That setup was
  artificially under-provisioned after promotion: starting at `M=10` promotes
  to `M=100`, and the restarted schedule needs enough scale levels and
  low-scale iterations to reach agreement.
- The corrected low-`M` cycle regression uses `num_optimization_scales=3` and
  `max_iteration_count=100`, and it passes with one promotion to `M=100`.

## 2026-06-29 23:30 PDT

- Do not model primal upper-bound decoding as persistent worker state for the
  MVP. The coordinator can request explicit worker-side computation later if
  it needs primal information.
- Do not assume one package per worker object. The in-process worker API now
  supports multiple loaded packages per worker, and solve requests must carry
  a `partition_id` when a worker owns more than one partition.

## 2026-06-29 23:38 PDT

- Do not add TCP behavior into the Stage 3 serialization module. Stage 3 is
  bytes-in/bytes-out only; socket lifecycle and partial network reads belong
  to Stage 4.
- The current frame decoder intentionally expects a complete frame buffer.
  Stage 4 can add a stream/framing reader that accumulates bytes from sockets
  before calling the strict decoder.

## 2026-06-29 23:56 PDT

- Do not test remote objective scaling with an unconstrained one-node local
  partition. That local problem can report a valid zero lower bound, so it is
  not a useful witness that `SCALE_OBJECTIVE` rescaled the live remote solver.
  The Stage 4 test now includes a source-side boundary constraint so the bound
  is nonzero before scaling and must scale by the requested factor.
- Do not treat Stage 4 loopback runtime as operational fault tolerance. It now
  propagates worker `ERROR` frames and rejects invalid handshakes, but worker
  disconnect/reconnect, heartbeat, replacement, and process-level fixture
  tests remain Stage 6/Stage 5 work.

## 2026-06-30 00:12 PDT

- Do not start process-level workers after an arbitrary sleep and assume the
  coordinator is listening. The Stage 5 test and local benchmark hook use the
  coordinator's `--ready-file` signal before launching workers to avoid
  nondeterministic connection-refused failures.
- Do not commit local Waterloo/bunny/adhead benchmark data paths into the test
  suite. Keep committed Stage 5 fixtures tiny and deterministic; use
  `scripts/run_local_process_benchmark.sh` for user-supplied local datasets.

## 2026-06-30 00:45 PDT

- Do not assume `adhead.n6c10 --capacity-multiplier 10000` is safe in the
  product/distributed path. The real max DIMACS arc capacity is `999999`, so
  scaling by `10000` exceeds the 32-bit package/solver capacity type.
- Do not treat saturating capacity overflow as exact. It is an explicit
  benchmark/compatibility mode for running the current 32-bit implementation
  when a requested multiplier is too large. Any run with
  `capacity_scale_saturation_count > 0` solved a clipped-capacity problem.
- Do not hide the distinction between modulo overflow and saturation. The old
  monolithic benchmark path multiplied `int` capacities unchecked. The product
  compatibility mode clamps to `INT_MAX`/`INT_MIN`, which is safer and
  diagnosable but not bit-for-bit old-overflow behavior.

## 2026-06-30 00:58 PDT

- Do not treat process-level coordinator/worker tests as evidence of parallel
  solve throughput by themselves. Before the `PartitionWorkerCoordinator`
  dispatch fix, the coordinator contacted workers serially, so a 4-worker
  `adhead` run showed one worker at 100% CPU while the coordinator and the
  other workers were idle.

## 2026-06-30 01:23 PDT

- Do not diagnose a one-hot-worker snapshot as a deadlock without checking
  round-level progress. After concurrent dispatch, `adhead.n6c10` still shows
  barrier tails where one worker finishes a much more expensive partition set
  while other workers wait for the next coordinator round.
- Do not rely on `ps %CPU` alone for solver health. It is lifetime-averaged
  and can miss short round-boundary changes. Use `--progress-every N` for
  optimizer fields and `pidstat` only as a process-level liveness/imbalance
  supplement.
- Do not treat the current `M=10000` saturated adhead telemetry smoke as an
  exact benchmark. It clipped `328844` capacities before solving; the smoke was
  only to validate health telemetry and identify load imbalance.

## 2026-06-30 09:33 PDT

- Do not use the old monolithic `adhead.n6c10 --capacity-multiplier 10000`
  result as an exact overflow-safe benchmark. It happened to reach the correct
  cut because `999999 * 10000` wrapped to a large positive value
  (`1410055408`) and those arcs still behaved as effectively infinite on this
  instance.
- Do not compare distributed/TCP with fewer workers than partitions and expect
  monolithic-like timing. With 4 workers and 10 partitions, multiple expensive
  partitions can be pinned behind one worker connection and solved
  sequentially. The exact local comparison should use 10 workers for 10
  partitions until there is a real dynamic work-stealing or batched
  multi-partition worker protocol.
- Do not resend full alpha state every round. Most alpha records are unchanged
  after early iterations; dirty-only alpha sync reduced exact adhead cumulative
  worker RPC overhead from `87912105 us` to `28968042 us`.

## 2026-06-30 10:09 PDT

- The prior one-worker-per-partition comparison rule is obsolete after the
  batched worker RPC implementation. Do not use older 4-worker/10-partition
  results from the per-partition RPC path as representative of current
  performance.
- Do not confuse fixed ownership batching with dynamic load balancing. A
  worker can now receive one batch and solve its owned partitions concurrently,
  but partition ownership is still static for the run; work stealing and
  repartitioning remain future work.
- Do not interpret summed `timing_coordinator_wait_worker_us` as elapsed wall
  time. It is accumulated across parallel worker RPCs and can exceed
  `timing_solve_wall_us`; use the wall-time fields plus per-worker progress
  timing to diagnose imbalance.

## 2026-06-30 10:10 PDT

- Do not treat the selected unregularized value from a regularized local solve
  as a certified original-problem lower bound. The local solver selects
  `argmin(F + r)` but reports `F(x_selected)`, which can be above the true
  local minimum of `F`.
- The conservative certificate for an accepted regularized round is:
  `F(x_selected) + r(x_selected) - R`, where `R` is the active regularization
  budget and `r(x_selected)` is the actual regularization contribution paid by
  the selected solution. Subtracting `R` directly from `F(x_selected)` is safe
  but unnecessarily pessimistic; not subtracting the slack at all can overstate
  the lower bound.

## 2026-06-30 10:31 PDT

- Do not revert to round-robin as the default static partition map. It ignores
  both package-size skew and worker resources. The current setup path uses a
  deterministic largest-partition-first weighted assignment from package node,
  arc, and boundary counts plus worker CPU/RAM estimates.
- Do not treat static weighted assignment as dynamic load balancing. Packages
  are still sent once and remain owned by the assigned worker for the solve;
  moving packages later would require explicit migration/reload semantics.
- Do not compare the pre-balancing 4-worker batched adhead run against the
  post-balancing run without noting the ownership change. On the checked
  `adhead.n6c10` run, static weighted assignment improved wall time from
  `3:45.06` to `3:22.74`.

## 2026-06-30 10:46 PDT

- Do not use the conservative certified lower bound as the user-facing solved
  objective after budget-safe regularized agreement. For adhead, the certificate
  is `48372.9` while the selected/original final objective is `48373`.
- Do not expose a separate `selected_objective` /
  `best_selected_objective` user-facing field. It duplicates `final_objective`
  once agreement certifies primal recovery and makes the reporting semantics
  harder to reason about. Keep `final_objective` for the solved value, and keep
  certified lower-bound plus regularized-objective fields as diagnostics.

## 2026-07-01 22:44 PDT

- Do not treat the first native mcpd3 adhead run as a completed benchmark.
  Command:
  `MCPD3_PARTITIONER=basic build/mcpd3-native/mcpd3_native_monolith_benchmark data/maxflow/adhead.n6c10/adhead.n6c10.max --directed --partitions 10 --objective-scale 1000 --schedule-start 10000 --schedule-levels 5 --max-iterations 10000`.
  It was terminated by SIGTERM after `552.78s` with no final result.
- Do not compare that aborted native run against mcpd4 distributed timings.
  It showed about `8.1 GB` RSS and roughly one-core CPU use before termination,
  so the next investigation should compare direct `DualDecomposition::solve()`
  stopping/scheduling against the worker-coordinator path before drawing
  performance conclusions.
- Do not rely on fully buffered benchmark stdout for long runs. The native
  benchmark now sets unbuffered stdout, but the aborted run happened before
  that change, leaving an empty `.out` file.

## 2026-07-01 23:39 PDT

- Do not treat the `adhead.n6c10 p16 objective_scale=1000` failure as proof
  that p16 has an inherent algorithmic convergence bug. The observed failure
  was: regularization budget exceeded `1000`, the coordinator attempted to
  promote by `10x`, and 32-bit capacity promotion overflowed.
- Exact p16 does converge when the objective scale is chosen high enough but
  still within int32 input capacity limits: `p16 objective_scale=2000` reached
  agreement with objective `48373`.
- `--truncate-capacity-overflow` now lets p16/os1000 promotion proceed to
  scale `10000`, but that run may clip promoted capacities. Use it only as
  explicit compatibility mode, not as the exact local baseline.

## 2026-07-02 00:17 PDT

- Do not run large-adhead distributed package generation through solver-backed
  `DualDecomposition` export. Before `construct_solvers=false`, p32
  partition-only on `adhead.n26c100` climbed to about `14.1 GB` RSS with swap
  full and only about `7.5 GB` disk free before it was stopped. The issue was
  double construction: local native solvers were being built only to export
  packages, then worker solvers would be built again.
- Do not assume fewer partitions are automatically safer for the large local
  setup path. p24 has lower boundary/package overhead than p32, but with BK
  `file_mmap` it drove free disk down to about `3.7 GB` before setup completed,
  so it was manually terminated. On this laptop, p32 completed setup while p24
  did not.
- Do not treat p40/p48 as better just because local subproblems are smaller.
  Partition-only probes increased boundary endpoint counts from p32's
  `4,194,304` to `5,275,972` and `6,291,456`, respectively, with no partition
  wall-time improvement.

## 2026-07-11 01:44 PDT

- Do not treat `babyface.n6c10` local TCP p10/w10 `objective_scale=1000` as an
  exact completed benchmark. Run
  `benchmark_results/local_tcp_optimized_full_babyface_p10_w10_none_20260711_013605`
  reached iteration `96`, best certified lower bound raw `18,599,400`, then
  exceeded the regularization budget and failed during objective-scale
  promotion with `objective scale promotion exceeds int`.
- Do not treat `babyface.n6c10` `objective_scale=10000` strict mode as runnable
  with the current int32 capacity representation. Run
  `benchmark_results/local_tcp_optimized_full_babyface_p10_w10_os10000_none_20260711_014141`
  failed during the initial scale pass with `objective scale exceeds int
  range`.
- If using `--saturate-capacity-overflow` for these local TCP performance
  sweeps, keep the result labeled as clipped-capacity compatibility data. The
  saturated `babyface.n6c10` scale-10000 run clipped `11,370` terminal
  capacities during initial scaling, so it is useful for transport/runtime
  performance but not an exact strict-capacity proof.

## 2026-07-11 02:06 PDT

- Do not use one `send()` per partition-package buffer as the scatter/gather
  implementation. Run
  `benchmark_results/local_tcp_scatter_package_defaultbk_babyface_p10_w2_none_20260711_020321`
  was slower than the contiguous move-return copy-drop run on the comparable
  mmap-backed p10/w2 one-iteration setup point:
  - per-buffer send: setup `17,758,739us`, total `24,141,107us`;
  - contiguous move-return copy drop:
    `benchmark_results/local_tcp_sendrecv_copydrop_defaultbk_babyface_p10_w2_none_20260711_015836`
    setup `17,300,658us`, total `23,832,448us`.
- Scatter/gather should use `sendmsg` or another batched write path. The
  `sendmsg` run
  `benchmark_results/local_tcp_sendmsg_package_defaultbk_babyface_p10_w2_none_20260711_020445`
  recovered the per-buffer-send loss while preserving the coordinator memory
  pressure benefit.

## 2026-07-11 02:24 PDT

- Do not claim the mcpd3 boundary-map reserve change as a proven standalone
  speedup from the p10/w10 local TCP benchmark. The first run improved setup
  substantially (`7,951,065us`), but the repeat was close to the previous
  p10/w10 baseline (`9,117,897us` versus `9,250,669us`). Keep the change as
  allocation hygiene with test coverage, and use the worker-count sweep as the
  stronger result.

## 2026-07-11 02:32 PDT

- Do not enable `TCP_NODELAY` as a claimed local TCP setup optimization based
  on current evidence. A temporary implementation with loopback coverage was
  benchmarked on p10/w10/file-backed mmap with `willneed`, then reverted:
  `benchmark_results/local_tcp_nodelay_willneed_filemmap_babyface_p10_w10_none_20260711_023104`
  setup `9,150,496us`, total `15,845,669us`, worse than the `willneed` runs
  without TCP_NODELAY.
- Do not default to snappy compression for localhost p10/w10 setup. It reduced
  coordinator wire TX from `572,751,240` to `278,055,791` bytes in
  `benchmark_results/local_tcp_snappy_filemmap_babyface_p10_w10_20260711_022738`,
  but setup `8,391,191us` and total `14,641,085us` did not beat no-compression
  with BK mmap `willneed`.

## 2026-07-11 02:42 PDT

- Do not compare local TCP benchmark runs launched concurrently as if they were
  independent points. The first p5/w5 and p7/w7 after-memory-free probes were
  started at the same time:
  - `benchmark_results/local_tcp_willneed_after_memfree_babyface_p5_w5_none_20260711_024001`;
  - `benchmark_results/local_tcp_willneed_after_memfree_babyface_p7_w7_none_20260711_024001`.
  They share CPU, disk, and page cache pressure, so use the later sequential
  p5/w5 and p7/w7 runs for comparisons.
- Do not run benchmarks from this worktree with a relative `data/...` DIMACS
  path unless the data directory has been linked into the worktree. Run
  `benchmark_results/local_tcp_willneed_after_memfree_babyface_p10_w10_none_20260711_023518`
  failed before graph load because `data/maxflow/babyface.n6c10/babyface.n6c10.max`
  was not present under `.worktrees/local-tcp-opt`; the valid rerun used the
  absolute path from the main checkout.

## 2026-07-11 02:53 PDT

- Do not switch localhost p6/w6 local TCP to Snappy by default after the compact
  worker-load package change. Run
  `benchmark_results/local_tcp_omit_l2g_snappy_filemmap_babyface_p6_w6_20260711_025317`
  reduced coordinator wire TX to `249,634,612` bytes, but took
  `9,425,194us` total versus `8,778,549us` for no-compression in
  `benchmark_results/local_tcp_omit_l2g_filemmap_babyface_p6_w6_none_20260711_024927`.
  Compression alone cost `1,100,580us`.

## 2026-07-11 03:01 PDT

- Do not claim compact endpoint records as a proven localhost wall-time speedup
  on p6/w6. The change reduced partition-load TX from `532,500,240` to
  `526,500,240` bytes and preserved outputs, but the measured file-backed run
  was `8,887,600us` versus `8,778,549us` before endpoint compaction, and the
  measured malloc run was `8,376,554us` versus `8,290,466us`. Treat it as a
  transport-size optimization, not a localhost timing win.

## 2026-07-11 03:07 PDT

- Do not assume fewer partitions are faster once malloc-backed BK storage fits.
  With `13Gi` available, p2/w2 through p7/w7 all fit on `babyface.n6c10`, but
  the one-iteration local TCP totals still favored p6/w6:
  - p2/w2: `12,078,335us`;
  - p3/w3: `8,478,821us`;
  - p4/w4: `8,522,239us`;
  - p5/w5: `10,385,090us`;
  - p6/w6 repeat: `8,324,249us`;
  - p7/w7: `9,580,102us`.
- p2 has the smallest partition-load transfer in this sweep, but its solve time
  (`4,773,711us`) dominates. Use p6/w6 as the current local TCP comparison
  point until a larger/more complete convergence benchmark says otherwise.

## 2026-07-11 03:54 PDT

- Do not push the `babyface.n6c10` one-iteration local TCP comparison above
  p6/w6 on this 12-thread laptop without new evidence. After the directed
  scaled-reader and compact single-direction capacity improvements, p8/p10/p12
  were all slower than the current p6/w6 point:
  - current p6/w6 baseline
    `benchmark_results/local_tcp_scaled_directed_reader_repeat_malloc_babyface_p6_w6_none_20260711_034426`:
    total `6,931,610us`, setup `2,227,187us`, solve `905,142us`,
    partition-load TX `405,000,240` bytes;
  - p8/w8
    `benchmark_results/local_tcp_scaled_reader_sweep_p8_w8_malloc_babyface_none_20260711_035406`:
    total `7,961,762us`, setup `2,983,056us`, solve `1,285,035us`,
    partition-load TX `411,750,320` bytes;
  - p10/w10
    `benchmark_results/local_tcp_scaled_reader_sweep_p10_w10_malloc_babyface_none_20260711_035414`:
    total `8,051,264us`, setup `3,498,093us`, solve `741,053us`,
    partition-load TX `418,500,400` bytes;
  - p12/w12
    `benchmark_results/local_tcp_scaled_reader_sweep_p12_w12_malloc_babyface_none_20260711_035422`:
    total `8,743,463us`, setup `4,110,057us`, solve `721,358us`,
    partition-load TX `425,250,480` bytes.
- More workers reduce one-iteration solve time at p10/p12, but setup grows more
  than enough to lose the total wall-time comparison. p6/w6 remains the current
  local TCP tuning point for small `babyface.n6c10` probes.

## 2026-07-11 03:18 PDT

- Do not replace `DualDecomposition::initializeDecomposition` constrained-node
  partition sets with per-node `std::vector<int>` de-duplication based on the
  current evidence. The experiment preserved results but did not improve the
  matched p6/w6 malloc/no-compression `babyface.n6c10` one-iteration probe:
  - post-reader baseline repeat
    `benchmark_results/local_tcp_presized_dimacs_repeat_malloc_babyface_p6_w6_none_20260711_031335`:
    total `7,993,139us`, partition `1,327,133us`, setup `3,260,440us`;
  - vector boundary-set run
    `benchmark_results/local_tcp_vector_boundary_sets_malloc_babyface_p6_w6_none_20260711_031732`:
    total `8,105,027us`, partition `1,388,056us`, setup `3,288,289us`;
  - vector boundary-set repeat
    `benchmark_results/local_tcp_vector_boundary_sets_repeat_malloc_babyface_p6_w6_none_20260711_031757`:
    total `8,076,785us`, partition `1,380,602us`, setup `3,279,833us`.
- The uncommitted code was reverted. If this area is revisited, first add
  finer-grained package-build timing so we can see whether boundary map/set
  maintenance is actually material on larger partition counts.

## 2026-07-11 03:28 PDT

- Do not use the narrower directed-capacity compaction condition "all backward
  capacity slots are zero." It passed tests but did not reduce
  `babyface.n6c10` package bytes because mcpd3 normalizes arcs by node order,
  so one-way directed capacities can appear in either the forward or backward
  slot. Run
  `benchmark_results/local_tcp_compact_directed_caps_malloc_babyface_p6_w6_none_20260711_032427`
  still transmitted `526,500,240` partition-load bytes and took
  `8,116,278us` total.
- The winning version is the signed single-direction format that compacts an
  arc when at most one direction has nonzero capacity.

## 2026-07-11 03:34 PDT

- Do not use sparse terminal-capacity worker-load payloads in the current local
  TCP path. The experiment encoded mostly-zero terminal vectors as
  `(local_index, capacity)` pairs and reduced p6/w6 `babyface.n6c10`
  partition-load bytes from `405,000,240` to `383,341,200`, but it made setup
  substantially slower:
  - baseline after single-direction capacity compaction:
    `benchmark_results/local_tcp_compact_single_dir_caps_cached_malloc_babyface_p6_w6_none_20260711_032828`,
    total `7,090,183us`, setup `2,250,509us`, load RPC aggregate
    `12,318,888us`;
  - sparse terminal run:
    `benchmark_results/local_tcp_sparse_terminals_malloc_babyface_p6_w6_none_20260711_033351`,
    total `8,195,761us`, setup `3,344,169us`, load RPC aggregate
    `18,880,272us`;
  - sparse terminal repeat:
    `benchmark_results/local_tcp_sparse_terminals_repeat_malloc_babyface_p6_w6_none_20260711_033419`,
    total `8,139,662us`, setup `3,321,798us`, load RPC aggregate
    `18,838,163us`.
- The code was reverted before commit. If terminal sparsity is revisited, it
  needs a lower-overhead worker-side construction path, not just a smaller wire
  representation that expands back into a full vector before BK load.

## 2026-07-11 03:37 PDT

- Do not default localhost p6/w6 local TCP to Snappy even after
  single-direction capacity compaction. Rebuilt no-compression baseline
  `benchmark_results/local_tcp_rebuilt_compact_caps_malloc_babyface_p6_w6_none_20260711_033648`
  took `7,047,881us` total, setup `2,241,004us`, and sent
  `405,000,744` wire bytes. Snappy reduced wire bytes to `190,318,936`, but
  remained slower:
  - `benchmark_results/local_tcp_rebuilt_compact_caps_snappy_malloc_babyface_p6_w6_20260711_033711`:
    total `7,391,746us`, setup `2,550,101us`, compression `793,746us`;
  - `benchmark_results/local_tcp_rebuilt_compact_caps_snappy_repeat_malloc_babyface_p6_w6_20260711_033735`:
    total `7,394,210us`, setup `2,547,665us`, compression `768,422us`.
- Snappy may still be useful across machines or slower networks, but it is a
  local-loopback loss at this problem size because compression CPU dominates
  the saved wire transfer.

## 2026-07-11 03:52 PDT

- Do not replace worker-side compact directed arc-capacity expansion with a
  direct receive-time expansion path in the current local TCP implementation.
  The goal was to avoid allocating a temporary compact vector before expanding
  to the full `arc_capacities` vector, but both variants were slower on the
  matched p6/w6 `babyface.n6c10` one-iteration probe:
  - chunked receive/expand run
    `benchmark_results/local_tcp_direct_expand_compact_caps_malloc_babyface_p6_w6_none_20260711_035039`:
    total `8,010,827us`, setup `3,328,416us`, load RPC aggregate
    `18,955,468us`;
  - one-read in-place backward expansion run
    `benchmark_results/local_tcp_inplace_expand_compact_caps_malloc_babyface_p6_w6_none_20260711_035141`:
    total `7,989,347us`, setup `3,332,146us`, load RPC aggregate
    `18,992,711us`;
  - current scaled-reader baseline
    `benchmark_results/local_tcp_scaled_directed_reader_repeat_malloc_babyface_p6_w6_none_20260711_034426`:
    total `6,931,610us`, setup `2,227,187us`, load RPC aggregate
    `12,276,980us`.
- Bytes and solver output were unchanged, so the regression is CPU/path
  overhead rather than transport volume. The implementation code was reverted.
  A stronger compact directed TCP test was kept to compare remote loaded solves
  against an in-process worker and cover both signed compact capacity branches.

## 2026-07-11 04:00 PDT

- Do not add an always-on worker-side timing hook around
  `worker->loadPartition` in `runWorkerClient`. The change only added
  `steady_clock` measurement, two status fields, and a `load_wall_us` log
  suffix, but it reproducibly slowed the p6/w6 `babyface.n6c10` local TCP
  setup path:
  - dirty timing-hook run
    `benchmark_results/local_tcp_worker_load_timing_probe_p6_w6_malloc_babyface_none_20260711_035744`:
    total `8,025,182us`, setup `3,361,237us`, load RPC aggregate
    `18,926,363us`;
  - dirty timing-hook repeat
    `benchmark_results/local_tcp_worker_load_timing_probe_repeat_p6_w6_malloc_babyface_none_20260711_035837`:
    total `7,993,266us`, setup `3,316,498us`, load RPC aggregate
    `18,895,258us`;
  - clean detached control worktree at `430813d`
    `benchmark_results/control_p6_w6_malloc_babyface_none_20260711_040030`:
    total `6,921,185us`, setup `2,201,914us`, load RPC aggregate
    `12,170,273us`.
- The control run was built and executed from a separate worktree at the same
  pushed commit, with the same Release build type and benchmark command. The
  timing-hook code was reverted. If this split is needed later, make it opt-in
  and re-measure the off path first.

## 2026-07-11 08:26 PDT

- Do not use `MCPD3_PARTITIONER=local` as a drop-in replacement for the tuned
  p9/w9 `adhead.n6c10` local TCP configuration. Run
  `benchmark_results/local_tcp_adhead_n6c10_p9_w9_localpart_os2000_start1000_iter60_malloc_20260711_082348`
  used the same memory-resident malloc worker shape as the current best
  basic-partitioner run, but was both slower and nonconvergent:
  - status `1`, stop_reason `4`;
  - final objective/certified lower bound `48373`, but final disagreements
    `10`;
  - objective scale promoted from `2000` to `20000`;
  - total iterations `221`, wall `110,724,502us`, solve `91,148,177us`;
  - regularization budget warning: `3330 >= 2000`.
- The current best basic-partitioner p9/w9 controls remain
  `benchmark_results/local_tcp_adhead_n6c10_p9_w9_os2000_start1000_iter60_malloc_20260711_081212`
  and repeat
  `benchmark_results/local_tcp_adhead_n6c10_p9_w9_os2000_start1000_iter60_malloc_repeat_20260711_081347`,
  both exact with zero disagreements in about `44.5s`.
