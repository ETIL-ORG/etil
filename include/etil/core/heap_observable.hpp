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

        // Tier 1: Core Temporal
        Timer,          // delay in state_.as_int (us), period in param_ (us, 0=one-shot)
        Delay,          // delay in param_ (us)
        Timestamp,      // no extra state
        TimeInterval,   // no extra state (delta computed at execution time)

        // Tier 2: Rate-Limiting
        DebounceTime,   // quiet window in param_ (us)
        ThrottleTime,   // throttle window in param_ (us)
        SampleTime,     // sample period in param_ (us)
        Timeout,        // timeout limit in param_ (us)

        // Tier 3: Windowed + Additional
        BufferTime,     // window size in param_ (us)
        TakeUntilTime,  // duration in param_ (us)
        DelayEach,      // xt in operator_xt_
        AuditTime,      // window in param_ (us)
        RetryDelay,     // delay in state_.as_int (us), max retries in param_

        // AVO Phase 1: Buffer + Composition
        Buffer,         // count in param_
        BufferWhen,     // predicate xt in operator_xt_
        Window,         // window size in param_
        FlatMap,        // mapping xt in operator_xt_
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

    // Temporal — Creation
    static HeapObservable* timer(int64_t delay_us, int64_t period_us);

    // Temporal — Transform
    static HeapObservable* delay(HeapObservable* source, int64_t delay_us);
    static HeapObservable* timestamp(HeapObservable* source);
    static HeapObservable* time_interval(HeapObservable* source);
    static HeapObservable* delay_each(HeapObservable* source, WordImpl* xt);

    // Temporal — Rate-Limiting
    static HeapObservable* debounce_time(HeapObservable* source, int64_t quiet_us);
    static HeapObservable* throttle_time(HeapObservable* source, int64_t window_us);
    static HeapObservable* sample_time(HeapObservable* source, int64_t period_us);
    static HeapObservable* timeout(HeapObservable* source, int64_t limit_us);
    static HeapObservable* audit_time(HeapObservable* source, int64_t window_us);

    // Temporal — Windowed + Limiting
    static HeapObservable* buffer_time(HeapObservable* source, int64_t window_us);
    static HeapObservable* take_until_time(HeapObservable* source, int64_t duration_us);

    // Temporal — Error
    static HeapObservable* retry_delay(HeapObservable* source, int64_t delay_us, int64_t max_retries);

    // AVO Phase 1 — Buffer + Composition
    static HeapObservable* buffer(HeapObservable* source, int64_t count);
    static HeapObservable* buffer_when(HeapObservable* source, WordImpl* predicate_xt);
    static HeapObservable* window(HeapObservable* source, int64_t size);
    static HeapObservable* flat_map(HeapObservable* source, WordImpl* xt);

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
