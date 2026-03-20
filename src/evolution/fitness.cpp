// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/fitness.hpp"
#include "etil/core/compiled_body.hpp"
#include "etil/core/heap_object.hpp"

#include <chrono>

namespace etil::evolution {

using namespace etil::core;

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
        // Clean up stack
        while (ctx.data_stack().size() > 0) {
            auto v = ctx.data_stack().pop();
            if (v) value_release(*v);
        }
        ctx.reset_limits();
        return false;
    }

    // Check outputs
    if (ctx.data_stack().size() != tc.expected.size()) {
        while (ctx.data_stack().size() > 0) {
            auto v = ctx.data_stack().pop();
            if (v) value_release(*v);
        }
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
    while (ctx.data_stack().size() > 0) {
        auto v = ctx.data_stack().pop();
        if (v) value_release(*v);
    }
    ctx.reset_limits();
    return match;
}

FitnessResult Fitness::evaluate(
    WordImpl& impl,
    const std::vector<TestCase>& tests,
    Dictionary& dict,
    size_t instruction_budget) {

    FitnessResult result;
    result.tests_total = tests.size();
    if (tests.empty()) {
        result.correctness = 1.0;
        result.fitness = 1.0;
        return result;
    }

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
                 ? total_time / static_cast<double>(timed_tests)
                 : 0.0;

    // Combined fitness: correctness dominates, speed is a tiebreaker
    // Normalize speed: faster = higher score (use 1/(1+speed_ns/1e6))
    double speed_score = 1.0 / (1.0 + result.speed / 1e6);
    result.fitness = result.correctness * (1.0 - speed_weight_)
                   + speed_score * speed_weight_;

    return result;
}

} // namespace etil::evolution
