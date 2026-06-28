#include "cmd_vset.hh"
#include "commands.hh"
#include "hash.hh"
#include "router.hh"
#include <resp/resp_types.hh>
#include <resp/resp_writer.hh>

#include <seastar/core/coroutine.hh>
#include <seastar/core/future.hh>
#include <seastar/core/iostream.hh>
#include <seastar/core/smp.hh>
#include <string>
#include "vector/vector_types.hh

static seastar::logger vset_logger{"cmd_vset"};

namespace shunyakv {
namespace {
seastar::future<bool> vset(shunyakv::service &service, std::string_view index,
                           std::string_view key, std::vector<float> embedding,
                           seastar::sstring value) {
    return service.local_vset(std::move(index), std::move(key),
                              std::move(embedding), std::move(value));
}
} // namespace
/**
 * VSET <KEY> <VALUE> <INDEX> <EMBEDDING>
 */
seastar::future<> handle_vset(const resp::ArgView &cmd,
                              seastar::output_stream<char> &out,
                              shunyakv::service &service) {
    if (cmd.size() < 4) {
        co_await resp::write_error(
            out, "ERR wrong number of arguements for 'V_SET'");
        co_return;
    }

    const auto &key = cmd[1];
    const auto &value = cmd[2];
    const auto &index = cmd[3];
    const auto &embedding = cmd[4];

    if (key.empty()) {
        co_await resp::write_error(out, "ERR empty key");
        co_return;
    }

    if (index.empty()) {
        co_await resp::write_error(out, "ERR empty index");
        co_return;
    }

    if (embedding.empty()) {
        co_await resp::write_error(out, "ERR empty embedding");
        co_return;
    }
    return vset(service, index, key, std::move(value), std::move(embedding));
}
} // namespace shunyakv
