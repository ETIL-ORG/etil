#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/word_impl.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"

#include <vector>

namespace etil::evolution {

/// Fitness evaluation mode.
enum class FitnessMode {
    Binary,    // Exact match: 0 or 1 per test case (original behavior)
    Distance,  // Distance-based: 1/(1+α|actual-expected|) per test case
};

/// A single test case: push inputs, run word, compare outputs.
struct TestCase {
    std::vector<etil::core::Value> inputs;
    std::vector<etil::core::Value> expected;
};

/// Result of evaluating an implementation against test cases.
struct FitnessResult {
    double correctness = 0.0;  // 0.0-1.0 (binary: fraction passed; distance: mean score)
    double speed = 0.0;        // Mean execution time in nanoseconds
    double fitness = 0.0;      // Combined score
    size_t tests_passed = 0;   // Exact matches (counted in both modes)
    size_t tests_total = 0;
    double mean_distance = 0.0; // Average distance across all tests (distance mode only)
};

/// Compute distance between two Values. Type-appropriate:
///   Integer/Float: |a - e| with cross-type promotion
///   Boolean: 0.0 or 1.0
///   Type mismatch: 1000.0
double value_distance(const etil::core::Value& actual, const etil::core::Value& expected);

/// Evaluates word implementations against test cases.
class Fitness {
public:
    /// Evaluate an implementation against test cases.
    /// Creates a temporary ExecutionContext for each test case.
    FitnessResult evaluate(
        etil::core::WordImpl& impl,
        const std::vector<TestCase>& tests,
        etil::core::Dictionary& dict,
        size_t instruction_budget = 100000,
        FitnessMode mode = FitnessMode::Binary,
        double distance_alpha = 1.0);

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

    /// Distance-mode: returns per-test distance (0.0 = exact match, higher = worse).
    /// Returns infinity on execution failure.
    double run_single_test_distance(
        etil::core::WordImpl& impl,
        const TestCase& tc,
        etil::core::Dictionary& dict,
        size_t instruction_budget,
        double& elapsed_ns,
        bool& exact_match);
};

} // namespace etil::evolution
