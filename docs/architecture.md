# 1. Vision: The Zero-Contention Model
ShunyaKV is a high-performance distributed in-memory data grid designed for the modern multi-core era. While traditional KV stores rely on shared-memory concurrency, ShunyaKV is built on a Shared-Nothing Architecture leveraging the Seastar framework.

## The Problem: The "Locking Tax"
In standard multithreaded systems, performance scales sub-linearly with core count. This is due to:
* **Coarse-Grained Locking:** Locking entire memory segments for granular access causes massive thread stalling.
* **Kernel Overhead:** The frequent context switching and system calls (futexes) required for lock acquisition dominate the CPU cycles.
* **Unbounded Wait:** Thread synchronization leads to unpredictable p99.9 latencies, making the system unsuitable for real-time high-performance needs.

## The Solution: Shared-Nothing
ShunyaKV eliminates shared-memory synchronization and minimizes cross-core coordination by partitioning the keyspace across CPU cores. Each core (shard) owns a specific subset of data and a dedicated memory pool. Communication between shards happens via explicit, non-blocking message passing, ensuring that a thread never blocks on a system call or another thread's activity.

# 2. Architecture

## 2.1. Sharding & Network Architecture
ShunyaKV operates as a distributed, shard-local service where each CPU core manages its own isolated environment.

### 2.1.1. Deterministic Data Partitioning
To ensure zero-contention, data is partitioned across shards using a 64-bit FNV-1a hashing algorithm. FNV-1a is a high-performance, non-cryptographic hash chosen for its sub-nanosecond execution time and excellent distribution characteristics. This ensures that keys are spread uniformly across the keyspace, effectively eliminating "hot spots" and maximizing the utilization of every core.

### 2.1.2. Shard-Local Memory Isolation
The Seastar framework enforces a strict memory-per-core policy. Each shard possesses exclusive ownership of its memory segment; cross-shard memory access is physically impossible by design. This isolation allows ShunyaKV to process requests without the performance penalties of atomic operations or memory barriers.

### 2.1.3. Connection Load Balancing
When a client establishes a connection, the global accept loop distributes the session across all available CPU shards using a weighted round-robin strategy.

While it may seem counter-intuitive to decouple the connection shard from the data shard, this approach offers several architectural advantages over a "multi-port" or "steered" connection model. To understand why a single-port model was chosen for ShunyaKV, we must examine the limitations of the "multi-port" approach in a DPDK context.

#### The POSIX Model: Software-Defined Steering
In a traditional POSIX networking stack, the Linux Kernel acts as a sophisticated traffic controller. It maintains a 5-tuple mapping (Source IP/Port, Destination IP/Port, and Protocol) to manage flow. When a packet arrives, the Kernel performs a table lookup to identify exactly which process or thread owns that specific destination IP/Port combination and routes the packet accordingly. This "steering" happens in software, making multi-port management seamless but CPU-intensive.

#### The DPDK Reality: Hardware-Defined Steering
In DPDK mode, ShunyaKV takes direct, exclusive control of the Network Interface Card (NIC), bypassing the Kernel entirely. Without the Kernel's mapping layer, the responsibility for packet distribution shifts to the hardware:
* **RSS (Receive Side Scaling):** The NIC uses Receive Side Scaling to distribute incoming packets across multiple hardware queues (shards).
* **Hardware Hashing:** The NIC places packets into these queues based on its own internal hardware hashing—not a software-defined table.
* **Direct Polling:** ShunyaKV shards poll these hardware RSS queues directly to ingest packets at line rate.

By moving to a single-port model, ShunyaKV embraces the way modern NICs naturally distribute traffic. Rather than fighting the hardware to force specific ports onto specific cores—which is often fragile and dependent on specific NIC capabilities—ShunyaKV allows the hardware to distribute traffic naturally.

#### The Routing Challenge: The "Hop" Penalty
In a single-port DPDK architecture, the NIC distributes incoming packets across CPU shards using hardware-level hashing. Mathematically, this creates a significant challenge for a shared-nothing system. Since a key is owned by a specific shard, the probability $P$ of a packet landing on the "correct" owner shard decreases as the system scales:

$P(\text{Direct Hit}) = \frac{1}{n}$
$P(\text{Hop}) = \frac{n-1}{n}$

| CPU shard count ($n$) | $P$ (not landing on owner shard) | % Reroute |
| :--- | :--- | :--- |
| 4 | 0.75 | 75% |
| 8 | 0.87 | 87% |
| 12 | 0.91 | 91% |
| 22 | 0.95 | 95% |
| 48 | 0.97 | 97% |

At 48 shards, ~98% of requests would require an internal `submit_to()` (SMP hop). While Seastar’s inter-shard communication is optimized, at the scale of 4.5M QPS, this massive volume of cross-core forwarding would lead to cache locality degradation, increased latency variance, and inter-core bus saturation.

#### The Solution: Shard-Aware "Smart" Clients
To mitigate this, ShunyaKV moves the "routing intelligence" to the client-side. Instead of treating the cache as a black box, the client participates in the sharding logic to ensure zero-hop execution.

* **Discovery via NODE_INFO:** Upon establishing a connection, the client issues a `NODE_INFO` command. This allows the client to identify exactly which CPU shard that specific connection has landed on.
* **Connection-to-Shard Mapping:** The client maintains a pool of connections and continues establishing new sessions until it has secured at least one direct path to every shard. By storing this mapping, the client can use the same 64-bit FNV-1a algorithm as the server to predict the owner shard for any given key.
* **Optimized Request Routing:** When a request is generated, the client:
    1. Calculates the Owner Shard using the key's hash.
    2. Selects the pre-established Connection that belongs to that shard.
    3. Transmits the request, ensuring it lands directly on the shard that owns the data.
* **The Fallback Safety Net:** ShunyaKV remains resilient. If a client is unable to secure an exclusive connection to a specific shard (or is not "Smart-Aware"), the server will still process the request by internally routing it to the correct owner. This ensures that while "Smart" clients achieve maximum performance, legacy or simple clients still maintain full compatibility.

## 2.2. Storage & Memory Efficiency

### 2.2.1. High-Performance Indexing (Abseil)
Instead of standard library containers, ShunyaKV utilizes Abseil’s B-tree and Flat Hash maps for its primary index. Abseil’s maps are cache-aware and significantly reduce the memory footprint per key, allowing ShunyaKV to store more data in the same physical RAM.

### 2.2.2. Lazy TTL Eviction
ShunyaKV employs a Lazy Eviction strategy for expired keys. Instead of dedicated background threads constantly scanning the keyspace—which can steal CPU cycles from the hot path and cause jitter—expiration is handled at the time of access.
* **The Strategy:** When a GET request is received, ShunyaKV checks the timestamp. If the key has expired, it is immediately purged, and a null is returned.
* **The Benefit:** This ensures that the Sequential Reaper and the Sieve Logic can focus entirely on reclaiming memory based on access patterns rather than timers, preserving the "Shared-Nothing" performance for active requests.

## 2.3. Data Admission & Eviction
To maintain high hit ratios under intense traffic, ShunyaKV utilizes Segmented Admission Control. This mechanism prevents "cache pollution," where a sudden burst of new or rarely accessed keys (the long tail) evicts high-value "hot" keys. This architecture is specifically optimized for real-world Zipfian traffic patterns, where a small percentage of data accounts for the vast majority of accesses.

The shard-local memory is divided into two distinct logical pools:

### 2.3.1. The Probationary Pool (Admission Filter)
All new incoming keys are initially placed in the Probationary Pool.
* **The Reaper Eviction:** To handle high-velocity ingestion, this pool employs a "Reaper" strategy—a high-speed, FIFO-based eviction that blindly reclaims space to accommodate new keys.
* **The Goal:** This acts as a high-throughput buffer that quickly discards "one-hit wonders" before they can impact the main cache. In internal stress tests with a keyspace 10x the size of physical RAM, this strategy maintained zero out-of-pool allocation errors over hour-long runs.

### 2.3.2. The Sanctuary Pool (Protected Core)
When a key in the Probationary Pool is accessed, it is promoted to the Sanctuary Pool.
* **The Sieve Algorithm:** Unlike traditional LRU (Least Recently Used) algorithms, which require global metadata updates and cache-line contention on every "hit," the Sanctuary Pool uses a Sieve eviction algorithm.
* **The Benefit:** Sieve enables a lock-free read path by eliminating the need to re-order elements on every access. In high-concurrency environments, this can result in read paths that are up to 2× faster than traditional LRU-based systems while maintaining comparable (or superior) hit ratios.

ShunyaKV utilizes intrusive data structures, where the pointers required for the Sieve and Reaper eviction lists are embedded directly within the data entries themselves.
* **Cache Locality:** This eliminates the need for external wrapper nodes, drastically reducing memory fragmentation and ensuring that eviction metadata stays in the same CPU cache line as the entry itself.
* **O(1) Pool Promotion:** Intrusive hooks allow for near-zero-cost promotion from the Probationary Pool to the Sanctuary Pool. Because the object "carries" its own links, moving it between pools is a simple pointer reassignment. There is no need to copy data, re-allocate memory, or update external trackers, ensuring that promotions happen in constant time without memory allocator overhead.

# 3. Performance & Benchmarks
To validate the architectural efficiency of ShunyaKV, comprehensive benchmarking was conducted on AWS EC2 c7g (Graviton3) instances. The tests focused on the performance delta between the Linux Native stack and the DPDK Kernel-Bypass stack.

## 3.1. Peak Performance Metrics
In high-concurrency scenarios, ShunyaKV demonstrates the raw throughput capabilities of a shared-nothing design. The below test results are for 100% reads.

### 4 Core Server | 8 Core Client
| Metric | POSIX Network Stack | DPDK Stack | Delta |
| :--- | :--- | :--- | :--- |
| Max GET Throughput | 1.29 M QPS | 2.28 M QPS | +76.7% |
| p99.9 Latency (GET) | 0.61 ms | 0.65 ms | Consistent |
| Max SET Throughput | 1.16 M QPS | 1.86 M QPs | +60.3% |

### 8 Core Server | 16 Core Client
| Metric | POSIX Network Stack | DPDK Stack | Delta |
| :--- | :--- | :--- | :--- |
| Max GET Throughput | 2.80 M QPS | 3.43 M QPS | +22.5% |
| p99.9 Latency (GET) | 0.58 ms | 0.39 ms | -32.7% |
| Max SET Throughput | 2.33 M QPS | 2.72 M QPS | +16.7% |

The benchmarking results across both 4-core and 8-core configurations provide empirical validation for the core architectural hypotheses of ShunyaKV. By doubling server resources, the system achieved near-linear scalability—highlighted by a 117% increase in POSIX throughput and a 50% increase in DPDK throughput—confirming that the Shared-Nothing model successfully bypasses the "locking tax" inherent in traditional multi-threaded designs. While the standard Linux stack experiences increased jitter as it nears saturation, the DPDK Kernel-Bypass implementation demonstrates superior efficiency at higher core counts, increasing throughput by 22.5% while simultaneously slashing tail latency (p99.9) by nearly 33% on the 8-core baseline.

For a detailed breakdown of testing methodologies, mixed-workload ratios (1:2), and saturation benchmarks, please refer to our comprehensive `perf.md` file in the repository.

# 4. Future Work & Roadmap
ShunyaKV’s current architecture establishes a high-performance, shared-nothing foundation optimized for single-node, multi-core scalability. The following roadmap outlines the next set of enhancements aimed at evolving the system into a fully distributed, intelligent, and production-grade data platform.

For detailed proposals, design trade-offs, and implementation plans, refer to `future.md`.
"""
