// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/selection/selection_engine.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <numeric>

namespace etil::selection {

SelectionEngine::SelectionEngine(Strategy strategy)
    : strategy_(strategy)
    , rng_(std::chrono::steady_clock::now().time_since_epoch().count())
{}

etil::core::WordImpl* SelectionEngine::select(
    const std::vector<etil::core::WordImplPtr>& impls) {
    if (impls.empty()) return nullptr;
    if (impls.size() == 1) return impls[0].get();

    switch (strategy_) {
        case Strategy::Latest:        return select_latest(impls);
        case Strategy::WeightedRandom: return select_weighted(impls);
        case Strategy::EpsilonGreedy:  return select_epsilon_greedy(impls);
        case Strategy::UCB1:          return select_ucb1(impls);
    }
    return select_latest(impls);
}

etil::core::WordImpl* SelectionEngine::select_latest(
    const std::vector<etil::core::WordImplPtr>& impls) {
    return impls.back().get();
}

etil::core::WordImpl* SelectionEngine::select_weighted(
    const std::vector<etil::core::WordImplPtr>& impls) {
    double total = 0.0;
    for (auto& impl : impls) total += impl->weight();
    if (total <= 0.0) return impls.back().get();

    std::uniform_real_distribution<double> dist(0.0, total);
    double r = dist(rng_);
    double cumulative = 0.0;
    for (auto& impl : impls) {
        cumulative += impl->weight();
        if (r < cumulative) return impl.get();
    }
    return impls.back().get();
}

etil::core::WordImpl* SelectionEngine::select_epsilon_greedy(
    const std::vector<etil::core::WordImplPtr>& impls) {
    std::uniform_real_distribution<double> coin(0.0, 1.0);
    if (coin(rng_) < epsilon_) {
        // Explore: uniform random
        std::uniform_int_distribution<size_t> idx(0, impls.size() - 1);
        return impls[idx(rng_)].get();
    }
    // Exploit: highest weight
    auto it = std::max_element(impls.begin(), impls.end(),
        [](const etil::core::WordImplPtr& a, const etil::core::WordImplPtr& b) {
            return a->weight() < b->weight();
        });
    return it->get();
}

etil::core::WordImpl* SelectionEngine::select_ucb1(
    const std::vector<etil::core::WordImplPtr>& impls) {
    // Total calls across all impls
    uint64_t total_calls = 0;
    for (auto& impl : impls) {
        total_calls += impl->profile().total_calls.load(std::memory_order_relaxed);
    }
    if (total_calls == 0) {
        // No data yet: pick first untried
        return impls.front().get();
    }

    double ln_total = std::log(static_cast<double>(total_calls));
    double best_score = -1e300;
    etil::core::WordImpl* best = impls.back().get();

    for (auto& impl : impls) {
        uint64_t calls = impl->profile().total_calls.load(std::memory_order_relaxed);
        if (calls == 0) {
            // Untried impl gets infinite score (must try it)
            return impl.get();
        }
        double exploit = impl->success_rate();
        double explore = std::sqrt(2.0 * ln_total / static_cast<double>(calls));
        double score = exploit + explore;
        if (score > best_score) {
            best_score = score;
            best = impl.get();
        }
    }
    return best;
}

} // namespace etil::selection
