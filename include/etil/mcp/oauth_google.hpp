#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#ifdef ETIL_JWT_ENABLED

#include "etil/mcp/oauth_provider.hpp"

#include <string>

namespace etil::mcp {

/// Google OAuth device flow provider.
///
/// Uses Google's device authorization grant (requires client_secret).
/// Endpoints:
/// - Device code: POST https://oauth2.googleapis.com/device/code
/// - Token poll:  POST https://oauth2.googleapis.com/token
/// - User info:   GET  https://www.googleapis.com/oauth2/v3/userinfo
class GoogleProvider : public OAuthProvider {
public:
    GoogleProvider(std::string client_id, std::string client_secret);

    const char* name() const override;
    std::optional<DeviceCodeResponse> request_device_code() override;
    std::optional<PollResult> poll_device_code(
        const std::string& device_code) override;
    std::optional<ProviderUserInfo> get_user_info(
        const std::string& access_token) override;

private:
    std::string client_id_;
    std::string client_secret_;
};

} // namespace etil::mcp

#endif // ETIL_JWT_ENABLED
