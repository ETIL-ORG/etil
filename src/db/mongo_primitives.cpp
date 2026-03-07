// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef ETIL_MONGODB_ENABLED

#include "etil/db/mongo_primitives.hpp"
#include "etil/db/mongo_client.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_json.hpp"
#include "etil/core/heap_map.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/value_helpers.hpp"
#include "etil/core/word_impl.hpp"
#include "etil/mcp/role_permissions.hpp"

#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/json.hpp>

#include <nlohmann/json.hpp>

#include <string>

namespace etil::db {

namespace {


/// Check mongo_access permission.  Returns false and pushes flag=0 if denied.
bool check_mongo_permission(etil::core::ExecutionContext& ctx) {
    auto* perms = ctx.permissions();
    if (perms && !perms->mongo_access) {
        ctx.err() << "Error: MongoDB access not permitted\n";
        ctx.data_stack().push(etil::core::Value(false));
        return false;
    }
    return true;
}

/// Check mongo client is available.  Returns null and pushes flag=0 if not.
MongoClient* check_mongo_client(etil::core::ExecutionContext& ctx) {
    auto* state = ctx.mongo_client_state();
    if (!state || !state->client || !state->client->connected()) {
        ctx.err() << "Error: MongoDB not configured\n";
        ctx.data_stack().push(etil::core::Value(false));
        return nullptr;
    }
    if (!state->can_query()) {
        ctx.err() << "Error: MongoDB query quota exceeded ("
                  << state->lifetime_queries << "/"
                  << state->query_quota << ")\n";
        ctx.data_stack().push(etil::core::Value(false));
        return nullptr;
    }
    return state->client;
}

// ---------------------------------------------------------------------------
// HeapMap → BSON conversion (recursive)
// ---------------------------------------------------------------------------

using namespace bsoncxx::builder::basic;

void value_to_bson(sub_document& doc, const std::string& key,
                   const etil::core::Value& val);
void value_to_bson_array(sub_array& arr, const etil::core::Value& val);

void value_to_bson(sub_document& doc, const std::string& key,
                   const etil::core::Value& val) {
    using T = etil::core::Value::Type;
    switch (val.type) {
    case T::Integer:
        doc.append(kvp(key, static_cast<std::int64_t>(val.as_int)));
        break;
    case T::Float:
        doc.append(kvp(key, val.as_float));
        break;
    case T::Boolean:
        doc.append(kvp(key, val.as_bool()));
        break;
    case T::String:
        if (val.as_ptr) {
            auto* hs = val.as_string();
            doc.append(kvp(key, std::string(hs->view())));
        }
        break;
    case T::Map:
        if (val.as_ptr) {
            auto* map = val.as_map();
            doc.append(kvp(key, [map](sub_document sub) {
                for (const auto& [k, v] : map->entries()) {
                    value_to_bson(sub, k, v);
                }
            }));
        }
        break;
    case T::Array:
        if (val.as_ptr) {
            auto* arr = val.as_array();
            doc.append(kvp(key, [arr](sub_array sub) {
                for (size_t i = 0; i < arr->length(); ++i) {
                    etil::core::Value elem;
                    if (arr->get(i, elem)) {
                        value_to_bson_array(sub, elem);
                        etil::core::value_release(elem);
                    }
                }
            }));
        }
        break;
    case T::Json:
        if (val.as_ptr) {
            auto* hj = val.as_json();
            auto subdoc = bsoncxx::from_json(hj->dump());
            doc.append(kvp(key, subdoc.view()));
        }
        break;
    default:
        break; // Unsupported types silently ignored
    }
}

void value_to_bson_array(sub_array& arr, const etil::core::Value& val) {
    using T = etil::core::Value::Type;
    switch (val.type) {
    case T::Integer:
        arr.append(static_cast<std::int64_t>(val.as_int));
        break;
    case T::Float:
        arr.append(val.as_float);
        break;
    case T::Boolean:
        arr.append(val.as_bool());
        break;
    case T::String:
        if (val.as_ptr) {
            arr.append(std::string(val.as_string()->view()));
        }
        break;
    case T::Map:
        if (val.as_ptr) {
            auto* map = val.as_map();
            arr.append([map](sub_document sub) {
                for (const auto& [k, v] : map->entries()) {
                    value_to_bson(sub, k, v);
                }
            });
        }
        break;
    case T::Array:
        if (val.as_ptr) {
            auto* inner = val.as_array();
            arr.append([inner](sub_array sub) {
                for (size_t i = 0; i < inner->length(); ++i) {
                    etil::core::Value elem;
                    if (inner->get(i, elem)) {
                        value_to_bson_array(sub, elem);
                        etil::core::value_release(elem);
                    }
                }
            });
        }
        break;
    case T::Json:
        if (val.as_ptr) {
            auto* hj = val.as_json();
            auto subdoc = bsoncxx::from_json(hj->dump());
            arr.append(subdoc.view());
        }
        break;
    default:
        break;
    }
}

/// Convert a HeapMap to an owned BSON document.
bsoncxx::document::value heap_map_to_bson(const etil::core::HeapMap& map) {
    auto builder = bsoncxx::builder::basic::document{};
    for (const auto& [key, val] : map.entries()) {
        value_to_bson(builder, key, val);
    }
    return builder.extract();
}

// ---------------------------------------------------------------------------
// MongoQueryOptions factory functions
// ---------------------------------------------------------------------------

/// Parse recognized option keys from a JSON string.
MongoQueryOptions options_from_json(const std::string& json) {
    MongoQueryOptions opts;
    auto j = nlohmann::json::parse(json);

    if (j.contains("skip") && j["skip"].is_number_integer())
        opts.skip = j["skip"].get<int64_t>();
    if (j.contains("limit") && j["limit"].is_number_integer())
        opts.limit = j["limit"].get<int64_t>();
    if (j.contains("sort") && j["sort"].is_object())
        opts.sort_doc = bsoncxx::from_json(j["sort"].dump());
    if (j.contains("projection") && j["projection"].is_object())
        opts.projection_doc = bsoncxx::from_json(j["projection"].dump());
    if (j.contains("hint") && j["hint"].is_object())
        opts.hint_doc = bsoncxx::from_json(j["hint"].dump());
    if (j.contains("collation") && j["collation"].is_object())
        opts.collation_doc = bsoncxx::from_json(j["collation"].dump());
    if (j.contains("max_time_ms") && j["max_time_ms"].is_number_integer())
        opts.max_time_ms = j["max_time_ms"].get<int64_t>();
    if (j.contains("batch_size") && j["batch_size"].is_number_integer())
        opts.batch_size = j["batch_size"].get<int32_t>();
    if (j.contains("upsert") && j["upsert"].is_boolean())
        opts.upsert = j["upsert"].get<bool>();

    return opts;
}

/// Extract recognized option keys from a HeapMap.
MongoQueryOptions options_from_map(const etil::core::HeapMap& map) {
    MongoQueryOptions opts;
    etil::core::Value val;

    if (map.get("skip", val)) {
        if (val.type == etil::core::Value::Type::Integer)
            opts.skip = val.as_int;
        etil::core::value_release(val);
    }
    if (map.get("limit", val)) {
        if (val.type == etil::core::Value::Type::Integer)
            opts.limit = val.as_int;
        etil::core::value_release(val);
    }
    if (map.get("sort", val)) {
        if (val.type == etil::core::Value::Type::Map && val.as_ptr)
            opts.sort_doc = heap_map_to_bson(*val.as_map());
        etil::core::value_release(val);
    }
    if (map.get("projection", val)) {
        if (val.type == etil::core::Value::Type::Map && val.as_ptr)
            opts.projection_doc = heap_map_to_bson(*val.as_map());
        etil::core::value_release(val);
    }
    if (map.get("hint", val)) {
        if (val.type == etil::core::Value::Type::Map && val.as_ptr)
            opts.hint_doc = heap_map_to_bson(*val.as_map());
        etil::core::value_release(val);
    }
    if (map.get("collation", val)) {
        if (val.type == etil::core::Value::Type::Map && val.as_ptr)
            opts.collation_doc = heap_map_to_bson(*val.as_map());
        etil::core::value_release(val);
    }
    if (map.get("max_time_ms", val)) {
        if (val.type == etil::core::Value::Type::Integer)
            opts.max_time_ms = val.as_int;
        etil::core::value_release(val);
    }
    if (map.get("batch_size", val)) {
        if (val.type == etil::core::Value::Type::Integer)
            opts.batch_size = static_cast<int32_t>(val.as_int);
        etil::core::value_release(val);
    }
    if (map.get("upsert", val)) {
        // Accept integer (FORTH flag -1/0) or any non-zero as true
        if (val.type == etil::core::Value::Type::Integer)
            opts.upsert = (val.as_int != 0);
        etil::core::value_release(val);
    }

    return opts;
}

/// Pop a String or Json value and return as JSON string for BSON parsing.
/// Accepts String (pass-through) or Json (dump). Returns nullopt on type error.
std::optional<std::string> pop_json_string(etil::core::ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return std::nullopt;
    if (opt->type == etil::core::Value::Type::String && opt->as_ptr) {
        auto* hs = opt->as_string();
        std::string result(hs->view());
        hs->release();
        return result;
    }
    if (opt->type == etil::core::Value::Type::Json && opt->as_ptr) {
        auto* hj = opt->as_json();
        std::string result = hj->dump();
        hj->release();
        return result;
    }
    ctx.err() << "Error: expected string or json\n";
    etil::core::value_release(*opt);
    return std::nullopt;
}

/// Pop options: accepts String, Json, or Map. Returns options struct.
/// Returns nullopt on error.
std::optional<MongoQueryOptions> pop_options(etil::core::ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return std::nullopt;
    if (opt->type == etil::core::Value::Type::String && opt->as_ptr) {
        auto* hs = opt->as_string();
        std::string str(hs->view());
        hs->release();
        try {
            return options_from_json(str);
        } catch (...) {
            return std::nullopt;
        }
    }
    if (opt->type == etil::core::Value::Type::Json && opt->as_ptr) {
        auto* hj = opt->as_json();
        std::string str = hj->dump();
        hj->release();
        try {
            return options_from_json(str);
        } catch (...) {
            return std::nullopt;
        }
    }
    if (opt->type == etil::core::Value::Type::Map && opt->as_ptr) {
        auto* m = opt->as_map();
        auto result = options_from_map(*m);
        m->release();
        return result;
    }
    ctx.err() << "Error: expected string, json, or map for options\n";
    etil::core::value_release(*opt);
    return std::nullopt;
}

} // anonymous namespace

// ===========================================================================
// MongoDB primitives (5 words) — accept String, Json, or Map for filter/opts
// ===========================================================================

// mongo-find ( coll filter opts -- result-json flag )
// filter/opts accept String or Json
static bool prim_mongo_find(etil::core::ExecutionContext& ctx) {
    using namespace etil::core;

    auto opts = pop_options(ctx);
    if (!opts) return false;

    auto filter_str = pop_json_string(ctx);
    if (!filter_str) return false;

    auto* coll_hs = pop_string(ctx);
    if (!coll_hs) return false;
    std::string collection(coll_hs->view());
    coll_hs->release();

    if (!check_mongo_permission(ctx)) return true;
    auto* client = check_mongo_client(ctx);
    if (!client) return true;

    ctx.mongo_client_state()->record_query();

    auto filter_doc = bsoncxx::from_json(*filter_str);
    auto result = client->find(collection, filter_doc.view(), *opts);
    if (!result) {
        ctx.err() << "Error: mongo-find failed\n";
        ctx.data_stack().push(Value(false));
        return true;
    }

    auto json = nlohmann::json::parse(*result);
    auto* hj = new etil::core::HeapJson(std::move(json));
    hj->set_tainted(true);
    ctx.data_stack().push(Value::from(hj));
    ctx.data_stack().push(Value(true));
    return true;
}

// mongo-count ( coll filter opts -- count flag )
static bool prim_mongo_count(etil::core::ExecutionContext& ctx) {
    using namespace etil::core;

    auto opts = pop_options(ctx);
    if (!opts) return false;

    auto filter_str = pop_json_string(ctx);
    if (!filter_str) return false;

    auto* coll_hs = pop_string(ctx);
    if (!coll_hs) return false;
    std::string collection(coll_hs->view());
    coll_hs->release();

    if (!check_mongo_permission(ctx)) return true;
    auto* client = check_mongo_client(ctx);
    if (!client) return true;

    ctx.mongo_client_state()->record_query();

    auto filter_doc = bsoncxx::from_json(*filter_str);
    int64_t count = client->count(collection, filter_doc.view(), *opts);
    if (count < 0) {
        ctx.err() << "Error: mongo-count failed\n";
        ctx.data_stack().push(Value(false));
        return true;
    }

    ctx.data_stack().push(Value(count));
    ctx.data_stack().push(Value(true));
    return true;
}

// mongo-insert ( coll doc -- id flag )
static bool prim_mongo_insert(etil::core::ExecutionContext& ctx) {
    using namespace etil::core;

    auto doc_opt = pop_json_string(ctx);
    if (!doc_opt) return false;
    std::string doc = std::move(*doc_opt);

    auto* coll_hs = pop_string(ctx);
    if (!coll_hs) return false;
    std::string collection(coll_hs->view());
    coll_hs->release();

    if (!check_mongo_permission(ctx)) return true;
    auto* client = check_mongo_client(ctx);
    if (!client) return true;

    ctx.mongo_client_state()->record_query();

    auto result = client->insert(collection, doc);
    if (!result) {
        ctx.err() << "Error: mongo-insert failed\n";
        ctx.data_stack().push(Value(false));
        return true;
    }

    auto* hs = HeapString::create(*result);
    hs->set_tainted(true);
    ctx.data_stack().push(Value::from(hs));
    ctx.data_stack().push(Value(true));
    return true;
}

// mongo-update ( coll filter update opts -- count flag )
static bool prim_mongo_update(etil::core::ExecutionContext& ctx) {
    using namespace etil::core;

    auto opts = pop_options(ctx);
    if (!opts) return false;

    auto update_str = pop_json_string(ctx);
    if (!update_str) return false;

    auto filter_str = pop_json_string(ctx);
    if (!filter_str) return false;

    auto* coll_hs = pop_string(ctx);
    if (!coll_hs) return false;
    std::string collection(coll_hs->view());
    coll_hs->release();

    if (!check_mongo_permission(ctx)) return true;
    auto* client = check_mongo_client(ctx);
    if (!client) return true;

    ctx.mongo_client_state()->record_query();

    auto filter_doc = bsoncxx::from_json(*filter_str);
    auto update_doc = bsoncxx::from_json(*update_str);
    int64_t count = client->update(collection, filter_doc.view(),
                                    update_doc.view(), *opts);
    if (count < 0) {
        ctx.err() << "Error: mongo-update failed\n";
        ctx.data_stack().push(Value(false));
        return true;
    }

    ctx.data_stack().push(Value(count));
    ctx.data_stack().push(Value(true));
    return true;
}

// mongo-delete ( coll filter opts -- count flag )
static bool prim_mongo_delete(etil::core::ExecutionContext& ctx) {
    using namespace etil::core;

    auto opts = pop_options(ctx);
    if (!opts) return false;

    auto filter_str = pop_json_string(ctx);
    if (!filter_str) return false;

    auto* coll_hs = pop_string(ctx);
    if (!coll_hs) return false;
    std::string collection(coll_hs->view());
    coll_hs->release();

    if (!check_mongo_permission(ctx)) return true;
    auto* client = check_mongo_client(ctx);
    if (!client) return true;

    ctx.mongo_client_state()->record_query();

    auto filter_doc = bsoncxx::from_json(*filter_str);
    int64_t count = client->remove(collection, filter_doc.view(), *opts);
    if (count < 0) {
        ctx.err() << "Error: mongo-delete failed\n";
        ctx.data_stack().push(Value(false));
        return true;
    }

    ctx.data_stack().push(Value(count));
    ctx.data_stack().push(Value(true));
    return true;
}

// ===========================================================================
// Registration
// ===========================================================================

void register_mongo_primitives(etil::core::Dictionary& dict) {
    using namespace etil::core;
    using TS = TypeSignature;
    using T = TS::Type;

    dict.register_word("mongo-find",
        make_primitive("prim_mongo_find", prim_mongo_find,
            {T::String, T::String, T::String}, {T::String, T::Integer}));

    dict.register_word("mongo-count",
        make_primitive("prim_mongo_count", prim_mongo_count,
            {T::String, T::String, T::String}, {T::Integer, T::Integer}));

    dict.register_word("mongo-insert",
        make_primitive("prim_mongo_insert", prim_mongo_insert,
            {T::String, T::String}, {T::String, T::Integer}));

    dict.register_word("mongo-update",
        make_primitive("prim_mongo_update", prim_mongo_update,
            {T::String, T::String, T::String, T::String},
            {T::Integer, T::Integer}));

    dict.register_word("mongo-delete",
        make_primitive("prim_mongo_delete", prim_mongo_delete,
            {T::String, T::String, T::String}, {T::Integer, T::Integer}));
}

} // namespace etil::db

#endif // ETIL_MONGODB_ENABLED
