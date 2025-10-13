// #pragma once

// #include <optional>
// #include <seastar/core/future.hh>
// #include <string>
// #include <unordered_map>

// namespace shunyakv {

// class service {
//   public:
//     service() = default;

//     // Lifecycle (idempotent)
//     seastar::future<> start();
//     seastar::future<> stop();

//     // Basic API used by your command handlers
//     seastar::future<> set(const std::string &key, const std::string &value);
//     seastar::future<std::optional<std::string>> get(const std::string &key);

//     // Optional helpers (not used yet, but handy)
//     seastar::future<std::optional<std::string>> del(const std::string &key);
//     seastar::future<size_t> size() const;

//   private:
//     bool _started{false};
//     std::unordered_map<key_t, value_t> _map;
// };

// // If some files still refer to kv::service, keep them working:
// } // namespace shunyakv

// namespace kv {
// using service = ::shunyakv::service;
// }
