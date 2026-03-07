// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/json_primitives.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/heap_json.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_map.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/interpreter.hpp"
#include <gtest/gtest.h>
#include <sstream>

using namespace etil::core;

class JsonPrimitivesTest : public ::testing::Test {
protected:
    Dictionary dict;
    std::ostringstream out;
    Interpreter interp{dict, out};

    void SetUp() override {
        register_primitives(dict);
    }

    void run(const std::string& code) {
        out.str("");
        interp.interpret_line(code);
    }

    ExecutionContext& ctx() { return interp.context(); }
};

// --- HeapJson construction ---

TEST_F(JsonPrimitivesTest, HeapJsonConstruction) {
    nlohmann::json j = {{"key", "value"}, {"n", 42}};
    auto* hj = new HeapJson(j);
    EXPECT_EQ(hj->kind(), HeapObject::Kind::Json);
    EXPECT_EQ(hj->refcount(), 1u);
    EXPECT_EQ(hj->json()["key"], "value");
    EXPECT_EQ(hj->json()["n"], 42);
    hj->release();
}

TEST_F(JsonPrimitivesTest, HeapJsonDump) {
    nlohmann::json j = {{"a", 1}};
    auto* hj = new HeapJson(j);
    auto compact = hj->dump();
    EXPECT_TRUE(compact.find("\"a\":1") != std::string::npos ||
                compact.find("\"a\": 1") != std::string::npos);
    auto pretty = hj->dump(2);
    EXPECT_TRUE(pretty.find('\n') != std::string::npos);
    hj->release();
}

TEST_F(JsonPrimitivesTest, HeapJsonRefcount) {
    nlohmann::json j = nlohmann::json::array({1, 2, 3});
    auto* hj = new HeapJson(j);
    EXPECT_EQ(hj->refcount(), 1u);
    hj->add_ref();
    EXPECT_EQ(hj->refcount(), 2u);
    hj->release();
    EXPECT_EQ(hj->refcount(), 1u);
    hj->release();  // should delete
}

// --- j| interpret mode ---

TEST_F(JsonPrimitivesTest, JPipeInterpret) {
    run("j| {\"key\": \"value\"} |");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Json);
    auto* hj = opt->as_json();
    EXPECT_EQ(hj->json()["key"], "value");
    hj->release();
}

TEST_F(JsonPrimitivesTest, JPipeInterpretArray) {
    run("j| [1, 2, 3] |");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Json);
    auto* hj = opt->as_json();
    EXPECT_TRUE(hj->json().is_array());
    EXPECT_EQ(hj->json().size(), 3u);
    hj->release();
}

TEST_F(JsonPrimitivesTest, JPipeInvalidJson) {
    run("j| {invalid} |");
    // Should fail, stack should be empty
    EXPECT_EQ(ctx().data_stack().size(), 0u);
}

// --- j| compile mode ---

TEST_F(JsonPrimitivesTest, JPipeCompile) {
    run(": test-json j| {\"x\": 42} | ;");
    run("test-json");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Json);
    auto* hj = opt->as_json();
    EXPECT_EQ(hj->json()["x"], 42);
    hj->release();
}

// --- json-parse ---

TEST_F(JsonPrimitivesTest, JsonParse) {
    run("s| {\"name\": \"test\"} | json-parse");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Json);
    auto* hj = opt->as_json();
    EXPECT_EQ(hj->json()["name"], "test");
    hj->release();
}

TEST_F(JsonPrimitivesTest, JsonParseInvalid) {
    run("s| not json | json-parse");
    EXPECT_EQ(ctx().data_stack().size(), 0u);
}

// --- json-dump ---

TEST_F(JsonPrimitivesTest, JsonDump) {
    run("j| {\"a\": 1} | json-dump");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::String);
    auto* hs = opt->as_string();
    std::string s(hs->view());
    EXPECT_TRUE(s.find("\"a\"") != std::string::npos);
    hs->release();
}

// --- json-pretty ---

TEST_F(JsonPrimitivesTest, JsonPretty) {
    run("j| {\"a\": 1} | json-pretty");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::String);
    auto* hs = opt->as_string();
    std::string s(hs->view());
    EXPECT_TRUE(s.find('\n') != std::string::npos);
    hs->release();
}

// --- json-get ---

TEST_F(JsonPrimitivesTest, JsonGetString) {
    run("j| {\"name\": \"alice\"} | s\" name\" json-get");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::String);
    EXPECT_EQ(std::string(opt->as_string()->view()), "alice");
    opt->release();
}

TEST_F(JsonPrimitivesTest, JsonGetInteger) {
    run("j| {\"n\": 42} | s\" n\" json-get");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Integer);
    EXPECT_EQ(opt->as_int, 42);
}

TEST_F(JsonPrimitivesTest, JsonGetFloat) {
    run("j| {\"f\": 3.14} | s\" f\" json-get");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Float);
    EXPECT_NEAR(opt->as_float, 3.14, 0.001);
}

TEST_F(JsonPrimitivesTest, JsonGetBoolean) {
    run("j| {\"b\": true} | s\" b\" json-get");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Boolean);
    EXPECT_TRUE(opt->as_bool());
}

TEST_F(JsonPrimitivesTest, JsonGetNested) {
    run("j| {\"inner\": {\"x\": 1}} | s\" inner\" json-get");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Json);
    auto* hj = opt->as_json();
    EXPECT_EQ(hj->json()["x"], 1);
    hj->release();
}

TEST_F(JsonPrimitivesTest, JsonGetMissing) {
    run("j| {\"a\": 1} | s\" b\" json-get");
    // Should fail, stack should be empty
    EXPECT_EQ(ctx().data_stack().size(), 0u);
}

// --- json-get with integer index (arrays) ---

TEST_F(JsonPrimitivesTest, JsonGetArrayIndex) {
    run("j| [10, 20, 30] | 1 json-get");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Integer);
    EXPECT_EQ(opt->as_int, 20);
}

TEST_F(JsonPrimitivesTest, JsonGetArrayFirstElement) {
    run("j| [\"hello\", \"world\"] | 0 json-get");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::String);
    EXPECT_EQ(std::string(opt->as_string()->view()), "hello");
    opt->release();
}

TEST_F(JsonPrimitivesTest, JsonGetArrayNestedObject) {
    run("j| [{\"x\": 99}] | 0 json-get s\" x\" json-get");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Integer);
    EXPECT_EQ(opt->as_int, 99);
}

TEST_F(JsonPrimitivesTest, JsonGetArrayOutOfRange) {
    run("j| [1, 2] | 5 json-get");
    EXPECT_EQ(ctx().data_stack().size(), 0u);
}

TEST_F(JsonPrimitivesTest, JsonGetArrayNegativeIndex) {
    run("j| [1, 2] | -1 json-get");
    EXPECT_EQ(ctx().data_stack().size(), 0u);
}

TEST_F(JsonPrimitivesTest, JsonGetIntOnObject) {
    run("j| {\"a\": 1} | 0 json-get");
    EXPECT_EQ(ctx().data_stack().size(), 0u);
}

// --- json-length ---

TEST_F(JsonPrimitivesTest, JsonLengthArray) {
    run("j| [1, 2, 3, 4] | json-length");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Integer);
    EXPECT_EQ(opt->as_int, 4);
}

TEST_F(JsonPrimitivesTest, JsonLengthObject) {
    run("j| {\"a\": 1, \"b\": 2} | json-length");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Integer);
    EXPECT_EQ(opt->as_int, 2);
}

// --- json-type ---

TEST_F(JsonPrimitivesTest, JsonTypeObject) {
    run("j| {\"a\": 1} | json-type");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::String);
    EXPECT_EQ(std::string(opt->as_string()->view()), "object");
    opt->release();
}

TEST_F(JsonPrimitivesTest, JsonTypeArray) {
    run("j| [1] | json-type");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(std::string(opt->as_string()->view()), "array");
    opt->release();
}

// --- json-keys ---

TEST_F(JsonPrimitivesTest, JsonKeys) {
    run("j| {\"x\": 1, \"y\": 2} | json-keys array-length");
    auto opt = ctx().data_stack().pop();  // length
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Integer);
    EXPECT_EQ(opt->as_int, 2);
    auto arr = ctx().data_stack().pop();  // array
    ASSERT_TRUE(arr.has_value());
    arr->release();
}

// --- Pack/Unpack ---

TEST_F(JsonPrimitivesTest, JsonToMap) {
    run("j| {\"name\": \"test\", \"n\": 42} | json->map");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Map);
    auto* m = opt->as_map();
    EXPECT_EQ(m->size(), 2u);
    Value val;
    EXPECT_TRUE(m->get("n", val));
    EXPECT_EQ(val.type, Value::Type::Integer);
    EXPECT_EQ(val.as_int, 42);
    m->release();
}

TEST_F(JsonPrimitivesTest, JsonToArray) {
    run("j| [10, 20, 30] | json->array array-length");
    auto opt = ctx().data_stack().pop();  // length
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Integer);
    EXPECT_EQ(opt->as_int, 3);
    auto arr = ctx().data_stack().pop();  // array (array-length is non-destructive)
    ASSERT_TRUE(arr.has_value());
    arr->release();
}

TEST_F(JsonPrimitivesTest, MapToJson) {
    run("map-new s\" x\" 42 map-set map->json json-dump");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::String);
    std::string s(opt->as_string()->view());
    EXPECT_TRUE(s.find("\"x\"") != std::string::npos);
    EXPECT_TRUE(s.find("42") != std::string::npos);
    opt->release();
}

TEST_F(JsonPrimitivesTest, ArrayToJson) {
    run("array-new 1 array-push 2 array-push 3 array-push array->json json-dump");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::String);
    std::string s(opt->as_string()->view());
    EXPECT_EQ(s, "[1,2,3]");
    opt->release();
}

TEST_F(JsonPrimitivesTest, JsonToValue) {
    run("j| 42 | json->value");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Integer);
    EXPECT_EQ(opt->as_int, 42);
}

TEST_F(JsonPrimitivesTest, JsonToValueObject) {
    run("j| {\"a\": 1} | json->value");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Map);
    auto* m = opt->as_map();
    Value val;
    EXPECT_TRUE(m->get("a", val));
    EXPECT_EQ(val.as_int, 1);
    m->release();
}

// --- Type switch coverage ---

TEST_F(JsonPrimitivesTest, DotPrintsJson) {
    run("j| {\"a\": 1} | .");
    std::string output = out.str();
    EXPECT_TRUE(output.find("\"a\"") != std::string::npos);
}

TEST_F(JsonPrimitivesTest, DotSShowsJson) {
    run("j| {\"a\": 1} | .s");
    std::string output = out.str();
    EXPECT_TRUE(output.find("<json>") != std::string::npos);
    // Clean up stack
    auto opt = ctx().data_stack().pop();
    if (opt) opt->release();
}

TEST_F(JsonPrimitivesTest, DumpShowsJson) {
    run("j| {\"a\": 1} | dump");
    std::string output = out.str();
    EXPECT_TRUE(output.find("(json)") != std::string::npos);
    // dump is non-destructive, clean up
    auto opt = ctx().data_stack().pop();
    if (opt) opt->release();
}

TEST_F(JsonPrimitivesTest, ArithRejectsJson) {
    // json + integer should fail
    run("j| {\"a\": 1} | 1 +");
    // Clean up whatever's left on the stack
    while (auto opt = ctx().data_stack().pop()) {
        opt->release();
    }
}

// --- Round-trip: map->json->map ---

TEST_F(JsonPrimitivesTest, RoundTripMapJsonMap) {
    run("map-new s\" key\" s\" val\" map-set map->json json->map s\" key\" map-get");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::String);
    EXPECT_EQ(std::string(opt->as_string()->view()), "val");
    opt->release();
}

// --- string conversion ---

TEST_F(JsonPrimitivesTest, JsonToString) {
    run("s\" prefix:\" j| {\"a\": 1} | s+");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::String);
    std::string s(opt->as_string()->view());
    EXPECT_TRUE(s.find("prefix:") != std::string::npos);
    EXPECT_TRUE(s.find("\"a\"") != std::string::npos);
    opt->release();
}
