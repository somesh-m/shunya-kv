#pragma once

#include "resp/resp_types.hh"
#include <seastar/core/future.hh>
#include <seastar/core/iostream.hh>
#include <seastar/core/sstring.hh>

namespace resp {

// +OK\r\n
seastar::future<> write_simple(seastar::output_stream<char> &out,
                               const seastar::sstring &msg);

// -ERR ...\r\n
seastar::future<> write_error(seastar::output_stream<char> &out,
                              const seastar::sstring &msg);

// :123\r\n
seastar::future<> write_integer(seastar::output_stream<char> &out, int64_t v);

// $<len>\r\n<bytes>\r\n
seastar::future<> write_bulk(seastar::output_stream<char> &out,
                             const seastar::sstring &bytes);

// $-1\r\n  (null bulk string; perfect for GET miss)
seastar::future<> write_null(seastar::output_stream<char> &out);

// Optional: write an array reply of bulk strings (rarely needed for your KV,
// but handy later for MGET/KEYS/etc.)
seastar::future<> write_array_bulk(seastar::output_stream<char> &out,
                                   const Array &items);

} // namespace resp
