#include "conn/socket_handler.hh"
#include "resp/resp_stream_processor.hh"
#include "resp/resp_types.hh"
#include <seastar/testing/test_case.hh>

std::optional<shunyakv::ParsedRequest> extract(std::string_view chunk,
                                               seastar::sstring &buf,
                                               RespStreamProcessor &parser) {
    buf.append(chunk.data(), chunk.size());
    return parser.try_extract_request(buf);
}

SEASTAR_TEST_CASE(resp_parser_set_command_incremental) {
    RespStreamProcessor parser = RespStreamProcessor();
    seastar::sstring buf;

    std::string_view chunk = "*3\r\n";
    auto req = extract(chunk, buf, parser);
    BOOST_REQUIRE(req == std::nullopt);

    chunk = "$3\r\nSET\r\n";
    req = extract(chunk, buf, parser);
    BOOST_REQUIRE(req == std::nullopt);

    chunk = "$4\r\nNAME\r\n";
    req = extract(chunk, buf, parser);
    BOOST_REQUIRE(req == std::nullopt);

    chunk = "$5\r\nTIGER\r\n";
    req = extract(chunk, buf, parser);
    BOOST_REQUIRE(req.has_value());
    BOOST_REQUIRE(req->argv.size() == 3);
    BOOST_REQUIRE(req->argv[0] == "SET");
    BOOST_REQUIRE(req->argv[1] == "NAME");
    BOOST_REQUIRE(req->argv[2] == "TIGER");

    co_return;
}

SEASTAR_TEST_CASE(resp_processor_get_command_incremental) {
    RespStreamProcessor parser = RespStreamProcessor();
    seastar::sstring buf;
    std::string_view chunk;

    chunk = "*2\r\n";
    auto req = extract(chunk, buf, parser);
    BOOST_REQUIRE(req == std::nullopt);

    chunk = "$3\r\nGET\r\n";
    req = extract(chunk, buf, parser);
    BOOST_REQUIRE(req == std::nullopt);

    chunk = "$4\r\nNAME\r\n";
    req = extract(chunk, buf, parser);
    BOOST_REQUIRE(req.has_value());
    BOOST_REQUIRE(req->argv.size() == 2);
    BOOST_REQUIRE(req->argv[0] == "GET");
    BOOST_REQUIRE(req->argv[1] == "NAME");

    co_return;
}

SEASTAR_TEST_CASE(resp_processor_set_command) {
    RespStreamProcessor parser = RespStreamProcessor();
    seastar::sstring buf = "*3\r\n$3\r\nSET\r\n$4\r\nNAME\r\n$5\r\nTIGER\r\n";
    auto req = parser.try_extract_request(buf);
    BOOST_REQUIRE(req.has_value());
    BOOST_REQUIRE(req->argv.size() == 3);
    BOOST_REQUIRE(req->argv[0] == "SET");
    BOOST_REQUIRE(req->argv[1] == "NAME");
    BOOST_REQUIRE(req->argv[2] == "TIGER");

    co_return;
}

SEASTAR_TEST_CASE(resp_processor_get_command) {
    RespStreamProcessor parser = RespStreamProcessor();
    seastar::sstring buf = "*2\r\n$3\r\nGET\r\n$4\r\nNAME\r\n";
    auto req = parser.try_extract_request(buf);
    BOOST_REQUIRE(req.has_value());
    BOOST_REQUIRE(req->argv.size() == 2);
    BOOST_REQUIRE(req->argv[0] == "GET");
    BOOST_REQUIRE(req->argv[1] == "NAME");

    co_return;
}

SEASTAR_TEST_CASE(resp_processor_invalid_command_start_sequence) {
    RespStreamProcessor parser = RespStreamProcessor();
    seastar::sstring buf = "+2\r\n$3\r\nGET\r\n$4\r\nNAME\r\n";
    auto req = parser.try_extract_request(buf);
    BOOST_REQUIRE(req.has_value());
    BOOST_REQUIRE(req->argv.size() == 0);
    BOOST_REQUIRE(req->frame == "-ERR protocol error\r\n");

    co_return;
}

SEASTAR_TEST_CASE(resp_processor_invalid_arg_count_overflow) {
    RespStreamProcessor parser = RespStreamProcessor();
    seastar::sstring buf = "*5\r\n$3\r\nGET\r\n$4\r\nNAME\r\n";
    auto req = parser.try_extract_request(buf);
    BOOST_REQUIRE(!req.has_value());
    co_return;
}

SEASTAR_TEST_CASE(resp_processor_invalid_arg_count_underflow) {
    RespStreamProcessor parser = RespStreamProcessor();
    seastar::sstring buf = "*1\r\n$3\r\nGET\r\n$4\r\nNAME\r\n";
    auto req = parser.try_extract_request(buf);
    BOOST_REQUIRE(req.has_value());
    BOOST_TEST_MESSAGE("frame=" << std::string(req->frame));

    BOOST_REQUIRE(req->argv.size() == 1);
    BOOST_REQUIRE(req->argv[0] == "GET");
    BOOST_REQUIRE(req->frame == "*1\r\n$3\r\nGET\r\n");

    co_return;
}

SEASTAR_TEST_CASE(resp_processor_min_arg_count) {
    RespStreamProcessor parser = RespStreamProcessor();
    seastar::sstring buf = "*0\r\n$3\r\nGET\r\n$4\r\nNAME\r\n";
    auto req = parser.try_extract_request(buf);
    BOOST_REQUIRE(req.has_value());

    BOOST_REQUIRE(req->frame == "-ERR invalid argc\r\n");

    co_return;
}

SEASTAR_TEST_CASE(resp_processor_max_arg_count) {
    RespStreamProcessor parser = RespStreamProcessor();
    seastar::sstring buf = "*1025\r\n$3\r\nGET\r\n$4\r\nNAME\r\n";
    auto req = parser.try_extract_request(buf);
    BOOST_REQUIRE(req.has_value());

    BOOST_REQUIRE(req->frame == "-ERR invalid argc\r\n");

    co_return;
}

SEASTAR_TEST_CASE(resp_processor_command_pipelined) {
    RespStreamProcessor parser = RespStreamProcessor();
    seastar::sstring buf;
    std::string_view chunk;
    chunk = "*3\r\n$3\r\nSET\r\n$4\r\nNAME\r\n$5\r\nTIGER\r\n";
    buf.append(chunk.data(), chunk.size());
    chunk = "*2\r\n$3\r\nGET\r\n$4\r\nNAME\r\n";
    buf.append(chunk.data(), chunk.size());
    auto req = parser.try_extract_request(buf);
    BOOST_REQUIRE(req.has_value());
    BOOST_REQUIRE(req->argv.size() == 3);
    BOOST_REQUIRE(req->argv[0] == "SET");
    BOOST_REQUIRE(req->argv[1] == "NAME");
    BOOST_REQUIRE(req->argv[2] == "TIGER");
    req = parser.try_extract_request(buf);
    BOOST_REQUIRE(req.has_value());
    BOOST_REQUIRE(req->argv.size() == 2);
    BOOST_REQUIRE(req->argv[0] == "GET");
    BOOST_REQUIRE(req->argv[1] == "NAME");
    co_return;
}

SEASTAR_TEST_CASE(resp_processor_command_pipelined_split) {
    RespStreamProcessor parser = RespStreamProcessor();
    seastar::sstring buf;
    std::string_view chunk;
    chunk = "*3\r\n$3\r\nSET\r\n$4\r\nNAME\r\n$5\r\nTIGER\r\n";
    buf.append(chunk.data(), chunk.size());
    chunk = "*2\r\n$3\r\nGET";
    buf.append(chunk.data(), chunk.size());
    auto req = parser.try_extract_request(buf);
    BOOST_REQUIRE(req.has_value());
    BOOST_REQUIRE(req->argv.size() == 3);
    BOOST_REQUIRE(req->argv[0] == "SET");
    BOOST_REQUIRE(req->argv[1] == "NAME");
    BOOST_REQUIRE(req->argv[2] == "TIGER");
    chunk = "\r\n$4\r\nNAME\r\n";
    buf.append(chunk.data(), chunk.size());
    req = parser.try_extract_request(buf);
    BOOST_REQUIRE(req.has_value());
    BOOST_REQUIRE(req->argv.size() == 2);
    BOOST_REQUIRE(req->argv[0] == "GET");
    BOOST_REQUIRE(req->argv[1] == "NAME");
    co_return;
}

SEASTAR_TEST_CASE(resp_processor_command_pipelined_valid_invalid) {
    RespStreamProcessor parser = RespStreamProcessor();
    seastar::sstring buf;
    std::string_view chunk;
    chunk = "*3\r\n$3\r\nSET\r\n$4\r\nNAME\r\n$5\r\nTIGER\r\n";
    buf.append(chunk.data(), chunk.size());
    chunk = "*-1\r\n$3\r\nGET";
    buf.append(chunk.data(), chunk.size());
    auto req = parser.try_extract_request(buf);
    BOOST_REQUIRE(req.has_value());
    BOOST_REQUIRE(req->argv.size() == 3);
    BOOST_REQUIRE(req->argv[0] == "SET");
    BOOST_REQUIRE(req->argv[1] == "NAME");
    BOOST_REQUIRE(req->argv[2] == "TIGER");
    chunk = "\r\n$4\r\nNAME\r\n";
    buf.append(chunk.data(), chunk.size());
    req = parser.try_extract_request(buf);
    BOOST_REQUIRE(req.has_value());
    BOOST_REQUIRE(req->argv.size() == 0);
    BOOST_REQUIRE(req->frame == "-ERR invalid argc\r\n");
    chunk = "*2\r\n$3\r\nGET\r\n$4\r\nNAME\r\n";
    buf.append(chunk.data(), chunk.size());
    req = parser.try_extract_request(buf);
    BOOST_REQUIRE(req.has_value());
    BOOST_REQUIRE(req->argv.size() == 2);
    BOOST_REQUIRE(req->argv[0] == "GET");
    BOOST_REQUIRE(req->argv[1] == "NAME");

    co_return;
}

SEASTAR_TEST_CASE(resp_processor_blank_command) {
    RespStreamProcessor parser = RespStreamProcessor();
    seastar::sstring buf;
    std::string_view chunk;
    chunk = "*3                                                              ";
    buf.append(chunk.data(), chunk.size());
    auto req = parser.try_extract_request(buf);
    BOOST_REQUIRE(!req.has_value());

    co_return;
}

SEASTAR_TEST_CASE(resp_processor_stress) {
    RespStreamProcessor parser = RespStreamProcessor();
    seastar::sstring buf;
    std::string_view chunk;
    chunk = "*";
    auto req = extract(chunk, buf, parser);
    BOOST_REQUIRE(req == std::nullopt);

    chunk = "1";
    req = extract(chunk, buf, parser);
    BOOST_REQUIRE(req == std::nullopt);

    chunk = "\r";
    req = extract(chunk, buf, parser);
    BOOST_REQUIRE(req == std::nullopt);

    chunk = "\n";
    req = extract(chunk, buf, parser);
    BOOST_REQUIRE(req == std::nullopt);

    chunk = "$4";
    req = extract(chunk, buf, parser);
    BOOST_REQUIRE(req == std::nullopt);

    chunk = "\r";
    req = extract(chunk, buf, parser);
    BOOST_REQUIRE(req == std::nullopt);

    chunk = "\nPI";
    req = extract(chunk, buf, parser);
    BOOST_REQUIRE(req == std::nullopt);

    chunk = "NG";
    req = extract(chunk, buf, parser);
    BOOST_REQUIRE(req == std::nullopt);

    chunk = "\r\n";
    req = extract(chunk, buf, parser);
    BOOST_REQUIRE(req.has_value());
    BOOST_REQUIRE(req->argv.size() == 1);
    BOOST_REQUIRE(req->argv[0] == "PING");

    co_return;
}

SEASTAR_TEST_CASE(resp_processor_missing_crlf) {
    RespStreamProcessor parser = RespStreamProcessor();
    seastar::sstring buf;
    std::string_view chunk;
    chunk = "*2\r\n$3\r\nGET\r\n$4\r\nNAME";
    auto req = extract(chunk, buf, parser);
    BOOST_REQUIRE(!req.has_value());

    chunk = "\r\n";
    req = extract(chunk, buf, parser);
    BOOST_REQUIRE(req.has_value());
    BOOST_REQUIRE(req->argv.size() == 2);
    BOOST_REQUIRE(req->argv[0] == "GET");
    BOOST_REQUIRE(req->argv[1] == "NAME");

    co_return;
}
