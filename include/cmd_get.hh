#pragma once
#include "commands.hh"
#include <resp/resp_types.hh>

namespace shunyakv {
seastar::future<> handle_get(const resp::ArgvView &cmd,
                             seastar::output_stream<char> &out,
                             shunyakv::service &svc);
}
