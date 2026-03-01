#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

#include <sys/mman.h>
#include <unistd.h>

#include "MemoryId.hh"

inline std::atomic<std::size_t> g_total_os_allocated_bytes{0};

class MemorySlab {
  public:
    MemorySlab() = default;

    explicit MemorySlab(std::size_t bytes) { acquire(bytes); }

    // Move-only
    MemorySlab(MemorySlab &&other) noexcept { steal_from(other); }
    MemorySlab &operator=(MemorySlab &&other) noexcept {
        if (this != &other) {
            release();
            steal_from(other);
        }
        return *this;
    }

    MemorySlab(const MemorySlab &) = delete;
    MemorySlab &operator=(const MemorySlab &) = delete;

    ~MemorySlab() { release(); }

    void acquire(std::size_t bytes) {
        if (base_ != nullptr) {
            throw std::logic_error(
                "MemorySlab::acquire called on already-acquired slab");
        }
        if (bytes == 0) {
            throw std::invalid_argument(
                "MemorySlab::acquire bytes must be > 0");
        }

        void *p = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) {
            const int e = errno;
            throw std::runtime_error("mmap failed: " +
                                     std::string(std::strerror(e)));
        }

        base_ = static_cast<std::byte *>(p);
        size_ = bytes;
        g_total_os_allocated_bytes.fetch_add(bytes, std::memory_order_relaxed);
    }

    void release() noexcept {
        if (!base_)
            return;
        const std::size_t released_bytes = size_;
        ::munmap(base_, size_);
        base_ = nullptr;
        size_ = 0;
        g_total_os_allocated_bytes.fetch_sub(released_bytes,
                                             std::memory_order_relaxed);
    }

    MemoryId id() const noexcept { return MemoryId{base_, size_}; }

    std::byte *data() noexcept { return base_; }
    const std::byte *data() const noexcept { return base_; }
    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return base_ == nullptr; }

    bool contains(const void *p) const noexcept {
        if (!base_)
            return false;
        auto addr = reinterpret_cast<std::uintptr_t>(p);
        auto start = reinterpret_cast<std::uintptr_t>(base_);
        auto end = start + size_;
        return addr >= start && addr < end;
    }

  private:
    void steal_from(MemorySlab &other) noexcept {
        base_ = other.base_;
        size_ = other.size_;
        other.base_ = nullptr;
        other.size_ = 0;
    }

    std::byte *base_{nullptr};
    std::size_t size_{0};
};
