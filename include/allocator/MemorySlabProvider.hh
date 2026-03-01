#pragma once
#include "MemorySlab.hh"
#include <cstddef>

class MemorySlabProvider {
  public:
    MemorySlabProvider() = default;

    // Dumb factory: creates and returns an owned slab.
    MemorySlab acquire(std::size_t bytes) { return MemorySlab(bytes); }
};
