#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/heap_object.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/value_helpers.hpp"

namespace etil::core {

class HeapArray;
class WordImpl;

/// Heap-allocated Observable node.
///
/// Each node represents one step in a lazy pipeline. Nodes form a linked list
/// from terminal back to source. Execution is triggered by terminal operators
/// (subscribe, reduce, to-array, count) which walk the chain recursively.
///
/// Per-node state (state_) provides closure-like data binding: each node owns
/// its own mutable accumulator/context, independent of other pipelines.
class HeapObservable final : public HeapObject {
public:
    enum class Kind {
        // Creation
        FromArray, Of, Empty, Range,
        // Transform
        Map, MapWith, Filter, FilterWith,
        // Accumulate
        Scan, Reduce,
        // Limiting
        Take, Skip, Distinct,
        // Combination
        Merge, Concat, Zip,
    };

    Kind obs_kind() const { return obs_kind_; }
    HeapObservable* source() const { return source_; }
    HeapObservable* source_b() const { return source_b_; }
    WordImpl* operator_xt() const { return operator_xt_; }
    const Value& state() const { return state_; }
    int64_t param() const { return param_; }
    HeapArray* source_array() const { return source_array_; }

    // --- Factory methods (each returns a new node with refcount 1) ---

    // Creation
    static HeapObservable* from_array(HeapArray* arr);
    static HeapObservable* of(Value val);
    static HeapObservable* empty();
    static HeapObservable* range(int64_t start, int64_t end);

    // Transform
    static HeapObservable* map(HeapObservable* source, WordImpl* xt);
    static HeapObservable* map_with(HeapObservable* source, WordImpl* xt, Value ctx);
    static HeapObservable* filter(HeapObservable* source, WordImpl* xt);
    static HeapObservable* filter_with(HeapObservable* source, WordImpl* xt, Value ctx);

    // Accumulate
    static HeapObservable* scan(HeapObservable* source, WordImpl* xt, Value init);

    // Limiting
    static HeapObservable* take(HeapObservable* source, int64_t n);
    static HeapObservable* skip(HeapObservable* source, int64_t n);
    static HeapObservable* distinct(HeapObservable* source);

    // Combination
    static HeapObservable* merge(HeapObservable* a, HeapObservable* b, int64_t max_concurrent);
    static HeapObservable* concat(HeapObservable* a, HeapObservable* b);
    static HeapObservable* zip(HeapObservable* a, HeapObservable* b);

    ~HeapObservable() override;

    /// Return a human-readable name for this node's kind.
    const char* kind_name() const;

private:
    explicit HeapObservable(Kind k);

    Kind obs_kind_;
    HeapObservable* source_ = nullptr;
    HeapObservable* source_b_ = nullptr;
    WordImpl* operator_xt_ = nullptr;
    Value state_ = {};
    int64_t param_ = 0;
    HeapArray* source_array_ = nullptr;
};

/// Pop a HeapObservable* from the data stack.
/// On type mismatch, the value is pushed back (stack unchanged).
inline HeapObservable* pop_observable(ExecutionContext& ctx) {
    return pop_heap_value<HeapObservable, Value::Type::Observable>(ctx);
}

} // namespace etil::core
