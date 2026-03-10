#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#ifdef ETIL_JWT_ENABLED

#include <optional>
#include <string>

namespace etil::mcp {

/// Claims extracted from a validated ETIL JWT.
struct JwtClaims {
    std::string sub;    // "github:12345"
    std::string email;
    std::string role;   // "researcher"
};

/// JWT minting and validation using jwt-cpp with RS256.
///
/// Owns copies of the PEM key material so it is independent of
/// AuthConfig lifetime (admin config swaps must not invalidate JWT auth).
/// Tokens carry the standard claims (iss, sub, email, role, iat, exp).
class JwtAuth {
public:
    /// Construct with PEM key material and token TTL.
    JwtAuth(std::string private_key_pem, std::string public_key_pem,
            int ttl_seconds);

    /// Mint a new ETIL JWT for the given user.
    /// Returns the signed JWT string.
    std::string mint_token(const std::string& user_id,
                           const std::string& email,
                           const std::string& role) const;

    /// Validate a JWT string and extract claims.
    ///
    /// Returns nullopt if the token is expired, has an invalid
    /// signature, wrong issuer, or is otherwise malformed.
    std::optional<JwtClaims> validate_token(const std::string& token) const;

    /// The issuer string used in all ETIL JWTs.
    static constexpr const char* ISSUER = "etil-mcp";

private:
    std::string private_key_;
    std::string public_key_;
    int ttl_seconds_;
};

} // namespace etil::mcp

#endif // ETIL_JWT_ENABLED
