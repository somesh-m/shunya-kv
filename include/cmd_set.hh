#pragma once
#include "commands.hh"

namespace shunyakv {
seastar::future<> handle_set(std::string_view args,
                             seastar::output_stream<char> &out,
                             shunyakv::service &svc);
}
