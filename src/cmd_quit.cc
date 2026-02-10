#include "cmd_quit.hh"
#include "commands.hh"
#include <resp/resp_types.hh>
#include <resp/resp_writer.hh>
#include <seastar/core/future.hh>

namespace shunyakv {
seastar::future<> handle_quit(const resp::Array &cmd /*rest*/,
                              seastar::output_stream<char> &out,
                              shunyakv::service & /*svc*/) {
    // We just say bye; the server loop will see QUIT succeeded and close the
    // connection.
    co_await resp::write_simple(out, "BYE");
    co_return;
}
} // namespace shunyakv
