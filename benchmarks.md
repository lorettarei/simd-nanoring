# SIMD NanoRing: Performance Benchmarks

**Platform:** Apple Silicon (ARM64 NEON)  
**Threads available:** 12  
**Compiler:** Clang 21.0  

## Results

| Experiment | Configuration | Messages | Time (ms) | Throughput (Mpps) | Notes |
| :--- | :--- | ---: | ---: | ---: | :--- |
| SIMD Decode | Scalar (sequential field extraction) | 20000000 | 25.21 | 793.42 |  |
| SIMD Decode | SIMD Vectorized (4-wide batch) | 20000000 | 27.04 | 739.58 | decoded=20000000 |
| AoS vs SoA | AoS scan (stride-48 per field) | 20000000 | 12.68 | 1576.68 | matches=156051 |
| AoS vs SoA | SoA scalar scan (contiguous fields) | 20000000 | 7.40 | 2703.46 | matches=156051 |
| AoS vs SoA | SoA SIMD scan (vectorized compare) | 20000000 | 4.14 | 4829.02 | matches=156051 |
| Broadcast Scale | 1 consumer thread(s) | 20000000 | 51.66 | 387.12 | total_reads=20000000 |
| Broadcast Scale | 2 consumer thread(s) | 20000000 | 67.45 | 296.51 | total_reads=40000000 |
| Broadcast Scale | 4 consumer thread(s) | 20000000 | 97.48 | 205.17 | total_reads=80000000 |
| Broadcast Scale | 8 consumer thread(s) | 20000000 | 267.32 | 74.82 | total_reads=160000000 |
| Pipeline | 1 book thread(s) | 20000000 | 654.76 | 30.55 | per_thread=[20000000] |
| Pipeline | 2 book thread(s) | 20000000 | 641.46 | 31.18 | per_thread=[8418057,11582020] |
| Pipeline | 4 book thread(s) | 20000000 | 336.99 | 59.35 | per_thread=[4381257,5939497,4045074,5642025] |
| Pipeline | 8 book thread(s) | 20000000 | 386.14 | 51.79 | per_thread=[2809209,3746293,2802964,2186929,1563474,2188680,1251553,3443020] |
| Latency | Producer→Consumer (1 thread) | 2000000 | 0.01 | 171423.67 | P50=42ns P99=1875ns P99.9=7667ns |
| Book Compute | Single book (all symbols, sequential) | 20000000 | 1686.07 | 11.86 | adds=11999882 execs=3999414 bbo_chg=27 |
| Book Compute | Sharded manager (sequential) | 20000000 | 762.25 | 26.24 | symbols=63 msgs=20000000 |
| Book Compute | Parallel sharded (2 threads) | 20000000 | 826.45 | 24.20 | total_processed=20000000 |
| Book Compute | Parallel sharded (4 threads) | 20000000 | 515.78 | 38.78 | total_processed=20000000 |
| Book Compute | Parallel sharded (8 threads) | 20000000 | 329.05 | 60.78 | total_processed=20000000 |
| Journal Opts | Naive (1 consumers) | 20000000 | 51.13 | 391.18 |  |
| Journal Opts | Naive (2 consumers) | 20000000 | 66.25 | 301.90 |  |
| Journal Opts | Naive (4 consumers) | 20000000 | 97.97 | 204.15 |  |
| Journal Opts | Naive (8 consumers) | 20000000 | 243.36 | 82.18 |  |
| Journal Opts | Batched-64 + cached (1 consumers) | 20000000 | 25.27 | 791.55 |  |
| Journal Opts | Batched-64 + cached (2 consumers) | 20000000 | 25.77 | 776.21 |  |
| Journal Opts | Batched-64 + cached (4 consumers) | 20000000 | 25.86 | 773.30 |  |
| Journal Opts | Batched-64 + cached (8 consumers) | 20000000 | 65.48 | 305.44 |  |
| Broadcast vs Routed | Broadcast (1 threads) | 20000000 | 644.54 | 31.03 | efficiency=112% |
| Broadcast vs Routed | Broadcast (2 threads) | 20000000 | 496.46 | 40.28 | efficiency=72% |
| Broadcast vs Routed | Broadcast (4 threads) | 20000000 | 291.55 | 68.60 | efficiency=62% |
| Broadcast vs Routed | Broadcast (8 threads) | 20000000 | 257.19 | 77.76 | efficiency=35% |
| Broadcast vs Routed | Routed (1 threads) | 20000000 | 933.94 | 21.41 | per_thread=[20000000] |
| Broadcast vs Routed | Routed (2 threads) | 20000000 | 785.71 | 25.45 | per_thread=[8437068,11562932] |
| Broadcast vs Routed | Routed (4 threads) | 20000000 | 452.62 | 44.19 | per_thread=[4374305,5938121,4062763,5624811] |
| Broadcast vs Routed | Routed (8 threads) | 20000000 | 553.90 | 36.11 | per_thread=[2812212,3750596,2813958,2186214,1562093,2187525,1248805,3438597] |

## Analysis

### Data-Level Parallelism (SIMD)
The vectorized decoder processes 4 messages per instruction using ARM NEON 128-bit registers.
Byte-swap operations (big-endian ITCH → little-endian host) are vectorized.

### Pipeline Parallelism
Three-stage pipeline: SIMD Decoder → Broadcast Journal → Sharded Order Books.
Each stage runs on dedicated cores with zero shared mutable state between book threads.

### Concurrency Scaling
Symbol-hash sharding eliminates cross-thread contention entirely.
Broadcast journal keeps index cache line in MESI Shared state (read-only consumers).
