// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef ETIL_JWT_ENABLED

#include "etil/mcp/oauth_provider.hpp"
#include "etil/mcp/oauth_google.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdio>

namespace etil::mcp {

GoogleProvider::GoogleProvider(std::string client_id,
                               std::string client_secret)
    : client_id_(std::move(client_id))
    , client_secret_(std::move(client_secret)) {}

const char* GoogleProvider::name() const { return "google"; }

std::optional<DeviceCodeResponse> GoogleProvider::request_device_code() {
    httplib::SSLClient cli("oauth2.googleapis.com");
    cli.set_connection_timeout(10);
    cli.set_read_timeout(10);

    std::string body = "client_id=" + client_id_ +
                       "&scope=openid%20email";

    auto res = cli.Post("/device/code", body,
                        "application/x-www-form-urlencoded");
    if (!res || res->status != 200) {
        fprintf(stderr, "Google device code request failed: %s\n",
                res ? std::to_string(res->status).c_str() : "network error");
        return std::nullopt;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(res->body);
    } catch (...) {
        fprintf(stderr, "Google device code: invalid JSON response\n");
        return std::nullopt;
    }

    if (j.contains("error")) {
        fprintf(stderr, "Google device code error: %s\n",
                j.value("error_description",
                        j.value("error", "unknown")).c_str());
        return std::nullopt;
    }

    DeviceCodeResponse resp;
    resp.device_code = j.value("device_code", "");
    resp.user_code = j.value("user_code", "");
    // Google uses "verification_url" (not "verification_uri")
    resp.verification_uri = j.value("verification_url",
                                    j.value("verification_uri", ""));
    resp.expires_in = j.value("expires_in", 0);
    resp.interval = j.value("interval", 5);

    if (resp.device_code.empty() || resp.user_code.empty()) {
        return std::nullopt;
    }

    return resp;
}

std::optional<PollResult> GoogleProvider::poll_device_code(
    const std::string& device_code) {
    httplib::SSLClient cli("oauth2.googleapis.com");
    cli.set_connection_timeout(10);
    cli.set_read_timeout(10);

    std::string body =
        "client_id=" + client_id_ +
        "&client_secret=" + client_secret_ +
        "&device_code=" + device_code +
        "&grant_type=urn:ietf:params:oauth:grant-type:device_code";

    auto res = cli.Post("/token", body,
                        "application/x-www-form-urlencoded");
    if (!res) {
        return std::nullopt;  // network error
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(res->body);
    } catch (...) {
        return std::nullopt;
    }

    // Check for access_token (success)
    if (j.contains("access_token") && j["access_token"].is_string()) {
        PollResult result;
        result.status = PollResult::Status::Granted;
        result.access_token = j["access_token"].get<std::string>();
        return result;
    }

    // Check for error states
    std::string error = j.value("error", "");
    PollResult result;

    if (error == "authorization_pending") {
        result.status = PollResult::Status::Pending;
    } else if (error == "slow_down") {
        result.status = PollResult::Status::SlowDown;
        result.interval = j.value("interval", 0);
    } else if (error == "expired_token") {
        result.status = PollResult::Status::ExpiredToken;
        result.error_description = j.value("error_description",
                                           "Device code expired");
    } else if (error == "access_denied") {
        result.status = PollResult::Status::AccessDenied;
        result.error_description = j.value("error_description",
                                           "User denied access");
    } else {
        result.status = PollResult::Status::Error;
        result.error_description = j.value("error_description", error);
    }

    return result;
}

std::optional<ProviderUserInfo> GoogleProvider::get_user_info(
    const std::string& access_token) {
    httplib::SSLClient cli("www.googleapis.com");
    cli.set_connection_timeout(10);
    cli.set_read_timeout(10);

    httplib::Headers headers = {
        {"Authorization", "Bearer " + access_token}
    };

    auto res = cli.Get("/oauth2/v3/userinfo", headers);
    if (!res || res->status != 200) {
        return std::nullopt;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(res->body);
    } catch (...) {
        return std::nullopt;
    }

    ProviderUserInfo info;

    // Google user ID is the "sub" field (string)
    if (j.contains("sub") && j["sub"].is_string()) {
        info.provider_id = j["sub"].get<std::string>();
    } else {
        return std::nullopt;
    }

    if (j.contains("email") && j["email"].is_string()) {
        info.email = j["email"].get<std::string>();
    }

    return info;
}

} // namespace etil::mcp

#endif // ETIL_JWT_ENABLED
