// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/escape_processing.hpp"
#include "etil/core/handler_set.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/metadata_json.hpp"
#include "etil/core/primitives.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <sstream>

using namespace etil::core;

// ============================================================
// InterpretHandlerSetTest
// ============================================================

class InterpretHandlerSetTest : public ::testing::Test {
protected:
    Dictionary dict;
    ExecutionContext ctx{0};
    std::ostringstream out;
    std::ostringstream err;
    bool print_all_words_called = false;

    std::unique_ptr<InterpretHandlerSet> handler;

    void SetUp() override {
        register_primitives(dict);
        ctx.set_dictionary(&dict);
        handler = std::make_unique<InterpretHandlerSet>(
            dict, ctx, out, err,
            [this]() { print_all_words_called = true; });
    }
};

TEST_F(InterpretHandlerSetTest, WordsReturns6Entries) {
    auto w = handler->words();
    EXPECT_EQ(w.size(), 6u);
    std::vector<std::string> expected = {"words", ".\"", "s\"", ".|", "s|", "j|"};
    for (const auto& name : expected) {
        EXPECT_TRUE(std::find(w.begin(), w.end(), name) != w.end())
            << "Missing word: " << name;
    }
}

TEST_F(InterpretHandlerSetTest, DispatchUnknownReturnsNullopt) {
    std::istringstream iss("");
    auto result = handler->dispatch("nonexistent", iss);
    EXPECT_FALSE(result.has_value());
}

TEST_F(InterpretHandlerSetTest, DotQuote) {
    std::istringstream iss(" Hello, World!\"");
    auto result = handler->dispatch(".\"", iss);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    EXPECT_EQ(out.str(), "Hello, World!");
}

TEST_F(InterpretHandlerSetTest, SQuote) {
    std::istringstream iss(" test string\"");
    auto result = handler->dispatch("s\"", iss);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    auto v = ctx.data_stack().pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->type, Value::Type::String);
    auto* hs = v->as_string();
    EXPECT_EQ(hs->view(), "test string");
    hs->release();
}

TEST_F(InterpretHandlerSetTest, WordsCallsCallback) {
    std::istringstream iss("");
    auto result = handler->dispatch("words", iss);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    EXPECT_TRUE(print_all_words_called);
}

TEST_F(InterpretHandlerSetTest, DotPipeBasic) {
    std::istringstream iss(" Hello, World!|");
    auto result = handler->dispatch(".|", iss);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    EXPECT_EQ(out.str(), "Hello, World!");
}

TEST_F(InterpretHandlerSetTest, DotPipeEscapes) {
    std::istringstream iss(" line1\\nline2|");
    auto result = handler->dispatch(".|", iss);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    EXPECT_EQ(out.str(), "line1\nline2");
}

TEST_F(InterpretHandlerSetTest, DotPipeEmbeddedQuotes) {
    std::istringstream iss(" say \"hello\"|");
    auto result = handler->dispatch(".|", iss);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    EXPECT_EQ(out.str(), "say \"hello\"");
}

TEST_F(InterpretHandlerSetTest, SPipeBasic) {
    std::istringstream iss(" test string|");
    auto result = handler->dispatch("s|", iss);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    auto v = ctx.data_stack().pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->type, Value::Type::String);
    auto* hs = v->as_string();
    EXPECT_EQ(hs->view(), "test string");
    hs->release();
}

TEST_F(InterpretHandlerSetTest, SPipeEscapedDelimiter) {
    std::istringstream iss(" pipe\\|here|");
    auto result = handler->dispatch("s|", iss);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    auto v = ctx.data_stack().pop();
    ASSERT_TRUE(v.has_value());
    auto* hs = v->as_string();
    EXPECT_EQ(hs->view(), "pipe|here");
    hs->release();
}

TEST_F(InterpretHandlerSetTest, SPipeHexEscape) {
    std::istringstream iss(" \\%41\\%42|");
    auto result = handler->dispatch("s|", iss);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    auto v = ctx.data_stack().pop();
    ASSERT_TRUE(v.has_value());
    auto* hs = v->as_string();
    EXPECT_EQ(hs->view(), "AB");
    hs->release();
}

// ============================================================
// CompileHandlerSetTest
// ============================================================

class CompileHandlerSetTest : public ::testing::Test {
protected:
    Dictionary dict;
    std::ostringstream err;
    std::string compiling_word_name = "testword";
    std::shared_ptr<ByteCode> bytecode;
    std::vector<size_t> control_stack;
    bool finalize_called = false;
    bool abandon_called = false;

    std::unique_ptr<CompileHandlerSet> handler;

    void SetUp() override {
        register_primitives(dict);
        bytecode = std::make_shared<ByteCode>();
        handler = std::make_unique<CompileHandlerSet>(
            err, compiling_word_name, bytecode, control_stack,
            [this]() { finalize_called = true; },
            [this]() { abandon_called = true; },
            dict);
    }
};

TEST_F(CompileHandlerSetTest, WordsReturns9Entries) {
    auto w = handler->words();
    EXPECT_EQ(w.size(), 9u);
    std::vector<std::string> expected = {";", "does>", ".\"", "s\"", ".|", "s|", "j|", "[']", "recurse"};
    for (const auto& name : expected) {
        EXPECT_TRUE(std::find(w.begin(), w.end(), name) != w.end())
            << "Missing word: " << name;
    }
}

TEST_F(CompileHandlerSetTest, DispatchUnknownReturnsNullopt) {
    std::istringstream iss("");
    auto result = handler->dispatch("nonexistent", iss);
    EXPECT_FALSE(result.has_value());
}

TEST_F(CompileHandlerSetTest, SemicolonFinalizes) {
    std::istringstream iss("");
    auto result = handler->dispatch(";", iss);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    EXPECT_TRUE(finalize_called);
    EXPECT_FALSE(abandon_called);
}

TEST_F(CompileHandlerSetTest, SemicolonUnresolvedControlFlow) {
    control_stack.push_back(0);
    std::istringstream iss("");
    auto result = handler->dispatch(";", iss);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(*result);
    EXPECT_TRUE(err.str().find("unresolved control structure") != std::string::npos);
    EXPECT_TRUE(abandon_called);
    EXPECT_FALSE(finalize_called);
}

TEST_F(CompileHandlerSetTest, Does) {
    Instruction dummy;
    dummy.op = Instruction::Op::PushInt;
    dummy.int_val = 42;
    bytecode->append(std::move(dummy));
    ASSERT_EQ(bytecode->size(), 1u);

    std::istringstream iss("");
    auto result = handler->dispatch("does>", iss);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_EQ(bytecode->size(), 2u);
    const auto& instr = bytecode->instructions()[1];
    EXPECT_EQ(instr.op, Instruction::Op::SetDoes);
    EXPECT_EQ(instr.int_val, 2);
}

TEST_F(CompileHandlerSetTest, DotQuote) {
    std::istringstream iss(" Hello!\"");
    auto result = handler->dispatch(".\"", iss);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_EQ(bytecode->size(), 1u);
    const auto& instr = bytecode->instructions()[0];
    EXPECT_EQ(instr.op, Instruction::Op::PrintString);
    EXPECT_EQ(instr.word_name, "Hello!");
}

TEST_F(CompileHandlerSetTest, SQuote) {
    std::istringstream iss(" test string\"");
    auto result = handler->dispatch("s\"", iss);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_EQ(bytecode->size(), 1u);
    const auto& instr = bytecode->instructions()[0];
    EXPECT_EQ(instr.op, Instruction::Op::PushString);
    EXPECT_EQ(instr.word_name, "test string");
}

TEST_F(CompileHandlerSetTest, DotPipe) {
    std::istringstream iss(" Hello\\n|");
    auto result = handler->dispatch(".|", iss);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_EQ(bytecode->size(), 1u);
    const auto& instr = bytecode->instructions()[0];
    EXPECT_EQ(instr.op, Instruction::Op::PrintString);
    EXPECT_EQ(instr.word_name, "Hello\n");
}

TEST_F(CompileHandlerSetTest, SPipe) {
    std::istringstream iss(" test\\tstring|");
    auto result = handler->dispatch("s|", iss);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_EQ(bytecode->size(), 1u);
    const auto& instr = bytecode->instructions()[0];
    EXPECT_EQ(instr.op, Instruction::Op::PushString);
    EXPECT_EQ(instr.word_name, "test\tstring");
}

TEST_F(CompileHandlerSetTest, SPipeUnterminated) {
    std::istringstream iss(" no closing pipe");
    auto result = handler->dispatch("s|", iss);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(*result);
    EXPECT_TRUE(err.str().find("unterminated string") != std::string::npos);
    EXPECT_TRUE(abandon_called);
}

// ============================================================
// EscapeProcessingTest — direct tests for read_escaped_string
// ============================================================

TEST(EscapeProcessingTest, BasicString) {
    std::istringstream iss(" hello world|");
    std::ostringstream err;
    auto result = etil::core::read_escaped_string(iss, '|', err);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "hello world");
}

TEST(EscapeProcessingTest, NewlineEscape) {
    std::istringstream iss(" line1\\nline2|");
    std::ostringstream err;
    auto result = etil::core::read_escaped_string(iss, '|', err);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "line1\nline2");
}

TEST(EscapeProcessingTest, TabEscape) {
    std::istringstream iss(" col1\\tcol2|");
    std::ostringstream err;
    auto result = etil::core::read_escaped_string(iss, '|', err);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "col1\tcol2");
}

TEST(EscapeProcessingTest, CarriageReturnEscape) {
    std::istringstream iss(" a\\rb|");
    std::ostringstream err;
    auto result = etil::core::read_escaped_string(iss, '|', err);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "a\rb");
}

TEST(EscapeProcessingTest, NullEscape) {
    std::istringstream iss(" a\\0b|");
    std::ostringstream err;
    auto result = etil::core::read_escaped_string(iss, '|', err);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 3u);
    EXPECT_EQ((*result)[0], 'a');
    EXPECT_EQ((*result)[1], '\0');
    EXPECT_EQ((*result)[2], 'b');
}

TEST(EscapeProcessingTest, BellEscape) {
    std::istringstream iss(" \\a|");
    std::ostringstream err;
    auto result = etil::core::read_escaped_string(iss, '|', err);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, std::string(1, '\a'));
}

TEST(EscapeProcessingTest, BackspaceEscape) {
    std::istringstream iss(" \\b|");
    std::ostringstream err;
    auto result = etil::core::read_escaped_string(iss, '|', err);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, std::string(1, '\b'));
}

TEST(EscapeProcessingTest, FormfeedEscape) {
    std::istringstream iss(" \\f|");
    std::ostringstream err;
    auto result = etil::core::read_escaped_string(iss, '|', err);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, std::string(1, '\f'));
}

TEST(EscapeProcessingTest, BackslashEscape) {
    std::istringstream iss(" a\\\\b|");
    std::ostringstream err;
    auto result = etil::core::read_escaped_string(iss, '|', err);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "a\\b");
}

TEST(EscapeProcessingTest, DelimiterEscape) {
    std::istringstream iss(" a\\|b|");
    std::ostringstream err;
    auto result = etil::core::read_escaped_string(iss, '|', err);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "a|b");
}

TEST(EscapeProcessingTest, HexEscape) {
    std::istringstream iss(" \\%41\\%42\\%43|");
    std::ostringstream err;
    auto result = etil::core::read_escaped_string(iss, '|', err);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "ABC");
}

TEST(EscapeProcessingTest, HexEscapeLowercase) {
    std::istringstream iss(" \\%0a|");
    std::ostringstream err;
    auto result = etil::core::read_escaped_string(iss, '|', err);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, std::string(1, '\n'));
}

TEST(EscapeProcessingTest, UnterminatedString) {
    std::istringstream iss(" no closing pipe");
    std::ostringstream err;
    auto result = etil::core::read_escaped_string(iss, '|', err);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(err.str().find("unterminated string") != std::string::npos);
}

TEST(EscapeProcessingTest, UnknownEscapeSequence) {
    std::istringstream iss(" \\q|");
    std::ostringstream err;
    auto result = etil::core::read_escaped_string(iss, '|', err);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(err.str().find("unknown escape") != std::string::npos);
}

TEST(EscapeProcessingTest, InvalidHexDigits) {
    std::istringstream iss(" \\%GZ|");
    std::ostringstream err;
    auto result = etil::core::read_escaped_string(iss, '|', err);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(err.str().find("invalid hex") != std::string::npos);
}

TEST(EscapeProcessingTest, IncompleteHexEscape) {
    std::istringstream iss(" \\%4|");
    std::ostringstream err;
    auto result = etil::core::read_escaped_string(iss, '|', err);
    // The '|' gets consumed as h2, and '4' + '|' are not both hex digits
    // Actually '4' is hex, '|' is not — so "invalid hex" error
    EXPECT_FALSE(result.has_value());
}

TEST(EscapeProcessingTest, BackslashAtEOF) {
    std::istringstream iss(" \\");
    std::ostringstream err;
    auto result = etil::core::read_escaped_string(iss, '|', err);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(err.str().find("backslash") != std::string::npos);
}

TEST(EscapeProcessingTest, LeadingSpaceStripped) {
    std::istringstream iss(" x|");
    std::ostringstream err;
    auto result = etil::core::read_escaped_string(iss, '|', err);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "x");
}

TEST(EscapeProcessingTest, NoLeadingSpace) {
    std::istringstream iss("x|");
    std::ostringstream err;
    auto result = etil::core::read_escaped_string(iss, '|', err);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "x");
}

// ============================================================
// ControlFlowHandlerSetTest
// ============================================================

class ControlFlowHandlerSetTest : public ::testing::Test {
protected:
    std::ostringstream err;
    std::shared_ptr<ByteCode> bytecode;
    std::vector<size_t> control_stack;

    std::unique_ptr<ControlFlowHandlerSet> handler;

    void SetUp() override {
        bytecode = std::make_shared<ByteCode>();
        handler = std::make_unique<ControlFlowHandlerSet>(
            err, bytecode, control_stack);
    }
};

TEST_F(ControlFlowHandlerSetTest, WordsReturns18Entries) {
    auto w = handler->words();
    EXPECT_EQ(w.size(), 18u);
    std::vector<std::string> expected = {
        "if", "else", "then", "do", "loop", "+loop",
        "i", "j", "begin", "until", "while", "repeat",
        ">r", "r>", "r@", "leave", "exit", "again"};
    for (const auto& name : expected) {
        EXPECT_TRUE(std::find(w.begin(), w.end(), name) != w.end())
            << "Missing word: " << name;
    }
}

TEST_F(ControlFlowHandlerSetTest, DispatchUnknownReturnsNullopt) {
    auto result = handler->dispatch("nonexistent");
    EXPECT_FALSE(result.has_value());
}

TEST_F(ControlFlowHandlerSetTest, If) {
    auto result = handler->dispatch("if");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_EQ(bytecode->size(), 1u);
    const auto& instr = bytecode->instructions()[0];
    EXPECT_EQ(instr.op, Instruction::Op::BranchIfFalse);
    EXPECT_EQ(instr.int_val, 0);
    ASSERT_EQ(control_stack.size(), 1u);
    EXPECT_EQ(control_stack[0], 0u);
}

TEST_F(ControlFlowHandlerSetTest, IfThen) {
    handler->dispatch("if");
    Instruction dummy;
    dummy.op = Instruction::Op::PushInt;
    dummy.int_val = 1;
    bytecode->append(std::move(dummy));
    ASSERT_EQ(bytecode->size(), 2u);

    auto result = handler->dispatch("then");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    EXPECT_EQ(bytecode->instructions()[0].int_val, 2);
    EXPECT_TRUE(control_stack.empty());
}

TEST_F(ControlFlowHandlerSetTest, IfElseThen) {
    handler->dispatch("if");
    Instruction body1;
    body1.op = Instruction::Op::PushInt;
    body1.int_val = 1;
    bytecode->append(std::move(body1));

    handler->dispatch("else");
    ASSERT_EQ(bytecode->size(), 3u);
    EXPECT_EQ(bytecode->instructions()[0].op, Instruction::Op::BranchIfFalse);
    EXPECT_EQ(bytecode->instructions()[0].int_val, 3);
    EXPECT_EQ(bytecode->instructions()[2].op, Instruction::Op::Branch);

    Instruction body2;
    body2.op = Instruction::Op::PushInt;
    body2.int_val = 0;
    bytecode->append(std::move(body2));

    auto result = handler->dispatch("then");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    EXPECT_EQ(bytecode->instructions()[2].int_val, 4);
    EXPECT_TRUE(control_stack.empty());
}

TEST_F(ControlFlowHandlerSetTest, ElseWithoutIf) {
    auto result = handler->dispatch("else");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(*result);
    EXPECT_TRUE(err.str().find("else without matching if") != std::string::npos);
}

TEST_F(ControlFlowHandlerSetTest, ThenWithoutIf) {
    auto result = handler->dispatch("then");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(*result);
    EXPECT_TRUE(err.str().find("then without matching if") != std::string::npos);
}

TEST_F(ControlFlowHandlerSetTest, Do) {
    auto result = handler->dispatch("do");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_EQ(bytecode->size(), 1u);
    EXPECT_EQ(bytecode->instructions()[0].op, Instruction::Op::DoSetup);
    ASSERT_EQ(control_stack.size(), 1u);
    EXPECT_EQ(control_stack[0], 1u);
}

TEST_F(ControlFlowHandlerSetTest, DoLoop) {
    handler->dispatch("do");
    Instruction body;
    body.op = Instruction::Op::DoI;
    bytecode->append(std::move(body));

    auto result = handler->dispatch("loop");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_EQ(bytecode->size(), 3u);
    const auto& loop_instr = bytecode->instructions()[2];
    EXPECT_EQ(loop_instr.op, Instruction::Op::DoLoop);
    EXPECT_EQ(loop_instr.int_val, 1);
    EXPECT_TRUE(control_stack.empty());
}

TEST_F(ControlFlowHandlerSetTest, DoPlusLoop) {
    handler->dispatch("do");
    Instruction body;
    body.op = Instruction::Op::DoI;
    bytecode->append(std::move(body));

    auto result = handler->dispatch("+loop");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_EQ(bytecode->size(), 3u);
    const auto& ploop_instr = bytecode->instructions()[2];
    EXPECT_EQ(ploop_instr.op, Instruction::Op::DoPlusLoop);
    EXPECT_EQ(ploop_instr.int_val, 1);
    EXPECT_TRUE(control_stack.empty());
}

TEST_F(ControlFlowHandlerSetTest, LoopWithoutDo) {
    auto result = handler->dispatch("loop");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(*result);
    EXPECT_TRUE(err.str().find("loop without matching do") != std::string::npos);
}

TEST_F(ControlFlowHandlerSetTest, PlusLoopWithoutDo) {
    auto result = handler->dispatch("+loop");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(*result);
    EXPECT_TRUE(err.str().find("+loop without matching do") != std::string::npos);
}

TEST_F(ControlFlowHandlerSetTest, I) {
    auto result = handler->dispatch("i");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_EQ(bytecode->size(), 1u);
    EXPECT_EQ(bytecode->instructions()[0].op, Instruction::Op::DoI);
}

TEST_F(ControlFlowHandlerSetTest, BeginUntil) {
    handler->dispatch("begin");
    ASSERT_EQ(control_stack.size(), 1u);
    EXPECT_EQ(control_stack[0], 0u);

    Instruction body;
    body.op = Instruction::Op::PushInt;
    body.int_val = 1;
    bytecode->append(std::move(body));

    auto result = handler->dispatch("until");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_EQ(bytecode->size(), 2u);
    const auto& branch = bytecode->instructions()[1];
    EXPECT_EQ(branch.op, Instruction::Op::BranchIfFalse);
    EXPECT_EQ(branch.int_val, 0);
    EXPECT_TRUE(control_stack.empty());
}

TEST_F(ControlFlowHandlerSetTest, UntilWithoutBegin) {
    auto result = handler->dispatch("until");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(*result);
    EXPECT_TRUE(err.str().find("until without matching begin") != std::string::npos);
}

TEST_F(ControlFlowHandlerSetTest, BeginWhileRepeat) {
    handler->dispatch("begin");
    ASSERT_EQ(control_stack.size(), 1u);
    EXPECT_EQ(control_stack[0], 0u);

    Instruction cond;
    cond.op = Instruction::Op::PushInt;
    cond.int_val = 1;
    bytecode->append(std::move(cond));

    handler->dispatch("while");
    ASSERT_EQ(bytecode->size(), 2u);
    ASSERT_EQ(control_stack.size(), 2u);
    EXPECT_EQ(control_stack[0], 0u);
    EXPECT_EQ(control_stack[1], 1u);

    Instruction body;
    body.op = Instruction::Op::PushInt;
    body.int_val = 0;
    bytecode->append(std::move(body));

    auto result = handler->dispatch("repeat");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_EQ(bytecode->size(), 4u);
    EXPECT_EQ(bytecode->instructions()[3].op, Instruction::Op::Branch);
    EXPECT_EQ(bytecode->instructions()[3].int_val, 0);
    EXPECT_EQ(bytecode->instructions()[1].int_val, 4);
    EXPECT_TRUE(control_stack.empty());
}

TEST_F(ControlFlowHandlerSetTest, RepeatWithoutBeginWhile) {
    control_stack.push_back(0);
    auto result = handler->dispatch("repeat");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(*result);
    EXPECT_TRUE(err.str().find("repeat without matching begin/while") !=
                std::string::npos);
}

TEST_F(ControlFlowHandlerSetTest, ToR) {
    auto result = handler->dispatch(">r");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_EQ(bytecode->size(), 1u);
    EXPECT_EQ(bytecode->instructions()[0].op, Instruction::Op::ToR);
}

TEST_F(ControlFlowHandlerSetTest, FromR) {
    auto result = handler->dispatch("r>");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_EQ(bytecode->size(), 1u);
    EXPECT_EQ(bytecode->instructions()[0].op, Instruction::Op::FromR);
}

TEST_F(ControlFlowHandlerSetTest, FetchR) {
    auto result = handler->dispatch("r@");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_EQ(bytecode->size(), 1u);
    EXPECT_EQ(bytecode->instructions()[0].op, Instruction::Op::FetchR);
}

TEST_F(ControlFlowHandlerSetTest, J) {
    auto result = handler->dispatch("j");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_EQ(bytecode->size(), 1u);
    EXPECT_EQ(bytecode->instructions()[0].op, Instruction::Op::DoJ);
}

TEST_F(ControlFlowHandlerSetTest, LeaveWithDo) {
    handler->dispatch("do");
    auto result = handler->dispatch("leave");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    // DoSetup at 0, DoLeave at 1
    ASSERT_EQ(bytecode->size(), 2u);
    EXPECT_EQ(bytecode->instructions()[1].op, Instruction::Op::DoLeave);
    EXPECT_EQ(bytecode->instructions()[1].int_val, 0);  // placeholder
}

TEST_F(ControlFlowHandlerSetTest, LeaveWithoutDo) {
    auto result = handler->dispatch("leave");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(*result);
    EXPECT_TRUE(err.str().find("leave without matching do") != std::string::npos);
}

TEST_F(ControlFlowHandlerSetTest, LeaveBackpatchedByLoop) {
    handler->dispatch("do");
    // Emit some body instructions
    Instruction body;
    body.op = Instruction::Op::DoI;
    bytecode->append(std::move(body));
    handler->dispatch("leave");
    // Now emit loop
    handler->dispatch("loop");
    // The leave instruction should be backpatched to point past the loop
    // DoSetup(0), DoI(1), DoLeave(2), DoLoop(3) → leave should point to 4
    ASSERT_EQ(bytecode->size(), 4u);
    EXPECT_EQ(bytecode->instructions()[2].op, Instruction::Op::DoLeave);
    EXPECT_EQ(bytecode->instructions()[2].int_val, 4);
}

TEST_F(ControlFlowHandlerSetTest, Exit) {
    auto result = handler->dispatch("exit");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_EQ(bytecode->size(), 1u);
    EXPECT_EQ(bytecode->instructions()[0].op, Instruction::Op::DoExit);
}

TEST_F(ControlFlowHandlerSetTest, BeginAgain) {
    handler->dispatch("begin");
    ASSERT_EQ(control_stack.size(), 1u);
    EXPECT_EQ(control_stack[0], 0u);

    Instruction body;
    body.op = Instruction::Op::PushInt;
    body.int_val = 1;
    bytecode->append(std::move(body));

    auto result = handler->dispatch("again");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_EQ(bytecode->size(), 2u);
    EXPECT_EQ(bytecode->instructions()[1].op, Instruction::Op::Branch);
    EXPECT_EQ(bytecode->instructions()[1].int_val, 0);
    EXPECT_TRUE(control_stack.empty());
}

TEST_F(ControlFlowHandlerSetTest, AgainWithoutBegin) {
    auto result = handler->dispatch("again");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(*result);
    EXPECT_TRUE(err.str().find("again without matching begin") != std::string::npos);
}

// --- CompileHandlerSet: recurse ---

TEST_F(CompileHandlerSetTest, Recurse) {
    std::istringstream iss("");
    auto result = handler->dispatch("recurse", iss);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_EQ(bytecode->size(), 1u);
    const auto& instr = bytecode->instructions()[0];
    EXPECT_EQ(instr.op, Instruction::Op::Call);
    EXPECT_EQ(instr.word_name, "testword");  // compiling_word_name from SetUp
}
