// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/heap_primitives.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_map.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/value_helpers.hpp"
#include "etil/core/word_impl.hpp"

#include <string>

namespace etil::core {

// map-new ( -- map )
bool prim_map_new(ExecutionContext& ctx) {
    auto* m = new HeapMap();
    ctx.data_stack().push(Value::from(m));
    return true;
}

// map-set ( map key val -- map )
bool prim_map_set(ExecutionContext& ctx) {

    // Pop val
    auto opt_val = ctx.data_stack().pop();
    if (!opt_val) return false;

    // Pop key (string)
    std::string key;
    auto opt_key = ctx.data_stack().pop();
    if (!opt_key) {
        ctx.data_stack().push(*opt_val);
        return false;
    }
    if (opt_key->type != Value::Type::String || !opt_key->as_ptr) {
        ctx.data_stack().push(*opt_key);
        ctx.data_stack().push(*opt_val);
        return false;
    }
    auto* hs = opt_key->as_string();
    key = std::string(hs->view());
    hs->release();

    // Pop map — pop_map pushes back on type mismatch
    auto* m = pop_map(ctx);
    if (!m) {
        // Reconstruct key and restore stack: ( <non-map pushed back> key val )
        ctx.data_stack().push(Value::from(HeapString::create(key)));
        ctx.data_stack().push(*opt_val);
        return false;
    }

    m->set(key, *opt_val);  // map takes ownership of val's ref
    ctx.data_stack().push(Value::from(m));  // re-push same map
    return true;
}

// map-get ( map key -- val )
bool prim_map_get(ExecutionContext& ctx) {

    // Pop key (string)
    std::string key;
    auto opt_key = ctx.data_stack().pop();
    if (!opt_key) return false;
    if (opt_key->type != Value::Type::String || !opt_key->as_ptr) {
        ctx.data_stack().push(*opt_key);
        return false;
    }
    auto* hs = opt_key->as_string();
    key = std::string(hs->view());
    hs->release();

    // Pop map — pop_map pushes back on type mismatch
    auto* m = pop_map(ctx);
    if (!m) {
        // Reconstruct key and restore stack: ( <non-map pushed back> key )
        ctx.data_stack().push(Value::from(HeapString::create(key)));
        return false;
    }

    Value val;
    if (!m->get(key, val)) {
        // Key not found — restore stack
        ctx.data_stack().push(Value::from(m));
        ctx.data_stack().push(Value::from(HeapString::create(key)));
        return false;
    }

    m->release();  // consume the map
    ctx.data_stack().push(val);  // val has been addref'd by get()
    return true;
}

// map-remove ( map key -- map )
bool prim_map_remove(ExecutionContext& ctx) {

    // Pop key (string)
    std::string key;
    auto opt_key = ctx.data_stack().pop();
    if (!opt_key) return false;
    if (opt_key->type != Value::Type::String || !opt_key->as_ptr) {
        ctx.data_stack().push(*opt_key);
        return false;
    }
    auto* hs = opt_key->as_string();
    key = std::string(hs->view());
    hs->release();

    // Pop map — pop_map pushes back on type mismatch
    auto* m = pop_map(ctx);
    if (!m) {
        // Reconstruct key and restore stack: ( <non-map pushed back> key )
        ctx.data_stack().push(Value::from(HeapString::create(key)));
        return false;
    }

    if (!m->remove(key)) {
        // Key not found — restore stack
        ctx.data_stack().push(Value::from(m));
        ctx.data_stack().push(Value::from(HeapString::create(key)));
        return false;
    }

    ctx.data_stack().push(Value::from(m));
    return true;
}

// map-length ( map -- n )
bool prim_map_length(ExecutionContext& ctx) {
    auto* m = pop_map(ctx);
    if (!m) return false;
    int64_t len = static_cast<int64_t>(m->size());
    m->release();  // consume the map
    ctx.data_stack().push(Value(len));
    return true;
}

// map-keys ( map -- arr )
bool prim_map_keys(ExecutionContext& ctx) {
    auto* m = pop_map(ctx);
    if (!m) return false;

    auto* arr = new HeapArray();
    for (const auto& k : m->keys()) {
        arr->push_back(Value::from(HeapString::create(k)));
    }

    m->release();  // consume the map
    ctx.data_stack().push(Value::from(arr));
    return true;
}

// map-values ( map -- arr )
bool prim_map_values(ExecutionContext& ctx) {
    auto* m = pop_map(ctx);
    if (!m) return false;

    auto vals = m->values();  // each value is addref'd
    auto* arr = new HeapArray();
    for (auto& v : vals) {
        arr->push_back(v);  // array takes ownership of the addref'd ref
    }

    m->release();  // consume the map
    ctx.data_stack().push(Value::from(arr));
    return true;
}

// map-has? ( map key -- flag )
bool prim_map_has(ExecutionContext& ctx) {

    // Pop key (string)
    std::string key;
    auto opt_key = ctx.data_stack().pop();
    if (!opt_key) return false;
    if (opt_key->type != Value::Type::String || !opt_key->as_ptr) {
        ctx.data_stack().push(*opt_key);
        return false;
    }
    auto* hs = opt_key->as_string();
    key = std::string(hs->view());
    hs->release();

    // Pop map — pop_map pushes back on type mismatch
    auto* m = pop_map(ctx);
    if (!m) {
        // Reconstruct key and restore stack: ( <non-map pushed back> key )
        ctx.data_stack().push(Value::from(HeapString::create(key)));
        return false;
    }

    bool flag = m->has(key);
    m->release();  // consume the map
    ctx.data_stack().push(Value(flag));
    return true;
}

void register_map_primitives(Dictionary& dict) {
    using TS = TypeSignature;
    using T = TS::Type;


    dict.register_word("map-new", make_primitive("map-new", prim_map_new,
        {}, {T::Unknown}));
    dict.register_word("map-set", make_primitive("map-set", prim_map_set,
        {T::Unknown, T::String, T::Unknown}, {T::Unknown}));
    dict.register_word("map-get", make_primitive("map-get", prim_map_get,
        {T::Unknown, T::String}, {T::Unknown}));
    dict.register_word("map-remove", make_primitive("map-remove", prim_map_remove,
        {T::Unknown, T::String}, {T::Unknown}));
    dict.register_word("map-length", make_primitive("map-length", prim_map_length,
        {T::Unknown}, {T::Integer}));
    dict.register_word("map-keys", make_primitive("map-keys", prim_map_keys,
        {T::Unknown}, {T::Array}));
    dict.register_word("map-values", make_primitive("map-values", prim_map_values,
        {T::Unknown}, {T::Array}));
    dict.register_word("map-has?", make_primitive("map-has?", prim_map_has,
        {T::Unknown, T::String}, {T::Integer}));
}

} // namespace etil::core
