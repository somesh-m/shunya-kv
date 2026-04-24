
# ShunyaKV In-Memory Benchmark Report

## Overview
This report presents **latency and throughput benchmarks** for **ShunyaKV**, an in-memory key-value store built on an event-driven, shard-per-core architecture.

The benchmarks evaluate:
- Service latency under low offered load
- Scaling behavior as concurrency increases
- Queueing effects at high throughput

All measurements were taken with the database operating entirely in memory, without any disk I/O.

## Benchmark Results

### Balanced QPS & Latencies

**Server:** c7g.xlarge (CPU: 4, Mem: 8GB)\
**Client:** c7g.2xlarge (CPU: 8, Mem: 16GB)

**Cache Eviction Config:**

|  | Probationary Pool | Sanctuary Pool |
| :---: | :---: | :---: |
| **Eviction Algo** | Reaper(FIFO) | Sieve |
| **Size(%)** | 75 | 25 |
| **Trigger(%)** | 80 | 80 |
| **Budget(Count)** | 500 | 500 |

**Scenario 1 (Non DPDK)**
Conns: 4
Pipeline: 18
Max Key: 620,300
Run Duration: 120s
Cache max key count: 154104 * 4 = 616,416

| Workload Ratio (Set:Get) | Throughput (ops/sec) | p50 | p90 | p95 | p99 | p99.9 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **1:0** | 1321856 | 0.320 | 0.710 | 0.834 | 1.038 | 1.390 |
| **0:1** | 1669015 | 0.248 | 0.496 | 0.586 | 0.784 | 1.023 |
| **1:2** | 1311247 | 0.304 | 0.688 | 0.838 | 1.282 | 3.375 |

**Scenario 2 (DPDK)**\
Conns: 4\
Pipeline: 32\
Max Key: 204404\
Run Duration: 120s\
Cache max key count: 51101 * 4 = 204404\

| Workload Ratio (Set:Get) | Throughput (ops/sec) | p50 | p90 | p95 | p99 | p99.9 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **1:0** | 2020725 | 0.291 | 0.762 | 0.919 | 1.215 | 1.536 |
| **0:1** | 2537331 | 0.187 | 0.487 | 0.605 | 1.842 | 1.067|
| **1:2** | 2023710 | 0.265 | 0.788 | 0.964 | 1.602 | 2.785

*To avoid Out-Of-Memory (OOM) termination on 8GB instances, the maximum cache size was limited to 2GB, leaving 6GB for DPDK's hugepage allocation and operating system overhead.*


**Server:** c7g.2xlarge (CPU: 8, Mem: 16GB)\
**Client:** c7g.4xlarge (CPU: 16, Mem: 32GB)

**Cache Eviction Config:**

|  | Probationary Pool | Sanctuary Pool |
| :---: | :---: | :---: |
| **Eviction Algo** | Reaper(FIFO) | Sieve |
| **Size(%)** | 75 | 25 |
| **Trigger(%)** | 80 | 80 |
| **Budget(Count)** | 500 | 500 |

**Scenario 1 (Non DPDK)**\
Conns: 6\
Pipeline: 10\
Max Key: 1,406,900\
Run Duration: 120s\
Cache max key count: 175662 * 8 = 1,405,296

| Workload Ratio (Set:Get) | Throughput (ops/sec) | p50 | p90 | p95 | p99 | p99.9 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **1:0** | 3029411 | 0.267 | 0.700 | 0.856 | 1.249 | 1.885 |
| **0:1** | 3905488 | 0.200 | 0.471 | 0.585 | 0.884 | 1.364 |
| **1:2** | 3003761 | 0.247 | 0.705 | 0.923 | 1.602 | 4.149 |

**Scenario 2 (DPDK)**\
Conns: 6\
Pipeline: 10\
Max Key: 1,406,900\
Run Duration: 120s\
Cache max key count: 175662 * 8 = 1,405,296

| Workload Ratio (Set:Get) | Throughput (ops/sec) | p50 | p90 | p95 | p99 | p99.9 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **1:0** | 3,459,203 | 0.223 | 0.586 | 0.719 | 1.038 | 1.533 |
| **0:1** | 4,514,472 | 0.161 | 0.374 | 0.458 | 0.667 | 0.992 |
| **1:2** | 3,402,916 | 0.214 | 0.592 | 0.821 | 1.457 | 4.018 |

**All the latency figures are in ms(milliseconds)*

### Sub ms Latency

**Max QPS at sub ms latencies**

**Server:** c7g.xlarge (CPU: 4, Mem: 8GB)
**Client:** c7g.2xlarge (CPU: 8, Mem: 16GB)

**Cache Eviction Config:**

|  | Probationary Pool | Sanctuary Pool |
| :---: | :---: | :---: |
| **Eviction Algo** | Reaper(FIFO) | Sieve |
| **Size(%)** | 75 | 25 |
| **Trigger(%)** | 80 | 80 |
| **Budget(Count)** | 500 | 500 |

**Scenario 1 (Non DPDK)**\
Conns: 4\
Pipeline: 12\
Max Key: 620,300\
Run Duration: 120s\
Cache max key count: 154104 * 4 = 616,416

| Workload Ratio (Set:Get) | Throughput (ops/sec) | p50 | p90 | p95 | p99 | p99.9 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **1:0** | 1157404 | 0.243 | 0.529 | 0.623 | 0.801 | 1.020 |
| **0:1** | 1290664 | 0.195 | 0.343 | 0.396 | 0.489 | 0.608 |
| **1:2** | 1153544 | 0.244 | 0.492 | 0.595 | 0.934 | 2.157 |

**Scenario 2 (DPDK)**
Conns: 4
Pipeline: 18
Max Key: 620,300
Run Duration: 120s
Cache Key Size: 51101 * 4 = 204404

| Workload Ratio (Set:Get) | Throughput (ops/sec) | p50 | p90 | p95 | p99 | p99.9 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **1:0** | 1857183 | 0.192 | 0.464 | 0.534 | 0.690 | 0.929 |
| **0:1** | 2285447 | 0.157 | 0.326 | 0.385 | 0.500 | 0.655 |
| **1:2** | 1858979 | 0.200 | 0.454 | 0.563 | 0.965 | 1.916 |

*To avoid Out-Of-Memory (OOM) termination on 8GB instances, the maximum cache size was limited to 2GB, leaving 6GB for DPDK's hugepage allocation and operating system overhead.*

**Server:** c7g.2xlarge (CPU: 8, Mem: 16GB)\
**Client:**c7g.4xlarge (CPU: 16, Mem: 32GB)

**Cache Eviction Config:**

|  | Probationary Pool | Sanctuary Pool |
| :---: | :---: | :---: |
| **Eviction Algo** | Reaper(FIFO) | Sieve |
| **Size(%)** | 75 | 25 |
| **Trigger(%)** | 80 | 80 |
| **Budget(Count)** | 500 | 500 |

**Cache Pool Size:** 175662 * 8 = 1,405,296

**Client Config:**

**Scenario 1 (Non DPDK)**\
Conns: 2\
Pipeline: 4\
Max Key: 1,406,900\
Run Duration: 120s\
Cache max key count: 175662 * 8 = 1,405,296\

| Workload Ratio (Set:Get) | Throughput (ops/sec) | p50 | p90 | p95 | p99 | p99.9 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **1:0** | 2,330,753 | 0.150 | 0.351 | 0.421 | 0.571 | 0.877 |
| **0:1** | 2,803,757 | 0.138 | 0.242 | 0.287 | 0.389 | 0.585 |
| **1:2** | 2,312,260 | 0.156 | 0.318 | 0.399 | 0.697 | 1.985 |

**Scenario 2 (DPDK)**
Conns: 2
Pipeline: 4
Max Key: 1,406,900
Run Duration: 120s
Cache max key count: 175662 * 8 = 1,405,296

| Workload Ratio (Set:Get) | Throughput (ops/sec) | p50 | p90 | p95 | p99 | p99.9 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **1:0** | 2,725,446 | 0.123 | 0.302 | 0.358 | 0.473 | 0.623 |
| **0:1** | 3,432,051 | 0.110 | 0.183 | 0.216 | 0.291 | 0.393 |
| **1:2** | 2,768,299 | 0.126 | 0.254 | 0.319 | 0.612 | 1.571 |

*All the latency figures are in ms(milliseconds)*
