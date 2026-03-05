#pragma once
#include "resp/resp_stream_processor.hh"
#include "socket_handler.hh"

class RespHandler final : public PipelinedSocketHandler {
  private:
    RespStreamProcessor _processor;

  protected:
    seastar::future<seastar::sstring>
    handle_request(shunyakv::ParsedRequest req) override;

    std::optional<shunyakv::ParsedRequest>
    try_extract_request(seastar::sstring &buf) override;
};
