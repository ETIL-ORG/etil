#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/compiled_body.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace etil::evolution {

enum class ASTNodeKind {
    Literal,              // PushInt, PushFloat, PushBool, PushString, PushJson
    WordCall,             // Call instruction
    Sequence,             // Linear block of nodes
    IfThen,               // if ... then (children[0] = then-body)
    IfThenElse,           // if ... else ... then (children[0] = then-body, [1] = else-body)
    DoLoop,               // do ... loop (children[0] = loop body)
    DoPlusLoop,           // do ... +loop (children[0] = loop body)
    BeginUntil,           // begin ... until (children[0] = body including condition)
    BeginWhileRepeat,     // begin ... while ... repeat (children[0] = condition, [1] = body)
    BeginAgain,           // begin ... again (children[0] = body)
    PrintString,          // ." text"
    PushXt,               // ['] word
    ToR,                  // >r
    FromR,                // r>
    FetchR,               // r@
    DoI,                  // i
    DoJ,                  // j
    Leave,                // leave
    Exit,                 // exit
    PushDataPtr,          // CREATE data field
    SetDoes,              // does> body
};

struct ASTNode {
    ASTNodeKind kind = ASTNodeKind::Sequence;

    // Literal values (for Literal nodes)
    etil::core::Instruction::Op literal_op = etil::core::Instruction::Op::PushInt;
    int64_t int_val = 0;
    double float_val = 0.0;
    std::string string_val;

    // Word name (for WordCall, PushXt, PrintString)
    std::string word_name;

    // Children (for Sequence, control flow nodes)
    std::vector<ASTNode> children;

    // Stack effect (populated by simulator in Stage 3, not by decompiler)
    struct StackEffect {
        int consumed = -1;
        int produced = -1;
        bool valid = false;
    };
    StackEffect effect;

    // Source tracking (instruction index range in original bytecode)
    size_t source_ip_start = 0;
    size_t source_ip_end = 0;

    // Semantic annotation (populated in Stage 5)
    std::string category;
    std::vector<std::string> semantic_tags;

    // --- Factory helpers ---

    static ASTNode make_literal_int(int64_t val);
    static ASTNode make_literal_float(double val);
    static ASTNode make_literal_bool(bool val);
    static ASTNode make_literal_string(const std::string& val);
    static ASTNode make_literal_json(const std::string& val);
    static ASTNode make_word_call(const std::string& name);
    static ASTNode make_print_string(const std::string& text);
    static ASTNode make_push_xt(const std::string& name);
    static ASTNode make_leaf(ASTNodeKind kind);
    static ASTNode make_sequence(std::vector<ASTNode> children);
};

/// Recursively count all nodes in an AST.
size_t count_nodes(const ASTNode& node);

/// Format an AST as indented text for debugging.
std::string ast_to_string(const ASTNode& node, int indent = 0);

} // namespace etil::evolution
