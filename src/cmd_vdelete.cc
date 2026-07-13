#include "cmd_vdelete.hh"
#include "commands.hh"
#include "router.hh"

#include <resp/resp_types.hh>
#include <resp/resp_writer.hh>

#include <seastar/core/future.hh>
#include <seastar/core/iostream.hh>

namespace shunyakv {

seastar::future<> handle_vdelete(const resp::ArgvView &cmd,
                                 seastar::output_stream<char> &out,
                                 shunyakv::service &service) {
    if (cmd.size() < 3) {
        co_await resp::write_error(out,
                                   "ERR wrong number of arguments for 'VDELETE'");
        co_return;
    }

    const auto &key = cmd[1];
    const auto &index = cmd[2];

    if (key.empty()) {
        co_await resp::write_error(out, "ERR empty key");
        co_return;
    }

    if (index.empty()) {
        co_await resp::write_error(out, "ERR empty index");
        co_return;
    }

    const bool ok = co_await service.vdelete(index, key);
    if (ok) {
        co_await resp::write_simple(out, "OK");
    } else {
        co_await resp::write_null(out);
    }
}

} // namespace shunyakv
