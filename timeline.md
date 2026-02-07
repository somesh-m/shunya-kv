# ShunyaKV Roadmap Timeline

> **Today:** 2026-02-07 (Asia/Kolkata)

This document captures the planned project phases and a timeline view. Dates are approximate and meant to guide execution and reporting.

---

## Phase Summary

- **Phase 1 — Public-ready single-node core**
  - Enable **DPDK** (posix ↔ dpdk mode)
  - Add **TTL** command set
  - Improve **robustness** (errors, edge-cases, safe parsing)
  - Support **config file + CLI overrides**
  - Repo hygiene: docs, scripts, reproducible benchmarks

- **Phase 2 — ML/Heuristic algorithms (adaptive performance)**
  - Metrics plumbing (per-shard inflight/queue depth/latency percentiles)
  - 2–3 heuristics (e.g., adaptive admission control, hot-key detection, dynamic batching)
  - A/B benchmark report for before/after

- **Phase 3 — Replication + Sharding (distributed system)**
  - Replication: primary–replica (async first), basic failure handling
  - Sharding: partitioned keyspace (client-side routing first), cluster topology metadata
  - Commands expansion becomes **secondary** and can move to later phases

---

## Timeline (Gantt)

```mermaid
gantt
    title ShunyaKV Roadmap (Tentative)
    dateFormat  YYYY-MM-DD
    axisFormat  %b %d

    section Phase 1 — Public-ready single-node core
    DPDK enablement (posix/dpdk)          :p1a, 2026-02-07, 6d
    TTL commands (EXPIRE/TTL/PERSIST)     :p1b, after p1a, 3d
    Robustness + error handling           :p1c, 2026-02-10, 7d
    Config file + CLI overrides           :p1d, 2026-02-12, 6d
    Docs + scripts + benchmark hygiene    :p1e, 2026-02-14, 7d
    Phase 1 release (tag + public repo)   :milestone, p1m, 2026-02-21, 1d

    section Phase 2 — ML/Heuristics (aim: ~90% by Feb end)
    Metrics plumbing + telemetry          :p2a, 2026-02-18, 6d
    Heuristic #1 (admission/backpressure) :p2b, after p2a, 4d
    Heuristic #2 (hot-key detection)      :p2c, after p2b, 4d
    Heuristic #3 (dynamic batching)       :p2d, 2026-02-26, 4d
    Phase 2 checkpoint (~90%)             :milestone, p2m, 2026-02-28, 1d

    section Phase 3 — Replication + Sharding
    Replication v1 (primary→replica async):p3a, 2026-03-01, 14d
    Sharding v1 (client-side routing)     :p3b, 2026-03-08, 14d
    Cluster metadata + node discovery     :p3c, 2026-03-15, 10d
    Failure handling (basic)              :p3d, 2026-03-22, 10d
    Phase 3 release (cluster alpha)       :milestone, p3m, 2026-04-01, 1d
```

---

## Execution Flow (Phase progression)

```mermaid
flowchart TD
  A[Phase 1: Public-ready single-node core] --> B[Phase 2: ML/Heuristics layer]
  B --> C[Phase 3: Replication + Sharding]
  C --> D[Later Phases: More commands & data structures]

  subgraph P1[Phase 1 Deliverables]
    P1a(DPDK mode: posix ↔ dpdk)
    P1b(TTL commands)
    P1c(Robustness: errors/edge cases)
    P1d(Config file + CLI overrides)
    P1e(Docs + scripts + reproducible benchmarks)
  end

  subgraph P2[Phase 2 Deliverables]
    P2a(Metrics/telemetry plumbing)
    P2b(Adaptive admission/backpressure)
    P2c(Hot-key detection)
    P2d(Dynamic batching / scheduling)
    P2e(A/B benchmarks + report)
  end

  subgraph P3[Phase 3 Deliverables]
    P3a(Replication v1: async primary→replica)
    P3b(Sharding v1: partitioned keyspace)
    P3c(Cluster metadata + discovery)
    P3d(Basic failure handling)
  end

  A --> P1
  B --> P2
  C --> P3
```

---

## Notes
- **Dates are estimates.** The milestone targets can be moved without changing the phase boundaries.
- The key intention is: **Phase 1 = publishable single-node**, **Phase 2 = adaptive performance**, **Phase 3 = distributed (replication + sharding)**.
