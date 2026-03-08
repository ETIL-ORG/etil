// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/heap_map.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/interpreter.hpp"
#include "etil/core/primitives.hpp"
#include "etil/net/http_primitives.hpp"

#include <gtest/gtest.h>
#include <sstream>

using namespace etil::core;

class HttpGetTest : public ::testing::Test {
protected:
    Dictionary dict;
    std::ostringstream out, err;
    std::unique_ptr<Interpreter> interp;

    void SetUp() override {
        register_primitives(dict);
        etil::net::register_http_primitives(dict);
        interp = std::make_unique<Interpreter>(dict, out, err);
    }

    void TearDown() override {
        interp->shutdown();
    }

    ExecutionContext& ctx() { return interp->context(); }
};

// --- Stack argument validation ---

TEST_F(HttpGetTest, UnderflowEmpty) {
    // Empty stack -> underflow
    interp->interpret_line("http-get");
    EXPECT_EQ(ctx().data_stack().size(), 0u);
}

TEST_F(HttpGetTest, UnderflowOnlyUrl) {
    // Only URL, no headers map -> underflow (pops "url" as if it were the map)
    // With just one item on stack, the second pop (url) will fail
    interp->interpret_line("s\" http://example.com\" http-get");
    // The string was popped as the map arg, type check fails -> returns false
    // Stack should be empty (both pops consumed or failed)
}

TEST_F(HttpGetTest, WrongTypeForHeaders) {
    // Push URL + integer (not a map) -> type error
    // prim pops the integer, sees wrong type, returns false.
    // The URL string remains on stack (prim failed before popping it).
    interp->interpret_line("s\" http://example.com\" 42 http-get");
    EXPECT_TRUE(err.str().find("expected Map") != std::string::npos)
        << "err: " << err.str();
    EXPECT_EQ(ctx().data_stack().size(), 1u);
    auto val = ctx().data_stack().pop();
    EXPECT_EQ(val->type, Value::Type::String);
    value_release(*val);
}

TEST_F(HttpGetTest, EmptyMapNoHttpState) {
    // URL + empty map -> should fail with "HTTP client not configured"
    // (no http_client_state on the context)
    interp->interpret_line("s\" http://example.com\" map-new http-get");
    EXPECT_TRUE(err.str().find("not configured") != std::string::npos
                || err.str().find("not permitted") != std::string::npos)
        << "err: " << err.str();
    // Should push false on failure
    ASSERT_EQ(ctx().data_stack().size(), 1u);
    auto val = ctx().data_stack().pop();
    EXPECT_EQ(val->type, Value::Type::Boolean);
    EXPECT_FALSE(val->as_bool());
}

TEST_F(HttpGetTest, MapWithHeadersNoHttpState) {
    // URL + map with headers -> still fails (no http state) but exercises
    // the header map pop path and verifies the map is consumed
    interp->interpret_line(
        "s\" http://example.com\" "
        "map-new s\" Accept\" s\" text/plain\" map-set "
        "http-get");
    EXPECT_TRUE(err.str().find("not configured") != std::string::npos
                || err.str().find("not permitted") != std::string::npos)
        << "err: " << err.str();
    // false flag on stack, url and map consumed
    ASSERT_EQ(ctx().data_stack().size(), 1u);
    auto val = ctx().data_stack().pop();
    EXPECT_EQ(val->type, Value::Type::Boolean);
    EXPECT_FALSE(val->as_bool());
}

TEST_F(HttpGetTest, MapWithMultipleHeadersNoHttpState) {
    // Multiple header entries in the map
    interp->interpret_line(
        "s\" http://example.com\" "
        "map-new "
        "s\" Accept\" s\" application/json\" map-set "
        "s\" X-Custom\" s\" my-value\" map-set "
        "http-get");
    EXPECT_TRUE(err.str().find("not configured") != std::string::npos
                || err.str().find("not permitted") != std::string::npos)
        << "err: " << err.str();
    ASSERT_EQ(ctx().data_stack().size(), 1u);
    auto val = ctx().data_stack().pop();
    EXPECT_FALSE(val->as_bool());
}

TEST_F(HttpGetTest, StringInsteadOfMap) {
    // Push string where map expected
    interp->interpret_line("s\" http://example.com\" s\" not-a-map\" http-get");
    EXPECT_TRUE(err.str().find("expected Map") != std::string::npos)
        << "err: " << err.str();
}
