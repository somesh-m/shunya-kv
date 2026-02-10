#include "kv_store.hh"
#include <coroutine>
#include <seastar/core/coroutine.hh>
#include <unordered_map>

namespace shunyakv {

seastar::future<> store::start(unsigned) { co_return; }

seastar::future<> store::stop() {
    _map.clear();
    _map.rehash(0);
    co_return;
}

seastar::future<bool> store::set(key_t key, std::string value) {
    // convert std::string -> sstring on the *owner shard*
    _map[std::move(key)] = seastar::sstring(value.data(), value.size());
    co_return true;
}

seastar::future<bool> store::set_with_ttl(key_t key, std::string value,
                                          uint64_t ttl) {
    // convert std::string -> sstring on the *owner shard*
    _map[std::move(key)] = seastar::sstring(value.data(), value.size());
    co_return true;
}

seastar::future<std::optional<std::string>> store::get(const key_t &key) const {
    auto it = _map.find(key);
    if (it == _map.end()) {
        co_return std::nullopt;
    }
    const seastar::sstring &s = it->second;
    co_return std::optional<std::string>(
        std::string(s.data(), s.size())); // safe across shards
}

} // namespace shunyakv
