#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#ifdef ETIL_JWT_ENABLED

#include <optional>
#include <string>

namespace etil::mcp {

struct AuthConfig;

/// Claims extracted from a validated ETIL JWT.
struct JwtClaims {
    std::string sub;    // "github:12345"
    std::string email;
    std::string role;   // "researcher"
};

/// JWT minting and validation using jwt-cpp with RS256.
///
/// All tokens are signed with the RS256 private key from AuthConfig
/// and validated against the RS256 public key.  Tokens carry the
/// standard claims (iss, sub, email, role, iat, exp).
class JwtAuth {
public:
    /// Construct from an AuthConfig.  The config must outlive JwtAuth.
    /// config->jwt_private_key and jwt_public_key must be valid PEM.
    explicit JwtAuth(const AuthConfig* config);

    /// Mint a new ETIL JWT for the given user.
    ///
    /// The role is resolved via config->role_for(user_id).
    /// Returns the signed JWT string.
    std::string mint_token(const std::string& user_id,
                           const std::string& email) const;

    /// Validate a JWT string and extract claims.
    ///
    /// Returns nullopt if the token is expired, has an invalid
    /// signature, wrong issuer, or is otherwise malformed.
    std::optional<JwtClaims> validate_token(const std::string& token) const;

    /// The issuer string used in all ETIL JWTs.
    static constexpr const char* ISSUER = "etil-mcp";

private:
    const AuthConfig* config_;
};

} // namespace etil::mcp

#endif // ETIL_JWT_ENABLED
