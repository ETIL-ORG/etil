// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/json_primitives.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_json.hpp"
#include "etil/core/heap_map.hpp"
#include "etil/core/heap_matrix.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/value_helpers.hpp"
#include "etil/core/word_impl.hpp"

#include <nlohmann/json.hpp>

namespace etil::core {

namespace {

// Convert a nlohmann::json leaf/node to an ETIL Value and push it.
// For objects and arrays, wraps in HeapJson (stays wrapped).
void json_value_to_stack(ExecutionContext& ctx, const nlohmann::json& j) {
    switch (j.type()) {
    case nlohmann::json::value_t::string: {
        auto* hs = HeapString::create(j.get<std::string>());
        ctx.data_stack().push(Value::from(hs));
        break;
    }
    case nlohmann::json::value_t::number_integer:
        ctx.data_stack().push(Value(j.get<int64_t>()));
        break;
    case nlohmann::json::value_t::number_unsigned: {
        auto uval = j.get<uint64_t>();
        ctx.data_stack().push(Value(static_cast<int64_t>(uval)));
        break;
    }
    case nlohmann::json::value_t::number_float:
        ctx.data_stack().push(Value(j.get<double>()));
        break;
    case nlohmann::json::value_t::boolean:
        ctx.data_stack().push(Value(j.get<bool>()));
        break;
    case nlohmann::json::value_t::null:
        ctx.data_stack().push(Value(false));
        break;
    case nlohmann::json::value_t::object:
    case nlohmann::json::value_t::array: {
        auto* hj = new HeapJson(j);
        ctx.data_stack().push(Value::from(hj));
        break;
    }
    default:
        ctx.data_stack().push(Value(false));
        break;
    }
}

// Recursively convert JSON object to HeapMap
HeapMap* json_to_heap_map(const nlohmann::json& j);
HeapArray* json_to_heap_array(const nlohmann::json& j);

Value json_to_etil_value(const nlohmann::json& j) {
    switch (j.type()) {
    case nlohmann::json::value_t::string: {
        auto* hs = HeapString::create(j.get<std::string>());
        return Value::from(hs);
    }
    case nlohmann::json::value_t::number_integer:
        return Value(j.get<int64_t>());
    case nlohmann::json::value_t::number_unsigned:
        return Value(static_cast<int64_t>(j.get<uint64_t>()));
    case nlohmann::json::value_t::number_float:
        return Value(j.get<double>());
    case nlohmann::json::value_t::boolean:
        return Value(j.get<bool>());
    case nlohmann::json::value_t::object: {
        auto* m = json_to_heap_map(j);
        return Value::from(m);
    }
    case nlohmann::json::value_t::array: {
        auto* a = json_to_heap_array(j);
        return Value::from(a);
    }
    default:
        return Value(false);
    }
}

HeapMap* json_to_heap_map(const nlohmann::json& j) {
    auto* m = new HeapMap();
    for (auto it = j.begin(); it != j.end(); ++it) {
        m->set(it.key(), json_to_etil_value(it.value()));
    }
    return m;
}

HeapArray* json_to_heap_array(const nlohmann::json& j) {
    auto* a = new HeapArray();
    for (const auto& elem : j) {
        a->push_back(json_to_etil_value(elem));
    }
    return a;
}

// Recursively convert HeapMap to JSON object
nlohmann::json heap_map_to_json(const HeapMap& map);
nlohmann::json heap_array_to_json(const HeapArray& arr);

nlohmann::json etil_value_to_json(const Value& v) {
    switch (v.type) {
    case Value::Type::Integer:
        return nlohmann::json(v.as_int);
    case Value::Type::Float:
        return nlohmann::json(v.as_float);
    case Value::Type::Boolean:
        return nlohmann::json(v.as_bool());
    case Value::Type::String:
        if (v.as_ptr) return nlohmann::json(std::string(v.as_string()->view()));
        return nlohmann::json("");
    case Value::Type::Map:
        if (v.as_ptr) return heap_map_to_json(*v.as_map());
        return nlohmann::json::object();
    case Value::Type::Array:
        if (v.as_ptr) return heap_array_to_json(*v.as_array());
        return nlohmann::json::array();
    case Value::Type::Json:
        if (v.as_ptr) return v.as_json()->json();
        return nlohmann::json(nullptr);
    case Value::Type::Matrix:
        if (v.as_ptr) {
            auto* mat = v.as_matrix();
            nlohmann::json rows = nlohmann::json::array();
            for (int64_t r = 0; r < mat->rows(); ++r) {
                nlohmann::json row = nlohmann::json::array();
                for (int64_t c = 0; c < mat->cols(); ++c) {
                    row.push_back(mat->get(r, c));
                }
                rows.push_back(row);
            }
            return nlohmann::json{{"type", "matrix"}, {"rows", mat->rows()},
                                  {"cols", mat->cols()}, {"data", rows}};
        }
        return nlohmann::json(nullptr);
    default:
        return nlohmann::json(nullptr);
    }
}

nlohmann::json heap_map_to_json(const HeapMap& map) {
    nlohmann::json obj = nlohmann::json::object();
    for (const auto& [key, val] : map.entries()) {
        obj[key] = etil_value_to_json(val);
    }
    return obj;
}

nlohmann::json heap_array_to_json(const HeapArray& arr) {
    nlohmann::json ja = nlohmann::json::array();
    for (size_t i = 0; i < arr.length(); ++i) {
        Value elem;
        if (arr.get(i, elem)) {
            ja.push_back(etil_value_to_json(elem));
            value_release(elem);
        }
    }
    return ja;
}

} // anonymous namespace

// json-parse ( string -- json )
bool prim_json_parse(ExecutionContext& ctx) {
    auto* hs = pop_string(ctx);
    if (!hs) return false;
    std::string str(hs->view());
    hs->release();
    try {
        auto j = nlohmann::json::parse(str);
        auto* hj = new HeapJson(std::move(j));
        ctx.data_stack().push(Value::from(hj));
        return true;
    } catch (const nlohmann::json::parse_error& e) {
        ctx.err() << "Error: json-parse: " << e.what() << "\n";
        return false;
    }
}

// json-dump ( json -- string )
bool prim_json_dump(ExecutionContext& ctx) {
    auto* hj = pop_json(ctx);
    if (!hj) return false;
    auto* hs = HeapString::create(hj->dump());
    hj->release();
    ctx.data_stack().push(Value::from(hs));
    return true;
}

// json-pretty ( json -- string )
bool prim_json_pretty(ExecutionContext& ctx) {
    auto* hj = pop_json(ctx);
    if (!hj) return false;
    auto* hs = HeapString::create(hj->dump(2));
    hj->release();
    ctx.data_stack().push(Value::from(hs));
    return true;
}

// json-get ( json key -- value )
// key is string for objects, integer for arrays
bool prim_json_get(ExecutionContext& ctx) {
    auto key_opt = ctx.data_stack().pop();
    if (!key_opt) return false;

    auto* hj = pop_json(ctx);
    if (!hj) { value_release(*key_opt); return false; }

    const auto& j = hj->json();

    if (key_opt->type == Value::Type::Integer) {
        // Array index access
        int64_t idx = key_opt->as_int;
        if (!j.is_array()) {
            ctx.err() << "Error: json-get: integer index on non-array\n";
            hj->release();
            return false;
        }
        if (idx < 0 || static_cast<size_t>(idx) >= j.size()) {
            ctx.err() << "Error: json-get: index " << idx << " out of range (size " << j.size() << ")\n";
            hj->release();
            return false;
        }
        json_value_to_stack(ctx, j[static_cast<size_t>(idx)]);
        hj->release();
        return true;
    }

    if (key_opt->type == Value::Type::String && key_opt->as_ptr) {
        // Object key access
        auto* key_hs = key_opt->as_string();
        std::string key(key_hs->view());
        key_hs->release();

        if (!j.is_object() || !j.contains(key)) {
            ctx.err() << "Error: json-get: key '" << key << "' not found\n";
            hj->release();
            return false;
        }
        json_value_to_stack(ctx, j[key]);
        hj->release();
        return true;
    }

    ctx.err() << "Error: json-get: key must be string or integer\n";
    value_release(*key_opt);
    hj->release();
    return false;
}

// json-length ( json -- int )
bool prim_json_length(ExecutionContext& ctx) {
    auto* hj = pop_json(ctx);
    if (!hj) return false;
    const auto& j = hj->json();
    int64_t len = 0;
    if (j.is_array() || j.is_object()) {
        len = static_cast<int64_t>(j.size());
    }
    hj->release();
    ctx.data_stack().push(Value(len));
    return true;
}

// json-type ( json -- string )
bool prim_json_type(ExecutionContext& ctx) {
    auto* hj = pop_json(ctx);
    if (!hj) return false;
    const char* type_str = "unknown";
    switch (hj->json().type()) {
    case nlohmann::json::value_t::object:           type_str = "object"; break;
    case nlohmann::json::value_t::array:            type_str = "array"; break;
    case nlohmann::json::value_t::string:           type_str = "string"; break;
    case nlohmann::json::value_t::number_integer:   type_str = "number"; break;
    case nlohmann::json::value_t::number_unsigned:   type_str = "number"; break;
    case nlohmann::json::value_t::number_float:     type_str = "number"; break;
    case nlohmann::json::value_t::boolean:          type_str = "boolean"; break;
    case nlohmann::json::value_t::null:             type_str = "null"; break;
    default: break;
    }
    hj->release();
    auto* hs = HeapString::create(type_str);
    ctx.data_stack().push(Value::from(hs));
    return true;
}

// json-keys ( json -- array )
bool prim_json_keys(ExecutionContext& ctx) {
    auto* hj = pop_json(ctx);
    if (!hj) return false;
    const auto& j = hj->json();
    if (!j.is_object()) {
        ctx.err() << "Error: json-keys: not an object\n";
        hj->release();
        return false;
    }
    auto* arr = new HeapArray();
    for (auto it = j.begin(); it != j.end(); ++it) {
        auto* hs = HeapString::create(it.key());
        arr->push_back(Value::from(hs));
    }
    hj->release();
    ctx.data_stack().push(Value::from(arr));
    return true;
}

// json->map ( json -- map )
bool prim_json_to_map(ExecutionContext& ctx) {
    auto* hj = pop_json(ctx);
    if (!hj) return false;
    const auto& j = hj->json();
    if (!j.is_object()) {
        ctx.err() << "Error: json->map: not an object\n";
        hj->release();
        return false;
    }
    auto* m = json_to_heap_map(j);
    hj->release();
    ctx.data_stack().push(Value::from(m));
    return true;
}

// json->array ( json -- array )
bool prim_json_to_array(ExecutionContext& ctx) {
    auto* hj = pop_json(ctx);
    if (!hj) return false;
    const auto& j = hj->json();
    if (!j.is_array()) {
        ctx.err() << "Error: json->array: not an array\n";
        hj->release();
        return false;
    }
    auto* a = json_to_heap_array(j);
    hj->release();
    ctx.data_stack().push(Value::from(a));
    return true;
}

// mat->json ( mat -- json )
bool prim_mat_to_json(ExecutionContext& ctx) {
    auto* mat = pop_matrix(ctx);
    if (!mat) return false;
    nlohmann::json data = nlohmann::json::array();
    for (int64_t r = 0; r < mat->rows(); ++r) {
        nlohmann::json row = nlohmann::json::array();
        for (int64_t c = 0; c < mat->cols(); ++c) {
            row.push_back(mat->get(r, c));
        }
        data.push_back(row);
    }
    auto j = nlohmann::json{{"rows", mat->rows()}, {"cols", mat->cols()}, {"data", data}};
    mat->release();
    ctx.data_stack().push(Value::from(new HeapJson(std::move(j))));
    return true;
}

// json->mat ( json -- mat )
bool prim_json_to_mat(ExecutionContext& ctx) {
    auto* hj = pop_json(ctx);
    if (!hj) return false;
    const auto& j = hj->json();
    if (!j.is_object()) {
        ctx.err() << "Error: json->mat: not an object\n";
        hj->release();
        return false;
    }
    if (!j.contains("rows") || !j.contains("cols") || !j.contains("data")) {
        ctx.err() << "Error: json->mat: missing rows, cols, or data key\n";
        hj->release();
        return false;
    }
    if (!j["rows"].is_number_integer() || !j["cols"].is_number_integer()) {
        ctx.err() << "Error: json->mat: rows and cols must be integers\n";
        hj->release();
        return false;
    }
    int64_t rows = j["rows"].get<int64_t>();
    int64_t cols = j["cols"].get<int64_t>();
    if (rows <= 0 || cols <= 0) {
        ctx.err() << "Error: json->mat: rows and cols must be positive\n";
        hj->release();
        return false;
    }
    const auto& data = j["data"];
    if (!data.is_array() || static_cast<int64_t>(data.size()) != rows) {
        ctx.err() << "Error: json->mat: data must be an array of " << rows << " rows\n";
        hj->release();
        return false;
    }
    auto* mat = new HeapMatrix(rows, cols);
    for (int64_t r = 0; r < rows; ++r) {
        const auto& row = data[static_cast<size_t>(r)];
        if (!row.is_array() || static_cast<int64_t>(row.size()) != cols) {
            ctx.err() << "Error: json->mat: row " << r << " must have " << cols << " elements\n";
            mat->release();
            hj->release();
            return false;
        }
        for (int64_t c = 0; c < cols; ++c) {
            const auto& elem = row[static_cast<size_t>(c)];
            if (!elem.is_number()) {
                ctx.err() << "Error: json->mat: non-numeric value at [" << r << "][" << c << "]\n";
                mat->release();
                hj->release();
                return false;
            }
            mat->set(r, c, elem.get<double>());
        }
    }
    hj->release();
    ctx.data_stack().push(Value::from(mat));
    return true;
}

// map->json ( map -- json )
bool prim_map_to_json(ExecutionContext& ctx) {
    auto* m = pop_map(ctx);
    if (!m) return false;
    auto j = heap_map_to_json(*m);
    m->release();
    auto* hj = new HeapJson(std::move(j));
    ctx.data_stack().push(Value::from(hj));
    return true;
}

// array->json ( array -- json )
bool prim_array_to_json(ExecutionContext& ctx) {
    auto* a = pop_array(ctx);
    if (!a) return false;
    auto j = heap_array_to_json(*a);
    a->release();
    auto* hj = new HeapJson(std::move(j));
    ctx.data_stack().push(Value::from(hj));
    return true;
}

// json->value ( json -- value )
bool prim_json_to_value(ExecutionContext& ctx) {
    auto* hj = pop_json(ctx);
    if (!hj) return false;
    const auto& j = hj->json();
    Value v = json_to_etil_value(j);
    hj->release();
    ctx.data_stack().push(v);
    return true;
}

void register_json_primitives(Dictionary& dict) {
    using TS = TypeSignature;
    using T = TS::Type;

    dict.register_word("json-parse",
        make_primitive("prim_json_parse", prim_json_parse,
            {T::String}, {T::Custom}));

    dict.register_word("json-dump",
        make_primitive("prim_json_dump", prim_json_dump,
            {T::Custom}, {T::String}));

    dict.register_word("json-pretty",
        make_primitive("prim_json_pretty", prim_json_pretty,
            {T::Custom}, {T::String}));

    dict.register_word("json-get",
        make_primitive("prim_json_get", prim_json_get,
            {T::Custom, T::String}, {T::Custom}));

    dict.register_word("json-length",
        make_primitive("prim_json_length", prim_json_length,
            {T::Custom}, {T::Integer}));

    dict.register_word("json-type",
        make_primitive("prim_json_type", prim_json_type,
            {T::Custom}, {T::String}));

    dict.register_word("json-keys",
        make_primitive("prim_json_keys", prim_json_keys,
            {T::Custom}, {T::Custom}));

    dict.register_word("json->map",
        make_primitive("prim_json_to_map", prim_json_to_map,
            {T::Custom}, {T::Custom}));

    dict.register_word("json->array",
        make_primitive("prim_json_to_array", prim_json_to_array,
            {T::Custom}, {T::Custom}));

    dict.register_word("map->json",
        make_primitive("prim_map_to_json", prim_map_to_json,
            {T::Custom}, {T::Custom}));

    dict.register_word("array->json",
        make_primitive("prim_array_to_json", prim_array_to_json,
            {T::Custom}, {T::Custom}));

    dict.register_word("json->value",
        make_primitive("prim_json_to_value", prim_json_to_value,
            {T::Custom}, {T::Custom}));

    dict.register_word("mat->json",
        make_primitive("prim_mat_to_json", prim_mat_to_json,
            {T::Custom}, {T::Custom}));

    dict.register_word("json->mat",
        make_primitive("prim_json_to_mat", prim_json_to_mat,
            {T::Custom}, {T::Custom}));
}

} // namespace etil::core
