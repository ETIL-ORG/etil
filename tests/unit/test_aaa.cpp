// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include <gtest/gtest.h>

#ifdef ETIL_MONGODB_ENABLED

#include "etil/aaa/user_store.hpp"
#include "etil/aaa/audit_log.hpp"
#include "etil/db/mongo_client.hpp"

#include <bsoncxx/json.hpp>

#include <cstdlib>

using namespace etil::aaa;
using namespace etil::db;

// ---------------------------------------------------------------------------
// UserStore unit tests (no live Atlas connection required)
// ---------------------------------------------------------------------------

TEST(UserStoreTest, NotAvailableWhenDisconnected) {
    MongoClient client;
    UserStore store(client);
    EXPECT_FALSE(store.available());
}

TEST(UserStoreTest, FindByEmailWhenNotConnected) {
    MongoClient client;
    UserStore store(client);
    auto result = store.find_by_email("test@example.com");
    EXPECT_FALSE(result.has_value());
}

TEST(UserStoreTest, CreateWhenNotConnected) {
    MongoClient client;
    UserStore store(client);
    auto result = store.create("test@example.com", "github:123",
                                "beta-tester", "Test User");
    EXPECT_FALSE(result.has_value());
}

TEST(UserStoreTest, RecordLoginWhenNotConnected) {
    MongoClient client;
    UserStore store(client);
    EXPECT_FALSE(store.record_login("test@example.com", "github:123"));
}

TEST(UserStoreTest, GetRoleWhenNotConnected) {
    MongoClient client;
    UserStore store(client);
    EXPECT_TRUE(store.get_role("test@example.com").empty());
}

TEST(UserStoreTest, EnsureIndexesWhenNotConnected) {
    MongoClient client;
    UserStore store(client);
    // Should not crash
    store.ensure_indexes();
}

// ---------------------------------------------------------------------------
// AuditLog unit tests (no live Atlas connection required)
// ---------------------------------------------------------------------------

TEST(AuditLogTest, NotAvailableWhenDisconnected) {
    MongoClient client;
    AuditLog log(client);
    EXPECT_FALSE(log.available());
}

TEST(AuditLogTest, LogPermissionDeniedWhenNotConnected) {
    MongoClient client;
    AuditLog log(client);
    // Should not crash — silently ignored
    log.log_permission_denied("test@example.com", "write_file", "beta-tester");
}

TEST(AuditLogTest, LogSessionCreateWhenNotConnected) {
    MongoClient client;
    AuditLog log(client);
    log.log_session_create("test@example.com", "sess-123", "admin");
}

TEST(AuditLogTest, LogSessionDestroyWhenNotConnected) {
    MongoClient client;
    AuditLog log(client);
    log.log_session_destroy("test@example.com", "sess-123");
}

TEST(AuditLogTest, LogLoginWhenNotConnected) {
    MongoClient client;
    AuditLog log(client);
    log.log_login("test@example.com", "github", "device_flow");
}

TEST(AuditLogTest, LogUserCreatedWhenNotConnected) {
    MongoClient client;
    AuditLog log(client);
    log.log_user_created("test@example.com", "github");
}

TEST(AuditLogTest, EnsureIndexesWhenNotConnected) {
    MongoClient client;
    AuditLog log(client);
    // Should not crash
    log.ensure_indexes();
}

// ---------------------------------------------------------------------------
// on_login_success tests
// ---------------------------------------------------------------------------

TEST(OnLoginSuccessTest, NullPointersNoOp) {
    // Should not crash with null pointers
    on_login_success(nullptr, nullptr, "test@example.com",
                     "github:123", "beta-tester", "github", "device_flow");
}

TEST(OnLoginSuccessTest, DisconnectedNoOp) {
    MongoClient client;
    UserStore store(client);
    AuditLog log(client);
    // Should not crash — disconnected guards kick in
    on_login_success(&store, &log, "test@example.com",
                     "github:123", "beta-tester", "github", "device_flow");
}

// ---------------------------------------------------------------------------
// Live Atlas tests (opt-in via ETIL_MONGODB_TEST_URI env var)
// ---------------------------------------------------------------------------

class AaaLiveTest : public ::testing::Test {
protected:
    std::unique_ptr<MongoClient> client;
    std::unique_ptr<UserStore> store;
    std::unique_ptr<AuditLog> audit;

    void SetUp() override {
        const char* uri = std::getenv("ETIL_MONGODB_TEST_URI");
        if (!uri || uri[0] == '\0') {
            GTEST_SKIP() << "ETIL_MONGODB_TEST_URI not set — skipping live test";
        }
        client = std::make_unique<MongoClient>();
        ASSERT_TRUE(client->connect(uri, "etil_test"));
        store = std::make_unique<UserStore>(*client);
        audit = std::make_unique<AuditLog>(*client);
        store->ensure_indexes();
        audit->ensure_indexes();
    }
};

TEST_F(AaaLiveTest, UserLifecycle) {
    std::string email = "aaa-test@example.com";

    // Clean up from previous runs
    auto cleanup_filter = bsoncxx::from_json(R"({"email":")" + email + "\"}");
    client->remove("users", cleanup_filter.view());

    // Create
    auto user = store->create(email, "github:live1",
                               "beta-tester", "Live Test");
    ASSERT_TRUE(user.has_value());
    EXPECT_EQ(user->email, email);
    EXPECT_EQ(user->role, "beta-tester");

    // Find
    auto found = store->find_by_email(email);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->login_count, 1);

    // Record login
    EXPECT_TRUE(store->record_login(email, "github:live2"));

    // Get role
    EXPECT_EQ(store->get_role(email), "beta-tester");

    // Cleanup
    client->remove("users", cleanup_filter.view());
}

TEST_F(AaaLiveTest, AuditLogEvents) {
    audit->log_login("live@example.com", "github", "device_flow");

    // Verify event exists
    auto audit_filter = bsoncxx::from_json(
        R"({"event":"login","email":"live@example.com"})");
    auto result = client->find("audit_log", audit_filter.view());
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->find("login"), std::string::npos);

    // Cleanup
    client->remove("audit_log", audit_filter.view());
}

#endif // ETIL_MONGODB_ENABLED

// Ensure the test binary links even when MongoDB is disabled
TEST(AaaDisabledTest, CompilationGuard) {
    SUCCEED();
}
