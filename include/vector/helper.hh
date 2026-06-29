#pragma once
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <system_error>
#include <span>
#include <vector>

namespace vdb {
inline bool parse_embedding(std::string_view raw, std::vector<float> &out) {
    out.clear();

    std::size_t pos = 0;
    while (pos < raw.size()) {
        while (pos < raw.size() &&
               (std::isspace(static_cast<unsigned char>(raw[pos])) ||
                raw[pos] == '[' || raw[pos] == ']' || raw[pos] == ',')) {
            ++pos;
        }

        if (pos >= raw.size()) {
            break;
        }

        std::size_t end = pos;
        while (end < raw.size() && raw[end] != ',' && raw[end] != ']') {
            ++end;
        }

        float value = 0.0f;
        const auto *begin = raw.data() + pos;
        const auto *finish = raw.data() + end;
        const auto [ptr, ec] = std::from_chars(begin, finish, value);
        if (ec != std::errc{} || ptr != finish) {
            return false;
        }

        out.push_back(value);
        pos = end;
    }

    return !out.empty();
}

inline float dot_product(std::span<const float> a, std::span<const float> b) {

    if (a.size() != b.size()) {
        throw std::invalid_argument(
            "dot_product: vectors must have equal dimensions");
    }

    float sum = 0.0;

    const std::size_t length = std::min(a.size(), b.size());

    for (std::size_t i = 0; i < length; ++i) {
        sum += a[i] * b[i];
    }

    return sum;
}

inline void normalize(std::span<float> a) {
    double sum_of_squares = 0.0;

    for (const double value : a) {
        sum_of_squares += value * value;
    }

    const double norm = std::sqrt(sum_of_squares);

    if (norm == 0.0) {
        return;
    }

    for (float &value : a) {
        value /= norm;
    }
}

inline double find_cosine_similarity(std::span<const float> embedding_a,
                                     std::span<const float> embedding_b) {
    // Assumes both embeddings are already normalized.
    return dot_product(embedding_a, embedding_b);
}
} // namespace vdb
