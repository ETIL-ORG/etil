// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef ETIL_JWT_ENABLED

#include "etil/mcp/auth_config.hpp"
#include "etil/mcp/jwt_auth.hpp"
#include "etil/mcp/oauth_provider.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <gtest/gtest.h>

namespace etil::mcp {

// ---------------------------------------------------------------------------
// Mock provider for testing endpoint logic without real network calls.
// ---------------------------------------------------------------------------

class MockProvider : public OAuthProvider {
public:
    const char* name() const override { return "mock"; }

    std::optional<DeviceCodeResponse> request_device_code() override {
        if (fail_device_code) return std::nullopt;
        return device_code_response;
    }

    std::optional<PollResult> poll_device_code(
        const std::string& /*device_code*/) override {
        if (fail_poll) return std::nullopt;
        return poll_result;
    }

    std::optional<ProviderUserInfo> get_user_info(
        const std::string& /*access_token*/) override {
        if (fail_user_info) return std::nullopt;
        return user_info;
    }

    // Configurable responses
    DeviceCodeResponse device_code_response{
        "DEVICE123", "WDJB-MJHT",
        "https://example.com/device", 900, 5};
    PollResult poll_result{PollResult::Status::Pending, "", "", 0};
    ProviderUserInfo user_info{"12345", "user@example.com"};
    bool fail_device_code = false;
    bool fail_poll = false;
    bool fail_user_info = false;
};

// ---------------------------------------------------------------------------
// AuthConfig provider parsing tests
// ---------------------------------------------------------------------------

class OAuthConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        generate_test_keys();

        auto pid = std::to_string(getpid());
        config_path_ = std::filesystem::temp_directory_path() /
                        ("etil_oauth_test_" + pid + ".json");
        key_path_ = std::filesystem::temp_directory_path() /
                     ("etil_oauth_test_" + pid + ".key");
        pub_path_ = std::filesystem::temp_directory_path() /
                     ("etil_oauth_test_" + pid + ".pub");

        {
            std::ofstream ofs(key_path_);
            ofs << private_key_;
        }
        {
            std::ofstream ofs(pub_path_);
            ofs << public_key_;
        }
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(config_path_, ec);
        std::filesystem::remove(key_path_, ec);
        std::filesystem::remove(pub_path_, ec);
    }

    void write_config(const nlohmann::json& config_json) {
        std::ofstream ofs(config_path_);
        ofs << config_json.dump(2);
    }

    nlohmann::json base_config() {
        return {
            {"roles", {
                {"admin", {{"http_domains", nlohmann::json::array({"*"})},
                           {"max_sessions", 20},
                           {"instruction_budget", 50000000},
                           {"file_io", true}}},
                {"beta-tester", {{"http_domains", nlohmann::json::array()},
                                 {"max_sessions", 2},
                                 {"instruction_budget", 10000000},
                                 {"file_io", false}}}
            }},
            {"user_roles", {{"github:12345", "admin"}}},
            {"default_role", "beta-tester"},
            {"jwt_private_key_file", key_path_.string()},
            {"jwt_public_key_file", pub_path_.string()},
            {"jwt_ttl_seconds", 3600}
        };
    }

    void generate_test_keys() {
        auto pid = std::to_string(getpid());
        auto key_file = std::filesystem::temp_directory_path() /
                         ("etil_oauth_gen_" + pid + ".key");
        auto pub_file = std::filesystem::temp_directory_path() /
                         ("etil_oauth_gen_" + pid + ".pub");

        std::string gen_cmd = "openssl genrsa -out " + key_file.string() +
                               " 2048 2>/dev/null";
        int rc = std::system(gen_cmd.c_str());
        if (rc != 0) { private_key_ = ""; public_key_ = ""; return; }

        std::string pub_cmd = "openssl rsa -in " + key_file.string() +
                               " -pubout -out " + pub_file.string() +
                               " 2>/dev/null";
        rc = std::system(pub_cmd.c_str());
        if (rc != 0) { private_key_ = ""; public_key_ = ""; return; }

        { std::ifstream ifs(key_file); std::ostringstream ss;
          ss << ifs.rdbuf(); private_key_ = ss.str(); }
        { std::ifstream ifs(pub_file); std::ostringstream ss;
          ss << ifs.rdbuf(); public_key_ = ss.str(); }

        std::error_code ec;
        std::filesystem::remove(key_file, ec);
        std::filesystem::remove(pub_file, ec);
    }

    std::filesystem::path config_path_;
    std::filesystem::path key_path_;
    std::filesystem::path pub_path_;
    std::string private_key_;
    std::string public_key_;
};

TEST_F(OAuthConfigTest, ParseGitHubProvider) {
    auto j = base_config();
    j["providers"] = {
        {"github", {{"enabled", true},
                    {"client_id", "Iv1.test123"}}}
    };
    write_config(j);

    auto config = AuthConfig::from_file(config_path_.string());
    ASSERT_EQ(config.providers.size(), 1u);
    ASSERT_TRUE(config.providers.count("github"));
    EXPECT_TRUE(config.providers.at("github").enabled);
    EXPECT_EQ(config.providers.at("github").client_id, "Iv1.test123");
    EXPECT_TRUE(config.providers.at("github").client_secret.empty());
}

TEST_F(OAuthConfigTest, ParseGoogleProvider) {
    auto j = base_config();
    j["providers"] = {
        {"google", {{"enabled", true},
                    {"client_id", "123.apps.googleusercontent.com"},
                    {"client_secret", "GOCSPX-secret"}}}
    };
    write_config(j);

    auto config = AuthConfig::from_file(config_path_.string());
    ASSERT_EQ(config.providers.size(), 1u);
    ASSERT_TRUE(config.providers.count("google"));
    EXPECT_TRUE(config.providers.at("google").enabled);
    EXPECT_EQ(config.providers.at("google").client_id,
              "123.apps.googleusercontent.com");
    EXPECT_EQ(config.providers.at("google").client_secret, "GOCSPX-secret");
}

TEST_F(OAuthConfigTest, ParseMultipleProviders) {
    auto j = base_config();
    j["providers"] = {
        {"github", {{"enabled", true}, {"client_id", "gh-id"}}},
        {"google", {{"enabled", false}, {"client_id", "g-id"},
                    {"client_secret", "g-secret"}}}
    };
    write_config(j);

    auto config = AuthConfig::from_file(config_path_.string());
    ASSERT_EQ(config.providers.size(), 2u);
    EXPECT_TRUE(config.providers.at("github").enabled);
    EXPECT_FALSE(config.providers.at("google").enabled);
}

TEST_F(OAuthConfigTest, ParseDisabledProvider) {
    auto j = base_config();
    j["providers"] = {
        {"github", {{"enabled", false}, {"client_id", "unused"}}}
    };
    write_config(j);

    auto config = AuthConfig::from_file(config_path_.string());
    ASSERT_EQ(config.providers.size(), 1u);
    EXPECT_FALSE(config.providers.at("github").enabled);
}

TEST_F(OAuthConfigTest, ParseMissingClientId) {
    auto j = base_config();
    j["providers"] = {
        {"github", {{"enabled", true}}}
    };
    write_config(j);

    auto config = AuthConfig::from_file(config_path_.string());
    ASSERT_EQ(config.providers.size(), 1u);
    EXPECT_TRUE(config.providers.at("github").client_id.empty());
}

TEST_F(OAuthConfigTest, NoProvidersKeyBackwardCompat) {
    auto j = base_config();
    // No "providers" key at all — backward compatible
    write_config(j);

    auto config = AuthConfig::from_file(config_path_.string());
    EXPECT_TRUE(config.providers.empty());
    // Existing config still works
    EXPECT_EQ(config.roles.size(), 2u);
    EXPECT_EQ(config.default_role, "beta-tester");
}

// ---------------------------------------------------------------------------
// Mock provider tests (endpoint logic without network)
// ---------------------------------------------------------------------------

TEST(MockProviderTest, DeviceCodeSuccess) {
    MockProvider mock;
    auto resp = mock.request_device_code();
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->device_code, "DEVICE123");
    EXPECT_EQ(resp->user_code, "WDJB-MJHT");
    EXPECT_EQ(resp->verification_uri, "https://example.com/device");
    EXPECT_EQ(resp->expires_in, 900);
    EXPECT_EQ(resp->interval, 5);
}

TEST(MockProviderTest, DeviceCodeFailure) {
    MockProvider mock;
    mock.fail_device_code = true;
    auto resp = mock.request_device_code();
    EXPECT_FALSE(resp.has_value());
}

TEST(MockProviderTest, PollPending) {
    MockProvider mock;
    mock.poll_result.status = PollResult::Status::Pending;
    auto result = mock.poll_device_code("any");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, PollResult::Status::Pending);
}

TEST(MockProviderTest, PollGranted) {
    MockProvider mock;
    mock.poll_result.status = PollResult::Status::Granted;
    mock.poll_result.access_token = "gho_test123";
    auto result = mock.poll_device_code("any");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, PollResult::Status::Granted);
    EXPECT_EQ(result->access_token, "gho_test123");
}

TEST(MockProviderTest, PollSlowDown) {
    MockProvider mock;
    mock.poll_result.status = PollResult::Status::SlowDown;
    mock.poll_result.interval = 10;
    auto result = mock.poll_device_code("any");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, PollResult::Status::SlowDown);
    EXPECT_EQ(result->interval, 10);
}

TEST(MockProviderTest, PollExpiredToken) {
    MockProvider mock;
    mock.poll_result.status = PollResult::Status::ExpiredToken;
    mock.poll_result.error_description = "Code expired";
    auto result = mock.poll_device_code("any");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, PollResult::Status::ExpiredToken);
    EXPECT_EQ(result->error_description, "Code expired");
}

TEST(MockProviderTest, PollAccessDenied) {
    MockProvider mock;
    mock.poll_result.status = PollResult::Status::AccessDenied;
    mock.poll_result.error_description = "User denied";
    auto result = mock.poll_device_code("any");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, PollResult::Status::AccessDenied);
}

TEST(MockProviderTest, PollError) {
    MockProvider mock;
    mock.poll_result.status = PollResult::Status::Error;
    mock.poll_result.error_description = "Something broke";
    auto result = mock.poll_device_code("any");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, PollResult::Status::Error);
}

TEST(MockProviderTest, PollNetworkFailure) {
    MockProvider mock;
    mock.fail_poll = true;
    auto result = mock.poll_device_code("any");
    EXPECT_FALSE(result.has_value());
}

TEST(MockProviderTest, UserInfoSuccess) {
    MockProvider mock;
    auto info = mock.get_user_info("token");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->provider_id, "12345");
    EXPECT_EQ(info->email, "user@example.com");
}

TEST(MockProviderTest, UserInfoFailure) {
    MockProvider mock;
    mock.fail_user_info = true;
    auto info = mock.get_user_info("token");
    EXPECT_FALSE(info.has_value());
}

// ---------------------------------------------------------------------------
// User ID formatting tests
// ---------------------------------------------------------------------------

TEST(OAuthUserIdTest, GitHubFormat) {
    // Provider name + ":" + provider_id
    std::string provider_name = "github";
    std::string provider_id = "12345";
    std::string user_id = provider_name + ":" + provider_id;
    EXPECT_EQ(user_id, "github:12345");
}

TEST(OAuthUserIdTest, GoogleFormat) {
    std::string provider_name = "google";
    std::string provider_id = "110248495827631005424";
    std::string user_id = provider_name + ":" + provider_id;
    EXPECT_EQ(user_id, "google:110248495827631005424");
}

// ---------------------------------------------------------------------------
// JWT minting after OAuth validation
// ---------------------------------------------------------------------------

class OAuthJwtMintTest : public OAuthConfigTest {};

TEST_F(OAuthJwtMintTest, MintTokenForOAuthUser) {
    if (private_key_.empty()) GTEST_SKIP() << "OpenSSL not available";

    auto j = base_config();
    j["providers"] = {
        {"github", {{"enabled", true}, {"client_id", "test"}}}
    };
    write_config(j);

    auto config = AuthConfig::from_file(config_path_.string());
    JwtAuth auth(&config);

    // Simulate: provider returned user_id=12345, email=test@gh.com
    std::string user_id = "github:12345";
    auto token = auth.mint_token(user_id, "test@gh.com");
    ASSERT_FALSE(token.empty());

    auto claims = auth.validate_token(token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ(claims->sub, "github:12345");
    EXPECT_EQ(claims->email, "test@gh.com");
    // User github:12345 is mapped to admin in base_config
    EXPECT_EQ(claims->role, "admin");
}

TEST_F(OAuthJwtMintTest, MintTokenForUnknownOAuthUser) {
    if (private_key_.empty()) GTEST_SKIP() << "OpenSSL not available";

    auto j = base_config();
    write_config(j);

    auto config = AuthConfig::from_file(config_path_.string());
    JwtAuth auth(&config);

    // New OAuth user not in user_roles
    std::string user_id = "google:999999";
    auto token = auth.mint_token(user_id, "new@google.com");
    ASSERT_FALSE(token.empty());

    auto claims = auth.validate_token(token);
    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ(claims->sub, "google:999999");
    // Falls back to default_role
    EXPECT_EQ(claims->role, "beta-tester");
}

} // namespace etil::mcp

#endif // ETIL_JWT_ENABLED
