// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef ETIL_JWT_ENABLED

#include "etil/mcp/auth_config.hpp"
#include "etil/mcp/jwt_auth.hpp"

#include <jwt-cpp/traits/nlohmann-json/defaults.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <gtest/gtest.h>

namespace etil::mcp {

class JwtAuthTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Generate keypair at runtime using OpenSSL for reliable tests
        generate_test_keys();

        // Create temporary config/key files (PID in name for parallel safety)
        auto pid = std::to_string(getpid());
        config_path_ = std::filesystem::temp_directory_path() /
                        ("etil_test_auth_" + pid + ".json");
        key_path_ = std::filesystem::temp_directory_path() /
                     ("etil_test_jwt_" + pid + ".key");
        pub_path_ = std::filesystem::temp_directory_path() /
                     ("etil_test_jwt_" + pid + ".pub");

        // Write key files
        {
            std::ofstream ofs(key_path_);
            ofs << private_key_;
        }
        {
            std::ofstream ofs(pub_path_);
            ofs << public_key_;
        }

        // Write config JSON
        nlohmann::json config_json = {
            {"roles", {
                {"admin", {
                    {"http_domains", nlohmann::json::array({"*"})},
                    {"max_sessions", 20},
                    {"instruction_budget", 50000000},
                    {"file_io", true}
                }},
                {"researcher", {
                    {"http_domains", nlohmann::json::array({"httpbin.org", "*.noaa.gov"})},
                    {"max_sessions", 5},
                    {"instruction_budget", 10000000},
                    {"file_io", true}
                }},
                {"beta-tester", {
                    {"http_domains", nlohmann::json::array()},
                    {"max_sessions", 2},
                    {"instruction_budget", 10000000},
                    {"file_io", false}
                }}
            }},
            {"user_roles", {
                {"github:12345", "admin"},
                {"github:67890", "researcher"}
            }},
            {"default_role", "beta-tester"},
            {"jwt_private_key_file", key_path_.string()},
            {"jwt_public_key_file", pub_path_.string()},
            {"jwt_ttl_seconds", 3600}
        };

        {
            std::ofstream ofs(config_path_);
            ofs << config_json.dump(2);
        }

        config_ = AuthConfig::from_file(config_path_.string());
        auth_ = std::make_unique<JwtAuth>(
            config_.jwt_private_key, config_.jwt_public_key,
            config_.jwt_ttl_seconds);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(config_path_, ec);
        std::filesystem::remove(key_path_, ec);
        std::filesystem::remove(pub_path_, ec);
    }

    void generate_test_keys() {
        // Use OpenSSL CLI to generate a fresh 2048-bit RSA keypair.
        // Include PID in filenames to avoid races when ctest runs parallel processes.
        auto pid = std::to_string(getpid());
        auto key_file = std::filesystem::temp_directory_path() /
                         ("etil_test_gen_" + pid + ".key");
        auto pub_file = std::filesystem::temp_directory_path() /
                         ("etil_test_gen_" + pid + ".pub");

        std::string gen_cmd = "openssl genrsa -out " + key_file.string() +
                               " 2048 2>/dev/null";
        int rc = std::system(gen_cmd.c_str());
        if (rc != 0) {
            // Fallback: skip tests that require keys
            private_key_ = "";
            public_key_ = "";
            return;
        }

        std::string pub_cmd = "openssl rsa -in " + key_file.string() +
                               " -pubout -out " + pub_file.string() +
                               " 2>/dev/null";
        rc = std::system(pub_cmd.c_str());
        if (rc != 0) {
            private_key_ = "";
            public_key_ = "";
            return;
        }

        {
            std::ifstream ifs(key_file);
            std::ostringstream ss;
            ss << ifs.rdbuf();
            private_key_ = ss.str();
        }
        {
            std::ifstream ifs(pub_file);
            std::ostringstream ss;
            ss << ifs.rdbuf();
            public_key_ = ss.str();
        }

        std::error_code ec;
        std::filesystem::remove(key_file, ec);
        std::filesystem::remove(pub_file, ec);
    }

    std::filesystem::path config_path_;
    std::filesystem::path key_path_;
    std::filesystem::path pub_path_;
    std::string private_key_;
    std::string public_key_;
    AuthConfig config_;
    std::unique_ptr<JwtAuth> auth_;
};

// ---------------------------------------------------------------------------
// AuthConfig tests
// ---------------------------------------------------------------------------

TEST_F(JwtAuthTest, AuthConfigLoadsRoles) {
    ASSERT_EQ(config_.roles.size(), 3u);
    ASSERT_TRUE(config_.roles.count("admin"));
    ASSERT_TRUE(config_.roles.count("researcher"));
    ASSERT_TRUE(config_.roles.count("beta-tester"));
}

TEST_F(JwtAuthTest, AuthConfigAdminRole) {
    const auto& admin = config_.roles.at("admin");
    // http_domains → net_client_domains (backward compat)
    ASSERT_EQ(admin.net_client_domains.size(), 1u);
    EXPECT_EQ(admin.net_client_domains[0], "*");
    EXPECT_EQ(admin.max_sessions, 20);
    EXPECT_EQ(admin.instruction_budget, 50000000);
    // file_io:true → lvfs_modify:true (backward compat)
    EXPECT_TRUE(admin.lvfs_modify);
    // Inferred from non-empty domains
    EXPECT_TRUE(admin.net_client_allowed);
}

TEST_F(JwtAuthTest, AuthConfigResearcherRole) {
    const auto& researcher = config_.roles.at("researcher");
    ASSERT_EQ(researcher.net_client_domains.size(), 2u);
    EXPECT_EQ(researcher.net_client_domains[0], "httpbin.org");
    EXPECT_EQ(researcher.net_client_domains[1], "*.noaa.gov");
    EXPECT_EQ(researcher.max_sessions, 5);
    EXPECT_TRUE(researcher.lvfs_modify);
}

TEST_F(JwtAuthTest, AuthConfigBetaTesterRole) {
    const auto& bt = config_.roles.at("beta-tester");
    EXPECT_TRUE(bt.net_client_domains.empty());
    EXPECT_EQ(bt.max_sessions, 2);
    EXPECT_FALSE(bt.lvfs_modify);
}

TEST_F(JwtAuthTest, AuthConfigUserRoleMappings) {
    EXPECT_EQ(config_.user_roles.size(), 2u);
    EXPECT_EQ(config_.user_roles.at("github:12345"), "admin");
    EXPECT_EQ(config_.user_roles.at("github:67890"), "researcher");
}

TEST_F(JwtAuthTest, AuthConfigDefaultRole) {
    EXPECT_EQ(config_.default_role, "beta-tester");
}

TEST_F(JwtAuthTest, AuthConfigRoleForKnownUser) {
    EXPECT_EQ(config_.role_for("github:12345"), "admin");
    EXPECT_EQ(config_.role_for("github:67890"), "researcher");
}

TEST_F(JwtAuthTest, AuthConfigRoleForUnknownUser) {
    EXPECT_EQ(config_.role_for("github:99999"), "beta-tester");
    EXPECT_EQ(config_.role_for("google:12345"), "beta-tester");
}

TEST_F(JwtAuthTest, AuthConfigPermissionsForKnownUser) {
    auto* perms = config_.permissions_for("github:12345");
    ASSERT_NE(perms, nullptr);
    EXPECT_EQ(perms->max_sessions, 20);
    EXPECT_TRUE(perms->lvfs_modify);
}

TEST_F(JwtAuthTest, AuthConfigPermissionsForUnknownUser) {
    auto* perms = config_.permissions_for("github:99999");
    ASSERT_NE(perms, nullptr);
    // Should get beta-tester permissions
    EXPECT_EQ(perms->max_sessions, 2);
    EXPECT_FALSE(perms->lvfs_modify);
}

TEST_F(JwtAuthTest, AuthConfigLoadsKeys) {
    EXPECT_FALSE(config_.jwt_private_key.empty());
    EXPECT_FALSE(config_.jwt_public_key.empty());
    EXPECT_TRUE(config_.jwt_private_key.find("BEGIN") != std::string::npos);
    EXPECT_TRUE(config_.jwt_public_key.find("BEGIN") != std::string::npos);
}

TEST_F(JwtAuthTest, AuthConfigTtl) {
    EXPECT_EQ(config_.jwt_ttl_seconds, 3600);
}

TEST_F(JwtAuthTest, AuthConfigFromFileNotFound) {
    EXPECT_THROW(AuthConfig::from_file("/nonexistent/auth.json"),
                 std::runtime_error);
}

TEST_F(JwtAuthTest, AuthConfigFromFileBadJson) {
    auto bad_path = std::filesystem::temp_directory_path() / "etil_bad.json";
    {
        std::ofstream ofs(bad_path);
        ofs << "not valid json {{{";
    }
    EXPECT_THROW(AuthConfig::from_file(bad_path.string()),
                 std::runtime_error);
    std::filesystem::remove(bad_path);
}

// ---------------------------------------------------------------------------
// New permission field parsing tests
// ---------------------------------------------------------------------------

TEST_F(JwtAuthTest, AuthConfigParsesNewFields) {
    // Write a config with all 18 fields for a role
    auto pid = std::to_string(getpid());
    auto path = std::filesystem::temp_directory_path() /
                 ("etil_test_new_fields_" + pid + ".json");
    nlohmann::json cfg = {
        {"roles", {
            {"full", {
                {"max_sessions", 10},
                {"instruction_budget", 5000000},
                {"allowlist_admin", true},
                {"list_sessions", true},
                {"session_kick", true},
                {"send_system_notification", true},
                {"send_user_notification", true},
                {"lvfs_modify", true},
                {"disk_quota", 52428800},
                {"net_client_allowed", true},
                {"net_client_domains", nlohmann::json::array({"example.com"})},
                {"net_client_quota", 500},
                {"net_server_bind", true},
                {"net_server_tcp", true},
                {"net_server_udp", true},
                {"evaluate", true},
                {"evaluate_tainted", true}
            }}
        }},
        {"default_role", "full"}
    };
    { std::ofstream ofs(path); ofs << cfg.dump(2); }

    auto config = AuthConfig::from_file(path.string());
    std::filesystem::remove(path);

    ASSERT_TRUE(config.roles.count("full"));
    const auto& p = config.roles.at("full");
    EXPECT_EQ(p.max_sessions, 10);
    EXPECT_EQ(p.instruction_budget, 5000000);
    EXPECT_TRUE(p.allowlist_admin);
    EXPECT_TRUE(p.list_sessions);
    EXPECT_TRUE(p.session_kick);
    EXPECT_TRUE(p.send_system_notification);
    EXPECT_TRUE(p.send_user_notification);
    EXPECT_TRUE(p.lvfs_modify);
    EXPECT_EQ(p.disk_quota, 52428800);
    EXPECT_TRUE(p.net_client_allowed);
    ASSERT_EQ(p.net_client_domains.size(), 1u);
    EXPECT_EQ(p.net_client_domains[0], "example.com");
    EXPECT_EQ(p.net_client_quota, 500);
    EXPECT_TRUE(p.net_server_bind);
    EXPECT_TRUE(p.net_server_tcp);
    EXPECT_TRUE(p.net_server_udp);
    EXPECT_TRUE(p.evaluate);
    EXPECT_TRUE(p.evaluate_tainted);
}

TEST_F(JwtAuthTest, AuthConfigBackwardCompatFileIo) {
    auto pid = std::to_string(getpid());
    auto path = std::filesystem::temp_directory_path() /
                 ("etil_test_compat_fio_" + pid + ".json");
    nlohmann::json cfg = {
        {"roles", {{"old", {{"file_io", true}}}}},
        {"default_role", "old"}
    };
    { std::ofstream ofs(path); ofs << cfg.dump(2); }

    auto config = AuthConfig::from_file(path.string());
    std::filesystem::remove(path);

    const auto& p = config.roles.at("old");
    EXPECT_TRUE(p.lvfs_modify);  // file_io:true → lvfs_modify:true
}

TEST_F(JwtAuthTest, AuthConfigBackwardCompatHttpDomains) {
    auto pid = std::to_string(getpid());
    auto path = std::filesystem::temp_directory_path() /
                 ("etil_test_compat_http_" + pid + ".json");
    nlohmann::json cfg = {
        {"roles", {{"old", {{"http_domains", nlohmann::json::array({"a.com", "b.com"})}}}}},
        {"default_role", "old"}
    };
    { std::ofstream ofs(path); ofs << cfg.dump(2); }

    auto config = AuthConfig::from_file(path.string());
    std::filesystem::remove(path);

    const auto& p = config.roles.at("old");
    ASSERT_EQ(p.net_client_domains.size(), 2u);
    EXPECT_EQ(p.net_client_domains[0], "a.com");
    EXPECT_EQ(p.net_client_domains[1], "b.com");
    // Inferred from non-empty domains
    EXPECT_TRUE(p.net_client_allowed);
}

TEST_F(JwtAuthTest, AuthConfigNewFieldsPreferred) {
    auto pid = std::to_string(getpid());
    auto path = std::filesystem::temp_directory_path() /
                 ("etil_test_prefer_new_" + pid + ".json");
    nlohmann::json cfg = {
        {"roles", {{"mixed", {
            {"file_io", false},
            {"lvfs_modify", true},
            {"http_domains", nlohmann::json::array({"old.com"})},
            {"net_client_domains", nlohmann::json::array({"new.com"})}
        }}}},
        {"default_role", "mixed"}
    };
    { std::ofstream ofs(path); ofs << cfg.dump(2); }

    auto config = AuthConfig::from_file(path.string());
    std::filesystem::remove(path);

    const auto& p = config.roles.at("mixed");
    EXPECT_TRUE(p.lvfs_modify);  // new name wins over old
    ASSERT_EQ(p.net_client_domains.size(), 1u);
    EXPECT_EQ(p.net_client_domains[0], "new.com");  // new name wins
}

TEST_F(JwtAuthTest, AuthConfigNetClientInferredFromDomains) {
    auto pid = std::to_string(getpid());
    auto path = std::filesystem::temp_directory_path() /
                 ("etil_test_infer_net_" + pid + ".json");
    // No net_client_allowed set, but domains non-empty → infer true
    nlohmann::json cfg = {
        {"roles", {{"infer", {
            {"net_client_domains", nlohmann::json::array({"api.example.com"})}
        }}}},
        {"default_role", "infer"}
    };
    { std::ofstream ofs(path); ofs << cfg.dump(2); }

    auto config = AuthConfig::from_file(path.string());
    std::filesystem::remove(path);

    const auto& p = config.roles.at("infer");
    EXPECT_TRUE(p.net_client_allowed);
}

// ---------------------------------------------------------------------------
// JwtAuth tests
// ---------------------------------------------------------------------------

TEST_F(JwtAuthTest, MintAndValidateRoundTrip) {
    if (private_key_.empty()) GTEST_SKIP() << "OpenSSL not available";

    auto token = auth_->mint_token("github:12345", "admin@example.com", "admin");
    ASSERT_FALSE(token.empty());

    auto claims = auth_->validate_token(token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ(claims->sub, "github:12345");
    EXPECT_EQ(claims->email, "admin@example.com");
    EXPECT_EQ(claims->role, "admin");
}

TEST_F(JwtAuthTest, MintUsesProvidedRole) {
    if (private_key_.empty()) GTEST_SKIP() << "OpenSSL not available";

    auto token = auth_->mint_token("github:67890", "researcher@example.com", "researcher");
    auto claims = auth_->validate_token(token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ(claims->role, "researcher");
}

TEST_F(JwtAuthTest, MintWithDefaultRole) {
    if (private_key_.empty()) GTEST_SKIP() << "OpenSSL not available";

    auto token = auth_->mint_token("github:99999", "new@example.com", "beta-tester");
    auto claims = auth_->validate_token(token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ(claims->role, "beta-tester");
}

TEST_F(JwtAuthTest, ValidateExpiredToken) {
    if (private_key_.empty()) GTEST_SKIP() << "OpenSSL not available";

    // Create a token that expired 1 hour ago
    auto now = std::chrono::system_clock::now();
    auto token = jwt::create()
        .set_issuer(JwtAuth::ISSUER)
        .set_subject("github:12345")
        .set_issued_at(now - std::chrono::hours(2))
        .set_expires_at(now - std::chrono::hours(1))
        .set_payload_claim("email", jwt::claim(std::string("test@example.com")))
        .set_payload_claim("role", jwt::claim(std::string("admin")))
        .sign(jwt::algorithm::rs256("", private_key_, "", ""));

    auto claims = auth_->validate_token(token);
    EXPECT_FALSE(claims.has_value());
}

TEST_F(JwtAuthTest, ValidateInvalidSignature) {
    if (private_key_.empty()) GTEST_SKIP() << "OpenSSL not available";

    auto token = auth_->mint_token("github:12345", "test@example.com", "admin");
    ASSERT_FALSE(token.empty());

    // Tamper with the token (flip a character in the signature)
    std::string tampered = token;
    if (!tampered.empty()) {
        auto last_dot = tampered.rfind('.');
        if (last_dot != std::string::npos && last_dot + 1 < tampered.size()) {
            char& c = tampered[last_dot + 1];
            c = (c == 'A') ? 'B' : 'A';
        }
    }

    auto claims = auth_->validate_token(tampered);
    EXPECT_FALSE(claims.has_value());
}

TEST_F(JwtAuthTest, ValidateWrongIssuer) {
    if (private_key_.empty()) GTEST_SKIP() << "OpenSSL not available";

    auto now = std::chrono::system_clock::now();
    auto token = jwt::create()
        .set_issuer("wrong-issuer")
        .set_subject("github:12345")
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::hours(1))
        .set_payload_claim("role", jwt::claim(std::string("admin")))
        .sign(jwt::algorithm::rs256("", private_key_, "", ""));

    auto claims = auth_->validate_token(token);
    EXPECT_FALSE(claims.has_value());
}

TEST_F(JwtAuthTest, ValidateGarbageToken) {
    auto claims = auth_->validate_token("not.a.jwt");
    EXPECT_FALSE(claims.has_value());
}

TEST_F(JwtAuthTest, ValidateEmptyToken) {
    auto claims = auth_->validate_token("");
    EXPECT_FALSE(claims.has_value());
}

TEST_F(JwtAuthTest, ValidateWrongKey) {
    if (private_key_.empty()) GTEST_SKIP() << "OpenSSL not available";

    // Generate a different keypair and sign with it
    auto other_key = std::filesystem::temp_directory_path() /
                      "etil_test_other.key";
    std::string cmd = "openssl genrsa -out " + other_key.string() +
                       " 2048 2>/dev/null";
    int rc = std::system(cmd.c_str());
    if (rc != 0) GTEST_SKIP() << "OpenSSL keygen failed";

    std::string other_private;
    {
        std::ifstream ifs(other_key);
        std::ostringstream ss;
        ss << ifs.rdbuf();
        other_private = ss.str();
    }
    std::filesystem::remove(other_key);

    auto now = std::chrono::system_clock::now();
    auto token = jwt::create()
        .set_issuer(JwtAuth::ISSUER)
        .set_subject("github:12345")
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::hours(1))
        .set_payload_claim("role", jwt::claim(std::string("admin")))
        .sign(jwt::algorithm::rs256("", other_private, "", ""));

    auto claims = auth_->validate_token(token);
    EXPECT_FALSE(claims.has_value());
}

// ---------------------------------------------------------------------------
// Phase 4.1: JWT validation hardening tests
// ---------------------------------------------------------------------------

TEST_F(JwtAuthTest, ValidateRejectsEmptySubject) {
    if (private_key_.empty()) GTEST_SKIP() << "OpenSSL not available";

    auto now = std::chrono::system_clock::now();
    auto token = jwt::create()
        .set_issuer(JwtAuth::ISSUER)
        .set_subject("")
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::hours(1))
        .set_payload_claim("role", jwt::claim(std::string("admin")))
        .sign(jwt::algorithm::rs256("", private_key_, "", ""));

    auto claims = auth_->validate_token(token);
    EXPECT_FALSE(claims.has_value());
}

TEST_F(JwtAuthTest, ValidateRejectsFutureIat) {
    if (private_key_.empty()) GTEST_SKIP() << "OpenSSL not available";

    // Token issued 5 minutes in the future (beyond 60s leeway)
    auto now = std::chrono::system_clock::now();
    auto token = jwt::create()
        .set_issuer(JwtAuth::ISSUER)
        .set_subject("github:12345")
        .set_issued_at(now + std::chrono::minutes(5))
        .set_expires_at(now + std::chrono::hours(2))
        .set_payload_claim("role", jwt::claim(std::string("admin")))
        .sign(jwt::algorithm::rs256("", private_key_, "", ""));

    auto claims = auth_->validate_token(token);
    EXPECT_FALSE(claims.has_value());
}

TEST_F(JwtAuthTest, ValidateAcceptsSlightClockSkew) {
    if (private_key_.empty()) GTEST_SKIP() << "OpenSSL not available";

    // Token expired 30 seconds ago — should be accepted with 60s leeway
    auto now = std::chrono::system_clock::now();
    auto token = jwt::create()
        .set_issuer(JwtAuth::ISSUER)
        .set_subject("github:12345")
        .set_issued_at(now - std::chrono::hours(1))
        .set_expires_at(now - std::chrono::seconds(30))
        .set_payload_claim("email", jwt::claim(std::string("test@example.com")))
        .set_payload_claim("role", jwt::claim(std::string("admin")))
        .sign(jwt::algorithm::rs256("", private_key_, "", ""));

    auto claims = auth_->validate_token(token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ(claims->sub, "github:12345");
}

TEST_F(JwtAuthTest, ValidateRejectsNonStringEmail) {
    if (private_key_.empty()) GTEST_SKIP() << "OpenSSL not available";

    auto now = std::chrono::system_clock::now();
    auto token = jwt::create()
        .set_issuer(JwtAuth::ISSUER)
        .set_subject("github:12345")
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::hours(1))
        .set_payload_claim("email", jwt::claim(int64_t(42)))
        .set_payload_claim("role", jwt::claim(std::string("admin")))
        .sign(jwt::algorithm::rs256("", private_key_, "", ""));

    auto claims = auth_->validate_token(token);
    EXPECT_FALSE(claims.has_value());
}

TEST_F(JwtAuthTest, ValidateRejectsNonStringRole) {
    if (private_key_.empty()) GTEST_SKIP() << "OpenSSL not available";

    auto now = std::chrono::system_clock::now();
    auto token = jwt::create()
        .set_issuer(JwtAuth::ISSUER)
        .set_subject("github:12345")
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::hours(1))
        .set_payload_claim("email", jwt::claim(std::string("test@example.com")))
        .set_payload_claim("role", jwt::claim(int64_t(99)))
        .sign(jwt::algorithm::rs256("", private_key_, "", ""));

    auto claims = auth_->validate_token(token);
    EXPECT_FALSE(claims.has_value());
}

// ---------------------------------------------------------------------------
// from_directory() tests
// ---------------------------------------------------------------------------

TEST_F(JwtAuthTest, FromDirectoryLoadsThreeFiles) {
    if (private_key_.empty()) GTEST_SKIP() << "OpenSSL not available";

    auto pid = std::to_string(getpid());
    auto dir = std::filesystem::temp_directory_path() /
               ("etil_test_dir_" + pid);
    std::filesystem::create_directories(dir);

    // roles.json
    nlohmann::json roles = {
        {"roles", {
            {"admin", {{"max_sessions", 20}, {"lvfs_modify", true}}},
            {"guest", {{"max_sessions", 1}}}
        }},
        {"default_role", "guest"}
    };
    { std::ofstream ofs(dir / "roles.json"); ofs << roles.dump(2); }

    // keys.json
    nlohmann::json keys = {
        {"jwt_private_key_file", key_path_.string()},
        {"jwt_public_key_file", pub_path_.string()},
        {"jwt_ttl_seconds", 7200},
        {"providers", {
            {"github", {{"enabled", true}, {"client_id", "gh-test"}}}
        }}
    };
    { std::ofstream ofs(dir / "keys.json"); ofs << keys.dump(2); }

    // users.json
    nlohmann::json users = {
        {"user_roles", {{"github:111", "admin"}}}
    };
    { std::ofstream ofs(dir / "users.json"); ofs << users.dump(2); }

    auto config = AuthConfig::from_directory(dir.string());

    // Roles loaded
    ASSERT_EQ(config.roles.size(), 2u);
    EXPECT_EQ(config.roles.at("admin").max_sessions, 20);
    EXPECT_TRUE(config.roles.at("admin").lvfs_modify);
    EXPECT_EQ(config.roles.at("guest").max_sessions, 1);
    EXPECT_EQ(config.default_role, "guest");

    // Users loaded
    ASSERT_EQ(config.user_roles.size(), 1u);
    EXPECT_EQ(config.user_roles.at("github:111"), "admin");

    // Keys loaded
    EXPECT_FALSE(config.jwt_private_key.empty());
    EXPECT_FALSE(config.jwt_public_key.empty());
    EXPECT_EQ(config.jwt_ttl_seconds, 7200);
    ASSERT_EQ(config.providers.size(), 1u);
    EXPECT_EQ(config.providers.at("github").client_id, "gh-test");

    // role_for / permissions_for work
    EXPECT_EQ(config.role_for("github:111"), "admin");
    EXPECT_EQ(config.role_for("unknown"), "guest");

    std::filesystem::remove_all(dir);
}

TEST_F(JwtAuthTest, FromDirectoryMissingFilesOk) {
    auto pid = std::to_string(getpid());
    auto dir = std::filesystem::temp_directory_path() /
               ("etil_test_empty_dir_" + pid);
    std::filesystem::create_directories(dir);

    // Only roles.json — no keys.json or users.json
    nlohmann::json roles = {
        {"roles", {{"solo", {{"max_sessions", 3}}}}},
        {"default_role", "solo"}
    };
    { std::ofstream ofs(dir / "roles.json"); ofs << roles.dump(2); }

    auto config = AuthConfig::from_directory(dir.string());

    ASSERT_EQ(config.roles.size(), 1u);
    EXPECT_EQ(config.default_role, "solo");
    EXPECT_TRUE(config.user_roles.empty());
    EXPECT_TRUE(config.jwt_private_key.empty());
    EXPECT_TRUE(config.providers.empty());

    std::filesystem::remove_all(dir);
}

TEST_F(JwtAuthTest, FromDirectoryBadJsonThrows) {
    auto pid = std::to_string(getpid());
    auto dir = std::filesystem::temp_directory_path() /
               ("etil_test_bad_dir_" + pid);
    std::filesystem::create_directories(dir);

    { std::ofstream ofs(dir / "roles.json"); ofs << "not valid {{{"; }

    EXPECT_THROW(AuthConfig::from_directory(dir.string()),
                 std::runtime_error);

    std::filesystem::remove_all(dir);
}

} // namespace etil::mcp

#endif // ETIL_JWT_ENABLED
