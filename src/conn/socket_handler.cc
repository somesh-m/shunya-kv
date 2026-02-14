#include "conn/socket_handler.hh"

#include "conn/connections.hh"

#include <utility>

#include <seastar/core/when_all.hh>

std::optional<seastar::sstring>
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
    return req;
}

seastar::future<> PipelinedSocketHandler::read_loop(shunyakv::connection &c) {
    auto &in = c.in();
    seastar::sstring buffer;

    try {
        while (true) {
            auto chunk = co_await in.read();
            if (!chunk) {
                break;
            }

            buffer.append(chunk.get(), chunk.size());

            while (true) {
                auto req_opt = try_extract_request(buffer);
                if (!req_opt) {
                    break;
                }

                co_await _slots.wait(1);
                _respq.push_back(handle_request(std::move(*req_opt)));
                _cv.signal();
            }
        }
    } catch (...) {
    }

    _eof = true;
    _cv.signal();
}

seastar::future<> PipelinedSocketHandler::write_loop(shunyakv::connection &c) {
    auto &out = c.out();

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
                resp = co_await std::move(fut);
            } catch (...) {
                co_return;
            }

            co_await out.write(resp);
            if (_respq.empty()) {
                co_await out.flush();
            }
            _slots.signal(1);
        }
    } catch (...) {
        co_return;
    }
}

seastar::future<> PipelinedSocketHandler::process(shunyakv::connection &c) {
    co_await seastar::when_all(read_loop(c), write_loop(c));
}
