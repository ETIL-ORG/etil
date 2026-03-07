#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include "etil/core/compiled_body.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/execution_context.hpp"

#include "absl/container/flat_hash_map.h"

#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace etil::core {

/// Abstract base for polymorphic word listing across handler groups.
class HandlerSetBase {
public:
    virtual ~HandlerSetBase() = default;

    /// Return the set of words this handler group recognizes.
    virtual std::vector<std::string> words() const = 0;
};

/// Interpret-mode handlers (5 words).
///
/// Handles: words, .", s", .|, s|
/// The remaining 9 parsing words (forget, forget-all, include, meta!, meta@,
/// meta-del, meta-keys, impl-meta!, impl-meta@) are now self-hosted in
/// builtins.til using stack-based primitives.
class InterpretHandlerSet : public HandlerSetBase {
public:
    InterpretHandlerSet(Dictionary& dict, ExecutionContext& ctx,
                        std::ostream& out, std::ostream& err,
                        std::function<void()> print_all_words);

    /// Dispatch a token. Returns nullopt if not recognized, else the handler result.
    std::optional<bool> dispatch(const std::string& token,
                                 std::istringstream& iss);

    std::vector<std::string> words() const override;

private:
    using Handler = bool (InterpretHandlerSet::*)(const std::string&,
                                                   std::istringstream&);
    static const absl::flat_hash_map<std::string, Handler>& handler_map();

    bool handle_words(const std::string& token, std::istringstream& iss);
    bool handle_dot_quote(const std::string& token, std::istringstream& iss);
    bool handle_s_quote(const std::string& token, std::istringstream& iss);
    bool handle_dot_pipe(const std::string& token, std::istringstream& iss);
    bool handle_s_pipe(const std::string& token, std::istringstream& iss);
    bool handle_j_pipe(const std::string& token, std::istringstream& iss);

    Dictionary& dict_;
    ExecutionContext& ctx_;
    std::ostream& out_;
    std::ostream& err_;
    std::function<void()> print_all_words_;
};

/// Compile-mode handlers (8 words).
///
/// Handles: ;, does>, .", s", .|, s|, ['], recurse
class CompileHandlerSet : public HandlerSetBase {
public:
    CompileHandlerSet(std::ostream& err, std::string& compiling_word_name,
                      std::shared_ptr<ByteCode>& current_bytecode,
                      std::vector<size_t>& control_stack,
                      std::function<void()> finalize_definition,
                      std::function<void()> abandon_definition,
                      Dictionary& dict);

    /// Dispatch a token. Returns nullopt if not recognized, else the handler result.
    std::optional<bool> dispatch(const std::string& token,
                                 std::istringstream& iss);

    std::vector<std::string> words() const override;

private:
    using Handler = bool (CompileHandlerSet::*)(const std::string&,
                                                 std::istringstream&);
    static const absl::flat_hash_map<std::string, Handler>& handler_map();

    bool handle_semicolon(const std::string& token, std::istringstream& iss);
    bool handle_does(const std::string& token, std::istringstream& iss);
    bool handle_dot_quote(const std::string& token, std::istringstream& iss);
    bool handle_s_quote(const std::string& token, std::istringstream& iss);
    bool handle_dot_pipe(const std::string& token, std::istringstream& iss);
    bool handle_s_pipe(const std::string& token, std::istringstream& iss);
    bool handle_j_pipe(const std::string& token, std::istringstream& iss);
    bool handle_bracket_tick(const std::string& token, std::istringstream& iss);
    bool handle_recurse(const std::string& token, std::istringstream& iss);

    std::ostream& err_;
    std::string& compiling_word_name_;
    std::shared_ptr<ByteCode>& current_bytecode_;
    std::vector<size_t>& control_stack_;
    std::function<void()> finalize_definition_;
    std::function<void()> abandon_definition_;
    Dictionary& dict_;
};

/// Control-flow handlers (18 compile-only words).
///
/// Handles: if, else, then, do, loop, +loop, i, j, begin, until, while, repeat,
///          >r, r>, r@, leave, exit, again
class ControlFlowHandlerSet : public HandlerSetBase {
public:
    ControlFlowHandlerSet(std::ostream& err,
                          std::shared_ptr<ByteCode>& current_bytecode,
                          std::vector<size_t>& control_stack);

    /// Dispatch a token. Returns nullopt if not recognized, else the handler result.
    std::optional<bool> dispatch(const std::string& token);

    std::vector<std::string> words() const override;

private:
    using Handler = bool (ControlFlowHandlerSet::*)(const std::string&);
    static const absl::flat_hash_map<std::string, Handler>& handler_map();

    bool handle_if(const std::string& word);
    bool handle_else(const std::string& word);
    bool handle_then(const std::string& word);
    bool handle_do(const std::string& word);
    bool handle_loop(const std::string& word);
    bool handle_plus_loop(const std::string& word);
    bool handle_i(const std::string& word);
    bool handle_begin(const std::string& word);
    bool handle_until(const std::string& word);
    bool handle_while(const std::string& word);
    bool handle_repeat(const std::string& word);
    bool handle_to_r(const std::string& word);
    bool handle_from_r(const std::string& word);
    bool handle_fetch_r(const std::string& word);
    bool handle_j(const std::string& word);
    bool handle_leave(const std::string& word);
    bool handle_exit(const std::string& word);
    bool handle_again(const std::string& word);

    std::ostream& err_;
    std::shared_ptr<ByteCode>& current_bytecode_;
    std::vector<size_t>& control_stack_;
    std::vector<std::vector<size_t>> leave_fixups_;
};

} // namespace etil::core
