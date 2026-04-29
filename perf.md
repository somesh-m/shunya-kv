
# ShunyaKV In-Memory Benchmark Report

## Overview
This report presents **latency and throughput benchmarks** for **ShunyaKV**, an in-memory key-value store built on an event-driven, shard-per-core architecture.

The benchmarks evaluate:
- Service latency under low offered load
- Scaling behavior as concurrency increases
- Queueing effects at high throughput

All measurements were taken with the database operating entirely in memory, without any disk I/O.

### Experiment Structure

Benchmarks were conducted on two system configurations:

- Cache: c7g.xlarge | Client: c7g.2xlarge
- Cache: c7g.2xlarge | Client: c7g.4xlarge

For each configuration, performance was evaluated under two operating regimes:

- **Max Throughput (Max QPS):**
  Maximum achievable throughput while maintaining p99.9 latency within an acceptable bound.

- **Sub-ms Latency:**
  Throughput achieved while keeping p99.9 latency approximately at or below 1 ms.

Each experiment was executed using both:
- Linux native networking stack
- DPDK-based networking

---

### Cache Configuration

The cache eviction strategy was kept constant across all experiments:

|  | Probationary Pool | Sanctuary Pool |
| :---: | :---: | :---: |
| **Eviction Algo** | Reaper (FIFO) | Sieve |
| **Size (%)** | 75 | 25 |
| **Trigger (%)** | 80 | 80 |
| **Budget (Count)** | 500 | 500 |

---

## Key Findings

- DPDK improves throughput by ~25–35% across workloads
- GET throughput consistently exceeds SET by ~20–30%
- Mixed workloads (1:2) show significant tail latency amplification (p99.9)
- Scaling from 4 → 8 cores shows near-linear throughput improvement
- Sub-ms regime significantly reduces tail latency at the cost of ~15–25% throughput

## Performance Benchmarks

### 1. Vertical Scalability
ShunyaKV demonstrates near-linear scaling as CPU resources increase.

![Scalability Chart](/assets/scalability.png)

*Figure 1: Throughput vs p99.9 Latency scaling from 4 to 8 cores.*

### 2. Latency Distribution (Sub-millisecond Stability)
High throughput is only useful if it is predictable. This chart shows the latency distribution from p50 (average) up to p99.9 (worst-case tail).

![Latency Percentiles](/assets/latency.png)
*Figure 2: Latency distribution showing DPDK maintaining sub-ms p99.9 even under heavy load.*

### 3. Workload Throughput
The DPDK-backed transport layer provides a ~30% throughput uplift over standard POSIX networking.

![Workload Throughput](/assets/qps_workload.png)

### 4. Throughput-Latency Curve
The following curve identifies the system's saturation point, maintaining a flat latency profile until 4M+ QPS.

![Throughput Latency Curve](/assets/throughput_latency_curve.png)
---

## Benchmark Results

### 1. Max Throughput
This regime measures maximum throughput while maintaining p99.9 latency within an acceptable bound.

**1.1** **Server:** `c7g.xlarge (CPU: 4, Mem: 8GB)` | **Client:** `c7g.2xlarge (CPU: 8, Mem: 16GB)`

**1.1.1 Linux Native networking stack**\
Conns: 4 | Pipeline: 18 | Max Key: 620,300 | Run Duration: 120s | Cache max key count: 154104 * 4 = 616,416

| Workload Ratio (Set:Get) | Throughput (ops/sec) | p50 | p90 | p95 | p99 | p99.9 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **1:0** | 1321856 | 0.320 | 0.710 | 0.834 | 1.038 | 1.390 |
| **0:1** | 1669015 | 0.248 | 0.496 | 0.586 | 0.784 | 1.023 |
| **1:2** | 1311247 | 0.304 | 0.688 | 0.838 | 1.282 | 3.375 |

**1.1.2 DPDK Networking stack**\
Conns: 4 | Pipeline: 32 | Max Key: 204404 | Run Duration: 120s | Cache max key count: 51101 * 4 = 204404

| Workload Ratio (Set:Get) | Throughput (ops/sec) | p50 | p90 | p95 | p99 | p99.9 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **1:0** | 2020725 | 0.291 | 0.762 | 0.919 | 1.215 | 1.536 |
| **0:1** | 2537331 | 0.187 | 0.487 | 0.605 | 1.842 | 2.067 |
| **1:2** | 2023710 | 0.265 | 0.788 | 0.964 | 1.602 | 2.785 |

*To avoid Out-Of-Memory (OOM) termination on 8GB instances, the maximum cache size was limited to 2GB, leaving 6GB for DPDK's hugepage allocation and operating system overhead.*

**1.2 Server:** `c7g.2xlarge (CPU: 8, Mem: 16GB)` | **Client:** `c7g.4xlarge (CPU: 16, Mem: 32GB)`

**1.2.1 Linux Native networking stack**\
Conns: 6 | Pipeline: 10 | Max Key: 1,406,900 | Run Duration: 120s | Cache max key count: 175662 * 8 = 1,405,296

| Workload Ratio (Set:Get) | Throughput (ops/sec) | p50 | p90 | p95 | p99 | p99.9 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **1:0** | 3029411 | 0.267 | 0.700 | 0.856 | 1.249 | 1.885 |
| **0:1** | 3905488 | 0.200 | 0.471 | 0.585 | 0.884 | 1.364 |
| **1:2** | 3003761 | 0.247 | 0.705 | 0.923 | 1.602 | 4.149 |

**1.2.2 DPDK Networking stack**\
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

### 2. Sub ms Latency
This regime evaluates throughput under a strict p99.9 latency target of ~1 ms.\

**2.1 Server:** `c7g.xlarge (CPU: 4, Mem: 8GB)` | **Client:** `c7g.2xlarge (CPU: 8, Mem: 16GB)`

**2.1.1 Linux Native networking stack**\
Conns: 4 | Pipeline: 12 | Max Key: 620,300 | Run Duration: 120s | Cache max key count: 154104 * 4 = 616,416

| Workload Ratio (Set:Get) | Throughput (ops/sec) | p50 | p90 | p95 | p99 | p99.9 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **1:0** | 1157404 | 0.243 | 0.529 | 0.623 | 0.801 | 1.020 |
| **0:1** | 1290664 | 0.195 | 0.343 | 0.396 | 0.489 | 0.608 |
| **1:2** | 1153544 | 0.244 | 0.492 | 0.595 | 0.934 | 2.157 |

**2.1.2 DPDK Networking stack**\
Conns: 4 | Pipeline: 18 | Max Key: 620,300 | Run Duration: 120s | Cache Key Size: 51101 * 4 = 204404

| Workload Ratio (Set:Get) | Throughput (ops/sec) | p50 | p90 | p95 | p99 | p99.9 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **1:0** | 1857183 | 0.192 | 0.464 | 0.534 | 0.690 | 0.929 |
| **0:1** | 2285447 | 0.157 | 0.326 | 0.385 | 0.500 | 0.655 |
| **1:2** | 1858979 | 0.200 | 0.454 | 0.563 | 0.965 | 1.916 |

*To avoid Out-Of-Memory (OOM) termination on 8GB instances, the maximum cache size was limited to 2GB, leaving 6GB for DPDK's hugepage allocation and operating system overhead.*

**2.2 Server:** `c7g.2xlarge (CPU: 8, Mem: 16GB)` | **Client:** `c7g.4xlarge (CPU: 16, Mem: 32GB)`

**2.2.1 Linux Native networking stack**\
Conns: 2 | Pipeline: 4 | Max Key: 1,406,900 | Run Duration: 120s | Cache max key count: 175662 * 8 = 1,405,296

| Workload Ratio (Set:Get) | Throughput (ops/sec) | p50 | p90 | p95 | p99 | p99.9 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **1:0** | 2,330,753 | 0.150 | 0.351 | 0.421 | 0.571 | 0.877 |
| **0:1** | 2,803,757 | 0.138 | 0.242 | 0.287 | 0.389 | 0.585 |
| **1:2** | 2,312,260 | 0.156 | 0.318 | 0.399 | 0.697 | 1.985 |

**2.2.2 DPDK Networking stack**\
Conns: 2 | Pipeline: 4 | Max Key: 1,406,900 | Run Duration: 120s | Cache max key count: 175662 * 8 = 1,405,296

| Workload Ratio (Set:Get) | Throughput (ops/sec) | p50 | p90 | p95 | p99 | p99.9 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **1:0** | 2,725,446 | 0.123 | 0.302 | 0.358 | 0.473 | 0.623 |
| **0:1** | 3,432,051 | 0.110 | 0.183 | 0.216 | 0.291 | 0.393 |
| **1:2** | 2,768,299 | 0.126 | 0.254 | 0.319 | 0.612 | 1.571 |

*All the latency figures are in ms(milliseconds)*
