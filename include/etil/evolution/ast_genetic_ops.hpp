#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/ast.hpp"
#include "etil/evolution/decompiler.hpp"
#include "etil/evolution/ast_compiler.hpp"
#include "etil/evolution/stack_simulator.hpp"
#include "etil/evolution/signature_index.hpp"
#include "etil/evolution/type_repair.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/word_impl.hpp"

#include <random>

namespace etil::evolution {

class EvolveLogger;
struct EvolutionConfig;

/// AST-level genetic operators: structural mutations that produce
/// type-valid bytecode via the decompile → mutate → repair → compile pipeline.
class ASTGeneticOps {
public:
    explicit ASTGeneticOps(etil::core::Dictionary& dict);

    /// Set the logger for mutation/crossover diagnostics.
    void set_logger(EvolveLogger* logger) { logger_ = logger; }

    /// Set config for max_ast_nodes and mutation weights.
    void set_config(const EvolutionConfig* config) { config_ = config; }

    /// Set word pool for the current evolution run (empty = full dictionary).
    void set_word_pool(const std::vector<std::string>* pool) { word_pool_ = pool; }

    /// Mutate a WordImpl using AST-level operators.
    /// Returns a new WordImpl with mutated bytecode, or null if mutation failed.
    etil::core::WordImplPtr mutate(const etil::core::WordImpl& parent);

    /// Crossover two parents at the AST level.
    /// Returns a child with a subtree from parent_b spliced into parent_a's AST.
    etil::core::WordImplPtr crossover(
        const etil::core::WordImpl& parent_a,
        const etil::core::WordImpl& parent_b);

    /// Rebuild the signature index (call when dictionary changes).
    void rebuild_index();

private:
    etil::core::Dictionary& dict_;
    Decompiler decompiler_;
    ASTCompiler compiler_;
    StackSimulator simulator_;
    SignatureIndex index_;
    TypeRepair repair_;
    std::mt19937_64 rng_;
    EvolveLogger* logger_ = nullptr;
    const EvolutionConfig* config_ = nullptr;
    const std::vector<std::string>* word_pool_ = nullptr;

    // Mutation operators (each returns true if applied)
    bool substitute_call(ASTNode& ast);
    bool perturb_constant(ASTNode& ast);
    bool move_block(ASTNode& ast);
    bool mutate_control_flow(ASTNode& ast);
    bool grow_node(ASTNode& ast);
    bool shrink_node(ASTNode& ast);
    bool block_crossover(ASTNode& ast_a, const ASTNode& ast_b);
};

} // namespace etil::evolution
