#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "MemoryId.hh"
#include "PageProvider.hh"
#include <seastar/core/shard_id.hh>

// Per-slot header: lets us find the owning page header from a user pointer.
struct SlotHeader {
    PageHeader *page_hdr{nullptr};
};

class EntryProvider {
  private:
    std::size_t entry_bytes_{8 * 1024}; // payload bytes returned to caller
    std::size_t page_bytes_{64 * 1024};

    PageProvider provider_;
    std::vector<MemoryId> pages_;
    std::size_t current_page_idx_{0};

    const uint32_t owner_id_;

    // Layout computed once
    std::size_t slots_base_offset_{
        0};                      // offset from page.base to first slot_raw
    std::size_t user_offset_{0}; // offset from slot_raw to user_ptr
    std::size_t slot_stride_{0}; // bytes per slot (header+pad+payload)

    // Per-page carving state
    uint32_t entry_per_page_{0};
    uint32_t page_slot_cursor_{0};

    // Free list of user pointers (payload pointers)
    std::vector<std::byte *> free_entry_;
    std::uint64_t total_pages_acquired_{0};
    std::uint64_t total_pages_in_use_{0};
    std::uint64_t alloc_ops_{0};
    std::uint64_t free_ops_{0};
    std::uint64_t alloc_fail_ops_{0};
    std::uint64_t total_entries_created_{0};
    std::uint64_t live_entries_{0};

  public:
    explicit EntryProvider(uint32_t owner_id)
        : provider_(page_bytes_), owner_id_(owner_id) {
        init();
    }

    EntryProvider(std::size_t entry_bytes, std::size_t page_bytes,
                  uint32_t owner_id)
        : entry_bytes_(entry_bytes), page_bytes_(page_bytes),
          provider_(page_bytes_), owner_id_(owner_id) {
        init();
    }

    // Allocates one Entry payload region.
    bool try_fetch_entry(MemoryId &entry) {
        enforce_owner_shard();
        ++alloc_ops_;
        if (free_entry_.empty()) {
            if (!refill_pages()) {
                ++alloc_fail_ops_;
                return false;
            }
        }
        if (free_entry_.empty()) {
            ++alloc_fail_ops_;
            return false;
        }

        std::byte *user_ptr = free_entry_.back();
        free_entry_.pop_back();

        increment_used_slot_count_from_user_ptr(user_ptr);
        ++live_entries_;

        entry = MemoryId{user_ptr, entry_bytes_};
        return true;
    }

    // Frees an Entry payload region.
    void deallocate(std::byte *user_ptr) {
        enforce_owner_shard();
        if (!user_ptr)
            return;
        ++free_ops_;

        PageHeader *hdr = page_hdr_from_user_ptr(user_ptr);
        if (!hdr)
            return; // debug: corrupted slot header
        if (hdr->slot_owner_id_ != owner_id_) {
            throw std::runtime_error(
                "EntryProvider::deallocate called with foreign-owner slot");
        }

        if (decrement_used_slot_count(reinterpret_cast<std::byte *>(hdr)) &&
            live_entries_ > 0) {
            --live_entries_;
        }

        // push back for reuse
        free_entry_.push_back(user_ptr);
    }

  private:
    void enforce_owner_shard() const {
        if (seastar::this_shard_id() != owner_id_) {
            throw std::runtime_error(
                "EntryProvider operation attempted from non-owner shard");
        }
    }

    void init() {
        if (entry_bytes_ == 0)
            throw std::invalid_argument("entry_bytes must be > 0");
        if (page_bytes_ == 0)
            throw std::invalid_argument("page_bytes must be > 0");

        // Since we don't know actual Entry type alignment yet, use conservative
        // alignment.
        constexpr std::size_t kAlign = alignof(std::max_align_t);

        // Slot layout inside a page:
        // page: [PageHeader][pad]-> slots_base
        // slot: [SlotHeader][pad]-> user_ptr ... payload bytes
        slots_base_offset_ = align_up_size(sizeof(PageHeader), kAlign);
        user_offset_ = align_up_size(sizeof(SlotHeader), kAlign);
        slot_stride_ = user_offset_ + entry_bytes_;

        if (slots_base_offset_ >= page_bytes_) {
            throw std::invalid_argument("PageHeader too large for page");
        }
        if (slot_stride_ == 0 || slot_stride_ > page_bytes_) {
            throw std::invalid_argument(
                "slot stride invalid (entry_bytes too large?)");
        }

        const std::size_t usable = page_bytes_ - slots_base_offset_;
        entry_per_page_ = static_cast<uint32_t>(usable / slot_stride_);

        if (entry_per_page_ == 0) {
            throw std::invalid_argument("entry_bytes too large to fit in page "
                                        "after header/alignment/slot header");
        }

        if (!try_acquire_page()) {
            throw std::runtime_error("Failed to acquire initial page");
        }

        partition_current_page();
    }

    bool try_acquire_page() {
        MemoryId page;
        if (!provider_.try_fetch_page(page))
            return false;

        pages_.push_back(page);
        ++total_pages_acquired_;
        current_page_idx_ = pages_.size() - 1;
        page_slot_cursor_ = 0;

        // EntryProvider-owned header fields (capacity/owner/live)
        set_entry_header(page.base);

        return true;
    }

    MemoryId current_page() const noexcept { return pages_[current_page_idx_]; }

    void partition_current_page() {
        const MemoryId page = current_page();
        std::byte *slots_base = page.base + slots_base_offset_;
        PageHeader *ph = reinterpret_cast<PageHeader *>(page.base);
        const std::uint32_t created_now = entry_per_page_ - page_slot_cursor_;

        while (page_slot_cursor_ < entry_per_page_) {
            std::byte *slot_raw =
                slots_base +
                (static_cast<std::size_t>(page_slot_cursor_) * slot_stride_);

            // Write per-slot header
            auto *sh = reinterpret_cast<SlotHeader *>(slot_raw);
            sh->page_hdr = ph;

            // User pointer returned to caller
            std::byte *user_ptr = slot_raw + user_offset_;
            free_entry_.push_back(user_ptr);

            page_slot_cursor_++;
        }
        total_entries_created_ += created_now;
    }

    bool refill_pages() {
        if (pages_.empty() || page_slot_cursor_ >= entry_per_page_) {
            if (!try_acquire_page())
                return false;
        }
        if (free_entry_.empty()) {
            partition_current_page();
        }
        return true;
    }

    // ----- Header updates (EntryProvider-owned fields) -----

    void set_entry_header(std::byte *page_base_addr) {
        auto *hdr = reinterpret_cast<PageHeader *>(page_base_addr);
        hdr->slots_capacity_ = entry_per_page_;
        hdr->slot_owner_id_ = owner_id_;
        hdr->live_slots_ = 0;
    }

    void increment_used_slot_count(std::byte *page_base_addr) {
        auto *hdr = reinterpret_cast<PageHeader *>(page_base_addr);
        if (hdr->live_slots_ == 0) {
            ++total_pages_in_use_;
        }
        ++hdr->live_slots_;
    }

    bool decrement_used_slot_count(std::byte *page_base_addr) {
        auto *hdr = reinterpret_cast<PageHeader *>(page_base_addr);
        if (hdr->live_slots_ == 0)
            return false;
        --hdr->live_slots_;
        if (hdr->live_slots_ == 0 && total_pages_in_use_ > 0) {
            --total_pages_in_use_;
        }
        return true;
    }

    // ----- Slot header decoding helpers -----

    SlotHeader *slot_header_from_user_ptr(std::byte *user_ptr) const noexcept {
        return reinterpret_cast<SlotHeader *>(user_ptr - user_offset_);
    }

    PageHeader *page_hdr_from_user_ptr(std::byte *user_ptr) const noexcept {
        SlotHeader *sh = slot_header_from_user_ptr(user_ptr);
        return sh ? sh->page_hdr : nullptr;
    }

    void increment_used_slot_count_from_user_ptr(std::byte *user_ptr) {
        PageHeader *hdr = page_hdr_from_user_ptr(user_ptr);
        if (!hdr)
            return;
        increment_used_slot_count(reinterpret_cast<std::byte *>(hdr));
    }

  public:
    std::uint64_t total_pages_acquired() const noexcept {
        return total_pages_acquired_;
    }
    std::uint64_t total_pages_in_use() const noexcept {
        return total_pages_in_use_;
    }
    std::uint64_t alloc_ops() const noexcept { return alloc_ops_; }
    std::uint64_t free_ops() const noexcept { return free_ops_; }
    std::uint64_t alloc_fail_ops() const noexcept { return alloc_fail_ops_; }
    std::uint64_t total_entries_created() const noexcept {
        return total_entries_created_;
    }
    double fragmentation_ratio() const noexcept {
        const double total_slots = static_cast<double>(total_pages_acquired_) *
                                   static_cast<double>(entry_per_page_);
        if (total_slots <= 0.0) {
            return 0.0;
        }
        return 1.0 - (static_cast<double>(live_entries_) / total_slots);
    }
    std::uint64_t total_pages_created() const noexcept {
        return provider_.total_pages_created();
    }

    // ----- Alignment helpers -----
    static inline std::size_t align_up_size(std::size_t x, std::size_t a) {
        return (x + (a - 1)) & ~(a - 1);
    }
};
