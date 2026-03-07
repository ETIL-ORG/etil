// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef ETIL_MONGODB_ENABLED

#include "etil/db/mongo_client.hpp"

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/exception/exception.hpp>
#include <mongocxx/hint.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/options/count.hpp>
#include <mongocxx/options/delete.hpp>
#include <mongocxx/options/find.hpp>
#include <mongocxx/options/index.hpp>
#include <mongocxx/options/update.hpp>
#include <mongocxx/pool.hpp>
#include <mongocxx/uri.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>

namespace etil::db {

// ---------------------------------------------------------------------------
// mongocxx::instance must be created exactly once per process.
// ---------------------------------------------------------------------------
namespace {
std::once_flag instance_flag;
void ensure_instance() {
    std::call_once(instance_flag, [] {
        static mongocxx::instance inst{};
    });
}
} // namespace

// ---------------------------------------------------------------------------
// MongoConnectionsConfig
// ---------------------------------------------------------------------------

namespace {
std::string read_file_contents(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}
} // namespace

MongoClientConfig MongoConnectionsConfig::from_file(const std::string& path) {
    auto content = read_file_contents(path);
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(content);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("Failed to parse MongoDB connections config '" +
                                 path + "': " + e.what());
    }

    if (!j.contains("default") || !j["default"].is_string()) {
        throw std::runtime_error(
            "MongoDB connections config missing 'default' key: " + path);
    }
    return from_file(path, j["default"].get<std::string>());
}

MongoClientConfig MongoConnectionsConfig::from_file(
    const std::string& path, const std::string& connection_name) {
    auto content = read_file_contents(path);
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(content);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("Failed to parse MongoDB connections config '" +
                                 path + "': " + e.what());
    }

    if (!j.contains("connections") || !j["connections"].is_object()) {
        throw std::runtime_error(
            "MongoDB connections config missing 'connections' object: " + path);
    }

    auto& conns = j["connections"];
    if (!conns.contains(connection_name) ||
        !conns[connection_name].is_object()) {
        throw std::runtime_error("MongoDB connection '" + connection_name +
                                 "' not found in: " + path);
    }

    auto& conn = conns[connection_name];
    MongoClientConfig cfg;
    if (conn.contains("uri") && conn["uri"].is_string()) {
        cfg.uri = conn["uri"].get<std::string>();
    }
    if (conn.contains("database") && conn["database"].is_string()) {
        cfg.database = conn["database"].get<std::string>();
    }
    if (cfg.database.empty()) {
        cfg.database = "etil";
    }
    return cfg;
}

MongoClientConfig MongoConnectionsConfig::resolve() {
    MongoClientConfig cfg;

    // 1. Try ETIL_MONGODB_CONFIG file
    if (const char* config_path = std::getenv("ETIL_MONGODB_CONFIG")) {
        if (config_path[0] != '\0') {
            try {
                cfg = from_file(config_path);
                fprintf(stderr, "MongoDB config loaded from: %s\n",
                        config_path);
            } catch (const std::exception& e) {
                fprintf(stderr, "Warning: failed to load MongoDB config '%s': "
                                "%s\n",
                        config_path, e.what());
            }
        }
    }

    // 2. Environment overrides
    if (const char* v = std::getenv("ETIL_MONGODB_URI")) {
        if (v[0] != '\0') cfg.uri = v;
    }
    if (const char* v = std::getenv("ETIL_MONGODB_DATABASE")) {
        if (v[0] != '\0') cfg.database = v;
    }
    if (cfg.database.empty()) {
        cfg.database = "etil";
    }

    return cfg;
}

// ---------------------------------------------------------------------------
// MongoClient::Impl (pimpl)
// ---------------------------------------------------------------------------

struct MongoClient::Impl {
    std::unique_ptr<mongocxx::pool> pool;
    std::string db_name;

    /// Get a database handle from a pooled client.
    mongocxx::database get_db(mongocxx::pool::entry& client) {
        return (*client)[db_name];
    }
};

// ---------------------------------------------------------------------------
// MongoClient
// ---------------------------------------------------------------------------

MongoClient::MongoClient() : impl_(std::make_unique<Impl>()) {}

MongoClient::~MongoClient() = default;

bool MongoClient::connect(const std::string& uri, const std::string& db_name) {
    ensure_instance();
    try {
        mongocxx::uri mongo_uri{uri};
        impl_->pool = std::make_unique<mongocxx::pool>(std::move(mongo_uri));
        impl_->db_name = db_name;

        // Verify connectivity
        auto client = impl_->pool->acquire();
        (void)impl_->get_db(client);

        fprintf(stderr, "MongoDB connected: %s (database: %s)\n",
                uri.c_str(), db_name.c_str());
        return true;
    } catch (const mongocxx::exception& e) {
        fprintf(stderr, "MongoDB connection failed: %s\n", e.what());
        return false;
    }
}

bool MongoClient::connected() const {
    return impl_->pool != nullptr;
}

// ---------------------------------------------------------------------------
// Index management
// ---------------------------------------------------------------------------

void MongoClient::ensure_unique_index(const std::string& collection,
                                       const std::string& field) {
    if (!connected()) return;
    try {
        using namespace bsoncxx::builder::basic;
        auto client = impl_->pool->acquire();
        auto db = impl_->get_db(client);
        auto index_doc = make_document(kvp(field, 1));
        mongocxx::options::index opts;
        opts.unique(true);
        db[collection].create_index(index_doc.view(), opts);
    } catch (const mongocxx::exception& e) {
        fprintf(stderr, "MongoDB ensure_unique_index error: %s\n", e.what());
    }
}

void MongoClient::ensure_ttl_index(const std::string& collection,
                                    const std::string& field,
                                    int64_t expire_seconds) {
    if (!connected()) return;
    try {
        using namespace bsoncxx::builder::basic;
        auto client = impl_->pool->acquire();
        auto db = impl_->get_db(client);
        auto index_doc = make_document(kvp(field, 1));
        mongocxx::options::index opts;
        opts.expire_after(std::chrono::seconds(expire_seconds));
        db[collection].create_index(index_doc.view(), opts);
    } catch (const mongocxx::exception& e) {
        fprintf(stderr, "MongoDB ensure_ttl_index error: %s\n", e.what());
    }
}

// ---------------------------------------------------------------------------
// Generic CRUD (for TIL primitives)
// ---------------------------------------------------------------------------

std::optional<std::string> MongoClient::find(const std::string& collection,
                                              bsoncxx::document::view filter,
                                              const MongoQueryOptions& opts) {
    if (!connected()) return std::nullopt;
    try {
        auto client = impl_->pool->acquire();
        auto db = impl_->get_db(client);

        mongocxx::options::find find_opts;
        if (opts.skip) find_opts.skip(*opts.skip);
        if (opts.limit) find_opts.limit(*opts.limit);
        if (opts.sort_doc) find_opts.sort(opts.sort_doc->view());
        if (opts.projection_doc) find_opts.projection(opts.projection_doc->view());
        if (opts.hint_doc) find_opts.hint(mongocxx::hint{opts.hint_doc->view()});
        if (opts.collation_doc) find_opts.collation(opts.collation_doc->view());
        if (opts.max_time_ms) find_opts.max_time(std::chrono::milliseconds(*opts.max_time_ms));
        if (opts.batch_size) find_opts.batch_size(*opts.batch_size);

        auto cursor = db[collection].find(filter, find_opts);

        std::string result = "[";
        bool first = true;
        for (const auto& doc : cursor) {
            if (!first) result += ",";
            result += bsoncxx::to_json(doc);
            first = false;
        }
        result += "]";
        return result;
    } catch (const mongocxx::exception& e) {
        fprintf(stderr, "MongoDB find error: %s\n", e.what());
        return std::nullopt;
    } catch (const std::invalid_argument& e) {
        fprintf(stderr, "MongoDB find JSON error: %s\n", e.what());
        return std::nullopt;
    }
}

int64_t MongoClient::count(const std::string& collection,
                            bsoncxx::document::view filter,
                            const MongoQueryOptions& opts) {
    if (!connected()) return -1;
    try {
        auto client = impl_->pool->acquire();
        auto db = impl_->get_db(client);

        mongocxx::options::count count_opts;
        if (opts.skip) count_opts.skip(*opts.skip);
        if (opts.limit) count_opts.limit(*opts.limit);
        if (opts.hint_doc) count_opts.hint(mongocxx::hint{opts.hint_doc->view()});
        if (opts.collation_doc) count_opts.collation(opts.collation_doc->view());
        if (opts.max_time_ms) count_opts.max_time(std::chrono::milliseconds(*opts.max_time_ms));

        return static_cast<int64_t>(
            db[collection].count_documents(filter, count_opts));
    } catch (const mongocxx::exception& e) {
        fprintf(stderr, "MongoDB count error: %s\n", e.what());
        return -1;
    } catch (const std::invalid_argument& e) {
        fprintf(stderr, "MongoDB count JSON error: %s\n", e.what());
        return -1;
    }
}

std::optional<std::string> MongoClient::insert(const std::string& collection,
                                                const std::string& doc_json) {
    if (!connected()) return std::nullopt;
    try {
        auto client = impl_->pool->acquire();
        auto db = impl_->get_db(client);

        auto doc = bsoncxx::from_json(doc_json);
        auto result = db[collection].insert_one(doc.view());
        if (result) {
            return result->inserted_id().get_oid().value.to_string();
        }
        return std::nullopt;
    } catch (const mongocxx::exception& e) {
        fprintf(stderr, "MongoDB insert error: %s\n", e.what());
        return std::nullopt;
    } catch (const std::invalid_argument& e) {
        fprintf(stderr, "MongoDB insert JSON error: %s\n", e.what());
        return std::nullopt;
    }
}

int64_t MongoClient::update(const std::string& collection,
                             bsoncxx::document::view filter,
                             bsoncxx::document::view update_doc,
                             const MongoQueryOptions& opts) {
    if (!connected()) return -1;
    try {
        auto client = impl_->pool->acquire();
        auto db = impl_->get_db(client);

        mongocxx::options::update update_opts;
        if (opts.upsert) update_opts.upsert(*opts.upsert);
        if (opts.hint_doc) update_opts.hint(mongocxx::hint{opts.hint_doc->view()});
        if (opts.collation_doc) update_opts.collation(opts.collation_doc->view());

        auto result =
            db[collection].update_many(filter, update_doc, update_opts);
        if (result) {
            return static_cast<int64_t>(result->modified_count());
        }
        return -1;
    } catch (const mongocxx::exception& e) {
        fprintf(stderr, "MongoDB update error: %s\n", e.what());
        return -1;
    } catch (const std::invalid_argument& e) {
        fprintf(stderr, "MongoDB update JSON error: %s\n", e.what());
        return -1;
    }
}

int64_t MongoClient::remove(const std::string& collection,
                             bsoncxx::document::view filter,
                             const MongoQueryOptions& opts) {
    if (!connected()) return -1;
    try {
        auto client = impl_->pool->acquire();
        auto db = impl_->get_db(client);

        mongocxx::options::delete_options delete_opts;
        if (opts.hint_doc) delete_opts.hint(mongocxx::hint{opts.hint_doc->view()});
        if (opts.collation_doc) delete_opts.collation(opts.collation_doc->view());

        auto result = db[collection].delete_many(filter, delete_opts);
        if (result) {
            return static_cast<int64_t>(result->deleted_count());
        }
        return -1;
    } catch (const mongocxx::exception& e) {
        fprintf(stderr, "MongoDB delete error: %s\n", e.what());
        return -1;
    } catch (const std::invalid_argument& e) {
        fprintf(stderr, "MongoDB delete JSON error: %s\n", e.what());
        return -1;
    }
}

} // namespace etil::db

#endif // ETIL_MONGODB_ENABLED
