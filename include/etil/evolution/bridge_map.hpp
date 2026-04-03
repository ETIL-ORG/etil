#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/word_impl.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace etil::evolution {

/// Type conversion edge in the bridge graph.
struct BridgeEdge {
    core::TypeSignature::Type from;
    core::TypeSignature::Type to;
    std::string word;  // the conversion word (e.g., "int->float")
};

/// Directed graph of type conversions built from evolve-bridge calls.
/// No persistence — rebuilt from TIL at load time.
class BridgeMap {
public:
    using T = core::TypeSignature::Type;

    /// Register a single conversion.
    void add(T from, T to, const std::string& word);

    /// Signal that construction is complete. Logs summary if bridge logging is on.
    void finalize();

    /// Direct conversions from a given type.
    const std::vector<BridgeEdge>& conversions_from(T from) const;

    /// Find single-hop bridge words: from → to. Returns empty if none.
    std::vector<std::string> find_bridge(T from, T to) const;

    /// Find a multi-hop path (BFS, max_hops). Returns ordered word sequence.
    std::vector<std::string> find_path(T from, T to, size_t max_hops = 2) const;

    /// Is there any conversion from this type?
    bool has_conversions(T from) const;

    /// Summary string for logging/diagnostics.
    std::string summary() const;

    /// Total number of edges in the graph.
    size_t size() const { return edge_count_; }

    /// Number of distinct source types.
    size_t source_type_count() const { return graph_.size(); }

    /// Parse a TIL type name string to TypeSignature::Type.
    /// Returns T::Unknown for unrecognized names.
    static T parse_sig_type(const std::string& name);

    /// Convert TypeSignature::Type to a display string.
    static const char* type_name(T t);

private:
    std::unordered_map<int, std::vector<BridgeEdge>> graph_;
    size_t edge_count_ = 0;
    bool finalized_ = false;

    static const std::vector<BridgeEdge> empty_edges_;
};

} // namespace etil::evolution
