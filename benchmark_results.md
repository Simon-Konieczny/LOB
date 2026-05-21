# LOB Benchmark Results

Benchmarks were executed using Google Benchmark to ensure proper cache warm-up, statistical stabilization, and memory barrier enforcement.

**Environment:**
* Architecture: Apple M4 Pro
* OS: macOS
* Compiler: Clang / GCC (-O3 -march=native)

## Google Benchmark Output
```text
Run on (14 X 24.0001 MHz CPU s)
CPU Caches:
L1 Data 64 KiB
L1 Instruction 128 KiB
L2 Unified 4096 KiB (x14)
Load Average: 2.51, 2.53, 2.64

----------------------------------------------------------------------------------------------
Benchmark                                    Time             CPU   Iterations UserCounters...
----------------------------------------------------------------------------------------------
BM_OrderBookAdd/iterations:10000000       36.8 ns         36.8 ns     10000000 items_per_second=27.1646M/s
```
## Latency Distribution
```text
========================================================
   M4 Pro Latency Distribution (ASCII Histogram)
========================================================
[   0 -  100 ns] | ######################################## (98.3%)
[100  - 200  ns] |                                          (0.0%)
[200  - 300  ns] |                                          (0.0%)
[300  - 400  ns] |                                          (0.0%)
[400  - 500  ns] |                                          (0.0%)
[500  - 600  ns] |                                          (0.0%)
[600  - 700  ns] |                                          (0.0%)
[700  - 800  ns] |                                          (0.0%)
[800  - 900  ns] |                                          (0.0%)
[900  - 1000 ns] |                                          (0.0%)
[1000 - 1100 ns] |                                          (1.7%)

--------------------------------------------------------
Percentiles:
p50  : 0.0 ns
p90  : 0.0 ns
p99  : 1000.0 ns
p99.9: 1000.0 ns
========================================================
```
`std::chrono::high_resolution_clock` on macOS forms a bottleneck depending on kernel state cause invalid `p50` metric (addOrder performed in under `~41ns` clock cycle). `1000ns` p90 caused by kernel interuptions on macOS.