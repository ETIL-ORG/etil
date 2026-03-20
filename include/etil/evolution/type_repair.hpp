#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/ast.hpp"
#include "etil/core/word_impl.hpp"
#include "etil/core/dictionary.hpp"

#include <vector>

namespace etil::evolution {

using SigType = etil::core::TypeSignature::Type;

/// Deterministic type-directed repair: walks a mutated AST, detects type
/// mismatches at each word call, and inserts minimal stack shuffling
/// (swap, rot, roll) to fix them. Returns false if unrepairable.
class TypeRepair {
public:
    /// Repair all type mismatches in the AST.
    /// Modifies the AST in-place by inserting shuffle nodes.
    /// Returns true if all mismatches resolved, false if unrepairable.
    bool repair(ASTNode& ast, const etil::core::Dictionary& dict);

    /// Compute the minimal shuffle sequence to bring the type at
    /// from_pos to to_pos (0 = TOS).
    static std::vector<ASTNode> compute_shuffle(size_t from_pos, size_t to_pos);

private:
    bool repair_sequence(ASTNode& seq, const etil::core::Dictionary& dict);

    int find_type_in_stack(const std::vector<SigType>& stack,
                           SigType needed, size_t start_pos);
};

} // namespace etil::evolution
