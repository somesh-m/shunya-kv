
# ShunyaKV In-Memory Benchmark Report

## Overview
This report presents **latency and throughput benchmarks** for **ShunyaKV**, an in-memory key-value store built on an event-driven, shard-per-core architecture.

The benchmarks evaluate:
- Service latency under low offered load
- Scaling behavior as concurrency increases
- Queueing effects at high throughput

All measurements were taken with the database operating entirely in memory, without any disk I/O.

## System Configuration

### Server
- Platform: AWS EC2
- CPU: 8 vCPUs (x86_64, virtualized)
- Memory: 16 GB RAM
- Storage: Not used (fully in-memory workload)
- OS: Linux (modern kernel)
- Database mode: In-memory only (no WAL, no fsync)

### Client
- Implementation: Node.js
- Concurrency controls:
  - conns – number of TCP connections
  - pipeline – inflight requests per connection
- Execution model: Single-threaded event loop with asynchronous I/O

### Network
- Client and server deployed on separate machines
- Standard AWS VPC networking
- End-to-end latency includes network overhead

## Dataset & Workload
- Keyspace size: 200,000 keys
- Key distribution: Uniform random
- Value size: Small, fixed
- Workload type: GET & SET
- Benchmark style: Fixed concurrency (inflight-controlled)

## Benchmark Results

| Scenario | vCPUs | Conns | Pipeline | Inflight | Ops/sec | p50 (ms) | p95 (ms) | p99 (ms) | p99.9 (ms) |
|--------|------:|------:|---------:|---------:|--------:|---------:|---------:|----------:|-----------:|
| High inflight (saturation) | 8 | 15 | 20 | 300 | 66079 | 1.72 | 2.04 | 3.31 | 3.86 |
| Low inflight baseline | 4 | 4 | 1 | 4 | 17735 | 0.17 | 0.26 | 0.34 | 0.46 |
| All vCPUs, low inflight | 8 | 4 | 1 | 4 | 16683 | 0.19 | 0.27 | 0.35 | 0.48 |
| All vCPUs, moderate inflight | 8 | 8 | 1 | 8 | 23024 | 0.24 | 0.37 | 0.45 | 0.80 |

## Conclusion
ShunyaKV demonstrates low and stable service latency, predictable throughput scaling, and well-behaved queueing under saturation, making it suitable for high-performance in-memory workloads.
