// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/evolution_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <random>
#include <sstream>

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
    ast_genetic_ops_.set_bridge_map(&bridge_map_);
    // Apply TBBP configuration
    bridge_map_.set_tbbp_enabled(config_.tbbp_enabled);
    bridge_map_.set_alpha(config_.tbbp_alpha);
    bridge_map_.set_min_weight(config_.tbbp_min_weight);
    bridge_map_.set_logger(&logger_);
    // Fitness error stream is wired lazily in evolve_word() after logging starts
}

void EvolutionEngine::seed_rng(uint64_t seed) {
    rng_.seed(seed);
    genetic_ops_.set_rng_seed(seed);
    ast_genetic_ops_.set_rng_seed(seed);
    bridge_map_.set_rng_seed(seed);
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

    // Route fitness evaluation errors to the evolution log file
    fitness_.set_error_stream(logger_.stream());

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
        auto fr = fitness_.evaluate(*impl, tests, dict_, config_.instruction_budget, config_.fitness_mode, config_.distance_alpha);
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
    size_t children_created = 0;

    for (size_t i = 0; i < config_.generation_size; ++i) {
        WordImplPtr child;
        double parent_fitness = 0.0;
        bool is_mutation = false;

        if (coin(rng_) < config_.mutation_rate || evolvable.size() < 2) {
            // Mutation
            auto* parent = parent_selector_.select(evolvable);
            if (!parent) continue;
            is_mutation = true;

            // Look up parent's baseline fitness from the results vector
            for (const auto& [impl, fr] : results) {
                if (impl.get() == parent) { parent_fitness = fr.fitness; break; }
            }

            if (logger_.enabled(EvolveLogCategory::Engine)) {
                logger_.log(EvolveLogCategory::Engine,
                    "Child " + std::to_string(i) + ": mutating impl#"
                    + std::to_string(parent->id())
                    + " (gen " + std::to_string(parent->generation())
                    + ", weight " + std::to_string(parent->weight()) + ")");
            }

            // Begin TBBP tracking for this mutation
            bridge_map_.begin_mutation();

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
                // Mutation failed — punish any bridges used during the attempt
                bridge_map_.end_mutation(0.0);
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
        auto fr = fitness_.evaluate(*child, tests, dict_, config_.instruction_budget, config_.fitness_mode, config_.distance_alpha);

        // TBBP: apply weight update based on fitness delta
        if (is_mutation) {
            double reward = (fr.fitness > parent_fitness) ? 1.0 : 0.0;
            bridge_map_.end_mutation(reward);
        }

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

    // TBBP: log bridge weight summary at end of each generation
    bridge_map_.log_weight_summary(10);

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

size_t EvolutionEngine::evolve_sub_concept(
    const std::string& sub_concept, const std::string& chain_word) {

    // Get chain word's tests
    auto chain_it = word_state_.find(chain_word);
    if (chain_it == word_state_.end() || chain_it->second.tests.empty()) {
        return 0;
    }
    auto& tests = chain_it->second.tests;

    // Route fitness errors to the evolution log file
    fitness_.set_error_stream(logger_.stream());

    // Set selection mode for fitness evaluation
    if (config_.mce_weighted_select) {
        fitness_.set_selection_engine(&parent_selector_);
    } else {
        fitness_.set_selection_engine(nullptr);
    }

    // Get chain word's best bytecode impl for evaluation
    auto chain_impls = dict_.get_implementations(chain_word);
    if (!chain_impls || chain_impls->empty()) return 0;
    WordImplPtr chain_impl;
    double best_w = -1.0;
    for (auto& impl : *chain_impls) {
        if (impl->bytecode() && impl->weight() > best_w) {
            chain_impl = impl;
            best_w = impl->weight();
        }
    }
    if (!chain_impl) return 0;

    // Get sub-concept's evolvable impls
    auto sub_impls = dict_.get_implementations(sub_concept);
    if (!sub_impls || sub_impls->empty()) return 0;
    std::vector<WordImplPtr> evolvable;
    for (auto& impl : *sub_impls) {
        if (impl->bytecode()) evolvable.push_back(impl);
    }
    if (evolvable.empty()) return 0;

    // Rebuild signature index
    ast_genetic_ops_.rebuild_index();

    // Use sub-concept's word pool if registered, otherwise full dictionary
    auto sub_state_it = word_state_.find(sub_concept);
    if (sub_state_it != word_state_.end() && !sub_state_it->second.word_pool.empty()) {
        ast_genetic_ops_.set_word_pool(&sub_state_it->second.word_pool);
    } else {
        ast_genetic_ops_.set_word_pool(nullptr);
    }

    if (logger_.enabled(EvolveLogCategory::Engine)) {
        logger_.log(EvolveLogCategory::Engine,
            "MCE: evolving sub-concept '" + sub_concept
            + "' via chain '" + chain_word
            + "' (" + std::to_string(evolvable.size()) + " evolvable impls, "
            + std::to_string(tests.size()) + " test cases)");
    }

    // Evaluate baselines: run chain impl (which calls sub-concept through dictionary)
    // This gives implicit credit — the sub-concept impl that happens to be selected
    // contributes to the chain's fitness.
    std::vector<std::pair<WordImplPtr, FitnessResult>> results;
    for (auto& impl : evolvable) {
        auto fr = fitness_.evaluate(*chain_impl, tests, dict_,
                                     config_.instruction_budget,
                                     config_.fitness_mode, config_.distance_alpha);
        impl->set_weight(std::max(fr.fitness, config_.prune_threshold));
        results.push_back({impl, fr});
    }

    // Generate children by mutating sub-concept impls
    std::uniform_real_distribution<double> coin(0.0, 1.0);
    size_t children_created = 0;

    for (size_t i = 0; i < config_.generation_size; ++i) {
        auto* parent = parent_selector_.select(evolvable);
        if (!parent) continue;

        double parent_fitness = 0.0;
        for (const auto& [impl, fr] : results) {
            if (impl.get() == parent) { parent_fitness = fr.fitness; break; }
        }

        bridge_map_.begin_mutation();

        WordImplPtr child;
        if (coin(rng_) < config_.mutation_rate || evolvable.size() < 2) {
            if (config_.use_ast_ops) {
                child = ast_genetic_ops_.mutate(*parent);
            } else {
                child = genetic_ops_.clone(*parent, dict_);
                if (child && child->bytecode()) {
                    genetic_ops_.mutate(*child->bytecode());
                }
            }
        } else {
            auto* p2 = parent_selector_.select(evolvable);
            if (!p2) { bridge_map_.end_mutation(0.0); continue; }
            for (int attempt = 0; attempt < 5 && p2 == parent && evolvable.size() > 1; ++attempt) {
                p2 = parent_selector_.select(evolvable);
            }
            if (config_.use_ast_ops) {
                child = ast_genetic_ops_.crossover(*parent, *p2);
            } else {
                child = genetic_ops_.crossover(*parent, *p2, dict_);
            }
        }

        if (!child) {
            bridge_map_.end_mutation(0.0);
            continue;
        }

        // Register child in dictionary so chain evaluation can use it
        dict_.register_word(sub_concept, child);

        // Evaluate chain fitness with child available via implicit credit
        auto fr = fitness_.evaluate(*chain_impl, tests, dict_,
                                     config_.instruction_budget,
                                     config_.fitness_mode, config_.distance_alpha);

        double reward = (fr.fitness > parent_fitness) ? 1.0 : 0.0;
        bridge_map_.end_mutation(reward);

        child->set_weight(std::max(fr.fitness, config_.prune_threshold));
        results.push_back({child, fr});

        if (logger_.enabled(EvolveLogCategory::Fitness)) {
            logger_.log(EvolveLogCategory::Fitness,
                "MCE child of '" + sub_concept + "' impl#"
                + std::to_string(child->id())
                + ": " + std::to_string(fr.tests_passed) + "/"
                + std::to_string(fr.tests_total)
                + " pass, fitness=" + std::to_string(fr.fitness));
        }

        children_created++;
    }

    // Update weights and prune sub-concept impls
    update_weights(sub_concept, results);
    prune(sub_concept);

    if (logger_.enabled(EvolveLogCategory::Engine)) {
        logger_.log(EvolveLogCategory::Engine,
            "MCE: " + std::to_string(children_created)
            + " children of '" + sub_concept + "' created");
    }

    // Track generations for the sub-concept
    word_state_[sub_concept].generations++;
    return children_created;
}

// --- ConceptDAG integration ---

bool EvolutionEngine::register_dag(const std::string& root_concept,
                                    std::vector<TestCase> tests) {
    // Register tests on the root word (reuses existing test infrastructure)
    word_state_[root_concept].tests = std::move(tests);

    // Build the DAG from the call graph
    ConceptDAG dag;
    dag.build(root_concept, dict_);

    if (dag.evolvable_concepts().empty()) {
        return false;
    }

    // Reset or preserve contribution weights
    if (!accumulate_contributions_) {
        dag.reset();
    }

    dags_[root_concept] = std::move(dag);

    if (logger_.enabled(EvolveLogCategory::DAG)) {
        auto& d = dags_[root_concept];
        logger_.log(EvolveLogCategory::DAG,
            "DAG registered: '" + root_concept
            + "' (" + std::to_string(d.size()) + " nodes"
            + ", " + std::to_string(d.evolvable_concepts().size()) + " evolvable"
            + ", depth " + std::to_string(d.max_depth()) + ")");
    }
    return true;
}

size_t EvolutionEngine::evolve_dag_generation(const std::string& root_concept) {
    auto dag_it = dags_.find(root_concept);
    if (dag_it == dags_.end()) return 0;

    auto& dag = dag_it->second;

    // Select concept weighted by contribution
    std::string selected = dag.select_for_evolution(
        rng_, config_.dag_depth_discount);
    if (selected.empty()) return 0;

    if (logger_.enabled(EvolveLogCategory::DAG)) {
        auto* sel_node = dag.node(selected);
        logger_.log(EvolveLogCategory::DAG,
            "DAG select: '" + selected
            + "' (contrib=" + std::to_string(sel_node ? sel_node->contribution : 0.0)
            + ", depth=" + std::to_string(sel_node ? sel_node->depth : 0) + ")");
    }

    // Evolve the selected concept via chain-level fitness
    size_t children = evolve_sub_concept(selected, root_concept);

    // Update DAG node statistics
    auto* node = dag.node(selected);
    if (node) {
        node->stats.generations_evolved++;
        node->stats.children_created += children;

        // Update impl count from dictionary
        auto impls = dict_.get_implementations(selected);
        node->stats.impl_count = impls ? impls->size() : 0;
    }

    // Compute contribution weights from per-concept impl-weight variance.
    // Impl weights were set by evolve_sub_concept() using chain-level fitness,
    // so their variance directly measures how much this concept's mutation
    // space affects root fitness. Zero variance (e.g. offset constant with
    // converged impls) → low contribution. High variance → high contribution.
    for (auto& concept_name : dag.evolvable_concepts()) {
        auto impls = dict_.get_implementations(concept_name);
        auto* cn = dag.node(concept_name);
        if (!cn) continue;

        if (!impls || impls->size() < 2) {
            // Not enough samples for variance — use a small floor so the
            // concept still gets some evolution but doesn't dominate.
            cn->contribution = 1e-6;
            continue;
        }

        // Compute sample variance of impl weights, skipping NaN/inf
        double sum = 0.0;
        double best = -std::numeric_limits<double>::infinity();
        double worst = std::numeric_limits<double>::infinity();
        size_t n = 0;
        for (const auto& impl : *impls) {
            double w = impl->weight();
            if (std::isnan(w) || std::isinf(w)) continue;
            sum += w;
            if (w > best) best = w;
            if (w < worst) worst = w;
            n++;
        }
        if (n < 2) { cn->contribution = 1e-6; continue; }

        double mean = sum / static_cast<double>(n);
        double sum_sq_diff = 0.0;
        for (const auto& impl : *impls) {
            double w = impl->weight();
            if (std::isnan(w) || std::isinf(w)) continue;
            double d = w - mean;
            sum_sq_diff += d * d;
        }
        double var = sum_sq_diff / static_cast<double>(n - 1);
        if (var < 0.0 || std::isnan(var)) var = 0.0;

        // Update display stats from impl-weight distribution
        cn->stats.best_fitness = best;
        cn->stats.worst_fitness = worst;
        cn->stats.mean_fitness = mean;
        cn->stats.fitness_variance = sum_sq_diff;
        cn->stats.eval_count = n;

        cn->contribution = var + 1e-6;  // floor prevents zero
    }
    dag.normalize_contributions();

    return children;
}

void EvolutionEngine::evolve_dag(const std::string& root_concept,
                                  size_t generations) {
    for (size_t i = 0; i < generations; ++i) {
        evolve_dag_generation(root_concept);

        // Mid-evolution stats snapshot
        if (config_.dag_stats_interval > 0
            && (i + 1) % config_.dag_stats_interval == 0
            && logger_.enabled(EvolveLogCategory::DAG)) {
            auto* d = dag(root_concept);
            if (d) {
                std::ostringstream oss;
                oss << "DAG stats (gen " << (i + 1) << "/" << generations << "):\n";
                d->dump(oss);
                logger_.log(EvolveLogCategory::DAG, oss.str());
            }
        }
    }

    // End-of-evolution stats dump
    if (logger_.enabled(EvolveLogCategory::DAG)) {
        auto* d = dag(root_concept);
        if (d) {
            std::ostringstream oss;
            oss << "DAG final stats (" << generations << " generations):\n";
            d->dump(oss);
            logger_.log(EvolveLogCategory::DAG, oss.str());
        }
    }
}

ConceptDAG* EvolutionEngine::dag(const std::string& root_concept) {
    auto it = dags_.find(root_concept);
    return it != dags_.end() ? &it->second : nullptr;
}

const ConceptDAG* EvolutionEngine::dag(const std::string& root_concept) const {
    auto it = dags_.find(root_concept);
    return it != dags_.end() ? &it->second : nullptr;
}

} // namespace etil::evolution
