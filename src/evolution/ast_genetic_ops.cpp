// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/ast_genetic_ops.hpp"
#include "etil/evolution/mutation_helpers.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace etil::evolution {

using namespace etil::core;

ASTGeneticOps::ASTGeneticOps(Dictionary& dict)
    : dict_(dict)
    , rng_(std::chrono::steady_clock::now().time_since_epoch().count())
{
    rebuild_index();
}

void ASTGeneticOps::rebuild_index() {
    index_.rebuild(dict_);
}

// --- Collect mutable nodes ---

template<typename Pred>
static void collect_nodes(ASTNode& node, Pred pred, std::vector<ASTNode*>& out) {
    if (pred(node)) out.push_back(&node);
    for (auto& child : node.children) collect_nodes(child, pred, out);
}

// --- Substitute a word call with a semantically compatible alternative ---

bool ASTGeneticOps::substitute_call(ASTNode& ast) {
    std::vector<ASTNode*> calls;
    collect_nodes(ast, [](const ASTNode& n) { return n.kind == ASTNodeKind::WordCall; }, calls);
    if (calls.empty()) return false;

    // Pick a random call to substitute
    std::uniform_int_distribution<size_t> dist(0, calls.size() - 1);
    ASTNode* target = calls[dist(rng_)];

    // Look up current word's signature
    auto impl = dict_.lookup(target->word_name);
    if (!impl) return false;
    const auto& sig = (*impl)->signature();
    int consumed = static_cast<int>(sig.inputs.size());
    int produced = static_cast<int>(sig.outputs.size());

    // Find compatible alternatives with tiered matching
    auto target_tags = index_.get_tags(target->word_name);
    auto candidates = index_.find_tiered(consumed, produced, target_tags);

    // Remove the current word from candidates
    candidates.erase(
        std::remove_if(candidates.begin(), candidates.end(),
            [&](const auto& p) { return p.first == target->word_name; }),
        candidates.end());

    if (candidates.empty()) return false;

    // Weighted selection: Level 1 (60%), Level 2 (25%), Level 3 (15%)
    std::vector<double> weights;
    for (const auto& [name, level] : candidates) {
        switch (level) {
            case 1: weights.push_back(6.0); break;
            case 2: weights.push_back(2.5); break;
            case 3: weights.push_back(1.5); break;
            default: weights.push_back(1.0); break;
        }
    }
    std::discrete_distribution<size_t> wdist(weights.begin(), weights.end());
    size_t chosen = wdist(rng_);

    target->word_name = candidates[chosen].first;
    return true;
}

// --- Perturb a numeric literal ---

bool ASTGeneticOps::perturb_constant(ASTNode& ast) {
    std::vector<ASTNode*> literals;
    collect_nodes(ast, [](const ASTNode& n) {
        return n.kind == ASTNodeKind::Literal &&
               (n.literal_op == Instruction::Op::PushInt ||
                n.literal_op == Instruction::Op::PushFloat);
    }, literals);
    if (literals.empty()) return false;

    std::uniform_int_distribution<size_t> dist(0, literals.size() - 1);
    ASTNode* target = literals[dist(rng_)];
    perturb_numeric(target->literal_op, target->int_val, target->float_val, 0.1, rng_);
    return true;
}

// --- Block crossover: splice a subtree from ast_b into ast_a ---

bool ASTGeneticOps::block_crossover(ASTNode& ast_a, const ASTNode& ast_b) {
    // Collect WordCall nodes from both ASTs
    std::vector<ASTNode*> calls_a;
    collect_nodes(ast_a, [](const ASTNode& n) { return n.kind == ASTNodeKind::WordCall; }, calls_a);
    if (calls_a.empty()) return false;

    // Find a call in A and replace it with one from B that has the same word
    // (simplest form of block crossover — word-level swap)
    std::vector<const ASTNode*> calls_b;
    std::function<void(const ASTNode&)> collect_b = [&](const ASTNode& n) {
        if (n.kind == ASTNodeKind::WordCall) calls_b.push_back(&n);
        for (const auto& c : n.children) collect_b(c);
    };
    collect_b(ast_b);
    if (calls_b.empty()) return false;

    // Pick random from B, substitute into A at a compatible position
    std::uniform_int_distribution<size_t> dist_b(0, calls_b.size() - 1);
    const ASTNode* source = calls_b[dist_b(rng_)];

    // Find a position in A with the same stack effect
    auto src_impl = dict_.lookup(source->word_name);
    if (!src_impl) return false;
    const auto& src_sig = (*src_impl)->signature();
    int src_consumed = static_cast<int>(src_sig.inputs.size());
    int src_produced = static_cast<int>(src_sig.outputs.size());

    // Find calls in A with matching effect
    std::vector<ASTNode*> compatible;
    for (auto* call : calls_a) {
        auto impl = dict_.lookup(call->word_name);
        if (!impl) continue;
        const auto& sig = (*impl)->signature();
        if (static_cast<int>(sig.inputs.size()) == src_consumed &&
            static_cast<int>(sig.outputs.size()) == src_produced) {
            compatible.push_back(call);
        }
    }
    if (compatible.empty()) return false;

    std::uniform_int_distribution<size_t> dist_a(0, compatible.size() - 1);
    ASTNode* target = compatible[dist_a(rng_)];
    target->word_name = source->word_name;
    return true;
}

// --- Public API ---

WordImplPtr ASTGeneticOps::mutate(const WordImpl& parent) {
    auto bc = parent.bytecode();
    if (!bc) return WordImplPtr();

    // Decompile
    auto ast = decompiler_.decompile(*bc);

    // Apply one random mutation
    std::uniform_int_distribution<int> choice(0, 1);
    bool mutated = false;
    switch (choice(rng_)) {
        case 0: mutated = substitute_call(ast); break;
        case 1: mutated = perturb_constant(ast); break;
    }
    if (!mutated) {
        // Try the other operator
        switch (choice(rng_)) {
            case 0: mutated = perturb_constant(ast); break;
            case 1: mutated = substitute_call(ast); break;
        }
    }
    if (!mutated) return WordImplPtr();

    // Repair type mismatches
    if (!repair_.repair(ast, dict_)) return WordImplPtr();

    // Compile back to bytecode
    auto new_bc = compiler_.compile(ast);

    // Create child WordImpl
    auto id = Dictionary::next_id();
    WordImplPtr child(new WordImpl(parent.name(), id));
    child->set_generation(parent.generation() + 1);
    child->add_parent(parent.id());
    child->set_weight(parent.weight());
    child->set_signature(parent.signature());
    child->set_bytecode(new_bc);
    child->add_mutation(MutationHistory::MutationType::Inline, "ast-mutation");
    return child;
}

WordImplPtr ASTGeneticOps::crossover(
    const WordImpl& parent_a, const WordImpl& parent_b) {
    auto bc_a = parent_a.bytecode();
    auto bc_b = parent_b.bytecode();
    if (!bc_a || !bc_b) return WordImplPtr();

    auto ast_a = decompiler_.decompile(*bc_a);
    auto ast_b = decompiler_.decompile(*bc_b);

    if (!block_crossover(ast_a, ast_b)) return WordImplPtr();

    // Repair
    if (!repair_.repair(ast_a, dict_)) return WordImplPtr();

    auto new_bc = compiler_.compile(ast_a);

    auto id = Dictionary::next_id();
    WordImplPtr child(new WordImpl(parent_a.name(), id));
    child->set_generation(
        std::max(parent_a.generation(), parent_b.generation()) + 1);
    child->add_parent(parent_a.id());
    child->add_parent(parent_b.id());
    child->set_weight((parent_a.weight() + parent_b.weight()) / 2.0);
    child->set_signature(parent_a.signature());
    child->set_bytecode(new_bc);
    child->add_mutation(MutationHistory::MutationType::Crossover, "ast-crossover");
    return child;
}

} // namespace etil::evolution
