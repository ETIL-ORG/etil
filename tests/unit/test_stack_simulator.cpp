// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/stack_simulator.hpp"
#include "etil/evolution/decompiler.hpp"
#include "etil/evolution/signature_index.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/interpreter.hpp"
#include <gtest/gtest.h>
#include <sstream>

using namespace etil::evolution;
using namespace etil::core;
using T = TypeSignature::Type;

class StackSimulatorTest : public ::testing::Test {
protected:
    Dictionary dict;
    std::ostringstream out;
    std::ostringstream err;
    Interpreter interp{dict, out, err};
    Decompiler decompiler;
    StackSimulator simulator;

    void SetUp() override {
        register_primitives(dict);
    }

    ASTNode decompile_word(const std::string& name) {
        auto impl = dict.lookup(name);
        EXPECT_TRUE(impl.has_value());
        return decompiler.decompile(*(*impl)->bytecode());
    }
};

// --- Basic stack effects ---

TEST_F(StackSimulatorTest, DupAdd) {
    interp.interpret_line(": test dup + ;");
    auto ast = decompile_word("test");
    bool ok = simulator.annotate(ast, dict);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(ast.effect.valid);
    // dup: consumes 1, produces 2. +: consumes 2, produces 1.
    // net: consumes 1, produces 1
    EXPECT_EQ(ast.effect.consumed, 1);
    EXPECT_EQ(ast.effect.produced, 1);
}

TEST_F(StackSimulatorTest, Add) {
    interp.interpret_line(": test + ;");
    auto ast = decompile_word("test");
    simulator.annotate(ast, dict);
    EXPECT_EQ(ast.effect.consumed, 2);
    EXPECT_EQ(ast.effect.produced, 1);
}

TEST_F(StackSimulatorTest, LiteralPush) {
    interp.interpret_line(": test 42 ;");
    auto ast = decompile_word("test");
    simulator.annotate(ast, dict);
    EXPECT_EQ(ast.effect.consumed, 0);
    EXPECT_EQ(ast.effect.produced, 1);
}

TEST_F(StackSimulatorTest, SwapTracking) {
    interp.interpret_line(": test swap + ;");
    auto ast = decompile_word("test");
    bool ok = simulator.annotate(ast, dict);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ast.effect.consumed, 2);
    EXPECT_EQ(ast.effect.produced, 1);
}

TEST_F(StackSimulatorTest, OverTracking) {
    interp.interpret_line(": test over + ;");
    auto ast = decompile_word("test");
    simulator.annotate(ast, dict);
    EXPECT_EQ(ast.effect.consumed, 2);
    EXPECT_EQ(ast.effect.produced, 2);
}

TEST_F(StackSimulatorTest, DropTracking) {
    interp.interpret_line(": test drop ;");
    auto ast = decompile_word("test");
    simulator.annotate(ast, dict);
    EXPECT_EQ(ast.effect.consumed, 1);
    EXPECT_EQ(ast.effect.produced, 0);
}

// --- Type inference ---

TEST_F(StackSimulatorTest, InferDupAdd) {
    interp.interpret_line(": test dup + ;");
    auto ast = decompile_word("test");
    auto sig = simulator.infer_signature(ast, dict);
    EXPECT_FALSE(sig.variable_inputs);
    EXPECT_FALSE(sig.variable_outputs);
    EXPECT_EQ(sig.inputs.size(), 1u);
    EXPECT_EQ(sig.outputs.size(), 1u);
}

// --- Opaque words ---

TEST_F(StackSimulatorTest, ExecuteIsOpaque) {
    interp.interpret_line(": test execute ;");
    auto ast = decompile_word("test");
    auto sig = simulator.infer_signature(ast, dict);
    // execute has no type signature in the dictionary → opaque
    EXPECT_TRUE(sig.variable_inputs || sig.variable_outputs);
}

// --- IfThen ---

TEST_F(StackSimulatorTest, IfThenEffect) {
    interp.interpret_line(": test dup 0> if 1 + then ;");
    auto ast = decompile_word("test");
    simulator.annotate(ast, dict);
    // net: consumes 1, produces 1
    EXPECT_EQ(ast.effect.consumed, 1);
    EXPECT_EQ(ast.effect.produced, 1);
}

// --- DoLoop ---

TEST_F(StackSimulatorTest, DoLoopReducing) {
    // Loop body: i + (net 0 per iteration — adds i to accumulator)
    interp.interpret_line(": test 0 swap 0 do i + loop ;");
    auto ast = decompile_word("test");
    simulator.annotate(ast, dict);
    EXPECT_EQ(ast.effect.consumed, 1);
    EXPECT_EQ(ast.effect.produced, 1);
}

// --- SignatureIndex ---

class SignatureIndexTest : public ::testing::Test {
protected:
    Dictionary dict;

    void SetUp() override {
        register_primitives(dict);
    }
};

TEST_F(SignatureIndexTest, RebuildAndQuery) {
    SignatureIndex index;
    index.rebuild(dict);
    // dup is (1 in, 2 out)
    auto results = index.find_compatible(1, 2);
    bool found_dup = false;
    for (const auto& name : results) {
        if (name == "dup") found_dup = true;
    }
    EXPECT_TRUE(found_dup);
}

TEST_F(SignatureIndexTest, FindExact) {
    SignatureIndex index;
    index.rebuild(dict);
    TypeSignature sig;
    sig.inputs = {T::Unknown, T::Unknown};
    sig.outputs = {T::Unknown};
    auto results = index.find_exact(sig);
    // Should include +, -, *, /, etc.
    EXPECT_GT(results.size(), 3u);
}

TEST_F(SignatureIndexTest, NoResults) {
    SignatureIndex index;
    index.rebuild(dict);
    auto results = index.find_compatible(99, 99);
    EXPECT_TRUE(results.empty());
}

TEST_F(SignatureIndexTest, Generation) {
    SignatureIndex index;
    index.rebuild(dict);
    EXPECT_EQ(index.generation(), dict.generation());
}

// ===================================================================
// Compile-time type inference (Phase 5)
// ===================================================================

class CompileTimeInferenceTest : public ::testing::Test {
protected:
    Dictionary dict;
    std::ostringstream out;
    std::ostringstream err;
    Interpreter interp{dict, out, err};

    void SetUp() override {
        register_primitives(dict);
    }
};

TEST_F(CompileTimeInferenceTest, ColonDefinitionGetsSignature) {
    interp.interpret_line(": double dup + ;");
    auto impl = dict.lookup("double");
    ASSERT_TRUE(impl.has_value());
    const auto& sig = (*impl)->signature();
    // dup + : consumes 1, produces 1
    EXPECT_EQ(sig.inputs.size(), 1u);
    EXPECT_EQ(sig.outputs.size(), 1u);
}

TEST_F(CompileTimeInferenceTest, TwoInputWord) {
    interp.interpret_line(": add-them + ;");
    auto impl = dict.lookup("add-them");
    ASSERT_TRUE(impl.has_value());
    const auto& sig = (*impl)->signature();
    EXPECT_EQ(sig.inputs.size(), 2u);
    EXPECT_EQ(sig.outputs.size(), 1u);
}

TEST_F(CompileTimeInferenceTest, LiteralOnlyWord) {
    interp.interpret_line(": push42 42 ;");
    auto impl = dict.lookup("push42");
    ASSERT_TRUE(impl.has_value());
    const auto& sig = (*impl)->signature();
    EXPECT_EQ(sig.inputs.size(), 0u);
    EXPECT_EQ(sig.outputs.size(), 1u);
}

TEST_F(CompileTimeInferenceTest, OpaqueWordMarkedVariable) {
    interp.interpret_line(": run-it execute ;");
    auto impl = dict.lookup("run-it");
    ASSERT_TRUE(impl.has_value());
    const auto& sig = (*impl)->signature();
    EXPECT_TRUE(sig.variable_inputs || sig.variable_outputs);
}

TEST_F(CompileTimeInferenceTest, UserWordUsableInIndex) {
    interp.interpret_line(": my-double dup + ;");
    SignatureIndex index;
    index.rebuild(dict);
    // my-double has signature (1, 1) — should appear in index
    auto results = index.find_compatible(1, 1);
    bool found = false;
    for (const auto& name : results) {
        if (name == "my-double") found = true;
    }
    EXPECT_TRUE(found);
}
