#pragma once
#include <cstddef>
#include <cstdint>

struct MemoryId {
    std::byte *base{nullptr};
    std::size_t size{0};
};

struct PageHeader {
    uint32_t live_slots_{0};
    uint32_t slots_capacity_{0}; // should be computed once
    uint32_t slot_owner_id_{0};
    std::byte *slab_base_addr_;
    uint32_t slab_index_;
    uint16_t version_{0};
    uint32_t magic = 0x50414745;

    PageHeader(std::byte *slab_base_addr, uint32_t slab_index)
        : slab_base_addr_(slab_base_addr), slab_index_(slab_index) {}
};
