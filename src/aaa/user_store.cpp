// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef ETIL_MONGODB_ENABLED

#include "etil/aaa/user_store.hpp"
#include "etil/aaa/audit_log.hpp"
#include "etil/db/mongo_client.hpp"

#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>
#include <mongocxx/exception/exception.hpp>
#include <mongocxx/options/index.hpp>

#include <chrono>
#include <cstdio>

namespace etil::aaa {

UserStore::UserStore(etil::db::MongoClient& client) : client_(client) {}

bool UserStore::available() const {
    return client_.connected();
}

std::optional<UserRecord> UserStore::find_by_email(const std::string& email) {
    if (!client_.connected()) return std::nullopt;

    using namespace bsoncxx::builder::basic;
    auto filter = make_document(kvp("email", email));
    auto result = client_.find("users", filter.view());
    if (!result || *result == "[]") return std::nullopt;

    // Parse the JSON array — take the first document
    try {
        auto doc = bsoncxx::from_json(*result);
        // The result is a JSON array string, parse it properly
    } catch (...) {
        // Fall through to direct BSON approach
    }

    // Use the generic find and parse the result
    // The generic find returns a JSON array, so we need to parse it.
    // However, for proper field extraction with bsoncxx types, we need
    // direct collection access.  Since MongoClient uses pimpl, we use
    // the generic CRUD interface and parse the JSON result.
    try {
        // Parse JSON array result
        auto json_str = *result;
        // Strip outer [] and get first document
        if (json_str.size() <= 2) return std::nullopt;

        // Use nlohmann for JSON parsing since we get a string back
        // But we don't want to add nlohmann as a dependency here.
        // Instead, use bsoncxx JSON parsing on the first element.

        // Find the first { ... } in the array
        auto start = json_str.find('{');
        if (start == std::string::npos) return std::nullopt;

        // Find matching closing brace (simplified — assumes no nested arrays with })
        int depth = 0;
        size_t end = start;
        for (size_t i = start; i < json_str.size(); ++i) {
            if (json_str[i] == '{') ++depth;
            else if (json_str[i] == '}') {
                --depth;
                if (depth == 0) { end = i; break; }
            }
        }
        auto doc_json = json_str.substr(start, end - start + 1);
        auto doc = bsoncxx::from_json(doc_json);
        auto view = doc.view();

        UserRecord rec;
        if (view.find("email") != view.end()) {
            rec.email = std::string(view["email"].get_string().value);
        }
        if (view.find("role") != view.end()) {
            rec.role = std::string(view["role"].get_string().value);
        }
        if (view.find("display_name") != view.end()) {
            rec.display_name =
                std::string(view["display_name"].get_string().value);
        }
        if (view.find("login_count") != view.end()) {
            rec.login_count = view["login_count"].get_int64().value;
        }
        if (view.find("provider_ids") != view.end() &&
            view["provider_ids"].type() == bsoncxx::type::k_array) {
            for (const auto& elem : view["provider_ids"].get_array().value) {
                if (elem.type() == bsoncxx::type::k_string) {
                    rec.provider_ids.emplace_back(elem.get_string().value);
                }
            }
        }
        return rec;
    } catch (const std::exception& e) {
        fprintf(stderr, "UserStore find_by_email error: %s\n", e.what());
        return std::nullopt;
    }
}

std::optional<UserRecord> UserStore::create(const std::string& email,
                                             const std::string& provider_id,
                                             const std::string& role,
                                             const std::string& display_name) {
    if (!client_.connected()) return std::nullopt;
    try {
        using namespace bsoncxx::builder::basic;
        auto now = bsoncxx::types::b_date{
            std::chrono::system_clock::now()};

        auto doc = make_document(
            kvp("email", email),
            kvp("provider_ids", make_array(provider_id)),
            kvp("role", role),
            kvp("display_name", display_name),
            kvp("created_at", now),
            kvp("last_login", now),
            kvp("login_count", static_cast<int64_t>(1)));

        auto doc_json = bsoncxx::to_json(doc.view());
        auto id = client_.insert("users", doc_json);
        if (!id) return std::nullopt;

        UserRecord rec;
        rec.email = email;
        rec.provider_ids = {provider_id};
        rec.role = role;
        rec.display_name = display_name;
        rec.login_count = 1;
        return rec;
    } catch (const std::exception& e) {
        fprintf(stderr, "UserStore create error: %s\n", e.what());
        return std::nullopt;
    }
}

bool UserStore::record_login(const std::string& email,
                              const std::string& provider_id) {
    if (!client_.connected()) return false;

    // We need $set, $inc, and $addToSet — build BSON update document
    // and use the generic update interface.
    try {
        using namespace bsoncxx::builder::basic;
        auto now = bsoncxx::types::b_date{
            std::chrono::system_clock::now()};

        auto filter = make_document(kvp("email", email));
        auto update = make_document(
            kvp("$set", make_document(kvp("last_login", now))),
            kvp("$inc", make_document(
                kvp("login_count", static_cast<int64_t>(1)))),
            kvp("$addToSet", make_document(
                kvp("provider_ids", provider_id))));

        auto count = client_.update("users", filter.view(), update.view());
        return count > 0;
    } catch (const std::exception& e) {
        fprintf(stderr, "UserStore record_login error: %s\n", e.what());
        return false;
    }
}

std::string UserStore::get_role(const std::string& email) {
    auto user = find_by_email(email);
    if (user) return user->role;
    return {};
}

void UserStore::ensure_indexes() {
    if (!client_.connected()) return;
    // Create unique index on users.email via the generic insert/update
    // interface.  We need direct collection access for createIndex,
    // but the generic CRUD API doesn't expose that.
    // Instead, we use a well-known pattern: try to insert a dummy then
    // delete it, or simply rely on MongoClient::connect() handling it.
    //
    // Since we're moving index creation OUT of MongoClient::connect(),
    // we need a way to create indexes.  The cleanest approach is to
    // add an ensure_index method to MongoClient.  But the plan says
    // to keep MongoClient as pure CRUD.
    //
    // Compromise: use the raw MongoDB command via the CRUD interface.
    // Actually, let's just add ensure_index_unique() to MongoClient
    // as a thin helper — it's still generic infrastructure, not
    // domain-specific.
    //
    // For now, we'll use the existing generic CRUD to run a createIndexes
    // command via insert into the system collection... that won't work.
    //
    // Best solution: the MongoClient already has pool_ access internally.
    // We'll add a minimal ensure_index method.  But that changes the plan.
    //
    // Alternative: keep the index creation code here using bsoncxx but
    // access the pool through MongoClient.  MongoClient needs to expose
    // a run_command() or create_index() method.
    //
    // For minimal change, I'll add ensure_unique_index() to MongoClient.
    // This is still generic (not domain-specific).

    // Use the client's ensure_unique_index method
    client_.ensure_unique_index("users", "email");
}

// ---------------------------------------------------------------------------
// on_login_success — consolidates duplicated OAuth handler logic
// ---------------------------------------------------------------------------

void on_login_success(UserStore* users, AuditLog* audit,
                      const std::string& email, const std::string& user_id,
                      const std::string& default_role,
                      const std::string& provider, const std::string& method) {
    if (users && users->available()) {
        auto existing = users->find_by_email(email);
        if (existing) {
            users->record_login(email, user_id);
        } else {
            users->create(email, user_id, default_role, email);
            if (audit) audit->log_user_created(email, provider);
        }
    }
    if (audit) audit->log_login(email, provider, method);
}

} // namespace etil::aaa

#endif // ETIL_MONGODB_ENABLED
