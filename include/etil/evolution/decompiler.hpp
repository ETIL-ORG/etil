#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/ast.hpp"
#include "etil/core/compiled_body.hpp"

namespace etil::evolution {

/// Decompiles marker-annotated bytecode into an AST.
class Decompiler {
public:
    /// Decompile a ByteCode object into an AST Sequence node.
    ASTNode decompile(const etil::core::ByteCode& code);

private:
    using Op = etil::core::Instruction::Op;

    ASTNode decompile_range(
        const std::vector<etil::core::Instruction>& instrs,
        size_t start, size_t end);

    ASTNode decompile_block(
        const std::vector<etil::core::Instruction>& instrs,
        size_t start, size_t end,
        etil::core::BlockKind kind);

    size_t find_matching_block_end(
        const std::vector<etil::core::Instruction>& instrs,
        size_t begin_ip);

    size_t find_separator(
        const std::vector<etil::core::Instruction>& instrs,
        size_t start, size_t end,
        etil::core::BlockKind kind);
};

} // namespace etil::evolution
