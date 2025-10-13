#include "server_debug.hh"
#include <algorithm>
#include <seastar/core/coroutine.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/smp.hh>
#include <vector>

static seastar::logger dbg_log{"server_debug"}; // local to this TU

namespace shunyakv {
extern unsigned
shard_for(std::string_view key); // or include the header that declares it

seastar::future<> hash_spread_check(std::size_t nkeys, std::string prefix) {
    if (seastar::this_shard_id() != 0)
        co_return;

    std::vector<uint64_t> cnt(seastar::smp::count, 0);
    for (size_t i = 0; i < nkeys; ++i) {
        std::string k = prefix + std::to_string(i);
        ++cnt[shard_for(k)];
    }

    auto [mn, mx] = std::minmax_element(cnt.begin(), cnt.end());
    double avg = double(nkeys) / double(seastar::smp::count);
    dbg_log.info("spread: min={}, max={}, avg={:.1f}, max/min={:.3f}", *mn, *mx,
                 avg, double(*mx) / std::max<double>(*mn, 1.0));
    for (unsigned s = 0; s < cnt.size(); ++s)
        dbg_log.info("shard {:2d} -> {}", s, cnt[s]);
    co_return;
}
} // namespace shunyakv
