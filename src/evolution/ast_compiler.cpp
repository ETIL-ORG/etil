// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/ast_compiler.hpp"

namespace etil::evolution {

using namespace etil::core;
using Op = Instruction::Op;

std::shared_ptr<ByteCode> ASTCompiler::compile(const ASTNode& ast) {
    auto bc = std::make_shared<ByteCode>();
    emit(*bc, ast);
    return bc;
}

void ASTCompiler::emit_marker(ByteCode& bc, Op op, BlockKind kind) {
    Instruction m;
    m.op = op;
    m.int_val = static_cast<int64_t>(kind);
    bc.append(std::move(m));
}

size_t ASTCompiler::emit_placeholder(ByteCode& bc, Op op) {
    Instruction instr;
    instr.op = op;
    instr.int_val = 0;
    size_t pos = bc.size();
    bc.append(std::move(instr));
    return pos;
}

void ASTCompiler::emit(ByteCode& bc, const ASTNode& node) {
    switch (node.kind) {

    case ASTNodeKind::Sequence:
        for (const auto& child : node.children) {
            emit(bc, child);
        }
        break;

    case ASTNodeKind::Literal: {
        Instruction instr;
        instr.op = node.literal_op;
        switch (node.literal_op) {
            case Op::PushInt:
                instr.int_val = node.int_val;
                break;
            case Op::PushFloat:
                instr.float_val = node.float_val;
                break;
            case Op::PushBool:
                instr.int_val = node.int_val;
                break;
            case Op::PushString:
            case Op::PushJson:
                instr.word_name = node.string_val;
                break;
            default:
                break;
        }
        bc.append(std::move(instr));
        break;
    }

    case ASTNodeKind::WordCall: {
        Instruction instr;
        instr.op = Op::Call;
        instr.word_name = node.word_name;
        bc.append(std::move(instr));
        break;
    }

    case ASTNodeKind::PrintString: {
        Instruction instr;
        instr.op = Op::PrintString;
        instr.word_name = node.string_val;
        bc.append(std::move(instr));
        break;
    }

    case ASTNodeKind::PushXt: {
        Instruction instr;
        instr.op = Op::PushXt;
        instr.word_name = node.word_name;
        bc.append(std::move(instr));
        break;
    }

    case ASTNodeKind::ToR:       { Instruction i; i.op = Op::ToR;       bc.append(std::move(i)); break; }
    case ASTNodeKind::FromR:     { Instruction i; i.op = Op::FromR;     bc.append(std::move(i)); break; }
    case ASTNodeKind::FetchR:    { Instruction i; i.op = Op::FetchR;    bc.append(std::move(i)); break; }
    case ASTNodeKind::DoI:       { Instruction i; i.op = Op::DoI;       bc.append(std::move(i)); break; }
    case ASTNodeKind::DoJ:       { Instruction i; i.op = Op::DoJ;       bc.append(std::move(i)); break; }
    case ASTNodeKind::Leave:     { Instruction i; i.op = Op::DoLeave; i.int_val = 0; bc.append(std::move(i)); break; }
    case ASTNodeKind::Exit:      { Instruction i; i.op = Op::DoExit;    bc.append(std::move(i)); break; }
    case ASTNodeKind::PushDataPtr: { Instruction i; i.op = Op::PushDataPtr; bc.append(std::move(i)); break; }

    case ASTNodeKind::SetDoes: {
        Instruction i;
        i.op = Op::SetDoes;
        i.int_val = node.int_val;
        bc.append(std::move(i));
        break;
    }

    // --- if ... then ---
    case ASTNodeKind::IfThen: {
        emit_marker(bc, Op::BlockBegin, BlockKind::IfThen);
        size_t branch_pos = emit_placeholder(bc, Op::BranchIfFalse);
        if (!node.children.empty()) emit(bc, node.children[0]);  // then-body
        bc.backpatch(branch_pos, static_cast<int64_t>(bc.size()));
        emit_marker(bc, Op::BlockEnd, BlockKind::IfThen);
        break;
    }

    // --- if ... else ... then ---
    case ASTNodeKind::IfThenElse: {
        emit_marker(bc, Op::BlockBegin, BlockKind::IfThenElse);
        size_t if_branch = emit_placeholder(bc, Op::BranchIfFalse);
        if (node.children.size() > 0) emit(bc, node.children[0]);  // then-body
        emit_marker(bc, Op::BlockSeparator, BlockKind::IfThenElse);
        size_t else_branch = emit_placeholder(bc, Op::Branch);
        bc.backpatch(if_branch, static_cast<int64_t>(bc.size()));
        if (node.children.size() > 1) emit(bc, node.children[1]);  // else-body
        bc.backpatch(else_branch, static_cast<int64_t>(bc.size()));
        emit_marker(bc, Op::BlockEnd, BlockKind::IfThenElse);
        break;
    }

    // --- do ... loop ---
    case ASTNodeKind::DoLoop: {
        emit_marker(bc, Op::BlockBegin, BlockKind::DoLoop);
        { Instruction setup; setup.op = Op::DoSetup; bc.append(std::move(setup)); }
        size_t loop_start = bc.size();
        if (!node.children.empty()) emit(bc, node.children[0]);  // body
        {
            Instruction loop;
            loop.op = Op::DoLoop;
            loop.int_val = static_cast<int64_t>(loop_start);
            bc.append(std::move(loop));
        }
        // Backpatch any Leave instructions in the body to point here
        backpatch_leaves(bc, loop_start, bc.size());
        emit_marker(bc, Op::BlockEnd, BlockKind::DoLoop);
        break;
    }

    // --- do ... +loop ---
    case ASTNodeKind::DoPlusLoop: {
        emit_marker(bc, Op::BlockBegin, BlockKind::DoPlusLoop);
        { Instruction setup; setup.op = Op::DoSetup; bc.append(std::move(setup)); }
        size_t loop_start = bc.size();
        if (!node.children.empty()) emit(bc, node.children[0]);  // body
        {
            Instruction ploop;
            ploop.op = Op::DoPlusLoop;
            ploop.int_val = static_cast<int64_t>(loop_start);
            bc.append(std::move(ploop));
        }
        backpatch_leaves(bc, loop_start, bc.size());
        emit_marker(bc, Op::BlockEnd, BlockKind::DoPlusLoop);
        break;
    }

    // --- begin ... until ---
    case ASTNodeKind::BeginUntil: {
        emit_marker(bc, Op::BlockBegin, BlockKind::BeginUntil);
        size_t loop_start = bc.size();
        if (!node.children.empty()) emit(bc, node.children[0]);  // body (includes condition)
        {
            Instruction branch;
            branch.op = Op::BranchIfFalse;
            branch.int_val = static_cast<int64_t>(loop_start);
            bc.append(std::move(branch));
        }
        emit_marker(bc, Op::BlockEnd, BlockKind::BeginUntil);
        break;
    }

    // --- begin ... while ... repeat ---
    case ASTNodeKind::BeginWhileRepeat: {
        emit_marker(bc, Op::BlockBegin, BlockKind::BeginWhileRepeat);
        size_t loop_start = bc.size();
        if (node.children.size() > 0) emit(bc, node.children[0]);  // condition
        size_t while_branch = emit_placeholder(bc, Op::BranchIfFalse);
        if (node.children.size() > 1) emit(bc, node.children[1]);  // body
        {
            Instruction branch;
            branch.op = Op::Branch;
            branch.int_val = static_cast<int64_t>(loop_start);
            bc.append(std::move(branch));
        }
        bc.backpatch(while_branch, static_cast<int64_t>(bc.size()));
        emit_marker(bc, Op::BlockEnd, BlockKind::BeginWhileRepeat);
        break;
    }

    // --- begin ... again ---
    case ASTNodeKind::BeginAgain: {
        emit_marker(bc, Op::BlockBegin, BlockKind::BeginAgain);
        size_t loop_start = bc.size();
        if (!node.children.empty()) emit(bc, node.children[0]);  // body
        {
            Instruction branch;
            branch.op = Op::Branch;
            branch.int_val = static_cast<int64_t>(loop_start);
            bc.append(std::move(branch));
        }
        emit_marker(bc, Op::BlockEnd, BlockKind::BeginAgain);
        break;
    }

    }
}

void ASTCompiler::backpatch_leaves(ByteCode& bc, size_t body_start, size_t target) {
    auto& instrs = bc.instructions();
    for (size_t i = body_start; i < instrs.size(); ++i) {
        if (instrs[i].op == Op::DoLeave && instrs[i].int_val == 0) {
            instrs[i].int_val = static_cast<int64_t>(target);
        }
    }
}

} // namespace etil::evolution
