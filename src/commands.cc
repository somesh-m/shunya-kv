#include "commands.hh"
#include "cmd_get.hh"
#include "cmd_info.hh"
#include "cmd_node_info.hh"
#include "cmd_quit.hh"
#include "cmd_set.hh"

namespace shunyakv {
const std::unordered_map<std::string_view, Handler> &command_dispatch() {
    static const std::unordered_map<std::string_view, Handler> table = {
        {"SET", shunyakv::handle_set},
        {"GET", shunyakv::handle_get},
        {"QUIT", shunyakv::handle_quit},
        {"NODE_INFO", shunyakv::handle_node_info},
        {"INFO", shunyakv::handle_info},
    };
    return table;
}
} // namespace shunyakv
