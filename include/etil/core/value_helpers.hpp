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

/// Pop a typed heap value from the data stack. Returns nullptr on type mismatch
/// or underflow. On type mismatch, the value is pushed back (stack unchanged).
/// Caller takes ownership of the returned pointer (no addref needed).
template <typename HeapType, Value::Type TypeTag>
inline HeapType* pop_heap_value(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return nullptr;
    if (opt->type != TypeTag || !opt->as_ptr) {
        ctx.data_stack().push(*opt);
        return nullptr;
    }
    return static_cast<HeapType*>(opt->as_ptr);
}

inline HeapString*    pop_string(ExecutionContext& ctx)     { return pop_heap_value<HeapString,    Value::Type::String>(ctx); }
inline HeapArray*     pop_array(ExecutionContext& ctx)      { return pop_heap_value<HeapArray,     Value::Type::Array>(ctx); }
inline HeapByteArray* pop_byte_array(ExecutionContext& ctx) { return pop_heap_value<HeapByteArray, Value::Type::ByteArray>(ctx); }
inline HeapMap*       pop_map(ExecutionContext& ctx)        { return pop_heap_value<HeapMap,       Value::Type::Map>(ctx); }
inline HeapJson*      pop_json(ExecutionContext& ctx)       { return pop_heap_value<HeapJson,      Value::Type::Json>(ctx); }
inline HeapMatrix*    pop_matrix(ExecutionContext& ctx)     { return pop_heap_value<HeapMatrix,    Value::Type::Matrix>(ctx); }

} // namespace etil::core
