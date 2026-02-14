#include <resp/resp_parser.hh>

#include <charconv>
#include <seastar/core/iostream.hh>
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
    std::string_view s(frame.data(), frame.size());

    if (s.empty() || s.front() != '*') {
        fail("expected array '*'");
    }
    size_t pos = 1;

    int64_t argc = 0;
    auto st = parse_int_crlf(s, pos, argc);
    if (st != frame_parse_status::ok || argc <= 0) {
        fail("invalid array length");
    }

    static constexpr int64_t MAX_ARGS = 1024;
    if (argc > MAX_ARGS) {
        fail("too many arguments");
    }

    Array argv;
    argv.reserve(size_t(argc));
    for (int64_t i = 0; i < argc; ++i) {
        if (pos >= s.size() || s[pos] != '$') {
            fail("expected bulk string '$' in array");
        }
        ++pos;

        int64_t len = 0;
        st = parse_int_crlf(s, pos, len);
        if (st != frame_parse_status::ok || len < 0) {
            fail("invalid bulk length");
        }

        static constexpr int64_t MAX_BULK = 64 * 1024 * 1024;
        if (len > MAX_BULK) {
            fail("bulk string too large");
        }

        const size_t need = pos + static_cast<size_t>(len) + 2;
        if (need > s.size() || s[pos + static_cast<size_t>(len)] != '\r' ||
            s[pos + static_cast<size_t>(len) + 1] != '\n') {
            fail("invalid bulk payload");
        }

        argv.emplace_back(s.substr(pos, static_cast<size_t>(len)));
        pos = need;
    }

    co_return argv;
}

void Reader::compact_if_needed() {
    if (_off == 0) {
        return;
    }
    if (_off >= _buf.size()) {
        _buf = {};
        _off = 0;
        return;
    }
    if (_off >= 4096 || _off * 2 >= _buf.size()) {
        _buf = _buf.substr(_off);
        _off = 0;
    }
}

future<> Reader::ensure(seastar::input_stream<char> &in, size_t n) {
    while (size() < n) {
        auto chunk = co_await in.read();
        if (chunk.empty()) {
            co_return;
        }
        compact_if_needed();
        _buf.append(chunk.get(), chunk.size());
    }
}

future<char> Reader::read_byte(seastar::input_stream<char> &in) {
    co_await ensure(in, 1);
    if (size() == 0) {
        co_return '\0';
    }
    const char c = _buf[_off];
    ++_off;
    compact_if_needed();
    co_return c;
}

future<sstring> Reader::read_line_crlf(seastar::input_stream<char> &in) {
    while (true) {
        const auto sv = view();
        const auto pos = sv.find("\r\n");
        if (pos != std::string_view::npos) {
            sstring line(sv.substr(0, pos));
            _off += pos + 2;
            compact_if_needed();
            co_return line;
        }

        auto chunk = co_await in.read();
        if (chunk.empty()) {
            if (size() == 0) {
                co_return sstring{};
            }
            fail("unexpected EOF while reading line");
        }
        compact_if_needed();
        _buf.append(chunk.get(), chunk.size());
    }
}

future<int64_t> Reader::read_int_line(seastar::input_stream<char> &in) {
    auto line = co_await read_line_crlf(in);
    if (line.empty()) {
        co_return 0;
    }
    int64_t n = 0;
    const auto [ptr, ec] =
        std::from_chars(line.data(), line.data() + line.size(), n);
    if (ec != std::errc{} || ptr != line.data() + line.size()) {
        fail("invalid integer");
    }
    co_return n;
}

future<sstring> Reader::read_bulk(seastar::input_stream<char> &in) {
    auto len_line = co_await read_line_crlf(in);
    if (len_line.empty()) {
        fail("unexpected EOF reading bulk length");
    }

    int64_t len = 0;
    const auto [ptr, ec] =
        std::from_chars(len_line.data(), len_line.data() + len_line.size(), len);
    if (ec != std::errc{} || ptr != len_line.data() + len_line.size()) {
        fail("invalid bulk length");
    }

    if (len == -1) {
        co_return sstring{};
    }
    if (len < 0) {
        fail("negative bulk length");
    }

    static constexpr int64_t MAX_BULK = 64 * 1024 * 1024;
    if (len > MAX_BULK) {
        fail("bulk string too large");
    }

    co_await ensure(in, size_t(len) + 2);
    if (size() < size_t(len) + 2) {
        fail("unexpected EOF reading bulk payload");
    }

    auto sv = view();
    sstring data(sv.substr(0, size_t(len)));

    if (sv[size_t(len)] != '\r' || sv[size_t(len) + 1] != '\n') {
        fail("missing CRLF after bulk payload");
    }

    _off += size_t(len) + 2;
    compact_if_needed();

    co_return data;
}

future<Array> Reader::read_command(seastar::input_stream<char> &in) {
    char t = co_await read_byte(in);
    if (t == '\0') {
        co_return Array{};
    }
    if (t != '*') {
        fail("expected array '*'");
    }

    const int64_t n = co_await read_int_line(in);
    if (n <= 0) {
        fail("array length must be positive");
    }
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
