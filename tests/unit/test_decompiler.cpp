// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/decompiler.hpp"
#include "etil/evolution/ast.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/interpreter.hpp"
#include <gtest/gtest.h>
#include <sstream>

using namespace etil::evolution;
using namespace etil::core;

class DecompilerTest : public ::testing::Test {
protected:
    Dictionary dict;
    std::ostringstream out;
    std::ostringstream err;
    Interpreter interp{dict, out, err};
    Decompiler decompiler;

    void SetUp() override {
        register_primitives(dict);
    }

    ASTNode decompile_word(const std::string& name) {
        auto impl = dict.lookup(name);
        EXPECT_TRUE(impl.has_value());
        return decompiler.decompile(*(*impl)->bytecode());
    }
};

// --- Linear (no control flow) ---

TEST_F(DecompilerTest, Linear) {
    interp.interpret_line(": test dup + ;");
    auto ast = decompile_word("test");
    ASSERT_EQ(ast.kind, ASTNodeKind::Sequence);
    ASSERT_EQ(ast.children.size(), 2u);
    EXPECT_EQ(ast.children[0].kind, ASTNodeKind::WordCall);
    EXPECT_EQ(ast.children[0].word_name, "dup");
    EXPECT_EQ(ast.children[1].kind, ASTNodeKind::WordCall);
    EXPECT_EQ(ast.children[1].word_name, "+");
}

TEST_F(DecompilerTest, Literals) {
    interp.interpret_line(": test 42 3.14 true ;");
    auto ast = decompile_word("test");
    ASSERT_EQ(ast.children.size(), 3u);
    EXPECT_EQ(ast.children[0].kind, ASTNodeKind::Literal);
    EXPECT_EQ(ast.children[0].int_val, 42);
    EXPECT_EQ(ast.children[1].kind, ASTNodeKind::Literal);
    EXPECT_DOUBLE_EQ(ast.children[1].float_val, 3.14);
    // "true" is a dictionary word compiled as Call, not a PushBool literal
    EXPECT_EQ(ast.children[2].kind, ASTNodeKind::WordCall);
    EXPECT_EQ(ast.children[2].word_name, "true");
}

TEST_F(DecompilerTest, StringLiteral) {
    interp.interpret_line(": test s\" hello\" ;");
    auto ast = decompile_word("test");
    ASSERT_EQ(ast.children.size(), 1u);
    EXPECT_EQ(ast.children[0].kind, ASTNodeKind::Literal);
    EXPECT_EQ(ast.children[0].string_val, "hello");
}

TEST_F(DecompilerTest, PrintString) {
    interp.interpret_line(": test .\" hello\" ;");
    auto ast = decompile_word("test");
    ASSERT_EQ(ast.children.size(), 1u);
    EXPECT_EQ(ast.children[0].kind, ASTNodeKind::PrintString);
    EXPECT_EQ(ast.children[0].string_val, "hello");
}

TEST_F(DecompilerTest, PushXt) {
    interp.interpret_line(": test ['] dup ;");
    auto ast = decompile_word("test");
    ASSERT_EQ(ast.children.size(), 1u);
    EXPECT_EQ(ast.children[0].kind, ASTNodeKind::PushXt);
    EXPECT_EQ(ast.children[0].word_name, "dup");
}

// --- if/then ---

TEST_F(DecompilerTest, IfThen) {
    interp.interpret_line(": test dup 0> if 1 + then ;");
    auto ast = decompile_word("test");
    // Sequence: [dup, 0>, IfThen[Sequence[1, +]]]
    ASSERT_GE(ast.children.size(), 3u);
    auto& if_node = ast.children[2];
    EXPECT_EQ(if_node.kind, ASTNodeKind::IfThen);
    ASSERT_EQ(if_node.children.size(), 1u);
    auto& then_body = if_node.children[0];
    EXPECT_EQ(then_body.kind, ASTNodeKind::Sequence);
    ASSERT_EQ(then_body.children.size(), 2u);
    EXPECT_EQ(then_body.children[0].int_val, 1);
    EXPECT_EQ(then_body.children[1].word_name, "+");
}

// --- if/else/then ---

TEST_F(DecompilerTest, IfElseThen) {
    interp.interpret_line(": test dup 0> if 1 + else 1 - then ;");
    auto ast = decompile_word("test");
    ASSERT_GE(ast.children.size(), 3u);
    auto& if_node = ast.children[2];
    EXPECT_EQ(if_node.kind, ASTNodeKind::IfThenElse);
    ASSERT_EQ(if_node.children.size(), 2u);
    // then-body: [1, +]
    EXPECT_EQ(if_node.children[0].children.size(), 2u);
    EXPECT_EQ(if_node.children[0].children[1].word_name, "+");
    // else-body: [1, -]
    EXPECT_EQ(if_node.children[1].children.size(), 2u);
    EXPECT_EQ(if_node.children[1].children[1].word_name, "-");
}

// --- do/loop ---

TEST_F(DecompilerTest, DoLoop) {
    interp.interpret_line(": test 10 0 do i loop ;");
    auto ast = decompile_word("test");
    // Sequence: [10, 0, DoLoop[Sequence[DoI]]]
    ASSERT_GE(ast.children.size(), 3u);
    auto& loop = ast.children[2];
    EXPECT_EQ(loop.kind, ASTNodeKind::DoLoop);
    ASSERT_EQ(loop.children.size(), 1u);
    auto& body = loop.children[0];
    ASSERT_GE(body.children.size(), 1u);
    EXPECT_EQ(body.children[0].kind, ASTNodeKind::DoI);
}

// --- do/+loop ---

TEST_F(DecompilerTest, DoPlusLoop) {
    interp.interpret_line(": test 10 0 do i 2 +loop ;");
    auto ast = decompile_word("test");
    ASSERT_GE(ast.children.size(), 3u);
    auto& loop = ast.children[2];
    EXPECT_EQ(loop.kind, ASTNodeKind::DoPlusLoop);
    ASSERT_EQ(loop.children.size(), 1u);
}

// --- begin/until ---

TEST_F(DecompilerTest, BeginUntil) {
    interp.interpret_line(": test begin 1 - dup 0= until ;");
    auto ast = decompile_word("test");
    ASSERT_GE(ast.children.size(), 1u);
    auto& loop = ast.children[0];
    EXPECT_EQ(loop.kind, ASTNodeKind::BeginUntil);
    ASSERT_EQ(loop.children.size(), 1u);
    // Body includes: 1, -, dup, 0=
    EXPECT_GE(loop.children[0].children.size(), 4u);
}

// --- begin/while/repeat ---

TEST_F(DecompilerTest, BeginWhileRepeat) {
    interp.interpret_line(": test begin dup 0> while 1 - repeat ;");
    auto ast = decompile_word("test");
    ASSERT_GE(ast.children.size(), 1u);
    auto& loop = ast.children[0];
    EXPECT_EQ(loop.kind, ASTNodeKind::BeginWhileRepeat);
    ASSERT_EQ(loop.children.size(), 2u);
    // condition: [dup, 0>]
    EXPECT_GE(loop.children[0].children.size(), 2u);
    // body: [1, -]
    EXPECT_GE(loop.children[1].children.size(), 2u);
}

// --- begin/again ---

TEST_F(DecompilerTest, BeginAgain) {
    interp.interpret_line(": test begin 1 again ;");
    auto ast = decompile_word("test");
    ASSERT_GE(ast.children.size(), 1u);
    auto& loop = ast.children[0];
    EXPECT_EQ(loop.kind, ASTNodeKind::BeginAgain);
    ASSERT_EQ(loop.children.size(), 1u);
    EXPECT_GE(loop.children[0].children.size(), 1u);
}

// --- Nested: do/loop containing if/then ---

TEST_F(DecompilerTest, NestedDoLoopWithIfThen) {
    interp.interpret_line(": test 10 0 do i 5 > if i then loop ;");
    auto ast = decompile_word("test");
    ASSERT_GE(ast.children.size(), 3u);
    auto& loop = ast.children[2];
    EXPECT_EQ(loop.kind, ASTNodeKind::DoLoop);
    // Find the IfThen inside the loop body
    bool found_if = false;
    for (const auto& child : loop.children[0].children) {
        if (child.kind == ASTNodeKind::IfThen) {
            found_if = true;
            ASSERT_EQ(child.children.size(), 1u);
            // then-body should contain DoI
            bool has_i = false;
            for (const auto& c : child.children[0].children) {
                if (c.kind == ASTNodeKind::DoI) has_i = true;
            }
            EXPECT_TRUE(has_i);
        }
    }
    EXPECT_TRUE(found_if);
}

// --- Return stack ---

TEST_F(DecompilerTest, ReturnStack) {
    interp.interpret_line(": test >r r@ r> ;");
    auto ast = decompile_word("test");
    ASSERT_EQ(ast.children.size(), 3u);
    EXPECT_EQ(ast.children[0].kind, ASTNodeKind::ToR);
    EXPECT_EQ(ast.children[1].kind, ASTNodeKind::FetchR);
    EXPECT_EQ(ast.children[2].kind, ASTNodeKind::FromR);
}

// --- Leave ---

TEST_F(DecompilerTest, LeaveInDoLoop) {
    interp.interpret_line(": test 10 0 do i 5 = if leave then loop ;");
    auto ast = decompile_word("test");
    ASSERT_GE(ast.children.size(), 3u);
    auto& loop = ast.children[2];
    EXPECT_EQ(loop.kind, ASTNodeKind::DoLoop);
    // Find Leave inside the if/then inside the loop
    bool found_leave = false;
    std::function<void(const ASTNode&)> search = [&](const ASTNode& n) {
        if (n.kind == ASTNodeKind::Leave) found_leave = true;
        for (const auto& c : n.children) search(c);
    };
    search(loop);
    EXPECT_TRUE(found_leave);
}

// --- Exit ---

TEST_F(DecompilerTest, Exit) {
    interp.interpret_line(": test dup 0> if exit then ;");
    auto ast = decompile_word("test");
    bool found_exit = false;
    std::function<void(const ASTNode&)> search = [&](const ASTNode& n) {
        if (n.kind == ASTNodeKind::Exit) found_exit = true;
        for (const auto& c : n.children) search(c);
    };
    search(ast);
    EXPECT_TRUE(found_exit);
}

// --- Nested do/loop with j ---

TEST_F(DecompilerTest, NestedDoLoopWithJ) {
    interp.interpret_line(": test 3 0 do 5 0 do j i + loop loop ;");
    auto ast = decompile_word("test");
    ASSERT_GE(ast.children.size(), 3u);
    auto& outer = ast.children[2];
    EXPECT_EQ(outer.kind, ASTNodeKind::DoLoop);
    // Find inner DoLoop
    bool found_inner = false;
    bool found_j = false;
    std::function<void(const ASTNode&)> search = [&](const ASTNode& n) {
        if (n.kind == ASTNodeKind::DoLoop && &n != &outer) found_inner = true;
        if (n.kind == ASTNodeKind::DoJ) found_j = true;
        for (const auto& c : n.children) search(c);
    };
    search(outer);
    EXPECT_TRUE(found_inner);
    EXPECT_TRUE(found_j);
}

// --- Debug output ---

TEST_F(DecompilerTest, AstToString) {
    interp.interpret_line(": test dup 0> if 1 + then ;");
    auto ast = decompile_word("test");
    auto s = ast_to_string(ast);
    EXPECT_TRUE(s.find("Sequence") != std::string::npos);
    EXPECT_TRUE(s.find("WordCall dup") != std::string::npos);
    EXPECT_TRUE(s.find("IfThen") != std::string::npos);
}
