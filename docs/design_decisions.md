This document captures the design decisions made during the development of ShunyaKV, along with the reasoning behind them. Maintaining this document is important because it provides a reference for the thought process behind each design choice and helps explain why the system is structured the way it is today.

#### Shared Nothing Architecture

Lock contention is a critical bottleneck in modern software systems. As processors scale to tens or hundreds of cores, algorithms and data structures can end up spending a significant amount of time contending for locks instead of doing useful work. This remains one of the key engineering challenges for latency-sensitive and high-throughput systems.

Lock contention also affects tail-latency predictability. When a request tries to acquire a contended lock, it is difficult to know beforehand how long it will wait. Under higher load, this waiting time can become worse because more threads are competing for the same shared resource. As a result, even if average latency looks acceptable, p99 or p99.9 latency can degrade sharply.

Adding more cores or increasing the number of threads does not automatically improve system performance. Amdahl’s Law states that the speedup of a system is limited by the portion of work that cannot be parallelized. In the context of an in-memory cache, some common sources of serialization or contention are:

1. Locks around shared data structures
2. Shared memory allocators
3. Cache-line bouncing
4. Global eviction structures
5. Shared metrics counters

No matter how much parallel execution a system uses, these shared or contended components can become bottlenecks. One effective way to reduce this bottleneck is to use a shared-nothing architecture.

In a shared-nothing architecture, the system is divided into independent shards. Each shard owns a portion of the keyspace and maintains its own local data structures. In ShunyaKV, this means each core can run its own request-handling path, operate on its own in-memory store, maintain its own eviction state, and collect its own local metrics.

Because each shard owns its data, the hot path does not require multiple cores to acquire locks around the same key-value structures. This reduces lock contention, improves cache locality, and reduces avoidable cache-line bouncing. Instead of many threads competing over shared state, each shard progresses independently and processes requests for the keys it owns.

This design does not remove all bottlenecks. Network I/O, request parsing, memory allocation, hot-key skew, cross-shard forwarding, and metrics aggregation can still limit performance. However, shared-nothing architecture reduces hidden serialization in the main request path. In terms of Amdahl’s Law, it tries to increase the parallelizable portion of the system by minimizing the amount of work that must pass through shared or contended components.

#### Single-Port Listener with Shard-Aware Sticky Connections

When I started development, I used a per-shard port model. If the machine had 8 cores, the cache would expose 8 ports, such as `60111`, `60112`, ..., `60118`. Each shard would listen on its own port. The server would announce which port was responsible for which key range, and the client could use the key hash to identify the correct shard and send the request directly to that shard’s port.

This model was simple from a server-side routing perspective because requests could directly enter the owning shard. However, it created operational and client-side complexity. On a 128-core machine, the cache would need 128 ports. Finding and managing such a large sequential block of available ports may not always be practical. If a continuous range of ports is not available, the system would need to use non-contiguous ports, and the client would need to maintain an explicit shard-to-port map.

These problems were manageable as bookkeeping issues, but the model became more problematic when considering DPDK networking mode.

In DPDK mode, packet distribution is usually driven by RSS flow hashing. When packets arrive at the NIC, the hardware hashes flow fields such as source IP, source port, destination IP, and destination port, depending on the RSS configuration. The resulting hash determines which receive queue receives the packet. Each receive queue is then typically polled by a specific CPU core or Seastar shard.

This means that even if the application exposes multiple ports, the NIC still decides which RX queue receives a flow based on its hardware-level RSS configuration. Some NICs provide advanced steering capabilities, but relying on custom hardware routing would make the design more dependent on specific NIC features, firmware capabilities, and cloud-provider restrictions.

Because of this, I moved to a single-port listener model.

In the single-port model, the cache exposes one port for client connections. When a client connects, the connection is assigned to a CPU shard by the networking stack or, in DPDK mode, by the NIC’s RSS flow hashing. Once the connection is established, it remains associated with that shard.

The client then sends a `NODE_INFO` command to learn which shard the connection is attached to, along with the total shard count and key ownership information. The client keeps opening connections to the same server port until it has obtained connections to all shards. Once it has a connection for each shard, it can hash the key, identify the owning shard, and pick the connection associated with that shard.

This gives the system a single operational port while still allowing the client to preserve shard locality.

To handle the case where a client is unable to obtain connections to all shards, ShunyaKV also supports cross-shard request forwarding. If a request reaches a non-owning shard, that shard can forward the request to the correct owner shard, wait for execution, and return the response to the client.

Cross-shard forwarding adds latency because it introduces additional scheduling and cross-core communication. However, it keeps the system functional even when the client does not have a perfect shard-to-connection mapping. In the ideal case, the client maintains shard-aware sticky connections and sends requests directly to the owning shard. In the fallback case, cross-shard forwarding preserves correctness at the cost of additional latency.

#### Memory pool for fixed-size entries

In the initial implementation, the cache performed reasonably well for simple `SET` and `GET` workloads. However, performance dropped noticeably when I forced a large number of updates on existing keys where the key or value sizes varied significantly. These updates caused repeated memory allocation, deallocation, and in some cases reallocation. As the workload became more write-heavy and update-heavy, allocator overhead became more visible in the request path.

I experimented with different general-purpose allocators, including `jemalloc`, but did not see enough improvement for this workload. The next obvious direction was to try a custom slab allocator that would preallocate memory and reuse it, avoiding the repeated cost of allocating and freeing memory during request processing.

I tried this approach, but the initial custom slab allocator made performance worse instead of improving it. The added complexity around variable-sized allocation, reuse, fragmentation, and bookkeeping created its own overhead. More importantly, it made memory behavior harder to reason about in a system that was already designed around shard-local ownership.

Instead of making the allocator more complex, I moved to a simpler object-pool design for fixed-size cache entries.

ShunyaKV now allows a configured slab size, which is used to calculate how many entry objects can be created within the allowed memory budget. At startup, each shard pre-creates a fixed number of entry objects and stores them in a shard-local object pool. When a new key-value entry is inserted, the shard takes an object from its local pool. When an entry is deleted or evicted, the object is returned to the same pool for reuse.

This design has a few advantages:

1. It reduces allocator calls on the hot path.
2. It makes per-shard memory ownership easier to reason about.
3. It gives the cache a predictable upper bound on the number of live entries.
4. It reduces memory fragmentation caused by repeated allocation and deallocation.
5. It fits naturally with the shared-nothing architecture because each shard manages its own pool.

The trade-off is that fixed-size entries can waste memory when stored values are much smaller than the configured entry size. It also means the maximum supported value size is constrained by the configured slab/object size unless the system adds a separate large-object path later.

To avoid exhausting memory needed by other parts of the system, ShunyaKV uses only a configured portion of the available memory for the object pool. In the current implementation, the pool is capped at 80% of the configured memory budget, leaving headroom for networking, request parsing, metadata, metrics, and other runtime overhead.

The important lesson from this design decision was that a custom allocator is not automatically faster just because it avoids general-purpose allocation. For this workload, a simpler fixed-size object pool gave more predictable memory behavior and aligned better with the shared-nothing design.
