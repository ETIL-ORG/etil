// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/fitness.hpp"
#include "etil/core/compiled_body.hpp"
#include "etil/core/heap_object.hpp"

#include <chrono>
#include <cmath>
#include <limits>

namespace etil::evolution {

using namespace etil::core;

static void drain_stack(ExecutionContext& ctx) {
    while (ctx.data_stack().size() > 0) {
        auto v = ctx.data_stack().pop();
        if (v) value_release(*v);
    }
}

// --- Distance functions ---

static double as_double(const Value& v) {
    if (v.type == Value::Type::Float) return v.as_float;
    if (v.type == Value::Type::Integer) return static_cast<double>(v.as_int);
    if (v.type == Value::Type::Boolean) return v.as_int ? 1.0 : 0.0;
    return std::numeric_limits<double>::quiet_NaN();
}

double value_distance(const Value& actual, const Value& expected) {
    // Same numeric types or cross-promotable
    bool actual_numeric = (actual.type == Value::Type::Integer ||
                           actual.type == Value::Type::Float);
    bool expected_numeric = (expected.type == Value::Type::Integer ||
                             expected.type == Value::Type::Float);

    if (actual_numeric && expected_numeric) {
        return std::abs(as_double(actual) - as_double(expected));
    }

    // Boolean
    if (actual.type == Value::Type::Boolean && expected.type == Value::Type::Boolean) {
        return (actual.as_int == expected.as_int) ? 0.0 : 1.0;
    }

    // Numeric vs boolean: treat bool as 0/1
    if ((actual_numeric && expected.type == Value::Type::Boolean) ||
        (actual.type == Value::Type::Boolean && expected_numeric)) {
        return std::abs(as_double(actual) - as_double(expected));
    }

    // Type mismatch
    return 1000.0;
}

bool Fitness::run_single_test(
    WordImpl& impl,
    const TestCase& tc,
    Dictionary& dict,
    size_t instruction_budget,
    double& elapsed_ns) {

    ExecutionContext ctx(0);
    ctx.set_dictionary(&dict);
    ctx.set_limits(instruction_budget, 10000, SIZE_MAX, 10.0);

    // Push inputs
    for (const auto& val : tc.inputs) {
        value_addref(const_cast<Value&>(val));
        ctx.data_stack().push(val);
    }

    // Execute
    auto start = std::chrono::steady_clock::now();
    bool ok = false;
    if (impl.native_code()) {
        ok = impl.native_code()(ctx);
    } else if (impl.bytecode()) {
        ok = execute_compiled(*impl.bytecode(), ctx);
    }
    auto end = std::chrono::steady_clock::now();
    elapsed_ns = std::chrono::duration<double, std::nano>(end - start).count();

    if (!ok) {
        drain_stack(ctx);
        ctx.reset_limits();
        return false;
    }

    // Check outputs
    if (ctx.data_stack().size() != tc.expected.size()) {
        drain_stack(ctx);
        ctx.reset_limits();
        return false;
    }

    // Compare from bottom to top (pop in reverse)
    bool match = true;
    for (int i = static_cast<int>(tc.expected.size()) - 1; i >= 0; --i) {
        auto actual = ctx.data_stack().pop();
        if (!actual) { match = false; break; }
        const auto& exp = tc.expected[static_cast<size_t>(i)];
        if (actual->type != exp.type) { match = false; value_release(*actual); break; }
        switch (exp.type) {
            case Value::Type::Integer:
                if (actual->as_int != exp.as_int) match = false;
                break;
            case Value::Type::Float:
                if (std::abs(actual->as_float - exp.as_float) > 1e-9) match = false;
                break;
            case Value::Type::Boolean:
                if (actual->as_int != exp.as_int) match = false;
                break;
            default:
                // For heap types, just check type matches (deep comparison is complex)
                break;
        }
        value_release(*actual);
        if (!match) break;
    }

    // Clean up remaining stack values
    drain_stack(ctx);
    ctx.reset_limits();
    return match;
}

double Fitness::run_single_test_distance(
    WordImpl& impl,
    const TestCase& tc,
    Dictionary& dict,
    size_t instruction_budget,
    double& elapsed_ns,
    bool& exact_match) {

    exact_match = false;
    ExecutionContext ctx(0);
    ctx.set_dictionary(&dict);
    ctx.set_limits(instruction_budget, 10000, SIZE_MAX, 10.0);

    // Push inputs
    for (const auto& val : tc.inputs) {
        value_addref(const_cast<Value&>(val));
        ctx.data_stack().push(val);
    }

    // Execute
    auto start = std::chrono::steady_clock::now();
    bool ok = false;
    if (impl.native_code()) {
        ok = impl.native_code()(ctx);
    } else if (impl.bytecode()) {
        ok = execute_compiled(*impl.bytecode(), ctx);
    }
    auto end = std::chrono::steady_clock::now();
    elapsed_ns = std::chrono::duration<double, std::nano>(end - start).count();

    if (!ok) {
        drain_stack(ctx);
        ctx.reset_limits();
        return std::numeric_limits<double>::infinity();
    }

    // Stack depth mismatch penalty
    size_t actual_depth = ctx.data_stack().size();
    size_t expected_depth = tc.expected.size();
    double depth_penalty = 0.0;
    if (actual_depth != expected_depth) {
        depth_penalty = std::abs(static_cast<double>(actual_depth) -
                                  static_cast<double>(expected_depth)) * 100.0;
    }

    // Compute element-wise distance
    double total_distance = depth_penalty;
    bool all_exact = (actual_depth == expected_depth);
    size_t compare_count = std::min(actual_depth, expected_depth);

    // Pop from top, compare against expected (reversed)
    for (size_t i = 0; i < compare_count; ++i) {
        size_t exp_idx = expected_depth - 1 - i;
        auto actual = ctx.data_stack().pop();
        if (!actual) { all_exact = false; break; }
        double d = value_distance(*actual, tc.expected[exp_idx]);
        total_distance += d;
        if (d > 1e-9) all_exact = false;
        value_release(*actual);
    }

    exact_match = all_exact && (depth_penalty == 0.0);

    drain_stack(ctx);
    ctx.reset_limits();
    return total_distance;
}

FitnessResult Fitness::evaluate(
    WordImpl& impl,
    const std::vector<TestCase>& tests,
    Dictionary& dict,
    size_t instruction_budget,
    FitnessMode mode,
    double distance_alpha) {

    FitnessResult result;
    result.tests_total = tests.size();
    if (tests.empty()) {
        result.correctness = 1.0;
        result.fitness = 1.0;
        return result;
    }

    if (mode == FitnessMode::Binary) {
        // Original binary evaluation
        double total_time = 0.0;
        size_t timed_tests = 0;
        for (const auto& tc : tests) {
            double elapsed = 0.0;
            bool passed = run_single_test(impl, tc, dict, instruction_budget, elapsed);
            if (passed) {
                result.tests_passed++;
                total_time += elapsed;
                timed_tests++;
            }
        }
        result.correctness = static_cast<double>(result.tests_passed)
                           / static_cast<double>(result.tests_total);
        result.speed = timed_tests > 0
                     ? total_time / static_cast<double>(timed_tests) : 0.0;
    } else {
        // Distance-based evaluation
        double total_score = 0.0;
        double total_distance = 0.0;
        double total_time = 0.0;

        for (const auto& tc : tests) {
            double elapsed = 0.0;
            bool exact = false;
            double dist = run_single_test_distance(
                impl, tc, dict, instruction_budget, elapsed, exact);

            if (exact) result.tests_passed++;
            total_time += elapsed;

            if (std::isinf(dist)) {
                // Execution failure → score 0
                total_distance += 1e6;
            } else {
                total_distance += dist;
                total_score += 1.0 / (1.0 + distance_alpha * dist);
            }
        }

        result.correctness = total_score / static_cast<double>(result.tests_total);
        result.mean_distance = total_distance / static_cast<double>(result.tests_total);
        result.speed = total_time / static_cast<double>(result.tests_total);
    }

    // Combined fitness: correctness dominates, speed is a tiebreaker
    double speed_score = 1.0 / (1.0 + result.speed / 1e6);
    result.fitness = result.correctness * (1.0 - speed_weight_)
                   + speed_score * speed_weight_;

    return result;
}

} // namespace etil::evolution
