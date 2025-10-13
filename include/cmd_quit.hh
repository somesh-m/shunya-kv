#pragma once
#include "commands.hh"

namespace shunyakv {
seastar::future<> handle_quit(std::string_view cmd,
                              seastar::output_stream<char> &out,
                              shunyakv::service &svc);
}
