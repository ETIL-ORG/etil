// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/mcp/http_transport.hpp"
#include "etil/mcp/mcp_server.hpp"
#include <gtest/gtest.h>
#include <set>
#include <regex>

using namespace etil::mcp;

// ---------------------------------------------------------------------------
// Origin validation tests
// ---------------------------------------------------------------------------

class OriginValidationTest : public ::testing::Test {
protected:
    HttpTransportConfig config_;
};

TEST_F(OriginValidationTest, NoRestrictionsAllowsAll) {
    // Empty allowed_origins = allow all
    config_.allowed_origins = {};
    HttpTransport transport(config_);

    EXPECT_TRUE(transport.validate_origin("https://example.com"));
    EXPECT_TRUE(transport.validate_origin("http://localhost"));
    EXPECT_TRUE(transport.validate_origin("https://evil.com"));
}

TEST_F(OriginValidationTest, AllowsMatchingOrigin) {
    config_.allowed_origins = {"https://example.com", "https://app.example.com"};
    HttpTransport transport(config_);

    EXPECT_TRUE(transport.validate_origin("https://example.com"));
    EXPECT_TRUE(transport.validate_origin("https://app.example.com"));
}

TEST_F(OriginValidationTest, BlocksNonMatchingOrigin) {
    config_.allowed_origins = {"https://example.com"};
    HttpTransport transport(config_);

    EXPECT_FALSE(transport.validate_origin("https://evil.com"));
    EXPECT_FALSE(transport.validate_origin("http://example.com")); // scheme mismatch
    EXPECT_FALSE(transport.validate_origin("https://sub.example.com"));
}

TEST_F(OriginValidationTest, EmptyOriginWithRestrictions) {
    config_.allowed_origins = {"https://example.com"};
    HttpTransport transport(config_);

    // Empty origin is not in the list — should be rejected
    EXPECT_FALSE(transport.validate_origin(""));
}

// ---------------------------------------------------------------------------
// API key validation tests
// ---------------------------------------------------------------------------

class ApiKeyValidationTest : public ::testing::Test {
protected:
    HttpTransportConfig config_;
};

TEST_F(ApiKeyValidationTest, NoKeyConfiguredAllowsAll) {
    config_.api_key = "";
    HttpTransport transport(config_);

    EXPECT_TRUE(transport.validate_api_key(""));
    EXPECT_TRUE(transport.validate_api_key("Bearer anything"));
    EXPECT_TRUE(transport.validate_api_key("garbage"));
}

TEST_F(ApiKeyValidationTest, ValidBearerToken) {
    config_.api_key = "test-secret-key-123";
    HttpTransport transport(config_);

    EXPECT_TRUE(transport.validate_api_key("Bearer test-secret-key-123"));
}

TEST_F(ApiKeyValidationTest, InvalidBearerToken) {
    config_.api_key = "test-secret-key-123";
    HttpTransport transport(config_);

    EXPECT_FALSE(transport.validate_api_key("Bearer wrong-key"));
}

TEST_F(ApiKeyValidationTest, MissingAuthHeader) {
    config_.api_key = "test-secret-key-123";
    HttpTransport transport(config_);

    EXPECT_FALSE(transport.validate_api_key(""));
}

TEST_F(ApiKeyValidationTest, MalformedAuthHeader) {
    config_.api_key = "test-secret-key-123";
    HttpTransport transport(config_);

    EXPECT_FALSE(transport.validate_api_key("Basic dXNlcjpwYXNz"));
    EXPECT_FALSE(transport.validate_api_key("bearer test-secret-key-123")); // wrong case
    EXPECT_FALSE(transport.validate_api_key("Bearer")); // no space + token
    EXPECT_FALSE(transport.validate_api_key("Bearer ")); // empty token
}

// ---------------------------------------------------------------------------
// Session ID generation tests
// ---------------------------------------------------------------------------

TEST(SessionIdTest, GeneratesNonEmpty) {
    auto id = McpServer::generate_session_id();
    EXPECT_FALSE(id.empty());
}

TEST(SessionIdTest, HasUuidFormat) {
    auto id = McpServer::generate_session_id();

    // UUID v4: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx (36 chars)
    EXPECT_EQ(id.size(), 36u);
    EXPECT_EQ(id[8], '-');
    EXPECT_EQ(id[13], '-');
    EXPECT_EQ(id[14], '4'); // version
    EXPECT_EQ(id[18], '-');
    EXPECT_EQ(id[23], '-');

    // Position 19 must be 8, 9, a, or b (variant)
    EXPECT_TRUE(id[19] == '8' || id[19] == '9' || id[19] == 'a' || id[19] == 'b');

    // All other chars must be hex
    std::regex uuid_re("^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");
    EXPECT_TRUE(std::regex_match(id, uuid_re)) << "Invalid UUID format: " << id;
}

TEST(SessionIdTest, GeneratesUniqueIds) {
    std::set<std::string> ids;
    for (int i = 0; i < 100; ++i) {
        ids.insert(McpServer::generate_session_id());
    }
    // All 100 should be unique
    EXPECT_EQ(ids.size(), 100u);
}

// ---------------------------------------------------------------------------
// SSE notification buffering tests
// ---------------------------------------------------------------------------

class SseNotificationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure clean state before each test
        HttpTransport::clear_pending_notifications();
    }
    void TearDown() override {
        // Clean up after each test
        HttpTransport::clear_pending_notifications();
    }
};

TEST_F(SseNotificationTest, SendBuffersNotification) {
    HttpTransportConfig config;
    HttpTransport transport(config);

    nlohmann::json notif = {
        {"jsonrpc", "2.0"},
        {"method", "notifications/message"},
        {"params", {{"level", "info"}, {"data", "hello"}}}
    };

    transport.send(notif);
    auto drained = HttpTransport::drain_pending_notifications();

    ASSERT_EQ(drained.size(), 1u);
    EXPECT_EQ(drained[0]["params"]["data"], "hello");
}

TEST_F(SseNotificationTest, DrainClearsBuffer) {
    HttpTransportConfig config;
    HttpTransport transport(config);

    nlohmann::json notif = {{"jsonrpc", "2.0"}, {"method", "test"}};
    transport.send(notif);

    auto first = HttpTransport::drain_pending_notifications();
    EXPECT_EQ(first.size(), 1u);

    // Second drain should be empty
    auto second = HttpTransport::drain_pending_notifications();
    EXPECT_TRUE(second.empty());
}

TEST_F(SseNotificationTest, ClearEmptiesBuffer) {
    HttpTransportConfig config;
    HttpTransport transport(config);

    nlohmann::json notif = {{"jsonrpc", "2.0"}, {"method", "test"}};
    transport.send(notif);
    transport.send(notif);

    HttpTransport::clear_pending_notifications();
    auto drained = HttpTransport::drain_pending_notifications();
    EXPECT_TRUE(drained.empty());
}

TEST_F(SseNotificationTest, MultipleNotificationsBuffered) {
    HttpTransportConfig config;
    HttpTransport transport(config);

    for (int i = 0; i < 5; ++i) {
        nlohmann::json notif = {
            {"jsonrpc", "2.0"},
            {"method", "notifications/message"},
            {"params", {{"level", "info"}, {"data", "msg" + std::to_string(i)}}}
        };
        transport.send(notif);
    }

    auto drained = HttpTransport::drain_pending_notifications();
    ASSERT_EQ(drained.size(), 5u);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(drained[i]["params"]["data"], "msg" + std::to_string(i));
    }
}

TEST_F(SseNotificationTest, BuildSseBodyFormat) {
    std::vector<nlohmann::json> notifications = {
        {{"jsonrpc", "2.0"}, {"method", "notifications/message"},
         {"params", {{"level", "info"}, {"data", "first"}}}},
        {{"jsonrpc", "2.0"}, {"method", "notifications/message"},
         {"params", {{"level", "info"}, {"data", "second"}}}}
    };
    nlohmann::json response = {
        {"jsonrpc", "2.0"}, {"id", 42}, {"result", {{"output", "done"}}}
    };

    auto body = HttpTransport::build_sse_body(notifications, response);

    // Should have 3 "data: " lines (2 notifications + 1 response)
    // Each followed by two newlines
    size_t count = 0;
    size_t pos = 0;
    while ((pos = body.find("data: ", pos)) != std::string::npos) {
        count++;
        pos += 6;
    }
    EXPECT_EQ(count, 3u);

    // First event should contain "first"
    EXPECT_NE(body.find("\"first\""), std::string::npos);
    // Second event should contain "second"
    EXPECT_NE(body.find("\"second\""), std::string::npos);
    // Final event should contain the response id
    EXPECT_NE(body.find("\"id\":42"), std::string::npos);

    // Each event ends with \n\n
    // The body should end with \n\n
    EXPECT_GE(body.size(), 2u);
    EXPECT_EQ(body.substr(body.size() - 2), "\n\n");
}

TEST_F(SseNotificationTest, BuildSseBodyNoNotifications) {
    std::vector<nlohmann::json> empty;
    nlohmann::json response = {
        {"jsonrpc", "2.0"}, {"id", 1}, {"result", nullptr}
    };

    auto body = HttpTransport::build_sse_body(empty, response);

    // Should have exactly 1 "data: " line (response only)
    size_t count = 0;
    size_t pos = 0;
    while ((pos = body.find("data: ", pos)) != std::string::npos) {
        count++;
        pos += 6;
    }
    EXPECT_EQ(count, 1u);
}

TEST_F(SseNotificationTest, EmptyDrainOnFreshState) {
    auto drained = HttpTransport::drain_pending_notifications();
    EXPECT_TRUE(drained.empty());
}
