// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/bridge_map.hpp"
#include "etil/evolution/evolve_logger.hpp"

#include <algorithm>
#include <iomanip>
#include <queue>
#include <sstream>
#include <unordered_set>

namespace etil::evolution {

using T = core::TypeSignature::Type;

const std::vector<BridgeEdge> BridgeMap::empty_edges_;

void BridgeMap::add(T from, T to, const std::string& word) {
    graph_[static_cast<int>(from)].push_back({from, to, word});
    edge_count_++;
    finalized_ = false;
}

void BridgeMap::finalize() {
    finalized_ = true;
}

const std::vector<BridgeEdge>& BridgeMap::conversions_from(T from) const {
    auto it = graph_.find(static_cast<int>(from));
    if (it != graph_.end()) return it->second;
    return empty_edges_;
}

std::vector<std::string> BridgeMap::find_bridge(T from, T to) const {
    std::vector<std::string> result;
    auto it = graph_.find(static_cast<int>(from));
    if (it == graph_.end()) return result;
    for (const auto& edge : it->second) {
        if (edge.to == to) {
            result.push_back(edge.word);
        }
    }
    return result;
}

std::vector<std::string> BridgeMap::find_path(T from, T to, size_t max_hops) const {
    if (from == to) return {};

    // BFS through the type graph
    struct PathNode {
        T type;
        std::vector<std::string> words;
    };

    std::queue<PathNode> queue;
    std::unordered_set<int> visited;

    queue.push({from, {}});
    visited.insert(static_cast<int>(from));

    while (!queue.empty()) {
        auto current = queue.front();
        queue.pop();

        if (current.words.size() >= max_hops) continue;

        auto it = graph_.find(static_cast<int>(current.type));
        if (it == graph_.end()) continue;

        for (const auto& edge : it->second) {
            if (edge.to == to) {
                auto path = current.words;
                path.push_back(edge.word);
                return path;
            }
            if (visited.find(static_cast<int>(edge.to)) == visited.end()) {
                visited.insert(static_cast<int>(edge.to));
                auto path = current.words;
                path.push_back(edge.word);
                queue.push({edge.to, path});
            }
        }
    }
    return {};
}

std::vector<std::string> BridgeMap::select_path(T from, T to, size_t max_hops) {
    // When TBBP disabled, fall through to deterministic BFS
    if (!tbbp_enabled_) {
        return find_path(from, to, max_hops);
    }

    if (from == to) return {};

    // Candidate path tracks both the word sequence and the edges traversed,
    // so we can record usages after selection.
    struct Candidate {
        double weight;
        std::vector<std::string> words;
        std::vector<EdgeRef> edges;
    };
    std::vector<Candidate> candidates;

    auto it_from = graph_.find(static_cast<int>(from));
    if (it_from != graph_.end()) {
        for (const auto& edge : it_from->second) {
            if (edge.to == to) {
                candidates.push_back({
                    edge.weight,
                    {edge.word},
                    {{edge.from, edge.to, edge.word}}
                });
            }
        }
    }

    if (candidates.empty() && max_hops >= 2 && it_from != graph_.end()) {
        for (const auto& edge1 : it_from->second) {
            if (edge1.to == to) continue;
            auto it_mid = graph_.find(static_cast<int>(edge1.to));
            if (it_mid == graph_.end()) continue;
            for (const auto& edge2 : it_mid->second) {
                if (edge2.to == to) {
                    candidates.push_back({
                        edge1.weight * edge2.weight,
                        {edge1.word, edge2.word},
                        {{edge1.from, edge1.to, edge1.word},
                         {edge2.from, edge2.to, edge2.word}}
                    });
                }
            }
        }
    }

    if (candidates.empty()) return {};

    size_t chosen_idx = 0;
    if (candidates.size() > 1) {
        std::vector<double> weights;
        weights.reserve(candidates.size());
        for (const auto& c : candidates) weights.push_back(c.weight);
        std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
        chosen_idx = dist(rng_);
    }

    // Record usage of every edge in the chosen path
    for (const auto& e : candidates[chosen_idx].edges) {
        record_usage(e.from, e.to, e.word);
    }

    // Log selection
    if (logger_ && logger_->enabled(EvolveLogCategory::Bridge)) {
        std::ostringstream msg;
        msg << "select: " << type_name(from) << "->" << type_name(to) << " chose ";
        bool first = true;
        for (const auto& w : candidates[chosen_idx].words) {
            if (!first) msg << " + ";
            msg << "'" << w << "'";
            first = false;
        }
        msg << " (w=" << std::fixed << std::setprecision(3)
            << candidates[chosen_idx].weight << ", "
            << candidates.size() << " candidate"
            << (candidates.size() == 1 ? "" : "s") << ")";
        logger_->log(EvolveLogCategory::Bridge, msg.str());
    }

    return candidates[chosen_idx].words;
}

bool BridgeMap::set_edge_weight(T from, T to, const std::string& word, double weight) {
    auto* edge = find_edge_mut(from, to, word);
    if (!edge) return false;
    edge->weight = weight;
    return true;
}

BridgeEdge* BridgeMap::find_edge_mut(T from, T to, const std::string& word) {
    auto it = graph_.find(static_cast<int>(from));
    if (it == graph_.end()) return nullptr;
    for (auto& edge : it->second) {
        if (edge.to == to && edge.word == word) {
            return &edge;
        }
    }
    return nullptr;
}

// --- TBBP per-mutation tracking ---

void BridgeMap::begin_mutation() {
    if (!tbbp_enabled_) return;
    current_mutation_usages_.clear();
}

void BridgeMap::record_usage(T from, T to, const std::string& word) {
    if (!tbbp_enabled_) return;
    current_mutation_usages_.push_back({from, to, word});
    auto* edge = find_edge_mut(from, to, word);
    if (edge) edge->selections++;
}

void BridgeMap::end_mutation(double reward) {
    if (!tbbp_enabled_) return;
    bool log_updates = logger_ && logger_->enabled(EvolveLogCategory::Bridge);
    for (const auto& ref : current_mutation_usages_) {
        auto* edge = find_edge_mut(ref.from, ref.to, ref.word);
        if (!edge) continue;
        double before = edge->weight;
        double updated = (1.0 - alpha_) * before + alpha_ * reward;
        if (updated < min_weight_) updated = min_weight_;
        edge->weight = updated;
        if (reward > 0.5) edge->successes++;

        if (log_updates) {
            std::ostringstream msg;
            msg << "update: '" << edge->word << "' "
                << std::fixed << std::setprecision(3) << before
                << " -> " << updated
                << " (reward=" << reward << ")";
            logger_->log(EvolveLogCategory::Bridge, msg.str());
        }
    }
    current_mutation_usages_.clear();
}

void BridgeMap::log_weight_summary(size_t top_n) const {
    if (!logger_ || !logger_->enabled(EvolveLogCategory::Bridge)) return;

    // Collect all edges with selection counts
    struct Entry {
        const BridgeEdge* edge;
        double rate;
    };
    std::vector<Entry> entries;
    for (const auto& [_, edges] : graph_) {
        for (const auto& e : edges) {
            if (e.selections == 0) continue;  // skip unused bridges
            double rate = (e.selections > 0)
                ? static_cast<double>(e.successes) / e.selections
                : 0.0;
            entries.push_back({&e, rate});
        }
    }

    // Sort by weight descending
    std::sort(entries.begin(), entries.end(),
        [](const Entry& a, const Entry& b) {
            return a.edge->weight > b.edge->weight;
        });

    std::ostringstream header;
    header << "summary: top " << std::min(top_n, entries.size())
           << " of " << entries.size() << " used bridges:";
    logger_->log(EvolveLogCategory::Bridge, header.str());

    for (size_t i = 0; i < std::min(top_n, entries.size()); ++i) {
        const auto& e = *entries[i].edge;
        std::ostringstream msg;
        msg << "  " << e.word << " (w=" << std::fixed << std::setprecision(3)
            << e.weight << ", " << e.selections << " sel, "
            << e.successes << " succ, "
            << std::setprecision(0) << (entries[i].rate * 100.0) << "% rate)";
        logger_->log(EvolveLogCategory::Bridge, msg.str());
    }
}

bool BridgeMap::has_conversions(T from) const {
    auto it = graph_.find(static_cast<int>(from));
    return it != graph_.end() && !it->second.empty();
}

std::string BridgeMap::summary() const {
    std::ostringstream ss;
    ss << edge_count_ << " edges, " << graph_.size() << " source types";

    for (const auto& [type_int, edges] : graph_) {
        auto from_type = static_cast<T>(type_int);
        ss << "\n  " << type_name(from_type) << " ->";

        // Group by target type
        std::unordered_map<int, int> counts;
        for (const auto& e : edges) {
            counts[static_cast<int>(e.to)]++;
        }
        bool first = true;
        for (const auto& [to_int, count] : counts) {
            if (!first) ss << ",";
            ss << " " << type_name(static_cast<T>(to_int))
               << "(" << count << ")";
            first = false;
        }
    }
    return ss.str();
}

T BridgeMap::parse_sig_type(const std::string& name) {
    if (name == "integer")   return T::Integer;
    if (name == "float")     return T::Float;
    if (name == "boolean")   return T::Boolean;
    if (name == "string")    return T::String;
    if (name == "array")     return T::Array;
    if (name == "bytearray") return T::ByteArray;
    if (name == "map")       return T::Map;
    if (name == "json")      return T::Json;
    if (name == "matrix")    return T::Matrix;
    if (name == "observable") return T::Observable;
    if (name == "xt")        return T::Xt;
    if (name == "dataref")   return T::DataRef;
    return T::Unknown;
}

const char* BridgeMap::type_name(T t) {
    switch (t) {
        case T::Unknown:    return "Unknown";
        case T::Integer:    return "Integer";
        case T::Float:      return "Float";
        case T::Boolean:    return "Boolean";
        case T::String:     return "String";
        case T::Array:      return "Array";
        case T::ByteArray:  return "ByteArray";
        case T::Map:        return "Map";
        case T::Json:       return "Json";
        case T::Matrix:     return "Matrix";
        case T::Observable: return "Observable";
        case T::Xt:         return "Xt";
        case T::DataRef:    return "DataRef";
        case T::Custom:     return "Custom";
    }
    return "Unknown";
}

} // namespace etil::evolution
