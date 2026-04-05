#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

// Shared bootstrap for constructing the auxiliary engines that attach to
// every ETIL interpreter session (selection, evolution).  This ensures all
// entry points (REPL, MCP server, WASM) wire up identical state.

#include <memory>

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

/// Owns the auxiliary engines for an Interpreter.  Keep this alongside the
/// Interpreter it was built for — the Interpreter's ExecutionContext holds
/// raw pointers into these owned objects.
///
/// Every ETIL entry point should hold one of these per live Interpreter.
struct InterpreterEngines {
    std::unique_ptr<etil::selection::SelectionEngine> selection;
    std::unique_ptr<etil::evolution::EvolutionEngine> evolution;

    InterpreterEngines();
    ~InterpreterEngines();

    // Non-copyable, non-movable — pointers are held by ExecutionContext
    InterpreterEngines(const InterpreterEngines&) = delete;
    InterpreterEngines& operator=(const InterpreterEngines&) = delete;
    InterpreterEngines(InterpreterEngines&&) = delete;
    InterpreterEngines& operator=(InterpreterEngines&&) = delete;
};

/// Build a SelectionEngine and EvolutionEngine, wire them into the
/// interpreter's ExecutionContext, and return the owned bundle.  Caller
/// must keep the returned object alive for the lifetime of the interpreter.
///
/// Uses default EvolutionConfig.  Callers that need custom config should
/// mutate the returned engine via engines.evolution->config().
std::unique_ptr<InterpreterEngines> bootstrap_engines(
    etil::core::Dictionary& dict,
    etil::core::Interpreter& interp);

} // namespace etil::core
