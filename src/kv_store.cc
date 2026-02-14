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

seastar::future<bool> store::set(key_t key, seastar::sstring value) {
    _map[std::move(key)] = std::move(value);
    co_return true;
}

seastar::future<bool> store::set_with_ttl(key_t key, seastar::sstring value,
                                          uint64_t ttl) {
    _map[std::move(key)] = std::move(value);
    co_return true;
}

seastar::future<std::optional<seastar::sstring>>
store::get(const key_t &key) const {
    auto it = _map.find(key);
    if (it == _map.end()) {
        co_return std::nullopt;
    }
    co_return std::optional<seastar::sstring>(it->second);
}

} // namespace shunyakv
