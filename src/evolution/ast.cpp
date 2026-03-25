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

// --- Code rendering for diff view ---

static std::string render_node_inline(const ASTNode& node) {
    using Op = etil::core::Instruction::Op;
    switch (node.kind) {
        case ASTNodeKind::WordCall:    return node.word_name;
        case ASTNodeKind::PrintString: return ".\" " + node.string_val + "\"";
        case ASTNodeKind::PushXt:      return "['] " + node.word_name;
        case ASTNodeKind::ToR:         return ">r";
        case ASTNodeKind::FromR:       return "r>";
        case ASTNodeKind::FetchR:      return "r@";
        case ASTNodeKind::DoI:         return "i";
        case ASTNodeKind::DoJ:         return "j";
        case ASTNodeKind::Leave:       return "leave";
        case ASTNodeKind::Exit:        return "exit";
        case ASTNodeKind::Literal:
            switch (node.literal_op) {
                case Op::PushInt:    return std::to_string(node.int_val);
                case Op::PushFloat:  return std::to_string(node.float_val);
                case Op::PushBool:   return node.int_val ? "true" : "false";
                case Op::PushString: return "s\" " + node.string_val + "\"";
                default:             return "<literal>";
            }
        default: break;
    }
    return "<node>";
}

std::vector<std::string> format_ast_as_code(const ASTNode& node, int indent) {
    std::vector<std::string> lines;
    std::string pad(static_cast<size_t>(indent * 2), ' ');

    switch (node.kind) {
        case ASTNodeKind::Sequence:
            for (const auto& child : node.children) {
                auto child_lines = format_ast_as_code(child, indent);
                lines.insert(lines.end(), child_lines.begin(), child_lines.end());
            }
            break;
        case ASTNodeKind::IfThen:
            lines.push_back(pad + "if");
            if (!node.children.empty()) {
                auto body = format_ast_as_code(node.children[0], indent + 1);
                lines.insert(lines.end(), body.begin(), body.end());
            }
            lines.push_back(pad + "then");
            break;
        case ASTNodeKind::IfThenElse:
            lines.push_back(pad + "if");
            if (node.children.size() > 0) {
                auto body = format_ast_as_code(node.children[0], indent + 1);
                lines.insert(lines.end(), body.begin(), body.end());
            }
            lines.push_back(pad + "else");
            if (node.children.size() > 1) {
                auto body = format_ast_as_code(node.children[1], indent + 1);
                lines.insert(lines.end(), body.begin(), body.end());
            }
            lines.push_back(pad + "then");
            break;
        case ASTNodeKind::DoLoop:
            lines.push_back(pad + "do");
            if (!node.children.empty()) {
                auto body = format_ast_as_code(node.children[0], indent + 1);
                lines.insert(lines.end(), body.begin(), body.end());
            }
            lines.push_back(pad + "loop");
            break;
        case ASTNodeKind::BeginUntil:
            lines.push_back(pad + "begin");
            if (!node.children.empty()) {
                auto body = format_ast_as_code(node.children[0], indent + 1);
                lines.insert(lines.end(), body.begin(), body.end());
            }
            lines.push_back(pad + "until");
            break;
        default:
            lines.push_back(pad + render_node_inline(node));
            break;
    }
    return lines;
}

std::string format_mutation_diff(
    const std::vector<std::string>& before,
    const std::vector<std::string>& after,
    const std::string& mutation_desc,
    const std::vector<std::string>& after_repair,
    const std::string& repair_desc,
    bool success) {

    // Column width
    constexpr size_t COL = 28;

    auto pad_to = [](const std::string& s, size_t width) -> std::string {
        if (s.size() >= width) return s;
        return s + std::string(width - s.size(), ' ');
    };

    std::ostringstream out;

    // Header
    out << "+-  MUTATION: " << mutation_desc << "  ";
    size_t header_len = 14 + mutation_desc.size() + 2;
    if (header_len < COL * 3 + 6) out << std::string(COL * 3 + 6 - header_len, '-');
    out << "\n";

    out << "| " << pad_to("BEFORE", COL) << "| " << pad_to("AFTER", COL) << "| ANNOTATION\n";

    // Diff lines
    size_t max_lines = std::max(before.size(), after.size());
    for (size_t i = 0; i < max_lines; ++i) {
        std::string b = (i < before.size()) ? before[i] : "";
        std::string a = (i < after.size()) ? after[i] : "";
        std::string annot;

        if (i >= before.size()) {
            annot = "<- inserted";
        } else if (i >= after.size()) {
            annot = "<- removed";
        } else if (b != a) {
            annot = "<- changed";
        }

        out << "| " << pad_to(b, COL) << "| " << pad_to(a, COL) << "| " << annot << "\n";
    }

    // Repair section
    if (!after_repair.empty()) {
        out << "+- TYPE REPAIR ";
        out << std::string(COL * 3 - 10, '-') << "\n";
        out << "| " << pad_to("AFTER REPAIR", COL) << "| " << pad_to("", COL) << "| ANNOTATION\n";
        for (size_t i = 0; i < after_repair.size(); ++i) {
            std::string annot;
            // Check if this line was added by repair (not in after)
            if (i >= after.size() || (i < after.size() && after_repair[i] != after[i])) {
                annot = "<- repair";
            }
            out << "| " << pad_to(after_repair[i], COL) << "| " << pad_to("", COL) << "| " << annot << "\n";
        }
        if (!repair_desc.empty()) {
            out << "| " << repair_desc << "\n";
        }
    } else if (!repair_desc.empty()) {
        out << "+- TYPE REPAIR: " << repair_desc << "\n";
    }

    // Result
    out << "+- RESULT: " << (success ? "success" : "rejected") << " ";
    out << std::string(COL * 3 - 5, '-') << "\n";

    return out.str();
}

} // namespace etil::evolution
