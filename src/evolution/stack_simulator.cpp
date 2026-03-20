// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/stack_simulator.hpp"

namespace etil::evolution {

using namespace etil::core;
using T = TypeSignature::Type;

bool StackSimulator::annotate(ASTNode& ast, const Dictionary& dict) {
    SimState state;
    state.initial_depth = 0;
    simulate_node(ast, state, dict);
    return state.valid;
}

TypeSignature StackSimulator::infer_signature(
    ASTNode& ast, const Dictionary& dict) {

    SimState state;
    state.initial_depth = 0;
    simulate_node(ast, state, dict);

    TypeSignature sig;
    if (!state.valid) {
        sig.variable_inputs = true;
        sig.variable_outputs = true;
        return sig;
    }

    // The number of items consumed from below what the word pushed
    int net = static_cast<int>(state.type_stack.size()) - state.initial_depth;
    // If we went below initial depth, that's how many inputs were needed
    // We track this by how many Unknown types we had to assume

    // For a simple approach: inputs = items consumed, outputs = items produced
    // We can read them from the effect annotation on the root node
    if (ast.effect.valid) {
        // Build input types (we may not know them precisely)
        for (int i = 0; i < ast.effect.consumed; ++i) {
            sig.inputs.push_back(T::Unknown);
        }
        for (int i = 0; i < ast.effect.produced; ++i) {
            if (i < static_cast<int>(state.type_stack.size())) {
                sig.outputs.push_back(state.type_stack[static_cast<size_t>(i)]);
            } else {
                sig.outputs.push_back(T::Unknown);
            }
        }
    }
    return sig;
}

bool StackSimulator::is_opaque_word(const std::string& word) const {
    return word == "execute" || word == "evaluate";
}

bool StackSimulator::is_shuffle_word(const std::string& word) const {
    return word == "dup" || word == "drop" || word == "swap" ||
           word == "over" || word == "rot" || word == "nip" ||
           word == "tuck" || word == "pick" || word == "roll" ||
           word == "?dup" || word == "-rot";
}

void StackSimulator::apply_shuffle(const std::string& word, SimState& state) {
    auto& s = state.type_stack;

    // Ensure minimum stack depth, adding Unknown inputs as needed
    auto ensure_depth = [&](size_t needed) {
        while (s.size() < needed) {
            s.insert(s.begin(), T::Unknown);
            state.initial_depth--;
        }
    };

    if (word == "dup") {
        ensure_depth(1);
        s.push_back(s.back());
    } else if (word == "drop") {
        ensure_depth(1);
        s.pop_back();
    } else if (word == "swap") {
        ensure_depth(2);
        auto sz = s.size();
        std::swap(s[sz - 1], s[sz - 2]);
    } else if (word == "over") {
        ensure_depth(2);
        s.push_back(s[s.size() - 2]);
    } else if (word == "rot") {
        ensure_depth(3);
        auto sz = s.size();
        auto tmp = s[sz - 3];
        s[sz - 3] = s[sz - 2];
        s[sz - 2] = s[sz - 1];
        s[sz - 1] = tmp;
    } else if (word == "-rot") {
        ensure_depth(3);
        auto sz = s.size();
        auto tmp = s[sz - 1];
        s[sz - 1] = s[sz - 2];
        s[sz - 2] = s[sz - 3];
        s[sz - 3] = tmp;
    } else if (word == "nip") {
        ensure_depth(2);
        s.erase(s.end() - 2);
    } else if (word == "tuck") {
        ensure_depth(2);
        s.insert(s.end() - 2, s.back());
    } else if (word == "?dup") {
        ensure_depth(1);
        s.push_back(s.back());
    } else {
        // pick, roll — can't simulate without runtime index
        state.valid = false;
    }
}

void StackSimulator::apply_word_signature(const TypeSignature& sig, SimState& state) {
    auto& s = state.type_stack;

    // Pop inputs — if stack is empty, the word needs external inputs
    for (size_t i = 0; i < sig.inputs.size(); ++i) {
        if (s.empty()) {
            state.initial_depth--;
        } else {
            s.pop_back();
        }
    }

    // Push outputs
    for (const auto& t : sig.outputs) {
        s.push_back(t);
    }
}

void StackSimulator::simulate_node(
    ASTNode& node, SimState& state, const Dictionary& dict) {

    size_t depth_before = state.type_stack.size();
    int initial_before = state.initial_depth;

    switch (node.kind) {

    case ASTNodeKind::Literal:
        switch (node.literal_op) {
            case Instruction::Op::PushInt:    state.type_stack.push_back(T::Integer); break;
            case Instruction::Op::PushFloat:  state.type_stack.push_back(T::Float); break;
            case Instruction::Op::PushBool:   state.type_stack.push_back(T::Boolean); break;
            case Instruction::Op::PushString: state.type_stack.push_back(T::String); break;
            case Instruction::Op::PushJson:   state.type_stack.push_back(T::Json); break;
            default: state.type_stack.push_back(T::Unknown); break;
        }
        break;

    case ASTNodeKind::WordCall: {
        if (is_shuffle_word(node.word_name)) {
            apply_shuffle(node.word_name, state);
        } else if (is_opaque_word(node.word_name)) {
            // Category 3: fully opaque (execute, evaluate)
            state.valid = false;
        } else {
            auto impl = dict.lookup(node.word_name);
            if (impl && (!(*impl)->signature().inputs.empty() ||
                         !(*impl)->signature().outputs.empty())) {
                apply_word_signature((*impl)->signature(), state);
            } else if (impl) {
                // Word exists but has no signature — passthrough
            } else {
                // Unknown word
                state.valid = false;
            }
        }
        break;
    }

    case ASTNodeKind::PrintString:
        // No stack effect
        break;

    case ASTNodeKind::PushXt:
        state.type_stack.push_back(T::Xt);
        break;

    case ASTNodeKind::ToR:
        if (!state.type_stack.empty()) state.type_stack.pop_back();
        else state.initial_depth--;
        break;

    case ASTNodeKind::FromR:
        state.type_stack.push_back(T::Unknown);  // type lost through return stack
        break;

    case ASTNodeKind::FetchR:
        state.type_stack.push_back(T::Unknown);
        break;

    case ASTNodeKind::DoI:
    case ASTNodeKind::DoJ:
        state.type_stack.push_back(T::Integer);
        break;

    case ASTNodeKind::Leave:
    case ASTNodeKind::Exit:
    case ASTNodeKind::PushDataPtr:
    case ASTNodeKind::SetDoes:
        // Complex effects — don't track
        break;

    case ASTNodeKind::Sequence:
        for (auto& child : node.children) {
            simulate_node(child, state, dict);
            if (!state.valid) break;
        }
        break;

    case ASTNodeKind::IfThen:
        // Condition consumed 1 boolean (already on stack from preceding nodes)
        if (!state.type_stack.empty()) state.type_stack.pop_back();
        else state.initial_depth--;
        // Then-body should have net 0 effect (FORTH convention)
        if (!node.children.empty()) {
            simulate_node(node.children[0], state, dict);
        }
        break;

    case ASTNodeKind::IfThenElse:
        if (!state.type_stack.empty()) state.type_stack.pop_back();
        else state.initial_depth--;
        // Both branches must have same effect; simulate then-branch
        if (!node.children.empty()) {
            simulate_node(node.children[0], state, dict);
        }
        break;

    case ASTNodeKind::DoLoop:
    case ASTNodeKind::DoPlusLoop:
        // DO consumes 2 (limit, index) from the stack
        for (int i = 0; i < 2; ++i) {
            if (!state.type_stack.empty()) state.type_stack.pop_back();
            else state.initial_depth--;
        }
        // Loop body runs multiple times — simulate once for effect tracking
        if (!node.children.empty()) {
            SimState body_state = state;
            simulate_node(node.children[0], body_state, dict);
            int body_net = static_cast<int>(body_state.type_stack.size()) -
                           static_cast<int>(state.type_stack.size());
            if (body_net != 0) {
                // Loop pushes per iteration — Category 2 (variable outputs)
                state.valid = false;
            } else {
                state = body_state;
            }
        }
        break;

    case ASTNodeKind::BeginUntil:
    case ASTNodeKind::BeginWhileRepeat:
    case ASTNodeKind::BeginAgain:
        // Loops: simulate body once
        if (!node.children.empty()) {
            simulate_node(node.children[0], state, dict);
        }
        // For while/repeat: condition consumed 1 boolean
        if (node.kind == ASTNodeKind::BeginWhileRepeat && node.children.size() > 1) {
            if (!state.type_stack.empty()) state.type_stack.pop_back();
            else state.initial_depth--;
            simulate_node(node.children[1], state, dict);
        }
        break;
    }

    // Annotate the node with its effect
    auto& effect = node.effect;
    int external_consumed = initial_before - state.initial_depth;
    int final_depth = static_cast<int>(state.type_stack.size());
    effect.consumed = external_consumed;
    effect.produced = final_depth;
    effect.valid = state.valid;
}

} // namespace etil::evolution
