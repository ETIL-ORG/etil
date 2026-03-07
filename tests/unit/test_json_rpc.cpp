// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/mcp/json_rpc.hpp"
#include <gtest/gtest.h>

using namespace etil::mcp;
using json = nlohmann::json;

// Helper to disambiguate string literal calls to parse_request(const string&, ...)
static std::optional<JsonRpcRequest> parse_str(const char* s, json& err) {
    return parse_request(std::string(s), err);
}

// ---------------------------------------------------------------------------
// parse_request (from string)
// ---------------------------------------------------------------------------

TEST(JsonRpcTest, ParseValidRequest) {
    json err;
    auto req = parse_str(
        R"({"jsonrpc":"2.0","id":1,"method":"test","params":{"x":42}})", err);
    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(req->jsonrpc, "2.0");
    ASSERT_TRUE(req->id.has_value());
    EXPECT_EQ(*req->id, 1);
    EXPECT_EQ(req->method, "test");
    EXPECT_EQ(req->params["x"], 42);
}

TEST(JsonRpcTest, ParseValidRequestStringId) {
    json err;
    auto req = parse_str(
        R"({"jsonrpc":"2.0","id":"abc","method":"test"})", err);
    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(*req->id, "abc");
}

TEST(JsonRpcTest, ParseValidRequestNullId) {
    json err;
    auto req = parse_str(
        R"({"jsonrpc":"2.0","id":null,"method":"test"})", err);
    ASSERT_TRUE(req.has_value());
    ASSERT_TRUE(req->id.has_value());
    EXPECT_TRUE(req->id->is_null());
}

TEST(JsonRpcTest, ParseNotification) {
    json err;
    auto req = parse_str(
        R"({"jsonrpc":"2.0","method":"notifications/initialized"})", err);
    ASSERT_TRUE(req.has_value());
    EXPECT_FALSE(req->id.has_value());
    EXPECT_EQ(req->method, "notifications/initialized");
}

TEST(JsonRpcTest, ParseMissingParams) {
    json err;
    auto req = parse_str(
        R"({"jsonrpc":"2.0","id":1,"method":"test"})", err);
    ASSERT_TRUE(req.has_value());
    EXPECT_TRUE(req->params.is_object());
    EXPECT_TRUE(req->params.empty());
}

TEST(JsonRpcTest, ParseArrayParams) {
    json err;
    auto req = parse_str(
        R"({"jsonrpc":"2.0","id":1,"method":"test","params":[1,2,3]})", err);
    ASSERT_TRUE(req.has_value());
    EXPECT_TRUE(req->params.is_array());
    EXPECT_EQ(req->params.size(), 3u);
}

TEST(JsonRpcTest, ParseInvalidJson) {
    json err;
    auto req = parse_str("not json at all", err);
    ASSERT_FALSE(req.has_value());
    EXPECT_EQ(err["error"]["code"], static_cast<int>(JsonRpcError::ParseError));
}

TEST(JsonRpcTest, ParseNotObject) {
    json err;
    auto req = parse_str("[1,2,3]", err);
    ASSERT_FALSE(req.has_value());
    EXPECT_EQ(err["error"]["code"], static_cast<int>(JsonRpcError::InvalidRequest));
}

TEST(JsonRpcTest, ParseMissingJsonrpc) {
    json err;
    auto req = parse_str(R"({"id":1,"method":"test"})", err);
    ASSERT_FALSE(req.has_value());
    EXPECT_EQ(err["error"]["code"], static_cast<int>(JsonRpcError::InvalidRequest));
}

TEST(JsonRpcTest, ParseWrongJsonrpcVersion) {
    json err;
    auto req = parse_str(
        R"({"jsonrpc":"1.0","id":1,"method":"test"})", err);
    ASSERT_FALSE(req.has_value());
    EXPECT_EQ(err["error"]["code"], static_cast<int>(JsonRpcError::InvalidRequest));
}

TEST(JsonRpcTest, ParseMissingMethod) {
    json err;
    auto req = parse_str(R"({"jsonrpc":"2.0","id":1})", err);
    ASSERT_FALSE(req.has_value());
    EXPECT_EQ(err["error"]["code"], static_cast<int>(JsonRpcError::InvalidRequest));
}

TEST(JsonRpcTest, ParseNonStringMethod) {
    json err;
    auto req = parse_str(
        R"({"jsonrpc":"2.0","id":1,"method":42})", err);
    ASSERT_FALSE(req.has_value());
    EXPECT_EQ(err["error"]["code"], static_cast<int>(JsonRpcError::InvalidRequest));
}

TEST(JsonRpcTest, ParseInvalidParamsType) {
    json err;
    auto req = parse_str(
        R"({"jsonrpc":"2.0","id":1,"method":"test","params":"bad"})", err);
    ASSERT_FALSE(req.has_value());
    EXPECT_EQ(err["error"]["code"], static_cast<int>(JsonRpcError::InvalidRequest));
}

// ---------------------------------------------------------------------------
// parse_request (from json object)
// ---------------------------------------------------------------------------

TEST(JsonRpcTest, ParseFromJsonObject) {
    json j = {{"jsonrpc", "2.0"}, {"id", 5}, {"method", "foo"}};
    json err;
    auto req = parse_request(j, err);
    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(req->method, "foo");
    EXPECT_EQ(*req->id, 5);
}

// ---------------------------------------------------------------------------
// make_response
// ---------------------------------------------------------------------------

TEST(JsonRpcTest, MakeResponse) {
    auto resp = make_response(42, {{"status", "ok"}});
    EXPECT_EQ(resp["jsonrpc"], "2.0");
    EXPECT_EQ(resp["id"], 42);
    EXPECT_EQ(resp["result"]["status"], "ok");
    EXPECT_FALSE(resp.contains("error"));
}

TEST(JsonRpcTest, MakeResponseNullId) {
    auto resp = make_response(nullptr, "done");
    EXPECT_TRUE(resp["id"].is_null());
    EXPECT_EQ(resp["result"], "done");
}

// ---------------------------------------------------------------------------
// make_error
// ---------------------------------------------------------------------------

TEST(JsonRpcTest, MakeErrorWithCode) {
    auto resp = make_error(1, -32600, "Invalid Request");
    EXPECT_EQ(resp["jsonrpc"], "2.0");
    EXPECT_EQ(resp["id"], 1);
    EXPECT_EQ(resp["error"]["code"], -32600);
    EXPECT_EQ(resp["error"]["message"], "Invalid Request");
    EXPECT_FALSE(resp["error"].contains("data"));
}

TEST(JsonRpcTest, MakeErrorWithData) {
    auto resp = make_error(1, -32600, "Invalid Request", {{"detail", "x"}});
    EXPECT_EQ(resp["error"]["data"]["detail"], "x");
}

TEST(JsonRpcTest, MakeErrorWithEnum) {
    auto resp = make_error(1, JsonRpcError::MethodNotFound, "Not found");
    EXPECT_EQ(resp["error"]["code"], -32601);
}

TEST(JsonRpcTest, MakeErrorNullId) {
    auto resp = make_error(nullptr, JsonRpcError::ParseError, "Parse error");
    EXPECT_TRUE(resp["id"].is_null());
}

// ---------------------------------------------------------------------------
// Round-trip
// ---------------------------------------------------------------------------

TEST(JsonRpcTest, RoundTripRequestResponse) {
    std::string input = R"({"jsonrpc":"2.0","id":99,"method":"echo","params":{"msg":"hi"}})";
    json err;
    auto req = parse_str(input.c_str(), err);
    ASSERT_TRUE(req.has_value());

    auto resp = make_response(*req->id, req->params);
    EXPECT_EQ(resp["id"], 99);
    EXPECT_EQ(resp["result"]["msg"], "hi");
}
