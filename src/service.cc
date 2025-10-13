// #include "service.hh"
// #include <seastar/core/coroutine.hh>

// namespace shunyakv {

// seastar::future<> service::start() {
//     if (_started)
//         co_return;
//     _started = true;
//     // (Optional) reserve if you know expected keys to reduce rehashing.
//     // _map.reserve(1 << 16);
//     co_return;
// }

// seastar::future<> service::stop() {
//     if (!_started)
//         co_return;
//     _map.clear();
//     _map.rehash(0);
//     _started = false;
//     co_return;
// }

// seastar::future<> service::set(const std::string &key,
//                                const std::string &value) {
//     _map[key] = value;
//     co_return;
// }

// seastar::future<std::optional<std::string>>
// service::get(const std::string &key) {
//     if (auto it = _map.find(key); it != _map.end()) {
//         co_return it->second; // copies value (fine for now)
//     }
//     co_return std::nullopt;
// }

// seastar::future<std::optional<std::string>>
// service::del(const std::string &key) {
//     if (auto it = _map.find(key); it != _map.end()) {
//         std::optional<std::string> old = std::move(it->second);
//         _map.erase(it);
//         co_return old;
//     }
//     co_return std::nullopt;
// }

// seastar::future<size_t> service::size() const { co_return _map.size(); }

// } // namespace shunyakv
