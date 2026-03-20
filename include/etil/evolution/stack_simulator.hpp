#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/ast.hpp"
#include "etil/core/word_impl.hpp"
#include "etil/core/dictionary.hpp"

#include <vector>

namespace etil::evolution {

using SigType = etil::core::TypeSignature::Type;

/// Simulates the type stack through an AST to compute stack effects
/// and annotate each node with its consumed/produced counts.
class StackSimulator {
public:
    /// Annotate all nodes in an AST with stack effects.
    /// Returns false if a type error is detected.
    bool annotate(ASTNode& ast, const etil::core::Dictionary& dict);

    /// Infer the TypeSignature for a word's AST.
    /// Returns a signature with variable_inputs/outputs flags set appropriately.
    etil::core::TypeSignature infer_signature(
        const ASTNode& ast, const etil::core::Dictionary& dict);

private:
    struct SimState {
        std::vector<SigType> type_stack;
        int initial_depth = 0;
        bool valid = true;
    };

    void simulate_node(const ASTNode& node, SimState& state,
                       const etil::core::Dictionary& dict);

    void apply_word_signature(const etil::core::TypeSignature& sig,
                              SimState& state);

    void apply_shuffle(const std::string& word, SimState& state);

    bool is_shuffle_word(const std::string& word) const;
    bool is_opaque_word(const std::string& word) const;
};

} // namespace etil::evolution
