// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef ETIL_JWT_ENABLED

#include "etil/mcp/jwt_auth.hpp"
#include "etil/mcp/auth_config.hpp"

#include <jwt-cpp/traits/nlohmann-json/defaults.h>

#include <chrono>

namespace etil::mcp {

JwtAuth::JwtAuth(const AuthConfig* config) : config_(config) {}

std::string JwtAuth::mint_token(const std::string& user_id,
                                const std::string& email) const {
    auto role = config_->role_for(user_id);
    auto now = std::chrono::system_clock::now();
    auto exp = now + std::chrono::seconds(config_->jwt_ttl_seconds);

    auto token = jwt::create()
        .set_issuer(ISSUER)
        .set_subject(user_id)
        .set_issued_at(now)
        .set_expires_at(exp)
        .set_payload_claim("email", jwt::claim(email))
        .set_payload_claim("role", jwt::claim(role))
        .sign(jwt::algorithm::rs256("", config_->jwt_private_key, "", ""));

    return token;
}

std::optional<JwtClaims>
JwtAuth::validate_token(const std::string& token) const {
    try {
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::rs256(config_->jwt_public_key,
                                                   "", "", ""))
            .with_issuer(ISSUER)
            .leeway(0);

        auto decoded = jwt::decode(token);
        verifier.verify(decoded);

        JwtClaims claims;
        claims.sub = decoded.get_subject();

        if (decoded.has_payload_claim("email")) {
            claims.email = decoded.get_payload_claim("email").as_string();
        }
        if (decoded.has_payload_claim("role")) {
            claims.role = decoded.get_payload_claim("role").as_string();
        }

        return claims;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace etil::mcp

#endif // ETIL_JWT_ENABLED
