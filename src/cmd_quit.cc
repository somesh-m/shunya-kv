#include "cmd_quit.hh"
#include "commands.hh"
#include <seastar/core/future.hh>

namespace shunyakv {
seastar::future<> handle_quit(std::string_view /*rest*/,
                              seastar::output_stream<char> &out,
                              shunyakv::service & /*svc*/) {
    // We just say bye; the server loop will see QUIT succeeded and close the
    // connection.
    co_await out.write("BYE\r\n");
    co_return;
}
} // namespace shunyakv
