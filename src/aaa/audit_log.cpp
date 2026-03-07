// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef ETIL_MONGODB_ENABLED

#include "etil/aaa/audit_log.hpp"
#include "etil/db/mongo_client.hpp"

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>
#include <mongocxx/exception/exception.hpp>
#include <mongocxx/options/index.hpp>

#include <chrono>
#include <cstdio>

namespace etil::aaa {

AuditLog::AuditLog(etil::db::MongoClient& client) : client_(client) {}

bool AuditLog::available() const {
    return client_.connected();
}

void AuditLog::log_event(const std::string& event,
                          const std::string& email,
                          const std::string& details_json) {
    if (!client_.connected()) return;
    try {
        using namespace bsoncxx::builder::basic;
        auto now = bsoncxx::types::b_date{
            std::chrono::system_clock::now()};

        auto builder = bsoncxx::builder::basic::document{};
        builder.append(kvp("timestamp", now));
        builder.append(kvp("event", event));
        builder.append(kvp("email", email));

        if (!details_json.empty()) {
            try {
                auto details_bson = bsoncxx::from_json(details_json);
                builder.append(kvp("details", details_bson.view()));
            } catch (...) {
                builder.append(kvp("details", details_json));
            }
        }

        auto doc_json = bsoncxx::to_json(builder.view());
        client_.insert("audit_log", doc_json);
    } catch (const std::exception& e) {
        fprintf(stderr, "AuditLog error: %s\n", e.what());
    }
}

void AuditLog::log_permission_denied(const std::string& email,
                                      const std::string& tool,
                                      const std::string& role) {
    log_event("permission_denied", email,
              R"({"tool":")" + tool + R"(","role":")" + role + "\"}");
}

void AuditLog::log_session_create(const std::string& email,
                                   const std::string& session_id,
                                   const std::string& role) {
    log_event("session_create", email,
              R"({"session_id":")" + session_id +
              R"(","role":")" + role + "\"}");
}

void AuditLog::log_session_destroy(const std::string& email,
                                    const std::string& session_id) {
    log_event("session_destroy", email,
              R"({"session_id":")" + session_id + "\"}");
}

void AuditLog::log_login(const std::string& email,
                          const std::string& provider,
                          const std::string& method) {
    log_event("login", email,
              R"({"provider":")" + provider +
              R"(","method":")" + method + "\"}");
}

void AuditLog::log_user_created(const std::string& email,
                                 const std::string& provider) {
    log_event("user_created", email,
              R"({"provider":")" + provider + "\"}");
}

void AuditLog::ensure_indexes() {
    if (!client_.connected()) return;
    client_.ensure_ttl_index("audit_log", "timestamp",
                              30 * 24 * 3600);  // 30 days
}

} // namespace etil::aaa

#endif // ETIL_MONGODB_ENABLED
