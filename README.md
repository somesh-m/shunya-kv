# ShunyaKV

**ShunyaKV** is a high-performance, shared-nothing Key-Value store designed to eliminate the "locking tax" in multi-core systems. By leveraging **DPDK** for kernel-bypass networking and a **shard-per-core** architecture, it delivers predictable sub-millisecond tail latency and near-linear vertical scalability.

## 🚀 Performance at a Glance

ShunyaKV is built for extreme efficiency. As CPU resources increase, throughput climbs while tail latency remains stable—or even improves—due to reduced contention.

![ShunyaKV Scalability](images/scalability_dual_axis.png)
*Figure: Doubling cores from 4 to 8 yields a ~1.8x throughput increase and reduces p99.9 latency by ~50%.*

### Key Metrics (8-Core DPDK)
*   **Max Throughput:** 4,514,472 ops/sec (GET workload)
*   **Tail Latency:** 0.992ms (p99.9)
*   **Efficiency:** ~30% faster than standard POSIX networking stacks.

---
👉 **[View the Full Performance Deep-Dive (perf.md)](./perf.md)**
---

## 🛠 Core Architecture

-   **Shared-Nothing Design:** Each CPU core manages its own memory shard, eliminating the need for mutexes or spinlocks.
-   **Kernel Bypass (DPDK):** Moves packet processing to user space, significantly reducing context-switching overhead and interrupt storms.
-   **Deterministic Partitioning:** Uses the **FNV-1a** hashing algorithm to ensure uniform data distribution across shards with zero cross-core communication.

## 📂 Project Structure

*   `src/`: Core source code for the KV engine and DPDK transport.
*   `benchmarks/`: Scripts and tools used for load generation.
*   `docs/`: Detailed architectural documentation.
*   `perf.md`: Comprehensive benchmark results and analysis.

## 🚀 Getting Started

### Prerequisites
*   Ubuntu 20.04+
*   DPDK 21.11+
*   NICs supported by DPDK (e.g., Intel 800 series or Mellanox ConnectX)

### Installation
```bash
git clone [https://github.com/yourusername/ShunyaKV.git](https://github.com/yourusername/ShunyaKV.git)
cd ShunyaKV
make build
