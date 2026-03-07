#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#ifdef ETIL_MONGODB_ENABLED

#include <string>

namespace etil::db { class MongoClient; }

namespace etil::aaa {

/// Typed audit logging backed by MongoDB.
///
/// All methods are fire-and-forget with internal null/connection guards.
/// Errors are silently ignored (non-critical path).
class AuditLog {
public:
    explicit AuditLog(etil::db::MongoClient& client);

    /// Returns true if the underlying client is connected.
    bool available() const;

    /// Log a permission-denied event.
    void log_permission_denied(const std::string& email,
                               const std::string& tool,
                               const std::string& role);

    /// Log a session creation event.
    void log_session_create(const std::string& email,
                            const std::string& session_id,
                            const std::string& role);

    /// Log a session destruction event.
    void log_session_destroy(const std::string& email,
                             const std::string& session_id);

    /// Log a login event.
    void log_login(const std::string& email,
                   const std::string& provider,
                   const std::string& method);

    /// Log a user creation event.
    void log_user_created(const std::string& email,
                          const std::string& provider);

    /// Create TTL index on audit_log.timestamp (30-day expiry).
    void ensure_indexes();

private:
    etil::db::MongoClient& client_;

    /// Internal helper — delegates to MongoClient::insert.
    void log_event(const std::string& event,
                   const std::string& email,
                   const std::string& details_json);
};

} // namespace etil::aaa

#endif // ETIL_MONGODB_ENABLED
