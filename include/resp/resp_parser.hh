#pragma once

#include "resp/resp_types.hh"
#include <seastar/core/future.hh>
#include <seastar/core/iostream.hh>
#include <seastar/core/sstring.hh>

namespace resp {

// Parser object holds buffered bytes across reads (per connection)
class Reader {
  public:
    // Returns empty Array if peer closed connection cleanly (and no partial
    // frame pending).
    seastar::future<Array> read_command(seastar::input_stream<char> &in);

  private:
    seastar::sstring _buf;

    seastar::future<> ensure(seastar::input_stream<char> &in, size_t n);
    seastar::future<char> read_byte(seastar::input_stream<char> &in);
    seastar::future<seastar::sstring>
    read_line_crlf(seastar::input_stream<char> &in); // without CRLF
    seastar::future<int64_t> read_int_line(seastar::input_stream<char> &in);
    seastar::future<seastar::sstring>
    read_bulk(seastar::input_stream<char> &in);
};

} // namespace resp
