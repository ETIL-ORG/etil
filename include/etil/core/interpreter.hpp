#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include "etil/core/dictionary.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/compiled_body.hpp"
#include "etil/core/handler_set.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace etil::lvfs { class Lvfs; }

namespace etil::core {

/// Source context for tracking where code originates.
enum class SourceContext { Interactive, Include, Evaluate };

/// Outer interpreter for the ETIL language.
///
/// Handles all language semantics: tokenizing, number parsing, word dispatch,
/// colon definitions, compile mode, control flow compilation, CREATE/DOES>,
/// forget, and the ." parsing word.
///
/// The REPL (or any other I/O channel) feeds lines to interpret_line() and
/// reads back results via the output stream.
class Interpreter {
public:
    explicit Interpreter(Dictionary& dict, std::ostream& out = std::cout,
                         std::ostream& err = std::cerr);

    /// Process one line of input.
    void interpret_line(const std::string& line);

    /// True if currently inside a colon definition.
    bool compiling() const { return compiling_; }

    /// Access the execution context (stacks, dictionary pointer, etc.).
    ExecutionContext& context() { return ctx_; }
    const ExecutionContext& context() const { return ctx_; }

    /// Access the dictionary.
    Dictionary& dictionary() { return dict_; }

    /// Format a Value for display.
    static std::string format_value(const Value& v);

    /// Evaluate a string as code (safe entry point for the `evaluate` word).
    /// Saves/restores input stream, detects unterminated definitions.
    /// Returns true on success.
    bool evaluate_string(const std::string& code);

    /// Load and interpret a file line by line.
    /// Returns true if all lines were processed successfully.
    /// On error, writes diagnostics to the output stream and stops.
    bool load_file(const std::string& path);

    /// Register handler words as dictionary concepts (no implementations)
    /// so that metadata (help text, categories) can be attached via meta!.
    /// Must be called before load_startup_files().
    void register_handler_words();

    /// Load a list of startup files, expanding ~ to $HOME.
    /// Returns true if all files loaded successfully.
    bool load_startup_files(const std::vector<std::string>& paths);

    /// Release all heap objects on the data stack. Call before exit to avoid
    /// ASan leak reports from values still on the stack.
    void shutdown();

    /// Set the actual filesystem path for the session home directory.
    /// When set, `include` resolves relative paths under this directory.
    void set_home_dir(const std::string& dir);

    /// Set the actual filesystem path for the shared library directory.
    /// When set, `library` resolves relative paths under this directory.
    void set_library_dir(const std::string& dir);

    /// Set the LVFS instance for virtual filesystem operations.
    /// When set, resolve_* methods delegate to the LVFS, and the LVFS
    /// pointer is also set on the execution context for primitives.
    void set_lvfs(etil::lvfs::Lvfs* lvfs);

    /// Access configured paths (empty if not set).
    const std::string& home_dir() const { return home_dir_; }
    const std::string& library_dir() const { return library_dir_; }

    /// Resolve a relative path under the home directory.
    /// Returns empty string on path traversal violation or if home_dir not set.
    /// If home_dir not set, returns the relative path as-is (backward compat).
    std::string resolve_home_path(const std::string& relative_path) const;

    /// Resolve a relative path under the library directory.
    /// Returns empty string on path traversal violation or if library_dir not set.
    std::string resolve_library_path(const std::string& relative_path) const;

    /// Resolve a logical path: /home/... → resolve_home_path,
    /// /library/... → resolve_library_path, otherwise return as-is.
    std::string resolve_logical_path(const std::string& path) const;

    /// Return a sorted vector of all completable words (dictionary + interpreter
    /// parsing words + compile-only words). Used for tab-completion and /words.
    std::vector<std::string> completable_words() const;

    /// Print all available words (dictionary + interpreter parsing/control words).
    void print_all_words() const;

    /// Return a string like "(3) 10 20 30" showing stack depth and top elements.
    std::string stack_status(size_t max_show = 10) const;

private:
    // Dispatch entry points.
    bool interpret_token(const std::string& token, std::istringstream& iss);
    bool compile_token(const std::string& token, std::istringstream& iss);

    // Utilities.
    static bool try_compile_number(const std::string& word, Instruction& instr);
    void finalize_definition();
    void abandon_definition();
    bool execute_word(WordImpl* impl);
    bool try_push_number(const std::string& token);
    // Path resolution helper: join base + relative, verify stays under base.
    // Returns empty string on traversal violation.
    static std::string resolve_under(const std::string& base,
                                     const std::string& relative);

    // State members (declared before handler sets for init-order safety).
    Dictionary& dict_;
    ExecutionContext ctx_;
    std::ostream& out_;
    std::ostream& err_;
    std::string home_dir_;     // Actual filesystem path for session home
    std::string library_dir_;  // Actual filesystem path for library
    etil::lvfs::Lvfs* lvfs_ = nullptr;  // Non-owning, set by Session
    bool compiling_ = false;
    std::string compiling_word_name_;
    size_t compiling_start_line_ = 0;
    std::shared_ptr<ByteCode> current_bytecode_;
    std::vector<size_t> control_stack_;

    // Source context tracking for error diagnostics.
    std::string source_file_;                              // Current file (empty for interactive/MCP)
    size_t source_line_ = 0;                               // Current line (1-based, 0 = not tracking)
    SourceContext source_context_ = SourceContext::Interactive;

    // Handler sets (must be declared after all state members they reference).
    InterpretHandlerSet interpret_handlers_;
    CompileHandlerSet compile_handlers_;
    ControlFlowHandlerSet control_flow_handlers_;
};

} // namespace etil::core
