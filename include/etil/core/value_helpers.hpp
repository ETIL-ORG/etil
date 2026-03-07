#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include "etil/core/execution_context.hpp"

namespace etil::core {

/// Pop a HeapString* from the data stack. Returns nullptr on type mismatch or underflow.
/// Caller takes ownership of the returned pointer (no addref needed).
inline HeapString* pop_string(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return nullptr;
    if (opt->type != Value::Type::String || !opt->as_ptr) return nullptr;
    return opt->as_string();
}

/// Pop a HeapArray* from the data stack. Returns nullptr on type mismatch or underflow.
inline HeapArray* pop_array(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return nullptr;
    if (opt->type != Value::Type::Array || !opt->as_ptr) return nullptr;
    return opt->as_array();
}

/// Pop a HeapByteArray* from the data stack. Returns nullptr on type mismatch or underflow.
inline HeapByteArray* pop_byte_array(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return nullptr;
    if (opt->type != Value::Type::ByteArray || !opt->as_ptr) return nullptr;
    return opt->as_byte_array();
}

/// Pop a HeapMap* from the data stack. Returns nullptr on type mismatch or underflow.
inline HeapMap* pop_map(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return nullptr;
    if (opt->type != Value::Type::Map || !opt->as_ptr) return nullptr;
    return opt->as_map();
}

/// Pop a HeapJson* from the data stack. Returns nullptr on type mismatch or underflow.
inline HeapJson* pop_json(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return nullptr;
    if (opt->type != Value::Type::Json || !opt->as_ptr) return nullptr;
    return opt->as_json();
}

} // namespace etil::core
