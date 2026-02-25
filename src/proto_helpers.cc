// Intentionally empty for now.
// Everything is inlined for zero overhead and easy use across shards.
// Add non-inline helpers here later if needed.
#include <chrono>
#include <cstdint>

inline uint64_t now_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch())
        .count();
}

inline uint64_t now_s() {
    using namespace std::chrono;
    return duration_cast<seconds>(steady_clock::now().time_since_epoch())
        .count();
}
