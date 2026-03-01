#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "MemoryId.hh"
#include "MemorySlabProvider.hh"

class PageProvider {
  private:
    std::size_t page_bytes_{64 * 1024};
    std::size_t slab_bytes_{2 * 1024 * 1024};

    uint32_t max_slabs_{24};
    uint32_t preallocate_count_{10};

    MemorySlabProvider slab_provider_;
    std::vector<MemorySlab> slabs_;
    std::size_t current_slab_idx_{0};

    std::vector<MemoryId> free_mem_;

    uint32_t page_allocated_count_{0}; // within current slab
    uint32_t pages_per_slab_{0};
    uint64_t total_pages_created_{0};

    uint32_t page_buffer_count_{10};

  public:
    PageProvider(std::size_t page_bytes, uint32_t preallocate_count = 10,
                 uint32_t max_slabs = 24,
                 std::size_t slab_bytes = 2 * 1024 * 1024)
        : page_bytes_(page_bytes), slab_bytes_(slab_bytes),
          max_slabs_(max_slabs), preallocate_count_(preallocate_count) {

        if (page_bytes_ == 0)
            throw std::invalid_argument("page_bytes must be > 0");
        if (slab_bytes_ == 0)
            throw std::invalid_argument("slab_bytes must be > 0");
        if (slab_bytes_ % page_bytes_ != 0) {
            throw std::invalid_argument(
                "slab_bytes must be divisible by page_bytes");
        }

        pages_per_slab_ = static_cast<uint32_t>(slab_bytes_ / page_bytes_);

        // Eager first slab (your current approach). Can be made fully lazy
        // later.
        if (!try_acquire_slab()) {
            throw std::runtime_error("Failed to acquire initial slab");
        }
        pre_partition_slabs();
    }

    bool try_fetch_page(MemoryId &mem) {
        if (free_mem_.empty()) {
            if (!refill_slabs())
                return false;
        }
        if (free_mem_.empty())
            return false;

        mem = free_mem_.back();
        free_mem_.pop_back();
        return true;
    }

    uint64_t total_pages_created() const noexcept {
        return total_pages_created_;
    }

  private:
    MemoryId current_slab_id() const noexcept {
        return slabs_[current_slab_idx_].id();
    }

    bool try_acquire_slab() {
        if (slabs_.size() >= max_slabs_)
            return false;

        slabs_.push_back(slab_provider_.acquire(slab_bytes_));
        current_slab_idx_ = slabs_.size() - 1;
        page_allocated_count_ = 0;

        // Clamp preallocate_count_ once (safe even if called multiple times).
        if (preallocate_count_ > pages_per_slab_)
            preallocate_count_ = pages_per_slab_;
        return true;
    }

    bool refill_slabs() {
        // Acquire a slab if none yet or current exhausted.
        if (slabs_.empty() || page_allocated_count_ >= pages_per_slab_) {
            if (!try_acquire_slab())
                return false;
        }

        const uint32_t remaining = pages_per_slab_ - page_allocated_count_;
        const uint32_t to_add = std::min(page_buffer_count_, remaining);

        const auto slab = current_slab_id();

        for (uint32_t i = 0; i < to_add; ++i) {
            auto mem = MemoryId{
                slab.base + (page_allocated_count_ * page_bytes_), page_bytes_};
            free_mem_.push_back(mem);
            page_allocated_count_++;
            set_page_header(mem.base, slab.base, current_slab_idx_);
        }
        total_pages_created_ += to_add;
        return true;
    }

    void pre_partition_slabs() {
        // Called only once after first slab acquisition.
        const auto slab = current_slab_id();
        for (uint32_t i = 0; i < preallocate_count_; ++i) {
            auto mem = MemoryId{slab.base + (i * page_bytes_), page_bytes_};
            free_mem_.push_back(mem);
            // set page header to this page
            set_page_header(mem.base, slab.base, current_slab_idx_);
            page_allocated_count_++;
        }
        total_pages_created_ += preallocate_count_;
    }

    /**
     * We need to set the page header so that we can track the page originated
     * from which slab. This will be needed for compaction and eviction.
     */
    void set_page_header(std::byte *page_base_addr, std::byte *slab_base_addr,
                         uint32_t slab_index) {
        new (page_base_addr) PageHeader(slab_base_addr, slab_index);
    }
};
