# 1. Future Work & Roadmap
ShunyaKV’s current architecture establishes a high-performance, shared-nothing foundation optimized for single-node, multi-core scalability. The following roadmap outlines the next set of enhancements aimed at evolving the system into a fully distributed, intelligent, and production-grade data platform.

```mermaid
graph TD
    %% Core Engine
    subgraph Phase 1: Core Engine
        A[Shared-Nothing Architecture] --> B[Basic Command Support]
        B --> C[Eviction Algorithm]
        C --> D[Memory Allocation Optimization]
    end

    %% Command Support
    subgraph Phase 2: Advanced Command Support
        D --> E["Collection Data Types: HashMap, HashSet, List"]
        E --> F["Semantic Cache / Semantic Search"]
        F --> G["Negative Caching & Backoff Markers"]
    end

    subgraph Phase 3: Security
        G --> H[Authentication]
        H --> I[Access Control List]
    end

    subgraph Phase 4: Scalability & Reliability
        I --> J[Database / Cluster Sharding]
        J --> K[Replication]
        K --> L[Consistency Model]
    end

    subgraph Phase 5: Advanced Engine Configuration
        L --> M["Routing-table-based CPU Shard Routing"]
        M --> N["Hot cpu shard mitigation"]
    end

    subgraph Phase 6: Telemetry
        N --> O["Metrics, Tracing, Profiling & Observability"]
    end



    %% Styling to make it look sharp in dark mode
    style A fill:#2d3748,stroke:#4a5568,stroke-width:2px,color:#ffffff
    style C fill:#2d3748,stroke:#4a5568,stroke-width:2px,color:#ffffff
    style E fill:#2d3748,stroke:#4a5568,stroke-width:2px,color:#ffffff
    style G fill:#2d3748,stroke:#4a5568,stroke-width:2px,color:#ffffff
    style I fill:#2d3748,stroke:#4a5568,stroke-width:2px,color:#ffffff
    style K fill:#2d3748,stroke:#4a5568,stroke-width:2px,color:#ffffff
    style M fill:#2d3748,stroke:#4a5568,stroke-width:2px,color:#ffffff
    style O fill:#2d3748,stroke:#4a5568,stroke-width:2px,color:#ffffff
