#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#ifdef ETIL_MONGODB_ENABLED

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <bsoncxx/document/value.hpp>
#include <bsoncxx/document/view.hpp>

namespace etil::db {

/// Unified query options for all MongoDB operations.
///
/// All document fields are owned `document::value` (not views),
/// eliminating the dangling-view class of bugs entirely.
struct MongoQueryOptions {
    std::optional<int64_t> skip;
    std::optional<int64_t> limit;
    std::optional<bsoncxx::document::value> sort_doc;
    std::optional<bsoncxx::document::value> projection_doc;
    std::optional<bsoncxx::document::value> hint_doc;
    std::optional<bsoncxx::document::value> collation_doc;
    std::optional<int64_t> max_time_ms;
    std::optional<int32_t> batch_size;
    std::optional<bool> upsert;
};

/// Resolved MongoDB connection configuration (URI + database name).
///
/// Immutable after construction.  Shared across all sessions.
struct MongoClientConfig {
    std::string uri;        // e.g. "mongodb+srv://..."
    std::string database;   // default: "etil"

    /// Returns true if a URI is configured.
    bool enabled() const { return !uri.empty(); }
};

/// Multi-connection configuration loaded from a JSON file.
///
/// File format (e.g. /opt/etil/mongodb/mongodb-connections.json):
/// ```json
/// {
///   "default": "atlas-prod",
///   "connections": {
///     "atlas-prod": { "uri": "mongodb+srv://...", "database": "etil" },
///     "local-dev":  { "uri": "mongodb://localhost:27017", "database": "etil_dev" }
///   }
/// }
/// ```
///
/// Resolution order:
///   1. ETIL_MONGODB_CONFIG env var → load file, select "default" connection
///   2. Environment overrides: ETIL_MONGODB_URI / ETIL_MONGODB_DATABASE
///      override the resolved URI/database (or provide standalone config
///      when no file is present)
struct MongoConnectionsConfig {
    /// Load from a JSON file.  Returns the "default" connection's config.
    /// Throws std::runtime_error on I/O or parse errors.
    static MongoClientConfig from_file(const std::string& path);

    /// Load a named connection from a JSON file.
    /// Throws std::runtime_error on I/O, parse, or missing-connection errors.
    static MongoClientConfig from_file(const std::string& path,
                                        const std::string& connection_name);

    /// Resolve MongoDB configuration.
    ///
    /// 1. If ETIL_MONGODB_CONFIG is set, load from that file.
    /// 2. Apply ETIL_MONGODB_URI / ETIL_MONGODB_DATABASE overrides.
    /// 3. If no file and no env vars, returns an empty (disabled) config.
    static MongoClientConfig resolve();
};

/// Per-session mutable MongoDB client state.
///
/// Tracks query counts for budget enforcement.  Owned by Session,
/// wired to ExecutionContext as non-owning pointer.
struct MongoClientState {
    const MongoClientConfig* config = nullptr;
    class MongoClient* client = nullptr;  // non-owning, points to server-wide singleton
    int per_interpret_queries = 0;
    int lifetime_queries = 0;
    int query_quota = 1000;  // from role permissions; <= 0 means unlimited

    bool can_query() const {
        return query_quota <= 0 || lifetime_queries < query_quota;
    }

    void record_query() {
        ++per_interpret_queries;
        ++lifetime_queries;
    }

    void reset_per_interpret() {
        per_interpret_queries = 0;
    }
};

/// MongoDB client wrapper using mongocxx::pool (pimpl pattern).
///
/// Thread-safe — pool entries are acquired per-operation.
/// One instance per server, shared across all sessions.
class MongoClient {
public:
    MongoClient();
    ~MongoClient();

    // Non-copyable
    MongoClient(const MongoClient&) = delete;
    MongoClient& operator=(const MongoClient&) = delete;

    /// Connect to MongoDB.  Returns true on success.
    bool connect(const std::string& uri, const std::string& db_name);

    /// Returns true if connected.
    bool connected() const;

    // --- Index management ---

    /// Create a unique index on a field in a collection.
    void ensure_unique_index(const std::string& collection,
                             const std::string& field);

    /// Create a TTL index on a field in a collection.
    void ensure_ttl_index(const std::string& collection,
                          const std::string& field,
                          int64_t expire_seconds);

    // --- Generic CRUD (for TIL primitives) ---

    /// Find documents matching a BSON filter with options.
    /// Returns JSON array string.  On error, returns nullopt.
    std::optional<std::string> find(const std::string& collection,
                                     bsoncxx::document::view filter,
                                     const MongoQueryOptions& opts = {});

    /// Count documents matching a BSON filter with options.
    /// Uses count_documents() (server-side).  On error, returns -1.
    int64_t count(const std::string& collection,
                  bsoncxx::document::view filter,
                  const MongoQueryOptions& opts = {});

    /// Insert a JSON document.  Returns the inserted _id as string.
    /// On error, returns nullopt.
    std::optional<std::string> insert(const std::string& collection,
                                       const std::string& doc_json);

    /// Update documents matching a BSON filter.  Returns modified count.
    /// On error, returns -1.
    int64_t update(const std::string& collection,
                   bsoncxx::document::view filter,
                   bsoncxx::document::view update_doc,
                   const MongoQueryOptions& opts = {});

    /// Delete documents matching a BSON filter.  Returns deleted count.
    /// On error, returns -1.
    int64_t remove(const std::string& collection,
                   bsoncxx::document::view filter,
                   const MongoQueryOptions& opts = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace etil::db

#endif // ETIL_MONGODB_ENABLED
