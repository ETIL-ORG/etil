// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/ast_genetic_ops.hpp"
#include "etil/evolution/evolve_logger.hpp"
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

    std::string old_name = target->word_name;
    target->word_name = candidates[chosen].first;

    if (logger_ && logger_->enabled(EvolveLogCategory::Substitute)) {
        // Count candidates per level
        size_t l1 = 0, l2 = 0, l3 = 0;
        for (const auto& [n, lev] : candidates) {
            if (lev == 1) l1++; else if (lev == 2) l2++; else l3++;
        }
        logger_->log(EvolveLogCategory::Substitute,
            "'" + old_name + "' → '" + candidates[chosen].first
            + "' (Level " + std::to_string(candidates[chosen].second)
            + ", candidates: L1=" + std::to_string(l1)
            + " L2=" + std::to_string(l2)
            + " L3=" + std::to_string(l3) + ")");
    }
    if (logger_ && logger_->granular(EvolveLogCategory::Substitute)) {
        std::string clist;
        for (size_t i = 0; i < candidates.size() && i < 20; ++i) {
            if (i > 0) clist += ", ";
            clist += candidates[i].first + "(L" + std::to_string(candidates[i].second) + ")";
        }
        if (candidates.size() > 20) clist += ", ...(" + std::to_string(candidates.size()) + " total)";
        logger_->detail(EvolveLogCategory::Substitute,
            "find_tiered(consumed=" + std::to_string(consumed)
            + ", produced=" + std::to_string(produced)
            + "): [" + clist + "]");
    }
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
    auto old_int = target->int_val;
    auto old_float = target->float_val;
    perturb_numeric(target->literal_op, target->int_val, target->float_val, 0.1, rng_);

    if (logger_ && logger_->enabled(EvolveLogCategory::Perturb)) {
        if (target->literal_op == Instruction::Op::PushInt) {
            logger_->log(EvolveLogCategory::Perturb,
                "int " + std::to_string(old_int) + " → " + std::to_string(target->int_val));
        } else {
            logger_->log(EvolveLogCategory::Perturb,
                "float " + std::to_string(old_float) + " → " + std::to_string(target->float_val));
        }
    }
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

// --- Move a WordCall to a different position in the sequence ---

bool ASTGeneticOps::move_block(ASTNode& ast) {
    if (ast.kind != ASTNodeKind::Sequence || ast.children.size() < 3) return false;

    // Find moveable nodes (WordCalls only, not control flow or literals)
    std::vector<size_t> moveable;
    for (size_t i = 0; i < ast.children.size(); ++i) {
        if (ast.children[i].kind == ASTNodeKind::WordCall) {
            moveable.push_back(i);
        }
    }
    if (moveable.size() < 2) return false;

    // Pick a random source and a different random target
    std::uniform_int_distribution<size_t> dist(0, moveable.size() - 1);
    size_t src_slot = dist(rng_);
    size_t dst_slot = dist(rng_);
    while (dst_slot == src_slot && moveable.size() > 1) dst_slot = dist(rng_);
    if (dst_slot == src_slot) return false;

    size_t src_idx = moveable[src_slot];
    size_t dst_idx = moveable[dst_slot];

    // Extract and reinsert
    std::string moved_name = ast.children[src_idx].word_name;
    auto node = std::move(ast.children[src_idx]);
    ast.children.erase(ast.children.begin() + static_cast<long>(src_idx));
    if (dst_idx > src_idx) dst_idx--;
    ast.children.insert(ast.children.begin() + static_cast<long>(dst_idx), std::move(node));

    if (logger_ && logger_->enabled(EvolveLogCategory::Move)) {
        logger_->log(EvolveLogCategory::Move,
            "'" + moved_name + "' position " + std::to_string(src_idx)
            + " → " + std::to_string(dst_idx));
    }
    return true;
}

// --- Wrap/unwrap control flow ---

bool ASTGeneticOps::mutate_control_flow(ASTNode& ast) {
    if (ast.kind != ASTNodeKind::Sequence || ast.children.empty()) return false;

    std::uniform_int_distribution<int> op_choice(0, 1);
    int op = op_choice(rng_);

    if (op == 0) {
        // Wrap a random WordCall in if/then (with always-true condition)
        std::vector<size_t> candidates;
        for (size_t i = 0; i < ast.children.size(); ++i) {
            if (ast.children[i].kind == ASTNodeKind::WordCall) {
                candidates.push_back(i);
            }
        }
        if (candidates.empty()) return false;

        std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
        size_t idx = candidates[dist(rng_)];

        // Build IfThen: condition is "true" (always executes), body is the word
        ASTNode if_node;
        if_node.kind = ASTNodeKind::IfThen;
        ASTNode body = ASTNode::make_sequence({std::move(ast.children[idx])});
        if_node.children.push_back(std::move(body));

        // The condition (boolean true) needs to be before the IfThen in the sequence
        // since BranchIfFalse pops the boolean from the stack
        std::string wrapped_name = ast.children[idx].word_name;
        ast.children[idx] = std::move(if_node);
        ast.children.insert(ast.children.begin() + static_cast<long>(idx),
                            ASTNode::make_word_call("true"));
        if (logger_ && logger_->enabled(EvolveLogCategory::ControlFlow)) {
            logger_->log(EvolveLogCategory::ControlFlow,
                "Wrapped '" + wrapped_name + "' at position " + std::to_string(idx) + " in if/then");
        }
        return true;
    } else {
        // Unwrap: find an IfThen and replace it with its body
        for (size_t i = 0; i < ast.children.size(); ++i) {
            if (ast.children[i].kind == ASTNodeKind::IfThen &&
                !ast.children[i].children.empty() &&
                ast.children[i].children[0].kind == ASTNodeKind::Sequence) {
                // Replace the IfThen with the body's children
                auto body_children = std::move(ast.children[i].children[0].children);
                ast.children.erase(ast.children.begin() + static_cast<long>(i));
                // Also remove the condition (the boolean before the IfThen)
                // The condition was pushed by whatever precedes the IfThen
                // For safety, just insert the body children at position i
                for (size_t j = 0; j < body_children.size(); ++j) {
                    ast.children.insert(
                        ast.children.begin() + static_cast<long>(i + j),
                        std::move(body_children[j]));
                }
                if (logger_ && logger_->enabled(EvolveLogCategory::ControlFlow)) {
                    logger_->log(EvolveLogCategory::ControlFlow,
                        "Unwrapped if/then at position " + std::to_string(i)
                        + " (" + std::to_string(body_children.size()) + " body nodes)");
                }
                return true;
            }
        }
        return false;  // no IfThen to unwrap
    }
}

// --- Public API ---

WordImplPtr ASTGeneticOps::mutate(const WordImpl& parent) {
    auto bc = parent.bytecode();
    if (!bc) return WordImplPtr();

    // Decompile
    auto ast = decompiler_.decompile(*bc);

    // Apply one random mutation from 4 operators
    static const char* op_names[] = {"substitute", "perturb", "move", "control-flow"};
    std::uniform_int_distribution<int> choice(0, 3);
    bool mutated = false;
    int first = choice(rng_);

    if (logger_ && logger_->enabled(EvolveLogCategory::Engine)) {
        logger_->log(EvolveLogCategory::Engine,
            "Selected operator: " + std::string(op_names[first]));
    }

    switch (first) {
        case 0: mutated = substitute_call(ast); break;
        case 1: mutated = perturb_constant(ast); break;
        case 2: mutated = move_block(ast); break;
        case 3: mutated = mutate_control_flow(ast); break;
    }
    // If first choice failed, try the others
    if (!mutated) {
        for (int i = 0; i < 4 && !mutated; ++i) {
            if (i == first) continue;
            if (logger_ && logger_->granular(EvolveLogCategory::Engine)) {
                logger_->detail(EvolveLogCategory::Engine,
                    std::string(op_names[first]) + " failed, trying " + op_names[i]);
            }
            switch (i) {
                case 0: mutated = substitute_call(ast); break;
                case 1: mutated = perturb_constant(ast); break;
                case 2: mutated = move_block(ast); break;
                case 3: mutated = mutate_control_flow(ast); break;
            }
        }
    }
    if (!mutated) {
        if (logger_ && logger_->enabled(EvolveLogCategory::Engine)) {
            logger_->log(EvolveLogCategory::Engine, "All 4 operators failed");
        }
        return WordImplPtr();
    }

    // Repair type mismatches
    bool repaired = repair_.repair(ast, dict_);
    if (logger_ && logger_->enabled(EvolveLogCategory::Repair)) {
        logger_->log(EvolveLogCategory::Repair,
            repaired ? "Type repair succeeded" : "Type repair failed (unrepairable)");
    }
    if (!repaired) return WordImplPtr();

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
