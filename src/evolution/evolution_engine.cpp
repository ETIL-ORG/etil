// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/evolution_engine.hpp"

#include <algorithm>
#include <chrono>
#include <random>

namespace etil::evolution {

using namespace etil::core;

EvolutionEngine::EvolutionEngine(EvolutionConfig config, Dictionary& dict)
    : config_(config)
    , dict_(dict)
    , genetic_ops_(config.mutation_config)
    , ast_genetic_ops_(dict)
    , parent_selector_(etil::selection::Strategy::WeightedRandom)
{
    ast_genetic_ops_.set_logger(&logger_);
    ast_genetic_ops_.set_config(&config_);
}

void EvolutionEngine::register_tests(
    const std::string& word, std::vector<TestCase> tests) {
    word_state_[word].tests = std::move(tests);
}

void EvolutionEngine::register_tests_with_pool(
    const std::string& word, std::vector<TestCase> tests,
    std::vector<std::string> pool) {
    auto& state = word_state_[word];
    state.tests = std::move(tests);
    state.word_pool = std::move(pool);
}

bool EvolutionEngine::has_tests(const std::string& word) const {
    auto it = word_state_.find(word);
    return it != word_state_.end() && !it->second.tests.empty();
}

size_t EvolutionEngine::generations_run(const std::string& word) const {
    auto it = word_state_.find(word);
    return it != word_state_.end() ? it->second.generations : 0;
}

std::vector<std::string> EvolutionEngine::registered_words() const {
    std::vector<std::string> words;
    for (const auto& [name, _] : word_state_) {
        if (!_.tests.empty()) words.push_back(name);
    }
    return words;
}

size_t EvolutionEngine::evolve_word(const std::string& word) {
    auto state_it = word_state_.find(word);
    if (state_it == word_state_.end() || state_it->second.tests.empty()) {
        return 0;
    }
    auto& state = state_it->second;
    auto& tests = state.tests;

    // Get current implementations
    auto impls_opt = dict_.get_implementations(word);
    if (!impls_opt || impls_opt->empty()) return 0;
    auto& impls = *impls_opt;

    // Only evolve bytecode implementations (not native primitives)
    std::vector<WordImplPtr> evolvable;
    for (auto& impl : impls) {
        if (impl->bytecode()) evolvable.push_back(impl);
    }
    if (evolvable.empty()) return 0;

    // Rebuild signature index if dictionary changed (tags added after construction)
    ast_genetic_ops_.rebuild_index();

    // Set per-word pool (empty = full dictionary)
    ast_genetic_ops_.set_word_pool(
        state.word_pool.empty() ? nullptr : &state.word_pool);

    if (logger_.enabled(EvolveLogCategory::Engine)) {
        logger_.log(EvolveLogCategory::Engine,
            "Gen " + std::to_string(state.generations) + ": evolving '" + word
            + "' (" + std::to_string(evolvable.size()) + " evolvable impls, "
            + std::to_string(tests.size()) + " test cases)");
    }

    // Evaluate existing implementations to set baseline weights
    std::vector<std::pair<WordImplPtr, FitnessResult>> results;
    for (auto& impl : evolvable) {
        auto fr = fitness_.evaluate(*impl, tests, dict_);
        impl->set_weight(std::max(fr.fitness, config_.prune_threshold));
        results.push_back({impl, fr});
        if (logger_.granular(EvolveLogCategory::Fitness)) {
            logger_.detail(EvolveLogCategory::Fitness,
                "Baseline impl#" + std::to_string(impl->id())
                + ": " + std::to_string(fr.tests_passed) + "/" + std::to_string(fr.tests_total)
                + " pass, fitness=" + std::to_string(fr.fitness));
        }
    }

    // Generate children
    std::uniform_real_distribution<double> coin(0.0, 1.0);
    std::mt19937_64 local_rng(
        std::chrono::steady_clock::now().time_since_epoch().count());
    size_t children_created = 0;

    for (size_t i = 0; i < config_.generation_size; ++i) {
        WordImplPtr child;

        if (coin(local_rng) < config_.mutation_rate || evolvable.size() < 2) {
            // Mutation
            auto* parent = parent_selector_.select(evolvable);
            if (!parent) continue;

            if (logger_.enabled(EvolveLogCategory::Engine)) {
                logger_.log(EvolveLogCategory::Engine,
                    "Child " + std::to_string(i) + ": mutating impl#"
                    + std::to_string(parent->id())
                    + " (gen " + std::to_string(parent->generation())
                    + ", weight " + std::to_string(parent->weight()) + ")");
            }

            if (config_.use_ast_ops) {
                child = ast_genetic_ops_.mutate(*parent);
            } else {
                child = genetic_ops_.clone(*parent, dict_);
                if (child && child->bytecode()) {
                    genetic_ops_.mutate(*child->bytecode());
                    child->add_mutation(
                        MutationHistory::MutationType::Inline, "bytecode-mutation");
                }
            }
            if (!child) {
                if (logger_.enabled(EvolveLogCategory::Engine)) {
                    logger_.log(EvolveLogCategory::Engine,
                        "Child " + std::to_string(i) + ": mutation failed");
                }
                continue;
            }
        } else {
            // Crossover
            auto* p1 = parent_selector_.select(evolvable);
            auto* p2 = parent_selector_.select(evolvable);
            if (!p1 || !p2) continue;
            for (int attempt = 0; attempt < 5 && p2 == p1 && evolvable.size() > 1; ++attempt) {
                p2 = parent_selector_.select(evolvable);
            }

            if (logger_.enabled(EvolveLogCategory::Crossover)) {
                logger_.log(EvolveLogCategory::Crossover,
                    "Child " + std::to_string(i) + ": crossover impl#"
                    + std::to_string(p1->id()) + " x impl#" + std::to_string(p2->id()));
            }

            if (config_.use_ast_ops) {
                child = ast_genetic_ops_.crossover(*p1, *p2);
            } else {
                child = genetic_ops_.crossover(*p1, *p2, dict_);
            }
            if (!child) {
                if (logger_.enabled(EvolveLogCategory::Crossover)) {
                    logger_.log(EvolveLogCategory::Crossover,
                        "Child " + std::to_string(i) + ": crossover failed");
                }
                continue;
            }
        }

        // Evaluate child
        auto fr = fitness_.evaluate(*child, tests, dict_);
        child->set_weight(std::max(fr.fitness, config_.prune_threshold));
        results.push_back({child, fr});

        if (logger_.enabled(EvolveLogCategory::Fitness)) {
            logger_.log(EvolveLogCategory::Fitness,
                "Child impl#" + std::to_string(child->id())
                + ": " + std::to_string(fr.tests_passed) + "/" + std::to_string(fr.tests_total)
                + " pass, fitness=" + std::to_string(fr.fitness));
        }

        // Register child in dictionary
        dict_.register_word(word, std::move(child));
        children_created++;
    }

    // Update weights for all implementations
    update_weights(word, results);

    // Prune if over population limit
    prune(word);

    if (logger_.enabled(EvolveLogCategory::Engine)) {
        logger_.log(EvolveLogCategory::Engine,
            "Gen " + std::to_string(state.generations) + ": "
            + std::to_string(children_created) + " children created");
    }

    state.generations++;
    return children_created;
}

void EvolutionEngine::evolve_all() {
    for (auto& [word, state] : word_state_) {
        if (!state.tests.empty()) {
            evolve_word(word);
        }
    }
}

void EvolutionEngine::update_weights(
    const std::string& /*word*/,
    const std::vector<std::pair<WordImplPtr, FitnessResult>>& results) {

    if (results.empty()) return;

    // Normalize fitness scores to sum to 1.0
    double total_fitness = 0.0;
    for (const auto& [impl, fr] : results) {
        total_fitness += fr.fitness;
    }
    if (total_fitness <= 0.0) return;

    for (const auto& [impl, fr] : results) {
        double normalized = fr.fitness / total_fitness;
        impl->set_weight(std::max(normalized, config_.prune_threshold));
    }
}

void EvolutionEngine::prune(const std::string& word) {
    auto impls_opt = dict_.get_implementations(word);
    if (!impls_opt) return;

    while (impls_opt->size() > config_.population_limit) {
        double min_weight = 1e300;
        size_t min_idx = 0;
        for (size_t i = 0; i < impls_opt->size(); ++i) {
            double w = (*impls_opt)[i]->weight();
            if (w < min_weight) {
                min_weight = w;
                min_idx = i;
            }
        }
        if (logger_.enabled(EvolveLogCategory::Selection)) {
            logger_.log(EvolveLogCategory::Selection,
                "Pruned impl#" + std::to_string((*impls_opt)[min_idx]->id())
                + " (weight " + std::to_string(min_weight)
                + ", below threshold " + std::to_string(config_.prune_threshold) + ")");
        }
        dict_.remove_implementation_at(word, min_idx);
        impls_opt = dict_.get_implementations(word);
        if (!impls_opt) break;
    }
}

} // namespace etil::evolution
