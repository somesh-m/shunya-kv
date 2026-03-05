#include "conn/socket_handler.hh"
#include "resp/resp_stream_processor.hh"
#include "resp/resp_types.hh"
#undef SEASTAR_TESTING_MAIN
#include <seastar/testing/test_case.hh>

seastar::future<seastar::sstring> handle(shunyakv::ParsedRequest req,
                                         RespStreamProcessor parser) {
    seastar::sstring command = co_await parser.handle_request(req);

    co_return command;
}

SEASTAR_TEST_CASE(resp_request_handler_empty_command) {
    RespStreamProcessor parser = RespStreamProcessor();

    // Mock empty command ParsedRequest
    shunyakv::ParsedRequest req{};
    seastar::sstring cmd = co_await handle(req, parser);
    BOOST_REQUIRE_EQUAL(cmd, "-ERR empty command\r\n");
    co_return;
}

SEASTAR_TEST_CASE(resp_request_handler_error_command) {
    RespStreamProcessor parser = RespStreamProcessor();

    // Mock empty command ParsedRequest
    shunyakv::ParsedRequest req{};
    req.frame = "-ERR protocol error\r\n";
    seastar::sstring cmd = co_await handle(req, parser);
    BOOST_REQUIRE_EQUAL(cmd, "-ERR protocol error\r\n");
    co_return;
}

SEASTAR_TEST_CASE(resp_request_handler_invalid_command) {
    RespStreamProcessor parser = RespStreamProcessor();

    // Mock empty command ParsedRequest
    shunyakv::ParsedRequest req{};
    req.frame = "*2\r\n$3\r\nYUP\r\n$4\r\nNAME\r\n";
    req.argv = {"YUP", "NAME"};
    seastar::sstring cmd = co_await handle(req, parser);
    BOOST_REQUIRE_EQUAL(cmd, "-ERR unknown command\r\n");
    co_return;
}
