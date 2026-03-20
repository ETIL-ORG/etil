#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/ast.hpp"
#include "etil/core/compiled_body.hpp"

#include <memory>

namespace etil::evolution {

/// Compiles an AST back to bytecode with markers and backpatched branches.
class ASTCompiler {
public:
    /// Compile an AST node (typically a Sequence) into a ByteCode object.
    std::shared_ptr<etil::core::ByteCode> compile(const ASTNode& ast);

private:
    using Op = etil::core::Instruction::Op;

    void emit(etil::core::ByteCode& bc, const ASTNode& node);
    void emit_marker(etil::core::ByteCode& bc, Op op, etil::core::BlockKind kind);
    size_t emit_placeholder(etil::core::ByteCode& bc, Op op);
    static void backpatch_leaves(etil::core::ByteCode& bc, size_t body_start, size_t target);
};

} // namespace etil::evolution
