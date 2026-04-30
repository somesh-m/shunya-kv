# ShunyaKV

> **Current Version:** `v0.1.1`
> **Status:** Beta — under active development

**ShunyaKV** is a high-performance, shared-nothing Key-Value store designed to eliminate the "locking tax" in multi-core systems. By leveraging **DPDK** for kernel-bypass networking and a **shard-per-core** architecture, it delivers predictable sub-millisecond tail latency and near-linear vertical scalability.

ShunyaKV is currently in **beta** and is under heavy development. APIs, configuration options, benchmarking behavior, and internal architecture may evolve as the project matures. The current release, **v0.1.1**, focuses on validating the core architecture, performance characteristics, Docker-based distribution, and benchmarking workflow.

## 🚀 Performance at a Glance

ShunyaKV is built for extreme efficiency. As CPU resources increase, throughput climbs while tail latency remains stable—or even improves—due to reduced contention.

![ShunyaKV Scalability](/assets/scalability.png)
\
*Figure: Doubling cores from 4 to 8 yields a ~1.8x throughput increase and reduces p99.9 latency by ~50%.*

### Key Metrics (8-Core DPDK)

- **Max Throughput:** 4,514,472 ops/sec (GET workload)
- **Tail Latency:** 0.992ms (p99.9)
- **Efficiency:** ~30% faster than standard POSIX networking stacks.

---

👉 **[View the Full Performance Deep-Dive (perf.md)](./perf.md)**

---

## 🛠 Core Architecture

- **Shared-Nothing Design:** Each CPU core manages its own memory shard, eliminating the need for mutexes or spinlocks.
- **Kernel Bypass (DPDK):** Moves packet processing to user space, significantly reducing context-switching overhead and interrupt storms.
- **Deterministic Partitioning:** Uses the **FNV-1a** hashing algorithm to ensure uniform data distribution across shards with zero cross-core communication.

## 🚀 Installation

You can build **ShunyaKV** directly from source using CMake. The following steps will compile the project and generate the binaries:

```bash
mkdir build && cd build
cmake ..
make -j
```

All build artifacts will be created inside the `build/` directory.

---

## 🧪 Running Tests

ShunyaKV uses **CTest** for unit testing. After building the project, run:

```bash
ctest
```

This validates core components such as request parsing, command handling, and internal data structures.

---

## 🐳 Docker Support

ShunyaKV provides Docker support for easy setup and multi-architecture builds.

### Build Locally

```bash
# ARM64
docker build -f Dockerfile.aarch64 -t shunyakv:arm64 .

# x86_64
docker build -f Dockerfile -t shunyakv:x86_64 .
```

### Use Prebuilt Image

To get started quickly without building locally:

```bash
docker pull shunyalabs/shunyakv:latest
```

For the current beta release:

```bash
docker pull shunyalabs/shunyakv:0.1.1
```

---

## ▶️ Starting ShunyaKV

### POSIX Network Stack

```bash
sudo docker run --rm -it --privileged \
  -p 60111:60111 \
  -v "$PWD/config.conf:/config.conf:ro" \
  shunyalabs/shunyakv:latest
```

**Optional flags:**

- `--smp <num_cores>`: Number of CPU cores to use
- `--memory <size>`: Memory allocation for ShunyaKV

> ⚠️ It is recommended to use **physical cores only**. Using logical/hyperthreaded cores can increase contention and may degrade performance.

---

### DPDK / Native Network Stack

```bash
sudo docker run --rm -it --privileged \
  -p 60111:60111 \
  -v "$PWD/config.conf:/config.conf:ro" \
  shunyalabs/shunyakv:latest \
  --network-stack native --dpdk-pmd --poll-mode
```

This mode enables high-performance packet processing using DPDK.

---

## 📊 Benchmarking Tool

To evaluate performance and run load tests, use the dedicated benchmarking tool:

https://github.com/somesh-m/shunyakv-benchmark-cpp

The benchmarking tool supports:

- Configurable workloads
- Pipelining

This allows you to measure throughput and latency under realistic scenarios.

---

## ⚠️ Project Status

ShunyaKV is currently in **beta** and under heavy development.

The current version is:

```text
v0.1.1
```

This release is intended for experimentation, benchmarking, architecture validation, and early feedback. Production usage is not recommended yet unless you are comfortable with possible breaking changes and evolving internals.
