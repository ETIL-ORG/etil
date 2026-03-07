// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef ETIL_JWT_ENABLED

#include "etil/mcp/oauth_provider.hpp"
#include "etil/mcp/oauth_github.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdio>

namespace etil::mcp {

GitHubProvider::GitHubProvider(std::string client_id)
    : client_id_(std::move(client_id)) {}

const char* GitHubProvider::name() const { return "github"; }

std::optional<DeviceCodeResponse> GitHubProvider::request_device_code() {
    httplib::SSLClient cli("github.com");
    cli.set_connection_timeout(10);
    cli.set_read_timeout(10);

    std::string body = "client_id=" + client_id_ +
                       "&scope=read:user%20user:email";

    httplib::Headers headers = {
        {"Accept", "application/json"}
    };

    auto res = cli.Post("/login/device/code", headers, body,
                        "application/x-www-form-urlencoded");
    if (!res || res->status != 200) {
        fprintf(stderr, "GitHub device code request failed: %s\n",
                res ? std::to_string(res->status).c_str() : "network error");
        return std::nullopt;
    }

    nlohmann::json j;
    try {
        // GitHub returns JSON when we send Accept header, but the default
        // response for this endpoint is application/x-www-form-urlencoded.
        // Try JSON first, fall back to form-urlencoded parsing.
        j = nlohmann::json::parse(res->body);
    } catch (...) {
        // Parse URL-encoded response: key=value&key=value
        j = parse_form_urlencoded(res->body);
    }

    if (j.contains("error")) {
        fprintf(stderr, "GitHub device code error: %s\n",
                j.value("error_description", j.value("error", "unknown")).c_str());
        return std::nullopt;
    }

    DeviceCodeResponse resp;
    resp.device_code = j.value("device_code", "");
    resp.user_code = j.value("user_code", "");
    resp.verification_uri = j.value("verification_uri", "");
    resp.expires_in = j.value("expires_in", 0);
    resp.interval = j.value("interval", 5);

    if (resp.device_code.empty() || resp.user_code.empty()) {
        return std::nullopt;
    }

    return resp;
}

std::optional<PollResult> GitHubProvider::poll_device_code(
    const std::string& device_code) {
    httplib::SSLClient cli("github.com");
    cli.set_connection_timeout(10);
    cli.set_read_timeout(10);

    std::string body =
        "client_id=" + client_id_ +
        "&device_code=" + device_code +
        "&grant_type=urn:ietf:params:oauth:grant-type:device_code";

    httplib::Headers headers = {
        {"Accept", "application/json"}
    };

    auto res = cli.Post("/login/oauth/access_token", headers, body,
                        "application/x-www-form-urlencoded");
    if (!res) {
        return std::nullopt;  // network error
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(res->body);
    } catch (...) {
        j = parse_form_urlencoded(res->body);
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

std::optional<ProviderUserInfo> GitHubProvider::get_user_info(
    const std::string& access_token) {
    httplib::SSLClient cli("api.github.com");
    cli.set_connection_timeout(10);
    cli.set_read_timeout(10);

    httplib::Headers headers = {
        {"Authorization", "Bearer " + access_token},
        {"Accept", "application/vnd.github+json"},
        {"X-GitHub-Api-Version", "2022-11-28"}
    };

    auto res = cli.Get("/user", headers);
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

    // GitHub user ID is an integer
    if (j.contains("id") && j["id"].is_number_integer()) {
        info.provider_id = std::to_string(j["id"].get<int64_t>());
    } else {
        return std::nullopt;
    }

    // Email may be null in /user response
    if (j.contains("email") && j["email"].is_string()) {
        info.email = j["email"].get<std::string>();
    }

    // If email is empty, try /user/emails endpoint
    if (info.email.empty()) {
        auto email_res = cli.Get("/user/emails", headers);
        if (email_res && email_res->status == 200) {
            try {
                auto emails = nlohmann::json::parse(email_res->body);
                if (emails.is_array()) {
                    for (const auto& e : emails) {
                        if (e.value("primary", false) &&
                            e.value("verified", false) &&
                            e.contains("email") && e["email"].is_string()) {
                            info.email = e["email"].get<std::string>();
                            break;
                        }
                    }
                }
            } catch (...) {
                // Non-fatal: proceed without email
            }
        }
    }

    return info;
}

nlohmann::json GitHubProvider::parse_form_urlencoded(const std::string& body) {
    nlohmann::json result = nlohmann::json::object();
    std::string key, value;
    bool in_value = false;

    for (size_t i = 0; i <= body.size(); ++i) {
        char c = (i < body.size()) ? body[i] : '&';
        if (c == '=') {
            in_value = true;
        } else if (c == '&') {
            if (!key.empty()) {
                // Try to parse as integer for numeric fields
                try {
                    size_t pos;
                    int num = std::stoi(value, &pos);
                    if (pos == value.size()) {
                        result[key] = num;
                    } else {
                        result[key] = value;
                    }
                } catch (...) {
                    result[key] = value;
                }
            }
            key.clear();
            value.clear();
            in_value = false;
        } else if (in_value) {
            value += c;
        } else {
            key += c;
        }
    }
    return result;
}

} // namespace etil::mcp

#endif // ETIL_JWT_ENABLED
