#pragma once

#include "socket_handler.hh"

class LineEchoHandler final : public PipelinedSocketHandler {
  protected:
    seastar::future<seastar::sstring>
    handle_request(shunyakv::ParsedRequest req) override {
        return seastar::make_ready_future<seastar::sstring>(req.frame + "\n");
    }
};
