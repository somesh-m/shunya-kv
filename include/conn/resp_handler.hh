#pragma once
#include "socket_handler.hh"
#include "resp/resp_parser.hh"
#include <optional>
#include <vector>

class RespHandler final : public PipelinedSocketHandler {
  private:
    // Streaming parser state
    enum class st {
        need_star,
        need_argc,
        need_dollar,
        need_bulk_len,
        need_bulk_data,
        need_bulk_crlf
    };

    st _st{st::need_star};
    size_t _pos{0};       // absolute cursor in the read buffer
    size_t _cmd_start{0}; // start offset of current command in read buffer

    int64_t _argc{0};
    int64_t _argi{0};
    int64_t _bulk_len{0};

    struct slice {
        uint32_t off;
        uint32_t len;
    };
    std::vector<slice> _slices;

    // Limits (tune)
    static constexpr size_t kMaxArgs = 1024;
    static constexpr size_t kMaxBulk = 64 * 1024 * 1024;

  private:
    // helpers
    static inline void compact_if_needed(seastar::sstring &buf, size_t &pos) {
        if (pos == 0)
            return;
        if (pos >= buf.size()) {
            buf = {};
            pos = 0;
            return;
        }
        if (pos >= 4096 || pos * 2 >= buf.size()) {
            buf = buf.substr(pos);
            pos = 0;
        }
    }

    // Find and parse an int terminated by CRLF starting at _pos; advances _pos
    // past '\n'
    resp::frame_parse_status parse_int_crlf(const seastar::sstring &buf,
                                            int64_t &out);

  protected:
    seastar::future<seastar::sstring>
    handle_request(shunyakv::ParsedRequest req) override;

    std::optional<shunyakv::ParsedRequest>
    try_extract_request(seastar::sstring &buf) override;
};
