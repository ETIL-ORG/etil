// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/ast_genetic_ops.hpp"
#include "etil/evolution/evolution_engine.hpp"
#include "etil/evolution/fitness.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/interpreter.hpp"
#include "etil/core/compiled_body.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <set>

using namespace etil::evolution;
using namespace etil::core;

class ASTGeneticOpsTest : public ::testing::Test {
protected:
    Dictionary dict;
    std::ostringstream out;
    std::ostringstream err;
    Interpreter interp{dict, out, err};

    void SetUp() override {
        register_primitives(dict);
    }

    std::vector<int64_t> execute_impl(WordImpl& impl, std::vector<int64_t> inputs) {
        auto bc = impl.bytecode();
        if (!bc) return {};
        ExecutionContext ctx(0);
        ctx.set_dictionary(&dict);
        for (auto v : inputs) ctx.data_stack().push(Value(v));
        bool ok = execute_compiled(*bc, ctx);
        if (!ok) return {};
        std::vector<int64_t> results;
        while (ctx.data_stack().size() > 0) {
            auto v = ctx.data_stack().pop();
            if (v) results.push_back(v->as_int);
        }
        std::reverse(results.begin(), results.end());
        return results;
    }
};

// --- Mutation produces a child ---

TEST_F(ASTGeneticOpsTest, MutateProducesChild) {
    interp.interpret_line(": test-word dup + ;");
    auto impl = dict.lookup("test-word");
    ASSERT_TRUE(impl.has_value());

    ASTGeneticOps ops(dict);
    // Try multiple times (mutation is probabilistic)
    bool produced = false;
    for (int i = 0; i < 20; ++i) {
        auto child = ops.mutate(**impl);
        if (child) { produced = true; break; }
    }
    EXPECT_TRUE(produced);
}

// --- Mutant has bytecode ---

TEST_F(ASTGeneticOpsTest, MutantHasBytecode) {
    interp.interpret_line(": test-bc 1 2 + ;");
    auto impl = dict.lookup("test-bc");
    ASTGeneticOps ops(dict);

    for (int i = 0; i < 20; ++i) {
        auto child = ops.mutate(**impl);
        if (child) {
            EXPECT_TRUE(child->bytecode() != nullptr);
            EXPECT_GT(child->bytecode()->size(), 0u);
            break;
        }
    }
}

// --- Mutant has correct lineage ---

TEST_F(ASTGeneticOpsTest, MutantLineage) {
    interp.interpret_line(": test-lin dup + ;");
    auto impl = dict.lookup("test-lin");
    ASTGeneticOps ops(dict);

    for (int i = 0; i < 20; ++i) {
        auto child = ops.mutate(**impl);
        if (child) {
            EXPECT_EQ(child->name(), "test-lin");
            EXPECT_EQ(child->generation(), (*impl)->generation() + 1);
            EXPECT_EQ(child->parent_ids().size(), 1u);
            EXPECT_EQ(child->parent_ids()[0], (*impl)->id());
            break;
        }
    }
}

// --- Constant perturbation changes a value ---

TEST_F(ASTGeneticOpsTest, PerturbChangesValue) {
    interp.interpret_line(": test-perturb 42 + ;");
    auto impl = dict.lookup("test-perturb");
    ASTGeneticOps ops(dict);

    bool value_changed = false;
    for (int i = 0; i < 50; ++i) {
        auto child = ops.mutate(**impl);
        if (child) {
            auto orig = execute_impl(**impl, {10});
            auto mutant = execute_impl(*child, {10});
            if (orig != mutant) { value_changed = true; break; }
        }
    }
    EXPECT_TRUE(value_changed);
}

// --- Crossover produces child ---

TEST_F(ASTGeneticOpsTest, CrossoverProducesChild) {
    interp.interpret_line(": parent-a dup + ;");
    interp.interpret_line(": parent-b dup * ;");
    auto a = dict.lookup("parent-a");
    auto b = dict.lookup("parent-b");
    ASSERT_TRUE(a.has_value() && b.has_value());

    ASTGeneticOps ops(dict);
    bool produced = false;
    for (int i = 0; i < 20; ++i) {
        auto child = ops.crossover(**a, **b);
        if (child) {
            produced = true;
            EXPECT_TRUE(child->bytecode() != nullptr);
            EXPECT_EQ(child->parent_ids().size(), 2u);
            break;
        }
    }
    EXPECT_TRUE(produced);
}

// --- Crossover on native returns null ---

TEST_F(ASTGeneticOpsTest, CrossoverNativeReturnsNull) {
    auto a = dict.lookup("+");
    auto b = dict.lookup("*");
    ASTGeneticOps ops(dict);
    auto child = ops.crossover(**a, **b);
    EXPECT_FALSE(child);
}

// --- Mutant executes without crash ---

TEST_F(ASTGeneticOpsTest, MutantExecutes) {
    interp.interpret_line(": test-exec 10 + ;");
    auto impl = dict.lookup("test-exec");
    ASTGeneticOps ops(dict);

    int executed = 0;
    for (int i = 0; i < 30; ++i) {
        auto child = ops.mutate(**impl);
        if (child && child->bytecode()) {
            ExecutionContext ctx(0);
            ctx.set_dictionary(&dict);
            ctx.data_stack().push(Value(int64_t(5)));
            ctx.set_limits(10000, 1000, 100, 1.0);
            execute_compiled(*child->bytecode(), ctx);
            // Just verify no crash — result may be wrong
            while (ctx.data_stack().size() > 0) {
                auto v = ctx.data_stack().pop();
                if (v) value_release(*v);
            }
            ctx.reset_limits();
            executed++;
        }
    }
    EXPECT_GT(executed, 0);
}

// --- EvolutionEngine uses AST ops ---

TEST_F(ASTGeneticOpsTest, EngineUsesASTOps) {
    interp.interpret_line(": evo-test dup + ;");

    EvolutionConfig config;
    config.generation_size = 3;
    config.population_limit = 10;
    config.use_ast_ops = true;
    EvolutionEngine engine(config, dict);

    std::vector<TestCase> tests;
    tests.push_back({{Value(int64_t(3))}, {Value(int64_t(6))}});
    tests.push_back({{Value(int64_t(5))}, {Value(int64_t(10))}});
    engine.register_tests("evo-test", tests);

    size_t created = engine.evolve_word("evo-test");
    EXPECT_GT(created, 0u);
    EXPECT_EQ(engine.generations_run("evo-test"), 1u);
}

// --- EvolutionEngine fallback to bytecode ops ---

TEST_F(ASTGeneticOpsTest, EngineFallbackBytecodeOps) {
    interp.interpret_line(": evo-fallback dup + ;");

    EvolutionConfig config;
    config.generation_size = 3;
    config.population_limit = 10;
    config.use_ast_ops = false;
    EvolutionEngine engine(config, dict);

    std::vector<TestCase> tests;
    tests.push_back({{Value(int64_t(3))}, {Value(int64_t(6))}});
    engine.register_tests("evo-fallback", tests);

    size_t created = engine.evolve_word("evo-fallback");
    EXPECT_GT(created, 0u);
}

// --- Multiple generations ---

TEST_F(ASTGeneticOpsTest, MultipleASTGenerations) {
    interp.interpret_line(": evo-multi 10 + ;");

    EvolutionConfig config;
    config.generation_size = 2;
    config.population_limit = 8;
    config.use_ast_ops = true;
    EvolutionEngine engine(config, dict);

    engine.register_tests("evo-multi",
        {{{Value(int64_t(5))}, {Value(int64_t(15))}}});

    for (int g = 0; g < 5; ++g) {
        engine.evolve_word("evo-multi");
    }
    EXPECT_EQ(engine.generations_run("evo-multi"), 5u);
}
