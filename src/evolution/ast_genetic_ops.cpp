// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/ast_genetic_ops.hpp"
#include "etil/evolution/evolution_engine.hpp"
#include "etil/evolution/evolve_logger.hpp"
#include "etil/evolution/mutation_helpers.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>

namespace etil::evolution {

using namespace etil::core;

// --- Inverse bridge pairs (symmetric) ---
// Inserting one next to its inverse creates a no-op cycle.

static const std::unordered_map<std::string, std::string>& inverse_bridges() {
    static const std::unordered_map<std::string, std::string> pairs = {
        {"int->float",     "float->int"},
        {"float->int",     "int->float"},
        {"string->bytes",  "bytes->string"},
        {"bytes->string",  "string->bytes"},
        {"ssplit",         "sjoin"},
        {"sjoin",          "ssplit"},
        {"map->json",      "json->map"},
        {"json->map",      "map->json"},
        {"array->mat",     "mat->array"},
        {"mat->array",     "array->mat"},
    };
    return pairs;
}

static bool is_bridge_word(const std::string& word) {
    return inverse_bridges().count(word) > 0;
}

bool ASTGeneticOps::is_inverse_bridge(
    const ASTNode& seq, size_t position, const std::string& bridge_word) {
    const auto& inv = inverse_bridges();
    auto it = inv.find(bridge_word);
    if (it == inv.end()) return false;  // not a bridge word — no cycle possible
    const auto& inverse = it->second;

    // Check the node before the insertion point
    if (position > 0 && position - 1 < seq.children.size()) {
        const auto& prev = seq.children[position - 1];
        if (prev.kind == ASTNodeKind::WordCall && prev.word_name == inverse)
            return true;
    }
    // Check the node at the insertion point (will be pushed right after insert)
    if (position < seq.children.size()) {
        const auto& next = seq.children[position];
        if (next.kind == ASTNodeKind::WordCall && next.word_name == inverse)
            return true;
    }
    return false;
}

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

    // Annotate AST to get type states at each node
    simulator_.annotate(ast, dict_);

    // Pick a random call to substitute
    std::uniform_int_distribution<size_t> dist(0, calls.size() - 1);
    ASTNode* target = calls[dist(rng_)];

    // Look up current word's signature
    auto impl = dict_.lookup(target->word_name);
    if (!impl) return false;
    const auto& sig = (*impl)->signature();
    int consumed = static_cast<int>(sig.inputs.size());
    int produced = static_cast<int>(sig.outputs.size());

    // Extract stack types at the target node for type-directed filtering
    auto type_state = simulator_.types_at(target);
    std::vector<TypeSignature::Type> stack_types;
    if (type_state.valid && !type_state.stack_types.empty() && consumed > 0) {
        // Take the top 'consumed' types from the stack (deepest consumed first)
        size_t stack_sz = type_state.stack_types.size();
        size_t start = (stack_sz >= static_cast<size_t>(consumed))
                       ? stack_sz - static_cast<size_t>(consumed) : 0;
        for (size_t i = start; i < stack_sz; ++i) {
            stack_types.push_back(type_state.stack_types[i]);
        }
    }

    // Find compatible alternatives — pool-restricted or tiered
    std::string old_name = target->word_name;
    std::string chosen_name;
    int chosen_level = 3;
    size_t l1 = 0, l2 = 0, l3 = 0;

    if (word_pool_ && !word_pool_->empty()) {
        // Pool-restricted: type-compatible words within the pool
        auto type_compat = index_.find_type_compatible(consumed, produced, stack_types);
        // Intersect with pool
        std::vector<std::string> restricted;
        for (const auto& name : type_compat) {
            for (const auto& p : *word_pool_) {
                if (name == p) { restricted.push_back(name); break; }
            }
        }
        restricted.erase(
            std::remove(restricted.begin(), restricted.end(), target->word_name),
            restricted.end());
        if (restricted.empty()) return false;
        std::uniform_int_distribution<size_t> rdist(0, restricted.size() - 1);
        chosen_name = restricted[rdist(rng_)];
        l1 = restricted.size();
        chosen_level = 1;
    } else {
        // Type-compatible candidates with tiered ranking from full dictionary
        auto type_compat = index_.find_type_compatible(consumed, produced, stack_types);

        // Apply tiered ranking to the type-compatible set
        auto target_tags = index_.get_tags(target->word_name);
        std::vector<std::pair<std::string, int>> candidates;
        for (const auto& name : type_compat) {
            if (name == target->word_name) continue;
            auto word_tags = index_.get_tags(name);
            int level = 3;
            if (!target_tags.empty() && !word_tags.empty()) {
                size_t matches = 0;
                for (const auto& t : target_tags) {
                    for (const auto& wt : word_tags) {
                        if (t == wt) { matches++; break; }
                    }
                }
                if (matches == target_tags.size() && matches == word_tags.size()) {
                    level = 1;
                } else if (matches > 0) {
                    level = 2;
                }
            }
            candidates.push_back({name, level});
        }
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

    // TBBP: record bridge usage if the chosen word is a bridge edge from
    // the TOS type. Note: in practice this rarely fires because mutation
    // operators select candidates by depth compatibility and Unknown-input
    // permissiveness, not by bridge role. The primary TBBP signal comes
    // from BridgeMap::select_path() in TypeRepair, which is called when
    // the chosen word must act AS a bridge (converting types).
    if (bridge_map_ && !stack_types.empty()) {
        bridge_map_->try_record_bridge_usage(stack_types.back(), chosen_name);
    }

    if (logger_ && logger_->enabled(EvolveLogCategory::Substitute)) {
        std::string pool_tag = (word_pool_ && !word_pool_->empty()) ? " [pool]" : "";
        std::string type_tag = stack_types.empty() ? "" : " [typed]";
        logger_->log(EvolveLogCategory::Substitute,
            "'" + old_name + "' → '" + chosen_name
            + "' (Level " + std::to_string(chosen_level)
            + ", candidates: L1=" + std::to_string(l1)
            + " L2=" + std::to_string(l2)
            + " L3=" + std::to_string(l3) + ")" + pool_tag + type_tag);
    }
    if ((is_bridge_word(old_name) || is_bridge_word(chosen_name)) &&
        logger_ && logger_->enabled(EvolveLogCategory::Bridge)) {
        logger_->log(EvolveLogCategory::Bridge,
            "substitute: '" + old_name + "' → '" + chosen_name + "'");
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

    // Annotate AST to get type states at each node
    simulator_.annotate(ast, dict_);

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

    // Get stack types at the insertion point for type-directed filtering
    std::vector<TypeSignature::Type> stack_types;
    if (insert_pos < target_seq->children.size()) {
        // State before the node at insert_pos = state at insertion point
        auto ts = simulator_.types_at(&target_seq->children[insert_pos]);
        if (ts.valid) stack_types = ts.stack_types;
    } else if (insert_pos == 0) {
        // Inserting at start of sequence — use sequence's own state
        auto ts = simulator_.types_at(target_seq);
        if (ts.valid) stack_types = ts.stack_types;
    }
    // else: inserting at end — no recorded state, fall back to depth-only

    // Extract just TOS for (1,1) word filtering
    std::vector<TypeSignature::Type> tos_type;
    if (!stack_types.empty()) {
        tos_type.push_back(stack_types.back());
    }

    // 70% grow-word, 30% grow-literal
    std::uniform_int_distribution<int> choice(0, 9);
    ASTNode new_node;

    if (choice(rng_) < 7) {
        // Grow-word: prefer (1,1) stack-neutral words, type-directed
        std::vector<std::string> candidates;
        if (word_pool_ && !word_pool_->empty()) {
            // Pool-restricted: type-compatible within pool
            auto type_compat = index_.find_type_compatible(1, 1, tos_type);
            for (const auto& name : type_compat) {
                for (const auto& p : *word_pool_) {
                    if (name == p) { candidates.push_back(name); break; }
                }
            }
            if (candidates.empty()) {
                // Fall back to pool-restricted (0,1)
                candidates = index_.find_restricted(0, 1, *word_pool_);
            }
        } else {
            candidates = index_.find_type_compatible(1, 1, tos_type);
            if (candidates.empty())
                candidates = index_.find_compatible(0, 1);
        }
        if (candidates.empty()) return false;

        // Select a word, rejecting inverse bridge cycles (try up to 5 times)
        std::uniform_int_distribution<size_t> word_dist(0, candidates.size() - 1);
        std::string chosen;
        for (int attempt = 0; attempt < 5; ++attempt) {
            chosen = candidates[word_dist(rng_)];
            if (!is_inverse_bridge(*target_seq, insert_pos, chosen)) break;
            if (logger_ && logger_->enabled(EvolveLogCategory::Bridge)) {
                // Find what the inverse is
                std::string inverse_of;
                if (insert_pos > 0 && insert_pos - 1 < target_seq->children.size() &&
                    target_seq->children[insert_pos - 1].kind == ASTNodeKind::WordCall)
                    inverse_of = target_seq->children[insert_pos - 1].word_name;
                else if (insert_pos < target_seq->children.size() &&
                         target_seq->children[insert_pos].kind == ASTNodeKind::WordCall)
                    inverse_of = target_seq->children[insert_pos].word_name;
                logger_->log(EvolveLogCategory::Bridge,
                    "cycle: " + chosen + " rejected (inverse of "
                    + inverse_of + " at position " + std::to_string(insert_pos) + ")");
            }
            chosen.clear();
        }
        if (chosen.empty()) return false;
        new_node = ASTNode::make_word_call(chosen);

        // TBBP: record bridge usage if the grown word is a bridge
        if (bridge_map_ && !tos_type.empty()) {
            bridge_map_->try_record_bridge_usage(tos_type[0], chosen);
        }

        if (logger_ && logger_->enabled(EvolveLogCategory::Grow)) {
            std::string type_tag = tos_type.empty() ? "" : " [typed]";
            logger_->log(EvolveLogCategory::Grow,
                "Inserted word '" + new_node.word_name
                + "' at position " + std::to_string(insert_pos)
                + " (" + std::to_string(count_nodes(ast)) + " nodes)" + type_tag);
        }
        if (is_bridge_word(new_node.word_name) &&
            logger_ && logger_->enabled(EvolveLogCategory::Bridge)) {
            logger_->log(EvolveLogCategory::Bridge,
                "grow: " + new_node.word_name + " at position "
                + std::to_string(insert_pos));
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

    if (logger_ && (logger_->enabled(EvolveLogCategory::Diff) ||
                     logger_->enabled(EvolveLogCategory::ASTDump))) {
        logger_->log(EvolveLogCategory::Engine,
            "===== MUTATE impl#" + std::to_string(parent.id())
            + " (gen " + std::to_string(parent.generation())
            + ", '" + parent.name() + "') BEGIN =====");
    }

    // Decompile
    auto ast = decompiler_.decompile(*bc);

    // Capture before-state for diff
    std::vector<std::string> before_code;
    if (logger_ && logger_->enabled(EvolveLogCategory::Diff)) {
        before_code = format_ast_as_code(ast);
    }

    // AST dump after decompilation
    if (logger_ && logger_->enabled(EvolveLogCategory::ASTDump)) {
        logger_->log(EvolveLogCategory::ASTDump,
            "AST DECOMPILED:\n" + ast_to_string(ast));
    }

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
            logger_->log(EvolveLogCategory::Engine,
                "===== MUTATE impl#" + std::to_string(parent.id()) + " END (all failed) =====");
        }
        return WordImplPtr();
    }

    // Capture after-mutation state for diff
    std::vector<std::string> after_code;
    std::string mutation_desc = op_names[first];
    if (logger_ && logger_->enabled(EvolveLogCategory::Diff)) {
        after_code = format_ast_as_code(ast);
    }

    // AST dump after mutation
    if (logger_ && logger_->enabled(EvolveLogCategory::ASTDump)) {
        logger_->log(EvolveLogCategory::ASTDump,
            "AST AFTER MUTATION (" + mutation_desc + "):\n" + ast_to_string(ast));
    }

    // Repair type mismatches
    bool repaired = repair_.repair(ast, dict_);
    if (logger_ && logger_->enabled(EvolveLogCategory::Repair)) {
        logger_->log(EvolveLogCategory::Repair,
            repaired ? "Type repair succeeded" : "Type repair failed (unrepairable)");
    }

    // Capture after-repair state and emit diff
    if (logger_ && logger_->enabled(EvolveLogCategory::Diff)) {
        std::vector<std::string> repair_code;
        std::string repair_desc;
        if (repaired) {
            repair_code = format_ast_as_code(ast);
            if (repair_code != after_code) {
                repair_desc = "shuffles inserted for type balance";
            }
        } else {
            repair_desc = "unrepairable";
        }

        bool show = repaired || (logger_->show_failed());
        if (show) {
            logger_->log(EvolveLogCategory::Diff,
                "\n" + format_mutation_diff(
                    before_code, after_code, mutation_desc,
                    (repair_code != after_code) ? repair_code : std::vector<std::string>{},
                    repair_desc, repaired));
        }
    }

    // AST dump after repair
    if (repaired && logger_ && logger_->enabled(EvolveLogCategory::ASTDump)) {
        logger_->log(EvolveLogCategory::ASTDump,
            "AST AFTER REPAIR:\n" + ast_to_string(ast));
    }

    if (!repaired) {
        if (logger_ && (logger_->enabled(EvolveLogCategory::Diff) ||
                         logger_->enabled(EvolveLogCategory::ASTDump))) {
            logger_->log(EvolveLogCategory::Engine,
                "===== MUTATE impl#" + std::to_string(parent.id()) + " END (rejected) =====");
        }
        return WordImplPtr();
    }

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

    if (logger_ && (logger_->enabled(EvolveLogCategory::Diff) ||
                     logger_->enabled(EvolveLogCategory::ASTDump))) {
        logger_->log(EvolveLogCategory::Engine,
            "===== MUTATE impl#" + std::to_string(parent.id())
            + " → impl#" + std::to_string(id) + " END (success) =====");
    }
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
