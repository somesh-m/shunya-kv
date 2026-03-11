#include "conn/socket_handler.hh"
#include <seastar/core/sleep.hh>
#include <seastar/core/when_all.hh>
#include <seastar/testing/test_case.hh>
#include <seastar/util/memory-data-sink.hh>
#include <seastar/util/memory-data-source.hh>
using namespace seastar;
using namespace shunyakv;

namespace shunyakv {
using OutBuf = std::vector<seastar::temporary_buffer<char>>;
using OutputStream = seastar::output_stream<char>;
} // namespace shunyakv

struct TestOut {
    OutBuf bufs;
    OutputStream out;

    TestOut()
        : bufs(),
          out(seastar::data_sink(
              std::make_unique<seastar::util::memory_data_sink>(bufs))) {}
};

static inline TestOut make_out() { return {}; }

static inline seastar::sstring bufs_to_sstring(OutBuf &bufs) {

    seastar::sstring s;
    for (auto &b : bufs) {
        s.append(b.get(), b.size());
    }
    return s;
}

static inline seastar::input_stream<char> make_in(seastar::sstring data) {
    return seastar::util::as_input_stream(
        seastar::temporary_buffer<char>(data.data(), data.size()));
}

class TestPipelineHandler final : public PipelinedSocketHandler {
  public:
    using PipelinedSocketHandler::read_loop_impl;
    using PipelinedSocketHandler::write_loop_impl;

  protected:
    seastar::future<seastar::sstring>
    handle_request(shunyakv::ParsedRequest req) override {
        if (req.frame == "slow") {
            return seastar::sleep(std::chrono::milliseconds(30)).then([] {
                return seastar::sstring("SLOW\r\n");
            });
        }
        if (req.frame == "fast") {
            return seastar::make_ready_future<seastar::sstring>("FAST\r\n");
        }
        return seastar::make_ready_future<seastar::sstring>(req.frame + "\r\n");
    }
};

class BackpressurePipelineHandler final : public PipelinedSocketHandler {
  public:
    using PipelinedSocketHandler::read_loop_impl;
    using PipelinedSocketHandler::write_loop_impl;

    size_t handled_count() const { return _frames.size(); }

    void release_all_available() {
        while (_released < _promises.size()) {
            auto &p = _promises[_released];
            p.set_value(_frames[_released] + "\r\n");
            ++_released;
        }
    }

    /**
     * semaphore in the read loop impl should stop calls to handle request if
     * slots are not acquired.
     */
  protected:
    seastar::future<seastar::sstring>
    handle_request(shunyakv::ParsedRequest req) override {
        _frames.push_back(req.frame);
        _promises.emplace_back();
        return _promises.back().get_future();
    }

  private:
    std::vector<seastar::sstring> _frames;
    std::vector<seastar::promise<seastar::sstring>> _promises;
    size_t _released{0};
};

SEASTAR_TEST_CASE(socket_handler_pipelining_semantics) {
    TestPipelineHandler handler;
    auto in = make_in("slow\nfast\n");
    auto out = make_out();

    co_await seastar::when_all(handler.read_loop_impl(in),
                               handler.write_loop_impl(out.out));

    BOOST_REQUIRE_EQUAL(bufs_to_sstring(out.bufs), "SLOW\r\nFAST\r\n");
    co_return;
}

SEASTAR_TEST_CASE(socket_handler_backpressure_test) {
    BackpressurePipelineHandler handler;
    constexpr uint32_t total_frames = 1050;

    seastar::sstring input;
    seastar::sstring expected;
    for (uint32_t i = 0; i < total_frames; ++i) {
        auto frame = seastar::format("frame_{}", i);
        input += frame + "\n";
        expected += frame + "\r\n";
    }

    auto in = make_in(input);
    auto out = make_out();

    auto read_fut = handler.read_loop_impl(in);
    co_await seastar::sleep(std::chrono::milliseconds(20));
    BOOST_REQUIRE_EQUAL(handler.handled_count(), 1024);

    auto write_fut = handler.write_loop_impl(out.out);
    while (handler.handled_count() < total_frames) {
        handler.release_all_available();
        co_await seastar::sleep(std::chrono::milliseconds(1));
    }
    handler.release_all_available();

    co_await seastar::when_all(std::move(read_fut), std::move(write_fut));
    BOOST_REQUIRE_EQUAL(bufs_to_sstring(out.bufs), expected);
    co_return;
}

class SlowPipelineHandler final : public PipelinedSocketHandler {
  public:
    using PipelinedSocketHandler::read_loop_impl;
    using PipelinedSocketHandler::write_loop_impl;

    size_t get_input_count() const { return input_length_; }

  protected:
    seastar::future<seastar::sstring>
    handle_request(shunyakv::ParsedRequest req) override {
        input_length_++;
        return seastar::make_ready_future<seastar::sstring>(req.frame + "\r\n");
    }

  private:
    size_t input_length_{0};
};

SEASTAR_TEST_CASE(socket_handler_pending_queue) {
    SlowPipelineHandler handler;
    // Make around 1000 reqs
    std::string req;
    for (uint32_t i = 0; i < 1000; i++) {
        req += "frame_" + std::to_string(i) + "\r\n";
    }
    auto in = make_in(req);
    auto out = make_out();

    co_await handler.read_loop_impl(in);
    // Make sure all the requests are inserted into the loop
    BOOST_REQUIRE_EQUAL(handler.get_input_count(), 1000);

    co_await handler.write_loop_impl(out.out);
    BOOST_REQUIRE_EQUAL(bufs_to_sstring(out.bufs), req);

    co_return;
}

class StuckPipelineHandler final : public PipelinedSocketHandler {
  public:
    using PipelinedSocketHandler::read_loop_impl;
    using PipelinedSocketHandler::write_loop_impl;

    size_t get_input_count() const { return input_length_; }

  protected:
    seastar::future<seastar::sstring>
    handle_request(shunyakv::ParsedRequest req) override {
        ++input_length_;
        if (!_stuck_emitted && input_length_ % 20 == 0) {
            // Return a never-resolved future to trigger write timeout path.
            _stuck_emitted = true;
            auto *p = new seastar::promise<seastar::sstring>();
            return p->get_future();
        }
        return seastar::make_ready_future<seastar::sstring>(req.frame +
                                                            "\r\n");
    }

  private:
    size_t input_length_{0};
    bool _stuck_emitted{false};
};

SEASTAR_TEST_CASE(socket_handler_stuck_handler) {
    StuckPipelineHandler handler;
    // Send enough requests so one sticks and forces write timeout/drop.
    std::string req;
    for (uint32_t i = 0; i < 1000; i++) {
        req += "frame_" + std::to_string(i) + "\r\n";
    }
    auto in = make_in(req);
    auto out = make_out();

    co_await seastar::when_all(handler.read_loop_impl(in),
                               handler.write_loop_impl(out.out));
    BOOST_REQUIRE_GE(handler.get_input_count(), 20);
    // write_loop_impl flushes only when queue is empty. On stuck timeout path
    // it can drop before final flush, so buffered output may be empty.
    BOOST_REQUIRE_EQUAL(bufs_to_sstring(out.bufs), "");
    co_await out.out.close();

    co_return;
}
