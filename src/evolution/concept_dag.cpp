// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/concept_dag.hpp"
#include "etil/core/compiled_body.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>

namespace etil::evolution {

using namespace etil::core;

// --- ConceptNodeStats ---

void ConceptNodeStats::record_fitness(double fitness) {
    eval_count++;
    if (fitness > best_fitness) best_fitness = fitness;
    if (fitness < worst_fitness) worst_fitness = fitness;

    // Welford's online algorithm for running mean and variance
    double delta = fitness - mean_fitness;
    mean_fitness += delta / static_cast<double>(eval_count);
    double delta2 = fitness - mean_fitness;
    fitness_variance += delta * delta2;
}

// --- ConceptDAG ---

void ConceptDAG::build(const std::string& root_concept, Dictionary& dict) {
    root_ = root_concept;
    nodes_.clear();
    topo_order_.clear();

    std::unordered_set<std::string> ancestors;
    scan_concept(root_concept, 0, dict, ancestors);
    compute_topo_order();

    // Initialize impl counts from dictionary
    for (auto& [name, node] : nodes_) {
        auto impls = dict.get_implementations(name);
        node.stats.impl_count = impls ? impls->size() : 0;
    }
}

void ConceptDAG::scan_concept(const std::string& concept_name,
                               size_t depth,
                               Dictionary& dict,
                               std::unordered_set<std::string>& ancestors) {
    // Already in DAG (shared sub-concept) — keep the shallowest depth
    if (nodes_.count(concept_name)) {
        auto& existing = nodes_[concept_name];
        if (depth < existing.depth) existing.depth = depth;
        return;
    }

    ConceptDAGNode node;
    node.name = concept_name;
    node.depth = depth;

    // Check for recursion (ancestor on the current DFS path)
    if (ancestors.count(concept_name)) {
        node.opaque = true;
        nodes_[concept_name] = std::move(node);
        return;
    }

    // Get implementations
    auto impls = dict.get_implementations(concept_name);
    if (!impls || impls->empty()) {
        node.opaque = true;
        nodes_[concept_name] = std::move(node);
        return;
    }

    // Check if all impls are native (primitive word)
    bool has_bytecode = false;
    for (const auto& impl : *impls) {
        if (impl->bytecode()) { has_bytecode = true; break; }
    }
    if (!has_bytecode) {
        node.opaque = true;
        nodes_[concept_name] = std::move(node);
        return;
    }

    // Extract type signature from first impl
    if (!impls->empty()) {
        node.type_contract = (*impls)[0]->signature();
    }

    // Scan all bytecode impls for Call instructions to discover children
    std::unordered_set<std::string> child_set;
    for (const auto& impl : *impls) {
        if (!impl->bytecode()) continue;
        for (const auto& instr : impl->bytecode()->instructions()) {
            if (instr.op == Instruction::Op::Call) {
                if (child_set.insert(instr.word_name).second) {
                    node.children.push_back(instr.word_name);
                }
            }
        }
    }

    // Add this node before recursing (so children can detect it as visited)
    nodes_[concept_name] = std::move(node);

    // Mark as ancestor for recursion detection, then scan children
    ancestors.insert(concept_name);
    for (const auto& child_name : nodes_[concept_name].children) {
        scan_concept(child_name, depth + 1, dict, ancestors);
    }
    ancestors.erase(concept_name);
}

void ConceptDAG::compute_topo_order() {
    topo_order_.clear();
    std::unordered_set<std::string> visited;
    std::vector<std::string> result;
    topo_dfs(root_, visited, result);
    std::reverse(result.begin(), result.end());
    topo_order_ = std::move(result);
}

void ConceptDAG::topo_dfs(const std::string& name,
                           std::unordered_set<std::string>& visited,
                           std::vector<std::string>& result) const {
    if (visited.count(name)) return;
    visited.insert(name);

    auto it = nodes_.find(name);
    if (it != nodes_.end() && !it->second.opaque) {
        for (const auto& child : it->second.children) {
            topo_dfs(child, visited, result);
        }
    }
    result.push_back(name);
}

ConceptDAGNode* ConceptDAG::node(const std::string& name) {
    auto it = nodes_.find(name);
    return it != nodes_.end() ? &it->second : nullptr;
}

const ConceptDAGNode* ConceptDAG::node(const std::string& name) const {
    auto it = nodes_.find(name);
    return it != nodes_.end() ? &it->second : nullptr;
}

size_t ConceptDAG::max_depth() const {
    size_t max_d = 0;
    for (const auto& [_, n] : nodes_) {
        if (n.depth > max_d) max_d = n.depth;
    }
    return max_d;
}

std::vector<std::string> ConceptDAG::evolvable_concepts() const {
    std::vector<std::string> result;
    for (const auto& name : topo_order_) {
        if (name == root_) continue;
        auto it = nodes_.find(name);
        if (it != nodes_.end() && !it->second.opaque) {
            result.push_back(name);
        }
    }
    return result;
}

std::string ConceptDAG::select_for_evolution(std::mt19937_64& rng,
                                              double depth_discount) const {
    auto evolvable = evolvable_concepts();
    if (evolvable.empty()) return {};

    // Build weighted distribution: contribution × depth_discount^depth
    std::vector<double> weights;
    weights.reserve(evolvable.size());
    for (const auto& name : evolvable) {
        auto it = nodes_.find(name);
        double w = it->second.contribution;
        if (depth_discount < 1.0) {
            w *= std::pow(depth_discount, static_cast<double>(it->second.depth));
        }
        weights.push_back(std::max(w, 1e-9));  // floor to prevent zero-weight
    }

    std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
    return evolvable[dist(rng)];
}

void ConceptDAG::reset() {
    for (auto& [_, node] : nodes_) {
        node.contribution = 1.0;
        size_t saved_impl_count = node.stats.impl_count;
        node.stats = ConceptNodeStats{};
        node.stats.impl_count = saved_impl_count;
    }
}

void ConceptDAG::normalize_contributions() {
    auto evolvable = evolvable_concepts();
    if (evolvable.empty()) return;

    double total = 0.0;
    for (const auto& name : evolvable) {
        total += nodes_[name].contribution;
    }
    if (total <= 0.0) return;

    for (const auto& name : evolvable) {
        nodes_[name].contribution /= total;
    }
}

void ConceptDAG::dump(std::ostream& out) const {
    if (nodes_.empty()) {
        out << "ConceptDAG: (empty)\n";
        return;
    }

    auto evolvable = evolvable_concepts();
    size_t opaque_count = 0;
    for (const auto& [_, n] : nodes_) {
        if (n.opaque) opaque_count++;
    }

    out << "ConceptDAG: " << root_
        << " (" << nodes_.size() << " nodes"
        << ", " << evolvable.size() << " evolvable"
        << ", " << opaque_count << " opaque"
        << ", depth " << max_depth() << ")\n";

    // Print in topological order with indentation by depth
    for (const auto& name : topo_order_) {
        auto it = nodes_.find(name);
        if (it == nodes_.end()) continue;
        const auto& n = it->second;

        // Indent by depth
        for (size_t i = 0; i < n.depth; ++i) out << "  ";

        out << n.name;
        if (name == root_) out << " [root]";
        if (n.opaque) out << " [opaque]";
        out << "  depth=" << n.depth
            << "  contrib=" << std::fixed << std::setprecision(3) << n.contribution
            << "  gens=" << n.stats.generations_evolved
            << "  impls=" << n.stats.impl_count;
        if (n.stats.eval_count > 0) {
            out << "  best=" << std::fixed << std::setprecision(3) << n.stats.best_fitness
                << "  mean=" << std::fixed << std::setprecision(3) << n.stats.mean_fitness;
            if (n.stats.eval_count > 1) {
                double var = n.stats.fitness_variance / static_cast<double>(n.stats.eval_count - 1);
                out << "  var=" << std::fixed << std::setprecision(4) << var;
            }
        }
        out << "\n";
    }
}

} // namespace etil::evolution
