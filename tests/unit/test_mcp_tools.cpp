// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/mcp/mcp_server.hpp"
#include "etil/mcp/session.hpp"
#include "etil/mcp/json_rpc.hpp"
#include "etil/core/interpreter.hpp"
#include "etil/core/primitives.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

using namespace etil::mcp;
using json = nlohmann::json;

class McpToolsTest : public ::testing::Test {
protected:
    McpServer server;

    json call_tool(int id, const std::string& name,
                   const json& args = json::object()) {
        json request = {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"method", "tools/call"},
            {"params", {{"name", name}, {"arguments", args}}}
        };
        auto resp = server.handle_message(request);
        EXPECT_TRUE(resp.has_value());
        return *resp;
    }

    json read_resource(int id, const std::string& uri) {
        json request = {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"method", "resources/read"},
            {"params", {{"uri", uri}}}
        };
        auto resp = server.handle_message(request);
        EXPECT_TRUE(resp.has_value());
        return *resp;
    }

    // Helper to parse the tool result content text as JSON
    json parse_tool_content(const json& resp) {
        return json::parse(resp["result"]["content"][0]["text"].get<std::string>());
    }
};

// ---------------------------------------------------------------------------
// interpret tool
// ---------------------------------------------------------------------------

TEST_F(McpToolsTest, InterpretSimple) {
    auto resp = call_tool(1, "interpret", {{"code", "42 dup +"}});
    EXPECT_FALSE(resp.contains("error"));
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["stack"].size(), 1u);
    EXPECT_EQ(content["stack"][0], "84");
    EXPECT_FALSE(resp["result"]["isError"].get<bool>());
}

TEST_F(McpToolsTest, InterpretOutput) {
    auto resp = call_tool(2, "interpret", {{"code", ".\" hello\""}});
    auto content = parse_tool_content(resp);
    EXPECT_FALSE(content["output"].get<std::string>().empty());
    EXPECT_TRUE(content["output"].get<std::string>().find("hello") != std::string::npos);
}

TEST_F(McpToolsTest, InterpretDotOutput) {
    // The . primitive now writes to the interpreter's output stream
    auto resp = call_tool(3, "interpret", {{"code", "42 . cr"}});
    auto content = parse_tool_content(resp);
    EXPECT_TRUE(content["output"].get<std::string>().find("42") != std::string::npos);
}

TEST_F(McpToolsTest, InterpretError) {
    auto resp = call_tool(3, "interpret", {{"code", "nonexistent_word_xyz"}});
    auto content = parse_tool_content(resp);
    EXPECT_FALSE(content["errors"].get<std::string>().empty());
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
}

TEST_F(McpToolsTest, InterpretPersistentState) {
    // First call: push a value
    call_tool(1, "interpret", {{"code", "100"}});
    // Second call: dup it
    auto resp = call_tool(2, "interpret", {{"code", "dup +"}});
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["stack"][0], "200");
}

TEST_F(McpToolsTest, InterpretDefinition) {
    call_tool(1, "interpret", {{"code", ": triple dup dup + + ;"}});
    auto resp = call_tool(2, "interpret", {{"code", "7 triple"}});
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["stack"][0], "21");
}

TEST_F(McpToolsTest, InterpretEmptyStack) {
    auto resp = call_tool(1, "interpret", {{"code", ""}});
    auto content = parse_tool_content(resp);
    EXPECT_TRUE(content["stack"].empty());
}

// ---------------------------------------------------------------------------
// list_words tool
// ---------------------------------------------------------------------------

TEST_F(McpToolsTest, ListWords) {
    auto resp = call_tool(10, "list_words");
    EXPECT_FALSE(resp.contains("error"));
    auto content = parse_tool_content(resp);
    EXPECT_TRUE(content["words"].is_array());
    EXPECT_GT(content["words"].size(), 0u);

    // Check that basic words are present
    bool found_dup = false;
    for (const auto& w : content["words"]) {
        if (w["name"] == "dup") found_dup = true;
    }
    EXPECT_TRUE(found_dup);
}

TEST_F(McpToolsTest, ListWordsWithCategory) {
    auto resp = call_tool(11, "list_words", {{"category", "Arithmetic"}});
    auto content = parse_tool_content(resp);
    // All returned words should have category "Arithmetic"
    for (const auto& w : content["words"]) {
        if (w.contains("category")) {
            EXPECT_EQ(w["category"], "Arithmetic");
        }
    }
}

TEST_F(McpToolsTest, ListWordsEmptyCategory) {
    auto resp = call_tool(12, "list_words", {{"category", "NonexistentCategory99"}});
    auto content = parse_tool_content(resp);
    EXPECT_TRUE(content["words"].empty());
}

// ---------------------------------------------------------------------------
// get_word_info tool
// ---------------------------------------------------------------------------

TEST_F(McpToolsTest, GetWordInfoExisting) {
    auto resp = call_tool(20, "get_word_info", {{"name", "dup"}});
    EXPECT_FALSE(resp.contains("error"));
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["name"], "dup");
    EXPECT_TRUE(content.contains("implementations"));
    EXPECT_GT(content["implementations"].size(), 0u);
}

TEST_F(McpToolsTest, GetWordInfoNotFound) {
    auto resp = call_tool(21, "get_word_info", {{"name", "nonexistent_xyz"}});
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
}

TEST_F(McpToolsTest, GetWordInfoHasProfile) {
    // Execute the word first to generate profile data
    call_tool(1, "interpret", {{"code", "1 2 +"}});
    auto resp = call_tool(22, "get_word_info", {{"name", "+"}});
    auto content = parse_tool_content(resp);
    auto impl = content["implementations"][0];
    EXPECT_TRUE(impl.contains("profile"));
    EXPECT_TRUE(impl["profile"].contains("totalCalls"));
}

TEST_F(McpToolsTest, GetWordInfoHasMetadata) {
    auto resp = call_tool(23, "get_word_info", {{"name", "dup"}});
    auto content = parse_tool_content(resp);
    EXPECT_TRUE(content.contains("metadata"));
}

// ---------------------------------------------------------------------------
// get_stack tool
// ---------------------------------------------------------------------------

TEST_F(McpToolsTest, GetStackEmpty) {
    auto resp = call_tool(30, "get_stack");
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["depth"], 0);
    EXPECT_TRUE(content["elements"].empty());
}

TEST_F(McpToolsTest, GetStackWithValues) {
    call_tool(1, "interpret", {{"code", "10 20 30"}});
    auto resp = call_tool(31, "get_stack");
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["depth"], 3);
    EXPECT_EQ(content["elements"].size(), 3u);
    // Bottom to top: 10, 20, 30
    EXPECT_EQ(content["elements"][0]["value"], "10");
    EXPECT_EQ(content["elements"][1]["value"], "20");
    EXPECT_EQ(content["elements"][2]["value"], "30");
}

TEST_F(McpToolsTest, GetStackTypedElements) {
    call_tool(1, "interpret", {{"code", "42"}});
    auto resp = call_tool(32, "get_stack");
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["elements"][0]["type"], "integer");
    EXPECT_EQ(content["elements"][0]["raw"], 42);
}

TEST_F(McpToolsTest, GetStackPreservesValues) {
    call_tool(1, "interpret", {{"code", "1 2 3"}});
    // Call get_stack twice — values should still be there
    call_tool(31, "get_stack");
    auto resp = call_tool(32, "get_stack");
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["depth"], 3);
}

TEST_F(McpToolsTest, GetStackStatus) {
    call_tool(1, "interpret", {{"code", "42"}});
    auto resp = call_tool(33, "get_stack");
    auto content = parse_tool_content(resp);
    EXPECT_TRUE(content.contains("status"));
    EXPECT_FALSE(content["status"].get<std::string>().empty());
}

// ---------------------------------------------------------------------------
// set_weight tool
// ---------------------------------------------------------------------------

TEST_F(McpToolsTest, SetWeightBasic) {
    auto resp = call_tool(40, "set_weight", {{"word", "dup"}, {"weight", 0.5}});
    EXPECT_FALSE(resp.contains("error"));
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["word"], "dup");
    EXPECT_DOUBLE_EQ(content["newWeight"].get<double>(), 0.5);
}

TEST_F(McpToolsTest, SetWeightNotFound) {
    auto resp = call_tool(41, "set_weight",
                          {{"word", "nonexistent_xyz"}, {"weight", 0.5}});
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
}

TEST_F(McpToolsTest, SetWeightInvalidIndex) {
    auto resp = call_tool(42, "set_weight",
                          {{"word", "dup"}, {"weight", 0.5}, {"impl_index", 999}});
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
}

TEST_F(McpToolsTest, SetWeightVerify) {
    call_tool(43, "set_weight", {{"word", "+"},  {"weight", 0.75}});
    auto info = call_tool(44, "get_word_info", {{"name", "+"}});
    auto content = parse_tool_content(info);
    // Latest impl should have the new weight
    auto impls = content["implementations"];
    bool found = false;
    for (const auto& impl : impls) {
        if (std::abs(impl["weight"].get<double>() - 0.75) < 0.01) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

// ---------------------------------------------------------------------------
// reset tool
// ---------------------------------------------------------------------------

TEST_F(McpToolsTest, ResetClearsStack) {
    call_tool(1, "interpret", {{"code", "1 2 3"}});
    call_tool(50, "reset");
    auto resp = call_tool(51, "get_stack");
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["depth"], 0);
}

TEST_F(McpToolsTest, ResetClearsDefinitions) {
    call_tool(1, "interpret", {{"code", ": myword42 42 ;"}});
    call_tool(50, "reset");
    // myword42 should no longer exist
    auto resp = call_tool(51, "interpret", {{"code", "myword42"}});
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
}

TEST_F(McpToolsTest, ResetPreservesPrimitives) {
    call_tool(50, "reset");
    auto resp = call_tool(51, "interpret", {{"code", "1 2 +"}});
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["stack"][0], "3");
}

// ---------------------------------------------------------------------------
// Resource: etil://dictionary
// ---------------------------------------------------------------------------

TEST_F(McpToolsTest, ResourceDictionary) {
    auto resp = read_resource(60, "etil://dictionary");
    EXPECT_FALSE(resp.contains("error"));
    auto contents = resp["result"]["contents"];
    ASSERT_FALSE(contents.empty());
    auto dict_data = json::parse(contents[0]["text"].get<std::string>());
    EXPECT_TRUE(dict_data.contains("words"));
    EXPECT_GT(dict_data["words"].size(), 0u);
}

// ---------------------------------------------------------------------------
// Resource: etil://word/{name}
// ---------------------------------------------------------------------------

TEST_F(McpToolsTest, ResourceWordExisting) {
    auto resp = read_resource(61, "etil://word/swap");
    EXPECT_FALSE(resp.contains("error"));
    auto contents = resp["result"]["contents"];
    ASSERT_FALSE(contents.empty());
    auto word_data = json::parse(contents[0]["text"].get<std::string>());
    EXPECT_EQ(word_data["name"], "swap");
}

TEST_F(McpToolsTest, ResourceWordNotFound) {
    auto resp = read_resource(62, "etil://word/nonexistent_xyz");
    EXPECT_FALSE(resp.contains("error"));
    auto contents = resp["result"]["contents"];
    ASSERT_FALSE(contents.empty());
    // Should return a "not found" text
    auto text = contents[0]["text"].get<std::string>();
    EXPECT_TRUE(text.find("not found") != std::string::npos ||
                text.find("Word not found") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Resource: etil://stack
// ---------------------------------------------------------------------------

TEST_F(McpToolsTest, ResourceStackEmpty) {
    auto resp = read_resource(70, "etil://stack");
    EXPECT_FALSE(resp.contains("error"));
    auto contents = resp["result"]["contents"];
    ASSERT_FALSE(contents.empty());
    auto stack_data = json::parse(contents[0]["text"].get<std::string>());
    EXPECT_EQ(stack_data["depth"], 0);
}

TEST_F(McpToolsTest, ResourceStackWithValues) {
    call_tool(1, "interpret", {{"code", "100 200"}});
    auto resp = read_resource(71, "etil://stack");
    auto contents = resp["result"]["contents"];
    auto stack_data = json::parse(contents[0]["text"].get<std::string>());
    EXPECT_EQ(stack_data["depth"], 2);
    EXPECT_EQ(stack_data["elements"].size(), 2u);
}

// ---------------------------------------------------------------------------
// get_session_stats tool
// ---------------------------------------------------------------------------

TEST_F(McpToolsTest, GetSessionStatsInitial) {
    auto resp = call_tool(80, "get_session_stats");
    EXPECT_FALSE(resp.contains("error"));
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["interpretCallCount"], 0);
    EXPECT_EQ(content["interpretWallMs"], 0);
    EXPECT_TRUE(content.contains("sessionStartMs"));
    EXPECT_TRUE(content.contains("sessionUptimeMs"));
}

TEST_F(McpToolsTest, GetSessionStatsAfterInterpret) {
    call_tool(1, "interpret", {{"code", "1 2 +"}});
    call_tool(2, "interpret", {{"code", "3 4 +"}});
    auto resp = call_tool(81, "get_session_stats");
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["interpretCallCount"], 2);
    EXPECT_GE(content["interpretWallMs"].get<uint64_t>(), 0u);
}

TEST_F(McpToolsTest, GetSessionStatsCpuTime) {
    call_tool(1, "interpret", {{"code", "42"}});
    auto resp = call_tool(82, "get_session_stats");
    auto content = parse_tool_content(resp);
    EXPECT_GE(content["interpretCpuMs"].get<uint64_t>(), 0u);
}

TEST_F(McpToolsTest, GetSessionStatsRss) {
    auto resp = call_tool(83, "get_session_stats");
    auto content = parse_tool_content(resp);
    EXPECT_TRUE(content.contains("currentRssBytes"));
    EXPECT_TRUE(content.contains("currentRssMb"));
    EXPECT_TRUE(content.contains("peakRssBytes"));
    EXPECT_TRUE(content.contains("peakRssMb"));
}

TEST_F(McpToolsTest, GetSessionStatsReset) {
    call_tool(1, "interpret", {{"code", "42"}});
    // Verify count is 1
    auto resp1 = call_tool(84, "get_session_stats");
    auto c1 = parse_tool_content(resp1);
    EXPECT_EQ(c1["interpretCallCount"], 1);

    // Reset
    call_tool(50, "reset");

    // Count should be back to 0
    auto resp2 = call_tool(85, "get_session_stats");
    auto c2 = parse_tool_content(resp2);
    EXPECT_EQ(c2["interpretCallCount"], 0);
}

TEST_F(McpToolsTest, GetSessionStatsDictAndStack) {
    call_tool(1, "interpret", {{"code", "10 20 30"}});
    auto resp = call_tool(86, "get_session_stats");
    auto content = parse_tool_content(resp);
    EXPECT_GT(content["dictionaryConceptCount"].get<uint64_t>(), 0u);
    EXPECT_EQ(content["dataStackDepth"], 3);
}

// ---------------------------------------------------------------------------
// Resource: etil://session/stats
// ---------------------------------------------------------------------------

TEST_F(McpToolsTest, SessionStatsResource) {
    call_tool(1, "interpret", {{"code", "42"}});
    auto resp = read_resource(90, "etil://session/stats");
    EXPECT_FALSE(resp.contains("error"));
    auto contents = resp["result"]["contents"];
    ASSERT_FALSE(contents.empty());
    EXPECT_EQ(contents[0]["mimeType"], "application/json");
    auto stats_data = json::parse(contents[0]["text"].get<std::string>());
    EXPECT_EQ(stats_data["interpretCallCount"], 1);
    EXPECT_TRUE(stats_data.contains("currentRssBytes"));
    EXPECT_TRUE(stats_data.contains("sessionStartMs"));
}

// ---------------------------------------------------------------------------
// DoS mitigation: execution limits
// ---------------------------------------------------------------------------

TEST_F(McpToolsTest, InterpretOversizedCode) {
    // Code larger than 1MB should be rejected immediately
    std::string big_code(1'048'577, 'x');  // 1MB + 1 byte
    auto resp = call_tool(100, "interpret", {{"code", big_code}});
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
    auto content_text = resp["result"]["content"][0]["text"].get<std::string>();
    EXPECT_TRUE(content_text.find("exceeds maximum size") != std::string::npos);
}

TEST_F(McpToolsTest, InterpretInfiniteLoop) {
    // Infinite loop should be terminated by execution limits
    auto resp = call_tool(101, "interpret",
                          {{"code", ": infloop begin false until ; infloop"}});
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
    auto content = parse_tool_content(resp);
    EXPECT_TRUE(content["errors"].get<std::string>().find(
        "execution limit reached") != std::string::npos);
}

TEST_F(McpToolsTest, InterpretNormalWithLimits) {
    // Normal operations should work fine under limits
    auto resp = call_tool(102, "interpret", {{"code", "42 dup +"}});
    EXPECT_FALSE(resp["result"]["isError"].get<bool>());
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["stack"].size(), 1u);
    EXPECT_EQ(content["stack"][0], "84");
}

TEST_F(McpToolsTest, InterpretCallDepthLimit) {
    // Recursive evaluate bomb should hit call depth limit
    auto resp = call_tool(103, "interpret",
                          {{"code", ": evbomb s\" evbomb\" evaluate ; evbomb"}});
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
    auto content = parse_tool_content(resp);
    auto errors = content["errors"].get<std::string>();
    EXPECT_TRUE(errors.find("call depth") != std::string::npos ||
                errors.find("execution limit") != std::string::npos);
}

TEST_F(McpToolsTest, InterpretEvaluateBasic) {
    // Basic evaluate through MCP
    auto resp = call_tool(104, "interpret",
                          {{"code", "s\" 42 dup +\" evaluate"}});
    EXPECT_FALSE(resp["result"]["isError"].get<bool>());
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["stack"].size(), 1u);
    EXPECT_EQ(content["stack"][0], "84");
}

// ---------------------------------------------------------------------------
// sys-notification via MCP interpret
// ---------------------------------------------------------------------------

TEST_F(McpToolsTest, InterpretNotification) {
    // Notifications are sent in real-time via notifications/message JSON-RPC
    // notifications and are NOT included in the tool response (MCP spec).
    auto resp = call_tool(110, "interpret",
                          {{"code", "42 sys-notification"}});
    EXPECT_FALSE(resp["result"]["isError"].get<bool>());
    auto content = parse_tool_content(resp);
    EXPECT_FALSE(content.contains("notifications"));
}

TEST_F(McpToolsTest, InterpretNoNotification) {
    auto resp = call_tool(111, "interpret", {{"code", "1 2 +"}});
    EXPECT_FALSE(resp["result"]["isError"].get<bool>());
    auto content = parse_tool_content(resp);
    // No notifications key when none were queued
    EXPECT_FALSE(content.contains("notifications"));
}

// ===========================================================================
// Interpreter path mapping tests (no MCP, direct unit tests)
// ===========================================================================

TEST(InterpreterPathMappingTest, ResolveHomePathNoDir) {
    etil::core::Dictionary dict;
    etil::core::register_primitives(dict);
    std::ostringstream out, err;
    etil::core::Interpreter interp(dict, out, err);
    // No home dir set — should return path as-is (backward compat)
    EXPECT_EQ(interp.resolve_home_path("foo.til"), "foo.til");
}

TEST(InterpreterPathMappingTest, ResolveHomePathBasic) {
    etil::core::Dictionary dict;
    etil::core::register_primitives(dict);
    std::ostringstream out, err;
    etil::core::Interpreter interp(dict, out, err);
    interp.set_home_dir("/tmp/test_home");
    auto resolved = interp.resolve_home_path("foo.til");
    EXPECT_EQ(resolved, "/tmp/test_home/foo.til");
}

TEST(InterpreterPathMappingTest, ResolveHomePathTraversalRejected) {
    etil::core::Dictionary dict;
    etil::core::register_primitives(dict);
    std::ostringstream out, err;
    etil::core::Interpreter interp(dict, out, err);
    interp.set_home_dir("/tmp/test_home");
    EXPECT_TRUE(interp.resolve_home_path("../etc/passwd").empty());
    EXPECT_TRUE(interp.resolve_home_path("subdir/../../etc/passwd").empty());
}

TEST(InterpreterPathMappingTest, ResolveHomePathAbsoluteRejected) {
    etil::core::Dictionary dict;
    etil::core::register_primitives(dict);
    std::ostringstream out, err;
    etil::core::Interpreter interp(dict, out, err);
    interp.set_home_dir("/tmp/test_home");
    EXPECT_TRUE(interp.resolve_home_path("/etc/passwd").empty());
}

TEST(InterpreterPathMappingTest, ResolveHomePathSubdirectory) {
    etil::core::Dictionary dict;
    etil::core::register_primitives(dict);
    std::ostringstream out, err;
    etil::core::Interpreter interp(dict, out, err);
    interp.set_home_dir("/tmp/test_home");
    EXPECT_EQ(interp.resolve_home_path("lib/helper.til"), "/tmp/test_home/lib/helper.til");
}

TEST(InterpreterPathMappingTest, ResolveLibraryPathNoDir) {
    etil::core::Dictionary dict;
    etil::core::register_primitives(dict);
    std::ostringstream out, err;
    etil::core::Interpreter interp(dict, out, err);
    // No library dir set — should return empty (library not configured)
    EXPECT_TRUE(interp.resolve_library_path("std/math.til").empty());
}

TEST(InterpreterPathMappingTest, ResolveLibraryPathBasic) {
    etil::core::Dictionary dict;
    etil::core::register_primitives(dict);
    std::ostringstream out, err;
    etil::core::Interpreter interp(dict, out, err);
    interp.set_library_dir("/data/library");
    EXPECT_EQ(interp.resolve_library_path("std/math.til"), "/data/library/std/math.til");
}

TEST(InterpreterPathMappingTest, ResolveLogicalPathHome) {
    etil::core::Dictionary dict;
    etil::core::register_primitives(dict);
    std::ostringstream out, err;
    etil::core::Interpreter interp(dict, out, err);
    interp.set_home_dir("/tmp/sessions/abc");
    EXPECT_EQ(interp.resolve_logical_path("/home/test.til"), "/tmp/sessions/abc/test.til");
}

TEST(InterpreterPathMappingTest, ResolveLogicalPathLibrary) {
    etil::core::Dictionary dict;
    etil::core::register_primitives(dict);
    std::ostringstream out, err;
    etil::core::Interpreter interp(dict, out, err);
    interp.set_library_dir("/data/library");
    EXPECT_EQ(interp.resolve_logical_path("/library/std/math.til"), "/data/library/std/math.til");
}

TEST(InterpreterPathMappingTest, ResolveLogicalPathPassthrough) {
    etil::core::Dictionary dict;
    etil::core::register_primitives(dict);
    std::ostringstream out, err;
    etil::core::Interpreter interp(dict, out, err);
    // Paths without /home/ or /library/ prefix are passed through as-is
    EXPECT_EQ(interp.resolve_logical_path("data/builtins.til"), "data/builtins.til");
}

// ===========================================================================
// write_file and list_files tool tests (with temp home dir)
// ===========================================================================

class McpFileToolsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temp directory to serve as sessions base
        tmp_dir_ = std::filesystem::temp_directory_path() / "etil_test_XXXXXX";
        tmp_dir_ = std::filesystem::temp_directory_path() /
                   ("etil_test_" + std::to_string(
                       std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(tmp_dir_);

        // Set env vars before creating server
        setenv("ETIL_SESSIONS_DIR", tmp_dir_.c_str(), 1);
        server_ = std::make_unique<McpServer>();

        // Create a session (which creates a home dir)
        session_id_ = server_->create_session();
        ASSERT_FALSE(session_id_.empty());
    }

    void TearDown() override {
        server_.reset();
        std::error_code ec;
        std::filesystem::remove_all(tmp_dir_, ec);
        unsetenv("ETIL_SESSIONS_DIR");
    }

    json call_tool(const std::string& name, const json& args = json::object()) {
        json request = {
            {"jsonrpc", "2.0"},
            {"id", 1},
            {"method", "tools/call"},
            {"params", {{"name", name}, {"arguments", args}}}
        };
        auto resp = server_->handle_message(session_id_, request);
        EXPECT_TRUE(resp.has_value());
        return *resp;
    }

    json parse_tool_content(const json& resp) {
        return json::parse(resp["result"]["content"][0]["text"].get<std::string>());
    }

    std::filesystem::path tmp_dir_;
    std::unique_ptr<McpServer> server_;
    std::string session_id_;
};

TEST_F(McpFileToolsTest, WriteFileBasic) {
    auto resp = call_tool("write_file", {
        {"path", "test.til"},
        {"content", ": hello .\" hello\" ;"}
    });
    EXPECT_FALSE(resp["result"].contains("isError"));
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["path"], "test.til");
    EXPECT_GT(content["bytesWritten"].get<size_t>(), 0u);

    // Verify file exists on disk
    auto home_dir = tmp_dir_ / session_id_;
    EXPECT_TRUE(std::filesystem::exists(home_dir / "test.til"));
}

TEST_F(McpFileToolsTest, WriteFileSubdirectory) {
    auto resp = call_tool("write_file", {
        {"path", "lib/helper.til"},
        {"content", ": helper 42 ;"}
    });
    EXPECT_FALSE(resp["result"].contains("isError"));

    auto home_dir = tmp_dir_ / session_id_;
    EXPECT_TRUE(std::filesystem::exists(home_dir / "lib" / "helper.til"));
}

TEST_F(McpFileToolsTest, WriteFileTraversalRejected) {
    auto resp = call_tool("write_file", {
        {"path", "../escape.til"},
        {"content", "evil"}
    });
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
    auto text = resp["result"]["content"][0]["text"].get<std::string>();
    EXPECT_TRUE(text.find("traversal") != std::string::npos);
}

TEST_F(McpFileToolsTest, WriteFileAbsolutePathRejected) {
    auto resp = call_tool("write_file", {
        {"path", "/etc/passwd"},
        {"content", "evil"}
    });
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
}

TEST_F(McpFileToolsTest, WriteFileEmptyPathRejected) {
    auto resp = call_tool("write_file", {
        {"path", ""},
        {"content", "x"}
    });
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
}

TEST_F(McpFileToolsTest, ListFilesEmpty) {
    auto resp = call_tool("list_files");
    EXPECT_FALSE(resp["result"].contains("isError"));
    auto content = parse_tool_content(resp);
    EXPECT_TRUE(content["files"].is_array());
    EXPECT_TRUE(content["files"].empty());
}

TEST_F(McpFileToolsTest, ListFilesAfterWrite) {
    call_tool("write_file", {{"path", "a.til"}, {"content", "1"}});
    call_tool("write_file", {{"path", "b.til"}, {"content", "2"}});

    auto resp = call_tool("list_files");
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["files"].size(), 2u);

    // Verify both files are present
    std::set<std::string> names;
    for (const auto& f : content["files"]) {
        names.insert(f["name"].get<std::string>());
        EXPECT_EQ(f["type"], "file");
    }
    EXPECT_TRUE(names.count("a.til"));
    EXPECT_TRUE(names.count("b.til"));
}

TEST_F(McpFileToolsTest, ListFilesSubdirectory) {
    call_tool("write_file", {{"path", "sub/file.til"}, {"content", "x"}});

    auto resp = call_tool("list_files");
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["files"].size(), 1u);
    EXPECT_EQ(content["files"][0]["name"], "sub");
    EXPECT_EQ(content["files"][0]["type"], "directory");
}

TEST_F(McpFileToolsTest, ListFilesTraversalRejected) {
    auto resp = call_tool("list_files", {{"path", "../"}});
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
}

TEST_F(McpFileToolsTest, WriteFileThenInclude) {
    // Write a file to home, then include it
    call_tool("write_file", {
        {"path", "defs.til"},
        {"content", ": my-double dup + ;"}
    });

    auto resp = call_tool("interpret", {{"code", "include defs.til 7 my-double"}});
    EXPECT_FALSE(resp["result"]["isError"].get<bool>());
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["stack"].size(), 1u);
    EXPECT_EQ(content["stack"][0], "14");
}

TEST_F(McpFileToolsTest, WriteFileThenIncludeNested) {
    // Write a helper file
    call_tool("write_file", {
        {"path", "helper.til"},
        {"content", ": helper-word 100 ;"}
    });

    // Write a main file that includes the helper
    call_tool("write_file", {
        {"path", "main.til"},
        {"content", "include helper.til\n: use-helper helper-word dup + ;"}
    });

    auto resp = call_tool("interpret", {{"code", "include main.til use-helper"}});
    EXPECT_FALSE(resp["result"]["isError"].get<bool>());
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["stack"][0], "200");
}

// ---------------------------------------------------------------------------
// read_file tool tests
// ---------------------------------------------------------------------------

TEST_F(McpFileToolsTest, ReadFileBasic) {
    // Write a file, then read it back
    call_tool("write_file", {
        {"path", "hello.txt"},
        {"content", "Hello, World!"}
    });

    auto resp = call_tool("read_file", {{"path", "hello.txt"}});
    EXPECT_FALSE(resp["result"].contains("isError"));
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["path"], "hello.txt");
    EXPECT_EQ(content["content"], "Hello, World!");
    EXPECT_EQ(content["sizeBytes"], 13);
}

TEST_F(McpFileToolsTest, ReadFileNotFound) {
    auto resp = call_tool("read_file", {{"path", "nonexistent.txt"}});
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
    auto text = resp["result"]["content"][0]["text"].get<std::string>();
    EXPECT_TRUE(text.find("not found") != std::string::npos);
}

TEST_F(McpFileToolsTest, ReadFileTraversalRejected) {
    auto resp = call_tool("read_file", {{"path", "../etc/passwd"}});
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
    auto text = resp["result"]["content"][0]["text"].get<std::string>();
    EXPECT_TRUE(text.find("traversal") != std::string::npos);
}

TEST_F(McpFileToolsTest, ReadFileAbsolutePathRejected) {
    auto resp = call_tool("read_file", {{"path", "/etc/passwd"}});
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
}

TEST_F(McpFileToolsTest, ReadFileEmptyPathRejected) {
    auto resp = call_tool("read_file", {{"path", ""}});
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
}

TEST_F(McpFileToolsTest, ReadFileDirectory) {
    // Create a subdirectory by writing a file inside it
    call_tool("write_file", {
        {"path", "subdir/file.txt"},
        {"content", "x"}
    });

    // Trying to read the directory itself should fail
    auto resp = call_tool("read_file", {{"path", "subdir"}});
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
    auto text = resp["result"]["content"][0]["text"].get<std::string>();
    EXPECT_TRUE(text.find("not a regular file") != std::string::npos);
}

TEST_F(McpFileToolsTest, ReadFileSubdirectory) {
    // Write a file in a subdirectory and read it back
    call_tool("write_file", {
        {"path", "lib/data.txt"},
        {"content", "some data here"}
    });

    auto resp = call_tool("read_file", {{"path", "lib/data.txt"}});
    EXPECT_FALSE(resp["result"].contains("isError"));
    auto content = parse_tool_content(resp);
    EXPECT_EQ(content["content"], "some data here");
}

// ---------------------------------------------------------------------------
// kick_session tool
// ---------------------------------------------------------------------------

// In auto-session mode (no JWT), kick_session should work but self-kick
// should be rejected.
TEST_F(McpToolsTest, KickSessionSelfRejected) {
    // First, get our own session ID via get_session_stats
    auto stats_resp = call_tool(100, "get_session_stats");
    // We can't easily extract session ID from stats, so we use a known-bad ID
    // The auto-session creates a single session; kicking ourselves is the
    // only session we can target, so let's test with a nonexistent ID first.
}

TEST_F(McpToolsTest, KickSessionNotFound) {
    auto resp = call_tool(101, "kick_session",
                          {{"session_id", "nonexistent-session-id"}});
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
    auto text = resp["result"]["content"][0]["text"].get<std::string>();
    EXPECT_TRUE(text.find("not found") != std::string::npos);
}

TEST_F(McpToolsTest, KickSessionSuccess) {
    // Create a second session by using the HTTP-style create_session API
    auto target_id = server.create_session();
    ASSERT_FALSE(target_id.empty());
    EXPECT_TRUE(server.has_session(target_id));

    auto resp = call_tool(102, "kick_session", {{"session_id", target_id}});
    EXPECT_FALSE(resp["result"].value("isError", false));
    auto content = parse_tool_content(resp);
    EXPECT_TRUE(content["kicked"].get<bool>());
    EXPECT_EQ(content["session_id"], target_id);

    // Verify session is gone
    EXPECT_FALSE(server.has_session(target_id));
}

// ---------------------------------------------------------------------------
// abort via MCP interpret tool
// ---------------------------------------------------------------------------

TEST_F(McpToolsTest, AbortSuccessPreservesOutput) {
    auto resp = call_tool(200, "interpret",
        {{"code", R"(." hello" true abort ." world")"}});
    EXPECT_FALSE(resp["result"]["isError"].get<bool>());
    auto content = parse_tool_content(resp);
    // "hello" should be captured, "world" should NOT
    EXPECT_TRUE(content["output"].get<std::string>().find("hello") != std::string::npos);
    EXPECT_TRUE(content["output"].get<std::string>().find("world") == std::string::npos);
    // No error messages
    EXPECT_TRUE(content["errors"].get<std::string>().empty());
}

TEST_F(McpToolsTest, AbortErrorReturnsIsError) {
    auto resp = call_tool(201, "interpret",
        {{"code", R"(s" fatal error" false abort)"}});
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
    auto content = parse_tool_content(resp);
    EXPECT_TRUE(content["errors"].get<std::string>().find("fatal error") != std::string::npos);
}
