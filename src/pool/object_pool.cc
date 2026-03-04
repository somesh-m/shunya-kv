#include "pool/object_pool.hh"
#include <seastar/core/coroutine.hh>

seastar::future<> CacheEntryPool::prepopulate_pool() {
    for (std::size_t i = 0; i < max_size_; i++) {
        pool_.push_back(std::make_unique<ttl::Entry>());
        if (i % 100 == 0) {
            co_await seastar::coroutine::maybe_yield();
        }
    }
    co_return;
}

std::size_t CacheEntryPool::get_available_slots() { return pool_.size(); }
std::size_t CacheEntryPool::get_total_slots() { return max_size_; }

seastar::future<std::unique_ptr<ttl::Entry>> CacheEntryPool::acquire() {
    if (pool_.empty()) {
        auto entry = std::make_unique<ttl::Entry>();
        entry->in_use_ = true;
        co_return entry;
    }

    auto entry = std::move(pool_.front());
    pool_.pop_front();
    entry->in_use_ = true;
    co_return entry;
}

void CacheEntryPool::release(std::unique_ptr<ttl::Entry> entry) {
    if (!entry) {
        return;
    }

    entry->in_use_ = false;
    entry->visited = false;
    entry->value.clear();
    entry->key = "";
    entry->expires_at = 0;
    entry->ver = 0;
    entry->heat = 0;
    entry->last_access = 0;

    if (pool_.size() < max_size_) {
        pool_.push_back(std::move(entry));
    }
}

std::size_t CacheEntryPool::get_per_entry_size_estimate() {
    return sizeof(ttl::Entry) + value_offset_;
}

std::size_t CacheEntryPool::get_usable_memory() {
    const std::size_t total_memory = seastar::memory::stats().total_memory();
    const std::size_t target_memory =
        total_memory * allowed_percentage_total_mem_;
    return target_memory;
}

std::size_t CacheEntryPool::calculate_optimal_pool_size() noexcept {
    const std::size_t target_memory = get_usable_memory();
    const std::size_t estimated_entry_size = get_per_entry_size_estimate();
    const std::size_t pool_size = target_memory / estimated_entry_size;
    pool_logger().info("Shard Id: {} Usable Memory: {} GB Pool Count: {}",
                       seastar::this_shard_id(),
                       target_memory / (1024 * 1024 * 1024), pool_size);
    return pool_size;
}
