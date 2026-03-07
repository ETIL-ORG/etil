// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/heap_object.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_byte_array.hpp"
#include "etil/core/execution_context.hpp"
#include <gtest/gtest.h>

using namespace etil::core;

// --- HeapObject lifecycle ---

TEST(HeapObjectTest, StringRefcountStartsAtOne) {
    auto* hs = HeapString::create("test");
    EXPECT_EQ(hs->refcount(), 1u);
    hs->release();
}

TEST(HeapObjectTest, AddRefIncrementsRefcount) {
    auto* hs = HeapString::create("test");
    hs->add_ref();
    EXPECT_EQ(hs->refcount(), 2u);
    hs->release();
    EXPECT_EQ(hs->refcount(), 1u);
    hs->release();  // deletes
}

TEST(HeapObjectTest, ReleaseDeletesAtZero) {
    // Just verify no crash/ASan error
    auto* hs = HeapString::create("hello");
    hs->add_ref();
    hs->add_ref();
    EXPECT_EQ(hs->refcount(), 3u);
    hs->release();
    hs->release();
    hs->release();  // should delete
}

TEST(HeapObjectTest, KindTagging) {
    auto* s = HeapString::create("x");
    EXPECT_EQ(s->kind(), HeapObject::Kind::String);
    s->release();

    auto* a = new HeapArray();
    EXPECT_EQ(a->kind(), HeapObject::Kind::Array);
    a->release();

    auto* b = new HeapByteArray(10);
    EXPECT_EQ(b->kind(), HeapObject::Kind::ByteArray);
    b->release();
}

// --- Value helpers ---

TEST(HeapObjectTest, MakeHeapValueString) {
    auto* hs = HeapString::create("test");
    Value v = Value::from(hs);
    EXPECT_EQ(v.type, Value::Type::String);
    EXPECT_EQ(v.as_ptr, static_cast<void*>(hs));
    EXPECT_TRUE(is_heap_value(v));
    EXPECT_EQ(as_heap_object(v), hs);
    hs->release();
}

TEST(HeapObjectTest, MakeHeapValueArray) {
    auto* a = new HeapArray();
    Value v = Value::from(a);
    EXPECT_EQ(v.type, Value::Type::Array);
    EXPECT_TRUE(is_heap_value(v));
    a->release();
}

TEST(HeapObjectTest, MakeHeapValueByteArray) {
    auto* b = new HeapByteArray(5);
    Value v = Value::from(b);
    EXPECT_EQ(v.type, Value::Type::ByteArray);
    EXPECT_TRUE(is_heap_value(v));
    b->release();
}

TEST(HeapObjectTest, IsHeapValueNonHeap) {
    Value i(int64_t(42));
    EXPECT_FALSE(is_heap_value(i));
    Value f(3.14);
    EXPECT_FALSE(is_heap_value(f));
    Value dr = make_dataref(0, 0);
    EXPECT_FALSE(is_heap_value(dr));
}

TEST(HeapObjectTest, ValueAddrefRelease) {
    auto* hs = HeapString::create("test");
    Value v = Value::from(hs);
    v.addref();
    EXPECT_EQ(hs->refcount(), 2u);
    v.release();
    EXPECT_EQ(hs->refcount(), 1u);
    v.release();  // deletes
}

TEST(HeapObjectTest, ValueAddrefReleaseNonHeap) {
    // Should be no-ops, just verify no crash
    Value i(int64_t(42));
    i.addref();
    i.release();
}

// --- Stack primitives with heap values ---

TEST(HeapObjectTest, DupAddsRef) {
    ExecutionContext ctx(0);
    auto* hs = HeapString::create("test");
    ctx.data_stack().push(Value::from(hs));
    // refcount = 1 (from create; push is POD copy, not addref)

    // dup: top() returns POD copy, addref, push copy
    auto opt = ctx.data_stack().top();
    ASSERT_TRUE(opt.has_value());
    opt->addref();  // simulating what prim_dup does
    ctx.data_stack().push(*opt);

    EXPECT_EQ(hs->refcount(), 2u);  // 1 original + 1 from dup's addref

    // Clean up: pop both, release both (one value, two refs)
    auto v1 = ctx.data_stack().pop();
    v1->release();
    auto v2 = ctx.data_stack().pop();
    v2->release();
    // hs should be deleted now (refcount went 2 -> 1 -> 0)
}

TEST(HeapObjectTest, DropReleasesRef) {
    ExecutionContext ctx(0);
    auto* hs = HeapString::create("test");
    hs->add_ref();  // give us an extra ref so we can check
    ctx.data_stack().push(Value::from(hs));

    EXPECT_EQ(hs->refcount(), 2u);
    auto opt = ctx.data_stack().pop();
    opt->release();
    EXPECT_EQ(hs->refcount(), 1u);
    hs->release();  // final cleanup
}

// --- HeapString ---

TEST(HeapStringTest, CreateAndAccess) {
    auto* hs = HeapString::create("hello world");
    EXPECT_EQ(hs->length(), 11u);
    EXPECT_STREQ(hs->c_str(), "hello world");
    EXPECT_EQ(hs->view(), "hello world");
    hs->release();
}

TEST(HeapStringTest, EmptyString) {
    auto* hs = HeapString::create("");
    EXPECT_EQ(hs->length(), 0u);
    EXPECT_STREQ(hs->c_str(), "");
    EXPECT_EQ(hs->view(), "");
    hs->release();
}

TEST(HeapStringTest, NullTerminated) {
    auto* hs = HeapString::create("abc");
    EXPECT_EQ(hs->c_str()[3], '\0');
    hs->release();
}

TEST(HeapStringTest, BinaryData) {
    std::string_view sv("ab\0cd", 5);
    auto* hs = HeapString::create(sv);
    EXPECT_EQ(hs->length(), 5u);
    EXPECT_EQ(hs->view(), sv);
    hs->release();
}

// --- HeapArray ---

TEST(HeapArrayTest, EmptyArray) {
    auto* arr = new HeapArray();
    EXPECT_EQ(arr->length(), 0u);
    arr->release();
}

TEST(HeapArrayTest, PushBackPopBack) {
    auto* arr = new HeapArray();
    arr->push_back(Value(int64_t(10)));
    arr->push_back(Value(int64_t(20)));
    arr->push_back(Value(int64_t(30)));
    EXPECT_EQ(arr->length(), 3u);

    Value v;
    ASSERT_TRUE(arr->pop_back(v));
    EXPECT_EQ(v.as_int, 30);
    ASSERT_TRUE(arr->pop_back(v));
    EXPECT_EQ(v.as_int, 20);
    ASSERT_TRUE(arr->pop_back(v));
    EXPECT_EQ(v.as_int, 10);
    EXPECT_FALSE(arr->pop_back(v));
    arr->release();
}

TEST(HeapArrayTest, GetSet) {
    auto* arr = new HeapArray();
    arr->push_back(Value(int64_t(1)));
    arr->push_back(Value(int64_t(2)));
    arr->push_back(Value(int64_t(3)));

    Value v;
    ASSERT_TRUE(arr->get(1, v));
    EXPECT_EQ(v.as_int, 2);

    ASSERT_TRUE(arr->set(1, Value(int64_t(99))));
    ASSERT_TRUE(arr->get(1, v));
    EXPECT_EQ(v.as_int, 99);

    EXPECT_FALSE(arr->get(10, v));  // out of bounds
    EXPECT_FALSE(arr->set(10, Value(int64_t(0))));  // out of bounds
    arr->release();
}

TEST(HeapArrayTest, ShiftUnshift) {
    auto* arr = new HeapArray();
    arr->push_back(Value(int64_t(1)));
    arr->push_back(Value(int64_t(2)));

    arr->unshift(Value(int64_t(0)));
    EXPECT_EQ(arr->length(), 3u);

    Value v;
    ASSERT_TRUE(arr->get(0, v));
    EXPECT_EQ(v.as_int, 0);

    ASSERT_TRUE(arr->shift(v));
    EXPECT_EQ(v.as_int, 0);
    EXPECT_EQ(arr->length(), 2u);
    arr->release();
}

TEST(HeapArrayTest, DestructorReleasesHeapElements) {
    auto* inner = HeapString::create("test");
    inner->add_ref();  // extra ref so we can check after array dies
    EXPECT_EQ(inner->refcount(), 2u);

    auto* arr = new HeapArray();
    arr->push_back(Value::from(inner));  // array owns one ref
    // inner now has refcount 2 (our extra + array)

    arr->release();  // should release the string element
    EXPECT_EQ(inner->refcount(), 1u);
    inner->release();
}

// --- HeapByteArray ---

TEST(HeapByteArrayTest, CreateZeroed) {
    auto* ba = new HeapByteArray(10);
    EXPECT_EQ(ba->length(), 10u);
    for (size_t i = 0; i < 10; ++i) {
        uint8_t v;
        ASSERT_TRUE(ba->get(i, v));
        EXPECT_EQ(v, 0u);
    }
    ba->release();
}

TEST(HeapByteArrayTest, GetSet) {
    auto* ba = new HeapByteArray(4);
    ASSERT_TRUE(ba->set(0, 0xAB));
    ASSERT_TRUE(ba->set(3, 0xCD));

    uint8_t v;
    ASSERT_TRUE(ba->get(0, v));
    EXPECT_EQ(v, 0xAB);
    ASSERT_TRUE(ba->get(3, v));
    EXPECT_EQ(v, 0xCD);

    EXPECT_FALSE(ba->get(4, v));   // out of bounds
    EXPECT_FALSE(ba->set(4, 0));   // out of bounds
    ba->release();
}

TEST(HeapByteArrayTest, Resize) {
    auto* ba = new HeapByteArray(2);
    ba->set(0, 0x11);
    ba->set(1, 0x22);

    ba->resize(4);
    EXPECT_EQ(ba->length(), 4u);
    uint8_t v;
    ba->get(0, v); EXPECT_EQ(v, 0x11);
    ba->get(1, v); EXPECT_EQ(v, 0x22);
    ba->get(2, v); EXPECT_EQ(v, 0);
    ba->get(3, v); EXPECT_EQ(v, 0);

    ba->resize(1);
    EXPECT_EQ(ba->length(), 1u);
    ba->get(0, v); EXPECT_EQ(v, 0x11);
    ba->release();
}
