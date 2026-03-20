// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/handler_set.hpp"

namespace etil::core {

ControlFlowHandlerSet::ControlFlowHandlerSet(
    std::ostream& err,
    std::shared_ptr<ByteCode>& current_bytecode,
    std::vector<size_t>& control_stack)
    : err_(err), current_bytecode_(current_bytecode),
      control_stack_(control_stack) {}

const absl::flat_hash_map<std::string, ControlFlowHandlerSet::Handler>&
ControlFlowHandlerSet::handler_map() {
    static const absl::flat_hash_map<std::string, Handler> map = {
        {"if", &ControlFlowHandlerSet::handle_if},
        {"else", &ControlFlowHandlerSet::handle_else},
        {"then", &ControlFlowHandlerSet::handle_then},
        {"do", &ControlFlowHandlerSet::handle_do},
        {"loop", &ControlFlowHandlerSet::handle_loop},
        {"+loop", &ControlFlowHandlerSet::handle_plus_loop},
        {"i", &ControlFlowHandlerSet::handle_i},
        {"begin", &ControlFlowHandlerSet::handle_begin},
        {"until", &ControlFlowHandlerSet::handle_until},
        {"while", &ControlFlowHandlerSet::handle_while},
        {"repeat", &ControlFlowHandlerSet::handle_repeat},
        {">r", &ControlFlowHandlerSet::handle_to_r},
        {"r>", &ControlFlowHandlerSet::handle_from_r},
        {"r@", &ControlFlowHandlerSet::handle_fetch_r},
        {"j", &ControlFlowHandlerSet::handle_j},
        {"leave", &ControlFlowHandlerSet::handle_leave},
        {"exit", &ControlFlowHandlerSet::handle_exit},
        {"again", &ControlFlowHandlerSet::handle_again},
    };
    return map;
}

std::optional<bool> ControlFlowHandlerSet::dispatch(const std::string& token) {
    const auto& map = handler_map();
    auto it = map.find(token);
    if (it == map.end()) return std::nullopt;
    return (this->*(it->second))(token);
}

std::vector<std::string> ControlFlowHandlerSet::words() const {
    std::vector<std::string> result;
    for (const auto& [word, _] : handler_map()) {
        result.push_back(word);
    }
    return result;
}

// --- Marker helpers ---

void ControlFlowHandlerSet::emit_marker(Instruction::Op op, BlockKind kind) {
    Instruction m;
    m.op = op;
    m.int_val = static_cast<int64_t>(kind);
    current_bytecode_->append(std::move(m));
}

// --- Handler implementations ---

bool ControlFlowHandlerSet::handle_if(const std::string& /*word*/) {
    emit_marker(Instruction::Op::BlockBegin, BlockKind::IfThen);
    Instruction instr;
    instr.op = Instruction::Op::BranchIfFalse;
    instr.int_val = 0;  // placeholder
    control_stack_.push_back(current_bytecode_->size());
    current_bytecode_->append(std::move(instr));
    return true;
}

bool ControlFlowHandlerSet::handle_else(const std::string& /*word*/) {
    if (control_stack_.empty()) {
        err_ << "Error: else without matching if\n";
        return false;
    }
    // Change BlockBegin from IfThen to IfThenElse
    size_t if_pos = control_stack_.back();
    // BlockBegin is at if_pos - 1 (emitted just before BranchIfFalse)
    if (if_pos > 0) {
        current_bytecode_->backpatch(
            if_pos - 1, static_cast<int64_t>(BlockKind::IfThenElse));
    }
    emit_marker(Instruction::Op::BlockSeparator, BlockKind::IfThenElse);
    Instruction branch;
    branch.op = Instruction::Op::Branch;
    branch.int_val = 0;  // placeholder
    size_t else_pos = current_bytecode_->size();
    current_bytecode_->append(std::move(branch));
    control_stack_.pop_back();
    current_bytecode_->backpatch(
        if_pos, static_cast<int64_t>(current_bytecode_->size()));
    control_stack_.push_back(else_pos);
    return true;
}

bool ControlFlowHandlerSet::handle_then(const std::string& /*word*/) {
    if (control_stack_.empty()) {
        err_ << "Error: then without matching if\n";
        return false;
    }
    size_t pos = control_stack_.back();
    control_stack_.pop_back();
    current_bytecode_->backpatch(
        pos, static_cast<int64_t>(current_bytecode_->size()));
    // Determine if this was if/then or if/else/then from the BlockBegin marker
    auto& instrs = current_bytecode_->instructions();
    BlockKind kind = BlockKind::IfThen;
    // Search backward for the BlockBegin that opened this structure
    for (size_t i = pos; i > 0; --i) {
        if (instrs[i - 1].op == Instruction::Op::BlockBegin) {
            kind = static_cast<BlockKind>(instrs[i - 1].int_val);
            break;
        }
    }
    emit_marker(Instruction::Op::BlockEnd, kind);
    return true;
}

bool ControlFlowHandlerSet::handle_do(const std::string& /*word*/) {
    emit_marker(Instruction::Op::BlockBegin, BlockKind::DoLoop);
    Instruction setup;
    setup.op = Instruction::Op::DoSetup;
    current_bytecode_->append(std::move(setup));
    control_stack_.push_back(current_bytecode_->size());
    leave_fixups_.emplace_back();
    return true;
}

bool ControlFlowHandlerSet::handle_loop(const std::string& /*word*/) {
    if (control_stack_.empty()) {
        err_ << "Error: loop without matching do\n";
        return false;
    }
    size_t loop_start = control_stack_.back();
    control_stack_.pop_back();
    Instruction loop_instr;
    loop_instr.op = Instruction::Op::DoLoop;
    loop_instr.int_val = static_cast<int64_t>(loop_start);
    current_bytecode_->append(std::move(loop_instr));
    if (!leave_fixups_.empty()) {
        auto& fixups = leave_fixups_.back();
        for (size_t pos : fixups) {
            current_bytecode_->backpatch(
                pos, static_cast<int64_t>(current_bytecode_->size()));
        }
        leave_fixups_.pop_back();
    }
    emit_marker(Instruction::Op::BlockEnd, BlockKind::DoLoop);
    return true;
}

bool ControlFlowHandlerSet::handle_plus_loop(const std::string& /*word*/) {
    if (control_stack_.empty()) {
        err_ << "Error: +loop without matching do\n";
        return false;
    }
    // Change BlockBegin from DoLoop to DoPlusLoop
    // BlockBegin was emitted by handle_do, right before DoSetup
    size_t loop_start = control_stack_.back();
    if (loop_start >= 2) {
        auto& instrs = current_bytecode_->instructions();
        if (instrs[loop_start - 2].op == Instruction::Op::BlockBegin) {
            instrs[loop_start - 2].int_val = static_cast<int64_t>(BlockKind::DoPlusLoop);
        }
    }
    control_stack_.pop_back();
    Instruction ploop;
    ploop.op = Instruction::Op::DoPlusLoop;
    ploop.int_val = static_cast<int64_t>(loop_start);
    current_bytecode_->append(std::move(ploop));
    if (!leave_fixups_.empty()) {
        auto& fixups = leave_fixups_.back();
        for (size_t pos : fixups) {
            current_bytecode_->backpatch(
                pos, static_cast<int64_t>(current_bytecode_->size()));
        }
        leave_fixups_.pop_back();
    }
    emit_marker(Instruction::Op::BlockEnd, BlockKind::DoPlusLoop);
    return true;
}

bool ControlFlowHandlerSet::handle_i(const std::string& /*word*/) {
    Instruction instr;
    instr.op = Instruction::Op::DoI;
    current_bytecode_->append(std::move(instr));
    return true;
}

bool ControlFlowHandlerSet::handle_begin(const std::string& /*word*/) {
    emit_marker(Instruction::Op::BlockBegin, BlockKind::BeginUntil);  // default; changed by while/again
    control_stack_.push_back(current_bytecode_->size());
    return true;
}

bool ControlFlowHandlerSet::handle_until(const std::string& /*word*/) {
    if (control_stack_.empty()) {
        err_ << "Error: until without matching begin\n";
        return false;
    }
    size_t begin_pos = control_stack_.back();
    control_stack_.pop_back();
    Instruction branch;
    branch.op = Instruction::Op::BranchIfFalse;
    branch.int_val = static_cast<int64_t>(begin_pos);
    current_bytecode_->append(std::move(branch));
    emit_marker(Instruction::Op::BlockEnd, BlockKind::BeginUntil);
    return true;
}

bool ControlFlowHandlerSet::handle_while(const std::string& /*word*/) {
    // Change BlockBegin from BeginUntil to BeginWhileRepeat
    // BlockBegin was emitted by handle_begin, right before the begin_pos
    if (control_stack_.size() >= 1) {
        size_t begin_pos = control_stack_.back();
        if (begin_pos > 0) {
            auto& instrs = current_bytecode_->instructions();
            if (instrs[begin_pos - 1].op == Instruction::Op::BlockBegin) {
                instrs[begin_pos - 1].int_val = static_cast<int64_t>(BlockKind::BeginWhileRepeat);
            }
        }
    }
    Instruction branch;
    branch.op = Instruction::Op::BranchIfFalse;
    branch.int_val = 0;  // placeholder
    control_stack_.push_back(current_bytecode_->size());
    current_bytecode_->append(std::move(branch));
    return true;
}

bool ControlFlowHandlerSet::handle_repeat(const std::string& /*word*/) {
    if (control_stack_.size() < 2) {
        err_ << "Error: repeat without matching begin/while\n";
        return false;
    }
    size_t while_pos = control_stack_.back();
    control_stack_.pop_back();
    size_t begin_pos = control_stack_.back();
    control_stack_.pop_back();
    Instruction branch;
    branch.op = Instruction::Op::Branch;
    branch.int_val = static_cast<int64_t>(begin_pos);
    current_bytecode_->append(std::move(branch));
    current_bytecode_->backpatch(
        while_pos, static_cast<int64_t>(current_bytecode_->size()));
    emit_marker(Instruction::Op::BlockEnd, BlockKind::BeginWhileRepeat);
    return true;
}

bool ControlFlowHandlerSet::handle_to_r(const std::string& /*word*/) {
    Instruction instr;
    instr.op = Instruction::Op::ToR;
    current_bytecode_->append(std::move(instr));
    return true;
}

bool ControlFlowHandlerSet::handle_from_r(const std::string& /*word*/) {
    Instruction instr;
    instr.op = Instruction::Op::FromR;
    current_bytecode_->append(std::move(instr));
    return true;
}

bool ControlFlowHandlerSet::handle_fetch_r(const std::string& /*word*/) {
    Instruction instr;
    instr.op = Instruction::Op::FetchR;
    current_bytecode_->append(std::move(instr));
    return true;
}

bool ControlFlowHandlerSet::handle_j(const std::string& /*word*/) {
    Instruction instr;
    instr.op = Instruction::Op::DoJ;
    current_bytecode_->append(std::move(instr));
    return true;
}

bool ControlFlowHandlerSet::handle_leave(const std::string& /*word*/) {
    if (leave_fixups_.empty()) {
        err_ << "Error: leave without matching do\n";
        return false;
    }
    Instruction instr;
    instr.op = Instruction::Op::DoLeave;
    instr.int_val = 0;  // placeholder, backpatched by LOOP/+LOOP
    leave_fixups_.back().push_back(current_bytecode_->size());
    current_bytecode_->append(std::move(instr));
    return true;
}

bool ControlFlowHandlerSet::handle_exit(const std::string& /*word*/) {
    Instruction instr;
    instr.op = Instruction::Op::DoExit;
    current_bytecode_->append(std::move(instr));
    return true;
}

bool ControlFlowHandlerSet::handle_again(const std::string& /*word*/) {
    if (control_stack_.empty()) {
        err_ << "Error: again without matching begin\n";
        return false;
    }
    // Change BlockBegin from BeginUntil to BeginAgain
    size_t begin_pos = control_stack_.back();
    if (begin_pos > 0) {
        auto& instrs = current_bytecode_->instructions();
        if (instrs[begin_pos - 1].op == Instruction::Op::BlockBegin) {
            instrs[begin_pos - 1].int_val = static_cast<int64_t>(BlockKind::BeginAgain);
        }
    }
    control_stack_.pop_back();
    Instruction branch;
    branch.op = Instruction::Op::Branch;
    branch.int_val = static_cast<int64_t>(begin_pos);
    current_bytecode_->append(std::move(branch));
    emit_marker(Instruction::Op::BlockEnd, BlockKind::BeginAgain);
    return true;
}

} // namespace etil::core
