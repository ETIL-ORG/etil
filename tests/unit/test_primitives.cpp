// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/primitives.hpp"
#include "etil/core/compiled_body.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_byte_array.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/interpreter.hpp"
#include "etil/core/metadata.hpp"
#include "etil/core/metadata_json.hpp"
#include "etil/mcp/role_permissions.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <sstream>
#include <fstream>

using namespace etil::core;

class PrimitivesTest : public ::testing::Test {
protected:
    ExecutionContext ctx{0};
};

// --- Addition ---

TEST_F(PrimitivesTest, AddIntegers) {
    ctx.data_stack().push(Value(int64_t(3)));
    ctx.data_stack().push(Value(int64_t(4)));
    ASSERT_TRUE(prim_add(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Integer);
    EXPECT_EQ(result->as_int, 7);
}

TEST_F(PrimitivesTest, AddFloats) {
    ctx.data_stack().push(Value(1.5));
    ctx.data_stack().push(Value(2.5));
    ASSERT_TRUE(prim_add(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Float);
    EXPECT_DOUBLE_EQ(result->as_float, 4.0);
}

TEST_F(PrimitivesTest, AddMixed) {
    ctx.data_stack().push(Value(int64_t(3)));
    ctx.data_stack().push(Value(1.5));
    ASSERT_TRUE(prim_add(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Float);
    EXPECT_DOUBLE_EQ(result->as_float, 4.5);
}

TEST_F(PrimitivesTest, AddUnderflow) {
    ctx.data_stack().push(Value(int64_t(1)));
    ASSERT_FALSE(prim_add(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 1u);
}

TEST_F(PrimitivesTest, AddUnderflowEmpty) {
    ASSERT_FALSE(prim_add(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 0u);
}

// --- Subtraction ---

TEST_F(PrimitivesTest, SubIntegers) {
    ctx.data_stack().push(Value(int64_t(10)));
    ctx.data_stack().push(Value(int64_t(3)));
    ASSERT_TRUE(prim_sub(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, 7);
}

// --- Multiplication ---

TEST_F(PrimitivesTest, MulIntegers) {
    ctx.data_stack().push(Value(int64_t(6)));
    ctx.data_stack().push(Value(int64_t(7)));
    ASSERT_TRUE(prim_mul(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, 42);
}

// --- Division (/) ---

TEST_F(PrimitivesTest, DivIntegers) {
    ctx.data_stack().push(Value(int64_t(10)));
    ctx.data_stack().push(Value(int64_t(3)));
    ASSERT_TRUE(prim_div(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Integer);
    EXPECT_EQ(result->as_int, 3);  // truncating division
}

TEST_F(PrimitivesTest, DivFloats) {
    ctx.data_stack().push(Value(7.0));
    ctx.data_stack().push(Value(2.0));
    ASSERT_TRUE(prim_div(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Float);
    EXPECT_DOUBLE_EQ(result->as_float, 3.5);
}

TEST_F(PrimitivesTest, DivByZero) {
    ctx.data_stack().push(Value(int64_t(10)));
    ctx.data_stack().push(Value(int64_t(0)));
    ASSERT_FALSE(prim_div(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 2u);
}

// --- Modulo (mod) ---

TEST_F(PrimitivesTest, ModIntegers) {
    ctx.data_stack().push(Value(int64_t(10)));
    ctx.data_stack().push(Value(int64_t(3)));
    ASSERT_TRUE(prim_mod(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Integer);
    EXPECT_EQ(result->as_int, 1);
}

TEST_F(PrimitivesTest, ModFloats) {
    ctx.data_stack().push(Value(7.5));
    ctx.data_stack().push(Value(2.0));
    ASSERT_TRUE(prim_mod(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Float);
    EXPECT_DOUBLE_EQ(result->as_float, 1.5);
}

TEST_F(PrimitivesTest, ModByZero) {
    ctx.data_stack().push(Value(int64_t(10)));
    ctx.data_stack().push(Value(int64_t(0)));
    ASSERT_FALSE(prim_mod(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 2u);
}

// --- DivMod (/mod) ---

TEST_F(PrimitivesTest, DivModIntegers) {
    ctx.data_stack().push(Value(int64_t(10)));
    ctx.data_stack().push(Value(int64_t(3)));
    ASSERT_TRUE(prim_divmod(ctx));
    // Stack: remainder (deeper), quotient (top)
    auto quot = ctx.data_stack().pop();
    auto rem = ctx.data_stack().pop();
    ASSERT_TRUE(quot.has_value());
    ASSERT_TRUE(rem.has_value());
    EXPECT_EQ(quot->as_int, 3);
    EXPECT_EQ(rem->as_int, 1);
}

TEST_F(PrimitivesTest, DivModByZero) {
    ctx.data_stack().push(Value(int64_t(10)));
    ctx.data_stack().push(Value(int64_t(0)));
    ASSERT_FALSE(prim_divmod(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 2u);
}

// --- NEGATE ---

TEST_F(PrimitivesTest, NegateInteger) {
    ctx.data_stack().push(Value(int64_t(5)));
    ASSERT_TRUE(prim_negate(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, -5);
}

TEST_F(PrimitivesTest, NegateNegativeInteger) {
    ctx.data_stack().push(Value(int64_t(-3)));
    ASSERT_TRUE(prim_negate(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 3);
}

TEST_F(PrimitivesTest, NegateFloat) {
    ctx.data_stack().push(Value(2.5));
    ASSERT_TRUE(prim_negate(ctx));
    EXPECT_DOUBLE_EQ(ctx.data_stack().pop()->as_float, -2.5);
}

TEST_F(PrimitivesTest, NegateUnderflow) {
    ASSERT_FALSE(prim_negate(ctx));
}

// --- ABS ---

TEST_F(PrimitivesTest, AbsPositive) {
    ctx.data_stack().push(Value(int64_t(5)));
    ASSERT_TRUE(prim_abs(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 5);
}

TEST_F(PrimitivesTest, AbsNegative) {
    ctx.data_stack().push(Value(int64_t(-7)));
    ASSERT_TRUE(prim_abs(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 7);
}

TEST_F(PrimitivesTest, AbsNegativeFloat) {
    ctx.data_stack().push(Value(-3.5));
    ASSERT_TRUE(prim_abs(ctx));
    EXPECT_DOUBLE_EQ(ctx.data_stack().pop()->as_float, 3.5);
}

TEST_F(PrimitivesTest, AbsUnderflow) {
    ASSERT_FALSE(prim_abs(ctx));
}

// --- DUP ---

TEST_F(PrimitivesTest, Dup) {
    ctx.data_stack().push(Value(int64_t(42)));
    ASSERT_TRUE(prim_dup(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 2u);
    auto top = ctx.data_stack().pop();
    auto second = ctx.data_stack().pop();
    EXPECT_EQ(top->as_int, 42);
    EXPECT_EQ(second->as_int, 42);
}

TEST_F(PrimitivesTest, DupUnderflow) {
    ASSERT_FALSE(prim_dup(ctx));
}

// --- DROP ---

TEST_F(PrimitivesTest, Drop) {
    ctx.data_stack().push(Value(int64_t(1)));
    ctx.data_stack().push(Value(int64_t(2)));
    ASSERT_TRUE(prim_drop(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 1u);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 1);
}

TEST_F(PrimitivesTest, DropUnderflow) {
    ASSERT_FALSE(prim_drop(ctx));
}

// --- SWAP ---

TEST_F(PrimitivesTest, Swap) {
    ctx.data_stack().push(Value(int64_t(1)));
    ctx.data_stack().push(Value(int64_t(2)));
    ASSERT_TRUE(prim_swap(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 2u);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 1);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 2);
}

TEST_F(PrimitivesTest, SwapUnderflow) {
    ctx.data_stack().push(Value(int64_t(1)));
    ASSERT_FALSE(prim_swap(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 1u);
}

// --- OVER ---

TEST_F(PrimitivesTest, Over) {
    ctx.data_stack().push(Value(int64_t(1)));
    ctx.data_stack().push(Value(int64_t(2)));
    ASSERT_TRUE(prim_over(ctx));
    // ( 1 2 -- 1 2 1 )
    EXPECT_EQ(ctx.data_stack().size(), 3u);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 1);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 2);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 1);
}

TEST_F(PrimitivesTest, OverUnderflow) {
    ctx.data_stack().push(Value(int64_t(1)));
    ASSERT_FALSE(prim_over(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 1u);
}

// --- ROT ---

TEST_F(PrimitivesTest, Rot) {
    ctx.data_stack().push(Value(int64_t(1)));
    ctx.data_stack().push(Value(int64_t(2)));
    ctx.data_stack().push(Value(int64_t(3)));
    ASSERT_TRUE(prim_rot(ctx));
    // ( 1 2 3 -- 2 3 1 )
    EXPECT_EQ(ctx.data_stack().size(), 3u);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 1);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 3);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 2);
}

TEST_F(PrimitivesTest, RotUnderflowOne) {
    ctx.data_stack().push(Value(int64_t(1)));
    ASSERT_FALSE(prim_rot(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 1u);
}

TEST_F(PrimitivesTest, RotUnderflowTwo) {
    ctx.data_stack().push(Value(int64_t(1)));
    ctx.data_stack().push(Value(int64_t(2)));
    ASSERT_FALSE(prim_rot(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 2u);
}

// --- PICK ---

TEST_F(PrimitivesTest, Pick0IsDup) {
    ctx.data_stack().push(Value(int64_t(10)));
    ctx.data_stack().push(Value(int64_t(20)));
    ctx.data_stack().push(Value(int64_t(0)));   // pick index
    ASSERT_TRUE(prim_pick(ctx));
    // ( 10 20 -- 10 20 20 )
    EXPECT_EQ(ctx.data_stack().size(), 3u);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 20);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 20);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 10);
}

TEST_F(PrimitivesTest, Pick1IsOver) {
    ctx.data_stack().push(Value(int64_t(10)));
    ctx.data_stack().push(Value(int64_t(20)));
    ctx.data_stack().push(Value(int64_t(1)));   // pick index
    ASSERT_TRUE(prim_pick(ctx));
    // ( 10 20 -- 10 20 10 )
    EXPECT_EQ(ctx.data_stack().size(), 3u);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 10);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 20);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 10);
}

TEST_F(PrimitivesTest, PickDeep) {
    ctx.data_stack().push(Value(int64_t(100)));
    ctx.data_stack().push(Value(int64_t(200)));
    ctx.data_stack().push(Value(int64_t(300)));
    ctx.data_stack().push(Value(int64_t(2)));   // pick index
    ASSERT_TRUE(prim_pick(ctx));
    // ( 100 200 300 -- 100 200 300 100 )
    EXPECT_EQ(ctx.data_stack().size(), 4u);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 100);
}

TEST_F(PrimitivesTest, PickUnderflow) {
    // Empty stack — can't even pop the index
    ASSERT_FALSE(prim_pick(ctx));
}

TEST_F(PrimitivesTest, PickNegativeIndex) {
    ctx.data_stack().push(Value(int64_t(42)));
    ctx.data_stack().push(Value(int64_t(-1)));
    std::ostringstream err;
    ctx.set_err(&err);
    ASSERT_FALSE(prim_pick(ctx));
    EXPECT_NE(err.str().find("Illegal pick index -1."), std::string::npos);
    // Index was consumed but original value remains
    EXPECT_EQ(ctx.data_stack().size(), 1u);
}

TEST_F(PrimitivesTest, PickOutOfRange) {
    ctx.data_stack().push(Value(int64_t(42)));
    ctx.data_stack().push(Value(int64_t(1)));   // only 1 item below, index 1 is out of range
    std::ostringstream err;
    ctx.set_err(&err);
    ASSERT_FALSE(prim_pick(ctx));
    EXPECT_NE(err.str().find("Illegal pick index 1."), std::string::npos);
    EXPECT_EQ(ctx.data_stack().size(), 1u);
}

TEST_F(PrimitivesTest, PickHeapRefcount) {
    auto* hs = HeapString::create("hello");
    Value sv = Value::from(hs);
    // hs refcount = 1 from create
    ctx.data_stack().push(sv);
    ctx.data_stack().push(Value(int64_t(0)));   // pick index 0
    ASSERT_TRUE(prim_pick(ctx));
    // String now in 2 stack slots — refcount should be 2
    EXPECT_EQ(ctx.data_stack().size(), 2u);
    // Drop both
    ASSERT_TRUE(prim_drop(ctx));
    ASSERT_TRUE(prim_drop(ctx));
    // Should not crash (refcount properly managed)
}

// --- NIP ---

TEST_F(PrimitivesTest, Nip) {
    ctx.data_stack().push(Value(int64_t(10)));
    ctx.data_stack().push(Value(int64_t(20)));
    ASSERT_TRUE(prim_nip(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 1u);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 20);
}

TEST_F(PrimitivesTest, NipUnderflow) {
    ctx.data_stack().push(Value(int64_t(10)));
    ASSERT_FALSE(prim_nip(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 1u);
}

TEST_F(PrimitivesTest, NipHeapRelease) {
    auto* hs = HeapString::create("discard");
    ctx.data_stack().push(Value::from(hs));
    ctx.data_stack().push(Value(int64_t(42)));
    ASSERT_TRUE(prim_nip(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 1u);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 42);
}

// --- TUCK ---

TEST_F(PrimitivesTest, Tuck) {
    ctx.data_stack().push(Value(int64_t(10)));
    ctx.data_stack().push(Value(int64_t(20)));
    ASSERT_TRUE(prim_tuck(ctx));
    // ( 10 20 -- 20 10 20 )
    EXPECT_EQ(ctx.data_stack().size(), 3u);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 20);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 10);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 20);
}

TEST_F(PrimitivesTest, TuckUnderflow) {
    ctx.data_stack().push(Value(int64_t(10)));
    ASSERT_FALSE(prim_tuck(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 1u);
}

// --- DEPTH ---

TEST_F(PrimitivesTest, DepthEmpty) {
    ASSERT_TRUE(prim_depth(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 0);
}

TEST_F(PrimitivesTest, DepthNonEmpty) {
    ctx.data_stack().push(Value(int64_t(10)));
    ctx.data_stack().push(Value(int64_t(20)));
    ASSERT_TRUE(prim_depth(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 3u);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 2);
}

// --- ?DUP ---

TEST_F(PrimitivesTest, QdupNonZero) {
    ctx.data_stack().push(Value(int64_t(42)));
    ASSERT_TRUE(prim_qdup(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 2u);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 42);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 42);
}

TEST_F(PrimitivesTest, QdupZero) {
    ctx.data_stack().push(Value(int64_t(0)));
    ASSERT_TRUE(prim_qdup(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 1u);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 0);
}

TEST_F(PrimitivesTest, QdupUnderflow) {
    ASSERT_FALSE(prim_qdup(ctx));
}

// --- ROLL ---

TEST_F(PrimitivesTest, Roll0IsNoop) {
    ctx.data_stack().push(Value(int64_t(10)));
    ctx.data_stack().push(Value(int64_t(20)));
    ctx.data_stack().push(Value(int64_t(0)));
    ASSERT_TRUE(prim_roll(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 2u);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 20);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 10);
}

TEST_F(PrimitivesTest, Roll1IsSwap) {
    ctx.data_stack().push(Value(int64_t(10)));
    ctx.data_stack().push(Value(int64_t(20)));
    ctx.data_stack().push(Value(int64_t(1)));
    ASSERT_TRUE(prim_roll(ctx));
    // ( 10 20 -- 20 10 )
    EXPECT_EQ(ctx.data_stack().size(), 2u);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 10);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 20);
}

TEST_F(PrimitivesTest, Roll2IsRot) {
    ctx.data_stack().push(Value(int64_t(10)));
    ctx.data_stack().push(Value(int64_t(20)));
    ctx.data_stack().push(Value(int64_t(30)));
    ctx.data_stack().push(Value(int64_t(2)));
    ASSERT_TRUE(prim_roll(ctx));
    // ( 10 20 30 -- 20 30 10 )
    EXPECT_EQ(ctx.data_stack().size(), 3u);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 10);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 30);
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 20);
}

TEST_F(PrimitivesTest, RollOutOfRange) {
    ctx.data_stack().push(Value(int64_t(42)));
    ctx.data_stack().push(Value(int64_t(1)));
    std::ostringstream err;
    ctx.set_err(&err);
    ASSERT_FALSE(prim_roll(ctx));
    EXPECT_NE(err.str().find("Illegal roll index 1."), std::string::npos);
    EXPECT_EQ(ctx.data_stack().size(), 1u);
}

TEST_F(PrimitivesTest, RollNegativeIndex) {
    ctx.data_stack().push(Value(int64_t(42)));
    ctx.data_stack().push(Value(int64_t(-1)));
    std::ostringstream err;
    ctx.set_err(&err);
    ASSERT_FALSE(prim_roll(ctx));
    EXPECT_NE(err.str().find("Illegal roll index -1."), std::string::npos);
}

// --- MAX ---

TEST_F(PrimitivesTest, MaxIntegers) {
    ctx.data_stack().push(Value(int64_t(3)));
    ctx.data_stack().push(Value(int64_t(7)));
    ASSERT_TRUE(prim_max(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 7);
}

TEST_F(PrimitivesTest, MaxFirstLarger) {
    ctx.data_stack().push(Value(int64_t(10)));
    ctx.data_stack().push(Value(int64_t(5)));
    ASSERT_TRUE(prim_max(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 10);
}

TEST_F(PrimitivesTest, MaxEqual) {
    ctx.data_stack().push(Value(int64_t(5)));
    ctx.data_stack().push(Value(int64_t(5)));
    ASSERT_TRUE(prim_max(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 5);
}

TEST_F(PrimitivesTest, MaxNegative) {
    ctx.data_stack().push(Value(int64_t(-10)));
    ctx.data_stack().push(Value(int64_t(-3)));
    ASSERT_TRUE(prim_max(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, -3);
}

TEST_F(PrimitivesTest, MaxMixed) {
    ctx.data_stack().push(Value(int64_t(3)));
    ctx.data_stack().push(Value(7.5));
    ASSERT_TRUE(prim_max(ctx));
    EXPECT_DOUBLE_EQ(ctx.data_stack().pop()->as_float, 7.5);
}

// --- MIN ---

TEST_F(PrimitivesTest, MinIntegers) {
    ctx.data_stack().push(Value(int64_t(3)));
    ctx.data_stack().push(Value(int64_t(7)));
    ASSERT_TRUE(prim_min(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 3);
}

TEST_F(PrimitivesTest, MinNegative) {
    ctx.data_stack().push(Value(int64_t(-10)));
    ctx.data_stack().push(Value(int64_t(-3)));
    ASSERT_TRUE(prim_min(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, -10);
}

TEST_F(PrimitivesTest, MinMixed) {
    ctx.data_stack().push(Value(int64_t(3)));
    ctx.data_stack().push(Value(1.5));
    ASSERT_TRUE(prim_min(ctx));
    EXPECT_DOUBLE_EQ(ctx.data_stack().pop()->as_float, 1.5);
}

// --- LSHIFT ---

TEST_F(PrimitivesTest, Lshift) {
    ctx.data_stack().push(Value(int64_t(1)));
    ctx.data_stack().push(Value(int64_t(8)));
    ASSERT_TRUE(prim_lshift(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 256);
}

TEST_F(PrimitivesTest, LshiftZero) {
    ctx.data_stack().push(Value(int64_t(42)));
    ctx.data_stack().push(Value(int64_t(0)));
    ASSERT_TRUE(prim_lshift(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 42);
}

TEST_F(PrimitivesTest, LshiftOverflow) {
    ctx.data_stack().push(Value(int64_t(1)));
    ctx.data_stack().push(Value(int64_t(64)));
    ASSERT_TRUE(prim_lshift(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 0);
}

TEST_F(PrimitivesTest, LshiftNegativeCount) {
    ctx.data_stack().push(Value(int64_t(1)));
    ctx.data_stack().push(Value(int64_t(-1)));
    ASSERT_TRUE(prim_lshift(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 0);
}

// --- RSHIFT ---

TEST_F(PrimitivesTest, Rshift) {
    ctx.data_stack().push(Value(int64_t(256)));
    ctx.data_stack().push(Value(int64_t(8)));
    ASSERT_TRUE(prim_rshift(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 1);
}

TEST_F(PrimitivesTest, RshiftLogical) {
    // Logical right shift: -1 >> 1 should give large positive (not -1)
    ctx.data_stack().push(Value(int64_t(-1)));
    ctx.data_stack().push(Value(int64_t(1)));
    ASSERT_TRUE(prim_rshift(ctx));
    EXPECT_GT(ctx.data_stack().pop()->as_int, 0);
}

TEST_F(PrimitivesTest, RshiftOverflow) {
    ctx.data_stack().push(Value(int64_t(255)));
    ctx.data_stack().push(Value(int64_t(64)));
    ASSERT_TRUE(prim_rshift(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 0);
}

// --- LROLL ---

TEST_F(PrimitivesTest, LrollBasic) {
    // Rotate 1 left by 1 → 2
    ctx.data_stack().push(Value(int64_t(1)));
    ctx.data_stack().push(Value(int64_t(1)));
    ASSERT_TRUE(prim_lroll(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 2);
}

TEST_F(PrimitivesTest, LrollWrap) {
    // Rotate 1 left by 63 → high bit set (same as 1 << 63 = INT64_MIN)
    ctx.data_stack().push(Value(int64_t(1)));
    ctx.data_stack().push(Value(int64_t(63)));
    ASSERT_TRUE(prim_lroll(ctx));
    auto result = static_cast<uint64_t>(ctx.data_stack().pop()->as_int);
    EXPECT_EQ(result, uint64_t(1) << 63);
}

TEST_F(PrimitivesTest, LrollBy64IsIdentity) {
    ctx.data_stack().push(Value(int64_t(42)));
    ctx.data_stack().push(Value(int64_t(64)));
    ASSERT_TRUE(prim_lroll(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 42);
}

TEST_F(PrimitivesTest, LrollNegativeCount) {
    // lroll by -1 = rroll by 1
    ctx.data_stack().push(Value(int64_t(2)));
    ctx.data_stack().push(Value(int64_t(-1)));
    ASSERT_TRUE(prim_lroll(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 1);
}

// --- RROLL ---

TEST_F(PrimitivesTest, RrollBasic) {
    // Rotate 2 right by 1 → 1
    ctx.data_stack().push(Value(int64_t(2)));
    ctx.data_stack().push(Value(int64_t(1)));
    ASSERT_TRUE(prim_rroll(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 1);
}

TEST_F(PrimitivesTest, RrollWrap) {
    // Rotate 1 right by 1 → high bit set (wraps to MSB)
    ctx.data_stack().push(Value(int64_t(1)));
    ctx.data_stack().push(Value(int64_t(1)));
    ASSERT_TRUE(prim_rroll(ctx));
    auto result = static_cast<uint64_t>(ctx.data_stack().pop()->as_int);
    EXPECT_EQ(result, uint64_t(1) << 63);
}

TEST_F(PrimitivesTest, RrollBy64IsIdentity) {
    ctx.data_stack().push(Value(int64_t(42)));
    ctx.data_stack().push(Value(int64_t(64)));
    ASSERT_TRUE(prim_rroll(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 42);
}

TEST_F(PrimitivesTest, RrollNegativeCount) {
    // rroll by -1 = lroll by 1
    ctx.data_stack().push(Value(int64_t(1)));
    ctx.data_stack().push(Value(int64_t(-1)));
    ASSERT_TRUE(prim_rroll(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 2);
}

// --- Comparison: = ---

TEST_F(PrimitivesTest, EqTrue) {
    ctx.data_stack().push(Value(int64_t(5)));
    ctx.data_stack().push(Value(int64_t(5)));
    ASSERT_TRUE(prim_eq(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->type, Value::Type::Boolean);
    EXPECT_TRUE(r->as_bool());
}

TEST_F(PrimitivesTest, EqFalse) {
    ctx.data_stack().push(Value(int64_t(3)));
    ctx.data_stack().push(Value(int64_t(5)));
    ASSERT_TRUE(prim_eq(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->type, Value::Type::Boolean);
    EXPECT_FALSE(r->as_bool());
}

TEST_F(PrimitivesTest, EqMixed) {
    ctx.data_stack().push(Value(int64_t(5)));
    ctx.data_stack().push(Value(5.0));
    ASSERT_TRUE(prim_eq(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->type, Value::Type::Boolean);
    EXPECT_TRUE(r->as_bool());
}

// --- Comparison: <> ---

TEST_F(PrimitivesTest, Neq) {
    ctx.data_stack().push(Value(int64_t(3)));
    ctx.data_stack().push(Value(int64_t(5)));
    ASSERT_TRUE(prim_neq(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->type, Value::Type::Boolean);
    EXPECT_TRUE(r->as_bool());
}

// --- Comparison: < ---

TEST_F(PrimitivesTest, LtTrue) {
    ctx.data_stack().push(Value(int64_t(3)));
    ctx.data_stack().push(Value(int64_t(5)));
    ASSERT_TRUE(prim_lt(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->type, Value::Type::Boolean);
    EXPECT_TRUE(r->as_bool());
}

TEST_F(PrimitivesTest, LtFalse) {
    ctx.data_stack().push(Value(int64_t(5)));
    ctx.data_stack().push(Value(int64_t(3)));
    ASSERT_TRUE(prim_lt(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->type, Value::Type::Boolean);
    EXPECT_FALSE(r->as_bool());
}

// --- Comparison: > ---

TEST_F(PrimitivesTest, GtTrue) {
    ctx.data_stack().push(Value(int64_t(5)));
    ctx.data_stack().push(Value(int64_t(3)));
    ASSERT_TRUE(prim_gt(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->type, Value::Type::Boolean);
    EXPECT_TRUE(r->as_bool());
}

// --- Comparison: <= ---

TEST_F(PrimitivesTest, LeTrue) {
    ctx.data_stack().push(Value(int64_t(5)));
    ctx.data_stack().push(Value(int64_t(5)));
    ASSERT_TRUE(prim_le(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->type, Value::Type::Boolean);
    EXPECT_TRUE(r->as_bool());
}

// --- Comparison: >= ---

TEST_F(PrimitivesTest, GeTrue) {
    ctx.data_stack().push(Value(int64_t(5)));
    ctx.data_stack().push(Value(int64_t(3)));
    ASSERT_TRUE(prim_ge(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->type, Value::Type::Boolean);
    EXPECT_TRUE(r->as_bool());
}

// --- Comparison: 0= ---

TEST_F(PrimitivesTest, ZeroEqTrue) {
    ctx.data_stack().push(Value(int64_t(0)));
    ASSERT_TRUE(prim_zero_eq(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->type, Value::Type::Boolean);
    EXPECT_TRUE(r->as_bool());
}

TEST_F(PrimitivesTest, ZeroEqFalse) {
    ctx.data_stack().push(Value(int64_t(42)));
    ASSERT_TRUE(prim_zero_eq(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->type, Value::Type::Boolean);
    EXPECT_FALSE(r->as_bool());
}

// --- Comparison: 0< ---

TEST_F(PrimitivesTest, ZeroLtTrue) {
    ctx.data_stack().push(Value(int64_t(-5)));
    ASSERT_TRUE(prim_zero_lt(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->type, Value::Type::Boolean);
    EXPECT_TRUE(r->as_bool());
}

// --- Comparison: 0> ---

TEST_F(PrimitivesTest, ZeroGtTrue) {
    ctx.data_stack().push(Value(int64_t(5)));
    ASSERT_TRUE(prim_zero_gt(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->type, Value::Type::Boolean);
    EXPECT_TRUE(r->as_bool());
}

// --- Logic: and ---

TEST_F(PrimitivesTest, And) {
    ctx.data_stack().push(Value(true));
    ctx.data_stack().push(Value(true));
    ASSERT_TRUE(prim_and(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->type, Value::Type::Boolean);
    EXPECT_TRUE(r->as_bool());
}

TEST_F(PrimitivesTest, AndFalse) {
    ctx.data_stack().push(Value(true));
    ctx.data_stack().push(Value(false));
    ASSERT_TRUE(prim_and(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->type, Value::Type::Boolean);
    EXPECT_FALSE(r->as_bool());
}

// --- Logic: or ---

TEST_F(PrimitivesTest, Or) {
    ctx.data_stack().push(Value(true));
    ctx.data_stack().push(Value(false));
    ASSERT_TRUE(prim_or(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->type, Value::Type::Boolean);
    EXPECT_TRUE(r->as_bool());
}

// --- Logic: xor ---

TEST_F(PrimitivesTest, Xor) {
    ctx.data_stack().push(Value(true));
    ctx.data_stack().push(Value(true));
    ASSERT_TRUE(prim_xor(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->type, Value::Type::Boolean);
    EXPECT_FALSE(r->as_bool());
}

// --- Logic: invert ---

TEST_F(PrimitivesTest, InvertTrue) {
    ctx.data_stack().push(Value(int64_t(0)));
    ASSERT_TRUE(prim_invert(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, -1);  // ~0 == -1
}

TEST_F(PrimitivesTest, InvertFalse) {
    ctx.data_stack().push(Value(int64_t(-1)));
    ASSERT_TRUE(prim_invert(ctx));
    EXPECT_EQ(ctx.data_stack().pop()->as_int, 0);  // ~(-1) == 0
}

// --- Dot (print) ---

TEST_F(PrimitivesTest, DotPrintsInteger) {
    ctx.data_stack().push(Value(int64_t(42)));
    testing::internal::CaptureStdout();
    ASSERT_TRUE(prim_dot(ctx));
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_EQ(output, "42 ");
    EXPECT_EQ(ctx.data_stack().size(), 0u);
}

TEST_F(PrimitivesTest, DotUnderflow) {
    ASSERT_FALSE(prim_dot(ctx));
}

// --- CR ---

TEST_F(PrimitivesTest, Cr) {
    testing::internal::CaptureStdout();
    ASSERT_TRUE(prim_cr(ctx));
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "\n");
}

// --- EMIT ---

TEST_F(PrimitivesTest, Emit) {
    ctx.data_stack().push(Value(int64_t(65)));  // 'A'
    testing::internal::CaptureStdout();
    ASSERT_TRUE(prim_emit(ctx));
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "A");
}

TEST_F(PrimitivesTest, EmitUnderflow) {
    ASSERT_FALSE(prim_emit(ctx));
}

// --- SPACE ---

TEST_F(PrimitivesTest, Space) {
    testing::internal::CaptureStdout();
    ASSERT_TRUE(prim_space(ctx));
    EXPECT_EQ(testing::internal::GetCapturedStdout(), " ");
}

// --- SPACES ---

TEST_F(PrimitivesTest, Spaces) {
    ctx.data_stack().push(Value(int64_t(3)));
    testing::internal::CaptureStdout();
    ASSERT_TRUE(prim_spaces(ctx));
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "   ");
}

TEST_F(PrimitivesTest, SpacesZero) {
    ctx.data_stack().push(Value(int64_t(0)));
    testing::internal::CaptureStdout();
    ASSERT_TRUE(prim_spaces(ctx));
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "");
}

TEST_F(PrimitivesTest, SpacesUnderflow) {
    ASSERT_FALSE(prim_spaces(ctx));
}

// --- Words ---

TEST_F(PrimitivesTest, WordsListsDictionary) {
    Dictionary dict;
    dict.register_word("foo", WordImplPtr(new WordImpl("foo_impl", Dictionary::next_id())));
    dict.register_word("bar", WordImplPtr(new WordImpl("bar_impl", Dictionary::next_id())));
    ctx.set_dictionary(&dict);

    testing::internal::CaptureStdout();
    ASSERT_TRUE(prim_words(ctx));
    std::string output = testing::internal::GetCapturedStdout();
    // Sorted alphabetically
    EXPECT_NE(output.find("bar"), std::string::npos);
    EXPECT_NE(output.find("foo"), std::string::npos);
}

TEST_F(PrimitivesTest, WordsNoDictionary) {
    // No dictionary set on ctx
    ASSERT_FALSE(prim_words(ctx));
}

// --- Registration ---

TEST_F(PrimitivesTest, RegisterPrimitives) {
    Dictionary dict;
    register_primitives(dict);
    EXPECT_EQ(dict.concept_count(), 253u);  // +25 matrix + 16 neural net + 3 conversion primitives
    // Arithmetic
    EXPECT_TRUE(dict.lookup("+").has_value());
    EXPECT_TRUE(dict.lookup("-").has_value());
    EXPECT_TRUE(dict.lookup("*").has_value());
    EXPECT_TRUE(dict.lookup("/").has_value());
    EXPECT_TRUE(dict.lookup("mod").has_value());
    EXPECT_TRUE(dict.lookup("/mod").has_value());
    EXPECT_TRUE(dict.lookup("negate").has_value());
    EXPECT_TRUE(dict.lookup("abs").has_value());
    // Stack
    EXPECT_TRUE(dict.lookup("dup").has_value());
    EXPECT_TRUE(dict.lookup("drop").has_value());
    EXPECT_TRUE(dict.lookup("swap").has_value());
    EXPECT_TRUE(dict.lookup("over").has_value());
    EXPECT_TRUE(dict.lookup("rot").has_value());
    // Comparison
    EXPECT_TRUE(dict.lookup("=").has_value());
    EXPECT_TRUE(dict.lookup("<>").has_value());
    EXPECT_TRUE(dict.lookup("<").has_value());
    EXPECT_TRUE(dict.lookup(">").has_value());
    EXPECT_TRUE(dict.lookup("<=").has_value());
    EXPECT_TRUE(dict.lookup(">=").has_value());
    EXPECT_TRUE(dict.lookup("0=").has_value());
    EXPECT_TRUE(dict.lookup("0<").has_value());
    EXPECT_TRUE(dict.lookup("0>").has_value());
    // Logic
    EXPECT_TRUE(dict.lookup("and").has_value());
    EXPECT_TRUE(dict.lookup("or").has_value());
    EXPECT_TRUE(dict.lookup("xor").has_value());
    EXPECT_TRUE(dict.lookup("invert").has_value());
    // I/O
    EXPECT_TRUE(dict.lookup(".").has_value());
    EXPECT_TRUE(dict.lookup("cr").has_value());
    EXPECT_TRUE(dict.lookup("emit").has_value());
    EXPECT_TRUE(dict.lookup("space").has_value());
    EXPECT_TRUE(dict.lookup("spaces").has_value());
    EXPECT_TRUE(dict.lookup("words").has_value());
    // Memory
    EXPECT_TRUE(dict.lookup(",").has_value());
    EXPECT_TRUE(dict.lookup("@").has_value());
    EXPECT_TRUE(dict.lookup("!").has_value());
    EXPECT_TRUE(dict.lookup("allot").has_value());
    // Math
    EXPECT_TRUE(dict.lookup("sqrt").has_value());
    EXPECT_TRUE(dict.lookup("sin").has_value());
    EXPECT_TRUE(dict.lookup("cos").has_value());
    EXPECT_TRUE(dict.lookup("tan").has_value());
    EXPECT_TRUE(dict.lookup("asin").has_value());
    EXPECT_TRUE(dict.lookup("acos").has_value());
    EXPECT_TRUE(dict.lookup("atan").has_value());
    EXPECT_TRUE(dict.lookup("atan2").has_value());
    EXPECT_TRUE(dict.lookup("log").has_value());
    EXPECT_TRUE(dict.lookup("log2").has_value());
    EXPECT_TRUE(dict.lookup("log10").has_value());
    EXPECT_TRUE(dict.lookup("exp").has_value());
    EXPECT_TRUE(dict.lookup("pow").has_value());
    EXPECT_TRUE(dict.lookup("ceil").has_value());
    EXPECT_TRUE(dict.lookup("floor").has_value());
    EXPECT_TRUE(dict.lookup("round").has_value());
    EXPECT_TRUE(dict.lookup("fmin").has_value());
    EXPECT_TRUE(dict.lookup("fmax").has_value());
    EXPECT_TRUE(dict.lookup("pi").has_value());
    // PRNG
    EXPECT_TRUE(dict.lookup("random").has_value());
    EXPECT_TRUE(dict.lookup("random-seed").has_value());
    EXPECT_TRUE(dict.lookup("random-range").has_value());
    // Map
    EXPECT_TRUE(dict.lookup("map-new").has_value());
    EXPECT_TRUE(dict.lookup("map-set").has_value());
    EXPECT_TRUE(dict.lookup("map-get").has_value());
    EXPECT_TRUE(dict.lookup("map-remove").has_value());
    EXPECT_TRUE(dict.lookup("map-length").has_value());
    EXPECT_TRUE(dict.lookup("map-keys").has_value());
    EXPECT_TRUE(dict.lookup("map-values").has_value());
    EXPECT_TRUE(dict.lookup("map-has?").has_value());
}

// --- Math primitives ---

TEST_F(PrimitivesTest, Sqrt) {
    ctx.data_stack().push(Value(int64_t(16)));
    ASSERT_TRUE(prim_sqrt(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_DOUBLE_EQ(r->as_float, 4.0);
}

TEST_F(PrimitivesTest, SqrtFloat) {
    ctx.data_stack().push(Value(2.0));
    ASSERT_TRUE(prim_sqrt(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_NEAR(r->as_float, 1.41421356, 1e-6);
}

TEST_F(PrimitivesTest, SqrtUnderflow) {
    EXPECT_FALSE(prim_sqrt(ctx));
}

TEST_F(PrimitivesTest, SinZero) {
    ctx.data_stack().push(Value(0.0));
    ASSERT_TRUE(prim_sin(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_DOUBLE_EQ(r->as_float, 0.0);
}

TEST_F(PrimitivesTest, CosZero) {
    ctx.data_stack().push(Value(0.0));
    ASSERT_TRUE(prim_cos(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_DOUBLE_EQ(r->as_float, 1.0);
}

TEST_F(PrimitivesTest, TanZero) {
    ctx.data_stack().push(Value(0.0));
    ASSERT_TRUE(prim_tan(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_DOUBLE_EQ(r->as_float, 0.0);
}

TEST_F(PrimitivesTest, AsinOne) {
    ctx.data_stack().push(Value(1.0));
    ASSERT_TRUE(prim_asin(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_NEAR(r->as_float, 1.5707963, 1e-6);  // pi/2
}

TEST_F(PrimitivesTest, AcosOne) {
    ctx.data_stack().push(Value(1.0));
    ASSERT_TRUE(prim_acos(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_DOUBLE_EQ(r->as_float, 0.0);
}

TEST_F(PrimitivesTest, AtanOne) {
    ctx.data_stack().push(Value(1.0));
    ASSERT_TRUE(prim_atan(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_NEAR(r->as_float, 0.7853981, 1e-6);  // pi/4
}

TEST_F(PrimitivesTest, Atan2) {
    ctx.data_stack().push(Value(1.0));   // y
    ctx.data_stack().push(Value(1.0));   // x
    ASSERT_TRUE(prim_atan2(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_NEAR(r->as_float, 0.7853981, 1e-6);  // pi/4
}

TEST_F(PrimitivesTest, LogE) {
    ctx.data_stack().push(Value(2.718281828));
    ASSERT_TRUE(prim_log(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_NEAR(r->as_float, 1.0, 1e-6);
}

TEST_F(PrimitivesTest, Log2Of8) {
    ctx.data_stack().push(Value(int64_t(8)));
    ASSERT_TRUE(prim_log2(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_DOUBLE_EQ(r->as_float, 3.0);
}

TEST_F(PrimitivesTest, Log10Of100) {
    ctx.data_stack().push(Value(int64_t(100)));
    ASSERT_TRUE(prim_log10(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_DOUBLE_EQ(r->as_float, 2.0);
}

TEST_F(PrimitivesTest, ExpZero) {
    ctx.data_stack().push(Value(0.0));
    ASSERT_TRUE(prim_exp(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_DOUBLE_EQ(r->as_float, 1.0);
}

TEST_F(PrimitivesTest, Pow) {
    ctx.data_stack().push(Value(int64_t(2)));  // base
    ctx.data_stack().push(Value(int64_t(10))); // exp
    ASSERT_TRUE(prim_pow(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_DOUBLE_EQ(r->as_float, 1024.0);
}

TEST_F(PrimitivesTest, Ceil) {
    ctx.data_stack().push(Value(3.2));
    ASSERT_TRUE(prim_ceil(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_DOUBLE_EQ(r->as_float, 4.0);
}

TEST_F(PrimitivesTest, Floor) {
    ctx.data_stack().push(Value(3.8));
    ASSERT_TRUE(prim_floor(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_DOUBLE_EQ(r->as_float, 3.0);
}

TEST_F(PrimitivesTest, Round) {
    ctx.data_stack().push(Value(3.5));
    ASSERT_TRUE(prim_round(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_DOUBLE_EQ(r->as_float, 4.0);
}

TEST_F(PrimitivesTest, TruncPositive) {
    ctx.data_stack().push(Value(3.7));
    ASSERT_TRUE(prim_trunc(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_DOUBLE_EQ(r->as_float, 3.0);
}

TEST_F(PrimitivesTest, TruncNegative) {
    ctx.data_stack().push(Value(-3.7));
    ASSERT_TRUE(prim_trunc(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_DOUBLE_EQ(r->as_float, -3.0);
}

TEST_F(PrimitivesTest, TruncWhole) {
    ctx.data_stack().push(Value(3.0));
    ASSERT_TRUE(prim_trunc(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_DOUBLE_EQ(r->as_float, 3.0);
}

TEST_F(PrimitivesTest, TruncIntPromoted) {
    ctx.data_stack().push(Value(int64_t(42)));
    ASSERT_TRUE(prim_trunc(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_DOUBLE_EQ(r->as_float, 42.0);
}

TEST_F(PrimitivesTest, TruncUnderflow) {
    EXPECT_FALSE(prim_trunc(ctx));
}

TEST_F(PrimitivesTest, FapproxAbsoluteTrue) {
    ctx.data_stack().push(Value(1.0));
    ctx.data_stack().push(Value(1.001));
    ctx.data_stack().push(Value(0.01));
    ASSERT_TRUE(prim_fapprox(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->as_int, -1);
}

TEST_F(PrimitivesTest, FapproxAbsoluteFalse) {
    ctx.data_stack().push(Value(1.0));
    ctx.data_stack().push(Value(2.0));
    ctx.data_stack().push(Value(0.5));
    ASSERT_TRUE(prim_fapprox(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->as_int, 0);
}

TEST_F(PrimitivesTest, FapproxExactTrue) {
    ctx.data_stack().push(Value(3.14));
    ctx.data_stack().push(Value(3.14));
    ctx.data_stack().push(Value(0.0));
    ASSERT_TRUE(prim_fapprox(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->as_int, -1);
}

TEST_F(PrimitivesTest, FapproxExactFalse) {
    ctx.data_stack().push(Value(3.14));
    ctx.data_stack().push(Value(3.15));
    ctx.data_stack().push(Value(0.0));
    ASSERT_TRUE(prim_fapprox(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->as_int, 0);
}

TEST_F(PrimitivesTest, FapproxRelativeTrue) {
    ctx.data_stack().push(Value(100.0));
    ctx.data_stack().push(Value(101.0));
    ctx.data_stack().push(Value(-0.02));
    ASSERT_TRUE(prim_fapprox(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->as_int, -1);
}

TEST_F(PrimitivesTest, FapproxRelativeFalse) {
    ctx.data_stack().push(Value(100.0));
    ctx.data_stack().push(Value(110.0));
    ctx.data_stack().push(Value(-0.02));
    ASSERT_TRUE(prim_fapprox(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_EQ(r->as_int, 0);
}

TEST_F(PrimitivesTest, FapproxUnderflow) {
    EXPECT_FALSE(prim_fapprox(ctx));
}

TEST_F(PrimitivesTest, Fmin) {
    ctx.data_stack().push(Value(int64_t(5)));
    ctx.data_stack().push(Value(int64_t(3)));
    ASSERT_TRUE(prim_fmin(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_DOUBLE_EQ(r->as_float, 3.0);
}

TEST_F(PrimitivesTest, Fmax) {
    ctx.data_stack().push(Value(int64_t(5)));
    ctx.data_stack().push(Value(int64_t(3)));
    ASSERT_TRUE(prim_fmax(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_DOUBLE_EQ(r->as_float, 5.0);
}

TEST_F(PrimitivesTest, Pi) {
    ASSERT_TRUE(prim_pi(ctx));
    auto r = ctx.data_stack().pop();
    EXPECT_NEAR(r->as_float, 3.14159265358979, 1e-10);
}

// --- Dictionary: forget ---

TEST(DictionaryTest, ForgetWord) {
    Dictionary dict;
    register_primitives(dict);
    EXPECT_TRUE(dict.lookup("+").has_value());
    EXPECT_TRUE(dict.forget_word("+"));
    EXPECT_FALSE(dict.lookup("+").has_value());
}

TEST(DictionaryTest, ForgetNotFound) {
    Dictionary dict;
    EXPECT_FALSE(dict.forget_word("nonexistent"));
}

TEST(DictionaryTest, ForgetWordRevealsPrevious) {
    Dictionary dict;
    auto id1 = Dictionary::next_id();
    auto id2 = Dictionary::next_id();
    dict.register_word("x", WordImplPtr(new WordImpl("x_v1", id1)));
    dict.register_word("x", WordImplPtr(new WordImpl("x_v2", id2)));

    // lookup returns the latest
    EXPECT_EQ(dict.lookup("x")->get()->id(), id2);

    // forget removes the latest, revealing the previous
    EXPECT_TRUE(dict.forget_word("x"));
    EXPECT_TRUE(dict.lookup("x").has_value());
    EXPECT_EQ(dict.lookup("x")->get()->id(), id1);

    // forget again removes the only remaining impl → concept gone
    EXPECT_TRUE(dict.forget_word("x"));
    EXPECT_FALSE(dict.lookup("x").has_value());
}

TEST(DictionaryTest, ForgetAll) {
    Dictionary dict;
    auto id1 = Dictionary::next_id();
    auto id2 = Dictionary::next_id();
    dict.register_word("x", WordImplPtr(new WordImpl("x_v1", id1)));
    dict.register_word("x", WordImplPtr(new WordImpl("x_v2", id2)));

    // forget_all removes the entire concept
    EXPECT_TRUE(dict.forget_all("x"));
    EXPECT_FALSE(dict.lookup("x").has_value());
}

TEST(DictionaryTest, ForgetAllNotFound) {
    Dictionary dict;
    EXPECT_FALSE(dict.forget_all("nonexistent"));
}

// ===== New primitives: input-reading, dictionary, metadata =====

// Helper fixture with interpreter context (for input_stream access)
class NewPrimitivesTest : public ::testing::Test {
protected:
    Dictionary dict;
    std::ostringstream out;
    std::unique_ptr<Interpreter> interp;

    void SetUp() override {
        register_primitives(dict);
        interp = std::make_unique<Interpreter>(dict, out);
    }

    ExecutionContext& ctx() { return interp->context(); }
};

// --- word-read ---

TEST_F(NewPrimitivesTest, WordReadSuccess) {
    std::istringstream iss("hello world");
    ctx().set_input_stream(&iss);
    ASSERT_TRUE(prim_word_read(ctx()));
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_TRUE(flag->as_bool());
    auto str = ctx().data_stack().pop();
    ASSERT_TRUE(str.has_value());
    EXPECT_EQ(str->type, Value::Type::String);
    auto* hs = str->as_string();
    EXPECT_EQ(std::string(hs->view()), "hello");
    str->release();
    ctx().set_input_stream(nullptr);
}

TEST_F(NewPrimitivesTest, WordReadEmpty) {
    std::istringstream iss("");
    ctx().set_input_stream(&iss);
    ASSERT_TRUE(prim_word_read(ctx()));
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_FALSE(flag->as_bool());
    ctx().set_input_stream(nullptr);
}

// --- string-read-delim ---

TEST_F(NewPrimitivesTest, StringReadDelimSuccess) {
    std::istringstream iss(" \"hello world\"");
    ctx().set_input_stream(&iss);
    ctx().data_stack().push(Value(int64_t(34)));  // '"'
    ASSERT_TRUE(prim_string_read_delim(ctx()));
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_TRUE(flag->as_bool());
    auto str = ctx().data_stack().pop();
    ASSERT_TRUE(str.has_value());
    EXPECT_EQ(str->type, Value::Type::String);
    auto* hs = str->as_string();
    EXPECT_EQ(std::string(hs->view()), "hello world");
    str->release();
    ctx().set_input_stream(nullptr);
}

TEST_F(NewPrimitivesTest, StringReadDelimNoStream) {
    ctx().set_input_stream(nullptr);
    ctx().data_stack().push(Value(int64_t(34)));
    ASSERT_TRUE(prim_string_read_delim(ctx()));
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_FALSE(flag->as_bool());
}

// --- dict-forget ---

TEST_F(NewPrimitivesTest, DictForgetSuccess) {
    auto* hs = HeapString::create("dup");
    ctx().data_stack().push(Value::from(hs));
    ASSERT_TRUE(prim_dict_forget(ctx()));
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_TRUE(flag->as_bool());
    EXPECT_FALSE(dict.lookup("dup").has_value());
}

TEST_F(NewPrimitivesTest, DictForgetNotFound) {
    auto* hs = HeapString::create("nonexistent");
    ctx().data_stack().push(Value::from(hs));
    ASSERT_TRUE(prim_dict_forget(ctx()));
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_FALSE(flag->as_bool());
}

// --- dict-forget-all ---

TEST_F(NewPrimitivesTest, DictForgetAllSuccess) {
    // Register two impls of "x"
    auto id1 = Dictionary::next_id();
    auto id2 = Dictionary::next_id();
    dict.register_word("x", WordImplPtr(new WordImpl("x_v1", id1)));
    dict.register_word("x", WordImplPtr(new WordImpl("x_v2", id2)));
    auto* hs = HeapString::create("x");
    ctx().data_stack().push(Value::from(hs));
    ASSERT_TRUE(prim_dict_forget_all(ctx()));
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_TRUE(flag->as_bool());
    EXPECT_FALSE(dict.lookup("x").has_value());
}

// --- file-load ---

TEST_F(NewPrimitivesTest, FileLoadSuccess) {
    // Write a temp .til file
    std::string tmp_path = "/tmp/etil_test_file_load.til";
    {
        std::ofstream f(tmp_path);
        f << ": file-load-test-word 42 ;\n";
    }
    auto* hs = HeapString::create(tmp_path);
    ctx().data_stack().push(Value::from(hs));
    ASSERT_TRUE(prim_file_load(ctx()));
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_TRUE(flag->as_bool());
    EXPECT_TRUE(dict.lookup("file-load-test-word").has_value());
    std::remove(tmp_path.c_str());
}

TEST_F(NewPrimitivesTest, FileLoadNotFound) {
    auto* hs = HeapString::create("/tmp/etil_nonexistent_file.til");
    ctx().data_stack().push(Value::from(hs));
    ASSERT_TRUE(prim_file_load(ctx()));
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_FALSE(flag->as_bool());
}

// --- include ---

TEST_F(NewPrimitivesTest, IncludeSuccess) {
    std::string tmp_path = "/tmp/etil_test_include.til";
    {
        std::ofstream f(tmp_path);
        f << ": include-test-word 99 ;\n";
    }
    std::istringstream iss(tmp_path);
    ctx().set_input_stream(&iss);
    ASSERT_TRUE(prim_include(ctx()));
    EXPECT_TRUE(dict.lookup("include-test-word").has_value());
    ctx().set_input_stream(nullptr);
    std::remove(tmp_path.c_str());
}

// --- dict-meta-set / dict-meta-get ---

TEST_F(NewPrimitivesTest, DictMetaSetAndGet) {
    // Push: word-str key-str fmt-str content-str
    ctx().data_stack().push(Value::from(HeapString::create("dup")));
    ctx().data_stack().push(Value::from(HeapString::create("desc")));
    ctx().data_stack().push(Value::from(HeapString::create("text")));
    ctx().data_stack().push(Value::from(HeapString::create("Duplicate top")));
    ASSERT_TRUE(prim_dict_meta_set(ctx()));
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_TRUE(flag->as_bool());

    // Get it back
    ctx().data_stack().push(Value::from(HeapString::create("dup")));
    ctx().data_stack().push(Value::from(HeapString::create("desc")));
    ASSERT_TRUE(prim_dict_meta_get(ctx()));
    auto get_flag = ctx().data_stack().pop();
    EXPECT_EQ(get_flag->type, Value::Type::Boolean);
    EXPECT_TRUE(get_flag->as_bool());
    auto content = ctx().data_stack().pop();
    ASSERT_TRUE(content.has_value());
    EXPECT_EQ(content->type, Value::Type::String);
    auto* hs = content->as_string();
    EXPECT_EQ(std::string(hs->view()), "Duplicate top");
    content->release();
}

TEST_F(NewPrimitivesTest, DictMetaGetNotFound) {
    ctx().data_stack().push(Value::from(HeapString::create("dup")));
    ctx().data_stack().push(Value::from(HeapString::create("nonexistent")));
    ASSERT_TRUE(prim_dict_meta_get(ctx()));
    auto flag = ctx().data_stack().pop();
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_FALSE(flag->as_bool());
}

// --- dict-meta-del ---

TEST_F(NewPrimitivesTest, DictMetaDelSuccess) {
    // Set metadata first
    ctx().data_stack().push(Value::from(HeapString::create("dup")));
    ctx().data_stack().push(Value::from(HeapString::create("desc")));
    ctx().data_stack().push(Value::from(HeapString::create("text")));
    ctx().data_stack().push(Value::from(HeapString::create("test")));
    ASSERT_TRUE(prim_dict_meta_set(ctx()));
    ctx().data_stack().pop(); // discard flag

    // Delete it
    ctx().data_stack().push(Value::from(HeapString::create("dup")));
    ctx().data_stack().push(Value::from(HeapString::create("desc")));
    ASSERT_TRUE(prim_dict_meta_del(ctx()));
    auto flag = ctx().data_stack().pop();
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_TRUE(flag->as_bool());

    // Verify gone
    ctx().data_stack().push(Value::from(HeapString::create("dup")));
    ctx().data_stack().push(Value::from(HeapString::create("desc")));
    ASSERT_TRUE(prim_dict_meta_get(ctx()));
    auto get_flag = ctx().data_stack().pop();
    EXPECT_EQ(get_flag->type, Value::Type::Boolean);
    EXPECT_FALSE(get_flag->as_bool());
}

// --- dict-meta-keys ---

TEST_F(NewPrimitivesTest, DictMetaKeys) {
    // Set two metadata entries
    ctx().data_stack().push(Value::from(HeapString::create("dup")));
    ctx().data_stack().push(Value::from(HeapString::create("desc")));
    ctx().data_stack().push(Value::from(HeapString::create("text")));
    ctx().data_stack().push(Value::from(HeapString::create("A")));
    ASSERT_TRUE(prim_dict_meta_set(ctx()));
    ctx().data_stack().pop();

    ctx().data_stack().push(Value::from(HeapString::create("dup")));
    ctx().data_stack().push(Value::from(HeapString::create("author")));
    ctx().data_stack().push(Value::from(HeapString::create("text")));
    ctx().data_stack().push(Value::from(HeapString::create("Claude")));
    ASSERT_TRUE(prim_dict_meta_set(ctx()));
    ctx().data_stack().pop();

    // Get keys
    ctx().data_stack().push(Value::from(HeapString::create("dup")));
    ASSERT_TRUE(prim_dict_meta_keys(ctx()));
    auto flag = ctx().data_stack().pop();
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_TRUE(flag->as_bool());
    auto arr_val = ctx().data_stack().pop();
    ASSERT_TRUE(arr_val.has_value());
    EXPECT_EQ(arr_val->type, Value::Type::Array);
    auto* arr = arr_val->as_array();
    EXPECT_EQ(arr->length(), 2u);
    arr_val->release();
}

// --- impl-meta-set / impl-meta-get ---

TEST_F(NewPrimitivesTest, ImplMetaSetAndGet) {
    ctx().data_stack().push(Value::from(HeapString::create("dup")));
    ctx().data_stack().push(Value::from(HeapString::create("version")));
    ctx().data_stack().push(Value::from(HeapString::create("text")));
    ctx().data_stack().push(Value::from(HeapString::create("v1.0")));
    ASSERT_TRUE(prim_impl_meta_set(ctx()));
    auto flag = ctx().data_stack().pop();
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_TRUE(flag->as_bool());

    ctx().data_stack().push(Value::from(HeapString::create("dup")));
    ctx().data_stack().push(Value::from(HeapString::create("version")));
    ASSERT_TRUE(prim_impl_meta_get(ctx()));
    auto get_flag = ctx().data_stack().pop();
    EXPECT_EQ(get_flag->type, Value::Type::Boolean);
    EXPECT_TRUE(get_flag->as_bool());
    auto content = ctx().data_stack().pop();
    ASSERT_TRUE(content.has_value());
    auto* hs = content->as_string();
    EXPECT_EQ(std::string(hs->view()), "v1.0");
    content->release();
}

TEST_F(NewPrimitivesTest, ImplMetaSetUnknownWord) {
    ctx().data_stack().push(Value::from(HeapString::create("nonexistent")));
    ctx().data_stack().push(Value::from(HeapString::create("key")));
    ctx().data_stack().push(Value::from(HeapString::create("text")));
    ctx().data_stack().push(Value::from(HeapString::create("val")));
    ASSERT_TRUE(prim_impl_meta_set(ctx()));
    auto flag = ctx().data_stack().pop();
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_FALSE(flag->as_bool());
}

TEST_F(NewPrimitivesTest, ImplMetaGetUnknownWord) {
    ctx().data_stack().push(Value::from(HeapString::create("nonexistent")));
    ctx().data_stack().push(Value::from(HeapString::create("key")));
    ASSERT_TRUE(prim_impl_meta_get(ctx()));
    auto flag = ctx().data_stack().pop();
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_FALSE(flag->as_bool());
}

// ===== Help primitive =====

TEST_F(NewPrimitivesTest, HelpKnownPrimitive) {
    // Set concept metadata for "dup" so help can find it
    dict.set_concept_metadata("dup", "description", MetadataFormat::Text, "Duplicate top of stack");
    dict.set_concept_metadata("dup", "stack-effect", MetadataFormat::Text, "( x -- x x )");
    dict.set_concept_metadata("dup", "category", MetadataFormat::Text, "stack");

    std::istringstream iss("dup");
    ctx().set_input_stream(&iss);
    out.str("");
    ASSERT_TRUE(prim_help(ctx()));
    std::string output = out.str();
    EXPECT_NE(output.find("dup"), std::string::npos);
    EXPECT_NE(output.find("Duplicate top of stack"), std::string::npos);
    EXPECT_NE(output.find("( x -- x x )"), std::string::npos);
    EXPECT_NE(output.find("stack"), std::string::npos);
    ctx().set_input_stream(nullptr);
}

TEST_F(NewPrimitivesTest, HelpUnknownWord) {
    std::istringstream iss("nonexistent_xyzzy");
    ctx().set_input_stream(&iss);
    out.str("");
    ASSERT_TRUE(prim_help(ctx()));
    std::string output = out.str();
    EXPECT_NE(output.find("No help available"), std::string::npos);
    ctx().set_input_stream(nullptr);
}

TEST_F(NewPrimitivesTest, HelpNoToken) {
    // Set metadata for at least one word so we can verify output
    dict.set_concept_metadata("dup", "description", MetadataFormat::Text, "Duplicate top of stack");
    // Register a handler word with metadata (simulates register_handler_words + help.til)
    dict.register_handler_word("if");
    dict.set_concept_metadata("if", "description", MetadataFormat::Text, "Conditional branch");
    std::istringstream iss("");
    ctx().set_input_stream(&iss);
    out.str("");
    ASSERT_TRUE(prim_help(ctx()));
    std::string output = out.str();
    // Should print help for all words, including dup and registered handler words
    EXPECT_NE(output.find("dup"), std::string::npos);
    EXPECT_NE(output.find("if"), std::string::npos);
    ctx().set_input_stream(nullptr);
}

TEST_F(NewPrimitivesTest, HelpHandlerWord) {
    // "if" is a handler word registered as a concept with metadata
    dict.register_handler_word("if");
    dict.set_concept_metadata("if", "description", MetadataFormat::Text, "Conditional branch");
    dict.set_concept_metadata("if", "category", MetadataFormat::Text, "control");
    std::istringstream iss("if");
    ctx().set_input_stream(&iss);
    out.str("");
    ASSERT_TRUE(prim_help(ctx()));
    std::string output = out.str();
    EXPECT_NE(output.find("if"), std::string::npos);
    EXPECT_NE(output.find("Conditional"), std::string::npos);
    EXPECT_NE(output.find("control"), std::string::npos);
    ctx().set_input_stream(nullptr);
}

TEST_F(NewPrimitivesTest, HelpNoInputStream) {
    // No input stream: should print help for all words
    dict.set_concept_metadata("dup", "description", MetadataFormat::Text, "Duplicate top of stack");
    dict.register_handler_word("if");
    dict.set_concept_metadata("if", "description", MetadataFormat::Text, "Conditional branch");
    ctx().set_input_stream(nullptr);
    out.str("");
    ASSERT_TRUE(prim_help(ctx()));
    std::string output = out.str();
    EXPECT_NE(output.find("dup"), std::string::npos);
    EXPECT_NE(output.find("if"), std::string::npos);
}

// ===== Dump primitive =====

TEST_F(PrimitivesTest, DumpInteger) {
    ctx.data_stack().push(Value(int64_t(42)));
    testing::internal::CaptureStdout();
    ASSERT_TRUE(prim_dump(ctx));
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("42"), std::string::npos);
    EXPECT_NE(output.find("integer"), std::string::npos);
    // Stack should be unchanged (non-destructive)
    EXPECT_EQ(ctx.data_stack().size(), 1u);
    auto val = ctx.data_stack().pop();
    EXPECT_EQ(val->as_int, 42);
}

TEST_F(PrimitivesTest, DumpFloat) {
    ctx.data_stack().push(Value(3.14));
    testing::internal::CaptureStdout();
    ASSERT_TRUE(prim_dump(ctx));
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("3.14"), std::string::npos);
    EXPECT_NE(output.find("float"), std::string::npos);
    EXPECT_EQ(ctx.data_stack().size(), 1u);
    auto val = ctx.data_stack().pop();
    EXPECT_DOUBLE_EQ(val->as_float, 3.14);
}

TEST_F(PrimitivesTest, DumpString) {
    auto* hs = HeapString::create("hello world");
    ctx.data_stack().push(Value::from(hs));
    testing::internal::CaptureStdout();
    ASSERT_TRUE(prim_dump(ctx));
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("hello world"), std::string::npos);
    EXPECT_NE(output.find("string"), std::string::npos);
    EXPECT_NE(output.find("11 bytes"), std::string::npos);
    // Stack should still have the value
    EXPECT_EQ(ctx.data_stack().size(), 1u);
    // Refcount should be stable (1 ref from stack)
    EXPECT_EQ(hs->refcount(), 1u);
    auto val = ctx.data_stack().pop();
    val->release();
}

TEST_F(PrimitivesTest, DumpArray) {
    auto* arr = new HeapArray();
    arr->push_back(Value(int64_t(1)));
    arr->push_back(Value(int64_t(2)));
    arr->push_back(Value(int64_t(3)));
    ctx.data_stack().push(Value::from(arr));
    testing::internal::CaptureStdout();
    ASSERT_TRUE(prim_dump(ctx));
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("array"), std::string::npos);
    EXPECT_NE(output.find("3 elements"), std::string::npos);
    EXPECT_EQ(ctx.data_stack().size(), 1u);
    auto val = ctx.data_stack().pop();
    val->release();
}

TEST_F(PrimitivesTest, DumpByteArray) {
    auto* ba = new HeapByteArray(4);
    ba->set(0, 0x48);  // 'H'
    ba->set(1, 0x65);  // 'e'
    ba->set(2, 0x6C);  // 'l'
    ba->set(3, 0x6C);  // 'l'
    ctx.data_stack().push(Value::from(ba));
    testing::internal::CaptureStdout();
    ASSERT_TRUE(prim_dump(ctx));
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("bytes"), std::string::npos);
    EXPECT_NE(output.find("4 bytes"), std::string::npos);
    EXPECT_NE(output.find("48"), std::string::npos);
    EXPECT_EQ(ctx.data_stack().size(), 1u);
    auto val = ctx.data_stack().pop();
    val->release();
}

TEST_F(PrimitivesTest, DumpEmptyStack) {
    testing::internal::CaptureStdout();
    ASSERT_TRUE(prim_dump(ctx));
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("Stack empty"), std::string::npos);
    EXPECT_EQ(ctx.data_stack().size(), 0u);
}

TEST_F(PrimitivesTest, DumpNestedArray) {
    auto* inner = new HeapArray();
    inner->push_back(Value(int64_t(99)));
    auto* outer = new HeapArray();
    outer->push_back(Value::from(inner));
    ctx.data_stack().push(Value::from(outer));
    testing::internal::CaptureStdout();
    ASSERT_TRUE(prim_dump(ctx));
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("1 elements"), std::string::npos);
    EXPECT_NE(output.find("99"), std::string::npos);
    EXPECT_EQ(ctx.data_stack().size(), 1u);
    auto val = ctx.data_stack().pop();
    val->release();
}

// ===== See primitive =====

TEST_F(NewPrimitivesTest, SeeNativePrimitive) {
    std::istringstream iss("dup");
    ctx().set_input_stream(&iss);
    out.str("");
    ASSERT_TRUE(prim_see(ctx()));
    std::string output = out.str();
    EXPECT_NE(output.find("primitive"), std::string::npos);
    EXPECT_NE(output.find("prim_dup"), std::string::npos);
    ctx().set_input_stream(nullptr);
}

TEST_F(NewPrimitivesTest, SeeCompiledWord) {
    // Define ": double dup + ;" via interpreter
    interp->interpret_line(": double dup + ;");
    std::istringstream iss("double");
    ctx().set_input_stream(&iss);
    out.str("");
    ASSERT_TRUE(prim_see(ctx()));
    std::string output = out.str();
    EXPECT_NE(output.find(": double"), std::string::npos);
    EXPECT_NE(output.find("Call dup"), std::string::npos);
    EXPECT_NE(output.find("Call +"), std::string::npos);
    ctx().set_input_stream(nullptr);
}

TEST_F(NewPrimitivesTest, SeeUnknownWord) {
    std::istringstream iss("nonexistent_xyzzy");
    ctx().set_input_stream(&iss);
    out.str("");
    ASSERT_TRUE(prim_see(ctx()));
    std::string output = out.str();
    EXPECT_NE(output.find("Unknown word"), std::string::npos);
    ctx().set_input_stream(nullptr);
}

TEST_F(NewPrimitivesTest, SeeNoToken) {
    std::istringstream iss("");
    ctx().set_input_stream(&iss);
    out.str("");
    ASSERT_TRUE(prim_see(ctx()));
    std::string output = out.str();
    EXPECT_NE(output.find("Usage"), std::string::npos);
    ctx().set_input_stream(nullptr);
}

TEST_F(NewPrimitivesTest, SeeHandlerWord) {
    std::istringstream iss("if");
    ctx().set_input_stream(&iss);
    out.str("");
    ASSERT_TRUE(prim_see(ctx()));
    std::string output = out.str();
    EXPECT_NE(output.find("handler"), std::string::npos);
    EXPECT_NE(output.find("compile-only"), std::string::npos);
    ctx().set_input_stream(nullptr);
}

// ===== Stream redirection tests =====
// Verify I/O primitives write to the context's output stream, not std::cout

TEST_F(NewPrimitivesTest, DotWritesToContextStream) {
    ctx().data_stack().push(Value(int64_t(42)));
    out.str("");
    ASSERT_TRUE(prim_dot(ctx()));
    EXPECT_EQ(out.str(), "42 ");
}

TEST_F(NewPrimitivesTest, CrWritesToContextStream) {
    out.str("");
    ASSERT_TRUE(prim_cr(ctx()));
    EXPECT_EQ(out.str(), "\n");
}

TEST_F(NewPrimitivesTest, EmitWritesToContextStream) {
    ctx().data_stack().push(Value(int64_t(65)));  // 'A'
    out.str("");
    ASSERT_TRUE(prim_emit(ctx()));
    EXPECT_EQ(out.str(), "A");
}

TEST_F(NewPrimitivesTest, SpaceWritesToContextStream) {
    out.str("");
    ASSERT_TRUE(prim_space(ctx()));
    EXPECT_EQ(out.str(), " ");
}

TEST_F(NewPrimitivesTest, TypeWritesToContextStream) {
    out.str("");
    interp->interpret_line("s\" hello\" type");
    EXPECT_EQ(out.str(), "hello");
}

// ===== Time primitives =====

TEST_F(PrimitivesTest, TimeUs) {
    ASSERT_TRUE(prim_time_us(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Integer);
    // Should be after 2020-01-01 (~1577836800000000 us) and before 2040-01-01 (~2208988800000000 us)
    EXPECT_GT(result->as_int, int64_t(1577836800) * 1000000);
    EXPECT_LT(result->as_int, int64_t(2208988800) * 1000000);
}

TEST_F(PrimitivesTest, UsToIsoEpoch) {
    ctx.data_stack().push(Value(int64_t(0)));
    ASSERT_TRUE(prim_us_to_iso(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::String);
    auto* hs = result->as_string();
    EXPECT_EQ(std::string(hs->view()), "19700101T000000Z");
    result->release();
}

TEST_F(PrimitivesTest, UsToIsoUsEpoch) {
    ctx.data_stack().push(Value(int64_t(0)));
    ASSERT_TRUE(prim_us_to_iso_us(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::String);
    auto* hs = result->as_string();
    EXPECT_EQ(std::string(hs->view()), "19700101T000000.000000Z");
    result->release();
}

TEST_F(PrimitivesTest, UsToIsoUnderflow) {
    ASSERT_FALSE(prim_us_to_iso(ctx));
}

TEST_F(PrimitivesTest, UsToIsoUsUnderflow) {
    ASSERT_FALSE(prim_us_to_iso_us(ctx));
}

TEST_F(PrimitivesTest, UsToIsoUsFractional) {
    // 1 second + 123456 microseconds = 1123456 us
    ctx.data_stack().push(Value(int64_t(1123456)));
    ASSERT_TRUE(prim_us_to_iso_us(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    auto* hs = result->as_string();
    EXPECT_EQ(std::string(hs->view()), "19700101T000001.123456Z");
    result->release();
}

TEST_F(PrimitivesTest, UsToIsoKnownDate) {
    // 2026-02-15T12:00:00Z = 1771156800 seconds = 1771156800000000 us
    ctx.data_stack().push(Value(int64_t(1771156800) * 1000000));
    ASSERT_TRUE(prim_us_to_iso(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    auto* hs = result->as_string();
    EXPECT_EQ(std::string(hs->view()), "20260215T120000Z");
    result->release();
}

// ===== Julian Date primitives =====

TEST_F(PrimitivesTest, UsToJdEpoch) {
    // Unix epoch → JD 2440587.5
    ctx.data_stack().push(Value(int64_t(0)));
    ASSERT_TRUE(prim_us_to_jd(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Float);
    EXPECT_DOUBLE_EQ(result->as_float, 2440587.5);
}

TEST_F(PrimitivesTest, UsToMjdEpoch) {
    // Unix epoch → MJD 40587.0
    ctx.data_stack().push(Value(int64_t(0)));
    ASSERT_TRUE(prim_us_to_mjd(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Float);
    EXPECT_DOUBLE_EQ(result->as_float, 40587.0);
}

TEST_F(PrimitivesTest, UsToJdJ2000) {
    // J2000.0 = 2000-01-01T12:00:00Z = 946728000 seconds = 946728000000000 us
    ctx.data_stack().push(Value(int64_t(946728000000000)));
    ASSERT_TRUE(prim_us_to_jd(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->as_float, 2451545.0);
}

TEST_F(PrimitivesTest, UsToMjdJ2000) {
    // J2000.0 → MJD 51544.5
    ctx.data_stack().push(Value(int64_t(946728000000000)));
    ASSERT_TRUE(prim_us_to_mjd(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->as_float, 51544.5);
}

TEST_F(PrimitivesTest, JdToUsEpoch) {
    // JD 2440587.5 → 0 us
    ctx.data_stack().push(Value(2440587.5));
    ASSERT_TRUE(prim_jd_to_us(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Integer);
    EXPECT_EQ(result->as_int, 0);
}

TEST_F(PrimitivesTest, MjdToUsEpoch) {
    // MJD 40587.0 → 0 us
    ctx.data_stack().push(Value(40587.0));
    ASSERT_TRUE(prim_mjd_to_us(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Integer);
    EXPECT_EQ(result->as_int, 0);
}

TEST_F(PrimitivesTest, JdToUsJ2000) {
    // JD 2451545.0 → 946728000000000 us
    ctx.data_stack().push(Value(2451545.0));
    ASSERT_TRUE(prim_jd_to_us(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, int64_t(946728000000000));
}

TEST_F(PrimitivesTest, JdToUsIntegerPromotion) {
    // Integer JD 2440588 (noon 1970-01-01) → 43200000000 us
    ctx.data_stack().push(Value(int64_t(2440588)));
    ASSERT_TRUE(prim_jd_to_us(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Integer);
    EXPECT_EQ(result->as_int, int64_t(43200000000));
}

TEST_F(PrimitivesTest, MjdToUsIntegerPromotion) {
    // Integer MJD 40588 (noon 1970-01-02) → 129600000000 us (1.5 days)
    ctx.data_stack().push(Value(int64_t(40588)));
    ASSERT_TRUE(prim_mjd_to_us(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Integer);
    EXPECT_EQ(result->as_int, int64_t(86400000000));
}

TEST_F(PrimitivesTest, UsToJdUnderflow) {
    ASSERT_FALSE(prim_us_to_jd(ctx));
}

TEST_F(PrimitivesTest, UsToMjdUnderflow) {
    ASSERT_FALSE(prim_us_to_mjd(ctx));
}

TEST_F(PrimitivesTest, JdToUsUnderflow) {
    ASSERT_FALSE(prim_jd_to_us(ctx));
}

TEST_F(PrimitivesTest, MjdToUsUnderflow) {
    ASSERT_FALSE(prim_mjd_to_us(ctx));
}

TEST_F(PrimitivesTest, UsToJdWrongType) {
    // Float input to us->jd should fail, value restored
    ctx.data_stack().push(Value(3.14));
    ASSERT_FALSE(prim_us_to_jd(ctx));
    auto restored = ctx.data_stack().pop();
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->type, Value::Type::Float);
    EXPECT_DOUBLE_EQ(restored->as_float, 3.14);
}

TEST_F(PrimitivesTest, UsToMjdWrongType) {
    ctx.data_stack().push(Value(3.14));
    ASSERT_FALSE(prim_us_to_mjd(ctx));
    auto restored = ctx.data_stack().pop();
    ASSERT_TRUE(restored.has_value());
    EXPECT_DOUBLE_EQ(restored->as_float, 3.14);
}

TEST_F(PrimitivesTest, JdRoundTrip) {
    // Push microseconds, convert to JD, convert back — should match
    int64_t original = int64_t(946728000000000);  // J2000.0
    ctx.data_stack().push(Value(original));
    ASSERT_TRUE(prim_us_to_jd(ctx));
    ASSERT_TRUE(prim_jd_to_us(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, original);
}

TEST_F(PrimitivesTest, MjdRoundTrip) {
    int64_t original = int64_t(946728000000000);
    ctx.data_stack().push(Value(original));
    ASSERT_TRUE(prim_us_to_mjd(ctx));
    ASSERT_TRUE(prim_mjd_to_us(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, original);
}

TEST_F(PrimitivesTest, JdMjdRelationship) {
    // For the same instant, JD - 2400000.5 = MJD
    ctx.data_stack().push(Value(int64_t(0)));
    ASSERT_TRUE(prim_us_to_jd(ctx));
    auto jd_val = ctx.data_stack().pop();
    ASSERT_TRUE(jd_val.has_value());

    ctx.data_stack().push(Value(int64_t(0)));
    ASSERT_TRUE(prim_us_to_mjd(ctx));
    auto mjd_val = ctx.data_stack().pop();
    ASSERT_TRUE(mjd_val.has_value());

    EXPECT_DOUBLE_EQ(jd_val->as_float - 2400000.5, mjd_val->as_float);
}

TEST_F(PrimitivesTest, UsToJdPreEpoch) {
    // 1969-12-31T00:00:00Z = -86400000000 us → JD 2440586.5
    ctx.data_stack().push(Value(int64_t(-86400000000)));
    ASSERT_TRUE(prim_us_to_jd(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->as_float, 2440586.5);
}

// --- sys-notification ---

TEST_F(PrimitivesTest, SysNotificationString) {
    auto* hs = HeapString::create("hello from TIL");
    ctx.data_stack().push(Value::from(hs));
    ASSERT_TRUE(prim_sys_notification(ctx));
    auto notes = ctx.drain_notifications();
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0], "hello from TIL");
    // Stack should be empty
    EXPECT_FALSE(ctx.data_stack().pop().has_value());
}

TEST_F(PrimitivesTest, SysNotificationInteger) {
    ctx.data_stack().push(Value(int64_t(42)));
    ASSERT_TRUE(prim_sys_notification(ctx));
    auto notes = ctx.drain_notifications();
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0], "42");
}

TEST_F(PrimitivesTest, SysNotificationUnderflow) {
    ASSERT_FALSE(prim_sys_notification(ctx));
    auto notes = ctx.drain_notifications();
    EXPECT_TRUE(notes.empty());
}

TEST_F(PrimitivesTest, SysNotificationMultiple) {
    auto* hs1 = HeapString::create("first");
    ctx.data_stack().push(Value::from(hs1));
    ASSERT_TRUE(prim_sys_notification(ctx));

    auto* hs2 = HeapString::create("second");
    ctx.data_stack().push(Value::from(hs2));
    ASSERT_TRUE(prim_sys_notification(ctx));

    auto notes = ctx.drain_notifications();
    ASSERT_EQ(notes.size(), 2u);
    EXPECT_EQ(notes[0], "first");
    EXPECT_EQ(notes[1], "second");

    // drain_notifications clears the queue
    auto notes2 = ctx.drain_notifications();
    EXPECT_TRUE(notes2.empty());
}

TEST_F(PrimitivesTest, SysNotificationPermissionDenied) {
    etil::mcp::RolePermissions perms;
    perms.send_system_notification = false;
    ctx.set_permissions(&perms);

    auto* hs = HeapString::create("blocked");
    ctx.data_stack().push(Value::from(hs));
    ASSERT_FALSE(prim_sys_notification(ctx));

    // Notification should not have been queued
    auto notes = ctx.drain_notifications();
    EXPECT_TRUE(notes.empty());

    // Value should still be on stack (not consumed since check is before pop)
    auto val = ctx.data_stack().pop();
    EXPECT_TRUE(val.has_value());
    val->release();

    ctx.set_permissions(nullptr);
}

TEST_F(PrimitivesTest, SysNotificationPermissionAllowed) {
    etil::mcp::RolePermissions perms;
    perms.send_system_notification = true;
    ctx.set_permissions(&perms);

    auto* hs = HeapString::create("allowed");
    ctx.data_stack().push(Value::from(hs));
    ASSERT_TRUE(prim_sys_notification(ctx));

    auto notes = ctx.drain_notifications();
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0], "allowed");

    ctx.set_permissions(nullptr);
}

// --- user-notification ---

TEST_F(PrimitivesTest, UserNotificationNoSender) {
    // Without a targeted sender wired, should push false (0)
    auto* msg = HeapString::create("hello");
    auto* uid = HeapString::create("github:12345");
    ctx.data_stack().push(Value::from(msg));
    ctx.data_stack().push(Value::from(uid));
    ASSERT_TRUE(prim_user_notification(ctx));
    auto flag = ctx.data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->as_int, 0);  // false — no sender
}

TEST_F(PrimitivesTest, UserNotificationWithSender) {
    std::string captured_user;
    std::string captured_msg;
    ctx.set_targeted_notification_sender(
        [&](const std::string& user_id, const std::string& msg) -> bool {
            captured_user = user_id;
            captured_msg = msg;
            return true;
        });

    auto* msg = HeapString::create("test message");
    auto* uid = HeapString::create("github:99");
    ctx.data_stack().push(Value::from(msg));
    ctx.data_stack().push(Value::from(uid));
    ASSERT_TRUE(prim_user_notification(ctx));

    auto flag = ctx.data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->as_int, -1);  // true
    EXPECT_EQ(captured_user, "github:99");
    EXPECT_EQ(captured_msg, "test message");

    ctx.clear_targeted_notification_sender();
}

TEST_F(PrimitivesTest, UserNotificationPermissionDenied) {
    etil::mcp::RolePermissions perms;
    perms.send_user_notification = false;
    ctx.set_permissions(&perms);

    auto* msg = HeapString::create("blocked");
    auto* uid = HeapString::create("github:1");
    ctx.data_stack().push(Value::from(msg));
    ctx.data_stack().push(Value::from(uid));
    ASSERT_FALSE(prim_user_notification(ctx));

    // Both strings still on stack (not consumed since check is before pop)
    auto v1 = ctx.data_stack().pop();
    auto v2 = ctx.data_stack().pop();
    EXPECT_TRUE(v1.has_value());
    EXPECT_TRUE(v2.has_value());
    v1->release();
    v2->release();

    ctx.set_permissions(nullptr);
}

TEST_F(PrimitivesTest, UserNotificationUnderflow) {
    // Only one value — should fail
    auto* msg = HeapString::create("hello");
    ctx.data_stack().push(Value::from(msg));
    ASSERT_FALSE(prim_user_notification(ctx));
    // Cleanup: msg was popped as user_id, then underflow on second pop
    // The first pop consumed the value, so stack is empty
}

TEST_F(PrimitivesTest, UserNotificationNonString) {
    ctx.data_stack().push(Value(int64_t(42)));
    auto* uid = HeapString::create("github:1");
    ctx.data_stack().push(Value::from(uid));
    ASSERT_FALSE(prim_user_notification(ctx));
}

// ===== Sleep primitive =====

TEST_F(PrimitivesTest, SleepBasic) {
    // Sleep 1ms, verify ~1ms elapsed
    ctx.data_stack().push(Value(int64_t(1000)));  // 1000 us = 1 ms
    auto before = std::chrono::steady_clock::now();
    ASSERT_TRUE(prim_sleep(ctx));
    auto elapsed = std::chrono::steady_clock::now() - before;
    EXPECT_GE(elapsed, std::chrono::microseconds(900));
    EXPECT_EQ(ctx.data_stack().size(), 0u);
}

TEST_F(PrimitivesTest, SleepZero) {
    ctx.data_stack().push(Value(int64_t(0)));
    ASSERT_TRUE(prim_sleep(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 0u);
}

TEST_F(PrimitivesTest, SleepNegative) {
    ctx.data_stack().push(Value(int64_t(-1)));
    ASSERT_TRUE(prim_sleep(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 0u);
}

TEST_F(PrimitivesTest, SleepDeadlineExceeded) {
    // Set a 10ms deadline, try to sleep 1 second
    ctx.set_limits(UINT64_MAX, SIZE_MAX, SIZE_MAX, 0.01);
    ctx.data_stack().push(Value(int64_t(1000000)));  // 1s = 1000000us
    std::ostringstream err_stream;
    ctx.set_err(&err_stream);
    ASSERT_FALSE(prim_sleep(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 0u);
    EXPECT_NE(err_stream.str().find("exceed execution deadline"), std::string::npos);
}

TEST_F(PrimitivesTest, SleepWrongType) {
    auto* hs = HeapString::create("not a number");
    ctx.data_stack().push(Value::from(hs));
    ASSERT_FALSE(prim_sleep(ctx));
    // String should be restored on stack
    EXPECT_EQ(ctx.data_stack().size(), 1u);
    auto restored = ctx.data_stack().pop();
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->type, Value::Type::String);
    restored->release();
}

TEST_F(PrimitivesTest, SleepFloat) {
    // Float input: 1000.5 us → truncated to 1000 us
    ctx.data_stack().push(Value(1000.5));
    auto before = std::chrono::steady_clock::now();
    ASSERT_TRUE(prim_sleep(ctx));
    auto elapsed = std::chrono::steady_clock::now() - before;
    EXPECT_GE(elapsed, std::chrono::microseconds(900));
    EXPECT_EQ(ctx.data_stack().size(), 0u);
}

TEST_F(PrimitivesTest, SleepUnderflow) {
    ASSERT_FALSE(prim_sleep(ctx));
}

// --- PRNG ---

TEST_F(PrimitivesTest, RandomReturnFloat) {
    ASSERT_TRUE(prim_random(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Float);
    EXPECT_GE(result->as_float, 0.0);
    EXPECT_LT(result->as_float, 1.0);
}

TEST_F(PrimitivesTest, RandomRepeatedCallsDiffer) {
    // Seed with a known value to avoid the unlikely case of identical outputs
    ctx.data_stack().push(Value(int64_t(12345)));
    ASSERT_TRUE(prim_random_seed(ctx));

    ASSERT_TRUE(prim_random(ctx));
    ASSERT_TRUE(prim_random(ctx));
    auto b = ctx.data_stack().pop();
    auto a = ctx.data_stack().pop();
    ASSERT_TRUE(a.has_value() && b.has_value());
    EXPECT_NE(a->as_float, b->as_float);
}

TEST_F(PrimitivesTest, RandomSeedDeterministic) {
    ctx.data_stack().push(Value(int64_t(42)));
    ASSERT_TRUE(prim_random_seed(ctx));
    ASSERT_TRUE(prim_random(ctx));
    auto first = ctx.data_stack().pop();

    ctx.data_stack().push(Value(int64_t(42)));
    ASSERT_TRUE(prim_random_seed(ctx));
    ASSERT_TRUE(prim_random(ctx));
    auto second = ctx.data_stack().pop();

    ASSERT_TRUE(first.has_value() && second.has_value());
    EXPECT_DOUBLE_EQ(first->as_float, second->as_float);
}

TEST_F(PrimitivesTest, RandomSeedUnderflow) {
    ASSERT_FALSE(prim_random_seed(ctx));
}

TEST_F(PrimitivesTest, RandomRangeInRange) {
    ctx.data_stack().push(Value(int64_t(10)));
    ctx.data_stack().push(Value(int64_t(20)));
    ASSERT_TRUE(prim_random_range(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Integer);
    EXPECT_GE(result->as_int, 10);
    EXPECT_LT(result->as_int, 20);
}

TEST_F(PrimitivesTest, RandomRangeRepeated) {
    // Run 50 times, all results should be in [0, 5)
    ctx.data_stack().push(Value(int64_t(99)));
    ASSERT_TRUE(prim_random_seed(ctx));
    for (int i = 0; i < 50; ++i) {
        ctx.data_stack().push(Value(int64_t(0)));
        ctx.data_stack().push(Value(int64_t(5)));
        ASSERT_TRUE(prim_random_range(ctx));
        auto result = ctx.data_stack().pop();
        ASSERT_TRUE(result.has_value());
        EXPECT_GE(result->as_int, 0);
        EXPECT_LT(result->as_int, 5);
    }
}

TEST_F(PrimitivesTest, RandomRangeInvalidRange) {
    // lo == hi
    ctx.data_stack().push(Value(int64_t(5)));
    ctx.data_stack().push(Value(int64_t(5)));
    ASSERT_FALSE(prim_random_range(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 2u);
    ctx.data_stack().pop();
    ctx.data_stack().pop();

    // lo > hi
    ctx.data_stack().push(Value(int64_t(10)));
    ctx.data_stack().push(Value(int64_t(3)));
    ASSERT_FALSE(prim_random_range(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 2u);
    ctx.data_stack().pop();
    ctx.data_stack().pop();
}

TEST_F(PrimitivesTest, RandomRangeUnderflow) {
    // Empty stack
    ASSERT_FALSE(prim_random_range(ctx));

    // Only one value
    ctx.data_stack().push(Value(int64_t(5)));
    ASSERT_FALSE(prim_random_range(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 1u);
}

// --- Execution token primitives ---

class XtPrimitivesTest : public ::testing::Test {
protected:
    Dictionary dict;
    ExecutionContext ctx{0};
    std::ostringstream out;
    std::ostringstream err;

    void SetUp() override {
        register_primitives(dict);
        ctx.set_dictionary(&dict);
        ctx.set_out(&out);
        ctx.set_err(&err);
    }
};

TEST_F(XtPrimitivesTest, TickPushesXt) {
    std::istringstream iss("dup");
    ctx.set_input_stream(&iss);
    ASSERT_TRUE(prim_tick(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Xt);
    EXPECT_NE(result->as_ptr, nullptr);
    auto* impl = result->as_xt_impl();
    EXPECT_EQ(impl->name(), "prim_dup");
    result->release();
    ctx.set_input_stream(nullptr);
}

TEST_F(XtPrimitivesTest, TickUnknownWord) {
    std::istringstream iss("nonexistent_word_xyz");
    ctx.set_input_stream(&iss);
    ASSERT_FALSE(prim_tick(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 0u);
    ctx.set_input_stream(nullptr);
}

TEST_F(XtPrimitivesTest, TickNoInput) {
    std::istringstream iss("");
    ctx.set_input_stream(&iss);
    ASSERT_FALSE(prim_tick(ctx));
    ctx.set_input_stream(nullptr);
}

TEST_F(XtPrimitivesTest, ExecuteNativeWord) {
    // Push 42, then push xt for dup, execute it → should duplicate 42
    ctx.data_stack().push(Value(int64_t(42)));
    std::istringstream iss("dup");
    ctx.set_input_stream(&iss);
    ASSERT_TRUE(prim_tick(ctx));
    ctx.set_input_stream(nullptr);
    ASSERT_TRUE(prim_execute(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 2u);
    auto top = ctx.data_stack().pop();
    auto second = ctx.data_stack().pop();
    EXPECT_EQ(top->as_int, 42);
    EXPECT_EQ(second->as_int, 42);
}

TEST_F(XtPrimitivesTest, ExecuteNonXtFails) {
    ctx.data_stack().push(Value(int64_t(42)));
    ASSERT_FALSE(prim_execute(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 0u);
}

TEST_F(XtPrimitivesTest, ExecuteEmptyStackFails) {
    ASSERT_FALSE(prim_execute(ctx));
}

TEST_F(XtPrimitivesTest, XtQueryTrue) {
    std::istringstream iss("dup");
    ctx.set_input_stream(&iss);
    ASSERT_TRUE(prim_tick(ctx));
    ctx.set_input_stream(nullptr);
    ASSERT_TRUE(prim_xt_query(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Boolean);
    EXPECT_TRUE(result->as_bool());
}

TEST_F(XtPrimitivesTest, XtQueryFalseForInt) {
    ctx.data_stack().push(Value(int64_t(42)));
    ASSERT_TRUE(prim_xt_query(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Boolean);
    EXPECT_FALSE(result->as_bool());
}

TEST_F(XtPrimitivesTest, XtQueryFalseForString) {
    auto* hs = HeapString::create("hello");
    ctx.data_stack().push(Value::from(hs));
    ASSERT_TRUE(prim_xt_query(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Boolean);
    EXPECT_FALSE(result->as_bool());
}

TEST_F(XtPrimitivesTest, XtToNameReturnsString) {
    std::istringstream iss("dup");
    ctx.set_input_stream(&iss);
    ASSERT_TRUE(prim_tick(ctx));
    ctx.set_input_stream(nullptr);
    ASSERT_TRUE(prim_xt_to_name(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::String);
    auto* hs = result->as_string();
    EXPECT_EQ(std::string(hs->view()), "prim_dup");
    result->release();
}

TEST_F(XtPrimitivesTest, XtToNameNonXtFails) {
    ctx.data_stack().push(Value(int64_t(42)));
    ASSERT_FALSE(prim_xt_to_name(ctx));
}

TEST_F(XtPrimitivesTest, XtDupRefcount) {
    // Tick pushes xt (refcount = initial + 1 from tick addref)
    std::istringstream iss("dup");
    ctx.set_input_stream(&iss);
    ASSERT_TRUE(prim_tick(ctx));
    ctx.set_input_stream(nullptr);
    // Dup the xt (should addref)
    ASSERT_TRUE(prim_dup(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 2u);
    // Drop both — should not crash
    ASSERT_TRUE(prim_drop(ctx));
    ASSERT_TRUE(prim_drop(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 0u);
}

TEST_F(XtPrimitivesTest, ExecuteCompiledWord) {
    // Define a compiled word: ": double-it 2 * ;"
    Interpreter interp(dict, out, err);
    interp.interpret_line(": double-it 2 * ;");

    // Get its xt via tick
    std::istringstream iss("double-it");
    interp.context().set_input_stream(&iss);
    interp.context().data_stack().push(Value(int64_t(21)));
    ASSERT_TRUE(prim_tick(interp.context()));
    interp.context().set_input_stream(nullptr);

    // Execute it
    ASSERT_TRUE(prim_execute(interp.context()));
    auto result = interp.context().data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, 42);
    interp.shutdown();
}

// --- .s (non-destructive stack display) ---

TEST_F(XtPrimitivesTest, DotSEmpty) {
    ASSERT_TRUE(prim_dot_s(ctx));
    EXPECT_EQ(out.str(), "<0>\n");
    EXPECT_EQ(ctx.data_stack().size(), 0u);
}

TEST_F(XtPrimitivesTest, DotSIntegers) {
    ctx.data_stack().push(Value(int64_t(1)));
    ctx.data_stack().push(Value(int64_t(2)));
    ctx.data_stack().push(Value(int64_t(3)));
    ASSERT_TRUE(prim_dot_s(ctx));
    EXPECT_EQ(out.str(), "<3> 1 2 3\n");
    // Verify stack unchanged
    EXPECT_EQ(ctx.data_stack().size(), 3u);
    auto v = ctx.data_stack().pop();
    EXPECT_EQ(v->as_int, 3);
    v = ctx.data_stack().pop();
    EXPECT_EQ(v->as_int, 2);
    v = ctx.data_stack().pop();
    EXPECT_EQ(v->as_int, 1);
}

TEST_F(XtPrimitivesTest, DotSMixedTypes) {
    ctx.data_stack().push(Value(int64_t(42)));
    ctx.data_stack().push(Value(3.14));
    auto* hs = HeapString::create("hello");
    ctx.data_stack().push(Value::from(hs));
    ASSERT_TRUE(prim_dot_s(ctx));
    EXPECT_EQ(out.str(), "<3> 42 3.14 hello\n");
    EXPECT_EQ(ctx.data_stack().size(), 3u);
    // Clean up
    ctx.data_stack().pop()->release();
    ctx.data_stack().pop();
    ctx.data_stack().pop();
}

// --- int->float ---

TEST_F(PrimitivesTest, IntToFloatConverts) {
    ctx.data_stack().push(Value(int64_t(42)));
    ASSERT_TRUE(prim_int_to_float(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Float);
    EXPECT_DOUBLE_EQ(result->as_float, 42.0);
}

TEST_F(PrimitivesTest, IntToFloatPassthrough) {
    ctx.data_stack().push(Value(3.14));
    ASSERT_TRUE(prim_int_to_float(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Float);
    EXPECT_DOUBLE_EQ(result->as_float, 3.14);
}

TEST_F(PrimitivesTest, IntToFloatErrorOnString) {
    auto* hs = HeapString::create("hello");
    ctx.data_stack().push(Value::from(hs));
    ASSERT_FALSE(prim_int_to_float(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 0u);
}

TEST_F(PrimitivesTest, IntToFloatUnderflow) {
    ASSERT_FALSE(prim_int_to_float(ctx));
}

// --- float->int ---

TEST_F(PrimitivesTest, FloatToIntTruncates) {
    ctx.data_stack().push(Value(3.7));
    ASSERT_TRUE(prim_float_to_int(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Integer);
    EXPECT_EQ(result->as_int, 3);
}

TEST_F(PrimitivesTest, FloatToIntNegative) {
    ctx.data_stack().push(Value(-3.7));
    ASSERT_TRUE(prim_float_to_int(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Integer);
    EXPECT_EQ(result->as_int, -3);
}

TEST_F(PrimitivesTest, FloatToIntPassthrough) {
    ctx.data_stack().push(Value(int64_t(42)));
    ASSERT_TRUE(prim_float_to_int(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::Integer);
    EXPECT_EQ(result->as_int, 42);
}

TEST_F(PrimitivesTest, FloatToIntErrorOnString) {
    auto* hs = HeapString::create("hello");
    ctx.data_stack().push(Value::from(hs));
    ASSERT_FALSE(prim_float_to_int(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 0u);
}

// --- number->string ---

TEST_F(PrimitivesTest, NumberToStringInt) {
    ctx.data_stack().push(Value(int64_t(42)));
    ASSERT_TRUE(prim_number_to_string(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::String);
    EXPECT_EQ(std::string(result->as_string()->view()), "42");
    result->release();
}

TEST_F(PrimitivesTest, NumberToStringFloat) {
    ctx.data_stack().push(Value(3.14));
    ASSERT_TRUE(prim_number_to_string(ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, Value::Type::String);
    EXPECT_EQ(std::string(result->as_string()->view()), "3.14");
    result->release();
}

TEST_F(PrimitivesTest, NumberToStringErrorOnString) {
    auto* hs = HeapString::create("hello");
    ctx.data_stack().push(Value::from(hs));
    ASSERT_FALSE(prim_number_to_string(ctx));
    EXPECT_EQ(ctx.data_stack().size(), 0u);
}

// --- string->number ---

TEST_F(PrimitivesTest, StringToNumberInt) {
    auto* hs = HeapString::create("42");
    ctx.data_stack().push(Value::from(hs));
    ASSERT_TRUE(prim_string_to_number(ctx));
    auto flag = ctx.data_stack().pop();
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_TRUE(flag->as_bool());
    auto val = ctx.data_stack().pop();
    EXPECT_EQ(val->type, Value::Type::Integer);
    EXPECT_EQ(val->as_int, 42);
}

TEST_F(PrimitivesTest, StringToNumberFloat) {
    auto* hs = HeapString::create("3.14");
    ctx.data_stack().push(Value::from(hs));
    ASSERT_TRUE(prim_string_to_number(ctx));
    auto flag = ctx.data_stack().pop();
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_TRUE(flag->as_bool());
    auto val = ctx.data_stack().pop();
    EXPECT_EQ(val->type, Value::Type::Float);
    EXPECT_DOUBLE_EQ(val->as_float, 3.14);
}

TEST_F(PrimitivesTest, StringToNumberInvalid) {
    auto* hs = HeapString::create("hello");
    ctx.data_stack().push(Value::from(hs));
    ASSERT_TRUE(prim_string_to_number(ctx));
    auto flag = ctx.data_stack().pop();
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_FALSE(flag->as_bool());
    EXPECT_EQ(ctx.data_stack().size(), 0u);
}

TEST_F(PrimitivesTest, StringToNumberEmpty) {
    auto* hs = HeapString::create("");
    ctx.data_stack().push(Value::from(hs));
    ASSERT_TRUE(prim_string_to_number(ctx));
    auto flag = ctx.data_stack().pop();
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_FALSE(flag->as_bool());
}

TEST_F(PrimitivesTest, StringToNumberNonStringFails) {
    ctx.data_stack().push(Value(int64_t(42)));
    ASSERT_FALSE(prim_string_to_number(ctx));
}

TEST_F(PrimitivesTest, StringToNumberNegativeInt) {
    auto* hs = HeapString::create("-99");
    ctx.data_stack().push(Value::from(hs));
    ASSERT_TRUE(prim_string_to_number(ctx));
    auto flag = ctx.data_stack().pop();
    EXPECT_EQ(flag->type, Value::Type::Boolean);
    EXPECT_TRUE(flag->as_bool());
    auto val = ctx.data_stack().pop();
    EXPECT_EQ(val->type, Value::Type::Integer);
    EXPECT_EQ(val->as_int, -99);
}

// --- abort ---

TEST_F(PrimitivesTest, AbortSuccessTrue) {
    ctx.data_stack().push(Value(int64_t(-1)));  // true flag
    ASSERT_TRUE(prim_abort(ctx));
    EXPECT_TRUE(ctx.abort_requested());
    EXPECT_TRUE(ctx.abort_success());
    EXPECT_TRUE(ctx.is_cancelled());
    ctx.clear_abort();
    ctx.reset_limits();
}

TEST_F(PrimitivesTest, AbortErrorWithMessage) {
    auto* hs = HeapString::create("something broke");
    ctx.data_stack().push(Value::from(hs));
    ctx.data_stack().push(Value(int64_t(0)));  // false flag
    ASSERT_TRUE(prim_abort(ctx));
    EXPECT_TRUE(ctx.abort_requested());
    EXPECT_FALSE(ctx.abort_success());
    EXPECT_EQ(ctx.abort_error_message(), "something broke");
    EXPECT_TRUE(ctx.is_cancelled());
    ctx.clear_abort();
    ctx.reset_limits();
}

TEST_F(PrimitivesTest, AbortErrorNoMessage) {
    // Only false flag on stack, no error message
    ctx.data_stack().push(Value(int64_t(0)));
    ASSERT_TRUE(prim_abort(ctx));
    EXPECT_TRUE(ctx.abort_requested());
    EXPECT_FALSE(ctx.abort_success());
    EXPECT_EQ(ctx.abort_error_message(), "abort");  // default message
    ctx.clear_abort();
    ctx.reset_limits();
}

TEST_F(PrimitivesTest, AbortUnderflow) {
    ASSERT_FALSE(prim_abort(ctx));
    EXPECT_FALSE(ctx.abort_requested());
}

TEST_F(PrimitivesTest, AbortStopsRemainingTokens) {
    // Use interpreter to verify remaining tokens are not executed
    Dictionary dict;
    register_primitives(dict);
    std::ostringstream out, err;
    Interpreter interp(dict, out, err);
    interp.register_handler_words();

    // Use -1 (FORTH true) instead of 'true' word (requires builtins.til)
    interp.interpret_line("42 -1 abort 99");

    // abort_requested should be set
    EXPECT_TRUE(interp.context().abort_requested());
    EXPECT_TRUE(interp.context().abort_success());

    // 42 should be on stack, 99 should NOT
    EXPECT_EQ(interp.context().data_stack().size(), 1u);
    auto top = interp.context().data_stack().pop();
    ASSERT_TRUE(top.has_value());
    EXPECT_EQ(top->as_int, 42);

    // No error messages should be printed (abort suppresses "execution limit reached")
    EXPECT_TRUE(err.str().empty());

    interp.context().clear_abort();
    interp.context().reset_limits();
    interp.shutdown();
}
