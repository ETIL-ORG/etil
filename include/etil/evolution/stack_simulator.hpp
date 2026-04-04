#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/ast.hpp"
#include "etil/core/word_impl.hpp"
#include "etil/core/dictionary.hpp"

#include <unordered_map>
#include <vector>

namespace etil::evolution {

using SigType = etil::core::TypeSignature::Type;

/// Type state at a specific AST node position (before the node executes).
struct TypeState {
    std::vector<SigType> stack_types;  // types from bottom to TOS
    bool valid = true;  // false if simulation state was invalid at this point
};

/// Simulates the type stack through an AST to compute stack effects
/// and annotate each node with its consumed/produced counts.
class StackSimulator {
public:
    /// Annotate all nodes in an AST with stack effects.
    /// Also records the type state before each node (queryable via types_at()).
    /// Returns false if a type error is detected.
    bool annotate(ASTNode& ast, const etil::core::Dictionary& dict);

    /// Get the type state before a specific node executes (after annotate()).
    /// Returns an invalid TypeState if the node was not seen during annotation.
    TypeState types_at(const ASTNode* node) const;

    /// Infer the TypeSignature for a word's AST.
    /// Returns a signature with variable_inputs/outputs flags set appropriately.
    etil::core::TypeSignature infer_signature(
        ASTNode& ast, const etil::core::Dictionary& dict);

private:
    struct SimState {
        std::vector<SigType> type_stack;
        int initial_depth = 0;
        bool valid = true;
    };

    void simulate_node(ASTNode& node, SimState& state,
                       const etil::core::Dictionary& dict);

    void apply_word_signature(const etil::core::TypeSignature& sig,
                              SimState& state);

    void apply_shuffle(const std::string& word, SimState& state);

    bool is_shuffle_word(const std::string& word) const;
    bool is_opaque_word(const std::string& word) const;

    /// Infer input types by analyzing which concrete-typed words first consume each input.
    void infer_input_types(ASTNode& ast, const etil::core::Dictionary& dict,
                           int n_inputs, std::vector<SigType>& input_types);

    /// Per-node type state snapshots, populated during annotate().
    std::unordered_map<const ASTNode*, TypeState> type_states_;
};

} // namespace etil::evolution
