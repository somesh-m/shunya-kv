#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>

inline uint64_t fnv1a64(std::string_view s) {
    uint64_t h =
        14695981039346656037ull; // 0xcbf29ce484222325  (NOTE the trailing 7)
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull; // 0x100000001b3
    }
    return h;
}

inline unsigned shard_for(std::string_view key) noexcept {
    const unsigned smp = seastar::smp::count;
    const unsigned first = g_first_data_shard;
    if (smp <= first) [[unlikely]] {
        return first; // degenerate: no data shards configured
    }
    const unsigned D = smp - first; // number of data shards
    const uint64_t h = fnv1a64(key);
    const unsigned idx = unsigned(((__uint128_t)h * D) >> 64); // [0..D)
    return first + idx;                                        // shard id
}

int main() {
    std::cout << fnv1a64("name") << "\n";
    return 0;
}
