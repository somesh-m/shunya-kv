#pragma once

#include <optional>
#include <resp/resp_types.hh>
#include <string_view>
#include <vector>

#include <seastar/core/circular_buffer.hh>
#include <seastar/core/condition-variable.hh>
#include <seastar/core/future.hh>
#include <seastar/core/semaphore.hh>
#include <seastar/core/sstring.hh>

namespace shunyakv {
struct ParsedRequest {
    seastar::sstring frame; // owns bytes (optional)
    resp::Array argv;
};
class connection;
void set_send_shard_details_on_connect(bool enabled) noexcept;
} // namespace shunyakv

class SocketHandler {
  public:
    virtual ~SocketHandler() = default;
    virtual seastar::future<> process(shunyakv::connection &c) = 0;
};

class PipelinedSocketHandler : public SocketHandler {
  protected:
    seastar::circular_buffer<seastar::future<seastar::sstring>> _respq;
    seastar::condition_variable _cv;
    bool _eof{false};
    static constexpr size_t max_inflight = 1024;
    seastar::semaphore _slots{max_inflight};

    seastar::future<> read_loop(shunyakv::connection &c);
    seastar::future<> write_loop(shunyakv::connection &c);

    // ---- hooks for derived classes ----
    // Parse & schedule a response for ONE complete message (line/resp
    // frame/etc.)
    virtual seastar::future<seastar::sstring>
    handle_request(shunyakv::ParsedRequest req) = 0;

    virtual std::optional<shunyakv::ParsedRequest>
    try_extract_request(seastar::sstring &buf) = 0;

  public:
    seastar::future<> process(shunyakv::connection &c) override;
};
