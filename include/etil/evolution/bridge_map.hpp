#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/word_impl.hpp"

#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace etil::evolution { class EvolveLogger; }


namespace etil::evolution {

/// Type conversion edge in the bridge graph.
struct BridgeEdge {
    core::TypeSignature::Type from;
    core::TypeSignature::Type to;
    std::string word;  // the conversion word (e.g., "int->float")

    // TBBP state (per-session, reset on BridgeMap construction)
    double weight = 1.0;       // current EMA weight
    uint64_t selections = 0;   // total times selected
    uint64_t successes = 0;    // selections that led to child fitness > parent
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

    /// Select a path using TBBP weighted-random sampling among paths of the
    /// shortest available length. When tbbp_enabled is false, falls through
    /// to find_path() (deterministic BFS, first path found).
    /// Returns ordered word sequence, or empty if no path exists.
    std::vector<std::string> select_path(T from, T to, size_t max_hops = 2);

    /// Override the RNG seed (for deterministic tests).
    void set_rng_seed(uint64_t seed) { rng_.seed(seed); }

    /// Directly set the weight of a specific edge. Returns true if found.
    /// Used for testing and manual tuning.
    bool set_edge_weight(T from, T to, const std::string& word, double weight);

    // --- TBBP per-mutation tracking and EMA update ---

    /// EMA learning rate (0 < alpha < 1). Default 0.1.
    void set_alpha(double a) { alpha_ = a; }
    double alpha() const { return alpha_; }

    /// Minimum weight floor to preserve exploration. Default 0.05.
    void set_min_weight(double m) { min_weight_ = m; }
    double min_weight() const { return min_weight_; }

    /// Start recording bridge usages for the current mutation.
    /// Subsequent calls to select_path or record_usage are tracked until
    /// end_mutation is called. No-op when tbbp_enabled is false.
    void begin_mutation();

    /// Record that an edge was selected for use in the current mutation.
    /// Called automatically by select_path; can be called manually.
    /// Increments the edge's selections counter. No-op when tbbp_enabled is false.
    void record_usage(T from, T to, const std::string& word);

    /// Apply EMA weight update to all edges recorded since begin_mutation.
    /// reward should be 1.0 if child_fitness > parent_fitness, 0.0 otherwise.
    /// Clears the current-mutation usage list. No-op when tbbp_enabled is false.
    void end_mutation(double reward);

    /// Attach a logger for bridge selection / weight update events.
    /// Events logged at EvolveLogCategory::Bridge when enabled.
    void set_logger(EvolveLogger* logger) { logger_ = logger; }

    /// Emit a summary of top-N bridges by weight at Bridge category.
    /// Called at generation end by EvolutionEngine.
    void log_weight_summary(size_t top_n = 10) const;

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

    // --- TBBP runtime toggle ---

    /// Enable or disable Type Bridge Back Propagation (TBBP).
    /// When disabled, select_path/record_usage/end_mutation are no-ops and
    /// weights remain at 1.0. Default: true.
    void set_tbbp_enabled(bool e) { tbbp_enabled_ = e; }
    bool tbbp_enabled() const { return tbbp_enabled_; }

private:
    struct EdgeRef {
        T from;
        T to;
        std::string word;
    };

    BridgeEdge* find_edge_mut(T from, T to, const std::string& word);

    std::unordered_map<int, std::vector<BridgeEdge>> graph_;
    size_t edge_count_ = 0;
    bool finalized_ = false;
    bool tbbp_enabled_ = true;
    std::mt19937_64 rng_{std::random_device{}()};

    // TBBP per-mutation tracking
    std::vector<EdgeRef> current_mutation_usages_;
    double alpha_ = 0.1;
    double min_weight_ = 0.05;

    // Logging
    EvolveLogger* logger_ = nullptr;

    static const std::vector<BridgeEdge> empty_edges_;
};

} // namespace etil::evolution
