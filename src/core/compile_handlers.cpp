// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/handler_set.hpp"
#include "etil/core/escape_processing.hpp"

#include <nlohmann/json.hpp>

namespace etil::core {

CompileHandlerSet::CompileHandlerSet(
    std::ostream& err, std::string& compiling_word_name,
    std::shared_ptr<ByteCode>& current_bytecode,
    std::vector<size_t>& control_stack,
    std::function<void()> finalize_definition,
    std::function<void()> abandon_definition,
    Dictionary& dict)
    : err_(err), compiling_word_name_(compiling_word_name),
      current_bytecode_(current_bytecode), control_stack_(control_stack),
      finalize_definition_(std::move(finalize_definition)),
      abandon_definition_(std::move(abandon_definition)),
      dict_(dict) {}

const absl::flat_hash_map<std::string, CompileHandlerSet::Handler>&
CompileHandlerSet::handler_map() {
    static const absl::flat_hash_map<std::string, Handler> map = {
        {";", &CompileHandlerSet::handle_semicolon},
        {"does>", &CompileHandlerSet::handle_does},
        {".\"", &CompileHandlerSet::handle_dot_quote},
        {"s\"", &CompileHandlerSet::handle_s_quote},
        {".|", &CompileHandlerSet::handle_dot_pipe},
        {"s|", &CompileHandlerSet::handle_s_pipe},
        {"j|", &CompileHandlerSet::handle_j_pipe},
        {"[']", &CompileHandlerSet::handle_bracket_tick},
        {"recurse", &CompileHandlerSet::handle_recurse},
    };
    return map;
}

std::optional<bool> CompileHandlerSet::dispatch(const std::string& token,
                                                 std::istringstream& iss) {
    const auto& map = handler_map();
    auto it = map.find(token);
    if (it == map.end()) return std::nullopt;
    return (this->*(it->second))(token, iss);
}

std::vector<std::string> CompileHandlerSet::words() const {
    std::vector<std::string> result;
    for (const auto& [word, _] : handler_map()) {
        result.push_back(word);
    }
    return result;
}

// --- Handler implementations ---

bool CompileHandlerSet::handle_semicolon(const std::string& /*token*/,
                                          std::istringstream& /*iss*/) {
    if (!control_stack_.empty()) {
        err_ << "Error: unresolved control structure in '"
             << compiling_word_name_ << "'\n";
        abandon_definition_();
        return false;
    }
    finalize_definition_();
    return true;
}

bool CompileHandlerSet::handle_does(const std::string& /*token*/,
                                     std::istringstream& /*iss*/) {
    Instruction set_does;
    set_does.op = Instruction::Op::SetDoes;
    set_does.int_val =
        static_cast<int64_t>(current_bytecode_->size() + 1);
    current_bytecode_->append(std::move(set_does));
    return true;
}

bool CompileHandlerSet::handle_dot_quote(const std::string& /*token*/,
                                          std::istringstream& iss) {
    std::string str;
    std::getline(iss, str, '"');
    if (!str.empty() && str[0] == ' ') {
        str = str.substr(1);
    }
    Instruction instr;
    instr.op = Instruction::Op::PrintString;
    instr.word_name = str;
    current_bytecode_->append(std::move(instr));
    return true;
}

bool CompileHandlerSet::handle_s_quote(const std::string& /*token*/,
                                        std::istringstream& iss) {
    std::string str;
    std::getline(iss, str, '"');
    if (!str.empty() && str[0] == ' ') {
        str = str.substr(1);
    }
    Instruction instr;
    instr.op = Instruction::Op::PushString;
    instr.word_name = str;
    current_bytecode_->append(std::move(instr));
    return true;
}

bool CompileHandlerSet::handle_dot_pipe(const std::string& /*token*/,
                                         std::istringstream& iss) {
    auto result = read_escaped_string(iss, '|', err_);
    if (!result) {
        abandon_definition_();
        return false;
    }
    Instruction instr;
    instr.op = Instruction::Op::PrintString;
    instr.word_name = *result;
    current_bytecode_->append(std::move(instr));
    return true;
}

bool CompileHandlerSet::handle_s_pipe(const std::string& /*token*/,
                                       std::istringstream& iss) {
    auto result = read_escaped_string(iss, '|', err_);
    if (!result) {
        abandon_definition_();
        return false;
    }
    Instruction instr;
    instr.op = Instruction::Op::PushString;
    instr.word_name = *result;
    current_bytecode_->append(std::move(instr));
    return true;
}

bool CompileHandlerSet::handle_j_pipe(const std::string& /*token*/,
                                       std::istringstream& iss) {
    auto result = read_escaped_string(iss, '|', err_);
    if (!result) {
        abandon_definition_();
        return false;
    }
    // Validate JSON at compile time
    try {
        auto _unused = nlohmann::json::parse(*result);
        (void)_unused;
    } catch (const nlohmann::json::parse_error& e) {
        err_ << "Error: j| invalid JSON: " << e.what() << "\n";
        abandon_definition_();
        return false;
    }
    Instruction instr;
    instr.op = Instruction::Op::PushJson;
    instr.word_name = *result;
    current_bytecode_->append(std::move(instr));
    return true;
}

bool CompileHandlerSet::handle_bracket_tick(const std::string& /*token*/,
                                             std::istringstream& iss) {
    std::string word_name;
    if (!(iss >> word_name)) {
        err_ << "Error: ['] expects a word name\n";
        abandon_definition_();
        return false;
    }
    // Verify the word exists at compile time
    if (!dict_.lookup(word_name)) {
        err_ << "Error: ['] unknown word '" << word_name << "'\n";
        abandon_definition_();
        return false;
    }
    Instruction instr;
    instr.op = Instruction::Op::PushXt;
    instr.word_name = word_name;
    current_bytecode_->append(std::move(instr));
    return true;
}

bool CompileHandlerSet::handle_recurse(const std::string& /*token*/,
                                        std::istringstream& /*iss*/) {
    Instruction instr;
    instr.op = Instruction::Op::Call;
    instr.word_name = compiling_word_name_;
    current_bytecode_->append(std::move(instr));
    return true;
}

} // namespace etil::core
