#include "conn/socket_handler.hh"

#include <seastar/core/future.hh>
#include <seastar/core/sstring.hh>

class LineEchoHandler final : public PipelinedSocketHandler {
  protected:
    seastar::future<seastar::sstring>
    handle_request(shunyakv::ParsedRequest req) override {
        co_return req.frame + "\n";
    }
};
