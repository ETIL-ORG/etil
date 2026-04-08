// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/interpreter_bootstrap.hpp"

#include "etil/core/dictionary.hpp"
#include "etil/core/interpreter.hpp"
#include "etil/core/primitives.hpp"
#include "etil/evolution/evolution_engine.hpp"
#include "etil/selection/selection_engine.hpp"

namespace etil::core {

// --- InterpreterBundle ---

InterpreterBundle::InterpreterBundle() = default;
InterpreterBundle::~InterpreterBundle() = default;

// --- Unified bootstrap ---

std::unique_ptr<InterpreterBundle> bootstrap_interpreter(
    BootstrapMode /*mode*/,
    std::ostream& out,
    std::ostream& err,
    const std::vector<std::string>& startup_files) {

    auto bundle = std::make_unique<InterpreterBundle>();

    // 1. Create Dictionary
    bundle->dict = std::make_unique<Dictionary>();

    // 2. Register all primitives
    register_primitives(*bundle->dict);

    // 3. Create Interpreter
    bundle->interp = std::make_unique<Interpreter>(
        *bundle->dict, out, err);

    // 4. Create and wire SelectionEngine + EvolutionEngine
    bundle->selection = std::make_unique<etil::selection::SelectionEngine>();

    etil::evolution::EvolutionConfig evo_config;
    bundle->evolution = std::make_unique<etil::evolution::EvolutionEngine>(
        evo_config, *bundle->dict);

    bundle->interp->context().set_selection_engine(bundle->selection.get());
    bundle->interp->context().set_evolution_engine(bundle->evolution.get());

    // 5. Register handler words (so help.til can attach metadata)
    bundle->interp->register_handler_words();

    // 6. Load startup files
    if (!startup_files.empty()) {
        bundle->interp->load_startup_files(startup_files);
    }

    return bundle;
}

// --- Legacy compatibility ---

InterpreterEngines::InterpreterEngines() = default;
InterpreterEngines::~InterpreterEngines() = default;

std::unique_ptr<InterpreterEngines> bootstrap_engines(
    Dictionary& dict, Interpreter& interp) {
    auto bundle = std::make_unique<InterpreterEngines>();

    bundle->selection = std::make_unique<etil::selection::SelectionEngine>();

    etil::evolution::EvolutionConfig evo_config;
    bundle->evolution = std::make_unique<etil::evolution::EvolutionEngine>(
        evo_config, dict);

    interp.context().set_selection_engine(bundle->selection.get());
    interp.context().set_evolution_engine(bundle->evolution.get());

    return bundle;
}

} // namespace etil::core
