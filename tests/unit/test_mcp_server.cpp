// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/mcp/mcp_server.hpp"
#include "etil/mcp/session.hpp"
#include "etil/mcp/json_rpc.hpp"
#include <gtest/gtest.h>
#include <regex>
#include <set>
#include <thread>

using namespace etil::mcp;
using json = nlohmann::json;

class McpServerTest : public ::testing::Test {
protected:
    McpServer server;

    json make_request(int id, const std::string& method,
                      const json& params = json::object()) {
        return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
    }

    json make_notification(const std::string& method) {
        return {{"jsonrpc", "2.0"}, {"method", method}};
    }
};

// ---------------------------------------------------------------------------
// initialize
// ---------------------------------------------------------------------------

TEST_F(McpServerTest, Initialize) {
    auto resp = server.handle_message(make_request(1, "initialize", {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", json::object()},
        {"clientInfo", {{"name", "test"}, {"version", "1.0"}}}
    }));

    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ((*resp)["id"], 1);
    auto result = (*resp)["result"];
    EXPECT_EQ(result["protocolVersion"], "2024-11-05");
    EXPECT_TRUE(result.contains("capabilities"));
    EXPECT_TRUE(result.contains("serverInfo"));
    EXPECT_EQ(result["serverInfo"]["name"], "etil-mcp");
}

// ---------------------------------------------------------------------------
// notifications/initialized
// ---------------------------------------------------------------------------

TEST_F(McpServerTest, NotificationsInitialized) {
    auto resp = server.handle_message(make_notification("notifications/initialized"));
    EXPECT_FALSE(resp.has_value());
}

// ---------------------------------------------------------------------------
// ping
// ---------------------------------------------------------------------------

TEST_F(McpServerTest, Ping) {
    auto resp = server.handle_message(make_request(2, "ping"));
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ((*resp)["id"], 2);
    EXPECT_TRUE((*resp)["result"].is_object());
}

// ---------------------------------------------------------------------------
// tools/list
// ---------------------------------------------------------------------------

TEST_F(McpServerTest, ToolsList) {
    auto resp = server.handle_message(make_request(3, "tools/list"));
    ASSERT_TRUE(resp.has_value());
    auto tools = (*resp)["result"]["tools"];
    ASSERT_TRUE(tools.is_array());
    EXPECT_GE(tools.size(), 9u);

    // Check tool names exist
    std::vector<std::string> names;
    for (const auto& t : tools) {
        names.push_back(t["name"].get<std::string>());
        EXPECT_TRUE(t.contains("description"));
        EXPECT_TRUE(t.contains("inputSchema"));
    }
    EXPECT_NE(std::find(names.begin(), names.end(), "interpret"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "list_words"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "get_word_info"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "get_stack"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "set_weight"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "reset"), names.end());
}

// ---------------------------------------------------------------------------
// tools/call — missing name
// ---------------------------------------------------------------------------

TEST_F(McpServerTest, ToolsCallMissingName) {
    auto resp = server.handle_message(make_request(4, "tools/call"));
    ASSERT_TRUE(resp.has_value());
    EXPECT_TRUE((*resp).contains("error"));
    EXPECT_EQ((*resp)["error"]["code"], static_cast<int>(JsonRpcError::InvalidParams));
}

// ---------------------------------------------------------------------------
// tools/call — unknown tool
// ---------------------------------------------------------------------------

TEST_F(McpServerTest, ToolsCallUnknownTool) {
    auto resp = server.handle_message(make_request(5, "tools/call", {
        {"name", "nonexistent_tool"}
    }));
    ASSERT_TRUE(resp.has_value());
    EXPECT_TRUE((*resp).contains("error"));
}

// ---------------------------------------------------------------------------
// resources/list
// ---------------------------------------------------------------------------

TEST_F(McpServerTest, ResourcesList) {
    auto resp = server.handle_message(make_request(6, "resources/list"));
    ASSERT_TRUE(resp.has_value());
    auto resources = (*resp)["result"]["resources"];
    ASSERT_TRUE(resources.is_array());
    EXPECT_GE(resources.size(), 4u);

    std::vector<std::string> uris;
    for (const auto& r : resources) {
        uris.push_back(r["uri"].get<std::string>());
    }
    EXPECT_NE(std::find(uris.begin(), uris.end(), "etil://dictionary"), uris.end());
    EXPECT_NE(std::find(uris.begin(), uris.end(), "etil://word/{name}"), uris.end());
    EXPECT_NE(std::find(uris.begin(), uris.end(), "etil://stack"), uris.end());
}

// ---------------------------------------------------------------------------
// resources/read — missing URI
// ---------------------------------------------------------------------------

TEST_F(McpServerTest, ResourcesReadMissingUri) {
    auto resp = server.handle_message(make_request(7, "resources/read", {}));
    ASSERT_TRUE(resp.has_value());
    EXPECT_TRUE((*resp).contains("error"));
}

// ---------------------------------------------------------------------------
// resources/read — unknown URI
// ---------------------------------------------------------------------------

TEST_F(McpServerTest, ResourcesReadUnknownUri) {
    auto resp = server.handle_message(make_request(8, "resources/read", {
        {"uri", "etil://nonexistent"}
    }));
    ASSERT_TRUE(resp.has_value());
    EXPECT_TRUE((*resp).contains("error"));
}

// ---------------------------------------------------------------------------
// resources/read — dictionary
// ---------------------------------------------------------------------------

TEST_F(McpServerTest, ResourcesReadDictionary) {
    auto resp = server.handle_message(make_request(9, "resources/read", {
        {"uri", "etil://dictionary"}
    }));
    ASSERT_TRUE(resp.has_value());
    EXPECT_FALSE((*resp).contains("error"));
    auto contents = (*resp)["result"]["contents"];
    ASSERT_TRUE(contents.is_array());
    EXPECT_FALSE(contents.empty());
    EXPECT_EQ(contents[0]["uri"], "etil://dictionary");
    EXPECT_EQ(contents[0]["mimeType"], "application/json");
}

// ---------------------------------------------------------------------------
// resources/read — stack
// ---------------------------------------------------------------------------

TEST_F(McpServerTest, ResourcesReadStack) {
    auto resp = server.handle_message(make_request(10, "resources/read", {
        {"uri", "etil://stack"}
    }));
    ASSERT_TRUE(resp.has_value());
    EXPECT_FALSE((*resp).contains("error"));
}

// ---------------------------------------------------------------------------
// resources/read — word (template match)
// ---------------------------------------------------------------------------

TEST_F(McpServerTest, ResourcesReadWord) {
    auto resp = server.handle_message(make_request(11, "resources/read", {
        {"uri", "etil://word/dup"}
    }));
    ASSERT_TRUE(resp.has_value());
    EXPECT_FALSE((*resp).contains("error"));
    auto contents = (*resp)["result"]["contents"];
    ASSERT_TRUE(contents.is_array());
    EXPECT_FALSE(contents.empty());
}

// ---------------------------------------------------------------------------
// Unknown method
// ---------------------------------------------------------------------------

TEST_F(McpServerTest, UnknownMethod) {
    auto resp = server.handle_message(make_request(12, "totally/unknown"));
    ASSERT_TRUE(resp.has_value());
    EXPECT_TRUE((*resp).contains("error"));
    EXPECT_EQ((*resp)["error"]["code"], static_cast<int>(JsonRpcError::MethodNotFound));
}

// ---------------------------------------------------------------------------
// Unknown notification (no response)
// ---------------------------------------------------------------------------

TEST_F(McpServerTest, UnknownNotification) {
    auto resp = server.handle_message(make_notification("totally/unknown"));
    EXPECT_FALSE(resp.has_value());
}

// ---------------------------------------------------------------------------
// Invalid JSON-RPC
// ---------------------------------------------------------------------------

TEST_F(McpServerTest, InvalidJsonRpc) {
    auto resp = server.handle_message(json{{"foo", "bar"}});
    ASSERT_TRUE(resp.has_value());
    EXPECT_TRUE((*resp).contains("error"));
}

// ---------------------------------------------------------------------------
// Server owns tools and resources
// ---------------------------------------------------------------------------

TEST_F(McpServerTest, HasToolDefinitions) {
    EXPECT_EQ(server.tools().size(), 21u);  // 10 base + list_sessions, kick_session, manage_allowlist + 8 admin tools
}

TEST_F(McpServerTest, HasResourceDefinitions) {
    EXPECT_EQ(server.resources().size(), 4u);
}

// ---------------------------------------------------------------------------
// Concurrent-safe: handle_message can be called multiple times
// ---------------------------------------------------------------------------

TEST_F(McpServerTest, MultipleSequentialCalls) {
    for (int i = 0; i < 5; ++i) {
        auto resp = server.handle_message(make_request(i, "ping"));
        ASSERT_TRUE(resp.has_value());
        EXPECT_EQ((*resp)["id"], i);
    }
}

// ---------------------------------------------------------------------------
// Full MCP handshake sequence
// ---------------------------------------------------------------------------

TEST_F(McpServerTest, FullHandshake) {
    // 1. initialize
    auto r1 = server.handle_message(make_request(1, "initialize", {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", json::object()},
        {"clientInfo", {{"name", "test"}, {"version", "1.0"}}}
    }));
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ((*r1)["result"]["protocolVersion"], "2024-11-05");

    // 2. notifications/initialized
    auto r2 = server.handle_message(make_notification("notifications/initialized"));
    EXPECT_FALSE(r2.has_value());

    // 3. tools/list
    auto r3 = server.handle_message(make_request(2, "tools/list"));
    ASSERT_TRUE(r3.has_value());
    EXPECT_GE((*r3)["result"]["tools"].size(), 7u);

    // 4. resources/list
    auto r4 = server.handle_message(make_request(3, "resources/list"));
    ASSERT_TRUE(r4.has_value());
    EXPECT_GE((*r4)["result"]["resources"].size(), 4u);
}

// ===========================================================================
// Multi-session tests
// ===========================================================================

class McpMultiSessionTest : public ::testing::Test {
protected:
    McpServer server;

    json make_request(int id, const std::string& method,
                      const json& params = json::object()) {
        return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
    }

    json call_tool(const std::string& session_id, int id,
                   const std::string& name, const json& args = json::object()) {
        json request = {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"method", "tools/call"},
            {"params", {{"name", name}, {"arguments", args}}}
        };
        auto resp = server.handle_message(session_id, request);
        EXPECT_TRUE(resp.has_value());
        return *resp;
    }

    json parse_tool_content(const json& resp) {
        return json::parse(resp["result"]["content"][0]["text"].get<std::string>());
    }
};

// ---------------------------------------------------------------------------
// Session lifecycle
// ---------------------------------------------------------------------------

TEST_F(McpMultiSessionTest, CreateSession) {
    auto id = server.create_session();
    EXPECT_FALSE(id.empty());
    EXPECT_TRUE(server.has_session(id));
    EXPECT_EQ(server.session_count(), 1u);
}

TEST_F(McpMultiSessionTest, CreateMultipleSessions) {
    auto id1 = server.create_session();
    auto id2 = server.create_session();
    EXPECT_NE(id1, id2);
    EXPECT_EQ(server.session_count(), 2u);
}

TEST_F(McpMultiSessionTest, DestroySession) {
    auto id = server.create_session();
    EXPECT_TRUE(server.has_session(id));
    server.destroy_session(id);
    EXPECT_FALSE(server.has_session(id));
    EXPECT_EQ(server.session_count(), 0u);
}

TEST_F(McpMultiSessionTest, DestroyNonexistentSession) {
    // Should be a no-op
    server.destroy_session("nonexistent-id");
    EXPECT_EQ(server.session_count(), 0u);
}

// ---------------------------------------------------------------------------
// Session isolation — independent interpreter state
// ---------------------------------------------------------------------------

TEST_F(McpMultiSessionTest, IndependentInterpreterState) {
    auto s1 = server.create_session();
    auto s2 = server.create_session();

    // Push values on session 1
    call_tool(s1, 1, "interpret", {{"code", "42"}});

    // Session 2 should have an empty stack
    auto resp2 = call_tool(s2, 2, "get_stack");
    auto content2 = parse_tool_content(resp2);
    EXPECT_EQ(content2["depth"], 0);

    // Session 1 should still have its value
    auto resp1 = call_tool(s1, 3, "get_stack");
    auto content1 = parse_tool_content(resp1);
    EXPECT_EQ(content1["depth"], 1);
    EXPECT_EQ(content1["elements"][0]["value"], "42");
}

TEST_F(McpMultiSessionTest, IndependentDefinitions) {
    auto s1 = server.create_session();
    auto s2 = server.create_session();

    // Define a word in session 1
    call_tool(s1, 1, "interpret", {{"code", ": triple dup dup + + ;"}});
    auto resp1 = call_tool(s1, 2, "interpret", {{"code", "7 triple"}});
    auto c1 = parse_tool_content(resp1);
    EXPECT_EQ(c1["stack"][0], "21");

    // Session 2 should not know about "triple"
    auto resp2 = call_tool(s2, 3, "interpret", {{"code", "triple"}});
    EXPECT_TRUE(resp2["result"]["isError"].get<bool>());
}

// ---------------------------------------------------------------------------
// Invalid session ID
// ---------------------------------------------------------------------------

TEST_F(McpMultiSessionTest, UnknownSessionReturnsError) {
    auto resp = server.handle_message("nonexistent-session-id",
                                       make_request(1, "ping"));
    ASSERT_TRUE(resp.has_value());
    EXPECT_TRUE((*resp).contains("error"));
}

// ---------------------------------------------------------------------------
// Max sessions limit
// ---------------------------------------------------------------------------

TEST_F(McpMultiSessionTest, MaxSessionsEnforced) {
    // Create MAX_SESSIONS sessions
    for (size_t i = 0; i < MAX_SESSIONS; ++i) {
        auto id = server.create_session();
        EXPECT_FALSE(id.empty()) << "Failed to create session " << i;
    }
    EXPECT_EQ(server.session_count(), MAX_SESSIONS);

    // Next creation should fail (empty string)
    auto overflow_id = server.create_session();
    EXPECT_TRUE(overflow_id.empty());
    EXPECT_EQ(server.session_count(), MAX_SESSIONS);
}

// ---------------------------------------------------------------------------
// Session ID format (UUID v4)
// ---------------------------------------------------------------------------

TEST(SessionIdGenerationTest, GeneratesValidUuid) {
    auto id = McpServer::generate_session_id();
    EXPECT_EQ(id.size(), 36u);
    EXPECT_EQ(id[14], '4');
    std::regex uuid_re("^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");
    EXPECT_TRUE(std::regex_match(id, uuid_re));
}

TEST(SessionIdGenerationTest, GeneratesUniqueIds) {
    std::set<std::string> ids;
    for (int i = 0; i < 100; ++i) {
        ids.insert(McpServer::generate_session_id());
    }
    EXPECT_EQ(ids.size(), 100u);
}

// ---------------------------------------------------------------------------
// Auto-session mode preserves backward compatibility
// ---------------------------------------------------------------------------

TEST_F(McpMultiSessionTest, AutoSessionCreatesDefault) {
    // Single-arg handle_message should work without explicit session creation
    auto resp = server.handle_message(make_request(1, "ping"));
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ((*resp)["id"], 1);

    // A default session should now exist
    EXPECT_GE(server.session_count(), 1u);
}

// ---------------------------------------------------------------------------
// Reset within an explicit session
// ---------------------------------------------------------------------------

TEST_F(McpMultiSessionTest, ResetWithinSession) {
    auto sid = server.create_session();

    call_tool(sid, 1, "interpret", {{"code", "1 2 3"}});
    call_tool(sid, 2, "reset");

    auto resp = call_tool(sid, 3, "get_stack");
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["depth"], 0);
}
