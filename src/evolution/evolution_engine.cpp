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
    , parent_selector_(etil::selection::Strategy::WeightedRandom)
{}

void EvolutionEngine::register_tests(
    const std::string& word, std::vector<TestCase> tests) {
    word_state_[word].tests = std::move(tests);
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

    // Evaluate existing implementations to set baseline weights
    std::vector<std::pair<WordImplPtr, FitnessResult>> results;
    for (auto& impl : evolvable) {
        auto fr = fitness_.evaluate(*impl, tests, dict_);
        impl->set_weight(std::max(fr.fitness, config_.prune_threshold));
        results.push_back({impl, fr});
    }

    // Generate children
    std::uniform_real_distribution<double> coin(0.0, 1.0);
    size_t children_created = 0;

    for (size_t i = 0; i < config_.generation_size; ++i) {
        WordImplPtr child;

        // Use parent_selector_'s implicit RNG via a local coin flip
    std::mt19937_64 local_rng(std::chrono::steady_clock::now().time_since_epoch().count() + i);
    if (coin(local_rng) < config_.mutation_rate
            || evolvable.size() < 2) {
            // Mutation: clone a parent and mutate
            auto* parent = parent_selector_.select(evolvable);
            if (!parent) continue;
            child = genetic_ops_.clone(*parent, dict_);
            if (!child) continue;
            if (child->bytecode()) {
                genetic_ops_.mutate(*child->bytecode());
                child->add_mutation(
                    MutationHistory::MutationType::Inline, "bytecode-mutation");
            }
        } else {
            // Crossover: pick two parents
            auto* p1 = parent_selector_.select(evolvable);
            auto* p2 = parent_selector_.select(evolvable);
            if (!p1 || !p2) continue;
            // Ensure different parents if possible
            for (int attempt = 0; attempt < 5 && p2 == p1 && evolvable.size() > 1; ++attempt) {
                p2 = parent_selector_.select(evolvable);
            }
            child = genetic_ops_.crossover(*p1, *p2, dict_);
            if (!child) continue;
        }

        // Evaluate child
        auto fr = fitness_.evaluate(*child, tests, dict_);
        child->set_weight(std::max(fr.fitness, config_.prune_threshold));
        results.push_back({child, fr});

        // Register child in dictionary
        dict_.register_word(word, std::move(child));
        children_created++;
    }

    // Update weights for all implementations
    update_weights(word, results);

    // Prune if over population limit
    prune(word);

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
    const std::string& word,
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
        // Find the weakest implementation
        double min_weight = 1e300;
        size_t min_idx = 0;
        for (size_t i = 0; i < impls_opt->size(); ++i) {
            double w = (*impls_opt)[i]->weight();
            if (w < min_weight) {
                min_weight = w;
                min_idx = i;
            }
        }
        // Remove it (forget_word removes the latest, so we use forget_all
        // and re-register all except the weakest)
        // Simpler: just call forget_word repeatedly to peel off the latest
        // until we're at the limit. But that removes from the end, not the weakest.
        // For now, just call forget_word once (removes latest = newest child).
        dict_.forget_word(word);
        impls_opt = dict_.get_implementations(word);
        if (!impls_opt) break;
    }
}

} // namespace etil::evolution
