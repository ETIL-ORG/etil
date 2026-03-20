// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/ast_compiler.hpp"
#include "etil/evolution/decompiler.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/interpreter.hpp"
#include "etil/core/compiled_body.hpp"
#include <gtest/gtest.h>
#include <sstream>

using namespace etil::evolution;
using namespace etil::core;

class ASTCompilerTest : public ::testing::Test {
protected:
    Dictionary dict;
    std::ostringstream out;
    std::ostringstream err;
    Interpreter interp{dict, out, err};
    Decompiler decompiler;
    ASTCompiler compiler;

    void SetUp() override {
        register_primitives(dict);
    }

    // Execute a bytecode with given integer inputs, return stack as vector of int64
    std::vector<int64_t> execute_with(ByteCode& bc, std::vector<int64_t> inputs) {
        ExecutionContext ctx(0);
        ctx.set_dictionary(&dict);
        for (auto v : inputs) ctx.data_stack().push(Value(v));
        bool ok = execute_compiled(bc, ctx);
        EXPECT_TRUE(ok) << "Execution failed, err: " << err.str();
        std::vector<int64_t> results;
        while (ctx.data_stack().size() > 0) {
            auto v = ctx.data_stack().pop();
            if (v) results.push_back(v->as_int);
        }
        std::reverse(results.begin(), results.end());
        return results;
    }

    // Round-trip test: compile word, decompile, recompile, compare execution
    void round_trip(const std::string& def, const std::string& name,
                    std::vector<int64_t> inputs, std::vector<int64_t> expected) {
        interp.interpret_line(def);
        auto impl = dict.lookup(name);
        ASSERT_TRUE(impl.has_value()) << "Word not found: " << name;
        auto& orig_bc = *(*impl)->bytecode();

        // Execute original
        auto orig_result = execute_with(orig_bc, inputs);
        EXPECT_EQ(orig_result, expected) << "Original execution wrong";

        // Decompile + recompile
        auto ast = decompiler.decompile(orig_bc);
        auto new_bc = compiler.compile(ast);

        // Execute recompiled
        auto new_result = execute_with(*new_bc, inputs);
        EXPECT_EQ(new_result, expected) << "Recompiled execution wrong";
        EXPECT_EQ(orig_result, new_result) << "Original != Recompiled";
    }
};

// --- Linear ---

TEST_F(ASTCompilerTest, RoundTripLinear) {
    round_trip(": rt-linear dup + ;", "rt-linear", {21}, {42});
}

TEST_F(ASTCompilerTest, RoundTripLiterals) {
    round_trip(": rt-lit 10 + ;", "rt-lit", {5}, {15});
}

// --- if/then ---

TEST_F(ASTCompilerTest, RoundTripIfThen) {
    round_trip(": rt-ifthen dup 0> if 1 + then ;", "rt-ifthen", {5}, {6});
    round_trip(": rt-ifthen2 dup 0> if 1 + then ;", "rt-ifthen2", {-3}, {-3});
}

// --- if/else/then ---

TEST_F(ASTCompilerTest, RoundTripIfElseThen) {
    round_trip(": rt-ifelse dup 0> if 1 + else negate then ;", "rt-ifelse", {5}, {6});
}

TEST_F(ASTCompilerTest, RoundTripIfElseThenNeg) {
    round_trip(": rt-ifelse2 dup 0> if 1 + else negate then ;", "rt-ifelse2", {-3}, {3});
}

// --- do/loop ---

TEST_F(ASTCompilerTest, RoundTripDoLoop) {
    round_trip(": rt-doloop 0 swap 0 do i + loop ;", "rt-doloop", {5}, {10});
}

// --- do/+loop ---

TEST_F(ASTCompilerTest, RoundTripDoPlusLoop) {
    // Sum even numbers 0,2,4,6,8
    round_trip(": rt-dploop 0 swap 0 do i + 2 +loop ;", "rt-dploop", {10}, {20});
}

// --- begin/until ---

TEST_F(ASTCompilerTest, RoundTripBeginUntil) {
    round_trip(": rt-buntil begin 1 - dup 0= until ;", "rt-buntil", {5}, {0});
}

// --- begin/while/repeat ---

TEST_F(ASTCompilerTest, RoundTripBeginWhileRepeat) {
    round_trip(": rt-bwr begin dup 0> while 1 - repeat ;", "rt-bwr", {5}, {0});
}

// --- Nested: do/loop with if/then ---

TEST_F(ASTCompilerTest, RoundTripNested) {
    // Sum only even indices: 0+2+4+6+8 = 20
    round_trip(": rt-nested 0 swap 0 do i 2 mod 0= if i + then loop ;",
               "rt-nested", {10}, {20});
}

// --- leave ---

TEST_F(ASTCompilerTest, RoundTripLeave) {
    // Sum 0+1+2+3+4, leave at 5
    round_trip(": rt-leave 0 10 0 do i 5 = if leave then i + loop ;",
               "rt-leave", {}, {10});
}

// --- return stack ---

TEST_F(ASTCompilerTest, RoundTripReturnStack) {
    round_trip(": rt-rstack >r r@ r> + ;", "rt-rstack", {3}, {6});
}

// --- Markers preserved ---

TEST_F(ASTCompilerTest, RecompiledHasMarkers) {
    interp.interpret_line(": rt-markers dup 0> if 1 + then ;");
    auto impl = dict.lookup("rt-markers");
    auto& orig_bc = *(*impl)->bytecode();
    auto ast = decompiler.decompile(orig_bc);
    auto new_bc = compiler.compile(ast);

    // Verify markers exist in recompiled bytecode
    bool has_begin = false, has_end = false;
    for (const auto& instr : new_bc->instructions()) {
        if (instr.op == Instruction::Op::BlockBegin) has_begin = true;
        if (instr.op == Instruction::Op::BlockEnd) has_end = true;
    }
    EXPECT_TRUE(has_begin);
    EXPECT_TRUE(has_end);
}

// --- Empty word ---

TEST_F(ASTCompilerTest, RoundTripEmpty) {
    round_trip(": rt-empty ;", "rt-empty", {42}, {42});
}
