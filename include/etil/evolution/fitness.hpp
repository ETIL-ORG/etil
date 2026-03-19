#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/word_impl.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"

#include <vector>

namespace etil::evolution {

/// A single test case: push inputs, run word, compare outputs.
struct TestCase {
    std::vector<etil::core::Value> inputs;
    std::vector<etil::core::Value> expected;
};

/// Result of evaluating an implementation against test cases.
struct FitnessResult {
    double correctness = 0.0;  // 0.0-1.0 (fraction of tests passed)
    double speed = 0.0;        // Mean execution time in nanoseconds
    double fitness = 0.0;      // Combined score
    size_t tests_passed = 0;
    size_t tests_total = 0;
};

/// Evaluates word implementations against test cases.
class Fitness {
public:
    /// Evaluate an implementation against test cases.
    /// Creates a temporary ExecutionContext for each test case.
    FitnessResult evaluate(
        etil::core::WordImpl& impl,
        const std::vector<TestCase>& tests,
        etil::core::Dictionary& dict,
        size_t instruction_budget = 100000);

    void set_speed_weight(double w) { speed_weight_ = w; }
    double speed_weight() const { return speed_weight_; }

private:
    double speed_weight_ = 0.1;

    bool run_single_test(
        etil::core::WordImpl& impl,
        const TestCase& tc,
        etil::core::Dictionary& dict,
        size_t instruction_budget,
        double& elapsed_ns);
};

} // namespace etil::evolution
