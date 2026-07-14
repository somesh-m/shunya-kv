#include "pool/vector_pool.hh"
#include <memory>
#include <seastar/core/coroutine.hh>
#include <seastar/core/shard_id.hh>
#include <stdexcept>
#include <vector>

seastar::future<> EntryPool::init() {
    if (initialized_) {
        co_return;
    }

    co_await grow(initial_count_);
    initialized_ = true;
}

seastar::future<std::unique_ptr<shunyakv::vdb::Entry>> EntryPool::acquire() {
    if (pool_.empty() || pool_.size() <= free_slots_low_watermark_) {
        co_await grow(growth_count_);
    }

    auto entry = std::move(pool_.front());
    pool_.pop_front();
    entry->in_use_ = true;

    co_return entry;
}

void EntryPool::release(std::unique_ptr<shunyakv::vdb::Entry> entry) {
    if (!entry) {
        return;
    }

    PoolTraits<shunyakv::vdb::Entry>::reset(*entry, cfg_);
    pool_.push_back(std::move(entry));
}

seastar::future<> EntryPool::grow(std::size_t count) {
    if (count == 0) {
        throw std::runtime_error("VECTOR_POOL_GROW_COUNT_IS_ZERO");
    }

    using Entry = shunyakv::vdb::Entry;
    const std::size_t bytes = PoolTraits<Entry>::estimated_size(cfg_) * count;

    if (!memory_manager_.try_reserve(bytes)) {
        throw std::runtime_error("VECTOR_POOL_MEMORY_LIMIT_REACHED");
    }

    try {
        std::vector<std::unique_ptr<Entry>> new_entries;
        new_entries.reserve(count);

        for (std::size_t i = 0; i < count; ++i) {
            auto entry = std::make_unique<Entry>(
                seastar::sstring{}, std::string{}, std::vector<float>{},
                shunyakv::centroid_id{0});
            PoolTraits<Entry>::initialize(*entry, cfg_);
            new_entries.push_back(std::move(entry));
        }

        pool_.reserve(pool_.size() + count);
        for (auto &entry : new_entries) {
            pool_.push_back(std::move(entry));
        }
    } catch (...) {
        memory_manager_.release(bytes);
        throw;
    }

    total_allocated_ += count;
    vector_logger().info(
        "Shard Id: {}; grown vector slots: {}; total free slots: {}; total "
        "allocated: {}",
        seastar::this_shard_id(), count, pool_.size(), total_allocated_);

    co_return;
}
