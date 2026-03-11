#include "resp/resp_stream_processor.hh"

#include "commands.hh"
#include "resp/resp_writer.hh"
#include "router.hh"

#include <algorithm>
#include <cstring>
#include <seastar/util/memory-data-sink.hh>

namespace {

seastar::sstring
buffer_vector_to_sstring(std::vector<seastar::temporary_buffer<char>> &bufs) {
    seastar::sstring out;
    for (auto &b : bufs) {
        out.append(b.get(), b.size());
    }
    return out;
}

bool ascii_ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if ('a' <= ca && ca <= 'z') {
            ca = (unsigned char)(ca - 'a' + 'A');
        }
        if ('a' <= cb && cb <= 'z') {
            cb = (unsigned char)(cb - 'a' + 'A');
        }
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

} // namespace

void RespStreamProcessor::compact_if_needed(seastar::sstring &buf, size_t &pos) {
    if (pos == 0) {
        return;
    }
    if (pos >= buf.size()) {
        buf = {};
        pos = 0;
        return;
    }
    if (pos >= 4096 || pos * 2 >= buf.size()) {
        buf = buf.substr(pos);
        pos = 0;
    }
}

void RespStreamProcessor::reset_and_resync(seastar::sstring &buf) {
    _st = st::need_star;

    if (_pos >= buf.size()) {
        compact_if_needed(buf, _pos);
        return;
    }

    auto view = std::string_view(buf.data() + _pos, buf.size() - _pos);
    auto star = view.find('*');
    if (star == std::string_view::npos) {
        _pos = buf.size();
    } else {
        _pos += star;
    }
    compact_if_needed(buf, _pos);
}

seastar::future<seastar::sstring>
RespStreamProcessor::handle_request(shunyakv::ParsedRequest req) {
    try {
        if (req.argv.empty() && !req.frame.empty() && req.frame[0] == '-') {
            co_return std::move(req.frame);
        }
        if (req.argv.empty()) {
            co_return seastar::sstring("-ERR empty command\r\n");
        }

        resp::ArgvView cmd;
        cmd.reserve(req.argv.size());
        for (const auto &arg : req.argv) {
            cmd.emplace_back(arg.data(), arg.size());
        }

        const auto &table = shunyakv::command_dispatch();
        const auto it =
            std::find_if(table.begin(), table.end(), [&](const auto &entry) {
                return ascii_ieq(cmd[0], entry.first);
            });

        if (it == table.end()) {
            co_return seastar::sstring("-ERR unknown command\r\n");
        }

        std::vector<seastar::temporary_buffer<char>> bufs;
        seastar::output_stream<char> out(seastar::data_sink(
            std::make_unique<seastar::util::memory_data_sink>(bufs)));
        co_await it->second(cmd, out, shunyakv::local_service())
            .finally([&out] {
                return out.close().handle_exception([](std::exception_ptr) {});
            });
        co_return buffer_vector_to_sstring(bufs);
    } catch (const std::exception &ex) {
        co_return seastar::sstring("-ERR ") + seastar::sstring(ex.what()) +
            "\r\n";
    } catch (...) {
        co_return seastar::sstring("-ERR internal error\r\n");
    }
}

resp::frame_parse_status
RespStreamProcessor::parse_int_crlf(const seastar::sstring &buf, int64_t &out) {
    const char *base = buf.data();
    const size_t n = buf.size();

    if (_pos >= n) {
        return resp::frame_parse_status::need_more;
    }

    const char *p = base + _pos;
    const char *end = base + n;
    const void *nl_ptr = memchr(p, '\n', (size_t)(end - p));
    if (!nl_ptr) {
        return resp::frame_parse_status::need_more;
    }

    const char *nl = (const char *)nl_ptr;
    if (nl == p) {
        return resp::frame_parse_status::invalid;
    }
    if (*(nl - 1) != '\r') {
        return resp::frame_parse_status::invalid;
    }

    const char *q = p;
    bool neg = false;
    if (*q == '-') {
        neg = true;
        ++q;
    }
    if (q == nl - 1) {
        return resp::frame_parse_status::invalid;
    }

    int64_t v = 0;
    for (; q < nl - 1; ++q) {
        const unsigned d = (unsigned)(*q - '0');
        if (d > 9) {
            return resp::frame_parse_status::invalid;
        }
        v = v * 10 + (int64_t)d;
    }
    out = neg ? -v : v;
    _pos = (size_t)((nl - base) + 1);
    return resp::frame_parse_status::ok;
}

std::optional<shunyakv::ParsedRequest>
RespStreamProcessor::try_extract_request(seastar::sstring &buf) {
    const size_t n = buf.size();
    if (_pos > n) {
        _pos = n;
    }

    while (true) {
        const size_t n_local = buf.size();

        switch (_st) {
        case st::need_star: {
            if (_pos >= n_local) {
                compact_if_needed(buf, _pos);
                return std::nullopt;
            }
            if (buf[_pos] != '*') {
                auto view = std::string_view(buf.data() + _pos, n_local - _pos);
                auto nl = view.find('\n');
                if (nl == std::string_view::npos) {
                    _pos = n_local;
                } else {
                    _pos += nl + 1;
                }
                compact_if_needed(buf, _pos);
                return shunyakv::ParsedRequest{
                    seastar::sstring("-ERR protocol error\r\n"), {}};
            }
            _cmd_start = _pos;
            ++_pos;
            _st = st::need_argc;
            break;
        }
        case st::need_argc: {
            int64_t argc = 0;
            auto stt = parse_int_crlf(buf, argc);
            if (stt != resp::frame_parse_status::ok) {
                if (stt == resp::frame_parse_status::need_more) {
                    return std::nullopt;
                }
                reset_and_resync(buf);
                return shunyakv::ParsedRequest{
                    seastar::sstring("-ERR protocol error\r\n"), {}};
            }
            if (argc <= 0 || (size_t)argc > kMaxArgs) {
                reset_and_resync(buf);
                return shunyakv::ParsedRequest{
                    seastar::sstring("-ERR invalid argc\r\n"), {}};
            }
            _argc = argc;
            _argi = 0;
            _slices.clear();
            _slices.reserve((size_t)argc);
            _st = st::need_dollar;
            break;
        }
        case st::need_dollar: {
            if (_pos >= n_local) {
                return std::nullopt;
            }
            if (buf[_pos] != '$') {
                reset_and_resync(buf);
                return shunyakv::ParsedRequest{
                    seastar::sstring("-ERR protocol error\r\n"), {}};
            }
            ++_pos;
            _st = st::need_bulk_len;
            break;
        }
        case st::need_bulk_len: {
            int64_t bl = 0;
            auto stt = parse_int_crlf(buf, bl);
            if (stt != resp::frame_parse_status::ok) {
                if (stt == resp::frame_parse_status::need_more) {
                    return std::nullopt;
                }
                reset_and_resync(buf);
                return shunyakv::ParsedRequest{
                    seastar::sstring("-ERR protocol error\r\n"), {}};
            }
            if (bl < 0 || (size_t)bl > kMaxBulk) {
                reset_and_resync(buf);
                return shunyakv::ParsedRequest{
                    seastar::sstring("-ERR invalid bulk length\r\n"), {}};
            }
            _bulk_len = bl;
            _st = st::need_bulk_data;
            break;
        }
        case st::need_bulk_data: {
            const size_t bl = (size_t)_bulk_len;
            if (_pos + bl > n_local) {
                return std::nullopt;
            }
            _slices.push_back(slice{(uint32_t)_pos, (uint32_t)bl});
            _pos += bl;
            _st = st::need_bulk_crlf;
            break;
        }
        case st::need_bulk_crlf: {
            if (_pos + 2 > n_local) {
                return std::nullopt;
            }
            if (buf[_pos] != '\r' || buf[_pos + 1] != '\n') {
                reset_and_resync(buf);
                return shunyakv::ParsedRequest{
                    seastar::sstring("-ERR protocol error\r\n"), {}};
            }
            _pos += 2;
            ++_argi;

            if (_argi == _argc) {
                const size_t frame_len = _pos - _cmd_start;
                shunyakv::ParsedRequest out;
                out.frame = buf.substr(_cmd_start, frame_len);
                out.argv.reserve(_slices.size());
                for (const auto &sl : _slices) {
                    const size_t rel_off = (size_t)sl.off - _cmd_start;
                    out.argv.emplace_back(
                        out.frame.substr(rel_off, (size_t)sl.len));
                }

                _st = st::need_star;
                compact_if_needed(buf, _pos);
                return out;
            }

            _st = st::need_dollar;
            break;
        }
        }
    }
}
