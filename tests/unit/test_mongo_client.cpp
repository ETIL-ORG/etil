// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include <gtest/gtest.h>

#ifdef ETIL_MONGODB_ENABLED

#include "etil/db/mongo_client.hpp"
#include "etil/db/mongo_primitives.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/mcp/role_permissions.hpp"

#include <bsoncxx/json.hpp>

#include <cstdio>
#include <fstream>

using namespace etil::db;
using namespace etil::core;

// ---------------------------------------------------------------------------
// MongoClient unit tests (no live Atlas connection required)
// ---------------------------------------------------------------------------

TEST(MongoClientTest, DefaultNotConnected) {
    MongoClient client;
    EXPECT_FALSE(client.connected());
}

TEST(MongoClientTest, ConnectInvalidUri) {
    MongoClient client;
    // Invalid URI should fail gracefully
    bool result = client.connect("mongodb://invalid-host-that-does-not-exist:27017",
                                  "test_db");
    // Note: connect() may succeed (pool creation doesn't verify connectivity)
    // or fail depending on driver behavior.  The important thing is it doesn't crash.
    (void)result;
}

TEST(MongoClientTest, CrudWhenNotConnected) {
    MongoClient client;
    auto empty_filter = bsoncxx::from_json("{}");
    auto update_doc = bsoncxx::from_json(R"({"$set":{"a":2}})");
    EXPECT_FALSE(client.find("test_coll", empty_filter.view()).has_value());
    EXPECT_FALSE(client.insert("test_coll", R"({"a":1})").has_value());
    EXPECT_EQ(client.count("test_coll", empty_filter.view()), -1);
    EXPECT_EQ(client.update("test_coll", empty_filter.view(), update_doc.view()), -1);
    EXPECT_EQ(client.remove("test_coll", empty_filter.view()), -1);
}

TEST(MongoClientTest, ResolveNoEnvDefaults) {
    // Unset all env vars for clean test
    unsetenv("ETIL_MONGODB_CONFIG");
    unsetenv("ETIL_MONGODB_URI");
    unsetenv("ETIL_MONGODB_DATABASE");

    auto cfg = MongoConnectionsConfig::resolve();
    EXPECT_TRUE(cfg.uri.empty());
    EXPECT_EQ(cfg.database, "etil");
    EXPECT_FALSE(cfg.enabled());
}

TEST(MongoClientTest, ResolveEnvOverrides) {
    unsetenv("ETIL_MONGODB_CONFIG");
    setenv("ETIL_MONGODB_URI", "mongodb://test-host:27017", 1);
    setenv("ETIL_MONGODB_DATABASE", "test_db", 1);

    auto cfg = MongoConnectionsConfig::resolve();
    EXPECT_EQ(cfg.uri, "mongodb://test-host:27017");
    EXPECT_EQ(cfg.database, "test_db");
    EXPECT_TRUE(cfg.enabled());

    unsetenv("ETIL_MONGODB_URI");
    unsetenv("ETIL_MONGODB_DATABASE");
}

TEST(MongoClientTest, FromFileDefaultConnection) {
    // Write a temporary connections file
    const char* path = "/tmp/etil_test_mongo_conns.json";
    {
        std::ofstream ofs(path);
        ofs << R"({
            "default": "prod",
            "connections": {
                "prod": { "uri": "mongodb+srv://prod-host", "database": "etil_prod" },
                "dev":  { "uri": "mongodb://localhost:27017", "database": "etil_dev" }
            }
        })";
    }

    auto cfg = MongoConnectionsConfig::from_file(path);
    EXPECT_EQ(cfg.uri, "mongodb+srv://prod-host");
    EXPECT_EQ(cfg.database, "etil_prod");
    EXPECT_TRUE(cfg.enabled());

    std::remove(path);
}

TEST(MongoClientTest, FromFileNamedConnection) {
    const char* path = "/tmp/etil_test_mongo_conns2.json";
    {
        std::ofstream ofs(path);
        ofs << R"({
            "default": "prod",
            "connections": {
                "prod": { "uri": "mongodb+srv://prod-host", "database": "etil_prod" },
                "dev":  { "uri": "mongodb://localhost:27017", "database": "etil_dev" }
            }
        })";
    }

    auto cfg = MongoConnectionsConfig::from_file(path, "dev");
    EXPECT_EQ(cfg.uri, "mongodb://localhost:27017");
    EXPECT_EQ(cfg.database, "etil_dev");

    std::remove(path);
}

TEST(MongoClientTest, FromFileMissingConnection) {
    const char* path = "/tmp/etil_test_mongo_conns3.json";
    {
        std::ofstream ofs(path);
        ofs << R"({
            "default": "prod",
            "connections": {
                "prod": { "uri": "mongodb+srv://prod-host", "database": "etil_prod" }
            }
        })";
    }

    EXPECT_THROW(MongoConnectionsConfig::from_file(path, "nonexistent"),
                 std::runtime_error);

    std::remove(path);
}

TEST(MongoClientTest, FromFileMissingDefault) {
    const char* path = "/tmp/etil_test_mongo_conns4.json";
    {
        std::ofstream ofs(path);
        ofs << R"({ "connections": { "prod": { "uri": "mongodb://x" } } })";
    }

    EXPECT_THROW(MongoConnectionsConfig::from_file(path),
                 std::runtime_error);

    std::remove(path);
}

TEST(MongoClientTest, ResolveFromConfigFile) {
    const char* path = "/tmp/etil_test_mongo_resolve.json";
    {
        std::ofstream ofs(path);
        ofs << R"({
            "default": "test",
            "connections": {
                "test": { "uri": "mongodb://file-host:27017", "database": "from_file" }
            }
        })";
    }

    unsetenv("ETIL_MONGODB_URI");
    unsetenv("ETIL_MONGODB_DATABASE");
    setenv("ETIL_MONGODB_CONFIG", path, 1);

    auto cfg = MongoConnectionsConfig::resolve();
    EXPECT_EQ(cfg.uri, "mongodb://file-host:27017");
    EXPECT_EQ(cfg.database, "from_file");

    // Env vars should override file values
    setenv("ETIL_MONGODB_DATABASE", "env_override", 1);
    cfg = MongoConnectionsConfig::resolve();
    EXPECT_EQ(cfg.uri, "mongodb://file-host:27017");
    EXPECT_EQ(cfg.database, "env_override");

    unsetenv("ETIL_MONGODB_CONFIG");
    unsetenv("ETIL_MONGODB_DATABASE");
    std::remove(path);
}

TEST(MongoClientTest, DatabaseDefaultsToEtil) {
    const char* path = "/tmp/etil_test_mongo_nodbname.json";
    {
        std::ofstream ofs(path);
        ofs << R"({
            "default": "minimal",
            "connections": {
                "minimal": { "uri": "mongodb://host:27017" }
            }
        })";
    }

    auto cfg = MongoConnectionsConfig::from_file(path);
    EXPECT_EQ(cfg.database, "etil");

    std::remove(path);
}

// ---------------------------------------------------------------------------
// Primitive tests (no MongoDB connection — test permission/null guards)
// ---------------------------------------------------------------------------

class MongoPrimitiveTest : public ::testing::Test {
protected:
    Dictionary dict;
    ExecutionContext ctx{0};

    void SetUp() override {
        register_primitives(dict);
        register_mongo_primitives(dict);
        ctx.set_dictionary(&dict);
    }
};

TEST_F(MongoPrimitiveTest, MongoFindNoPermission) {
    etil::mcp::RolePermissions perms;
    perms.mongo_access = false;
    ctx.set_permissions(&perms);

    auto* coll = HeapString::create("test_coll");
    auto* filter = HeapString::create("{}");
    auto* opts = HeapString::create("{}");
    ctx.data_stack().push(Value::from(coll));
    ctx.data_stack().push(Value::from(filter));
    ctx.data_stack().push(Value::from(opts));

    auto impl = dict.lookup("mongo-find");
    ASSERT_TRUE(impl.has_value());
    bool ok = (*impl)->native_code()(ctx);
    EXPECT_TRUE(ok);

    ASSERT_GE(ctx.data_stack().size(), 1u);
    auto flag = ctx.data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->as_int, 0);
}

TEST_F(MongoPrimitiveTest, MongoFindNoClient) {
    etil::mcp::RolePermissions perms;
    perms.mongo_access = true;
    ctx.set_permissions(&perms);

    auto* coll = HeapString::create("test_coll");
    auto* filter = HeapString::create("{}");
    auto* opts = HeapString::create("{}");
    ctx.data_stack().push(Value::from(coll));
    ctx.data_stack().push(Value::from(filter));
    ctx.data_stack().push(Value::from(opts));

    auto impl = dict.lookup("mongo-find");
    ASSERT_TRUE(impl.has_value());
    bool ok = (*impl)->native_code()(ctx);
    EXPECT_TRUE(ok);

    ASSERT_GE(ctx.data_stack().size(), 1u);
    auto flag = ctx.data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->as_int, 0);
}

TEST_F(MongoPrimitiveTest, MongoInsertNoPermission) {
    etil::mcp::RolePermissions perms;
    perms.mongo_access = false;
    ctx.set_permissions(&perms);

    auto* coll = HeapString::create("test_coll");
    auto* doc = HeapString::create(R"({"name":"test"})");
    ctx.data_stack().push(Value::from(coll));
    ctx.data_stack().push(Value::from(doc));

    auto impl = dict.lookup("mongo-insert");
    ASSERT_TRUE(impl.has_value());
    bool ok = (*impl)->native_code()(ctx);
    EXPECT_TRUE(ok);

    ASSERT_GE(ctx.data_stack().size(), 1u);
    auto flag = ctx.data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->as_int, 0);
}

TEST_F(MongoPrimitiveTest, MongoUpdateNoClient) {
    etil::mcp::RolePermissions perms;
    perms.mongo_access = true;
    ctx.set_permissions(&perms);

    auto* coll = HeapString::create("test_coll");
    auto* filter = HeapString::create("{}");
    auto* update = HeapString::create(R"({"$set":{"x":1}})");
    auto* opts = HeapString::create("{}");
    ctx.data_stack().push(Value::from(coll));
    ctx.data_stack().push(Value::from(filter));
    ctx.data_stack().push(Value::from(update));
    ctx.data_stack().push(Value::from(opts));

    auto impl = dict.lookup("mongo-update");
    ASSERT_TRUE(impl.has_value());
    bool ok = (*impl)->native_code()(ctx);
    EXPECT_TRUE(ok);

    ASSERT_GE(ctx.data_stack().size(), 1u);
    auto flag = ctx.data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->as_int, 0);
}

TEST_F(MongoPrimitiveTest, MongoDeleteNoClient) {
    etil::mcp::RolePermissions perms;
    perms.mongo_access = true;
    ctx.set_permissions(&perms);

    auto* coll = HeapString::create("test_coll");
    auto* filter = HeapString::create("{}");
    auto* opts = HeapString::create("{}");
    ctx.data_stack().push(Value::from(coll));
    ctx.data_stack().push(Value::from(filter));
    ctx.data_stack().push(Value::from(opts));

    auto impl = dict.lookup("mongo-delete");
    ASSERT_TRUE(impl.has_value());
    bool ok = (*impl)->native_code()(ctx);
    EXPECT_TRUE(ok);

    ASSERT_GE(ctx.data_stack().size(), 1u);
    auto flag = ctx.data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->as_int, 0);
}

TEST_F(MongoPrimitiveTest, AllFiveWordsRegistered) {
    EXPECT_TRUE(dict.lookup("mongo-find").has_value());
    EXPECT_TRUE(dict.lookup("mongo-count").has_value());
    EXPECT_TRUE(dict.lookup("mongo-insert").has_value());
    EXPECT_TRUE(dict.lookup("mongo-update").has_value());
    EXPECT_TRUE(dict.lookup("mongo-delete").has_value());
    // Removed words
    EXPECT_FALSE(dict.lookup("mongo-query").has_value());
    EXPECT_FALSE(dict.lookup("mongo-map-find").has_value());
    EXPECT_FALSE(dict.lookup("mongo-map-count").has_value());
    EXPECT_FALSE(dict.lookup("mongo-map-insert").has_value());
    EXPECT_FALSE(dict.lookup("mongo-map-update").has_value());
    EXPECT_FALSE(dict.lookup("mongo-map-delete").has_value());
}

TEST_F(MongoPrimitiveTest, MongoCountNoClient) {
    etil::mcp::RolePermissions perms;
    perms.mongo_access = true;
    ctx.set_permissions(&perms);

    auto* coll = HeapString::create("test_coll");
    auto* filter = HeapString::create("{}");
    auto* opts = HeapString::create("{}");
    ctx.data_stack().push(Value::from(coll));
    ctx.data_stack().push(Value::from(filter));
    ctx.data_stack().push(Value::from(opts));

    auto impl = dict.lookup("mongo-count");
    ASSERT_TRUE(impl.has_value());
    bool ok = (*impl)->native_code()(ctx);
    EXPECT_TRUE(ok);

    ASSERT_GE(ctx.data_stack().size(), 1u);
    auto flag = ctx.data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->as_int, 0);
}

TEST_F(MongoPrimitiveTest, StandalonePermissions) {
    // Standalone mode (no permissions set) — mongo primitives should
    // still fail gracefully because no MongoClientState is wired.
    auto* coll = HeapString::create("test_coll");
    auto* filter = HeapString::create("{}");
    auto* opts = HeapString::create("{}");
    ctx.data_stack().push(Value::from(coll));
    ctx.data_stack().push(Value::from(filter));
    ctx.data_stack().push(Value::from(opts));

    auto impl = dict.lookup("mongo-find");
    ASSERT_TRUE(impl.has_value());
    bool ok = (*impl)->native_code()(ctx);
    EXPECT_TRUE(ok);

    ASSERT_GE(ctx.data_stack().size(), 1u);
    auto flag = ctx.data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->as_int, 0);
}

// ---------------------------------------------------------------------------
// Live Atlas tests (opt-in via ETIL_MONGODB_TEST_URI env var)
// ---------------------------------------------------------------------------

class MongoClientLiveTest : public ::testing::Test {
protected:
    std::unique_ptr<MongoClient> client;

    void SetUp() override {
        const char* uri = std::getenv("ETIL_MONGODB_TEST_URI");
        if (!uri || uri[0] == '\0') {
            GTEST_SKIP() << "ETIL_MONGODB_TEST_URI not set — skipping live test";
        }
        client = std::make_unique<MongoClient>();
        ASSERT_TRUE(client->connect(uri, "etil_test"));
    }
};

TEST_F(MongoClientLiveTest, InsertFindDelete) {
    // Insert
    auto id = client->insert("live_test",
        R"({"test_key":"live_test_value","ts":)" +
        std::to_string(std::chrono::system_clock::now()
            .time_since_epoch().count()) + "}");
    ASSERT_TRUE(id.has_value());
    EXPECT_FALSE(id->empty());

    // Find
    auto filter = bsoncxx::from_json(R"({"test_key":"live_test_value"})");
    auto result = client->find("live_test", filter.view());
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->find("live_test_value"), std::string::npos);

    // Count
    auto count = client->count("live_test", filter.view());
    EXPECT_GE(count, 1);

    // Delete
    auto deleted = client->remove("live_test", filter.view());
    EXPECT_GE(deleted, 1);
}

#endif // ETIL_MONGODB_ENABLED

// Ensure the test binary links even when MongoDB is disabled
TEST(MongoClientDisabledTest, CompilationGuard) {
    // This test exists so the test binary always has at least one test
    SUCCEED();
}
