#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/genetic_ops.hpp"
#include "etil/evolution/ast_genetic_ops.hpp"
#include "etil/evolution/fitness.hpp"
#include "etil/evolution/evolve_logger.hpp"
#include "etil/selection/selection_engine.hpp"
#include "etil/core/dictionary.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace etil::evolution {

/// Weights for the 6 AST mutation operators.
struct MutationWeights {
    double substitute = 0.30;
    double perturb    = 0.15;
    double move       = 0.10;
    double control    = 0.10;
    double grow       = 0.20;
    double shrink     = 0.15;
};

struct EvolutionConfig {
    size_t population_limit = 10;
    size_t generation_size = 5;
    double mutation_rate = 0.8;   // prob mutation vs crossover
    double prune_threshold = 0.01;
    bool use_ast_ops = true;      // use AST-level operators (default)
    MutationConfig mutation_config;
    MutationWeights mutation_weights;
    size_t max_ast_nodes = 30;    // bloat control for grow mutation
    FitnessMode fitness_mode = FitnessMode::Binary;
    double distance_alpha = 1.0;  // scaling for distance fitness

    // Logging — controlled via TIL words (evolve-log-start, etc.)
    EvolveLogLevel log_level = EvolveLogLevel::Off;
    uint32_t log_categories = static_cast<uint32_t>(EvolveLogCategory::All);
    std::string log_directory;
};

/// Drives evolution of word implementations via genetic operators
/// and fitness evaluation.
class EvolutionEngine {
public:
    EvolutionEngine(EvolutionConfig config, etil::core::Dictionary& dict);

    /// Run one generation of evolution for a word concept.
    /// Returns the number of children created and evaluated.
    size_t evolve_word(const std::string& word);

    /// Evolve all words that have test cases registered.
    void evolve_all();

    /// Register test cases for a word.
    void register_tests(const std::string& word, std::vector<TestCase> tests);

    /// Register test cases with a restricted word pool for mutations.
    void register_tests_with_pool(const std::string& word,
                                   std::vector<TestCase> tests,
                                   std::vector<std::string> pool);

    /// Check if a word has registered test cases.
    bool has_tests(const std::string& word) const;

    /// Get the number of generations run for a word.
    size_t generations_run(const std::string& word) const;

    /// Get registered word names.
    std::vector<std::string> registered_words() const;

    const EvolutionConfig& config() const { return config_; }
    EvolutionConfig& config() { return config_; }
    Fitness& fitness() { return fitness_; }
    EvolveLogger& logger() { return logger_; }

private:
    EvolutionConfig config_;
    etil::core::Dictionary& dict_;
    GeneticOps genetic_ops_;           // bytecode-level (fallback + constant perturbation)
    ASTGeneticOps ast_genetic_ops_;    // AST-level (structural mutations)
    Fitness fitness_;
    EvolveLogger logger_;
    etil::selection::SelectionEngine parent_selector_;

    struct WordEvolution {
        std::vector<TestCase> tests;
        std::vector<std::string> word_pool;  // empty = full dictionary
        size_t generations = 0;
    };
    std::unordered_map<std::string, WordEvolution> word_state_;

    void update_weights(
        const std::string& word,
        const std::vector<std::pair<etil::core::WordImplPtr, FitnessResult>>& results);
    void prune(const std::string& word);
};

} // namespace etil::evolution
