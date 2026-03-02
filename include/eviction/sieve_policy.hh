// eviction/sieve_policy.hh
#pragma once
#include "eviction/eviction_config.hh"
#include "ttl/entry.hh"
#include <boost/intrusive/list.hpp>
#include <seastar/coroutine/maybe_yield.hh>

namespace bi = boost::intrusive;

using SieveList =
    bi::list<ttl::Entry,
             bi::member_hook<ttl::Entry, decltype(ttl::Entry::list_hook),
                             &ttl::Entry::list_hook>>;

class SievePolicy {
  public:
    explicit SievePolicy(eviction::EvictionConfig cfg) : evCfg_(cfg) {
        hand_ = sieveList_.end();
    }

    void on_insert(ttl::Entry &e);
    void on_erase(ttl::Entry &e);
    void on_hit(ttl::Entry &e);

    seastar::future<std::size_t> evict();

    std::size_t size() const noexcept;
    bool empty() const noexcept;

  private:
    SieveList sieveList_;
    SieveList::iterator hand_;
    eviction::EvictionConfig evCfg_;
};
