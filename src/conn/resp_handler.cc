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

} // namespace

std::optional<seastar::sstring>
RespHandler::try_extract_request(seastar::sstring &buf) {
    if (buf.empty()) {
        return std::nullopt;
    }

    size_t frame_len = 0;
    switch (resp::parse_frame_length(std::string_view(buf.data(), buf.size()),
                                     frame_len)) {
    case resp::frame_parse_status::ok: {
        auto frame = buf.substr(0, frame_len);
        buf.erase(buf.begin(), buf.begin() + frame_len);
        return frame;
    }
    case resp::frame_parse_status::need_more:
        return std::nullopt;
    case resp::frame_parse_status::invalid: {
        // Consume up to line-end so bad input does not stall the connection.
        auto nl = buf.find('\n');
        if (nl == seastar::sstring::npos) {
            auto bad = std::move(buf);
            buf = {};
            return bad;
        }
        auto bad = buf.substr(0, nl + 1);
        buf.erase(buf.begin(), buf.begin() + nl + 1);
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
        co_await out.close();
        co_return buffer_vector_to_sstring(bufs);
    } catch (const std::exception &ex) {
        co_return seastar::sstring("-ERR ") + seastar::sstring(ex.what()) + "\r\n";
    } catch (...) {
        co_return seastar::sstring("-ERR internal error\r\n");
    }
}
