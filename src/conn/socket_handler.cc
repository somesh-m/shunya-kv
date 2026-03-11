#include "conn/socket_handler.hh"
#include "cmd_node_info.hh"
#include "conn/connections.hh"
#include "hotpath_metrics.hh"
#include <chrono>
#include <seastar/core/coroutine.hh>
#include <seastar/core/future-util.hh>
#include <seastar/core/when_all.hh>
#include <seastar/util/log.hh>
#include <utility>

static seastar::logger socket_logger{"socket_logger"};

namespace shunyakv {
thread_local bool g_send_shard_details_on_connect = false;

void set_send_shard_details_on_connect(bool enabled) noexcept {
    g_send_shard_details_on_connect = enabled;
}
} // namespace shunyakv

static seastar::logger socket_handler_log{"socket_handler"};

static seastar::future<> send_shard_details(shunyakv::connection &c) {
    auto &out = c.out();
    seastar::sstring json =
        shunyakv::compute_hash(static_cast<uint16_t>(seastar::this_shard_id()));
    auto resp = seastar::format("${}\r\n{}\r\n", json.size(), json);

    co_await out.write(resp);
    co_await out.flush();
}

std::optional<shunyakv::ParsedRequest>
PipelinedSocketHandler::try_extract_request(seastar::sstring &buf) {
    auto pos = buf.find('\n');
    if (pos == seastar::sstring::npos) {
        return std::nullopt;
    }

    seastar::sstring req = buf.substr(0, pos);
    buf.erase(buf.begin(), buf.begin() + pos + 1);
    if (!req.empty() && req.back() == '\r') {
        req = req.substr(0, req.size() - 1);
    }
    return shunyakv::ParsedRequest{std::move(req), {}};
}

seastar::future<>
PipelinedSocketHandler::read_loop_impl(seastar::input_stream<char> &in) {
    seastar::sstring buffer;

    try {
        while (true) {
            auto chunk = co_await in.read();
            if (!chunk) {
                break;
            }

            buffer.append(chunk.get(), chunk.size());
            size_t drained = 0;

            while (true) {
                HOTPATHLOGS(socket_logger.info(
                    "trying to extract request = {} ", buffer));
                auto req_opt = try_extract_request(buffer);
                if (!req_opt)
                    break;

                co_await _slots.wait(1);
                _respq.push_back(handle_request(std::move(*req_opt)));
                _cv.signal();

                if ((++drained & 0xFF) == 0) { // every 256
                    co_await seastar::maybe_yield();
                }
            }
        }
    } catch (...) {
    }

    _eof = true;
    _cv.signal();
}

seastar::future<> PipelinedSocketHandler::read_loop(shunyakv::connection &c) {
    auto &in = c.in();
    return read_loop_impl(in);
}

seastar::future<>
PipelinedSocketHandler::write_loop_impl(seastar::output_stream<char> &out) {
    try {
        while (true) {
            while (_respq.empty()) {
                if (_eof) {
                    co_return;
                }
                co_await _cv.wait();
            }

            auto fut = std::move(_respq.front());
            _respq.pop_front();

            seastar::sstring resp;
            try {
                const auto deadline =
                    seastar::lowres_clock::now() + std::chrono::seconds(2);
                resp = co_await seastar::with_timeout(deadline, std::move(fut));
            } catch (...) {
                socket_handler_log.warn("dropping connection on shard {} due "
                                        "to stuck request future",
                                        seastar::this_shard_id());
                _eof = true;
                _cv.broadcast();
                // Unblock read_loop if it is waiting for an inflight slot.
                _slots.signal(max_inflight);
                co_return;
            }

            co_await out.write(resp);
            if (_respq.empty()) {
                co_await out.flush();
            }
            _slots.signal(1);
        }
    } catch (...) {
        _eof = true;
        _cv.broadcast();
        // Unblock read_loop if it is waiting for an inflight slot.
        _slots.signal(max_inflight);
        co_return;
    }
}
seastar::future<> PipelinedSocketHandler::write_loop(shunyakv::connection &c) {
    auto &out = c.out();
    /**
     * write_loop_impl is unit testable as it has no dependency on connection
     */
    return write_loop_impl(out);
}

seastar::future<> PipelinedSocketHandler::process(shunyakv::connection &c) {
    std::exception_ptr ep;
    if (shunyakv::g_send_shard_details_on_connect) {
        try {
            co_await send_shard_details(c);
        } catch (...) {
            ep = std::current_exception();
        }
    }
    if (!ep) {
        try {
            co_await seastar::when_all(read_loop(c), write_loop(c));
        } catch (...) {
            ep = std::current_exception();
        }
    }

    co_await c.out().close().handle_exception([](std::exception_ptr) {});
    co_await c.in().close().handle_exception([](std::exception_ptr) {});

    if (ep) {
        std::rethrow_exception(ep);
    }
}
