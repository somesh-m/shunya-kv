#pragma once

#include "socket_handler.hh"

class RespHandler final : public PipelinedSocketHandler {
  protected:
    seastar::future<seastar::sstring>
    handle_request(seastar::sstring req) override;

    std::optional<seastar::sstring>
    try_extract_request(seastar::sstring &buf) override;
};
