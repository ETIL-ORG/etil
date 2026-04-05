#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/ast.hpp"
#include "etil/evolution/bridge_map.hpp"
#include "etil/core/word_impl.hpp"
#include "etil/core/dictionary.hpp"

#include <vector>

namespace etil::evolution {

using SigType = etil::core::TypeSignature::Type;

/// Deterministic type-directed repair: walks a mutated AST, detects type
/// mismatches at each word call, and inserts minimal stack shuffling
/// (swap, rot, roll) to fix them. When a BridgeMap is available, can also
/// insert bridge words to convert types that aren't on the stack at all.
/// Returns false if unrepairable.
class TypeRepair {
public:
    /// Set the bridge map for bridge word insertion during repair.
    /// Non-const pointer because select_path mutates edge selection counters.
    void set_bridge_map(BridgeMap* map) { bridge_map_ = map; }

    /// Repair all type mismatches in the AST.
    /// Modifies the AST in-place by inserting shuffle and/or bridge nodes.
    /// Returns true if all mismatches resolved, false if unrepairable.
    bool repair(ASTNode& ast, const etil::core::Dictionary& dict);

    /// Compute the minimal shuffle sequence to bring the type at
    /// from_pos to to_pos (0 = TOS).
    static std::vector<ASTNode> compute_shuffle(size_t from_pos, size_t to_pos);

private:
    BridgeMap* bridge_map_ = nullptr;

    bool repair_sequence(ASTNode& seq, const etil::core::Dictionary& dict);

    int find_type_in_stack(const std::vector<SigType>& stack,
                           SigType needed, size_t start_pos);
};

} // namespace etil::evolution
