// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/ast.hpp"

#include <sstream>

namespace etil::evolution {

using Op = etil::core::Instruction::Op;

ASTNode ASTNode::make_literal_int(int64_t val) {
    ASTNode n;
    n.kind = ASTNodeKind::Literal;
    n.literal_op = Op::PushInt;
    n.int_val = val;
    return n;
}

ASTNode ASTNode::make_literal_float(double val) {
    ASTNode n;
    n.kind = ASTNodeKind::Literal;
    n.literal_op = Op::PushFloat;
    n.float_val = val;
    return n;
}

ASTNode ASTNode::make_literal_bool(bool val) {
    ASTNode n;
    n.kind = ASTNodeKind::Literal;
    n.literal_op = Op::PushBool;
    n.int_val = val ? 1 : 0;
    return n;
}

ASTNode ASTNode::make_literal_string(const std::string& val) {
    ASTNode n;
    n.kind = ASTNodeKind::Literal;
    n.literal_op = Op::PushString;
    n.string_val = val;
    return n;
}

ASTNode ASTNode::make_literal_json(const std::string& val) {
    ASTNode n;
    n.kind = ASTNodeKind::Literal;
    n.literal_op = Op::PushJson;
    n.string_val = val;
    return n;
}

ASTNode ASTNode::make_word_call(const std::string& name) {
    ASTNode n;
    n.kind = ASTNodeKind::WordCall;
    n.word_name = name;
    return n;
}

ASTNode ASTNode::make_print_string(const std::string& text) {
    ASTNode n;
    n.kind = ASTNodeKind::PrintString;
    n.string_val = text;
    return n;
}

ASTNode ASTNode::make_push_xt(const std::string& name) {
    ASTNode n;
    n.kind = ASTNodeKind::PushXt;
    n.word_name = name;
    return n;
}

ASTNode ASTNode::make_leaf(ASTNodeKind kind) {
    ASTNode n;
    n.kind = kind;
    return n;
}

ASTNode ASTNode::make_sequence(std::vector<ASTNode> children) {
    ASTNode n;
    n.kind = ASTNodeKind::Sequence;
    n.children = std::move(children);
    return n;
}

// --- Debug printing ---

static const char* kind_name(ASTNodeKind k) {
    switch (k) {
        case ASTNodeKind::Literal:            return "Literal";
        case ASTNodeKind::WordCall:           return "WordCall";
        case ASTNodeKind::Sequence:           return "Sequence";
        case ASTNodeKind::IfThen:             return "IfThen";
        case ASTNodeKind::IfThenElse:         return "IfThenElse";
        case ASTNodeKind::DoLoop:             return "DoLoop";
        case ASTNodeKind::DoPlusLoop:         return "DoPlusLoop";
        case ASTNodeKind::BeginUntil:         return "BeginUntil";
        case ASTNodeKind::BeginWhileRepeat:   return "BeginWhileRepeat";
        case ASTNodeKind::BeginAgain:         return "BeginAgain";
        case ASTNodeKind::PrintString:        return "PrintString";
        case ASTNodeKind::PushXt:             return "PushXt";
        case ASTNodeKind::ToR:                return "ToR";
        case ASTNodeKind::FromR:              return "FromR";
        case ASTNodeKind::FetchR:             return "FetchR";
        case ASTNodeKind::DoI:                return "DoI";
        case ASTNodeKind::DoJ:                return "DoJ";
        case ASTNodeKind::Leave:              return "Leave";
        case ASTNodeKind::Exit:               return "Exit";
        case ASTNodeKind::PushDataPtr:        return "PushDataPtr";
        case ASTNodeKind::SetDoes:            return "SetDoes";
    }
    return "Unknown";
}

size_t count_nodes(const ASTNode& node) {
    size_t count = 1;
    for (const auto& child : node.children) {
        count += count_nodes(child);
    }
    return count;
}

std::string ast_to_string(const ASTNode& node, int indent) {
    std::ostringstream os;
    std::string pad(static_cast<size_t>(indent * 2), ' ');

    os << pad << kind_name(node.kind);

    switch (node.kind) {
        case ASTNodeKind::Literal:
            if (node.literal_op == Op::PushInt)
                os << " " << node.int_val;
            else if (node.literal_op == Op::PushFloat)
                os << " " << node.float_val;
            else if (node.literal_op == Op::PushBool)
                os << " " << (node.int_val ? "true" : "false");
            else
                os << " \"" << node.string_val << "\"";
            os << "\n";
            break;
        case ASTNodeKind::WordCall:
            os << " " << node.word_name << "\n";
            break;
        case ASTNodeKind::PrintString:
            os << " \"" << node.string_val << "\"\n";
            break;
        case ASTNodeKind::PushXt:
            os << " " << node.word_name << "\n";
            break;
        default:
            os << "\n";
            for (const auto& child : node.children) {
                os << ast_to_string(child, indent + 1);
            }
            break;
    }
    return os.str();
}

} // namespace etil::evolution
