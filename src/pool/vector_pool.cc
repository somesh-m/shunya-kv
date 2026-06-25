#include "pool/vector_pool.hh"
#include <memory>
#include <seastar/core/coroutine.hh>

seastar::future<std::unique_ptr<vdb::Entry>> EntryPool::acquire() {
    co_return co_await pool_.acquire();
}

void EntryPool::release(std::unique_ptr<vdb::Entry> entry) {
    pool_.release(std::move(entry));
}
