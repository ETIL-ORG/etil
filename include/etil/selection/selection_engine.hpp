#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/word_impl.hpp"

#include <random>
#include <vector>

namespace etil::selection {

enum class Strategy {
    Latest,          // Current behavior: always pick .back()
    WeightedRandom,  // Probability proportional to weight_
    EpsilonGreedy,   // Exploit best (1-e), explore random (e)
    UCB1             // Upper Confidence Bound (exploration bonus)
};

/// Selects one implementation from a set of candidates.
/// When strategy is Latest, behavior is identical to Dictionary::lookup().
class SelectionEngine {
public:
    explicit SelectionEngine(Strategy strategy = Strategy::Latest);

    /// Select one implementation from a concept's implementations.
    /// Returns nullptr if impls is empty.
    etil::core::WordImpl* select(
        const std::vector<etil::core::WordImplPtr>& impls);

    /// Configuration
    void set_strategy(Strategy s) { strategy_ = s; }
    Strategy strategy() const { return strategy_; }
    void set_epsilon(double eps) { epsilon_ = eps; }
    double epsilon() const { return epsilon_; }

private:
    Strategy strategy_;
    double epsilon_ = 0.1;
    std::mt19937_64 rng_;

    etil::core::WordImpl* select_latest(
        const std::vector<etil::core::WordImplPtr>& impls);
    etil::core::WordImpl* select_weighted(
        const std::vector<etil::core::WordImplPtr>& impls);
    etil::core::WordImpl* select_epsilon_greedy(
        const std::vector<etil::core::WordImplPtr>& impls);
    etil::core::WordImpl* select_ucb1(
        const std::vector<etil::core::WordImplPtr>& impls);
};

} // namespace etil::selection
