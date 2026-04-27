#pragma once
#include "eviction/sieve_policy.hh"
#include "pool/pool.hh"
#include <boost/intrusive/list.hpp>
#include <cstddef>
#include <dbconfig.hh>
#include <memory>
#include <optional>
#include <seastar/core/circular_buffer.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/memory.hh>
#include <seastar/core/shard_id.hh>
#include <seastar/util/log.hh>

inline seastar::logger &pool_logger() {
    static seastar::logger logger{"object_pool"};
    return logger;
}

using EvictionCallback =
    std::function<seastar::future<>(std::vector<seastar::sstring>)>;

namespace bi = boost::intrusive;

using ProbationList =
    bi::list<ttl::Entry,
             bi::member_hook<ttl::Entry,
                             bi::list_member_hook<bi::link_mode<bi::safe_link>>,
                             &ttl::Entry::probation_hook>>;

class CacheEntryPool {
  public:
    explicit CacheEntryPool(std::size_t max_size = 0) : max_size_(max_size) {
        probation_hand_ = probation_list_.end();
    }

    seastar::future<>
    init(const db_config &cfg,
         SievePolicy &sieve_policy) { // call this after construction
        if (initialized_) {
            co_return;
        }
        cfg_ = cfg;
        sieve_policy_ = &sieve_policy;
        value_offset_ = cfg.pool.page_size_goal + cfg.pool.key_reserve;
        auto stats = seastar::memory::stats();
        // Keeping 15 percent reserved for seastar overhead
        usable_memory_ =
            (1 - cfg_.pool.memory_reserve_percentage) * stats.total_memory();
        pool_max_memory_percent_ = cfg_.pool.pool_max_memory_percent;
        pool_logger().info("pool percent: {}",
                           cfg_.pool.prob_pool_size_percent);
        if (max_size_ == 0) {
            max_size_ = calculate_optimal_pool_size();
            prob_pool_max_size_ =
                floor(cfg_.pool.prob_pool_size_percent * max_size_);
            reaper_budget_ = cfg_.ev_config.prob_evict_.prob_budget;
            prob_threshold_ =
                cfg_.ev_config.prob_evict_.trigger * prob_pool_max_size_;
            pool_logger().info("Max Size: {}; reaper budget: {}; prob pool "
                               "size: {}; prob threshold: {}",
                               max_size_, reaper_budget_, prob_pool_max_size_,
                               prob_threshold_);
        } else {
            // User provided max size. Verify that it fits into the allowed
            // pool memory budget now that runtime memory information is known.
            const std::size_t estimated_per_entry_size =
                get_per_entry_size_estimate();
            const std::size_t total_mem_req =
                estimated_per_entry_size * max_size_;
            if (total_mem_req > get_max_allowed_memory_for_pool()) {
                const std::size_t requested_max_size = max_size_;
                max_size_ = calculate_optimal_pool_size();

                pool_logger().info("Pool size overflow. Requested {} Feasible "
                                   "{}. Falling back to max entry possible",
                                   requested_max_size, max_size_);
            }
            prob_pool_max_size_ =
                floor(cfg_.pool.prob_pool_size_percent * max_size_);
            reaper_budget_ = cfg_.ev_config.prob_evict_.prob_budget;
            prob_threshold_ =
                cfg_.ev_config.prob_evict_.trigger * prob_pool_max_size_;
        }
        pool_logger().info("Max Size: {}; reaper budget: {}; prob pool "
                           "size: {}; prob threshold: {}",
                           max_size_, reaper_budget_, prob_pool_max_size_,
                           prob_threshold_);
        initialized_ = true;
        co_await prepopulate_pool();
    }

    seastar::future<std::unique_ptr<ttl::Entry>> acquire();
    void release(std::unique_ptr<ttl::Entry> entry);
    seastar::future<std::unique_ptr<ttl::Entry>> do_acquire();
    void do_release(std::unique_ptr<ttl::Entry> entry);
    seastar::future<std::unique_ptr<ttl::Entry>> do_prob_acquire();
    void do_prob_release(std::unique_ptr<ttl::Entry> entry);
    seastar::future<> run_sequential_reaper();
    void promote_to_sanctuary(ttl::Entry &entry);
    void set_sequential_eviction_callback(EvictionCallback cb) {
        on_sequential_evict_ = std::move(cb);
    }
    void remove_from_probation(ttl::Entry &entry);

    std::size_t calculate_optimal_pool_size() noexcept;
    std::size_t get_available_slots() const;
    std::size_t get_total_slots() const;
    std::size_t get_available_prob_slots() const;
    std::size_t get_total_prob_slots() const;
    std::size_t get_per_entry_size_estimate();
    std::size_t get_max_allowed_memory_for_pool();
    std::size_t get_used_slots() const;
    std::size_t get_used_prob_slots() const;
    std::size_t get_used_sanctuary_slots() const;
    std::size_t get_total_sanctuary_slots() const;
    std::size_t get_prob_eviction_count() const;

  private:
    seastar::circular_buffer<std::unique_ptr<ttl::Entry>> pool_;
    std::size_t value_offset_ = 65432;
    std::size_t free_after_pool_{0};
    double pool_max_memory_percent_ = 0.7;
    std::size_t usable_memory_{0};
    std::size_t max_size_{0};
    std::size_t prob_pool_max_size_{0};
    uint64_t prob_threshold_{0};
    uint64_t reaper_budget_{0};
    uint64_t prob_count_{0};
    uint64_t sanc_count_{0};
    bool initialized_ = false;
    std::shared_ptr<SievePolicy> policy_;
    seastar::future<> prepopulate_pool();
    db_config cfg_;
    uint64_t prob_eviction_count_{0};

    ProbationList probation_list_;

    // The 'hand' for the SIEVE algorithm inside the Sanctuary
    ProbationList::iterator probation_hand_;
    SievePolicy *sieve_policy_ = nullptr;

    EvictionCallback on_sequential_evict_;
};
