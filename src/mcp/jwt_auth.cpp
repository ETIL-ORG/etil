// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef ETIL_JWT_ENABLED

#include "etil/mcp/jwt_auth.hpp"

#include <jwt-cpp/traits/nlohmann-json/defaults.h>

#include <chrono>

namespace etil::mcp {

JwtAuth::JwtAuth(std::string private_key_pem, std::string public_key_pem,
                 int ttl_seconds)
    : private_key_(std::move(private_key_pem)),
      public_key_(std::move(public_key_pem)),
      ttl_seconds_(ttl_seconds) {}

std::string JwtAuth::mint_token(const std::string& user_id,
                                const std::string& email,
                                const std::string& role) const {
    auto now = std::chrono::system_clock::now();
    auto exp = now + std::chrono::seconds(ttl_seconds_);

    auto token = jwt::create()
        .set_issuer(ISSUER)
        .set_subject(user_id)
        .set_issued_at(now)
        .set_expires_at(exp)
        .set_payload_claim("email", jwt::claim(email))
        .set_payload_claim("role", jwt::claim(role))
        .sign(jwt::algorithm::rs256("", private_key_, "", ""));

    return token;
}

std::optional<JwtClaims>
JwtAuth::validate_token(const std::string& token) const {
    try {
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::rs256(public_key_,
                                                   "", "", ""))
            .with_issuer(ISSUER)
            .leeway(60);  // 60s clock skew tolerance

        auto decoded = jwt::decode(token);
        verifier.verify(decoded);

        JwtClaims claims;
        claims.sub = decoded.get_subject();

        // Reject empty subject — user identity is required
        if (claims.sub.empty()) return std::nullopt;

        // Validate iat: reject tokens issued in the future (beyond leeway)
        if (decoded.has_payload_claim("iat")) {
            auto iat = decoded.get_issued_at();
            auto now = std::chrono::system_clock::now();
            if (iat > now + std::chrono::seconds(60)) {
                return std::nullopt;  // token from the future
            }
        }

        // Type-safe claim extraction — reject non-string claims
        if (decoded.has_payload_claim("email")) {
            auto claim = decoded.get_payload_claim("email");
            if (claim.get_type() != jwt::json::type::string)
                return std::nullopt;
            claims.email = claim.as_string();
        }
        if (decoded.has_payload_claim("role")) {
            auto claim = decoded.get_payload_claim("role");
            if (claim.get_type() != jwt::json::type::string)
                return std::nullopt;
            claims.role = claim.as_string();
        }

        return claims;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace etil::mcp

#endif // ETIL_JWT_ENABLED
