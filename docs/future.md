# 1. Future Work & Roadmap
ShunyaKV’s current architecture establishes a high-performance, shared-nothing foundation optimized for single-node, multi-core scalability. The following roadmap outlines the next set of enhancements aimed at evolving the system into a fully distributed, intelligent, and production-grade data platform.

## 1.1. Distributed Replication & Fault Tolerance
The current design focuses on intra-node scalability. The next phase introduces inter-node distribution.

### Replication Models (Under Exploration):
* **Primary–Replica (Leader-Based):**
    * Single shard leader per partition
    * Asynchronous replication for low latency
* **Leaderless (Quorum-Based):**
    * Inspired by Dynamo-style systems
    * Tunable consistency (R/W quorum)

### Key Challenges:
* Maintaining shard affinity across nodes
* Minimizing cross-node hops (similar to intra-node `submit_to`)
* Handling partial failures without impacting tail latency

### Planned Direction:
* Extend `NODE_INFO` to include cluster topology + shard ownership
* Introduce replication-aware client routing

## 1.2. Consistency Models & Write Semantics
ShunyaKV currently operates as a high-performance cache with implicit eventual consistency. Future enhancements will include:

### Configurable consistency levels:
* **LOCAL** (single shard, fastest)
* **QUORUM**
* **ALL**

### Write durability modes:
* **Fire-and-forget** (current)
* **Acknowledged writes**
* **Replicated writes**

**Goal:** Allow applications to explicitly trade off latency vs durability vs consistency.

## 1.3. Adaptive Client-Side Routing (Intelligent SDK)
The current shard-aware client uses static mapping.

### Future evolution:
* **Adaptive routing based on runtime signals:**
    * Shard load (CPU, queue depth)
    * Latency trends
* **Connection rebalancing**
* **Hot shard detection + mitigation**

### Advanced Direction:
* Incorporate lightweight heuristics (e.g., multi-armed bandit style routing)
* **Dynamic decision:** direct hit vs alternative shard vs retry

## 1.4. Advanced Data Structures & Query Primitives
Beyond simple KV operations, ShunyaKV will evolve into a richer data platform.

### Planned primitives:
* **Adaptive Radix Tree (ART):** Fast prefix queries; Sharded by first K characters (K=2)
* **Range Indexes:** Efficient range scans
* **Interval Trees:** Time/window-based queries
* **Prefix Routing Table (Longest Prefix Match):** Networking and routing use-cases

**Goal:** Enable low-latency queryable state, not just key lookup.

## 1.5. Intelligent Caching Features
To improve real-world workload efficiency:
* **Dependency Graph for Invalidation:** Fine-grained cache invalidation across keys
* **Negative Caching (NEG markers):** Avoid repeated misses
* **Backoff Markers (BACKOFF):** Cache-driven request throttling
* **Per-Key Eviction Policies:** Different strategies for hot vs cold data

## 1.6. Memory Management Enhancements
Building on current object pool design:
* **Fixed-size Slab Allocator:** Page-based allocation (e.g., 64KB pages) to reduce fragmentation.
* **Allocation-aware eviction:** Evict based on memory pressure patterns.
* **NUMA-aware allocation (future):** For multi-socket systems.

**Goal:** Sustain predictable latency under allocator-heavy workloads.

## 1.7. Observability & Telemetry
ShunyaKV will expose deep runtime introspection:
* **Extended INFO command:** Per-shard QPS, latency, forwarding ratio, and memory pool stats.
* **Periodic metrics streaming:** Integration with benchmark orchestrator.

### Future Direction:
* **Real-time visualization:** CPU usage per shard, hit/miss ratios, and eviction patterns.

## 1.8. Failure Injection & Benchmarking Framework
To ensure production readiness:
* **Built-in failure simulation:** Shard crash, replica lag, network partition.
* **Workload simulation:** Zipfian distribution, allocator churn, mixed read/write.

**Goal:** Validate correctness and performance under real-world stress.

## 1.9. Security
* Authentication layer (device/user level)
* Namespace isolation
