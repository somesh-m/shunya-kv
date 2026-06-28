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
#include <charconv>
#include <cctype>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "vector/vector_types.hh"

static seastar::logger vsearch_logger{"cmd_vsearch"};

namespace shunyakv {
namespace {

bool parse_embedding(std::string_view raw, std::vector<float> &out) {
    out.clear();

    std::size_t pos = 0;
    while (pos < raw.size()) {
        while (pos < raw.size() &&
               (std::isspace(static_cast<unsigned char>(raw[pos])) ||
                raw[pos] == '[' || raw[pos] == ']' || raw[pos] == ',')) {
            ++pos;
        }

        if (pos >= raw.size()) {
            break;
        }

        std::size_t end = pos;
        while (end < raw.size() && raw[end] != ',' && raw[end] != ']') {
            ++end;
        }

        float value = 0.0f;
        const auto *begin = raw.data() + pos;
        const auto *finish = raw.data() + end;
        const auto [ptr, ec] = std::from_chars(begin, finish, value);
        if (ec != std::errc{} || ptr != finish) {
            return false;
        }

        out.push_back(value);
        pos = end;
    }

    return !out.empty();
}

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
seastar::future<> handle_vsearch(const resp::ArgView &cmd,
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
    if (!parse_embedding(raw_embedding, embedding)) {
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
