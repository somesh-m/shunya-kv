#include "pool/object_pool.hh"

void CacheEntryPool::prepopulate_pool() {
    for (std::size_t i = 0; i < max_size_; i++) {
        pool_.push_back(std::make_unique<ttl::Entry>());
    }
}

std::size_t CacheEntryPool::get_available_slots() { return pool_.size(); }
std::size_t CacheEntryPool::get_total_slots() { return max_size_; }

seastar::future<std::unique_ptr<ttl::Entry>> CacheEntryPool::acquire() {
    if (pool_.empty()) {
        auto entry = std::make_unique<ttl::Entry>();
        entry->in_use = true;
        co_return entry;
    }

    auto entry = std::move(pool_.front());
    pool_.pop_front();
    entry->in_use = true;
    co_return entry;
}

void CacheEntryPool::release(std::unique_ptr<ttl::Entry> entry) {
    if (!entry) {
        return;
    }

    entry->in_use = false;
    entry->value.clear();
    entry->expires_at = 0;
    entry->ver = 0;
    entry->heat = 0;
    entry->last_access = 0;

    if (pool_.size() < max_size_) {
        pool_.push_back(std::move(entry));
    }
}

std::size_t CacheEntryPool::calculate_optimal_pool_size() noexcept {
    const std::size_t total_memory = seastar::memory::stats().total_memory();
    const std::size_t target_memory = total_memory * 0.8;
    const std::size_t estimated_entry_size = sizeof(ttl::Entry) + 4096;
    const std::size_t pool_size = target_memory / estimated_entry_size;
    pool_logger().info("Shard Id: {} Assigned Memory: {} GB Pool Count: {}",
                       seastar::this_shard_id(),
                       total_memory / (1024 * 1024 * 1024), pool_size);
    return pool_size;
}
