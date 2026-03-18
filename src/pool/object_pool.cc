#include "pool/object_pool.hh"
#include <memory>
#include <seastar/core/coroutine.hh>

void remove_from_probation(ttl::Entry &entry);
seastar::future<> CacheEntryPool::prepopulate_pool() {
    prob_pool_.reserve(prob_pool_max_size_);
    pool_.reserve(max_size_);
    // Populate the full time pool
    for (std::size_t i = 0; i < max_size_; i++) {
        auto entry = std::make_unique<ttl::Entry>();
        entry->value.reserve(value_offset_);
        pool_.push_back(std::move(entry));
        if (i % 500 == 0) {
            co_await seastar::coroutine::maybe_yield();
        }
    }
    // Populate the probationary pool
    for (std::size_t i = 0; i < prob_pool_max_size_; i++) {
        auto entry = std::make_unique<ttl::Entry>();
        entry->value.reserve(value_offset_);
        prob_pool_.push_back(std::move(entry));
        if (i % 500 == 0) {
            co_await seastar::coroutine::maybe_yield();
        }
    }
    // Record the free mem which is supposed to be the baseline for on
    auto stats = seastar::memory::stats();
    free_after_pool_ = stats.free_memory();
    pool_logger().info("Shard Id: {}; Pool size: {} ", seastar::this_shard_id(),
                       pool_.size());
    pool_logger().info("Shard Id: {}; Probationary Pool size: {} ",
                       seastar::this_shard_id(), prob_pool_.size());
    co_return;
}

std::size_t CacheEntryPool::get_available_slots() const { return pool_.size(); }
std::size_t CacheEntryPool::get_total_slots() const { return max_size_; }
std::size_t CacheEntryPool::get_used_slots() const {
    return max_size_ - pool_.size();
}
std::size_t CacheEntryPool::get_available_prob_slots() const {
    return prob_pool_max_size_ - prob_count_;
}
std::size_t CacheEntryPool::get_total_prob_slots() const {
    return prob_pool_max_size_;
}

std::size_t CacheEntryPool::get_used_prob_slots() const { return prob_count_; }

seastar::future<> CacheEntryPool::run_sequential_reaper() {
    /**
     * This function runs the sequential reaper
     * It removes reaper_budget_ county of entry from the probation_hand_
     * Advances probation_hand_ by reaper_budget_
     */
    if (probation_list_.empty()) {
        return;
    }
    std::vector<seastar::sstring> victim_list;
    victim_list.reserve(reaper_budget_);
    if (probation_hand_ == probation_list_.end()) {
        probation_hand_ = probation_list_.begin();
    }

    std::size_t evicted_count = 0;
    while (evicted_count < reaper_budget_) {
        ttl::Entry &cur = *probation_hand_;
        victim_list.push_back(probation_hand_->key);
        probation_list_.erase(probation_hand_);
        if (probation_list_.empty()) {
            probation_hand_ = probation_list_.end();
            break;
        }
        ++probation_hand_;
        ++evicted_count;
        prob_count_--;

        if (evicted_count % 500 == 0) {
            co_await seastar::coroutine::maybe_yield();
        }

        if (probation_hand_ == probation_list_.end()) {
            probation_hand_ = probation_list_.begin();
        }
    }
    if (on_sequential_evict_) {
        co_await on_sequential_evict_(victim_list);
    }
}

void remove_from_probation(ttl::Entry &entry) {
    if (!entry.probation_hook.is_linked()) {
        return;
    }
    auto it = ProbationList::s_iterator_to(entry);
    probation_list_.erase(it);

    if (prob_count_ > 0) {
        prob_count_--;
    }
}

void CacheEntryPool::promote_to_sanctuary(std::unique_ptr<ttl::Entry> entry) {
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
    remove_from_probation(*entry.get());

    sieve_policy_.on_insert(*entry.get());
    sanc_count_++;
}

seastar::future<std::unique_ptr<ttl::Entry>> CacheEntryPool::acquire() {
    if (pool_.empty()) {
        // pool_logger().info("Bucket empty, generating new object");
        auto entry = std::make_unique<ttl::Entry>();
        entry->value.reserve(value_offset_);
        entry->in_use_ = true;
        entry->pool_type = ttl::PoolType::Probation;
        prob_count_++;

        // TODO: Check if there is a way to run this but not block the return.
        // Kind of like run on next tick
        co_await run_sequential_reaper();
        co_return entry;
    }

    if (prob_count_ >= prob_threshold_) {
        co_await run_sequential_reaper();
    }
    auto entry = std::move(pool_.front());
    pool_.pop_front();
    entry->in_use_ = true;
    entry->pool_type = ttl::PoolType::Probation;
    prob_count_++;
    probation_list_.push_back(*entry);
    co_return entry;
}

void CacheEntryPool::release(std::unique_ptr<ttl::Entry> entry) {
    if (!entry) {
        return;
    }

    if (entry->pool_type == ttl::PoolType::Probation) {
        remove_from_probation(*entry.get());
        prob_count_--;
    } else {
        sieve_policy_.on_erase(*entry.get());
        sanc_count_--;
    }

    entry->in_use_ = false;
    entry->visited = false;
    entry->value.clear();
    entry->key = "";
    entry->expires_at = 0;
    entry->ver = 0;
    entry->heat = 0;
    entry->last_access = 0;

    if (entry->value.capacity() > value_offset_ * 2) {
        std::string tmp;
        tmp.reserve(value_offset_);
        entry->value.swap(tmp);
    }

    if (pool_.size() < max_size_) {
        pool_.push_back(std::move(entry));
        return;
    }

    entry.reset();
}

std::size_t CacheEntryPool::get_per_entry_size_estimate() {
    return sizeof(ttl::Entry) + value_offset_;
}

std::size_t CacheEntryPool::get_max_allowed_memory_for_pool() {
    pool_logger().info("Pool max percent {} Usable memory {}",
                       pool_max_memory_percent_, usable_memory_);
    const std::size_t target_memory = usable_memory_ * pool_max_memory_percent_;
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
    const std::size_t pool_size = target_memory / estimated_entry_size;
    pool_logger().info("Shard Id: {}\n Usable Memory: {} GB \nPool Count: {}\n "
                       "Estimated entry size {} \n",
                       seastar::this_shard_id(),
                       target_memory / (1024.0 * 1024.0 * 1024.0), pool_size,
                       estimated_entry_size);
    return pool_size;
}
