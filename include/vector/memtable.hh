#pragma once

#include <absl/container/flat_hash_set.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <queue>
#include <random>
#include <seastar/core/semaphore.hh>
#include <seastar/coroutine/maybe_yield.hh>
#include <utility>
#include <vector>

#include "vector/helper.hh"
#include "vector/search_result.hh"
#include "vector/vector_storage_manager.hh"
#include "vector/vector_types.hh"

namespace shunyakv {

class Memtable {
  private:
    using scored_node = std::pair<std::size_t, float>;

    static constexpr std::size_t threshold_ = 7000;
    static constexpr std::size_t yield_every_ =
        20; // Increased to protect tight CPU loops from scheduling overhead
    static constexpr std::size_t max_immutable_indexes_ = 5;

    VectorStorageManager &storage_;
    mutable seastar::semaphore operation_sem_{1};
    absl::flat_hash_set<GenerationId> entries_;
    std::deque<absl::flat_hash_set<GenerationId>> pending_compilation_queue_;
    std::vector<std::shared_ptr<HnswIndex>> immutable_indexes_;

    seastar::future<> build_hnsw_index();
    seastar::future<> compact_hnsw_indexes();
    seastar::future<> rebuild_hnsw_index(HnswIndex &index);
    seastar::future<> insert_into_hnsw_index(GenerationId id, HnswIndex &index);
    void connect_bidirectional(std::size_t a, std::size_t b, std::size_t level,
                               HnswIndex &index);
    void prune_neighbours(std::size_t node_id, std::size_t level,
                          HnswIndex &index);

    bool is_valid_hnsw_node(std::size_t node_id, const HnswIndex &index) const;
    bool is_deleted_hnsw_node(std::size_t node_id,
                              const HnswIndex &index) const;
    bool has_hnsw_level(std::size_t node_id, std::size_t level,
                        const HnswIndex &index) const;
    float score_hnsw_node(const std::vector<float> &query, std::size_t node_id,
                          const HnswIndex &index) const;
    seastar::future<std::size_t>
    greedy_search_vector(const std::vector<float> &query,
                         std::size_t entry_point, std::size_t level,
                         const HnswIndex &index) const;
    seastar::future<std::vector<scored_node>>
    search_layer_vector(const std::vector<float> &query,
                        std::size_t entry_point, std::size_t ef,
                        std::size_t level, const HnswIndex &index) const;

    static std::vector<std::size_t>
    select_top_m(std::vector<scored_node> candidates, std::size_t m);
    static std::size_t random_level(std::size_t m, std::size_t max_level);

  public:
    explicit Memtable(VectorStorageManager &storage) : storage_(storage) {
        entries_.reserve(threshold_);
    }

    Memtable(const Memtable &) = delete;
    Memtable &operator=(const Memtable &) = delete;

    std::size_t size() const noexcept { return entries_.size(); }

    seastar::future<bool> insert(GenerationId id);
    seastar::future<bool> erase(GenerationId id);
    seastar::future<std::vector<VectorSearchResult>>
    search(const std::vector<float> &embedding, std::size_t top_k) const;
};

inline seastar::future<bool> Memtable::insert(GenerationId id) {

    auto operation_units = co_await seastar::get_units(operation_sem_, 1);
    if (storage_.get(id) == nullptr) {
        co_return false;
    }

    const auto [_, inserted] = entries_.insert(id);
    if (!inserted) {
        co_return false;
    }

    if (entries_.size() >= threshold_) {
        pending_compilation_queue_.push_back(std::move(entries_));
        entries_.clear();
        entries_.reserve(threshold_);
        (void)build_hnsw_index().handle_exception([](std::exception_ptr ep) {
            try {
                std::rethrow_exception(ep);
            } catch (const std::exception &e) {
                std::cerr << "HNSW index build failed: " << e.what() << '\n';
            }
        });
    }
    co_return true;
}

inline seastar::future<> Memtable::build_hnsw_index() {
    while (!pending_compilation_queue_.empty()) {
        auto members = std::move(pending_compilation_queue_.front());
        pending_compilation_queue_.pop_front();

        auto index = std::make_shared<HnswIndex>();
        index->neighbours.reserve(members.size());
        index->member_keys.reserve(members.size());
        index->key_to_node.reserve(members.size());
        index->deleted.reserve(members.size());

        std::size_t inserted_count = 0;
        for (const GenerationId id : members) {
            co_await insert_into_hnsw_index(id, *index);
            if (++inserted_count % yield_every_ == 0) {
                co_await seastar::coroutine::maybe_yield();
            }
        }
        if (!index->member_keys.empty()) {
            immutable_indexes_.push_back(std::move(index));
            // if (immutable_indexes_.size() > max_immutable_indexes_) {
            //     co_await compact_hnsw_indexes();
            // }
        }
    }
    co_return;
}

inline seastar::future<> Memtable::compact_hnsw_indexes() {
    if (immutable_indexes_.size() <= max_immutable_indexes_) {
        co_return;
    }

    std::size_t live_count = 0;
    for (const auto &index : immutable_indexes_) {
        live_count += index->member_keys.size() - index->tombstone_count;
    }

    auto compacted = std::make_shared<HnswIndex>();
    const HnswIndex &config_source = *immutable_indexes_.front();
    compacted->m = config_source.m;
    compacted->ef_construction = config_source.ef_construction;
    compacted->ef_search = config_source.ef_search;
    compacted->neighbours.reserve(live_count);
    compacted->member_keys.reserve(live_count);
    compacted->key_to_node.reserve(live_count);
    compacted->deleted.reserve(live_count);

    std::size_t inserted_count = 0;
    const auto immutable_indexes_snapshot = immutable_indexes_;
    for (const auto &index : immutable_indexes_snapshot) {
        for (std::size_t node_id = 0; node_id < index->member_keys.size();
             ++node_id) {
            if (is_deleted_hnsw_node(node_id, *index)) {
                continue;
            }
            const GenerationId id = index->member_keys[node_id];
            if (storage_.get(id) == nullptr) {
                continue;
            }
            co_await insert_into_hnsw_index(id, *compacted);
            if (++inserted_count % yield_every_ == 0) {
                co_await seastar::coroutine::maybe_yield();
            }
        }
    }

    immutable_indexes_.clear();
    if (!compacted->member_keys.empty()) {
        immutable_indexes_.push_back(std::move(compacted));
    }
    co_return;
}

inline seastar::future<> Memtable::insert_into_hnsw_index(GenerationId id,
                                                          HnswIndex &index) {
    const VectorEntry *entry = storage_.get(id);
    if (entry == nullptr || index.key_to_node.contains(id)) {
        co_return;
    }

    const auto &embedding = entry->embedding;
    const std::size_t new_level = random_level(index.m, 8);
    const std::size_t new_id = index.member_keys.size();

    index.member_keys.push_back(id);
    index.key_to_node.emplace(id, new_id);
    index.deleted.push_back(false);
    index.neighbours.emplace_back(new_level + 1);

    if (!index.entry_point.has_value()) {
        index.entry_point = new_id;
        index.max_level = new_level;
        co_return;
    }

    std::size_t ep = *index.entry_point;
    for (std::size_t level = index.max_level; level > new_level; --level) {
        ep = co_await greedy_search_vector(embedding, ep, level, index);
    }

    const std::size_t start_level = std::min(new_level, index.max_level);
    for (std::size_t level = start_level + 1; level-- > 0;) {
        auto candidates = co_await search_layer_vector(
            embedding, ep, index.ef_construction, level, index);
        const auto selected = select_top_m(std::move(candidates), index.m);
        for (const std::size_t neighbour : selected) {
            connect_bidirectional(new_id, neighbour, level, index);
        }
        if (!selected.empty()) {
            ep = selected.front();
        }
    }

    if (new_level > index.max_level) {
        index.entry_point = new_id;
        index.max_level = new_level;
    }
    co_return;
}

inline bool Memtable::is_valid_hnsw_node(std::size_t node_id,
                                         const HnswIndex &index) const {
    return node_id < index.member_keys.size() &&
           node_id < index.neighbours.size() && node_id < index.deleted.size();
}

inline bool Memtable::is_deleted_hnsw_node(std::size_t node_id,
                                           const HnswIndex &index) const {
    return node_id >= index.deleted.size() || index.deleted[node_id];
}

inline bool Memtable::has_hnsw_level(std::size_t node_id, std::size_t level,
                                     const HnswIndex &index) const {
    return is_valid_hnsw_node(node_id, index) &&
           level < index.neighbours[node_id].size();
}

inline float Memtable::score_hnsw_node(const std::vector<float> &query,
                                       std::size_t node_id,
                                       const HnswIndex &index) const {
    if (!is_valid_hnsw_node(node_id, index) ||
        is_deleted_hnsw_node(node_id, index)) {
        return -std::numeric_limits<float>::infinity();
    }
    const VectorEntry *entry = storage_.get(index.member_keys[node_id]);
    if (entry == nullptr) {
        return -std::numeric_limits<float>::infinity();
    }
    return static_cast<float>(
        ::vdb::find_cosine_similarity(query, entry->embedding));
}

inline seastar::future<std::size_t>
Memtable::greedy_search_vector(const std::vector<float> &query,
                               std::size_t entry_point, std::size_t level,
                               const HnswIndex &index) const {
    if (!has_hnsw_level(entry_point, level, index)) {
        co_return entry_point;
    }

    std::size_t current = entry_point;
    float current_score = score_hnsw_node(query, current, index);
    std::size_t scored_count = 0;
    bool changed = true;
    while (changed) {
        changed = false;
        if (!has_hnsw_level(current, level, index)) {
            break;
        }
        for (const std::size_t neighbour : index.neighbours[current][level]) {
            if (++scored_count % yield_every_ == 0) {
                co_await seastar::coroutine::maybe_yield();
            }
            if (!has_hnsw_level(neighbour, level, index)) {
                continue;
            }
            const float score = score_hnsw_node(query, neighbour, index);
            if (score > current_score) {
                current = neighbour;
                current_score = score;
                changed = true;
            }
        }
    }
    co_return current;
}

inline seastar::future<std::vector<Memtable::scored_node>>
Memtable::search_layer_vector(const std::vector<float> &query,
                              std::size_t entry_point, std::size_t ef,
                              std::size_t level, const HnswIndex &index) const {
    if (ef == 0 || !has_hnsw_level(entry_point, level, index)) {
        co_return std::vector<scored_node>{};
    }

    struct CandidateLess {
        bool operator()(const scored_node &a, const scored_node &b) const {
            return a.second < b.second;
        }
    };
    struct ResultGreater {
        bool operator()(const scored_node &a, const scored_node &b) const {
            return a.second > b.second;
        }
    };

    std::priority_queue<scored_node, std::vector<scored_node>, CandidateLess>
        candidates;
    std::priority_queue<scored_node, std::vector<scored_node>, ResultGreater>
        nearest;
    absl::flat_hash_set<std::size_t> visited;

    const float entry_score = score_hnsw_node(query, entry_point, index);
    candidates.emplace(entry_point, entry_score);
    nearest.emplace(entry_point, entry_score);
    visited.insert(entry_point);

    std::size_t scored_count = 0;
    while (!candidates.empty()) {
        const auto current = candidates.top();
        candidates.pop();
        if (nearest.size() >= ef && current.second < nearest.top().second) {
            break;
        }

        for (const std::size_t neighbour :
             index.neighbours[current.first][level]) {
            if (++scored_count % yield_every_ == 0) {
                co_await seastar::coroutine::maybe_yield();
            }
            if (!visited.insert(neighbour).second ||
                !has_hnsw_level(neighbour, level, index)) {
                continue;
            }
            const float score = score_hnsw_node(query, neighbour, index);
            if (nearest.size() < ef || score > nearest.top().second) {
                candidates.emplace(neighbour, score);
                nearest.emplace(neighbour, score);
                if (nearest.size() > ef) {
                    nearest.pop();
                }
            }
        }
    }

    std::vector<scored_node> result;
    result.reserve(nearest.size());
    while (!nearest.empty()) {
        result.push_back(nearest.top());
        nearest.pop();
    }
    std::sort(result.begin(), result.end(),
              [](const scored_node &a, const scored_node &b) {
                  return a.second > b.second;
              });
    co_return result;
}

inline std::vector<std::size_t>
Memtable::select_top_m(std::vector<scored_node> candidates, std::size_t m) {
    if (candidates.size() > m) {
        candidates.resize(m);
    }
    std::vector<std::size_t> selected;
    selected.reserve(candidates.size());
    for (const auto &[node, _] : candidates) {
        selected.push_back(node);
    }
    return selected;
}

inline std::size_t Memtable::random_level(std::size_t m,
                                          std::size_t max_level) {
    if (m <= 1) {
        return 0;
    }
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    const double probability = 1.0 / static_cast<double>(m);
    std::size_t level = 0;
    while (level < max_level && dist(rng) < probability) {
        ++level;
    }
    return level;
}

inline void Memtable::connect_bidirectional(std::size_t a, std::size_t b,
                                            std::size_t level,
                                            HnswIndex &index) {
    if (!has_hnsw_level(a, level, index) || !has_hnsw_level(b, level, index) ||
        a == b) {
        return;
    }
    index.neighbours[a][level].push_back(b);
    index.neighbours[b][level].push_back(a);
    prune_neighbours(a, level, index);
    prune_neighbours(b, level, index);
}

inline void Memtable::prune_neighbours(std::size_t node_id, std::size_t level,
                                       HnswIndex &index) {
    if (!has_hnsw_level(node_id, level, index)) {
        return;
    }
    const VectorEntry *node = storage_.get(index.member_keys[node_id]);
    if (node == nullptr) {
        index.neighbours[node_id][level].clear();
        return;
    }

    auto &neighbours = index.neighbours[node_id][level];
    std::sort(neighbours.begin(), neighbours.end());
    neighbours.erase(std::unique(neighbours.begin(), neighbours.end()),
                     neighbours.end());
    if (neighbours.size() <= index.m) {
        return;
    }

    const std::vector<std::size_t> old_neighbours = neighbours;
    std::vector<scored_node> scored;
    scored.reserve(neighbours.size());
    for (const std::size_t neighbour : neighbours) {
        const float score = score_hnsw_node(node->embedding, neighbour, index);
        if (std::isfinite(score)) {
            scored.emplace_back(neighbour, score);
        }
    }
    std::sort(scored.begin(), scored.end(),
              [](const scored_node &a, const scored_node &b) {
                  return a.second > b.second;
              });
    if (scored.size() > index.m) {
        scored.resize(index.m);
    }
    neighbours.clear();
    for (const auto &[neighbour, _] : scored) {
        neighbours.push_back(neighbour);
    }

    absl::flat_hash_set<std::size_t> retained(neighbours.begin(),
                                              neighbours.end());
    for (const std::size_t removed : old_neighbours) {
        if (retained.contains(removed) ||
            !has_hnsw_level(removed, level, index)) {
            continue;
        }
        auto &reverse = index.neighbours[removed][level];
        reverse.erase(std::remove(reverse.begin(), reverse.end(), node_id),
                      reverse.end());
    }
}

inline seastar::future<> Memtable::rebuild_hnsw_index(HnswIndex &index) {
    std::vector<GenerationId> live;
    live.reserve(index.member_keys.size() - index.tombstone_count);
    for (std::size_t i = 0; i < index.member_keys.size(); ++i) {
        if (!is_deleted_hnsw_node(i, index) &&
            storage_.get(index.member_keys[i]) != nullptr) {
            live.push_back(index.member_keys[i]);
        }
    }

    const std::size_t m = index.m;
    const std::size_t ef_construction = index.ef_construction;
    const std::size_t ef_search = index.ef_search;
    index = HnswIndex{};
    index.m = m;
    index.ef_construction = ef_construction;
    index.ef_search = ef_search;
    index.neighbours.reserve(live.size());
    index.member_keys.reserve(live.size());
    index.key_to_node.reserve(live.size());
    index.deleted.reserve(live.size());
    std::size_t inserted_count = 0;
    for (const GenerationId id : live) {
        co_await insert_into_hnsw_index(id, index);
        if (++inserted_count % yield_every_ == 0) {
            co_await seastar::coroutine::maybe_yield();
        }
    }
    co_return;
}

inline seastar::future<bool> Memtable::erase(GenerationId id) {
    auto operation_units = co_await seastar::get_units(operation_sem_, 1);
    if (entries_.erase(id) != 0) {
        co_return true;
    }
    for (auto &index : immutable_indexes_) {
        const auto it = index->key_to_node.find(id);
        if (it == index->key_to_node.end()) {
            continue;
        }
        // Amortized O(1) delete: mark tombstones and rely on compaction for
        // cleanup
        index->deleted[it->second] = true;
        index->key_to_node.erase(it);
        ++index->tombstone_count;
        co_return true;
    }
    co_return false;
}

inline seastar::future<std::vector<VectorSearchResult>>
Memtable::search(const std::vector<float> &embedding, std::size_t top_k) const {
    auto operation_units = co_await seastar::get_units(operation_sem_, 1);
    if (top_k == 0) {
        co_return std::vector<VectorSearchResult>{};
    }

    struct ScoredEntry {
        GenerationId id;
        float score;
    };
    struct ScoreGreater {
        bool operator()(const ScoredEntry &a, const ScoredEntry &b) const {
            return a.score > b.score;
        }
    };
    std::priority_queue<ScoredEntry, std::vector<ScoredEntry>, ScoreGreater>
        winners;

    const auto consider = [&](GenerationId id, float score) {
        winners.push(ScoredEntry{.id = id, .score = score});
        if (winners.size() > top_k) {
            winners.pop();
        }
    };

    std::size_t scored_count = 0;
    // This loop is to find the closest one in the working set
    for (const GenerationId id : entries_) {
        const VectorEntry *entry = storage_.get(id);
        if (entry != nullptr) {
            consider(id, static_cast<float>(::vdb::find_cosine_similarity(
                             embedding, entry->embedding)));
        }
        if (++scored_count % yield_every_ == 0) {
            co_await seastar::coroutine::maybe_yield();
        }
    }

    const auto immutable_indexes_snapshot = immutable_indexes_;
    for (const auto &index : immutable_indexes_snapshot) {
        if (!index->entry_point.has_value()) {
            continue;
        }
        std::size_t ep = *index->entry_point;
        for (std::size_t level = index->max_level; level > 0; --level) {
            ep = co_await greedy_search_vector(embedding, ep, level, *index);
        }
        const std::size_t ef = std::max(top_k, index->ef_search);
        const auto candidates =
            co_await search_layer_vector(embedding, ep, ef, 0, *index);
        for (const auto &[node_id, score] : candidates) {
            // Filter out tombstones returned during traversal
            if (!is_deleted_hnsw_node(node_id, *index)) {
                consider(index->member_keys[node_id], score);
            }
        }
    }

    std::vector<VectorSearchResult> results;
    results.reserve(winners.size());
    while (!winners.empty()) {
        const ScoredEntry winner = winners.top();
        winners.pop();
        const VectorEntry *entry = storage_.get(winner.id);
        if (entry != nullptr) {
            results.push_back(VectorSearchResult{
                winner.score, std::string(entry->key.data(), entry->key.size()),
                entry->value});
        }
    }
    std::reverse(results.begin(), results.end());
    co_return results;
}

} // namespace shunyakv
