#pragma once
#include "kv_store.hh"

#include <array>
#include <cmath>
#include <cstdint>
#include <functional> // std::hash
#include <limits>
#include <optional>
#include <seastar/core/future.hh>
#include <seastar/core/smp.hh>              // smp::count, this_shard_id
#include <seastar/core/sstring.hh>
#include <seastar/core/temporary_buffer.hh> // seastar::temporary_buffer
#include <seastar/net/api.hh>
#include <string_view> // not <string_value>

// static seastar::logger store_logger{"store"};
namespace shunyakv {

struct request_counters {
    uint64_t get_total{0};
    uint64_t get_forwarded{0};
    uint64_t set_total{0};
    uint64_t set_forwarded{0};
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

        uint64_t rank =
            static_cast<uint64_t>(std::ceil(q * static_cast<long double>(count)));
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
    latency_histogram get_non_forwarded;
    latency_histogram get_forwarded;
    latency_histogram set_non_forwarded;
    latency_histogram set_forwarded;

    void merge_from(const request_latency_counters &other) noexcept {
        get_non_forwarded.merge_from(other.get_non_forwarded);
        get_forwarded.merge_from(other.get_forwarded);
        set_non_forwarded.merge_from(other.set_non_forwarded);
        set_forwarded.merge_from(other.set_forwarded);
    }
};

// inline unsigned shard_for(std::string_view key) {
//     return static_cast<unsigned>(std::hash<std::string_view>{}(key) %
//                                  seastar::smp::count);
// }

// TODO: Move all the logic to router.cc, we want to keep the header file clean
class service {
  public:
    seastar::future<> start() {
        // store_logger.info("Starting store on shard {}",
        //                   seastar::this_shard_id());
        return _store.start(seastar::this_shard_id());
    }

    seastar::future<> stop() { return _store.stop(); }

    seastar::future<bool> local_set(std::string_view key,
                                    seastar::sstring value) {
        key_t k{key.data(), key.size()}; // sstring on this shard
        return _store.set(std::move(k), std::move(value));
    }
    seastar::future<bool> local_set(std::string_view key,
                                    seastar::sstring value,
                                    uint64_t ttl) {
        key_t k{key.data(), key.size()}; // sstring on this shard
        return _store.set_with_ttl(std::move(k), std::move(value), ttl);
    }
    seastar::future<std::optional<seastar::sstring>>
    local_get(std::string_view key) const {
        key_t k{key.data(), key.size()};
        return _store.get(k);
    }

    void record_get(bool forwarded) noexcept {
        ++_req_counters.get_total;
        if (forwarded) {
            ++_req_counters.get_forwarded;
        }
    }

    void record_set(bool forwarded) noexcept {
        ++_req_counters.set_total;
        if (forwarded) {
            ++_req_counters.set_forwarded;
        }
    }

    void record_get_latency(bool forwarded, uint64_t latency_us) noexcept {
        if (forwarded) {
            _latency_counters.get_forwarded.add_us(latency_us);
        } else {
            _latency_counters.get_non_forwarded.add_us(latency_us);
        }
    }

    void record_set_latency(bool forwarded, uint64_t latency_us) noexcept {
        if (forwarded) {
            _latency_counters.set_forwarded.add_us(latency_us);
        } else {
            _latency_counters.set_non_forwarded.add_us(latency_us);
        }
    }

    request_counters snapshot_request_counters() const noexcept {
        return _req_counters;
    }

    request_latency_counters snapshot_request_latency_counters() const noexcept {
        return _latency_counters;
    }

  private:
    store _store;
    request_counters _req_counters;
    request_latency_counters _latency_counters;
};

inline service &local_service() {
    static thread_local service svc;
    return svc;
}

} // namespace shunyakv
