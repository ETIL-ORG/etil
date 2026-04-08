#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

// Unified interpreter bootstrap — the ONE entry point for creating a
// fully initialized ETIL interpreter. All platforms (REPL, MCP, WASM)
// must use bootstrap_interpreter() to ensure identical initialization.

#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace etil::core {
class Dictionary;
class Interpreter;
}

namespace etil::selection {
class SelectionEngine;
}

namespace etil::evolution {
class EvolutionEngine;
struct EvolutionConfig;
}

namespace etil::core {

/// Platform mode — controls which optional subsystems are initialized.
enum class BootstrapMode {
    Repl,   // Native REPL: core + evolution + selection
    Mcp,    // MCP server: core + evolution + selection (HTTP/MongoDB wired separately)
    Wasm,   // Browser WASM: core + evolution + selection (no async I/O)
};

/// Owns all auxiliary state for an interpreter session.
/// Keep alive for the lifetime of the interpreter — ExecutionContext
/// holds raw pointers into these owned objects.
struct InterpreterBundle {
    std::unique_ptr<Dictionary> dict;
    std::unique_ptr<Interpreter> interp;
    std::unique_ptr<etil::selection::SelectionEngine> selection;
    std::unique_ptr<etil::evolution::EvolutionEngine> evolution;

    InterpreterBundle();
    ~InterpreterBundle();

    // Non-copyable, non-movable — pointers are held by ExecutionContext
    InterpreterBundle(const InterpreterBundle&) = delete;
    InterpreterBundle& operator=(const InterpreterBundle&) = delete;
    InterpreterBundle(InterpreterBundle&&) = delete;
    InterpreterBundle& operator=(InterpreterBundle&&) = delete;
};

/// The ONE entry point for creating a fully initialized ETIL interpreter.
///
/// Performs in this exact order:
/// 1. Create Dictionary
/// 2. Register all primitives
/// 3. Create Interpreter with provided output/error streams
/// 4. Create and wire SelectionEngine + EvolutionEngine
/// 5. Register handler words (so help.til can attach metadata)
/// 6. Load startup files (builtins.til, help.til)
///
/// The mode parameter does NOT change the initialization sequence —
/// all modes get the same engines. It is reserved for future
/// platform-specific behavior (e.g., WASM http overrides).
///
/// startup_files: paths to .til files loaded at init (e.g., {"data/builtins.til", "data/help.til"}).
/// Pass empty vector to skip startup loading.
std::unique_ptr<InterpreterBundle> bootstrap_interpreter(
    BootstrapMode mode,
    std::ostream& out,
    std::ostream& err,
    const std::vector<std::string>& startup_files);

// --- Legacy compatibility ---
// TODO: Remove after all callers migrate to bootstrap_interpreter().

/// Owns just the auxiliary engines (selection + evolution).
/// Deprecated — use InterpreterBundle instead.
struct InterpreterEngines {
    std::unique_ptr<etil::selection::SelectionEngine> selection;
    std::unique_ptr<etil::evolution::EvolutionEngine> evolution;

    InterpreterEngines();
    ~InterpreterEngines();

    InterpreterEngines(const InterpreterEngines&) = delete;
    InterpreterEngines& operator=(const InterpreterEngines&) = delete;
    InterpreterEngines(InterpreterEngines&&) = delete;
    InterpreterEngines& operator=(InterpreterEngines&&) = delete;
};

/// Deprecated — use bootstrap_interpreter() instead.
/// Wires engines into an existing interpreter. Does NOT create
/// dictionary, register primitives, or load startup files.
std::unique_ptr<InterpreterEngines> bootstrap_engines(
    Dictionary& dict, Interpreter& interp);

} // namespace etil::core
