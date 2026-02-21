// router.cc
#include "router.hh"
using namespace seastar;
namespace shunyakv {

future<> service::start() { return _store.start(this_shard_id()); }

future<> service::stop() { return _store.stop(); }

future<bool> service::local_set(std::string_view key, sstring value) {
    key_t k{key.data(), key.size()};
    return _store.set(std::move(k), std::move(value));
}

future<bool> service::local_set(std::string_view key, sstring value,
                                uint64_t ttl) {
    key_t k{key.data(), key.size()};
    return _store.set_with_ttl(std::move(k), std::move(value), ttl);
}

future<std::optional<sstring>> service::local_get(std::string_view key) const {
    key_t k{key.data(), key.size()};
    return _store.get(k);
}

void service::record_set(bool forwarded) noexcept {
    ++_req_counters.set_total;
    if (forwarded) {
        ++_req_counters.set_forwarded;
    }
}

void service::record_get(bool forwarded) noexcept {
    ++_req_counters.get_total;
    if (forwarded) {
        ++_req_counters.get_forwarded;
    }
}

void service::record_get_latency(uint64_t latency_us) noexcept {
    _latency_counters.total.add_us(latency_us);
}

void service::record_set_latency(uint64_t latency_us) noexcept {
    _latency_counters.total.add_us(latency_us);
}

void service::record_cache_miss() noexcept { ++_req_counters.cache_miss; }

request_counters service::snapshot_request_counters() const noexcept {
    return _req_counters;
}

request_latency_counters service::snapshot_request_latency_counters() const noexcept {
    return _latency_counters;
}

} // namespace shunyakv
