#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#ifdef ETIL_MONGODB_ENABLED

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace etil::db { class MongoClient; }

namespace etil::aaa {

/// User record from the MongoDB `users` collection.
struct UserRecord {
    std::string email;
    std::vector<std::string> provider_ids;
    std::string role;
    std::string display_name;
    int64_t login_count = 0;
};

/// User management backed by MongoDB.
///
/// Depends on `etil::db::MongoClient` for storage.  All methods are
/// no-ops when the client is not connected (null or disconnected).
class UserStore {
public:
    explicit UserStore(etil::db::MongoClient& client);

    /// Returns true if the underlying client is connected.
    bool available() const;

    /// Find a user by email.
    std::optional<UserRecord> find_by_email(const std::string& email);

    /// Create a new user.  Returns the created record, or nullopt on error.
    std::optional<UserRecord> create(const std::string& email,
                                      const std::string& provider_id,
                                      const std::string& role,
                                      const std::string& display_name);

    /// Update login timestamp and count, add provider_id if new.
    bool record_login(const std::string& email,
                      const std::string& provider_id);

    /// Get the role for a user by email.  Returns empty string if not found.
    std::string get_role(const std::string& email);

    /// Create unique index on users.email.
    void ensure_indexes();

private:
    etil::db::MongoClient& client_;
};

class AuditLog;  // forward

/// Handle a successful OAuth login: find-or-create user + audit.
///
/// Eliminates the duplicated blocks in http_transport.cpp.
void on_login_success(UserStore* users, AuditLog* audit,
                      const std::string& email, const std::string& user_id,
                      const std::string& default_role,
                      const std::string& provider, const std::string& method);

} // namespace etil::aaa

#endif // ETIL_MONGODB_ENABLED
