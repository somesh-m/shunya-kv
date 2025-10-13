#pragma once
#include <seastar/core/future.hh>
#include <string>

namespace shunyakv {
seastar::future<> hash_spread_check(std::size_t nkeys,
                                    std::string prefix = "k");
}
