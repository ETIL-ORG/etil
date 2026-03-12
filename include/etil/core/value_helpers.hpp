#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include "etil/core/execution_context.hpp"
#include "etil/core/compiled_body.hpp"
#include "etil/core/word_impl.hpp"

namespace etil::core {

/// Execute an Xt (WordImpl*) on the given context.
/// The caller is responsible for pushing arguments and popping results.
/// Returns true on success, false on failure.
inline bool execute_xt(WordImpl* impl, ExecutionContext& ctx) {
    if (impl->native_code()) {
        return impl->native_code()(ctx);
    } else if (impl->bytecode()) {
        return execute_compiled(*impl->bytecode(), ctx);
    }
    return false;
}

/// Pop a HeapString* from the data stack. Returns nullptr on type mismatch or underflow.
/// On type mismatch, the value is pushed back onto the stack (stack unchanged).
/// Caller takes ownership of the returned pointer (no addref needed).
inline HeapString* pop_string(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return nullptr;
    if (opt->type != Value::Type::String || !opt->as_ptr) {
        ctx.data_stack().push(*opt);
        return nullptr;
    }
    return opt->as_string();
}

/// Pop a HeapArray* from the data stack. Returns nullptr on type mismatch or underflow.
/// On type mismatch, the value is pushed back onto the stack (stack unchanged).
inline HeapArray* pop_array(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return nullptr;
    if (opt->type != Value::Type::Array || !opt->as_ptr) {
        ctx.data_stack().push(*opt);
        return nullptr;
    }
    return opt->as_array();
}

/// Pop a HeapByteArray* from the data stack. Returns nullptr on type mismatch or underflow.
/// On type mismatch, the value is pushed back onto the stack (stack unchanged).
inline HeapByteArray* pop_byte_array(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return nullptr;
    if (opt->type != Value::Type::ByteArray || !opt->as_ptr) {
        ctx.data_stack().push(*opt);
        return nullptr;
    }
    return opt->as_byte_array();
}

/// Pop a HeapMap* from the data stack. Returns nullptr on type mismatch or underflow.
/// On type mismatch, the value is pushed back onto the stack (stack unchanged).
inline HeapMap* pop_map(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return nullptr;
    if (opt->type != Value::Type::Map || !opt->as_ptr) {
        ctx.data_stack().push(*opt);
        return nullptr;
    }
    return opt->as_map();
}

/// Pop a HeapJson* from the data stack. Returns nullptr on type mismatch or underflow.
/// On type mismatch, the value is pushed back onto the stack (stack unchanged).
inline HeapJson* pop_json(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return nullptr;
    if (opt->type != Value::Type::Json || !opt->as_ptr) {
        ctx.data_stack().push(*opt);
        return nullptr;
    }
    return opt->as_json();
}

/// Pop a HeapMatrix* from the data stack. Returns nullptr on type mismatch or underflow.
/// On type mismatch, the value is pushed back onto the stack (stack unchanged).
inline HeapMatrix* pop_matrix(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return nullptr;
    if (opt->type != Value::Type::Matrix || !opt->as_ptr) {
        ctx.data_stack().push(*opt);
        return nullptr;
    }
    return opt->as_matrix();
}

} // namespace etil::core
