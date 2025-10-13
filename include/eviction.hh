// eviction.hh
class eviction_policy {
  public:
    virtual void touch(const sstring &key, size_t sz) = 0;
    virtual void on_insert(const sstring &key, size_t sz) = 0;
    virtual std::optional<sstring> victim() = 0;
    virtual ~eviction_policy() = default;
};

std : unique_ptr<eviction_policy> make_clock_pro(size_t bytes_budget);
