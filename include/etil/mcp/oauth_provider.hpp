#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#ifdef ETIL_JWT_ENABLED

#include <optional>
#include <string>

namespace etil::mcp {

/// Response from initiating an OAuth device flow.
struct DeviceCodeResponse {
    std::string device_code;
    std::string user_code;
    std::string verification_uri;
    int expires_in = 0;
    int interval = 5;
};

/// Result of polling a device code.
struct PollResult {
    enum class Status {
        Pending,
        Granted,
        SlowDown,
        ExpiredToken,
        AccessDenied,
        Error
    };
    Status status = Status::Error;
    std::string access_token;       // valid only when Granted
    std::string error_description;
    int interval = 0;               // updated interval (for SlowDown)
};

/// User info retrieved from an OAuth provider.
struct ProviderUserInfo {
    std::string provider_id;   // "12345" (GitHub int) or "110248..." (Google sub)
    std::string email;
};

/// Abstract interface for OAuth device flow providers.
///
/// Each provider (GitHub, Google) implements this interface to handle
/// the provider-specific HTTP calls for the device authorization grant.
class OAuthProvider {
public:
    virtual ~OAuthProvider() = default;

    /// Provider name (e.g., "github", "google").
    virtual const char* name() const = 0;

    /// Initiate the device authorization flow.
    /// Returns nullopt if the provider call fails.
    virtual std::optional<DeviceCodeResponse> request_device_code() = 0;

    /// Poll the provider for a device code grant.
    /// Returns nullopt if the provider call fails entirely (network error).
    virtual std::optional<PollResult> poll_device_code(
        const std::string& device_code) = 0;

    /// Exchange an access token for user info.
    /// Returns nullopt if the token is invalid or the call fails.
    virtual std::optional<ProviderUserInfo> get_user_info(
        const std::string& access_token) = 0;
};

} // namespace etil::mcp

#endif // ETIL_JWT_ENABLED
