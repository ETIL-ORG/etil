// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/bridge_map.hpp"

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

    // Enumerate all 1-hop direct edges from → to
    struct Candidate {
        double weight;
        std::vector<std::string> words;
    };
    std::vector<Candidate> candidates;

    auto it_from = graph_.find(static_cast<int>(from));
    if (it_from != graph_.end()) {
        for (const auto& edge : it_from->second) {
            if (edge.to == to) {
                candidates.push_back({edge.weight, {edge.word}});
            }
        }
    }

    // If no direct edges and multi-hop allowed, enumerate 2-hop paths
    if (candidates.empty() && max_hops >= 2 && it_from != graph_.end()) {
        for (const auto& edge1 : it_from->second) {
            if (edge1.to == to) continue;  // already handled
            auto it_mid = graph_.find(static_cast<int>(edge1.to));
            if (it_mid == graph_.end()) continue;
            for (const auto& edge2 : it_mid->second) {
                if (edge2.to == to) {
                    candidates.push_back({
                        edge1.weight * edge2.weight,
                        {edge1.word, edge2.word}
                    });
                }
            }
        }
    }

    if (candidates.empty()) return {};
    if (candidates.size() == 1) return candidates[0].words;

    // Weighted-random selection via discrete_distribution
    std::vector<double> weights;
    weights.reserve(candidates.size());
    for (const auto& c : candidates) weights.push_back(c.weight);

    std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
    size_t chosen = dist(rng_);
    return candidates[chosen].words;
}

bool BridgeMap::set_edge_weight(T from, T to, const std::string& word, double weight) {
    auto it = graph_.find(static_cast<int>(from));
    if (it == graph_.end()) return false;
    for (auto& edge : it->second) {
        if (edge.to == to && edge.word == word) {
            edge.weight = weight;
            return true;
        }
    }
    return false;
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
