// wal.hh
struct wal_record {
    enum clas op: uint8_t { put=1, del=2};
    op o;
    sstring key;
    temporary_buffer<char> val; // empty if del
    uint64_t cas;
    std::optional<int64_t> ttl_ms; // empty if no ttl
};

class wal {
    public:
        seastar::future<> open(unsigned shard_id);
        seastar::future<> append(const wal_record& r);
        seastar::future<> flush();
        seastar::future<> replay(std::function<future<>() yield, std::function<void(wal_record&&)> on);
        seastar::future<> close();
};
