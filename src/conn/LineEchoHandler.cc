#include "conn/socket_handler.hh"

#include <seastar/core/future.hh>
#include <seastar/core/sstring.hh>

class LineEchoHandler final : public PipelinedSocketHandler {
  protected:
    seastar::future<seastar::sstring>
    handle_request(seastar::sstring req) override {
        co_return req + "\n";
    }
};
