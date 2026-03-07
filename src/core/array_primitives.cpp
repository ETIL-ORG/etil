// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/heap_primitives.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/value_helpers.hpp"
#include "etil/core/word_impl.hpp"

#include <string>

namespace etil::core {

// array-new ( -- arr )
bool prim_array_new(ExecutionContext& ctx) {
    auto* arr = new HeapArray();
    ctx.data_stack().push(Value::from(arr));
    return true;
}

// array-push ( arr val -- arr ) — append element, transfers val's ref to array
bool prim_array_push(ExecutionContext& ctx) {
    auto opt_val = ctx.data_stack().pop();
    if (!opt_val) return false;
    auto* arr = pop_array(ctx);
    if (!arr) {
        ctx.data_stack().push(*opt_val);
        return false;
    }
    arr->push_back(*opt_val);  // array takes ownership of val's ref
    ctx.data_stack().push(Value::from(arr));  // re-push same array (same ref)
    return true;
}

// array-pop ( arr -- arr val ) — remove+return last element
bool prim_array_pop(ExecutionContext& ctx) {
    auto* arr = pop_array(ctx);
    if (!arr) return false;
    Value val;
    if (!arr->pop_back(val)) {
        ctx.data_stack().push(Value::from(arr));
        return false;
    }
    ctx.data_stack().push(Value::from(arr));
    ctx.data_stack().push(val);  // val's ref transferred from array to caller
    return true;
}

// array-get ( arr idx -- val ) — bounds-checked index, addref returned val, release arr
bool prim_array_get(ExecutionContext& ctx) {
    auto opt_idx = ctx.data_stack().pop();
    if (!opt_idx) return false;
    auto* arr = pop_array(ctx);
    if (!arr) {
        ctx.data_stack().push(*opt_idx);
        return false;
    }
    Value val;
    if (!arr->get(static_cast<size_t>(opt_idx->as_int), val)) {
        ctx.data_stack().push(Value::from(arr));
        ctx.data_stack().push(*opt_idx);
        return false;
    }
    arr->release();
    ctx.data_stack().push(val);  // val has been addref'd by get()
    return true;
}

// array-set ( arr idx val -- arr ) — bounds-checked store
bool prim_array_set(ExecutionContext& ctx) {
    auto opt_val = ctx.data_stack().pop();
    if (!opt_val) return false;
    auto opt_idx = ctx.data_stack().pop();
    if (!opt_idx) {
        ctx.data_stack().push(*opt_val);
        return false;
    }
    auto* arr = pop_array(ctx);
    if (!arr) {
        ctx.data_stack().push(*opt_idx);
        ctx.data_stack().push(*opt_val);
        return false;
    }
    if (!arr->set(static_cast<size_t>(opt_idx->as_int), *opt_val)) {
        // Out of bounds — restore stack
        ctx.data_stack().push(Value::from(arr));
        ctx.data_stack().push(*opt_idx);
        ctx.data_stack().push(*opt_val);
        return false;
    }
    // set() took ownership of val's ref (and released old)
    ctx.data_stack().push(Value::from(arr));
    return true;
}

// array-length ( arr -- arr n )
bool prim_array_length(ExecutionContext& ctx) {
    auto* arr = pop_array(ctx);
    if (!arr) return false;
    int64_t len = static_cast<int64_t>(arr->length());
    ctx.data_stack().push(Value::from(arr));
    ctx.data_stack().push(Value(len));
    return true;
}

// array-shift ( arr -- arr val ) — remove+return first element
bool prim_array_shift(ExecutionContext& ctx) {
    auto* arr = pop_array(ctx);
    if (!arr) return false;
    Value val;
    if (!arr->shift(val)) {
        ctx.data_stack().push(Value::from(arr));
        return false;
    }
    ctx.data_stack().push(Value::from(arr));
    ctx.data_stack().push(val);
    return true;
}

// array-unshift ( arr val -- arr ) — prepend element
bool prim_array_unshift(ExecutionContext& ctx) {
    auto opt_val = ctx.data_stack().pop();
    if (!opt_val) return false;
    auto* arr = pop_array(ctx);
    if (!arr) {
        ctx.data_stack().push(*opt_val);
        return false;
    }
    arr->unshift(*opt_val);
    ctx.data_stack().push(Value::from(arr));
    return true;
}

// array-compact ( arr -- arr ) — shrink-to-fit
bool prim_array_compact(ExecutionContext& ctx) {
    auto* arr = pop_array(ctx);
    if (!arr) return false;
    arr->compact();
    ctx.data_stack().push(Value::from(arr));
    return true;
}

// array-reverse ( arr -- arr ) — reverse element order in-place
bool prim_array_reverse(ExecutionContext& ctx) {
    auto* arr = pop_array(ctx);
    if (!arr) return false;
    arr->reverse();
    ctx.data_stack().push(Value::from(arr));
    return true;
}

void register_array_primitives(Dictionary& dict) {
    using TS = TypeSignature;
    using T = TS::Type;

    auto make_word = [](const char* name, WordImpl::FunctionPtr fn,
                        std::vector<T> inputs, std::vector<T> outputs) {
        return make_primitive(name, fn, std::move(inputs), std::move(outputs));
    };

    dict.register_word("array-new", make_word("prim_array_new", prim_array_new,
        {}, {T::Array}));
    dict.register_word("array-push", make_word("prim_array_push", prim_array_push,
        {T::Array, T::Unknown}, {T::Array}));
    dict.register_word("array-pop", make_word("prim_array_pop", prim_array_pop,
        {T::Array}, {T::Array, T::Unknown}));
    dict.register_word("array-get", make_word("prim_array_get", prim_array_get,
        {T::Array, T::Integer}, {T::Unknown}));
    dict.register_word("array-set", make_word("prim_array_set", prim_array_set,
        {T::Array, T::Integer, T::Unknown}, {T::Array}));
    dict.register_word("array-length", make_word("prim_array_length", prim_array_length,
        {T::Array}, {T::Array, T::Integer}));
    dict.register_word("array-shift", make_word("prim_array_shift", prim_array_shift,
        {T::Array}, {T::Array, T::Unknown}));
    dict.register_word("array-unshift", make_word("prim_array_unshift", prim_array_unshift,
        {T::Array, T::Unknown}, {T::Array}));
    dict.register_word("array-compact", make_word("prim_array_compact", prim_array_compact,
        {T::Array}, {T::Array}));
    dict.register_word("array-reverse", make_word("prim_array_reverse", prim_array_reverse,
        {T::Array}, {T::Array}));
}

// --- ssplit and sjoin implementations (depend on HeapArray) ---

// ssplit ( str delim -- array ) — split into HeapArray of HeapStrings
bool prim_ssplit(ExecutionContext& ctx) {
    auto opt_delim = ctx.data_stack().pop();
    if (!opt_delim) return false;
    if (opt_delim->type != Value::Type::String || !opt_delim->as_ptr) return false;
    auto* delim = opt_delim->as_string();

    auto opt_str = ctx.data_stack().pop();
    if (!opt_str) {
        ctx.data_stack().push(*opt_delim);
        return false;
    }
    if (opt_str->type != Value::Type::String || !opt_str->as_ptr) {
        ctx.data_stack().push(*opt_str);
        ctx.data_stack().push(*opt_delim);
        return false;
    }
    auto* str = opt_str->as_string();

    bool tainted = str->is_tainted();
    auto* arr = new HeapArray();
    auto sv = str->view();
    auto dv = delim->view();

    if (dv.empty()) {
        // Empty delimiter: each character becomes an element
        for (size_t i = 0; i < sv.size(); ++i) {
            auto* elem = HeapString::create(sv.substr(i, 1));
            if (tainted) elem->set_tainted(true);
            arr->push_back(Value::from(elem));
        }
    } else {
        size_t pos = 0;
        while (true) {
            size_t found = sv.find(dv, pos);
            if (found == std::string_view::npos) {
                auto* elem = HeapString::create(sv.substr(pos));
                if (tainted) elem->set_tainted(true);
                arr->push_back(Value::from(elem));
                break;
            }
            auto* elem = HeapString::create(sv.substr(pos, found - pos));
            if (tainted) elem->set_tainted(true);
            arr->push_back(Value::from(elem));
            pos = found + dv.size();
        }
    }

    str->release();
    delim->release();
    ctx.data_stack().push(Value::from(arr));
    return true;
}

// sjoin ( array delim -- str ) — join array of strings with delimiter
bool prim_sjoin(ExecutionContext& ctx) {
    auto opt_delim = ctx.data_stack().pop();
    if (!opt_delim) return false;
    if (opt_delim->type != Value::Type::String || !opt_delim->as_ptr) return false;
    auto* delim = opt_delim->as_string();

    auto opt_arr = ctx.data_stack().pop();
    if (!opt_arr) {
        ctx.data_stack().push(*opt_delim);
        return false;
    }
    if (opt_arr->type != Value::Type::Array || !opt_arr->as_ptr) {
        ctx.data_stack().push(*opt_arr);
        ctx.data_stack().push(*opt_delim);
        return false;
    }
    auto* arr = opt_arr->as_array();

    std::string result;
    bool any_tainted = false;
    auto dv = delim->view();
    for (size_t i = 0; i < arr->length(); ++i) {
        if (i > 0) result.append(dv);
        Value elem;
        if (arr->get(i, elem) && elem.type == Value::Type::String && elem.as_ptr) {
            auto* hs = elem.as_string();
            if (hs->is_tainted()) any_tainted = true;
            result.append(hs->view());
            hs->release();  // release the addref'd copy from get()
        }
    }

    arr->release();
    delim->release();
    auto* result_hs = HeapString::create(result);
    if (any_tainted) result_hs->set_tainted(true);
    ctx.data_stack().push(Value::from(result_hs));
    return true;
}

} // namespace etil::core
