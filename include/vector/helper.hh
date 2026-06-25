#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace vdb {
inline float dot_product(std::span<float> a, std::span<float> b) {

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

    for (double &value : a) {
        value /= norm;
    }
}

inline double find_cosine_similarity(std::span<float> embedding_a,
                                     std::span<float> embedding_b) {
    // Assumes both embeddings are already normalized.
    return dot_product(embedding_a, embedding_b);
}
} // namespace vdb
