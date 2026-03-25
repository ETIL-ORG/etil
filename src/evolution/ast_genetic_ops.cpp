// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/ast_genetic_ops.hpp"
#include "etil/evolution/evolution_engine.hpp"
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

    // Find compatible alternatives — pool-restricted or tiered
    std::string old_name = target->word_name;
    std::string chosen_name;
    int chosen_level = 3;
    size_t l1 = 0, l2 = 0, l3 = 0;

    if (word_pool_ && !word_pool_->empty()) {
        // Pool-restricted: only words in the pool
        auto restricted = index_.find_restricted(consumed, produced, *word_pool_);
        restricted.erase(
            std::remove(restricted.begin(), restricted.end(), target->word_name),
            restricted.end());
        if (restricted.empty()) return false;
        std::uniform_int_distribution<size_t> rdist(0, restricted.size() - 1);
        chosen_name = restricted[rdist(rng_)];
        l1 = restricted.size();  // all pool words are "Level 1" conceptually
        chosen_level = 1;
    } else {
        // Tiered matching from full dictionary
        auto target_tags = index_.get_tags(target->word_name);
        auto candidates = index_.find_tiered(consumed, produced, target_tags);
        candidates.erase(
            std::remove_if(candidates.begin(), candidates.end(),
                [&](const auto& p) { return p.first == target->word_name; }),
            candidates.end());
        if (candidates.empty()) return false;

        // Count per level
        for (const auto& [n, lev] : candidates) {
            if (lev == 1) l1++; else if (lev == 2) l2++; else l3++;
        }

        // Weighted selection
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
        size_t chosen_idx = wdist(rng_);
        chosen_name = candidates[chosen_idx].first;
        chosen_level = candidates[chosen_idx].second;
    }

    target->word_name = chosen_name;

    if (logger_ && logger_->enabled(EvolveLogCategory::Substitute)) {
        std::string pool_tag = (word_pool_ && !word_pool_->empty()) ? " [pool]" : "";
        logger_->log(EvolveLogCategory::Substitute,
            "'" + old_name + "' → '" + chosen_name
            + "' (Level " + std::to_string(chosen_level)
            + ", candidates: L1=" + std::to_string(l1)
            + " L2=" + std::to_string(l2)
            + " L3=" + std::to_string(l3) + ")" + pool_tag);
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

// --- Grow: insert a new node into a Sequence ---

bool ASTGeneticOps::grow_node(ASTNode& ast) {
    // Bloat control
    size_t max_nodes = config_ ? config_->max_ast_nodes : 30;
    if (count_nodes(ast) >= max_nodes) {
        if (logger_ && logger_->enabled(EvolveLogCategory::Grow)) {
            logger_->log(EvolveLogCategory::Grow,
                "Rejected: AST has " + std::to_string(count_nodes(ast))
                + " nodes (max " + std::to_string(max_nodes) + ")");
        }
        return false;
    }

    // Find all Sequence nodes
    std::vector<ASTNode*> sequences;
    collect_nodes(ast, [](const ASTNode& n) { return n.kind == ASTNodeKind::Sequence; }, sequences);
    if (sequences.empty()) return false;

    // Pick a random Sequence
    std::uniform_int_distribution<size_t> seq_dist(0, sequences.size() - 1);
    ASTNode* target_seq = sequences[seq_dist(rng_)];

    // Pick a random insertion position (0 to children.size() inclusive)
    std::uniform_int_distribution<size_t> pos_dist(0, target_seq->children.size());
    size_t insert_pos = pos_dist(rng_);

    // 70% grow-word, 30% grow-literal
    std::uniform_int_distribution<int> choice(0, 9);
    ASTNode new_node;

    if (choice(rng_) < 7) {
        // Grow-word: prefer (1,1) stack-neutral words
        // Use pool if configured, otherwise full dictionary
        std::vector<std::string> candidates;
        if (word_pool_ && !word_pool_->empty()) {
            candidates = index_.find_restricted(1, 1, *word_pool_);
            if (candidates.empty())
                candidates = index_.find_restricted(0, 1, *word_pool_);
        } else {
            candidates = index_.find_compatible(1, 1);
            if (candidates.empty())
                candidates = index_.find_compatible(0, 1);
        }
        if (candidates.empty()) return false;

        std::uniform_int_distribution<size_t> word_dist(0, candidates.size() - 1);
        new_node = ASTNode::make_word_call(candidates[word_dist(rng_)]);

        if (logger_ && logger_->enabled(EvolveLogCategory::Grow)) {
            logger_->log(EvolveLogCategory::Grow,
                "Inserted word '" + new_node.word_name
                + "' at position " + std::to_string(insert_pos)
                + " (" + std::to_string(count_nodes(ast)) + " nodes)");
        }
    } else {
        // Grow-literal: random int [-10, 10] or float [-1.0, 1.0]
        std::uniform_int_distribution<int> type_choice(0, 1);
        if (type_choice(rng_) == 0) {
            std::uniform_int_distribution<int64_t> int_dist(-10, 10);
            int64_t val = int_dist(rng_);
            new_node = ASTNode::make_literal_int(val);
            if (logger_ && logger_->enabled(EvolveLogCategory::Grow)) {
                logger_->log(EvolveLogCategory::Grow,
                    "Inserted literal " + std::to_string(val)
                    + " at position " + std::to_string(insert_pos));
            }
        } else {
            std::uniform_real_distribution<double> float_dist(-1.0, 1.0);
            double val = float_dist(rng_);
            new_node = ASTNode::make_literal_float(val);
            if (logger_ && logger_->enabled(EvolveLogCategory::Grow)) {
                logger_->log(EvolveLogCategory::Grow,
                    "Inserted literal " + std::to_string(val)
                    + " at position " + std::to_string(insert_pos));
            }
        }
    }

    target_seq->children.insert(
        target_seq->children.begin() + static_cast<long>(insert_pos),
        std::move(new_node));
    return true;
}

// --- Shrink: remove a node from a Sequence ---

bool ASTGeneticOps::shrink_node(ASTNode& ast) {
    // Find all Sequence nodes with ≥2 children
    std::vector<ASTNode*> sequences;
    collect_nodes(ast, [](const ASTNode& n) {
        return n.kind == ASTNodeKind::Sequence && n.children.size() >= 2;
    }, sequences);
    if (sequences.empty()) return false;

    // Pick a random Sequence
    std::uniform_int_distribution<size_t> seq_dist(0, sequences.size() - 1);
    ASTNode* target_seq = sequences[seq_dist(rng_)];

    // Find removable children (WordCall or Literal only — never control flow)
    std::vector<size_t> removable;
    for (size_t i = 0; i < target_seq->children.size(); ++i) {
        auto kind = target_seq->children[i].kind;
        if (kind == ASTNodeKind::WordCall || kind == ASTNodeKind::Literal) {
            removable.push_back(i);
        }
    }
    if (removable.empty()) return false;

    // Pick a random removable child
    std::uniform_int_distribution<size_t> rem_dist(0, removable.size() - 1);
    size_t remove_idx = removable[rem_dist(rng_)];

    if (logger_ && logger_->enabled(EvolveLogCategory::Shrink)) {
        auto& child = target_seq->children[remove_idx];
        std::string desc = (child.kind == ASTNodeKind::WordCall)
            ? "word '" + child.word_name + "'"
            : "literal";
        logger_->log(EvolveLogCategory::Shrink,
            "Removed " + desc + " at position " + std::to_string(remove_idx)
            + " (" + std::to_string(count_nodes(ast)) + " nodes)");
    }

    target_seq->children.erase(
        target_seq->children.begin() + static_cast<long>(remove_idx));
    return true;
}

// --- Public API ---

WordImplPtr ASTGeneticOps::mutate(const WordImpl& parent) {
    auto bc = parent.bytecode();
    if (!bc) return WordImplPtr();

    // Decompile
    auto ast = decompiler_.decompile(*bc);

    // Weighted selection from 6 operators
    static const char* op_names[] = {
        "substitute", "perturb", "move", "control-flow", "grow", "shrink"
    };

    // Build weights vector from config (or defaults)
    MutationWeights w;
    if (config_) w = config_->mutation_weights;
    std::vector<double> weights = {
        w.substitute, w.perturb, w.move, w.control, w.grow, w.shrink
    };
    std::discrete_distribution<int> choice(weights.begin(), weights.end());

    auto try_operator = [&](int op, ASTNode& a) -> bool {
        switch (op) {
            case 0: return substitute_call(a);
            case 1: return perturb_constant(a);
            case 2: return move_block(a);
            case 3: return mutate_control_flow(a);
            case 4: return grow_node(a);
            case 5: return shrink_node(a);
            default: return false;
        }
    };

    int first = choice(rng_);
    if (logger_ && logger_->enabled(EvolveLogCategory::Engine)) {
        logger_->log(EvolveLogCategory::Engine,
            "Selected operator: " + std::string(op_names[first]));
    }

    bool mutated = try_operator(first, ast);

    // If first choice failed, try the others in order
    if (!mutated) {
        for (int i = 0; i < 6 && !mutated; ++i) {
            if (i == first) continue;
            if (logger_ && logger_->granular(EvolveLogCategory::Engine)) {
                logger_->detail(EvolveLogCategory::Engine,
                    std::string(op_names[first]) + " failed, trying " + op_names[i]);
            }
            mutated = try_operator(i, ast);
        }
    }
    if (!mutated) {
        if (logger_ && logger_->enabled(EvolveLogCategory::Engine)) {
            logger_->log(EvolveLogCategory::Engine, "All 6 operators failed");
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
