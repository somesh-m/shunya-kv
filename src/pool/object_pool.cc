#include "pool/object_pool.hh"
#include <memory>
#include <seastar/core/coroutine.hh>

std::size_t CacheEntryPool::get_available_slots() const { return pool_.available(); }
std::size_t CacheEntryPool::get_total_slots() const { return pool_.total_allocated(); }
std::size_t CacheEntryPool::get_used_slots() const { return pool_.used(); }

std::size_t CacheEntryPool::get_available_prob_slots() const {
    return prob_pool_max_size_ - prob_count_;
}
std::size_t CacheEntryPool::get_total_prob_slots() const {
    return prob_pool_max_size_;
}

std::size_t CacheEntryPool::get_prob_eviction_count() const {
    return prob_eviction_count_;
}

std::size_t CacheEntryPool::get_used_prob_slots() const { return prob_count_; }

std::size_t CacheEntryPool::get_used_sanctuary_slots() const {
    return sanc_count_;
}

std::size_t CacheEntryPool::get_total_sanctuary_slots() const {
    return get_total_slots() - get_total_prob_slots();
}

seastar::future<> CacheEntryPool::run_sequential_reaper() {

    if (probation_list_.empty()) {

        co_return;
    }

    // Reset to a known-good iterator at the start of each run so we don't
    // carry a stale intrusive-list iterator across unrelated mutations.
    probation_hand_ = probation_list_.begin();
    std::vector<seastar::sstring> victim_list;
    victim_list.reserve(reaper_budget_);

    std::size_t evicted_count = 0;
    while (evicted_count < reaper_budget_ && !probation_list_.empty()) {
        auto victim_it = probation_hand_;

        // 1. Determine where the 'hand' should move next BEFORE erasing
        // If we are at the last element, wrap to the beginning.
        auto next_it = std::next(victim_it);
        if (next_it == probation_list_.end()) {
            next_it = probation_list_.begin();
        }

        // 2. Collect data and erase

        victim_list.push_back(victim_it->key);
        probation_list_.erase(victim_it);

        prob_count_--;
        evicted_count++;

        // 3. Update the persistent hand
        // If the list is now empty, point to end(); otherwise, point to our
        // pre-calculated next element
        probation_hand_ =
            probation_list_.empty() ? probation_list_.end() : next_it;

        // 4. Cooperative multitasking yield
        if (evicted_count % 1000 == 0) {
            co_await seastar::coroutine::maybe_yield();
        }
    }
    prob_eviction_count_ += evicted_count;
    if (on_sequential_evict_ && !victim_list.empty()) {

        co_await on_sequential_evict_(victim_list);
    }

    co_return;
}

void CacheEntryPool::remove_from_probation(ttl::Entry &entry) {
    if (!entry.probation_hook.is_linked()) {
        return;
    }
    auto it = ProbationList::s_iterator_to(entry);
    auto next_it = std::next(it);
    if (next_it == probation_list_.end()) {
        next_it = probation_list_.begin();
    }
    const bool removed_hand = (it == probation_hand_);
    probation_list_.erase(it);

    if (prob_count_ > 0) {
        prob_count_--;
    }

    if (probation_list_.empty()) {
        probation_hand_ = probation_list_.end();
    } else if (removed_hand) {
        probation_hand_ = next_it;
    }
}

void CacheEntryPool::promote_to_sanctuary(ttl::Entry &entry) {
    /**
     * This function takes an entry and does:
     * return if the entry is already in sanctuary
     * change the type to sanctuary
     * update the intrusive hooks
     * update the counters
     */
    if (entry.pool_type == ttl::PoolType::Sanctuary) {
        return;
    }
    remove_from_probation(entry);
    if (sieve_policy_ != nullptr) {
        sieve_policy_->on_insert(entry);
    } else {
        entry.pool_type = ttl::PoolType::Sanctuary;
    }
    sanc_count_++;
}

seastar::future<std::unique_ptr<ttl::Entry>> CacheEntryPool::do_acquire() {
    auto entry = co_await pool_.acquire();

    entry->in_use_ = true;
    sanc_count_++;

    if (sieve_policy_ != nullptr) {
        sieve_policy_->on_insert(*entry);
    }

    co_return entry;
}

seastar::future<std::unique_ptr<ttl::Entry>> CacheEntryPool::do_prob_acquire() {
    if (prob_count_ >= prob_threshold_) {
        try {
            co_await run_sequential_reaper();
        } catch (...) {
            try {
                throw;
            } catch (const std::exception &e) {
                pool_logger().error("acquire reaper failed on threshold path: "
                                    "{}, backtrace: {}",
                                    e.what(), seastar::current_backtrace());
            } catch (...) {
                pool_logger().error("acquire reaper failed on threshold path: "
                                    "unknown exception, backtrace: {}",
                                    seastar::current_backtrace());
            }
            throw;
        }
    }

    auto entry = co_await pool_.acquire();

    entry->in_use_ = true;
    entry->pool_type = ttl::PoolType::Probation;
    prob_count_++;

    probation_list_.push_back(*entry);

    co_return entry;
}

seastar::future<std::unique_ptr<ttl::Entry>> CacheEntryPool::acquire() {
    if (prob_pool_max_size_ <= 0) {
        co_return co_await do_acquire();
    }
    co_return co_await do_prob_acquire();
}

void CacheEntryPool::do_release(std::unique_ptr<ttl::Entry> entry) {
    if (!entry) {
        return;
    }

    if (sieve_policy_ != nullptr) {
        sieve_policy_->on_erase(*entry);
    }

    if (sanc_count_ > 0) {
        sanc_count_--;
    }

    pool_.release(std::move(entry));
}

void CacheEntryPool::do_prob_release(std::unique_ptr<ttl::Entry> entry) {
    if (!entry) {
        return;
    }

    if (entry->pool_type == ttl::PoolType::Probation) {
        remove_from_probation(*entry);
    } else {
        if (sieve_policy_ != nullptr) {
            sieve_policy_->on_erase(*entry);
        }

        if (sanc_count_ > 0) {
            sanc_count_--;
        }
    }

    pool_.release(std::move(entry));
}

void CacheEntryPool::release(std::unique_ptr<ttl::Entry> entry) {
    if (prob_pool_max_size_ <= 0) {
        do_release(std::move(entry));
        return;
    }
    do_prob_release(std::move(entry));
}

std::size_t CacheEntryPool::get_per_entry_size_estimate() {
    return sizeof(ttl::Entry) + value_offset_;
}

std::size_t CacheEntryPool::get_max_allowed_memory_for_pool() {
    pool_logger().info("Pool max percent {} Usable memory {}",
                       pool_max_memory_percent_, usable_memory_);
    const std::size_t target_memory =
        usable_memory_ * (pool_max_memory_percent_ / 100.0);
    return target_memory;
}

std::size_t CacheEntryPool::calculate_optimal_pool_size() noexcept {
    const std::size_t target_memory = get_max_allowed_memory_for_pool();
    if (target_memory == 0) {
        pool_logger().warn("Pool target memory is zero; usable_memory={} "
                           "pool_max_memory_percent={}",
                           usable_memory_, pool_max_memory_percent_);
        return 0;
    }
    const std::size_t estimated_entry_size = get_per_entry_size_estimate();
    const std::size_t pool_size =
        std::max<std::size_t>(1, target_memory / estimated_entry_size);
    pool_logger().info("Shard Id: {}\n Usable Memory: {} GB \nPool Count: {}\n "
                       "Estimated entry size {} \n",
                       seastar::this_shard_id(),
                       target_memory / (1024.0 * 1024.0 * 1024.0), pool_size,
                       estimated_entry_size);
    return pool_size;
}
