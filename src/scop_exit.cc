#include <functional>

class scope_exit {
    std::function<void()> _fn;
    bool _active = true;

  public:
    explicit scope_exit(std::function<void()> fn) noexcept
        : _fn(std::move(fn)) {}
    ~scope_exit() noexcept {
        if (_active && _fn)
            _fn();
    }

    // non-copyable
    scope_exit(const scope_exit &) = delete;
    scope_exit &operator=(const scope_exit &) = delete;

    // movable
    scope_exit(scope_exit &&other) noexcept
        : _fn(std::move(other._fn)), _active(other._active) {
        other._active = false;
    }
};
