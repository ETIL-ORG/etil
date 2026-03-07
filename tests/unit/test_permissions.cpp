// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/mcp/role_permissions.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/interpreter.hpp"
#include "etil/core/primitives.hpp"
#include "etil/lvfs/lvfs.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

namespace etil::mcp {

class PermissionsTest : public ::testing::Test {
protected:
    void SetUp() override {
        dict_ = std::make_unique<core::Dictionary>();
        core::register_primitives(*dict_);
        interp_ = std::make_unique<core::Interpreter>(*dict_, out_, err_);
        interp_->load_startup_files({"data/builtins.til"});
        out_.str("");
        err_.str("");
    }

    core::ExecutionContext& ctx() { return interp_->context(); }

    std::unique_ptr<core::Dictionary> dict_;
    std::unique_ptr<core::Interpreter> interp_;
    std::ostringstream out_;
    std::ostringstream err_;
};

// --- evaluate permission ---

TEST_F(PermissionsTest, EvaluateNullPerms) {
    // nullptr perms = standalone = all permitted
    ASSERT_EQ(ctx().permissions(), nullptr);
    interp_->interpret_line("s\" 1 2 +\" evaluate");
    EXPECT_EQ(ctx().data_stack().size(), 1u);
    auto top = ctx().data_stack().pop();
    ASSERT_TRUE(top.has_value());
    EXPECT_EQ(top->as_int, 3);
}

TEST_F(PermissionsTest, EvaluateDenied) {
    RolePermissions perms;
    perms.evaluate = false;
    ctx().set_permissions(&perms);

    err_.str("");
    interp_->interpret_line("s\" 1 2 +\" evaluate");
    EXPECT_NE(err_.str().find("not permitted"), std::string::npos);
}

TEST_F(PermissionsTest, EvaluateAllowed) {
    RolePermissions perms;
    perms.evaluate = true;
    ctx().set_permissions(&perms);

    interp_->interpret_line("s\" 1 2 +\" evaluate");
    EXPECT_EQ(ctx().data_stack().size(), 1u);
    auto top = ctx().data_stack().pop();
    ASSERT_TRUE(top.has_value());
    EXPECT_EQ(top->as_int, 3);
}

TEST_F(PermissionsTest, EvaluateTaintedDenied) {
    RolePermissions perms;
    perms.evaluate = true;
    perms.evaluate_tainted = false;
    ctx().set_permissions(&perms);

    // Create a tainted string manually
    auto* hs = core::HeapString::create("1 2 +");
    hs->set_tainted(true);
    ctx().data_stack().push(core::Value::from(hs));

    err_.str("");
    interp_->interpret_line("evaluate");
    EXPECT_NE(err_.str().find("tainted"), std::string::npos);
}

TEST_F(PermissionsTest, EvaluateTaintedAllowed) {
    RolePermissions perms;
    perms.evaluate = true;
    perms.evaluate_tainted = true;
    ctx().set_permissions(&perms);

    auto* hs = core::HeapString::create("1 2 +");
    hs->set_tainted(true);
    ctx().data_stack().push(core::Value::from(hs));

    interp_->interpret_line("evaluate");
    EXPECT_EQ(ctx().data_stack().size(), 1u);
    auto top = ctx().data_stack().pop();
    ASSERT_TRUE(top.has_value());
    EXPECT_EQ(top->as_int, 3);
}

// --- sregex-replace untaint ---

TEST_F(PermissionsTest, SregexUntaintsWithPerm) {
    RolePermissions perms;
    perms.evaluate_tainted = true;
    ctx().set_permissions(&perms);

    // Push tainted string, pattern, replacement
    auto* hs = core::HeapString::create("hello123world");
    hs->set_tainted(true);
    ctx().data_stack().push(core::Value::from(hs));

    interp_->interpret_line("s\" [0-9]+\" s\" _\" sregex-replace");
    ASSERT_EQ(ctx().data_stack().size(), 1u);
    auto top = ctx().data_stack().pop();
    ASSERT_TRUE(top.has_value());
    ASSERT_EQ(top->type, core::Value::Type::String);
    auto* result = top->as_string();
    EXPECT_FALSE(result->is_tainted());  // untainted with permission
    result->release();
}

TEST_F(PermissionsTest, SregexKeepsTaintWithoutPerm) {
    RolePermissions perms;
    perms.evaluate_tainted = false;
    ctx().set_permissions(&perms);

    auto* hs = core::HeapString::create("hello123world");
    hs->set_tainted(true);
    ctx().data_stack().push(core::Value::from(hs));

    interp_->interpret_line("s\" [0-9]+\" s\" _\" sregex-replace");
    ASSERT_EQ(ctx().data_stack().size(), 1u);
    auto top = ctx().data_stack().pop();
    ASSERT_TRUE(top.has_value());
    ASSERT_EQ(top->type, core::Value::Type::String);
    auto* result = top->as_string();
    EXPECT_TRUE(result->is_tainted());  // stays tainted without permission
    result->release();
}

// --- lvfs_modify ---

TEST_F(PermissionsTest, LvfsModifyDenied) {
    // Set up LVFS with a temp directory
    auto tmp = std::filesystem::temp_directory_path() /
               ("etil_perm_test_" + std::to_string(getpid()));
    std::filesystem::create_directories(tmp);

    lvfs::Lvfs lvfs(tmp.string(), "");
    ctx().set_lvfs(&lvfs);

    RolePermissions perms;
    perms.lvfs_modify = false;
    ctx().set_permissions(&perms);

    err_.str("");
    interp_->interpret_line("s\" hello\" s\" /home/test.txt\" write-file-sync");

    // Should have pushed false (denied)
    ASSERT_GE(ctx().data_stack().size(), 1u);
    auto top = ctx().data_stack().pop();
    EXPECT_EQ(top->type, core::Value::Type::Boolean);
    EXPECT_FALSE(top->as_bool());
    EXPECT_NE(err_.str().find("not permitted"), std::string::npos);

    ctx().set_lvfs(nullptr);
    std::filesystem::remove_all(tmp);
}

TEST_F(PermissionsTest, LvfsModifyAllowed) {
    auto tmp = std::filesystem::temp_directory_path() /
               ("etil_perm_test_" + std::to_string(getpid()));
    std::filesystem::create_directories(tmp);

    lvfs::Lvfs lvfs(tmp.string(), "");
    ctx().set_lvfs(&lvfs);

    RolePermissions perms;
    perms.lvfs_modify = true;
    ctx().set_permissions(&perms);

    interp_->interpret_line("s\" hello\" s\" /home/test.txt\" write-file-sync");

    ASSERT_GE(ctx().data_stack().size(), 1u);
    auto top = ctx().data_stack().pop();
    EXPECT_EQ(top->type, core::Value::Type::Boolean);
    EXPECT_TRUE(top->as_bool());  // success

    // Verify file was written
    auto file_path = tmp / "test.txt";
    EXPECT_TRUE(std::filesystem::exists(file_path));

    ctx().set_lvfs(nullptr);
    std::filesystem::remove_all(tmp);
}

// --- disk quota ---

TEST_F(PermissionsTest, DiskQuotaExceeded) {
    auto tmp = std::filesystem::temp_directory_path() /
               ("etil_perm_quota_" + std::to_string(getpid()));
    std::filesystem::create_directories(tmp);

    // Pre-fill with a 500-byte file
    {
        std::ofstream ofs(tmp / "existing.txt");
        ofs << std::string(500, 'x');
    }

    lvfs::Lvfs lvfs(tmp.string(), "");
    ctx().set_lvfs(&lvfs);

    RolePermissions perms;
    perms.lvfs_modify = true;
    perms.disk_quota = 600;  // 600 byte quota, 500 used
    ctx().set_permissions(&perms);

    err_.str("");
    // Try to write 200 bytes — should exceed 600 byte quota
    interp_->interpret_line(
        "s\" 12345678901234567890123456789012345678901234567890"
        "12345678901234567890123456789012345678901234567890"
        "12345678901234567890123456789012345678901234567890"
        "12345678901234567890123456789012345678901234567890"
        "\" s\" /home/new.txt\" write-file-sync");

    ASSERT_GE(ctx().data_stack().size(), 1u);
    auto top = ctx().data_stack().pop();
    EXPECT_EQ(top->type, core::Value::Type::Boolean);
    EXPECT_FALSE(top->as_bool());  // denied
    EXPECT_NE(err_.str().find("quota"), std::string::npos);

    ctx().set_lvfs(nullptr);
    std::filesystem::remove_all(tmp);
}

// --- net_client_allowed ---

// Note: We can't easily test http-get in a unit test (requires full
// http client setup), but we can verify the permission struct defaults
// and that the field parses correctly from JSON.

TEST_F(PermissionsTest, NetClientDefaultDenied) {
    RolePermissions perms;  // defaults
    EXPECT_FALSE(perms.net_client_allowed);
    EXPECT_TRUE(perms.net_client_domains.empty());
    EXPECT_EQ(perms.net_client_quota, 100);
}

// --- list_sessions permission ---

TEST_F(PermissionsTest, ListSessionsDefaultDenied) {
    RolePermissions perms;  // defaults
    EXPECT_FALSE(perms.list_sessions);
}

TEST_F(PermissionsTest, ListSessionsAllowed) {
    RolePermissions perms;
    perms.list_sessions = true;
    EXPECT_TRUE(perms.list_sessions);
}

} // namespace etil::mcp
