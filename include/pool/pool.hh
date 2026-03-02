#pragma once
#include "ttl/entry.hh"
#include <memory>
#include <seastar/core/future.hh>

struct IEntryPool {
    virtual ~IEntryPool() = default;
    virtual seastar::future<std::unique_ptr<ttl::Entry>> acquire() = 0;
    virtual void release(std::unique_ptr<ttl::Entry>) = 0;
};
