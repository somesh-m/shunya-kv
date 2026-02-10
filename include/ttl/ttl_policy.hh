#pragma once

#include "ttl/entry.hh"
#include <algorithm>
#include <cstdint>

// All time units are measures in ms

namespace ttl {

enum class Mode {
    Fixed,            // expires_at = base_ttl
    Sliding,          // expires_at = now + base_ttl
    TieredExponential // ttl = min(base_ttl * 2^heat, max_ttl) with idle reset
};

struct ttl_policy {
    Mode mode = Mode::Sliding;
    // TODO: make these values configurable from a config file
    uint64_t base_ttl = 5000;
    uint64_t max_ttl = 600000;
    uint64_t idle_window = 30000;
    uint8_t heat_max = 10;

    uint64_t compute_new_expiry(uint64_t now, Entry &e) const {
        // Fixed no adaptive ttl
        if (mode == Mode::Fixed) {
            return now;
        }

        // Linear ttl adaptation
        if (mode == Mode::Sliding) {
            e.last_access = now;
            return now + base_ttl;
        }

        // Exponential ttl adaptation
        // If the key is idle for more than the idle window then make the heat 0
        if (idle_window != 0 && e.last_access != 0 &&
            (now - e.last_access) > idle_window) {
            e.heat = 0;
        }

        if (e.heat < heat_max)
            e.heat++;

        // Compute ttl
        uint64_t ttl = base_ttl;
        uint8_t shift = e.heat;

        while (shift-- > 0) {
            if (ttl >= max_ttl / 2) {
                ttl = max_ttl;
                break;
            }

            ttl <<= 1;
        }
        ttl = std::min(ttl, max_ttl);

        e.last_access = now;
        return now + ttl;
    }
};
} // namespace ttl
