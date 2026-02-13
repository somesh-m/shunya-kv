#include <resp/resp_parser.hh>

#include <charconv>
#include <cstring>
#include <seastar/core/iostream.hh>
#include <seastar/util/memory-data-source.hh>
#include <stdexcept>
#include <string>

using namespace resp;
namespace resp {

using seastar::future;
using seastar::sstring;

static inline void fail(const char *msg) {
    throw std::runtime_error(std::string("RESP error: ") + msg);
}

static frame_parse_status parse_int_crlf(std::string_view s, size_t &pos,
                                         int64_t &out) {
    const size_t start = pos;
    const size_t cr = s.find('\r', start);
    if (cr == std::string_view::npos) {
        return frame_parse_status::need_more;
    }
    if (cr + 1 >= s.size()) {
        return frame_parse_status::need_more;
    }
    if (s[cr + 1] != '\n') {
        return frame_parse_status::invalid;
    }

    out = 0;
    const auto [ptr, ec] =
        std::from_chars(s.data() + start, s.data() + cr, out);
    if (ec != std::errc{} || ptr != s.data() + cr) {
        return frame_parse_status::invalid;
    }

    pos = cr + 2;
    return frame_parse_status::ok;
}

frame_parse_status parse_frame_length(std::string_view s, size_t &frame_len) {
    if (s.empty()) {
        return frame_parse_status::need_more;
    }
    if (s.front() != '*') {
        return frame_parse_status::invalid;
    }

    size_t pos = 1;
    int64_t argc = 0;
    auto st = parse_int_crlf(s, pos, argc);
    if (st != frame_parse_status::ok) {
        return st;
    }
    if (argc <= 0) {
        return frame_parse_status::invalid;
    }

    for (int64_t i = 0; i < argc; ++i) {
        if (pos >= s.size()) {
            return frame_parse_status::need_more;
        }
        if (s[pos] != '$') {
            return frame_parse_status::invalid;
        }
        ++pos;

        int64_t bulk_len = 0;
        st = parse_int_crlf(s, pos, bulk_len);
        if (st != frame_parse_status::ok) {
            return st;
        }
        if (bulk_len < 0) {
            return frame_parse_status::invalid;
        }

        const size_t need = pos + static_cast<size_t>(bulk_len) + 2;
        if (need > s.size()) {
            return frame_parse_status::need_more;
        }
        if (s[pos + static_cast<size_t>(bulk_len)] != '\r' ||
            s[pos + static_cast<size_t>(bulk_len) + 1] != '\n') {
            return frame_parse_status::invalid;
        }

        pos = need;
    }

    frame_len = pos;
    return frame_parse_status::ok;
}

future<Array> parse_command_from_frame(const seastar::sstring &frame) {
    seastar::temporary_buffer<char> b(frame.size());
    std::memcpy(b.get_write(), frame.data(), frame.size());
    auto in = seastar::util::as_input_stream(std::move(b));

    Reader r;
    auto cmd = co_await r.read_command(in);
    co_await in.close();
    co_return cmd;
}

future<> Reader::ensure(seastar::input_stream<char> &in, size_t n) {
    while (_buf.size() < n) {
        auto chunk = co_await in.read(); // temporary_buffer<char>
        if (chunk.empty()) {
            // EOF
            co_return;
        }
        _buf.append(chunk.get(), chunk.size());
    }
}

future<char> Reader::read_byte(seastar::input_stream<char> &in) {
    co_await ensure(in, 1);
    if (_buf.empty()) {
        // clean EOF
        co_return '\0';
    }
    char c = _buf[0];
    _buf.erase(_buf.begin(), _buf.begin() + 1);

    co_return c;
}

future<sstring> Reader::read_line_crlf(seastar::input_stream<char> &in) {
    // Read until "\r\n" and return the line excluding CRLF.
    while (true) {
        auto pos = _buf.find("\r\n");
        if (pos != sstring::npos) {
            sstring line = _buf.substr(0, pos);
            _buf.erase(_buf.begin(), _buf.begin() + pos + 2);

            co_return line;
        }

        auto chunk = co_await in.read();
        if (chunk.empty()) {
            // EOF: if no buffered bytes -> signal clean close
            if (_buf.empty()) {
                co_return sstring{};
            }
            fail("unexpected EOF while reading line");
        }
        _buf.append(chunk.get(), chunk.size());
    }
}

future<int64_t> Reader::read_int_line(seastar::input_stream<char> &in) {
    auto line = co_await read_line_crlf(in);
    if (line.empty()) {
        // Could be clean close; caller decides if that's allowed
        co_return 0;
    }

    // Convert to std::string for stoll (simple + safe)
    try {
        co_return std::stoll(std::string(line.data(), line.size()));
    } catch (...) {
        fail("invalid integer");
    }
}

future<sstring> Reader::read_bulk(seastar::input_stream<char> &in) {
    // '$' already consumed by caller
    auto len_line = co_await read_line_crlf(in);
    if (len_line.empty()) {
        fail("unexpected EOF reading bulk length");
    }

    int64_t len = 0;
    try {
        len = std::stoll(std::string(len_line.data(), len_line.size()));
    } catch (...) {
        fail("invalid bulk length");
    }

    if (len == -1) {
        // Null bulk string (allowed by RESP, but commands usually shouldn't use
        // it)
        co_return sstring{};
    }
    if (len < 0) {
        fail("negative bulk length");
    }

    // Optional safety limit (strongly recommended)
    static constexpr int64_t MAX_BULK = 64 * 1024 * 1024; // 64MB
    if (len > MAX_BULK) {
        fail("bulk string too large");
    }

    // Need: <len bytes> + "\r\n"
    co_await ensure(in, size_t(len) + 2);
    if (_buf.size() < size_t(len) + 2) {
        fail("unexpected EOF reading bulk payload");
    }

    sstring data = _buf.substr(0, size_t(len));

    if (_buf[size_t(len)] != '\r' || _buf[size_t(len) + 1] != '\n') {
        fail("missing CRLF after bulk payload");
    }

    _buf.erase(_buf.begin(), _buf.begin() + size_t(len) + 2);

    co_return data;
}

future<Array> Reader::read_command(seastar::input_stream<char> &in) {
    // Expect: *<n>\r\n then n x $<len>\r\n<data>\r\n
    char t = co_await read_byte(in); // returns exactly one byte
    if (t == '\0') {
        // clean close
        co_return Array{};
    }
    if (t != '*') {
        fail("expected array '*'");
    }

    auto n_line = co_await read_line_crlf(in);
    if (n_line.empty()) {
        fail("unexpected EOF reading array length");
    }

    int64_t n = 0;
    try {
        n = std::stoll(std::string(n_line.data(), n_line.size()));
    } catch (...) {
        fail("invalid array length");
    }

    if (n <= 0) {
        fail("array length must be positive");
    }

    // Optional safety limit
    static constexpr int64_t MAX_ARGS = 1024;
    if (n > MAX_ARGS) {
        fail("too many arguments");
    }

    Array argv;
    argv.reserve(size_t(n));

    for (int64_t i = 0; i < n; i++) {
        char bt = co_await read_byte(in);
        if (bt == '\0') {
            fail("unexpected EOF inside array");
        }
        if (bt != '$') {
            fail("expected bulk string '$' in array");
        }
        auto s = co_await read_bulk(in);
        argv.push_back(std::move(s));
    }

    co_return argv;
}

} // namespace resp
