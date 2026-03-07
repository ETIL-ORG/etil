#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#ifdef ETIL_JWT_ENABLED

#include "etil/mcp/oauth_provider.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace etil::mcp {

/// GitHub OAuth device flow provider.
///
/// Uses GitHub's device authorization grant (no client_secret needed for
/// public clients).  Endpoints:
/// - Device code: POST https://github.com/login/device/code
/// - Token poll:  POST https://github.com/login/oauth/access_token
/// - User info:   GET  https://api.github.com/user
class GitHubProvider : public OAuthProvider {
public:
    explicit GitHubProvider(std::string client_id);

    const char* name() const override;
    std::optional<DeviceCodeResponse> request_device_code() override;
    std::optional<PollResult> poll_device_code(
        const std::string& device_code) override;
    std::optional<ProviderUserInfo> get_user_info(
        const std::string& access_token) override;

private:
    std::string client_id_;

    /// Parse URL-encoded response body into JSON object.
    static nlohmann::json parse_form_urlencoded(const std::string& body);
};

} // namespace etil::mcp

#endif // ETIL_JWT_ENABLED
