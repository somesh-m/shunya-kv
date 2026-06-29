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

#include "vector/helper.hh"
#include "vector/vector_types.hh"

static seastar::logger vset_logger{"cmd_vset"};

namespace shunyakv {
/**
 * VSET <KEY> <VALUE> <INDEX> <EMBEDDING>
 */
seastar::future<> handle_vset(const resp::ArgvView &cmd,
                              seastar::output_stream<char> &out,
                              shunyakv::service &service) {
    if (cmd.size() < 5) {
        co_await resp::write_error(
            out, "ERR wrong number of arguements for 'V_SET'");
        co_return;
    }

    const auto &key = cmd[1];
    const auto &value = cmd[2];
    const auto &index = cmd[3];
    const auto &raw_embedding = cmd[4];

    if (key.empty()) {
        co_await resp::write_error(out, "ERR empty key");
        co_return;
    }

    if (index.empty()) {
        co_await resp::write_error(out, "ERR empty index");
        co_return;
    }

    if (raw_embedding.empty()) {
        co_await resp::write_error(out, "ERR empty embedding");
        co_return;
    }

    std::vector<float> embedding;
    if (!::vdb::parse_embedding(raw_embedding, embedding)) {
        co_await resp::write_error(out, "ERR invalid embedding");
        co_return;
    }

    const bool ok = co_await service.vset(index, key, std::move(embedding),
                                          std::string(value));
    if (ok) {
        co_await resp::write_simple(out, "OK");
    } else {
        co_await resp::write_error(out, "NOT STORED");
    }
    co_return;
}
} // namespace shunyakv
