#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <stdexcept>

#include <dbconfig.hh>
#include <pool/pool_traits.hh>
#include <pool/shard_memory_manager.hh>

#include <seastar/core/circular_buffer.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/shard_id.hh>
#include <seastar/util/log.hh>

inline seastar::logger &object_pool_template_logger() {
    static seastar::logger logger{"object_pool_template"};
    return logger;
}

template <typename T> class ObjectPool {
  public:
    ObjectPool(pool::ShardMemoryManager &memory, const db_config &cfg,
               std::size_t initial_count, std::size_t growth_count)
        : memory_manager_(memory), cfg_(cfg), initial_count_(initial_count),
          growth_count_(growth_count),
          free_slots_low_watermark_(
              std::max<std::size_t>(1, growth_count / 10)) {}

    seastar::future<> init() { co_await grow(initial_count_); }

    seastar::future<std::unique_ptr<T>> acquire() {
        if (pool_.empty()) {
            co_await grow(growth_count_);
        } else if (pool_.size() <= free_slots_low_watermark_) {
            co_await grow(growth_count_);
        }

        auto obj = std::move(pool_.front());
        pool_.pop_front();

        co_return obj;
    }

    void release(std::unique_ptr<T> obj) {
        if (!obj) {
            return;
        }

        PoolTraits<T>::reset(*obj, memory_manager_);
        pool_.push_back(std::move(obj));
    }

    std::size_t available() const { return pool_.size(); }

    std::size_t total_allocated() const { return total_allocated_; }

    std::size_t used() const { return total_allocated_ - pool_.size(); }

  private:
    pool::ShardMemoryManager &memory_manager_;
    const db_config &cfg_;

    seastar::circular_buffer<std::unique_ptr<T>> pool_;

    std::size_t initial_count_ = 0;
    std::size_t growth_count_ = 0;
    std::size_t free_slots_low_watermark_ = 1;
    std::size_t total_allocated_ = 0;

    seastar::future<> grow(std::size_t count) {
        if (count == 0) {
            throw std::runtime_error("OBJECT_POOL_GROW_COUNT_IS_ZERO");
        }

        const std::size_t bytes = PoolTraits<T>::estimated_size(cfg_) * count;

        if (!memory_manager_.try_reserve(bytes)) {
            throw std::runtime_error("OBJECT_POOL_MEMORY_LIMIT_REACHED");
        }

        pool_.reserve(pool_.size() + count);

        for (std::size_t i = 0; i < count; ++i) {
            auto obj = std::make_unique<T>();

            PoolTraits<T>::initialize(*obj, memory_manager_);

            pool_.push_back(std::move(obj));

            if (i % 100 == 0) {
                co_await seastar::coroutine::maybe_yield();
            }
        }

        total_allocated_ += count;

        object_pool_template_logger().info(
            "Shard Id: {}; grown slots: {}; total free slots: {}; total "
            "allocated: {}",
            seastar::this_shard_id(), count, pool_.size(), total_allocated_);

        co_return;
    }
};
