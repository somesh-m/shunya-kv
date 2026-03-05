#pragma once

#include "conn/socket_handler.hh"
#include "resp/resp_parser.hh"

#include <optional>
#include <vector>

class RespStreamProcessor {
  private:
    enum class st {
        need_star,
        need_argc,
        need_dollar,
        need_bulk_len,
        need_bulk_data,
        need_bulk_crlf
    };

    st _st{st::need_star};
    size_t _pos{0};
    size_t _cmd_start{0};

    int64_t _argc{0};
    int64_t _argi{0};
    int64_t _bulk_len{0};

    struct slice {
        uint32_t off;
        uint32_t len;
    };
    std::vector<slice> _slices;

    static constexpr size_t kMaxArgs = 1024;
    static constexpr size_t kMaxBulk = 64 * 1024 * 1024;

    static void compact_if_needed(seastar::sstring &buf, size_t &pos);
    void reset_and_resync(seastar::sstring &buf);
    resp::frame_parse_status parse_int_crlf(const seastar::sstring &buf,
                                            int64_t &out);

  public:
    seastar::future<seastar::sstring> handle_request(shunyakv::ParsedRequest req);

    std::optional<shunyakv::ParsedRequest>
    try_extract_request(seastar::sstring &buf);
};
