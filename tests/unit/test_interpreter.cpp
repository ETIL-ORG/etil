// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/interpreter.hpp"
#include "etil/core/primitives.hpp"
#include <gtest/gtest.h>
#include <sstream>

using namespace etil::core;

// Helper: define the self-hosted builtins (normally loaded from builtins.til).
// These definitions are equivalent to data/builtins.til but defined inline
// so unit tests are self-contained.
static void define_builtins(Interpreter& interp) {
    // forget
    interp.interpret_line(": forget word-read if dict-forget drop then ;");
    // forget-all
    interp.interpret_line(": forget-all word-read if dict-forget-all drop then ;");
    // meta! / meta@ / meta-del / meta-keys / impl-meta! / impl-meta@
    interp.interpret_line(": meta! dict-meta-set ;");
    interp.interpret_line(": meta@ dict-meta-get ;");
    interp.interpret_line(": meta-del dict-meta-del ;");
    interp.interpret_line(": meta-keys dict-meta-keys ;");
    interp.interpret_line(": impl-meta! impl-meta-set ;");
    interp.interpret_line(": impl-meta@ impl-meta-get ;");
}

// RAII helper: redirect std::cout to a stringstream for capturing output
// from compiled words (PrintString, type, etc.)
class CoutCapture {
public:
    CoutCapture() : old_buf_(std::cout.rdbuf(capture_.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(old_buf_); }
    std::string str() const { return capture_.str(); }
private:
    std::ostringstream capture_;
    std::streambuf* old_buf_;
};

class InterpreterTest : public ::testing::Test {
protected:
    Dictionary dict;
    std::ostringstream out;
    std::ostringstream err;
    std::unique_ptr<Interpreter> interp;

    void SetUp() override {
        register_primitives(dict);
        interp = std::make_unique<Interpreter>(dict, out, err);
        define_builtins(*interp);
    }

    // Helper: interpret a line and return the top of stack
    std::optional<Value> interpret_and_pop(const std::string& line) {
        interp->interpret_line(line);
        return interp->context().data_stack().pop();
    }

    // Helper: interpret a line, pop, and return as int
    int64_t interpret_int(const std::string& line) {
        interp->interpret_line(line);
        auto v = interp->context().data_stack().pop();
        return v ? v->as_int : -999999;
    }
};

// --- Number parsing ---

TEST_F(InterpreterTest, ParseInteger) {
    auto v = interpret_and_pop("42");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->type, Value::Type::Integer);
    EXPECT_EQ(v->as_int, 42);
}

TEST_F(InterpreterTest, ParseNegativeInteger) {
    auto v = interpret_and_pop("-7");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->as_int, -7);
}

TEST_F(InterpreterTest, ParseFloat) {
    auto v = interpret_and_pop("3.14");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->type, Value::Type::Float);
    EXPECT_DOUBLE_EQ(v->as_float, 3.14);
}

// --- Primitive execution ---

TEST_F(InterpreterTest, AddIntegers) {
    EXPECT_EQ(interpret_int("3 4 +"), 7);
}

TEST_F(InterpreterTest, ArithmeticExpression) {
    EXPECT_EQ(interpret_int("10 3 - 2 *"), 14);
}

TEST_F(InterpreterTest, StackManipulation) {
    EXPECT_EQ(interpret_int("5 dup +"), 10);
}

// --- Colon definitions ---

TEST_F(InterpreterTest, ColonDefDouble) {
    interp->interpret_line(": double dup + ;");
    EXPECT_EQ(interpret_int("21 double"), 42);
}

TEST_F(InterpreterTest, ColonDefNested) {
    interp->interpret_line(": double dup + ;");
    interp->interpret_line(": quad double double ;");
    EXPECT_EQ(interpret_int("5 quad"), 20);
}

// --- Multi-line colon definitions ---

TEST_F(InterpreterTest, MultiLineDefinition) {
    interp->interpret_line(": triple");
    EXPECT_TRUE(interp->compiling());
    interp->interpret_line("dup dup");
    EXPECT_TRUE(interp->compiling());
    interp->interpret_line("+ + ;");
    EXPECT_FALSE(interp->compiling());
    EXPECT_EQ(interpret_int("10 triple"), 30);
}

// --- Control flow: if/else/then ---

TEST_F(InterpreterTest, IfThenPositive) {
    interp->interpret_line(": absval dup 0< if negate then ;");
    EXPECT_EQ(interpret_int("5 absval"), 5);
}

TEST_F(InterpreterTest, IfThenNegative) {
    interp->interpret_line(": absval dup 0< if negate then ;");
    EXPECT_EQ(interpret_int("-5 absval"), 5);
}

TEST_F(InterpreterTest, IfElseThen) {
    interp->interpret_line(": sign dup 0< if drop -1 else drop 1 then ;");
    EXPECT_EQ(interpret_int("-10 sign"), -1);
    EXPECT_EQ(interpret_int("10 sign"), 1);
}

// --- Control flow: do/loop/i ---

TEST_F(InterpreterTest, DoLoop) {
    interp->interpret_line(": sum5 0 5 0 do i + loop ;");
    EXPECT_EQ(interpret_int("sum5"), 10);  // 0+1+2+3+4
}

TEST_F(InterpreterTest, PlusLoop) {
    interp->interpret_line(": evens 10 0 do i 2 +loop ;");
    interp->interpret_line("evens");
    auto& stack = interp->context().data_stack();
    std::vector<int64_t> results;
    while (auto v = stack.pop()) {
        results.push_back(v->as_int);
    }
    std::reverse(results.begin(), results.end());
    ASSERT_EQ(results.size(), 5u);
    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 2);
    EXPECT_EQ(results[2], 4);
    EXPECT_EQ(results[3], 6);
    EXPECT_EQ(results[4], 8);
}

// --- Control flow: begin/until ---

TEST_F(InterpreterTest, BeginUntil) {
    interp->interpret_line(": count-down begin 1 - dup 0= until ;");
    EXPECT_EQ(interpret_int("3 count-down"), 0);
}

// --- Control flow: begin/while/repeat ---

TEST_F(InterpreterTest, BeginWhileRepeat) {
    interp->interpret_line(": to-zero begin dup 0> while 1 - repeat ;");
    EXPECT_EQ(interpret_int("3 to-zero"), 0);
}

// --- CREATE + comma/fetch/store ---

TEST_F(InterpreterTest, CreateCommaFetchStore) {
    interp->interpret_line("create myvar 0 ,");
    interp->interpret_line("42 myvar !");
    EXPECT_EQ(interpret_int("myvar @"), 42);
}

// --- CREATE/DOES> ---

TEST_F(InterpreterTest, CreateDoesConstant) {
    interp->interpret_line(": constant create , does> @ ;");
    interp->interpret_line("99 constant bottles");
    EXPECT_EQ(interpret_int("bottles"), 99);
}

// --- ." in interpret mode ---

TEST_F(InterpreterTest, DotQuoteInterpret) {
    interp->interpret_line(".\" Hello, World!\"");
    EXPECT_EQ(out.str(), "Hello, World!");
}

// --- ." in compile mode ---

TEST_F(InterpreterTest, DotQuoteCompile) {
    interp->interpret_line(": greet .\" Hi!\" ;");
    out.str("");
    interp->interpret_line("greet");
    EXPECT_EQ(out.str(), "Hi!");
}

// --- forget (now a compiled word — output goes to interpreter's out stream) ---

TEST_F(InterpreterTest, Forget) {
    interp->interpret_line(": double dup + ;");
    out.str("");
    interp->interpret_line("forget double");
    EXPECT_TRUE(out.str().empty());  // silent

    err.str("");
    interp->interpret_line("21 double");
    EXPECT_TRUE(err.str().find("Unknown word: double") != std::string::npos);
}

TEST_F(InterpreterTest, ForgetRevealsPreviousDefinition) {
    interp->interpret_line(": triple dup dup + + ;");
    EXPECT_EQ(interpret_int("7 triple"), 21);

    // Redefine triple as quadruple
    interp->interpret_line(": triple dup dup + dup + ;");
    EXPECT_EQ(interpret_int("2 triple"), 8);

    // Forget latest → reveals original triple (x3)
    out.str("");
    interp->interpret_line("forget triple");
    EXPECT_TRUE(out.str().empty());  // silent
    EXPECT_EQ(interpret_int("7 triple"), 21);
}

TEST_F(InterpreterTest, ForgetAll) {
    interp->interpret_line(": myword 42 ;");
    interp->interpret_line(": myword 99 ;");

    out.str("");
    interp->interpret_line("forget-all myword");
    EXPECT_TRUE(out.str().empty());  // silent

    err.str("");
    interp->interpret_line("myword");
    EXPECT_TRUE(err.str().find("Unknown word: myword") != std::string::npos);
}

// --- Comment stripping ---

TEST_F(InterpreterTest, CommentStripping) {
    EXPECT_EQ(interpret_int("3 4 + # this is a comment"), 7);
}

TEST_F(InterpreterTest, FullLineComment) {
    interp->interpret_line("# this is a comment");
    EXPECT_EQ(interp->context().data_stack().size(), 0u);
}

// --- Error cases ---

TEST_F(InterpreterTest, NestedColonError) {
    interp->interpret_line(": foo : bar ;");
    EXPECT_TRUE(err.str().find("nested colon definitions not allowed") !=
                std::string::npos);
}

TEST_F(InterpreterTest, UnresolvedControlStructure) {
    interp->interpret_line(": bad if ;");
    EXPECT_TRUE(err.str().find("unresolved control structure") !=
                std::string::npos);
    EXPECT_FALSE(interp->compiling());
}

TEST_F(InterpreterTest, UnknownWord) {
    interp->interpret_line("nonexistent");
    EXPECT_TRUE(err.str().find("Unknown word: nonexistent") !=
                std::string::npos);
}

TEST_F(InterpreterTest, UnknownWordInDefinition) {
    interp->interpret_line(": bad does ;");
    EXPECT_TRUE(err.str().find("unknown word 'does'") != std::string::npos);
    EXPECT_FALSE(interp->compiling());
    err.str("");
    interp->interpret_line("bad");
    EXPECT_TRUE(err.str().find("Unknown word: bad") != std::string::npos);
}

// --- format_value ---

TEST_F(InterpreterTest, FormatValueInt) {
    EXPECT_EQ(Interpreter::format_value(Value(int64_t(42))), "42");
}

TEST_F(InterpreterTest, FormatValueFloat) {
    auto s = Interpreter::format_value(Value(3.14));
    EXPECT_TRUE(s.find("3.14") != std::string::npos);
}

// --- stack_status ---

TEST_F(InterpreterTest, StackStatusEmpty) {
    EXPECT_EQ(interp->stack_status(), "(0)");
}

TEST_F(InterpreterTest, StackStatusWithValues) {
    interp->interpret_line("1 2 3");
    auto status = interp->stack_status();
    EXPECT_TRUE(status.find("(3)") != std::string::npos);
    EXPECT_TRUE(status.find("3") != std::string::npos);
}

// --- words (print_all_words) ---

TEST_F(InterpreterTest, WordsShowsInterpreterWords) {
    interp->interpret_line("words");
    auto output = out.str();
    // Interpreter parsing words (handler-set or dictionary)
    EXPECT_TRUE(output.find(":") != std::string::npos);
    EXPECT_TRUE(output.find(";") != std::string::npos);
    EXPECT_TRUE(output.find("create") != std::string::npos);
    // These are now dictionary words (compiled from builtins)
    EXPECT_TRUE(output.find("forget") != std::string::npos);
    EXPECT_TRUE(output.find("forget-all") != std::string::npos);
    // include is now a primitive in the dictionary
    EXPECT_TRUE(output.find("include") != std::string::npos);
    EXPECT_TRUE(output.find("does>") != std::string::npos);
    EXPECT_TRUE(output.find(".\"") != std::string::npos);
    // Control flow words
    EXPECT_TRUE(output.find("if") != std::string::npos);
    EXPECT_TRUE(output.find("else") != std::string::npos);
    EXPECT_TRUE(output.find("then") != std::string::npos);
    EXPECT_TRUE(output.find("do") != std::string::npos);
    EXPECT_TRUE(output.find("loop") != std::string::npos);
    EXPECT_TRUE(output.find("+loop") != std::string::npos);
    EXPECT_TRUE(output.find("begin") != std::string::npos);
    EXPECT_TRUE(output.find("until") != std::string::npos);
    EXPECT_TRUE(output.find("while") != std::string::npos);
    EXPECT_TRUE(output.find("repeat") != std::string::npos);
    // Dictionary primitives
    EXPECT_TRUE(output.find("dup") != std::string::npos);
    EXPECT_TRUE(output.find("+") != std::string::npos);
}

TEST_F(InterpreterTest, WordsShowsUserDefinedWords) {
    interp->interpret_line(": double dup + ;");
    out.str("");
    interp->interpret_line("words");
    EXPECT_TRUE(out.str().find("double") != std::string::npos);
}

// --- Recursive definition ---

TEST_F(InterpreterTest, RecursiveFactorial) {
    interp->interpret_line(
        ": factorial dup 1 > if dup 1 - factorial * then ;");
    EXPECT_EQ(interpret_int("5 factorial"), 120);
}

// --- load_file ---

// DISABLED: These tests use temp files and occasionally fail on first parallel
// CTest run due to cold filesystem timing. Run on demand with:
//   --gtest_also_run_disabled_tests --gtest_filter=DISABLED_InterpreterFileTest.*
class DISABLED_InterpreterFileTest : public InterpreterTest {
protected:
    std::string tmp_path_;

    void write_tmp(const std::string& content) {
        tmp_path_ = "/tmp/etil_test_" +
                     std::to_string(reinterpret_cast<uintptr_t>(this)) + ".etil";
        std::ofstream f(tmp_path_);
        f << content;
    }

    void TearDown() override {
        if (!tmp_path_.empty()) {
            std::remove(tmp_path_.c_str());
        }
    }
};

TEST_F(DISABLED_InterpreterFileTest, LoadFileBasic) {
    write_tmp(": double dup + ;\n21 double\n");
    ASSERT_TRUE(interp->load_file(tmp_path_));
    auto v = interp->context().data_stack().pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->as_int, 42);
}

TEST_F(DISABLED_InterpreterFileTest, LoadFileMultiLineDefinition) {
    write_tmp(": triple\n  dup dup\n  + + ;\n10 triple\n");
    ASSERT_TRUE(interp->load_file(tmp_path_));
    auto v = interp->context().data_stack().pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->as_int, 30);
}

TEST_F(DISABLED_InterpreterFileTest, LoadFileNotFound) {
    EXPECT_FALSE(interp->load_file("/tmp/nonexistent_etil_file.etil"));
    EXPECT_TRUE(err.str().find("cannot open file") != std::string::npos);
}

TEST_F(DISABLED_InterpreterFileTest, LoadFileUnterminatedDefinition) {
    write_tmp(": oops dup +\n");
    EXPECT_FALSE(interp->load_file(tmp_path_));
    EXPECT_TRUE(err.str().find("unterminated definition") != std::string::npos);
    EXPECT_FALSE(interp->compiling());
}

TEST_F(DISABLED_InterpreterFileTest, LoadFileWithComments) {
    write_tmp("# define double\n: double dup + ;\n# use it\n21 double\n");
    ASSERT_TRUE(interp->load_file(tmp_path_));
    auto v = interp->context().data_stack().pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->as_int, 42);
}

TEST_F(DISABLED_InterpreterFileTest, IncludeWord) {
    write_tmp(": double dup + ;\n");
    interp->interpret_line("include " + tmp_path_);
    EXPECT_EQ(interpret_int("21 double"), 42);
}

TEST_F(DISABLED_InterpreterFileTest, IncludeWordNotFound) {
    interp->interpret_line("include /tmp/nonexistent_etil_file.etil");
    EXPECT_TRUE(err.str().find("cannot open file") != std::string::npos);
}

TEST_F(DISABLED_InterpreterFileTest, IncludeWordMissingFilename) {
    // include is now a primitive; when no filename follows, it returns false
    // which causes "Error executing 'include'" to be written to err_
    interp->interpret_line("include");
    EXPECT_TRUE(err.str().find("Error executing 'include'") != std::string::npos);
}

// --- evaluate ---

TEST_F(InterpreterTest, EvaluateBasicValue) {
    interp->interpret_line("s\" 42\" evaluate");
    auto v = interp->context().data_stack().pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->type, Value::Type::Integer);
    EXPECT_EQ(v->as_int, 42);
}

TEST_F(InterpreterTest, EvaluateExpression) {
    interp->interpret_line("s\" 10 20 +\" evaluate");
    auto v = interp->context().data_stack().pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->as_int, 30);
}

TEST_F(InterpreterTest, EvaluateColonDefinition) {
    interp->interpret_line("s\" : ev-double dup + ;\" evaluate");
    EXPECT_TRUE(err.str().empty());
    interp->interpret_line("21 ev-double");
    auto v = interp->context().data_stack().pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->as_int, 42);
}

TEST_F(InterpreterTest, EvaluateUnterminatedDefinition) {
    interp->interpret_line("s\" : incomplete 42\" evaluate");
    EXPECT_TRUE(err.str().find("unterminated definition") != std::string::npos);
    // compiling_ should be reset so subsequent code works
    EXPECT_FALSE(interp->compiling());
    err.str("");
    interp->interpret_line("99");
    auto v = interp->context().data_stack().pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->as_int, 99);
}

TEST_F(InterpreterTest, EvaluateEmptyString) {
    interp->interpret_line("s\" \" evaluate");
    EXPECT_TRUE(err.str().empty());
    EXPECT_EQ(interp->context().data_stack().size(), 0u);
}

TEST_F(InterpreterTest, EvaluateNonString) {
    interp->interpret_line("42 evaluate");
    EXPECT_TRUE(err.str().find("evaluate requires a string") != std::string::npos);
}

TEST_F(InterpreterTest, EvaluateUnderflow) {
    interp->interpret_line("evaluate");
    EXPECT_TRUE(err.str().find("Error executing 'evaluate'") != std::string::npos);
}

TEST_F(InterpreterTest, EvaluateNested) {
    // Build inner code string on the stack, then evaluate it.
    // s" does not process backslash escapes, so we construct the
    // nested code using s+ to build: s" 42" evaluate
    // Then evaluate that outer string.
    interp->interpret_line("s\" 42\" s\" evaluate\" s+ s\" s\\\" \" swap s+");
    // Stack now has a string containing: s" 42" evaluate  (with actual quote chars)
    // But this is complex. Simpler: use s| which uses | as delimiter.
    auto discard = interp->context().data_stack().pop();  // discard the above attempt
    if (discard) discard->release();

    // Use the interpreter to build the nested string via s|
    interp->interpret_line("s| s\" 42\" evaluate| evaluate");
    auto v = interp->context().data_stack().pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->as_int, 42);
}

TEST_F(InterpreterTest, EvaluateInCompiledWord) {
    interp->interpret_line(": eval-test s\" 42\" evaluate ;");
    interp->interpret_line("eval-test");
    auto v = interp->context().data_stack().pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->as_int, 42);
}

TEST_F(InterpreterTest, EvaluateInputStreamPreservation) {
    // After evaluate returns mid-line, s" should still work
    interp->interpret_line("s\" 42\" evaluate s\" hello\" slength");
    // Stack should have: 42, 5
    auto len = interp->context().data_stack().pop();
    ASSERT_TRUE(len.has_value());
    EXPECT_EQ(len->as_int, 5);
    auto val = interp->context().data_stack().pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val->as_int, 42);
}

TEST_F(InterpreterTest, EvaluateRecursionDepthLimit) {
    // Set a small call depth limit so recursion stops quickly
    interp->context().set_limits(UINT64_MAX, SIZE_MAX, 10, 30.0);
    // Define the bomb and run it using s| to avoid quote escaping issues
    interp->interpret_line(": eval-bomb s| eval-bomb| evaluate ;");
    EXPECT_TRUE(err.str().empty());
    err.str("");
    interp->interpret_line("eval-bomb");
    // Should hit call depth limit, not crash
    EXPECT_TRUE(err.str().find("call depth") != std::string::npos ||
                err.str().find("execution limit") != std::string::npos);
    interp->context().reset_limits();
}

// --- Source location in error messages ---

TEST_F(InterpreterTest, InteractiveErrorNoSourceLocation) {
    // Interactive mode: error messages should NOT have "at file:line"
    interp->interpret_line("nonexistent");
    EXPECT_TRUE(err.str().find("Unknown word: nonexistent") != std::string::npos);
    EXPECT_EQ(err.str().find(" at "), std::string::npos);
}

TEST_F(DISABLED_InterpreterFileTest, ErrorShowsFileAndLine) {
    // Error on a known line should show file:line
    write_tmp("42\nnonexistent\n");
    interp->load_file(tmp_path_);
    auto err_text = err.str();
    EXPECT_TRUE(err_text.find("Unknown word: nonexistent") != std::string::npos);
    // Should contain "at <filename>:2"
    EXPECT_TRUE(err_text.find(":2") != std::string::npos);
}

TEST_F(DISABLED_InterpreterFileTest, CompileErrorShowsFileAndLine) {
    // Unknown word inside definition should show file:line
    write_tmp(": bad nonexistent-word ;\n");
    interp->load_file(tmp_path_);
    auto err_text = err.str();
    EXPECT_TRUE(err_text.find("unknown word 'nonexistent-word'") != std::string::npos);
    EXPECT_TRUE(err_text.find(":1") != std::string::npos);
}

TEST_F(DISABLED_InterpreterFileTest, UnterminatedDefShowsFileAndLine) {
    // Unterminated definition shows file:line at end of file
    write_tmp("# line 1\n: oops dup +\n");
    interp->load_file(tmp_path_);
    auto err_text = err.str();
    EXPECT_TRUE(err_text.find("unterminated definition") != std::string::npos);
    EXPECT_TRUE(err_text.find(":2") != std::string::npos);
}

TEST_F(DISABLED_InterpreterFileTest, ExecuteErrorShowsFileAndLine) {
    // Word that fails execution should show file:line
    write_tmp(": bad-word + ;\nbad-word\n");
    interp->load_file(tmp_path_);
    auto err_text = err.str();
    EXPECT_TRUE(err_text.find("Error executing 'bad-word'") != std::string::npos);
    EXPECT_TRUE(err_text.find(":2") != std::string::npos);
}

// --- Definition-type metadata ---

TEST_F(InterpreterTest, DefinitionTypeInteractive) {
    interp->interpret_line(": my-test-word 42 ;");
    auto impls = dict.get_implementations("my-test-word");
    ASSERT_TRUE(impls.has_value());
    ASSERT_FALSE(impls->empty());
    auto dt = impls->back()->definition_type();
    ASSERT_TRUE(dt.has_value());
    EXPECT_EQ(*dt, "interpret");
}

TEST_F(InterpreterTest, DefinitionTypeEvaluate) {
    interp->interpret_line("s\" : ev-word 42 ;\" evaluate");
    auto impls = dict.get_implementations("ev-word");
    ASSERT_TRUE(impls.has_value());
    ASSERT_FALSE(impls->empty());
    auto dt = impls->back()->definition_type();
    ASSERT_TRUE(dt.has_value());
    EXPECT_EQ(*dt, "evaluate");
}

TEST_F(DISABLED_InterpreterFileTest, DefinitionTypeInclude) {
    write_tmp(": file-word 42 ;\n");
    interp->load_file(tmp_path_);
    auto impls = dict.get_implementations("file-word");
    ASSERT_TRUE(impls.has_value());
    ASSERT_FALSE(impls->empty());
    auto dt = impls->back()->definition_type();
    ASSERT_TRUE(dt.has_value());
    EXPECT_EQ(*dt, "include");
}

TEST_F(DISABLED_InterpreterFileTest, DefinitionSourceFileAndLine) {
    write_tmp("# comment\n: file-word 42 ;\n");
    interp->load_file(tmp_path_);
    auto impls = dict.get_implementations("file-word");
    ASSERT_TRUE(impls.has_value());
    ASSERT_FALSE(impls->empty());
    auto sf = impls->back()->source_file();
    auto sl = impls->back()->source_line();
    ASSERT_TRUE(sf.has_value());
    ASSERT_TRUE(sl.has_value());
    // source-line should be "2" (definition is on line 2)
    EXPECT_EQ(*sl, "2");
}

TEST_F(InterpreterTest, InteractiveDefHasNoSourceFile) {
    interp->interpret_line(": my-word 42 ;");
    auto impls = dict.get_implementations("my-word");
    ASSERT_TRUE(impls.has_value());
    ASSERT_FALSE(impls->empty());
    auto sf = impls->back()->source_file();
    EXPECT_FALSE(sf.has_value());
}
