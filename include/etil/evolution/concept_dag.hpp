#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/word_impl.hpp"
#include "etil/core/dictionary.hpp"

#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace etil::evolution {

/// Statistics tracked per concept node across generations.
struct ConceptNodeStats {
    size_t generations_evolved = 0;
    size_t children_created = 0;
    size_t children_pruned = 0;
    double best_fitness = 0.0;
    double worst_fitness = 1.0;
    double mean_fitness = 0.0;
    double fitness_variance = 0.0;
    size_t eval_count = 0;         // number of fitness evaluations (for running stats)
    size_t impl_count = 0;         // current number of impls in dictionary

    /// Update running statistics with a new fitness observation.
    void record_fitness(double fitness);
};

/// A node in the ConceptDAG. Represents a word concept at a specific
/// position in the call graph relative to a root.
struct ConceptDAGNode {
    std::string name;                          // concept name (dictionary key)
    double contribution = 1.0;                 // Tier 2 weight: evolution scheduling priority
    size_t depth = 0;                          // distance from root
    bool opaque = false;                       // true for primitives and recursive concepts
    core::TypeSignature type_contract;         // expected stack effect
    ConceptNodeStats stats;
    std::vector<std::string> children;         // child concept names (from Call instructions)
};

/// The ConceptDAG: an explicit, inspectable representation of the concept
/// call graph rooted at a single entry point. Built eagerly from
/// bytecode Call instructions at registration time.
///
/// Node types alternate: Concept -> impl -> Concept -> impl.
/// Concept nodes are represented by ConceptDAGNode.
/// Impl nodes are implicit (they live in the Dictionary).
///
/// Recursive concepts are detected and marked opaque (treated as leaves).
/// Primitives (native code, no bytecode) are also opaque.
class ConceptDAG {
public:
    /// Build the DAG by scanning bytecode of root_concept and all reachable
    /// concepts. Recursive concepts are marked opaque. Primitives are leaves.
    void build(const std::string& root_concept,
               core::Dictionary& dict);

    /// Get a node by concept name. Returns nullptr if not in DAG.
    ConceptDAGNode* node(const std::string& name);
    const ConceptDAGNode* node(const std::string& name) const;

    /// Get all concept names in the DAG (topological order, root first).
    const std::vector<std::string>& topo_order() const { return topo_order_; }

    /// Get the root concept name.
    const std::string& root() const { return root_; }

    /// Total number of nodes in the DAG.
    size_t size() const { return nodes_.size(); }

    /// Maximum depth in the DAG.
    size_t max_depth() const;

    /// Get all non-opaque, non-root concept names (evolvable targets).
    std::vector<std::string> evolvable_concepts() const;

    /// Select a concept for evolution, weighted by contribution × depth_discount^depth.
    /// Returns empty string if no evolvable concepts exist.
    std::string select_for_evolution(std::mt19937_64& rng,
                                     double depth_discount = 1.0) const;

    /// Reset all contribution weights to 1.0 and clear statistics.
    void reset();

    /// Normalize contribution weights so they sum to 1.0 across evolvable concepts.
    void normalize_contributions();

    /// Print DAG structure with weights and statistics to an output stream.
    void dump(std::ostream& out) const;

    /// Check if the DAG has been built (has at least a root node).
    bool empty() const { return nodes_.empty(); }

private:
    std::string root_;
    std::vector<std::string> topo_order_;
    std::unordered_map<std::string, ConceptDAGNode> nodes_;

    /// Recursively scan an impl's bytecode for Call instructions,
    /// adding discovered child concepts to the DAG.
    void scan_concept(const std::string& concept_name,
                      size_t depth,
                      core::Dictionary& dict,
                      std::unordered_set<std::string>& ancestors);

    /// Compute topological order via DFS post-order reversal.
    void compute_topo_order();
    void topo_dfs(const std::string& name,
                  std::unordered_set<std::string>& visited,
                  std::vector<std::string>& result) const;
};

} // namespace etil::evolution
