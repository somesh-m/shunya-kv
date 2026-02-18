#include "conn/resp_handler.hh"

#include "commands.hh"
#include "resp/resp_parser.hh"
#include "resp/resp_writer.hh"
#include "router.hh"

#include <algorithm>
#include <string_view>
#include <vector>
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
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[i]);
        if ('a' <= ca && ca <= 'z') {
            ca = static_cast<unsigned char>(ca - 'a' + 'A');
        }
        if ('a' <= cb && cb <= 'z') {
            cb = static_cast<unsigned char>(cb - 'a' + 'A');
        }
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

inline void compact_if_needed(seastar::sstring &buf, size_t &off) {
    if (off == 0) {
        return;
    }
    if (off >= buf.size()) {
        buf = {};
        off = 0;
        return;
    }
    if (off >= 4096 || off * 2 >= buf.size()) {
        buf = buf.substr(off);
        off = 0;
    }
}

} // namespace

std::optional<seastar::sstring>
RespHandler::try_extract_request(seastar::sstring &buf) {
    if (_off >= buf.size()) {
        buf = {};
        _off = 0;
        return std::nullopt;
    }
    if (buf.empty()) {
        return std::nullopt;
    }

    std::string_view view(buf.data() + _off, buf.size() - _off);
    size_t frame_len = 0;
    switch (resp::parse_frame_length(view, frame_len)) {
    case resp::frame_parse_status::ok: {
        auto frame = buf.substr(_off, frame_len);
        _off += frame_len;
        compact_if_needed(buf, _off);
        return frame;
    }
    case resp::frame_parse_status::need_more:
        compact_if_needed(buf, _off);
        return std::nullopt;
    case resp::frame_parse_status::invalid: {
        // Consume up to line-end so bad input does not stall the connection.
        auto nl = view.find('\n');
        if (nl == seastar::sstring::npos) {
            auto bad = buf.substr(_off);
            _off = buf.size();
            compact_if_needed(buf, _off);
            return bad;
        }
        auto bad = buf.substr(_off, nl + 1);
        _off += nl + 1;
        compact_if_needed(buf, _off);
        return bad;
    }
    }
    return std::nullopt;
}

seastar::future<seastar::sstring> RespHandler::handle_request(seastar::sstring req) {
    try {
        auto cmd = co_await resp::parse_command_from_frame(req);
        if (cmd.empty()) {
            co_return seastar::sstring("-ERR empty command\r\n");
        }

        const auto &table = shunyakv::command_dispatch();
        const auto it = std::find_if(
            table.begin(), table.end(), [&](const auto &entry) {
                return ascii_ieq(cmd[0], entry.first);
            });
        if (it == table.end()) {
            co_return seastar::sstring("-ERR unknown command\r\n");
        }

        std::vector<seastar::temporary_buffer<char>> bufs;
        seastar::output_stream<char> out(seastar::data_sink(
            std::make_unique<seastar::util::memory_data_sink>(bufs)));

        co_await it->second(cmd, out, shunyakv::local_service());
        co_await out.flush();
        co_return buffer_vector_to_sstring(bufs);
    } catch (const std::exception &ex) {
        co_return seastar::sstring("-ERR ") + seastar::sstring(ex.what()) + "\r\n";
    } catch (...) {
        co_return seastar::sstring("-ERR internal error\r\n");
    }
}
