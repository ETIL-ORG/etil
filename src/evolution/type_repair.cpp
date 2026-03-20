// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/type_repair.hpp"

namespace etil::evolution {

using namespace etil::core;
using T = TypeSignature::Type;

bool TypeRepair::repair(ASTNode& ast, const Dictionary& dict) {
    if (ast.kind == ASTNodeKind::Sequence) {
        return repair_sequence(ast, dict);
    }
    // For non-sequence nodes, repair each child sequence
    for (auto& child : ast.children) {
        if (!repair(child, dict)) return false;
    }
    return true;
}

int TypeRepair::find_type_in_stack(
    const std::vector<SigType>& stack, SigType needed, size_t start_pos) {
    // Search from start_pos deeper into the stack
    // stack.back() is TOS (position 0), stack[size-2] is position 1, etc.
    for (size_t i = start_pos; i < stack.size(); ++i) {
        size_t idx = stack.size() - 1 - i;
        if (stack[idx] == needed || needed == T::Unknown || stack[idx] == T::Unknown) {
            return static_cast<int>(i);
        }
    }
    return -1;  // not found
}

std::vector<ASTNode> TypeRepair::compute_shuffle(size_t from_pos, size_t to_pos) {
    std::vector<ASTNode> nodes;
    if (from_pos == to_pos) return nodes;

    if (from_pos == 1 && to_pos == 0) {
        nodes.push_back(ASTNode::make_word_call("swap"));
    } else if (from_pos == 2 && to_pos == 0) {
        nodes.push_back(ASTNode::make_word_call("rot"));
    } else {
        // General case: push index, then roll
        nodes.push_back(ASTNode::make_literal_int(static_cast<int64_t>(from_pos)));
        nodes.push_back(ASTNode::make_word_call("roll"));
    }
    return nodes;
}

bool TypeRepair::repair_sequence(ASTNode& seq, const Dictionary& dict) {
    if (seq.kind != ASTNodeKind::Sequence) return true;

    std::vector<SigType> type_stack;
    std::vector<ASTNode> repaired_children;

    for (size_t ci = 0; ci < seq.children.size(); ++ci) {
        auto& child = seq.children[ci];

        // Recursively repair child control structures
        if (child.kind == ASTNodeKind::IfThen ||
            child.kind == ASTNodeKind::IfThenElse ||
            child.kind == ASTNodeKind::DoLoop ||
            child.kind == ASTNodeKind::DoPlusLoop ||
            child.kind == ASTNodeKind::BeginUntil ||
            child.kind == ASTNodeKind::BeginWhileRepeat ||
            child.kind == ASTNodeKind::BeginAgain) {
            if (!repair(child, dict)) return false;
            repaired_children.push_back(std::move(child));
            continue;
        }

        // For WordCall nodes, check type compatibility
        if (child.kind == ASTNodeKind::WordCall) {
            auto impl = dict.lookup(child.word_name);
            if (!impl) {
                repaired_children.push_back(std::move(child));
                continue;
            }

            const auto& sig = (*impl)->signature();
            if (sig.inputs.empty() && sig.outputs.empty()) {
                // No signature — pass through
                repaired_children.push_back(std::move(child));
                // Can't update type stack without knowing the effect
                continue;
            }

            // Check each input (TOS first)
            bool needs_repair = false;
            for (size_t i = 0; i < sig.inputs.size(); ++i) {
                size_t stack_pos = i;  // 0 = TOS
                SigType needed = sig.inputs[sig.inputs.size() - 1 - i];
                if (needed == T::Unknown) continue;  // any type accepted

                if (stack_pos >= type_stack.size()) continue;  // underflow, can't repair

                size_t stack_idx = type_stack.size() - 1 - stack_pos;
                SigType actual = type_stack[stack_idx];
                if (actual == T::Unknown) continue;  // don't know what's there
                if (actual == needed) continue;  // matches

                // Mismatch! Search deeper for the needed type
                int found = find_type_in_stack(type_stack, needed, stack_pos + 1);
                if (found < 0) {
                    // Needed type not on stack — unrepairable
                    return false;
                }

                // Insert shuffle to bring it to the right position
                auto shuffle = compute_shuffle(static_cast<size_t>(found), stack_pos);
                for (auto& s : shuffle) {
                    repaired_children.push_back(std::move(s));
                }

                // Apply the shuffle to our simulated type stack
                if (found == 1 && stack_pos == 0 && type_stack.size() >= 2) {
                    std::swap(type_stack[type_stack.size()-1], type_stack[type_stack.size()-2]);
                } else if (found == 2 && stack_pos == 0 && type_stack.size() >= 3) {
                    auto sz = type_stack.size();
                    auto tmp = type_stack[sz-3];
                    type_stack[sz-3] = type_stack[sz-2];
                    type_stack[sz-2] = type_stack[sz-1];
                    type_stack[sz-1] = tmp;
                }
                // For roll: more complex, but the type at found_pos moves to TOS
                needs_repair = true;
            }

            // Apply word's effect to type stack
            for (size_t i = 0; i < sig.inputs.size(); ++i) {
                if (!type_stack.empty()) type_stack.pop_back();
            }
            for (const auto& t : sig.outputs) {
                type_stack.push_back(t);
            }

            repaired_children.push_back(std::move(child));
            continue;
        }

        // For all other node types, simulate their type effect simply
        switch (child.kind) {
            case ASTNodeKind::Literal:
                switch (child.literal_op) {
                    case Instruction::Op::PushInt:    type_stack.push_back(T::Integer); break;
                    case Instruction::Op::PushFloat:  type_stack.push_back(T::Float); break;
                    case Instruction::Op::PushBool:   type_stack.push_back(T::Boolean); break;
                    case Instruction::Op::PushString: type_stack.push_back(T::String); break;
                    case Instruction::Op::PushJson:   type_stack.push_back(T::Json); break;
                    default: type_stack.push_back(T::Unknown); break;
                }
                break;
            case ASTNodeKind::PushXt:
                type_stack.push_back(T::Xt);
                break;
            case ASTNodeKind::DoI:
            case ASTNodeKind::DoJ:
                type_stack.push_back(T::Integer);
                break;
            case ASTNodeKind::ToR:
                if (!type_stack.empty()) type_stack.pop_back();
                break;
            case ASTNodeKind::FromR:
            case ASTNodeKind::FetchR:
                type_stack.push_back(T::Unknown);
                break;
            default:
                break;
        }

        repaired_children.push_back(std::move(child));
    }

    seq.children = std::move(repaired_children);
    return true;
}

} // namespace etil::evolution
