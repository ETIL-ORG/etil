// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/decompiler.hpp"

namespace etil::evolution {

using namespace etil::core;
using Op = Instruction::Op;

ASTNode Decompiler::decompile(const ByteCode& code) {
    return decompile_range(code.instructions(), 0, code.size());
}

ASTNode Decompiler::decompile_range(
    const std::vector<Instruction>& instrs,
    size_t start, size_t end) {

    std::vector<ASTNode> nodes;
    size_t ip = start;

    while (ip < end) {
        const auto& instr = instrs[ip];

        switch (instr.op) {

        case Op::BlockBegin: {
            auto kind = static_cast<BlockKind>(instr.int_val);
            size_t block_end = find_matching_block_end(instrs, ip);
            auto node = decompile_block(instrs, ip + 1, block_end, kind);
            node.source_ip_start = ip;
            node.source_ip_end = block_end;
            nodes.push_back(std::move(node));
            ip = block_end + 1;  // skip past BlockEnd
            break;
        }

        // BlockEnd/BlockSeparator at this level means malformed bytecode;
        // skip them gracefully
        case Op::BlockEnd:
        case Op::BlockSeparator:
            ++ip;
            break;

        case Op::PushInt:
            nodes.push_back(ASTNode::make_literal_int(instr.int_val));
            ++ip;
            break;

        case Op::PushFloat:
            nodes.push_back(ASTNode::make_literal_float(instr.float_val));
            ++ip;
            break;

        case Op::PushBool:
            nodes.push_back(ASTNode::make_literal_bool(instr.int_val != 0));
            ++ip;
            break;

        case Op::PushString:
            nodes.push_back(ASTNode::make_literal_string(instr.word_name));
            ++ip;
            break;

        case Op::PushJson:
            nodes.push_back(ASTNode::make_literal_json(instr.word_name));
            ++ip;
            break;

        case Op::PrintString:
            nodes.push_back(ASTNode::make_print_string(instr.word_name));
            ++ip;
            break;

        case Op::Call:
            nodes.push_back(ASTNode::make_word_call(instr.word_name));
            ++ip;
            break;

        case Op::PushXt:
            nodes.push_back(ASTNode::make_push_xt(instr.word_name));
            ++ip;
            break;

        case Op::DoI:    nodes.push_back(ASTNode::make_leaf(ASTNodeKind::DoI));    ++ip; break;
        case Op::DoJ:    nodes.push_back(ASTNode::make_leaf(ASTNodeKind::DoJ));    ++ip; break;
        case Op::ToR:    nodes.push_back(ASTNode::make_leaf(ASTNodeKind::ToR));    ++ip; break;
        case Op::FromR:  nodes.push_back(ASTNode::make_leaf(ASTNodeKind::FromR));  ++ip; break;
        case Op::FetchR: nodes.push_back(ASTNode::make_leaf(ASTNodeKind::FetchR)); ++ip; break;
        case Op::DoLeave:nodes.push_back(ASTNode::make_leaf(ASTNodeKind::Leave));  ++ip; break;
        case Op::DoExit: nodes.push_back(ASTNode::make_leaf(ASTNodeKind::Exit));   ++ip; break;
        case Op::PushDataPtr: nodes.push_back(ASTNode::make_leaf(ASTNodeKind::PushDataPtr)); ++ip; break;

        case Op::SetDoes: {
            auto node = ASTNode::make_leaf(ASTNodeKind::SetDoes);
            node.int_val = instr.int_val;
            nodes.push_back(std::move(node));
            ++ip;
            break;
        }

        // Branch/loop instructions are structural — their semantics are
        // captured by the enclosing BlockBegin/BlockEnd markers.
        // At top level (outside a block), skip them.
        case Op::Branch:
        case Op::BranchIfFalse:
        case Op::DoSetup:
        case Op::DoLoop:
        case Op::DoPlusLoop:
            ++ip;
            break;
        }
    }

    return ASTNode::make_sequence(std::move(nodes));
}

ASTNode Decompiler::decompile_block(
    const std::vector<Instruction>& instrs,
    size_t start, size_t end,
    BlockKind kind) {

    ASTNode node;

    switch (kind) {

    case BlockKind::IfThen: {
        // BranchIfFalse ... then-body ... BlockEnd
        // start points to BranchIfFalse; body starts at start+1
        auto body = decompile_range(instrs, start + 1, end);
        node.kind = ASTNodeKind::IfThen;
        node.children.push_back(std::move(body));
        break;
    }

    case BlockKind::IfThenElse: {
        // BranchIfFalse ... then-body ... BlockSeparator Branch ... else-body ... BlockEnd
        size_t sep = find_separator(instrs, start, end, kind);
        auto then_body = decompile_range(instrs, start + 1, sep);
        // Skip BlockSeparator + Branch (2 instructions after sep)
        size_t else_start = sep + 2;
        auto else_body = decompile_range(instrs, else_start, end);
        node.kind = ASTNodeKind::IfThenElse;
        node.children.push_back(std::move(then_body));
        node.children.push_back(std::move(else_body));
        break;
    }

    case BlockKind::DoLoop: {
        // DoSetup ... body ... DoLoop BlockEnd
        // start points to DoSetup; body is start+1 to end-1 (skip DoLoop)
        auto body = decompile_range(instrs, start + 1, end - 1);
        node.kind = ASTNodeKind::DoLoop;
        node.children.push_back(std::move(body));
        break;
    }

    case BlockKind::DoPlusLoop: {
        // DoSetup ... body ... DoPlusLoop BlockEnd
        auto body = decompile_range(instrs, start + 1, end - 1);
        node.kind = ASTNodeKind::DoPlusLoop;
        node.children.push_back(std::move(body));
        break;
    }

    case BlockKind::BeginUntil: {
        // body ... BranchIfFalse BlockEnd
        // body includes the condition (everything before BranchIfFalse)
        auto body = decompile_range(instrs, start, end - 1);
        node.kind = ASTNodeKind::BeginUntil;
        node.children.push_back(std::move(body));
        break;
    }

    case BlockKind::BeginWhileRepeat: {
        // condition ... BranchIfFalse ... body ... Branch BlockEnd
        // Find BranchIfFalse (the "while" instruction)
        size_t while_ip = start;
        for (size_t i = start; i < end; ++i) {
            if (instrs[i].op == Op::BranchIfFalse) {
                while_ip = i;
                break;
            }
        }
        auto condition = decompile_range(instrs, start, while_ip);
        // Body is after BranchIfFalse to before final Branch
        auto body = decompile_range(instrs, while_ip + 1, end - 1);
        node.kind = ASTNodeKind::BeginWhileRepeat;
        node.children.push_back(std::move(condition));
        node.children.push_back(std::move(body));
        break;
    }

    case BlockKind::BeginAgain: {
        // body ... Branch BlockEnd
        auto body = decompile_range(instrs, start, end - 1);
        node.kind = ASTNodeKind::BeginAgain;
        node.children.push_back(std::move(body));
        break;
    }

    }

    return node;
}

size_t Decompiler::find_matching_block_end(
    const std::vector<Instruction>& instrs,
    size_t begin_ip) {
    int depth = 1;
    for (size_t ip = begin_ip + 1; ip < instrs.size(); ++ip) {
        if (instrs[ip].op == Op::BlockBegin) depth++;
        if (instrs[ip].op == Op::BlockEnd) {
            depth--;
            if (depth == 0) return ip;
        }
    }
    return instrs.size();  // malformed: unclosed block
}

size_t Decompiler::find_separator(
    const std::vector<Instruction>& instrs,
    size_t start, size_t end,
    BlockKind kind) {
    int depth = 0;
    for (size_t ip = start; ip < end; ++ip) {
        if (instrs[ip].op == Op::BlockBegin) depth++;
        if (instrs[ip].op == Op::BlockEnd) depth--;
        if (depth == 0 && instrs[ip].op == Op::BlockSeparator &&
            static_cast<BlockKind>(instrs[ip].int_val) == kind) {
            return ip;
        }
    }
    return end;  // not found
}

} // namespace etil::evolution
