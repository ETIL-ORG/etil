// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/execution_context.hpp"
#include <benchmark/benchmark.h>

using namespace etil;

// Benchmark ValueStack push
static void BM_StackPush(benchmark::State& state) {
    core::ValueStack stack;
    core::Value v(int64_t(42));

    for (auto _ : state) {
        stack.push(v);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_StackPush);

// Benchmark ValueStack push + pop
static void BM_StackPushPop(benchmark::State& state) {
    core::ValueStack stack;
    core::Value v(int64_t(42));

    for (auto _ : state) {
        stack.push(v);
        auto val = stack.pop();
        benchmark::DoNotOptimize(val);
    }

    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_StackPushPop);

// Benchmark execution context creation
static void BM_ContextCreation(benchmark::State& state) {
    uint32_t thread_id = 0;

    for (auto _ : state) {
        core::ExecutionContext ctx(thread_id++);
        benchmark::DoNotOptimize(ctx);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ContextCreation);

BENCHMARK_MAIN();
