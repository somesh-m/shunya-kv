#include "conn/resp_handler.hh"

seastar::future<seastar::sstring>
RespHandler::handle_request(shunyakv::ParsedRequest req) {
    return _processor.handle_request(std::move(req));
}

std::optional<shunyakv::ParsedRequest>
RespHandler::try_extract_request(seastar::sstring &buf) {
    return _processor.try_extract_request(buf);
}
