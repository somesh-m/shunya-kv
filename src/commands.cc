#include "commands.hh"
#include "cmd_get.hh"
#include "cmd_info.hh"
#include "cmd_node_info.hh"
#include "cmd_quit.hh"
#include "cmd_set.hh"
#include "cmd_vget.hh"
#include "cmd_vsearch.hh"
#include "cmd_vset.hh"

namespace shunyakv {
const std::unordered_map<std::string_view, Handler> &command_dispatch() {
    static const std::unordered_map<std::string_view, Handler> table = {
        {"SET", shunyakv::handle_set},
        {"GET", shunyakv::handle_get},
        {"QUIT", shunyakv::handle_quit},
        {"NODE_INFO", shunyakv::handle_node_info},
        {"INFO", shunyakv::handle_info},
        {"VSET", shunyakv::handle_vset},
        {"VSEARCH", shunyakv::handle_vsearch},
        {"VGET", shunyakv::handle_vget},
    };
    return table;
}
} // namespace shunyakv
