#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace shunyakv {

struct request_counters {
    uint64_t get_total{0};
    uint64_t get_forwarded{0};
    uint64_t set_total{0};
    uint64_t set_forwarded{0};
    uint64_t cache_miss{0};
};

struct latency_histogram {
    static constexpr size_t kBuckets = 64;
    std::array<uint64_t, kBuckets> buckets{};
    uint64_t count{0};

    void add_us(uint64_t us) noexcept {
        ++count;
        size_t idx = 0;
        if (us != 0) {
            idx = static_cast<size_t>(63 - __builtin_clzll(us));
            if (idx >= kBuckets) {
                idx = kBuckets - 1;
            }
        }
        ++buckets[idx];
    }

    void merge_from(const latency_histogram &other) noexcept {
        count += other.count;
        for (size_t i = 0; i < kBuckets; ++i) {
            buckets[i] += other.buckets[i];
        }
    }

    uint64_t quantile_us(double q) const noexcept {
        if (count == 0) {
            return 0;
        }
        if (q < 0.0) {
            q = 0.0;
        }
        if (q > 1.0) {
            q = 1.0;
        }

        uint64_t rank = static_cast<uint64_t>(
            std::ceil(q * static_cast<long double>(count)));
        if (rank == 0) {
            rank = 1;
        }

        uint64_t seen = 0;
        for (size_t i = 0; i < kBuckets; ++i) {
            seen += buckets[i];
            if (seen >= rank) {
                if (i >= 63) {
                    return std::numeric_limits<uint64_t>::max();
                }
                return (uint64_t{1} << (i + 1)) - 1;
            }
        }
        return std::numeric_limits<uint64_t>::max();
    }
};

struct request_latency_counters {
    latency_histogram total;

    void merge_from(const request_latency_counters &other) noexcept {
        total.merge_from(other.total);
    }
};

} // namespace shunyakv
