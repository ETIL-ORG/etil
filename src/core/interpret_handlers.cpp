// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/handler_set.hpp"
#include "etil/core/escape_processing.hpp"
#include "etil/core/heap_json.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_string.hpp"

#include <nlohmann/json.hpp>

namespace etil::core {

InterpretHandlerSet::InterpretHandlerSet(
    Dictionary& dict, ExecutionContext& ctx,
    std::ostream& out, std::ostream& err,
    std::function<void()> print_all_words)
    : dict_(dict), ctx_(ctx), out_(out), err_(err),
      print_all_words_(std::move(print_all_words)) {}

const absl::flat_hash_map<std::string, InterpretHandlerSet::Handler>&
InterpretHandlerSet::handler_map() {
    static const absl::flat_hash_map<std::string, Handler> map = {
        {"words", &InterpretHandlerSet::handle_words},
        {".\"", &InterpretHandlerSet::handle_dot_quote},
        {"s\"", &InterpretHandlerSet::handle_s_quote},
        {".|", &InterpretHandlerSet::handle_dot_pipe},
        {"s|", &InterpretHandlerSet::handle_s_pipe},
        {"j|", &InterpretHandlerSet::handle_j_pipe},
    };
    return map;
}

std::optional<bool> InterpretHandlerSet::dispatch(const std::string& token,
                                                   std::istringstream& iss) {
    const auto& map = handler_map();
    auto it = map.find(token);
    if (it == map.end()) return std::nullopt;
    return (this->*(it->second))(token, iss);
}

std::vector<std::string> InterpretHandlerSet::words() const {
    std::vector<std::string> result;
    for (const auto& [word, _] : handler_map()) {
        result.push_back(word);
    }
    return result;
}

// --- Handler implementations ---

bool InterpretHandlerSet::handle_words(const std::string& /*token*/,
                                        std::istringstream& /*iss*/) {
    print_all_words_();
    return true;
}

bool InterpretHandlerSet::handle_dot_quote(const std::string& /*token*/,
                                            std::istringstream& iss) {
    std::string str;
    std::getline(iss, str, '"');
    if (!str.empty() && str[0] == ' ') {
        str = str.substr(1);
    }
    out_ << str;
    return true;
}

bool InterpretHandlerSet::handle_s_quote(const std::string& /*token*/,
                                          std::istringstream& iss) {
    std::string str;
    std::getline(iss, str, '"');
    if (!str.empty() && str[0] == ' ') {
        str = str.substr(1);
    }
    auto* hs = HeapString::create(str);
    ctx_.data_stack().push(Value::from(hs));
    return true;
}

bool InterpretHandlerSet::handle_dot_pipe(const std::string& /*token*/,
                                           std::istringstream& iss) {
    auto result = read_escaped_string(iss, '|', err_);
    if (!result) return false;
    out_ << *result;
    return true;
}

bool InterpretHandlerSet::handle_s_pipe(const std::string& /*token*/,
                                         std::istringstream& iss) {
    auto result = read_escaped_string(iss, '|', err_);
    if (!result) return false;
    auto* hs = HeapString::create(*result);
    ctx_.data_stack().push(Value::from(hs));
    return true;
}

bool InterpretHandlerSet::handle_j_pipe(const std::string& /*token*/,
                                         std::istringstream& iss) {
    auto result = read_escaped_string(iss, '|', err_);
    if (!result) return false;
    try {
        auto j = nlohmann::json::parse(*result);
        auto* hj = new HeapJson(std::move(j));
        ctx_.data_stack().push(Value::from(hj));
        return true;
    } catch (const nlohmann::json::parse_error& e) {
        err_ << "Error: j| invalid JSON: " << e.what() << "\n";
        return false;
    }
}

} // namespace etil::core
