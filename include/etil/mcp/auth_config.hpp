#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#ifdef ETIL_JWT_ENABLED

#include "etil/mcp/role_permissions.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace etil::mcp {

/// OAuth provider configuration (loaded from auth.json "providers" section).
struct OAuthProviderConfig {
    bool enabled = false;
    std::string client_id;
    std::string client_secret;  // required for Google, empty for GitHub
};

/// Authentication/authorization configuration.
///
/// Maps users to roles and roles to permissions.  JWT keys are loaded from
/// PEM-encoded files referenced by path in the config.
///
/// Production layout (3-file directory):
///   roles.json — roles + default_role
///   keys.json  — jwt_private_key_file, jwt_public_key_file, jwt_ttl_seconds, providers
///   users.json — user_roles
///
/// Legacy layout (single file) is still supported via from_file().
struct AuthConfig {
    std::unordered_map<std::string, RolePermissions> roles;
    std::unordered_map<std::string, std::string> user_roles;  // "github:12345" -> "admin"
    std::string default_role;

    std::string jwt_private_key;   // RS256 PEM content (loaded from file)
    std::string jwt_public_key;    // RS256 PEM content (loaded from file)
    int jwt_ttl_seconds = 86400;   // 24h

    /// OAuth provider configurations (keyed by provider name).
    std::unordered_map<std::string, OAuthProviderConfig> providers;

    /// Load configuration from a directory containing roles.json, keys.json,
    /// and users.json.  Throws std::runtime_error on I/O or parse errors.
    /// Missing files are silently skipped (partial config is valid).
    static AuthConfig from_directory(const std::string& dir_path);

    /// Load configuration from a single JSON file (legacy / test use).
    /// Throws std::runtime_error on I/O or parse errors.
    static AuthConfig from_file(const std::string& path);

    /// Resolve the role for a user ID (e.g., "github:12345").
    /// Returns the user's mapped role, or default_role if not mapped.
    std::string role_for(const std::string& user_id) const;

    /// Get the permissions for a user ID.
    /// Returns nullptr if the resolved role has no permissions entry.
    const RolePermissions* permissions_for(const std::string& user_id) const;
};

} // namespace etil::mcp

#endif // ETIL_JWT_ENABLED
