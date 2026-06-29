#include "cmd_vsearch.hh"
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
#include <vector>

#include "vector/helper.hh"
#include "vector/vector_types.hh"

static seastar::logger vsearch_logger{"cmd_vsearch"};

namespace shunyakv {
namespace {

seastar::future<> write_array_len(seastar::output_stream<char> &out,
                                  std::size_t len) {
    auto hdr = "*" + seastar::to_sstring(len);
    co_await out.write(hdr.data(), hdr.size());
    co_await out.write("\r\n", 2);
}

seastar::future<> write_search_results(
    seastar::output_stream<char> &out,
    const std::vector<VectorSearchResult> &results) {
    co_await write_array_len(out, results.size());

    for (const auto &result : results) {
        co_await write_array_len(out, 3);
        co_await resp::write_bulk(out, seastar::sstring(result.key));
        co_await resp::write_bulk(out, seastar::sstring(result.value));
        co_await resp::write_bulk(out, seastar::to_sstring(result.score));
    }
}

} // namespace

/**
 * VSEARCH <INDEX> <EMBEDDING>
 */
seastar::future<> handle_vsearch(const resp::ArgvView &cmd,
                                 seastar::output_stream<char> &out,
                                 shunyakv::service &service) {
    if (cmd.size() < 3) {
        co_await resp::write_error(
            out, "ERR wrong number of arguements for 'V_SEARCH'");
        co_return;
    }

    const auto &index = cmd[1];
    const auto &raw_embedding = cmd[2];

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

    auto results = co_await service.vsearch(index, std::move(embedding));
    if (results.empty()) {
        co_await resp::write_null(out);
    } else {
        co_await write_search_results(out, results);
    }
    co_return;
};
} // namespace shunyakv
