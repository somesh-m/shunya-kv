# 1. Future Work & Roadmap
ShunyaKV’s current architecture establishes a high-performance, shared-nothing foundation optimized for single-node, multi-core scalability. The following roadmap outlines the next set of enhancements aimed at evolving the system into a fully distributed, intelligent, and production-grade data platform.

# 1. Future Work & Roadmap

ShunyaKV’s current architecture establishes a high-performance, shared-nothing foundation optimized for single-node, multi-core scalability. The following roadmap outlines the next set of enhancements aimed at evolving the system into a fully distributed, intelligent, and production-grade data platform.

We have divided the roadmap for ShunyaKV into multiple phases, and till now I have been able to finalize it till Phase 6. I will update the roadmap and add more items as and when they are discovered or deemed necessary.

## Phase 1: Core Engine

This phase includes developing and stabilizing the core engine. This includes the shared-nothing architecture, basic command support, eviction logic, and memory allocation improvements. The core development is done and we are already proceeding with Phase 2. In parallel, I am also planning a vulnerability scanning setup so that I can secure the cache from risky code or configurations.

## Phase 2: Advanced Command Support

I plan to implement additional commands and capabilities to make the cache more suitable for broader workloads.

1. **Collection Data Types:** Support for HashMap, HashSet, and List.
2. **Semantic Cache:** Support for semantic search integration, with the ability to integrate with third-party providers like OpenAI, AWS Bedrock, and Azure OpenAI for vectorization. This will be a plug-and-play model, and users will have to use their own API keys.
3. **Negative Caching & Backoff Markers:** Negative caching can be helpful in reducing traffic to the main DB when a key is not present in the DB. Negative entries can be created in the cache, which would return a special response so that the client knows the key is not available in the DB. Backoff markers will return a response to the client with a retry-after field.

## Phase 3: Security

As of today, ShunyaKV does not support authentication or authorization. We will be adding support for authentication and ACLs so that the DB can be configured securely.

## Phase 4: Telemetry

A system is as good as its monitoring. In this phase, we plan to implement strong profiling and observability support to enable users to monitor their DB performance. Instead of using a third-party sidecar agent to monitor the system, I have decided to integrate these monitoring capabilities directly into the cache engine. Further details on this architecture will be released in the coming days.

## Phase 5: Scalability & Reliability

Scalability and reliability are among the key features of any modern system. We are planning to implement database sharding and replication to enable ShunyaKV to scale and provide data redundancy. Further details on the architecture related to these features will be added in due course of time. I plan to implement a leader-follower replication strategy.

## Phase 6: Advanced Engine Configuration

In this phase, I will add advanced features to the core engine. One of them is routing-table-based CPU shard routing. Currently, CPU shard routing is done on the basis of the key hash, but we also want to support routing-table-based configuration in case the user wants to use it. This kind of configuration can be useful for supporting skewed loads and can also enable future support for hot CPU shard mitigation.


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

    subgraph Phase 4: Telemetry
        I --> J["Metrics, Tracing, Profiling & Observability"]
    end

    subgraph Phase 5: Scalability & Reliability
        J --> K[Database / Cluster Sharding]
        K --> L[Replication]
        L --> M[Consistency Model]
    end

    subgraph Phase 6: Advanced Engine Configuration
        M --> N["Routing-table-based CPU Shard Routing"]
        N --> O["Hot cpu shard mitigation"]
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
