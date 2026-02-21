#pragma once
#include "protocol.hh"
#include "router.hh"
#include <functional>
#include <resp/resp_types.hh>
#include <seastar/core/future.hh>
#include <seastar/core/sharded.hh> // ✅ seastar::sharded<T>
#include <seastar/net/api.hh>
#include <string_view>
#include <unordered_map>

namespace shunyakv {
using Handler = std::function<seastar::future<>(const resp::ArgvView &,
                                                seastar::output_stream<char> &,
                                                shunyakv::service &)>;

const std::unordered_map<std::string_view, Handler> &command_dispatch();
} // namespace shunyakv
