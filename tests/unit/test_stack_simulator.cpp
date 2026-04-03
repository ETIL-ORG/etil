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

// --- Phase 0: Type signature backfill validation ---

TEST_F(StackSimulatorTest, ComparisonOutputBoolean) {
    // Comparisons must return Boolean, not Integer
    auto impl = dict.lookup("=");
    ASSERT_TRUE(impl.has_value());
    const auto& sig = (*impl)->signature();
    ASSERT_EQ(sig.outputs.size(), 1u);
    EXPECT_EQ(sig.outputs[0], T::Boolean);
}

TEST_F(StackSimulatorTest, ZeroComparisonOutputBoolean) {
    auto impl = dict.lookup("0=");
    ASSERT_TRUE(impl.has_value());
    const auto& sig = (*impl)->signature();
    ASSERT_EQ(sig.outputs.size(), 1u);
    EXPECT_EQ(sig.outputs[0], T::Boolean);
}

TEST_F(StackSimulatorTest, TrueFalseOutputBoolean) {
    auto impl_t = dict.lookup("true");
    ASSERT_TRUE(impl_t.has_value());
    ASSERT_EQ((*impl_t)->signature().outputs.size(), 1u);
    EXPECT_EQ((*impl_t)->signature().outputs[0], T::Boolean);

    auto impl_f = dict.lookup("false");
    ASSERT_TRUE(impl_f.has_value());
    ASSERT_EQ((*impl_f)->signature().outputs.size(), 1u);
    EXPECT_EQ((*impl_f)->signature().outputs[0], T::Boolean);
}

TEST_F(StackSimulatorTest, NotBoolOutputBoolean) {
    auto impl_not = dict.lookup("not");
    ASSERT_TRUE(impl_not.has_value());
    ASSERT_EQ((*impl_not)->signature().outputs.size(), 1u);
    EXPECT_EQ((*impl_not)->signature().outputs[0], T::Boolean);

    auto impl_bool = dict.lookup("bool");
    ASSERT_TRUE(impl_bool.has_value());
    ASSERT_EQ((*impl_bool)->signature().outputs.size(), 1u);
    EXPECT_EQ((*impl_bool)->signature().outputs[0], T::Boolean);
}

TEST_F(StackSimulatorTest, WithinOutputBoolean) {
    auto impl = dict.lookup("within");
    ASSERT_TRUE(impl.has_value());
    const auto& sig = (*impl)->signature();
    ASSERT_EQ(sig.outputs.size(), 1u);
    EXPECT_EQ(sig.outputs[0], T::Boolean);
}

TEST_F(StackSimulatorTest, TickOutputXt) {
    auto impl = dict.lookup("'");
    ASSERT_TRUE(impl.has_value());
    const auto& sig = (*impl)->signature();
    ASSERT_EQ(sig.outputs.size(), 1u);
    EXPECT_EQ(sig.outputs[0], T::Xt);
}

TEST_F(StackSimulatorTest, ExecuteInputXt) {
    auto impl = dict.lookup("execute");
    ASSERT_TRUE(impl.has_value());
    const auto& sig = (*impl)->signature();
    ASSERT_EQ(sig.inputs.size(), 1u);
    EXPECT_EQ(sig.inputs[0], T::Xt);
}

TEST_F(StackSimulatorTest, XtQueryInputXtOutputBoolean) {
    auto impl = dict.lookup("xt?");
    ASSERT_TRUE(impl.has_value());
    const auto& sig = (*impl)->signature();
    ASSERT_EQ(sig.inputs.size(), 1u);
    EXPECT_EQ(sig.inputs[0], T::Xt);
    ASSERT_EQ(sig.outputs.size(), 1u);
    EXPECT_EQ(sig.outputs[0], T::Boolean);
}

TEST_F(StackSimulatorTest, XtBodyInputXtOutputDataRef) {
    auto impl = dict.lookup("xt-body");
    ASSERT_TRUE(impl.has_value());
    const auto& sig = (*impl)->signature();
    ASSERT_EQ(sig.inputs.size(), 1u);
    EXPECT_EQ(sig.inputs[0], T::Xt);
    ASSERT_EQ(sig.outputs.size(), 1u);
    EXPECT_EQ(sig.outputs[0], T::DataRef);
}

TEST_F(StackSimulatorTest, AndOrXorRemainPolymorphic) {
    // and/or/xor are dual-purpose (boolean + bitwise integer)
    // They must remain Unknown for inputs and outputs
    for (const char* word : {"and", "or", "xor"}) {
        auto impl = dict.lookup(word);
        ASSERT_TRUE(impl.has_value()) << word;
        const auto& sig = (*impl)->signature();
        for (const auto& t : sig.inputs) {
            EXPECT_EQ(t, T::Unknown) << word << " input should be Unknown (polymorphic)";
        }
        for (const auto& t : sig.outputs) {
            EXPECT_EQ(t, T::Unknown) << word << " output should be Unknown (polymorphic)";
        }
    }
}

TEST_F(StackSimulatorTest, ArithmeticInputsRemainPolymorphic) {
    // Arithmetic words accept Integer and Float — must stay Unknown until
    // a Numeric meta-type is added
    for (const char* word : {"+", "-", "*", "/", "mod"}) {
        auto impl = dict.lookup(word);
        ASSERT_TRUE(impl.has_value()) << word;
        const auto& sig = (*impl)->signature();
        for (const auto& t : sig.inputs) {
            EXPECT_EQ(t, T::Unknown) << word << " input should be Unknown (Int/Float polymorphic)";
        }
    }
}

// --- Input type inference for TIL-compiled words ---

TEST_F(CompileTimeInferenceTest, InferStringInputFromSplus) {
    // `: str-pair dup s+ ;` — s+ needs (String, String), so input must be String
    interp.interpret_line(": str-pair dup s+ ;");
    auto impl = dict.lookup("str-pair");
    ASSERT_TRUE(impl.has_value());
    const auto& sig = (*impl)->signature();
    ASSERT_EQ(sig.inputs.size(), 1u);
    EXPECT_EQ(sig.inputs[0], T::String);
}

TEST_F(CompileTimeInferenceTest, InferUnknownInputFromAdd) {
    // `: double dup + ;` — + takes Unknown (polymorphic), so input stays Unknown
    interp.interpret_line(": my-dbl dup + ;");
    auto impl = dict.lookup("my-dbl");
    ASSERT_TRUE(impl.has_value());
    const auto& sig = (*impl)->signature();
    ASSERT_EQ(sig.inputs.size(), 1u);
    EXPECT_EQ(sig.inputs[0], T::Unknown);
}

TEST_F(CompileTimeInferenceTest, InferArrayInputFromArrayLength) {
    // `: len array-length ;` — array-length needs Array
    interp.interpret_line(": len array-length ;");
    auto impl = dict.lookup("len");
    ASSERT_TRUE(impl.has_value());
    const auto& sig = (*impl)->signature();
    ASSERT_EQ(sig.inputs.size(), 1u);
    EXPECT_EQ(sig.inputs[0], T::Array);
}

TEST_F(CompileTimeInferenceTest, InferOutputTypeFromBody) {
    // `: to-str number->string ;` — output should be String
    interp.interpret_line(": to-str number->string ;");
    auto impl = dict.lookup("to-str");
    ASSERT_TRUE(impl.has_value());
    const auto& sig = (*impl)->signature();
    ASSERT_EQ(sig.outputs.size(), 1u);
    EXPECT_EQ(sig.outputs[0], T::String);
}
