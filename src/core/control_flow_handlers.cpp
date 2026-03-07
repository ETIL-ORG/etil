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

// --- Handler implementations ---

bool ControlFlowHandlerSet::handle_if(const std::string& /*word*/) {
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
    Instruction branch;
    branch.op = Instruction::Op::Branch;
    branch.int_val = 0;  // placeholder
    size_t else_pos = current_bytecode_->size();
    current_bytecode_->append(std::move(branch));
    size_t if_pos = control_stack_.back();
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
    return true;
}

bool ControlFlowHandlerSet::handle_do(const std::string& /*word*/) {
    Instruction setup;
    setup.op = Instruction::Op::DoSetup;
    current_bytecode_->append(std::move(setup));
    control_stack_.push_back(current_bytecode_->size());
    leave_fixups_.emplace_back();  // new leave fixup list for this loop level
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
    // Backpatch all LEAVE branches to point past this LOOP
    if (!leave_fixups_.empty()) {
        auto& fixups = leave_fixups_.back();
        for (size_t pos : fixups) {
            current_bytecode_->backpatch(
                pos, static_cast<int64_t>(current_bytecode_->size()));
        }
        leave_fixups_.pop_back();
    }
    return true;
}

bool ControlFlowHandlerSet::handle_plus_loop(const std::string& /*word*/) {
    if (control_stack_.empty()) {
        err_ << "Error: +loop without matching do\n";
        return false;
    }
    size_t loop_start = control_stack_.back();
    control_stack_.pop_back();
    Instruction ploop;
    ploop.op = Instruction::Op::DoPlusLoop;
    ploop.int_val = static_cast<int64_t>(loop_start);
    current_bytecode_->append(std::move(ploop));
    // Backpatch all LEAVE branches to point past this +LOOP
    if (!leave_fixups_.empty()) {
        auto& fixups = leave_fixups_.back();
        for (size_t pos : fixups) {
            current_bytecode_->backpatch(
                pos, static_cast<int64_t>(current_bytecode_->size()));
        }
        leave_fixups_.pop_back();
    }
    return true;
}

bool ControlFlowHandlerSet::handle_i(const std::string& /*word*/) {
    Instruction instr;
    instr.op = Instruction::Op::DoI;
    current_bytecode_->append(std::move(instr));
    return true;
}

bool ControlFlowHandlerSet::handle_begin(const std::string& /*word*/) {
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
    return true;
}

bool ControlFlowHandlerSet::handle_while(const std::string& /*word*/) {
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
    size_t begin_pos = control_stack_.back();
    control_stack_.pop_back();
    Instruction branch;
    branch.op = Instruction::Op::Branch;
    branch.int_val = static_cast<int64_t>(begin_pos);
    current_bytecode_->append(std::move(branch));
    return true;
}

} // namespace etil::core
