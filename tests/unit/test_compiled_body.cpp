// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/compiled_body.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/interpreter.hpp"
#include "etil/core/primitives.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <sstream>
#include <thread>

using namespace etil::core;

class CompiledBodyTest : public ::testing::Test {
protected:
    ExecutionContext ctx{0};
    Dictionary dict;

    void SetUp() override {
        register_primitives(dict);
        ctx.set_dictionary(&dict);
    }

    // Helper: register a compiled word
    void compile_word(const std::string& name,
                      std::shared_ptr<ByteCode> code) {
        auto id = Dictionary::next_id();
        auto* impl = new WordImpl(name, id);
        impl->set_bytecode(code);
        impl->set_weight(1.0);
        impl->set_generation(0);
        dict.register_word(name, WordImplPtr(impl));
    }

    // Helper: make a Call instruction
    static Instruction make_call(const std::string& word) {
        Instruction i;
        i.op = Instruction::Op::Call;
        i.word_name = word;
        return i;
    }

    // Helper: make a PushInt instruction
    static Instruction make_push_int(int64_t val) {
        Instruction i;
        i.op = Instruction::Op::PushInt;
        i.int_val = val;
        return i;
    }

    // Helper: make a PushFloat instruction
    static Instruction make_push_float(double val) {
        Instruction i;
        i.op = Instruction::Op::PushFloat;
        i.float_val = val;
        return i;
    }

    // Helper: make a Branch instruction
    static Instruction make_branch(int64_t target) {
        Instruction i;
        i.op = Instruction::Op::Branch;
        i.int_val = target;
        return i;
    }

    // Helper: make a BranchIfFalse instruction
    static Instruction make_branch_if_zero(int64_t target) {
        Instruction i;
        i.op = Instruction::Op::BranchIfFalse;
        i.int_val = target;
        return i;
    }

    // Helper: make a DoSetup instruction
    static Instruction make_do_setup() {
        Instruction i;
        i.op = Instruction::Op::DoSetup;
        return i;
    }

    // Helper: make a DoLoop instruction
    static Instruction make_do_loop(int64_t target) {
        Instruction i;
        i.op = Instruction::Op::DoLoop;
        i.int_val = target;
        return i;
    }

    // Helper: make a DoPlusLoop instruction
    static Instruction make_do_plus_loop(int64_t target) {
        Instruction i;
        i.op = Instruction::Op::DoPlusLoop;
        i.int_val = target;
        return i;
    }

    // Helper: make a DoI instruction
    static Instruction make_do_i() {
        Instruction i;
        i.op = Instruction::Op::DoI;
        return i;
    }

    // Helper: make a PushBool instruction
    static Instruction make_push_bool(bool val) {
        Instruction i;
        i.op = Instruction::Op::PushBool;
        i.int_val = val ? 1 : 0;
        return i;
    }

    // Helper: make a PrintString instruction
    static Instruction make_print_string(const std::string& text) {
        Instruction i;
        i.op = Instruction::Op::PrintString;
        i.word_name = text;
        return i;
    }
};

// --- Basic operations ---

TEST_F(CompiledBodyTest, PushInt) {
    auto code = std::make_shared<ByteCode>();
    code->append(make_push_int(42));
    ASSERT_TRUE(execute_compiled(*code, ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, 42);
}

TEST_F(CompiledBodyTest, PushFloat) {
    auto code = std::make_shared<ByteCode>();
    code->append(make_push_float(3.14));
    ASSERT_TRUE(execute_compiled(*code, ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->as_float, 3.14);
}

TEST_F(CompiledBodyTest, CallPrimitive) {
    // Equivalent to: 3 4 +
    auto code = std::make_shared<ByteCode>();
    code->append(make_push_int(3));
    code->append(make_push_int(4));
    code->append(make_call("+"));
    ASSERT_TRUE(execute_compiled(*code, ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, 7);
}

// --- Colon definitions: : double dup + ; ---

TEST_F(CompiledBodyTest, ColonDefDouble) {
    auto code = std::make_shared<ByteCode>();
    code->append(make_call("dup"));
    code->append(make_call("+"));
    compile_word("double", code);

    // Execute: 21 double
    ctx.data_stack().push(Value(int64_t(21)));
    auto impl = dict.lookup("double");
    ASSERT_TRUE(impl.has_value());
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, 42);
}

// --- Nested calls: : quad double double ; ---

TEST_F(CompiledBodyTest, NestedCalls) {
    auto dbl_code = std::make_shared<ByteCode>();
    dbl_code->append(make_call("dup"));
    dbl_code->append(make_call("+"));
    compile_word("double", dbl_code);

    auto quad_code = std::make_shared<ByteCode>();
    quad_code->append(make_call("double"));
    quad_code->append(make_call("double"));
    compile_word("quad", quad_code);

    ctx.data_stack().push(Value(int64_t(5)));
    auto impl = dict.lookup("quad");
    ASSERT_TRUE(impl.has_value());
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, 20);
}

// --- IF/THEN ---

TEST_F(CompiledBodyTest, IfThenTrue) {
    // : absval dup 0< if negate then ;
    // With positive input, if-body skipped
    auto code = std::make_shared<ByteCode>();
    code->append(make_call("dup"));       // 0
    code->append(make_call("0<"));        // 1
    code->append(make_branch_if_zero(4)); // 2: skip to then
    code->append(make_call("negate"));    // 3: if-body
    // 4: then (implicit, nothing here)

    compile_word("absval", code);

    ctx.data_stack().push(Value(int64_t(5)));
    auto impl = dict.lookup("absval");
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    auto result = ctx.data_stack().pop();
    EXPECT_EQ(result->as_int, 5);
}

TEST_F(CompiledBodyTest, IfThenFalse) {
    // : absval dup 0< if negate then ;
    // With negative input, if-body executes
    auto code = std::make_shared<ByteCode>();
    code->append(make_call("dup"));       // 0
    code->append(make_call("0<"));        // 1
    code->append(make_branch_if_zero(4)); // 2
    code->append(make_call("negate"));    // 3
    compile_word("absval", code);

    ctx.data_stack().push(Value(int64_t(-5)));
    auto impl = dict.lookup("absval");
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    auto result = ctx.data_stack().pop();
    EXPECT_EQ(result->as_int, 5);
}

// --- IF/ELSE/THEN ---

TEST_F(CompiledBodyTest, IfElseThen) {
    // : sign dup 0> if drop 1 else dup 0< if drop -1 else drop 0 then then ;
    // Simplified: dup 0< if drop -1 else drop 1 then
    auto code = std::make_shared<ByteCode>();
    code->append(make_call("dup"));       // 0
    code->append(make_call("0<"));        // 1
    code->append(make_branch_if_zero(5)); // 2: jump to else
    code->append(make_call("drop"));      // 3: if-body
    code->append(make_push_int(-1));      // 4
    code->append(make_branch(8));         // 5: jump past else — WAIT, this is wrong

    // Let me redo the layout:
    // 0: dup
    // 1: 0<
    // 2: BranchIfFalse → 6 (else)
    // 3: drop
    // 4: push -1
    // 5: Branch → 8 (past then)
    // 6: drop
    // 7: push 1
    // 8: (end)

    // Actually need to rebuild from scratch
    auto code2 = std::make_shared<ByteCode>();
    code2->append(make_call("dup"));       // 0
    code2->append(make_call("0<"));        // 1
    code2->append(make_branch_if_zero(6)); // 2: to else-body
    code2->append(make_call("drop"));      // 3: if-body
    code2->append(make_push_int(-1));      // 4
    code2->append(make_branch(8));         // 5: skip else
    code2->append(make_call("drop"));      // 6: else-body
    code2->append(make_push_int(1));       // 7
    // 8: then (past end)
    compile_word("sign", code2);

    // Test negative
    ctx.data_stack().push(Value(int64_t(-10)));
    auto impl = dict.lookup("sign");
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    auto result = ctx.data_stack().pop();
    EXPECT_EQ(result->as_int, -1);

    // Test positive
    ctx.data_stack().push(Value(int64_t(10)));
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    result = ctx.data_stack().pop();
    EXPECT_EQ(result->as_int, 1);
}

// --- DO/LOOP with I ---

TEST_F(CompiledBodyTest, DoLoopSum) {
    // : sum-to  0 swap 0 do i + loop ;
    // But simpler test: 0  5 0 do i + loop  → 0+0+1+2+3+4 = 10
    auto code = std::make_shared<ByteCode>();
    code->append(make_push_int(0));     // 0: accumulator
    code->append(make_push_int(5));     // 1: limit
    code->append(make_push_int(0));     // 2: start index
    code->append(make_do_setup());      // 3
    code->append(make_do_i());          // 4: loop body start - push i
    code->append(make_call("+"));       // 5: add to accumulator
    code->append(make_do_loop(4));      // 6: branch back to 4
    // 7: (after loop)
    compile_word("sum5", code);

    auto impl = dict.lookup("sum5");
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, 10);  // 0+1+2+3+4
}

TEST_F(CompiledBodyTest, DoLoopCount) {
    // 10 0 do i loop → pushes 0 1 2 ... 9 onto stack
    auto code = std::make_shared<ByteCode>();
    code->append(make_push_int(3));   // 0: limit
    code->append(make_push_int(0));   // 1: start
    code->append(make_do_setup());    // 2
    code->append(make_do_i());        // 3: body
    code->append(make_do_loop(3));    // 4: back to 3
    compile_word("count3", code);

    auto impl = dict.lookup("count3");
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    // Stack should have 0 1 2 (top = 2)
    auto v2 = ctx.data_stack().pop();
    auto v1 = ctx.data_stack().pop();
    auto v0 = ctx.data_stack().pop();
    EXPECT_EQ(v2->as_int, 2);
    EXPECT_EQ(v1->as_int, 1);
    EXPECT_EQ(v0->as_int, 0);
}

// --- +LOOP ---

TEST_F(CompiledBodyTest, PlusLoop) {
    // 10 0 do i 2 +loop → pushes 0 2 4 6 8
    auto code = std::make_shared<ByteCode>();
    code->append(make_push_int(10));    // 0
    code->append(make_push_int(0));     // 1
    code->append(make_do_setup());      // 2
    code->append(make_do_i());          // 3: body
    code->append(make_push_int(2));     // 4
    code->append(make_do_plus_loop(3)); // 5: back to 3
    compile_word("evens", code);

    auto impl = dict.lookup("evens");
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    auto v4 = ctx.data_stack().pop();
    auto v3 = ctx.data_stack().pop();
    auto v2 = ctx.data_stack().pop();
    auto v1 = ctx.data_stack().pop();
    auto v0 = ctx.data_stack().pop();
    EXPECT_EQ(v4->as_int, 8);
    EXPECT_EQ(v3->as_int, 6);
    EXPECT_EQ(v2->as_int, 4);
    EXPECT_EQ(v1->as_int, 2);
    EXPECT_EQ(v0->as_int, 0);
}

// --- BEGIN/UNTIL ---

TEST_F(CompiledBodyTest, BeginUntil) {
    // : count-down  begin dup 1 - dup 0= until ;
    // Start with 3: 3 → 2 → 1 → 0 (0= true, stop)
    auto code = std::make_shared<ByteCode>();
    // begin:
    code->append(make_push_int(1));     // 0: begin
    code->append(make_call("-"));       // 1: subtract 1
    code->append(make_call("dup"));     // 2
    code->append(make_call("0="));      // 3
    code->append(make_branch_if_zero(0)); // 4: until — branch back if false (not zero)
    compile_word("count-down", code);

    ctx.data_stack().push(Value(int64_t(3)));
    auto impl = dict.lookup("count-down");
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    auto result = ctx.data_stack().pop();
    EXPECT_EQ(result->as_int, 0);
}

// --- BEGIN/WHILE/REPEAT ---

TEST_F(CompiledBodyTest, BeginWhileRepeat) {
    // : countdown  begin dup while dup 1 - repeat drop ;
    // Actually let's test: subtract 1 while > 0, leave final value
    // begin dup 0> while 1 - repeat
    // Input 3: 3>0 → 2, 2>0 → 1, 1>0 → 0, 0>0 = false → exit with 0
    auto code = std::make_shared<ByteCode>();
    // 0: begin
    code->append(make_call("dup"));         // 0
    code->append(make_call("0>"));          // 1
    code->append(make_branch_if_zero(6));   // 2: while — exit if false
    code->append(make_push_int(1));         // 3
    code->append(make_call("-"));           // 4
    code->append(make_branch(0));           // 5: repeat — back to begin
    // 6: (exit)
    compile_word("to-zero", code);

    ctx.data_stack().push(Value(int64_t(3)));
    auto impl = dict.lookup("to-zero");
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    auto result = ctx.data_stack().pop();
    EXPECT_EQ(result->as_int, 0);
}

// --- PrintString ---

TEST_F(CompiledBodyTest, PrintString) {
    auto code = std::make_shared<ByteCode>();
    code->append(make_print_string("Hello"));
    std::ostringstream oss;
    ctx.set_out(&oss);
    ASSERT_TRUE(execute_compiled(*code, ctx));
    ctx.set_out(&std::cout);
    EXPECT_EQ(oss.str(), "Hello");
}

// --- Nested DO loops ---

TEST_F(CompiledBodyTest, NestedDoLoops) {
    // Outer: 2 0 do  inner: 3 0 do i loop  loop
    // Inner loop runs 3 times per outer iteration = 6 i-values total
    // But i returns inner loop index, so: 0,1,2, 0,1,2
    auto code = std::make_shared<ByteCode>();
    code->append(make_push_int(2));      // 0: outer limit
    code->append(make_push_int(0));      // 1: outer start
    code->append(make_do_setup());       // 2: outer do
    code->append(make_push_int(3));      // 3: inner limit
    code->append(make_push_int(0));      // 4: inner start
    code->append(make_do_setup());       // 5: inner do
    code->append(make_do_i());           // 6: push inner i
    code->append(make_do_loop(6));       // 7: inner loop
    code->append(make_do_loop(3));       // 8: outer loop
    compile_word("nested", code);

    auto impl = dict.lookup("nested");
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    // Stack: 0 1 2 0 1 2 (top = 2)
    std::vector<int64_t> results;
    while (auto v = ctx.data_stack().pop()) {
        results.push_back(v->as_int);
    }
    std::reverse(results.begin(), results.end());
    ASSERT_EQ(results.size(), 6u);
    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 1);
    EXPECT_EQ(results[2], 2);
    EXPECT_EQ(results[3], 0);
    EXPECT_EQ(results[4], 1);
    EXPECT_EQ(results[5], 2);
}

// --- CREATE with data field ---

TEST_F(CompiledBodyTest, CreateAndDataField) {
    // Simulate: create myvar 0 ,
    // myvar should push DataRef to data_field_[0]
    auto code = std::make_shared<ByteCode>();
    Instruction push_ptr;
    push_ptr.op = Instruction::Op::PushDataPtr;
    code->append(std::move(push_ptr));
    code->data_field().push_back(Value(int64_t(0)));

    auto id = Dictionary::next_id();
    auto* impl = new WordImpl("myvar", id);
    impl->set_bytecode(code);
    impl->set_weight(1.0);
    impl->set_generation(0);
    dict.register_word("myvar", WordImplPtr(impl));
    ctx.set_last_created(impl);

    // Execute myvar — should push DataRef
    ASSERT_TRUE(execute_compiled(*impl->bytecode(), ctx));
    auto addr = ctx.data_stack().pop();
    ASSERT_TRUE(addr.has_value());
    EXPECT_EQ(addr->type, Value::Type::DataRef);

    // Resolve through registry to verify it points to the data field
    auto reg_idx = dataref_index(*addr);
    auto offset = dataref_offset(*addr);
    EXPECT_EQ(offset, 0u);
    auto* field = ctx.data_field_registry().resolve(reg_idx);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ((*field)[0].as_int, 0);

    // Store 42 via prim_store
    ctx.data_stack().push(Value(int64_t(42)));
    ctx.data_stack().push(*addr);
    ASSERT_TRUE(prim_store(ctx));
    EXPECT_EQ(impl->bytecode()->data_field()[0].as_int, 42);
}

// --- Memory primitives: , @ ! ---

TEST_F(CompiledBodyTest, CommaFetchStore) {
    // Create a word with empty data field
    auto code = std::make_shared<ByteCode>();
    Instruction push_ptr;
    push_ptr.op = Instruction::Op::PushDataPtr;
    code->append(std::move(push_ptr));

    auto id = Dictionary::next_id();
    auto* impl = new WordImpl("myvar", id);
    impl->set_bytecode(code);
    impl->set_weight(1.0);
    impl->set_generation(0);
    dict.register_word("myvar", WordImplPtr(impl));
    ctx.set_last_created(impl);

    // , (comma): append 99 to data_field
    ctx.data_stack().push(Value(int64_t(99)));
    ASSERT_TRUE(prim_comma(ctx));
    ASSERT_EQ(impl->bytecode()->data_field().size(), 1u);
    EXPECT_EQ(impl->bytecode()->data_field()[0].as_int, 99);

    // Execute myvar to get DataRef
    ASSERT_TRUE(execute_compiled(*impl->bytecode(), ctx));
    // Stack has DataRef to data_field[0]

    // @ (fetch): read the value
    ASSERT_TRUE(prim_fetch(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, 99);

    // ! (store): write 42 to myvar
    ctx.data_stack().push(Value(int64_t(42)));
    ASSERT_TRUE(execute_compiled(*impl->bytecode(), ctx)); // push DataRef
    ASSERT_TRUE(prim_store(ctx));
    EXPECT_EQ(impl->bytecode()->data_field()[0].as_int, 42);
}

// --- Allot ---

TEST_F(CompiledBodyTest, Allot) {
    auto code = std::make_shared<ByteCode>();
    Instruction push_ptr;
    push_ptr.op = Instruction::Op::PushDataPtr;
    code->append(std::move(push_ptr));

    auto id = Dictionary::next_id();
    auto* impl = new WordImpl("buf", id);
    impl->set_bytecode(code);
    impl->set_weight(1.0);
    impl->set_generation(0);
    dict.register_word("buf", WordImplPtr(impl));
    ctx.set_last_created(impl);

    // allot 10 cells
    ctx.data_stack().push(Value(int64_t(10)));
    ASSERT_TRUE(prim_allot(ctx));
    EXPECT_EQ(impl->bytecode()->data_field().size(), 10u);
}

// --- Error: unknown word in compiled body ---

TEST_F(CompiledBodyTest, UnknownWordError) {
    auto code = std::make_shared<ByteCode>();
    code->append(make_call("nonexistent"));
    EXPECT_FALSE(execute_compiled(*code, ctx));
}

// --- Recursive definition ---

TEST_F(CompiledBodyTest, RecursiveFactorial) {
    // : factorial dup 1 > if dup 1 - factorial * then ;
    // 0: dup
    // 1: push 1
    // 2: >
    // 3: BranchIfFalse → 8 (then)
    // 4: dup
    // 5: push 1
    // 6: -
    // 7: call factorial
    // 8: *        -- WAIT, * should be inside the if
    // Let me think again:
    // : factorial dup 1 > if dup 1 - factorial * then ;
    // This means: if n > 1, compute n * factorial(n-1)
    // Layout:
    // 0: dup        ( n n )
    // 1: push 1     ( n n 1 )
    // 2: call >     ( n flag )
    // 3: BranchIfFalse → 8  (skip if-body)
    // 4: dup        ( n n )
    // 5: push 1     ( n n 1 )
    // 6: call -     ( n n-1 )
    // 7: call factorial  ( n factorial(n-1) )
    // 8: call *     -- NO, this is outside the if!

    // Actually in FORTH: : factorial dup 1 > if dup 1 - factorial * then ;
    // The * is INSIDE the if/then block:
    // 0: dup
    // 1: push 1
    // 2: call >
    // 3: BranchIfFalse → 9
    // 4: dup
    // 5: push 1
    // 6: call -
    // 7: call factorial
    // 8: call *
    // 9: (end/then)

    auto code = std::make_shared<ByteCode>();
    code->append(make_call("dup"));       // 0
    code->append(make_push_int(1));       // 1
    code->append(make_call(">"));         // 2
    code->append(make_branch_if_zero(9)); // 3
    code->append(make_call("dup"));       // 4
    code->append(make_push_int(1));       // 5
    code->append(make_call("-"));         // 6
    code->append(make_call("factorial")); // 7
    code->append(make_call("*"));         // 8
    // 9: then
    compile_word("factorial", code);

    ctx.data_stack().push(Value(int64_t(5)));
    auto impl = dict.lookup("factorial");
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, 120);
}

// --- Empty compiled word ---

TEST_F(CompiledBodyTest, EmptyWord) {
    auto code = std::make_shared<ByteCode>();
    compile_word("noop", code);
    auto impl = dict.lookup("noop");
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    EXPECT_EQ(ctx.data_stack().size(), 0u);
}

// --- DataRef bounds checking ---

TEST_F(CompiledBodyTest, FetchOutOfBounds) {
    // Create a word with empty data field (no comma'd values)
    auto code = std::make_shared<ByteCode>();
    Instruction push_ptr;
    push_ptr.op = Instruction::Op::PushDataPtr;
    code->append(std::move(push_ptr));
    // data_field is empty — offset 0 should be out of bounds

    auto id = Dictionary::next_id();
    auto* impl = new WordImpl("empty_var", id);
    impl->set_bytecode(code);
    impl->set_weight(1.0);
    impl->set_generation(0);
    dict.register_word("empty_var", WordImplPtr(impl));

    // Execute to get DataRef with offset 0
    ASSERT_TRUE(execute_compiled(*impl->bytecode(), ctx));
    // @ should fail because data_field is empty
    EXPECT_FALSE(prim_fetch(ctx));
}

TEST_F(CompiledBodyTest, InvalidatedDataRef) {
    // Create a ByteCode, register it, get a DataRef, then destroy the ByteCode
    Value saved_ref;
    {
        auto code = std::make_shared<ByteCode>();
        Instruction push_ptr;
        push_ptr.op = Instruction::Op::PushDataPtr;
        code->append(std::move(push_ptr));
        code->data_field().push_back(Value(int64_t(42)));

        ASSERT_TRUE(execute_compiled(*code, ctx));
        auto addr = ctx.data_stack().pop();
        ASSERT_TRUE(addr.has_value());
        EXPECT_EQ(addr->type, Value::Type::DataRef);
        saved_ref = *addr;
        // code goes out of scope here — destructor invalidates registry entry
    }

    // Now try to @ the stale DataRef — should fail gracefully
    ctx.data_stack().push(saved_ref);
    EXPECT_FALSE(prim_fetch(ctx));
}

TEST_F(CompiledBodyTest, SysDatafields) {
    // Create a word with data field to populate the registry
    auto code = std::make_shared<ByteCode>();
    Instruction push_ptr;
    push_ptr.op = Instruction::Op::PushDataPtr;
    code->append(std::move(push_ptr));
    code->data_field().push_back(Value(int64_t(10)));
    code->data_field().push_back(Value(int64_t(20)));

    auto id = Dictionary::next_id();
    auto* impl = new WordImpl("testvar", id);
    impl->set_bytecode(code);
    impl->set_weight(1.0);
    impl->set_generation(0);
    dict.register_word("testvar", WordImplPtr(impl));

    // Execute to trigger lazy registration
    ASSERT_TRUE(execute_compiled(*impl->bytecode(), ctx));
    ctx.data_stack().pop(); // discard DataRef

    // Run sys-datafields and check output
    std::ostringstream oss;
    ctx.set_out(&oss);
    ASSERT_TRUE(prim_sys_datafields(ctx));
    ctx.set_out(&std::cout);

    std::string output = oss.str();
    EXPECT_TRUE(output.find("1 entries") != std::string::npos);
    EXPECT_TRUE(output.find("1 live") != std::string::npos);
    EXPECT_TRUE(output.find("2 cells") != std::string::npos);
}

// --- Execution limits ---

TEST_F(CompiledBodyTest, InstructionBudgetTermination) {
    // begin false until — infinite loop, but budget should terminate it
    auto code = std::make_shared<ByteCode>();
    code->append(make_push_bool(false));    // 0: push false (Boolean)
    code->append(make_branch_if_zero(0));   // 1: until — branch back if false
    compile_word("inf", code);

    std::ostringstream oss_err;
    ctx.set_err(&oss_err);
    ctx.set_limits(1000, SIZE_MAX, SIZE_MAX, 60.0);  // 1000 instruction budget

    auto impl = dict.lookup("inf");
    ASSERT_TRUE(impl.has_value());
    EXPECT_FALSE(execute_compiled(*(*impl)->bytecode(), ctx));
    EXPECT_TRUE(oss_err.str().find("execution limit reached") != std::string::npos);
    EXPECT_LE(ctx.instructions_executed(), 1001u);

    ctx.reset_limits();
    ctx.set_err(&std::cerr);
}

TEST_F(CompiledBodyTest, CallDepthTermination) {
    // : recurse-forever recurse-forever ;
    auto code = std::make_shared<ByteCode>();
    code->append(make_call("recurse-forever"));
    compile_word("recurse-forever", code);

    std::ostringstream oss_err;
    ctx.set_err(&oss_err);
    ctx.set_limits(UINT64_MAX, SIZE_MAX, 10, 60.0);  // max 10 call depth

    auto impl = dict.lookup("recurse-forever");
    ASSERT_TRUE(impl.has_value());
    EXPECT_FALSE(execute_compiled(*(*impl)->bytecode(), ctx));
    EXPECT_TRUE(oss_err.str().find("call depth exceeded") != std::string::npos);

    ctx.reset_limits();
    ctx.set_err(&std::cerr);
}

TEST_F(CompiledBodyTest, ZeroPlusLoopIncrement) {
    // 10 0 do 0 +loop — zero increment should error, not infinite loop
    auto code = std::make_shared<ByteCode>();
    code->append(make_push_int(10));        // 0: limit
    code->append(make_push_int(0));         // 1: start
    code->append(make_do_setup());          // 2
    code->append(make_do_i());              // 3: body
    code->append(make_push_int(0));         // 4: zero increment
    code->append(make_do_plus_loop(3));     // 5: +loop back to 3

    std::ostringstream oss_err;
    ctx.set_err(&oss_err);

    EXPECT_FALSE(execute_compiled(*code, ctx));
    EXPECT_TRUE(oss_err.str().find("+LOOP increment is zero") != std::string::npos);

    ctx.set_err(&std::cerr);
}

TEST_F(CompiledBodyTest, DeadlineTermination) {
    // begin false until — infinite loop, but deadline should terminate it
    auto code = std::make_shared<ByteCode>();
    code->append(make_push_bool(false));    // 0: push false (Boolean)
    code->append(make_branch_if_zero(0));   // 1: until — branch back if false
    compile_word("inf2", code);

    std::ostringstream oss_err;
    ctx.set_err(&oss_err);
    // Very short deadline — 10ms
    ctx.set_limits(UINT64_MAX, SIZE_MAX, SIZE_MAX, 0.01);

    auto impl = dict.lookup("inf2");
    ASSERT_TRUE(impl.has_value());
    EXPECT_FALSE(execute_compiled(*(*impl)->bytecode(), ctx));
    EXPECT_TRUE(oss_err.str().find("execution limit reached") != std::string::npos);

    ctx.reset_limits();
    ctx.set_err(&std::cerr);
}

TEST_F(CompiledBodyTest, CancellationFlag) {
    // begin false until — infinite loop, cancelled externally
    auto code = std::make_shared<ByteCode>();
    code->append(make_push_bool(false));    // 0: push false (Boolean)
    code->append(make_branch_if_zero(0));   // 1: until
    compile_word("inf3", code);

    std::ostringstream oss_err;
    ctx.set_err(&oss_err);
    ctx.set_limits(UINT64_MAX, SIZE_MAX, SIZE_MAX, 60.0);
    ctx.cancel();  // Set cancelled before running

    auto impl = dict.lookup("inf3");
    ASSERT_TRUE(impl.has_value());
    EXPECT_FALSE(execute_compiled(*(*impl)->bytecode(), ctx));

    ctx.reset_limits();
    ctx.set_err(&std::cerr);
}

TEST_F(CompiledBodyTest, NormalExecutionWithLimits) {
    // Normal code should work fine with generous limits
    auto code = std::make_shared<ByteCode>();
    code->append(make_push_int(21));
    code->append(make_call("dup"));
    code->append(make_call("+"));
    compile_word("dbl-limited", code);

    ctx.set_limits(1'000'000, 100'000, 1'000, 30.0);

    auto impl = dict.lookup("dbl-limited");
    ASSERT_TRUE(impl.has_value());
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, 42);

    ctx.reset_limits();
}

// --- Return stack: >r / r> / r@ ---

TEST_F(CompiledBodyTest, ToRFromR) {
    // : test  >r 1 r> + ;
    // Move 10 to return stack, push 1, move 10 back, add = 11
    auto code = std::make_shared<ByteCode>();
    Instruction tor; tor.op = Instruction::Op::ToR;
    code->append(std::move(tor));
    code->append(make_push_int(1));
    Instruction fromr; fromr.op = Instruction::Op::FromR;
    code->append(std::move(fromr));
    code->append(make_call("+"));
    compile_word("test-tor", code);

    ctx.data_stack().push(Value(int64_t(10)));
    auto impl = dict.lookup("test-tor");
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, 11);
}

TEST_F(CompiledBodyTest, FetchR) {
    // >r r@ r> + — copy return stack top, then pop it, add both
    auto code = std::make_shared<ByteCode>();
    Instruction tor; tor.op = Instruction::Op::ToR;
    code->append(std::move(tor));
    Instruction fetchr; fetchr.op = Instruction::Op::FetchR;
    code->append(std::move(fetchr));
    Instruction fromr; fromr.op = Instruction::Op::FromR;
    code->append(std::move(fromr));
    code->append(make_call("+"));
    compile_word("test-fetchr", code);

    ctx.data_stack().push(Value(int64_t(21)));
    auto impl = dict.lookup("test-fetchr");
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, 42);  // 21 + 21
}

TEST_F(CompiledBodyTest, FromRUnderflow) {
    auto code = std::make_shared<ByteCode>();
    Instruction fromr; fromr.op = Instruction::Op::FromR;
    code->append(std::move(fromr));

    std::ostringstream oss_err;
    ctx.set_err(&oss_err);
    EXPECT_FALSE(execute_compiled(*code, ctx));
    EXPECT_TRUE(oss_err.str().find("return stack underflow") != std::string::npos);
    ctx.set_err(&std::cerr);
}

// --- J: outer loop index ---

TEST_F(CompiledBodyTest, DoJ) {
    // 3 0 do  2 0 do  j i  loop  loop
    // Should push: outer=0,inner=0, outer=0,inner=1, outer=1,inner=0, outer=1,inner=1, outer=2,...
    auto code = std::make_shared<ByteCode>();
    code->append(make_push_int(3));      // 0: outer limit
    code->append(make_push_int(0));      // 1: outer start
    code->append(make_do_setup());       // 2: outer do
    code->append(make_push_int(2));      // 3: inner limit
    code->append(make_push_int(0));      // 4: inner start
    code->append(make_do_setup());       // 5: inner do
    Instruction doj; doj.op = Instruction::Op::DoJ;
    code->append(std::move(doj));        // 6: push outer index (j)
    code->append(make_do_i());           // 7: push inner index (i)
    code->append(make_do_loop(6));       // 8: inner loop
    code->append(make_do_loop(3));       // 9: outer loop
    compile_word("test-j", code);

    auto impl = dict.lookup("test-j");
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    // Stack: j0 i0 j0 i1  j1 i0 j1 i1  j2 i0 j2 i1 = 12 values
    std::vector<int64_t> results;
    while (auto v = ctx.data_stack().pop()) {
        results.push_back(v->as_int);
    }
    std::reverse(results.begin(), results.end());
    ASSERT_EQ(results.size(), 12u);
    // j=0,i=0  j=0,i=1  j=1,i=0  j=1,i=1  j=2,i=0  j=2,i=1
    EXPECT_EQ(results[0], 0); EXPECT_EQ(results[1], 0);   // j=0 i=0
    EXPECT_EQ(results[2], 0); EXPECT_EQ(results[3], 1);   // j=0 i=1
    EXPECT_EQ(results[4], 1); EXPECT_EQ(results[5], 0);   // j=1 i=0
    EXPECT_EQ(results[6], 1); EXPECT_EQ(results[7], 1);   // j=1 i=1
    EXPECT_EQ(results[8], 2); EXPECT_EQ(results[9], 0);   // j=2 i=0
    EXPECT_EQ(results[10], 2); EXPECT_EQ(results[11], 1); // j=2 i=1
}

TEST_F(CompiledBodyTest, DoJWithoutNestedLoop) {
    // j outside nested loop should fail
    auto code = std::make_shared<ByteCode>();
    code->append(make_push_int(3));
    code->append(make_push_int(0));
    code->append(make_do_setup());
    Instruction doj; doj.op = Instruction::Op::DoJ;
    code->append(std::move(doj));
    code->append(make_do_loop(3));

    std::ostringstream oss_err;
    ctx.set_err(&oss_err);
    EXPECT_FALSE(execute_compiled(*code, ctx));
    EXPECT_TRUE(oss_err.str().find("j requires nested DO loop") != std::string::npos);
    ctx.set_err(&std::cerr);
}

// --- LEAVE ---

TEST_F(CompiledBodyTest, DoLeave) {
    // : test  0  10 0 do  i 5 = if leave then  i +  loop ;
    // Sum 0+1+2+3+4 = 10, then leave at i=5
    auto code = std::make_shared<ByteCode>();
    code->append(make_push_int(0));           // 0: accumulator
    code->append(make_push_int(10));          // 1: limit
    code->append(make_push_int(0));           // 2: start
    code->append(make_do_setup());            // 3: do
    code->append(make_do_i());                // 4: i
    code->append(make_push_int(5));           // 5: 5
    code->append(make_call("="));             // 6: =
    code->append(make_branch_if_zero(10));    // 7: if → skip to then
    Instruction leave; leave.op = Instruction::Op::DoLeave;
    leave.int_val = 13;                       // past the loop instruction
    code->append(std::move(leave));           // 8: leave
    code->append(make_branch(10));            // 9: skip then (would be else, but just skip)
    // 10: then
    code->append(make_do_i());                // 10: i
    code->append(make_call("+"));             // 11: +
    code->append(make_do_loop(4));            // 12: loop → back to 4
    // 13: after loop
    compile_word("test-leave", code);

    auto impl = dict.lookup("test-leave");
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, 10);  // 0+1+2+3+4
}

// --- EXIT ---

TEST_F(CompiledBodyTest, DoExit) {
    // : test  42 exit 99 ;
    // Should return after pushing 42, never push 99
    auto code = std::make_shared<ByteCode>();
    code->append(make_push_int(42));
    Instruction exit_instr; exit_instr.op = Instruction::Op::DoExit;
    code->append(std::move(exit_instr));
    code->append(make_push_int(99));
    compile_word("test-exit", code);

    auto impl = dict.lookup("test-exit");
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    EXPECT_EQ(ctx.data_stack().size(), 1u);
    auto result = ctx.data_stack().pop();
    EXPECT_EQ(result->as_int, 42);
}

// --- AGAIN (unconditional branch back to begin) ---

TEST_F(CompiledBodyTest, BeginAgain) {
    // : test  0  begin dup 5 = if exit then 1 + again ;
    // Count up from 0, exit when reaching 5
    auto code = std::make_shared<ByteCode>();
    code->append(make_push_int(0));           // 0: initial value
    // begin:
    code->append(make_call("dup"));           // 1
    code->append(make_push_int(5));           // 2
    code->append(make_call("="));             // 3
    code->append(make_branch_if_zero(7));     // 4: if false, skip exit
    Instruction exit_instr; exit_instr.op = Instruction::Op::DoExit;
    code->append(std::move(exit_instr));      // 5: exit
    code->append(make_branch(7));             // 6: skip (unreachable but structurally correct)
    // then:
    code->append(make_push_int(1));           // 7
    code->append(make_call("+"));             // 8
    code->append(make_branch(1));             // 9: again → back to begin (offset 1)
    compile_word("test-again", code);

    auto impl = dict.lookup("test-again");
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, 5);
}

// --- RECURSE (via interpreter) ---

TEST_F(CompiledBodyTest, Recurse) {
    // : factorial  dup 1 > if dup 1 - factorial * then ;
    // This test uses direct Call instruction (same as recurse emits)
    // The compile handler test in test_handler_sets covers the handler itself
    auto code = std::make_shared<ByteCode>();
    code->append(make_call("dup"));       // 0
    code->append(make_push_int(1));       // 1
    code->append(make_call(">"));         // 2
    code->append(make_branch_if_zero(9)); // 3
    code->append(make_call("dup"));       // 4
    code->append(make_push_int(1));       // 5
    code->append(make_call("-"));         // 6
    code->append(make_call("fact-r"));    // 7: recursive call
    code->append(make_call("*"));         // 8
    compile_word("fact-r", code);

    ctx.data_stack().push(Value(int64_t(6)));
    auto impl = dict.lookup("fact-r");
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, 720);
}

// --- Return stack with DO loop interaction ---

TEST_F(CompiledBodyTest, ToRFromRInDoLoop) {
    // : test  0  3 0 do  i >r r> +  loop ;
    // Balance >r/r> within each iteration (>r values sit on top of DO params)
    // 0 + 0 + 1 + 2 = 3
    auto code = std::make_shared<ByteCode>();
    code->append(make_push_int(0));           // 0: accumulator
    code->append(make_push_int(3));           // 1: limit
    code->append(make_push_int(0));           // 2: start
    code->append(make_do_setup());            // 3: do
    code->append(make_do_i());                // 4: i
    Instruction tor; tor.op = Instruction::Op::ToR;
    code->append(std::move(tor));             // 5: >r (push i to rstack)
    Instruction fromr; fromr.op = Instruction::Op::FromR;
    code->append(std::move(fromr));           // 6: r> (pop i back)
    code->append(make_call("+"));             // 7: + (add to accumulator)
    code->append(make_do_loop(4));            // 8: loop → back to 4
    compile_word("test-tor-loop", code);

    auto impl = dict.lookup("test-tor-loop");
    ASSERT_TRUE(execute_compiled(*(*impl)->bytecode(), ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, 3);  // 0+1+2
}

// --- immediate ---

TEST_F(CompiledBodyTest, ImmediateExecutesDuringCompilation) {
    std::ostringstream out, err;
    Interpreter interp(dict, out, err);

    // Define a word, mark it immediate — it should execute during compilation
    interp.interpret_line(": emit-42 42 . ;");
    interp.interpret_line("immediate");

    out.str("");  // Clear output from definition
    // When emit-42 appears in a definition, it should execute (print 42)
    interp.interpret_line(": test emit-42 ;");
    EXPECT_NE(out.str().find("42"), std::string::npos);

    // The compiled word "test" should be empty (no Call to emit-42)
    auto lookup = dict.lookup("test");
    ASSERT_TRUE(lookup);
    auto bc = (*lookup)->bytecode();
    ASSERT_TRUE(bc);
    EXPECT_EQ(bc->instructions().size(), 0u);
    interp.shutdown();
}

TEST_F(CompiledBodyTest, ImmediateNoLastCreated) {
    std::ostringstream out, err;
    Interpreter interp(dict, out, err);

    // Calling immediate without defining a word first should fail
    interp.interpret_line("immediate");
    EXPECT_NE(err.str().find("no word defined yet"), std::string::npos);
    interp.shutdown();
}

TEST_F(CompiledBodyTest, NonImmediateStillCompiles) {
    std::ostringstream out, err;
    Interpreter interp(dict, out, err);

    // Non-immediate word should be compiled as usual
    interp.interpret_line(": my-add + ;");
    interp.interpret_line(": test 3 4 my-add ;");

    auto lookup = dict.lookup("test");
    ASSERT_TRUE(lookup);
    auto bc = (*lookup)->bytecode();
    ASSERT_TRUE(bc);
    // Should have 3 instructions: PushInt(3), PushInt(4), Call(my-add)
    EXPECT_EQ(bc->instructions().size(), 3u);
    EXPECT_EQ(bc->instructions()[2].op, Instruction::Op::Call);
    interp.shutdown();
}

// --- xt-body ---

TEST_F(CompiledBodyTest, XtBodyReturnsDataRef) {
    std::ostringstream out, err;
    Interpreter interp(dict, out, err);

    // Create a word with data field
    interp.interpret_line("create mydata 99 , 100 ,");

    // Get its xt and call xt-body
    interp.interpret_line("' mydata xt-body @ .");
    EXPECT_NE(out.str().find("99"), std::string::npos);
    interp.shutdown();
}

TEST_F(CompiledBodyTest, XtBodyNonXtFails) {
    std::ostringstream out, err;
    Interpreter interp(dict, out, err);

    // Push a non-xt value and try xt-body
    interp.interpret_line("42 xt-body");
    EXPECT_NE(err.str().find("expected xt"), std::string::npos);
    interp.shutdown();
}

TEST_F(CompiledBodyTest, XtBodyNoDataFieldFails) {
    std::ostringstream out, err;
    Interpreter interp(dict, out, err);

    // Define a word without a data field (normal colon def)
    interp.interpret_line(": my-add + ;");
    interp.interpret_line("' my-add xt-body");
    EXPECT_NE(err.str().find("no data field"), std::string::npos);
    interp.shutdown();
}

// --- marker ---

TEST_F(CompiledBodyTest, MarkerBasic) {
    std::ostringstream out, err;
    Interpreter interp(dict, out, err);

    // Define a word before the marker
    interp.interpret_line(": old-word 10 ;");
    EXPECT_TRUE(dict.lookup("old-word").has_value());

    // Create marker
    interp.interpret_line("marker mymark");
    EXPECT_TRUE(dict.lookup("mymark").has_value());

    // Define words after the marker
    interp.interpret_line(": new-word 20 ;");
    interp.interpret_line(": another-new 30 ;");
    EXPECT_TRUE(dict.lookup("new-word").has_value());
    EXPECT_TRUE(dict.lookup("another-new").has_value());

    // Execute marker — should forget new words
    interp.interpret_line("mymark");

    EXPECT_TRUE(dict.lookup("old-word").has_value());
    EXPECT_FALSE(dict.lookup("new-word").has_value());
    EXPECT_FALSE(dict.lookup("another-new").has_value());
    interp.shutdown();
}

TEST_F(CompiledBodyTest, MarkerForgetsItself) {
    std::ostringstream out, err;
    Interpreter interp(dict, out, err);

    interp.interpret_line("marker mymark");
    EXPECT_TRUE(dict.lookup("mymark").has_value());

    interp.interpret_line("mymark");
    EXPECT_FALSE(dict.lookup("mymark").has_value());
    interp.shutdown();
}

TEST_F(CompiledBodyTest, MarkerImplTrimming) {
    std::ostringstream out, err;
    Interpreter interp(dict, out, err);

    // Define a word with one implementation
    interp.interpret_line(": trimtest 10 ;");
    auto impls_before = dict.get_implementations("trimtest");
    ASSERT_TRUE(impls_before.has_value());
    EXPECT_EQ(impls_before->size(), 1u);

    // Create marker
    interp.interpret_line("marker mymark");

    // Add a second implementation to the same word
    interp.interpret_line(": trimtest 20 ;");
    auto impls_after = dict.get_implementations("trimtest");
    ASSERT_TRUE(impls_after.has_value());
    EXPECT_EQ(impls_after->size(), 2u);

    // Execute marker — should trim back to 1 impl
    interp.interpret_line("mymark");
    auto impls_restored = dict.get_implementations("trimtest");
    ASSERT_TRUE(impls_restored.has_value());
    EXPECT_EQ(impls_restored->size(), 1u);
    interp.shutdown();
}

TEST_F(CompiledBodyTest, MarkerEmpty) {
    std::ostringstream out, err;
    Interpreter interp(dict, out, err);

    size_t count_before = dict.concept_count();
    interp.interpret_line("marker m");
    EXPECT_EQ(dict.concept_count(), count_before + 1);

    // Execute immediately — only marker itself should be forgotten
    interp.interpret_line("m");
    EXPECT_EQ(dict.concept_count(), count_before);
    EXPECT_FALSE(dict.lookup("m").has_value());
    interp.shutdown();
}

// --- Call trace diagnostics ---

TEST_F(CompiledBodyTest, NativePrimitiveFailureShowsWordAndDepth) {
    // : inner + ;  — calling inner with empty stack should fail at +
    auto code = std::make_shared<ByteCode>();
    code->append(make_call("+"));
    compile_word("inner", code);

    std::ostringstream oss_err;
    ctx.set_err(&oss_err);

    auto impl = dict.lookup("inner");
    ASSERT_TRUE(impl.has_value());
    EXPECT_FALSE(execute_compiled(*(*impl)->bytecode(), ctx));

    auto err_text = oss_err.str();
    EXPECT_TRUE(err_text.find("Error in '+'") != std::string::npos);
    EXPECT_TRUE(err_text.find("stack depth:") != std::string::npos);
    ctx.set_err(&std::cerr);
}

TEST_F(CompiledBodyTest, NestedCallTraceShowsBothWords) {
    // : inner + ;
    // : outer inner ;
    // : top outer ;
    // Call top with empty stack — should fail at + inside inner,
    // then show trace for inner and outer (called via Call instructions)
    auto inner_code = std::make_shared<ByteCode>();
    inner_code->append(make_call("+"));
    compile_word("trace-inner", inner_code);

    auto outer_code = std::make_shared<ByteCode>();
    outer_code->append(make_call("trace-inner"));
    compile_word("trace-outer", outer_code);

    auto top_code = std::make_shared<ByteCode>();
    top_code->append(make_call("trace-outer"));
    compile_word("trace-top", top_code);

    std::ostringstream oss_err;
    ctx.set_err(&oss_err);

    auto impl = dict.lookup("trace-top");
    ASSERT_TRUE(impl.has_value());
    EXPECT_FALSE(execute_compiled(*(*impl)->bytecode(), ctx));

    auto err_text = oss_err.str();
    // Should contain trace for both inner and outer
    EXPECT_TRUE(err_text.find("Error in '+'") != std::string::npos);
    EXPECT_TRUE(err_text.find("in 'trace-inner'") != std::string::npos);
    EXPECT_TRUE(err_text.find("in 'trace-outer'") != std::string::npos);
    ctx.set_err(&std::cerr);
}

TEST_F(CompiledBodyTest, CallTraceShowsSourceLocation) {
    // Define a word via interpreter (which will set definition-type metadata)
    // then trigger an error to verify the trace includes source info
    std::ostringstream out, err;
    Interpreter interp(dict, out, err);

    // Simulate loading from file by setting impl-level source metadata
    interp.interpret_line(": src-inner + ;");
    auto src_impls = dict.get_implementations("src-inner");
    ASSERT_TRUE(src_impls.has_value());
    ASSERT_FALSE(src_impls->empty());
    src_impls->back()->mark_as_include("mylib.til", 12);

    interp.interpret_line(": src-outer src-inner ;");

    // Call src-outer with empty stack — will fail at + in src-inner
    err.str("");
    interp.interpret_line("src-outer");
    auto err_text = err.str();
    EXPECT_TRUE(err_text.find("defined at mylib.til:12") != std::string::npos);
    interp.shutdown();
}
