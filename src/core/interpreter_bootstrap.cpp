// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/interpreter_bootstrap.hpp"

#include "etil/core/dictionary.hpp"
#include "etil/core/interpreter.hpp"
#include "etil/evolution/evolution_engine.hpp"
#include "etil/selection/selection_engine.hpp"

namespace etil::core {

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
