// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/heap_primitives.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_byte_array.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/value_helpers.hpp"
#include "etil/core/word_impl.hpp"

#include <string>

namespace etil::core {

/// Maximum byte array size (prevents DoS via huge allocations).
constexpr int64_t MAX_BYTE_ARRAY_SIZE = 10 * 1024 * 1024;  // 10 MB

// bytes-new ( n -- bytes ) — create byte array of size n (zeroed)
bool prim_bytes_new(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    int64_t n = opt->as_int;
    if (n < 0) n = 0;
    if (n > MAX_BYTE_ARRAY_SIZE) {
        ctx.err() << "Error: bytes-new size exceeds " << MAX_BYTE_ARRAY_SIZE << " byte limit\n";
        return false;
    }
    auto* ba = new HeapByteArray(static_cast<size_t>(n));
    ctx.data_stack().push(Value::from(ba));
    return true;
}

// bytes-get ( bytes idx -- n ) — read byte at index (0-255), release bytes ref
bool prim_bytes_get(ExecutionContext& ctx) {
    auto opt_idx = ctx.data_stack().pop();
    if (!opt_idx) return false;
    auto* ba = pop_byte_array(ctx);
    if (!ba) {
        ctx.data_stack().push(*opt_idx);
        return false;
    }
    uint8_t val;
    if (!ba->get(static_cast<size_t>(opt_idx->as_int), val)) {
        ctx.data_stack().push(Value::from(ba));
        ctx.data_stack().push(*opt_idx);
        return false;
    }
    ba->release();
    ctx.data_stack().push(Value(static_cast<int64_t>(val)));
    return true;
}

// bytes-set ( bytes idx val -- bytes ) — write byte at index
bool prim_bytes_set(ExecutionContext& ctx) {
    auto opt_val = ctx.data_stack().pop();
    if (!opt_val) return false;
    auto opt_idx = ctx.data_stack().pop();
    if (!opt_idx) {
        ctx.data_stack().push(*opt_val);
        return false;
    }
    auto* ba = pop_byte_array(ctx);
    if (!ba) {
        ctx.data_stack().push(*opt_idx);
        ctx.data_stack().push(*opt_val);
        return false;
    }
    uint8_t byte_val = static_cast<uint8_t>(opt_val->as_int & 0xFF);
    if (!ba->set(static_cast<size_t>(opt_idx->as_int), byte_val)) {
        ctx.data_stack().push(Value::from(ba));
        ctx.data_stack().push(*opt_idx);
        ctx.data_stack().push(*opt_val);
        return false;
    }
    ctx.data_stack().push(Value::from(ba));
    return true;
}

// bytes-length ( bytes -- bytes n )
bool prim_bytes_length(ExecutionContext& ctx) {
    auto* ba = pop_byte_array(ctx);
    if (!ba) return false;
    int64_t len = static_cast<int64_t>(ba->length());
    ctx.data_stack().push(Value::from(ba));
    ctx.data_stack().push(Value(len));
    return true;
}

// bytes-resize ( bytes n -- bytes )
bool prim_bytes_resize(ExecutionContext& ctx) {
    auto opt_n = ctx.data_stack().pop();
    if (!opt_n) return false;
    auto* ba = pop_byte_array(ctx);
    if (!ba) {
        ctx.data_stack().push(*opt_n);
        return false;
    }
    int64_t n = opt_n->as_int;
    if (n < 0) n = 0;
    ba->resize(static_cast<size_t>(n));
    ctx.data_stack().push(Value::from(ba));
    return true;
}

// bytes->string ( bytes -- str ) — convert to HeapString (UTF-8 interpretation)
bool prim_bytes_to_string(ExecutionContext& ctx) {
    auto* ba = pop_byte_array(ctx);
    if (!ba) return false;
    bool tainted = ba->is_tainted();
    std::string_view sv(reinterpret_cast<const char*>(ba->data()), ba->length());
    auto* hs = HeapString::create(sv);
    if (tainted) hs->set_tainted(true);
    ba->release();
    ctx.data_stack().push(Value::from(hs));
    return true;
}

// string->bytes ( str -- bytes ) — convert HeapString to byte array
bool prim_string_to_bytes(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::String || !opt->as_ptr) return false;
    auto* hs = opt->as_string();
    bool tainted = hs->is_tainted();
    auto* ba = new HeapByteArray(hs->length());
    std::memcpy(ba->data(), hs->c_str(), hs->length());
    if (tainted) ba->set_tainted(true);
    hs->release();
    ctx.data_stack().push(Value::from(ba));
    return true;
}

void register_byte_primitives(Dictionary& dict) {
    using TS = TypeSignature;
    using T = TS::Type;


    dict.register_word("bytes-new", make_primitive("bytes-new", prim_bytes_new,
        {T::Integer}, {T::Unknown}));
    dict.register_word("bytes-get", make_primitive("bytes-get", prim_bytes_get,
        {T::Unknown, T::Integer}, {T::Integer}));
    dict.register_word("bytes-set", make_primitive("bytes-set", prim_bytes_set,
        {T::Unknown, T::Integer, T::Integer}, {T::Unknown}));
    dict.register_word("bytes-length", make_primitive("bytes-length", prim_bytes_length,
        {T::Unknown}, {T::Unknown, T::Integer}));
    dict.register_word("bytes-resize", make_primitive("bytes-resize", prim_bytes_resize,
        {T::Unknown, T::Integer}, {T::Unknown}));
    dict.register_word("bytes->string", make_primitive("bytes->string", prim_bytes_to_string,
        {T::Unknown}, {T::String}));
    dict.register_word("string->bytes", make_primitive("string->bytes", prim_string_to_bytes,
        {T::String}, {T::Unknown}));
}

} // namespace etil::core
