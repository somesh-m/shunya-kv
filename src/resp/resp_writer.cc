#include "resp/resp_writer.hh"

#include <seastar/core/iostream.hh>
#include <seastar/core/sstring.hh>

namespace resp {

using seastar::future;
using seastar::sstring;

static future<> write_crlf(seastar::output_stream<char> &out) {
    co_await out.write("\r\n", 2);
}

future<> write_simple(seastar::output_stream<char> &out, const sstring &msg) {
    co_await out.write("+", 1);
    co_await out.write(msg.data(), msg.size());
    co_await write_crlf(out);
}

future<> write_error(seastar::output_stream<char> &out, const sstring &msg) {
    co_await out.write("-", 1);
    co_await out.write(msg.data(), msg.size());
    co_await write_crlf(out);
}

future<> write_integer(seastar::output_stream<char> &out, int64_t v) {
    auto line = ":" + seastar::to_sstring(v);
    co_await out.write(line.data(), line.size());
    co_await write_crlf(out);
}

future<> write_bulk(seastar::output_stream<char> &out, const sstring &bytes) {
    // RESP bulk string: $<len>\r\n<bytes>\r\n
    auto hdr = "$" + seastar::to_sstring(bytes.size());
    co_await out.write(hdr.data(), hdr.size());
    co_await write_crlf(out);

    if (!bytes.empty()) {
        co_await out.write(bytes.data(), bytes.size());
    }
    co_await write_crlf(out);
}

future<> write_null(seastar::output_stream<char> &out) {
    co_await out.write("$-1\r\n", 5);
}

future<> write_array_bulk(seastar::output_stream<char> &out,
                          const Array &items) {
    // *<n>\r\n ($len\r\nitem\r\n)...
    auto hdr = "*" + seastar::to_sstring(items.size());
    co_await out.write(hdr.data(), hdr.size());
    co_await write_crlf(out);

    for (const auto &s : items) {
        co_await write_bulk(out, s);
    }
}

} // namespace resp
