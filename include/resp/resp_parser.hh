#pragma once

#include "resp/resp_types.hh"
#include <seastar/core/future.hh>
#include <seastar/core/iostream.hh>
#include <seastar/core/sstring.hh>
#include <string_view>

namespace resp {

enum class frame_parse_status { ok, need_more, invalid };

frame_parse_status parse_frame_length(std::string_view s, size_t &frame_len);

seastar::future<Array> parse_command_from_frame(const seastar::sstring &frame);

// Parser object holds buffered bytes across reads (per connection)
class Reader {
  public:
    // Returns empty Array if peer closed connection cleanly (and no partial
    // frame pending).
    seastar::future<Array> read_command(seastar::input_stream<char> &in);

  private:
    seastar::sstring _buf;
    size_t _off{0};

    void compact_if_needed();
    size_t size() const { return _buf.size() - _off; }
    std::string_view view() const {
        return std::string_view(_buf.data() + _off, size());
    }

    seastar::future<> ensure(seastar::input_stream<char> &in, size_t n);
    seastar::future<char> read_byte(seastar::input_stream<char> &in);
    seastar::future<seastar::sstring>
    read_line_crlf(seastar::input_stream<char> &in); // without CRLF
    seastar::future<int64_t> read_int_line(seastar::input_stream<char> &in);
    seastar::future<seastar::sstring>
    read_bulk(seastar::input_stream<char> &in);
};

} // namespace resp
