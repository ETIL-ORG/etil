# ETIL Observable Design — Implementation Plan

## Date: 2026-03-12

## Context

Implement RxJS-style Observables in ETIL using **Approach 3B**: the Observable itself carries per-node state as a heap object. Each operator in a pipeline is a `HeapObservable` node that bundles `(operator_xt, state, upstream)`, effectively building closures by hand. The Observable *is* the closure environment.

### Why Not the Alternatives

- **CREATE/DOES> (Approach 1)** — Data fields are per-word-definition, not per-invocation. All calls to a CREATE'd word share the same mutable `data_field_`. Recursive or concurrent pipelines corrupt each other's state. You'd need to CREATE a new dictionary word per subscription at runtime — fragile and leaks dictionary entries.
- **Stack passing (Approach 2)** — Works for single linear pipelines but breaks with combination operators (merge, zip) or any interleaving, because the data stack is a single shared resource with no per-subscription isolation.

### Sub-Option B: Observable-Level Context Slot

The `state_` field on each `HeapObservable` node stores a single `Value`. Operators that need extra context (beyond the emitted value) use `-with` variants that store the context in `state_` and push it before calling the Xt:

```
\ obs-map: Xt receives ( value -- value' )
\ obs-map-with: Xt receives ( context value -- value' )
array obs-from ' add-ten obs-map           \ no context needed
array obs-from ' + 10 obs-map-with         \ 10 stored in node, pushed before each call
```

Sub-option A (CREATE/DOES> for read-only curried words) already works today and requires no new code. Sub-option C (Map as context) is just B where the Value happens to be a HeapMap — a usage pattern, not a feature.

---

## Design

### HeapObservable — New Heap Type

A linked list of operator nodes. Each node knows its kind, upstream source(s), operator callback, per-node state, and numeric parameter.

```cpp
// include/etil/core/heap_observable.hpp

class HeapObservable : public HeapObject {
public:
    enum class Kind {
        // Creation
        FromArray,      // Emit each element of a HeapArray
        Of,             // Emit a single value
        Empty,          // Complete immediately
        Range,          // Emit integers from start to end
        Interval,       // Emit on timer (async, future phase)

        // Transform
        Map,            // ( value -- value' ) via operator_xt
        MapWith,        // ( state value -- value' ) via operator_xt + state
        Filter,         // ( value -- bool ) via operator_xt
        FilterWith,     // ( state value -- bool ) via operator_xt + state

        // Accumulate
        Scan,           // ( accum value -- accum' ) via operator_xt, state = accumulator
        Reduce,         // Like Scan but only emits final value

        // Limiting
        Take,           // Emit first N values (param = N)
        Skip,           // Skip first N values (param = N)
        Distinct,       // Suppress consecutive duplicates

        // Combination
        Merge,          // Interleave from two sources (param = max concurrency, 0 = unlimited)
        Concat,         // Sequential: source_a then source_b
        Zip,            // Pair-wise from two sources
    };

    Kind kind() const { return kind_; }
    HeapObservable* source() const { return source_; }
    HeapObservable* source_b() const { return source_b_; }
    WordImpl* operator_xt() const { return operator_xt_; }
    const Value& state() const { return state_; }
    int64_t param() const { return param_; }

    // Factory methods (each creates a new node with refcount 1)
    static HeapObservable* from_array(HeapArray* arr);
    static HeapObservable* of(Value val);
    static HeapObservable* empty();
    static HeapObservable* range(int64_t start, int64_t end);

    static HeapObservable* map(HeapObservable* source, WordImpl* xt);
    static HeapObservable* map_with(HeapObservable* source, WordImpl* xt, Value ctx);
    static HeapObservable* filter(HeapObservable* source, WordImpl* xt);
    static HeapObservable* filter_with(HeapObservable* source, WordImpl* xt, Value ctx);
    static HeapObservable* scan(HeapObservable* source, WordImpl* xt, Value init);
    static HeapObservable* reduce(HeapObservable* source, WordImpl* xt, Value init);
    static HeapObservable* take(HeapObservable* source, int64_t n);
    static HeapObservable* skip(HeapObservable* source, int64_t n);
    static HeapObservable* distinct(HeapObservable* source);

    static HeapObservable* merge(HeapObservable* a, HeapObservable* b, int64_t max_concurrent);
    static HeapObservable* concat(HeapObservable* a, HeapObservable* b);
    static HeapObservable* zip(HeapObservable* a, HeapObservable* b);

    ~HeapObservable();

private:
    HeapObservable(Kind kind);

    Kind kind_;
    HeapObservable* source_ = nullptr;       // upstream (null for creation operators)
    HeapObservable* source_b_ = nullptr;     // second upstream (merge/zip/concat)
    WordImpl* operator_xt_ = nullptr;        // transform/predicate/reducer callback
    Value state_ = {};                       // per-node state (accumulator, context, etc.)
    int64_t param_ = 0;                      // numeric parameter (take count, concurrency, etc.)

    // For FromArray source
    HeapArray* source_array_ = nullptr;
};
```

### Value Type Extension

```cpp
// In Value type enum
enum class Type {
    // ... existing types ...
    Observable,     // HeapObservable*
};

// In heap_object.hpp
enum class Kind {
    // ... existing kinds ...
    Observable,
};
```

### Lifecycle & Refcounting

Same pattern as all other heap types:

- Factory methods return a new object with refcount 1
- Each node addrefs its children (source, source_b, operator_xt, source_array, state)
- Destructor releases all children
- `value_addref` / `value_release` handle `Value::Type::Observable`
- Pushing to stack: no addref (transfers ownership)
- `dup`: addref
- `drop`: release
- Pipeline nodes form a tree (DAG-free by construction) — no reference cycles

```cpp
HeapObservable::~HeapObservable() {
    if (source_) source_->release();
    if (source_b_) source_b_->release();
    if (operator_xt_) operator_xt_->release();
    if (source_array_) source_array_->release();
    state_.release();
}
```

### Execution Model — Push-Based Emission

When `obs-subscribe` or `obs-reduce` is called (terminal operators), execution walks the chain recursively. The innermost source emits values, each value propagates through the chain.

```cpp
// Core execution function
// observer: the downstream node (or terminal) that receives values
// Returns: true = completed normally, false = error or cancelled
bool execute_observable(HeapObservable* obs, ExecutionContext& ctx,
                        std::function<bool(Value, ExecutionContext&)> observer);
```

For each `Kind`, the execution logic differs:

#### Creation operators (leaf nodes)

```cpp
case Kind::FromArray: {
    auto* arr = obs->source_array_;
    for (size_t i = 0; i < arr->length(); ++i) {
        if (!ctx.tick()) return false;
        Value v;
        if (!arr->get(i, v)) return false;
        if (!observer(v, ctx)) return false;  // downstream said stop
    }
    return true;  // completed
}

case Kind::Of: {
    Value v = obs->state_;
    value_addref(v);
    return observer(v, ctx);
}

case Kind::Range: {
    int64_t start = obs->state_.as_int;
    int64_t end = obs->param_;
    for (int64_t i = start; i < end; ++i) {
        if (!ctx.tick()) return false;
        if (!observer(Value(i), ctx)) return false;
    }
    return true;
}
```

#### Transform operators (chain nodes)

Each transform creates its own observer function and passes it upstream:

```cpp
case Kind::Map: {
    auto* xt = obs->operator_xt_;
    return execute_observable(obs->source_, ctx, [&](Value v, ExecutionContext& ctx) -> bool {
        ctx.data_stack().push(v);
        bool ok = execute_xt(xt, ctx);
        if (!ok) return false;
        auto result = ctx.data_stack().pop();
        if (!result) return false;
        return observer(*result, ctx);
    });
}

case Kind::MapWith: {
    auto* xt = obs->operator_xt_;
    Value context = obs->state_;
    return execute_observable(obs->source_, ctx, [&](Value v, ExecutionContext& ctx) -> bool {
        value_addref(context);
        ctx.data_stack().push(context);  // push context first
        ctx.data_stack().push(v);         // then value
        bool ok = execute_xt(xt, ctx);
        if (!ok) return false;
        auto result = ctx.data_stack().pop();
        if (!result) return false;
        return observer(*result, ctx);
    });
}

case Kind::Filter: {
    auto* xt = obs->operator_xt_;
    return execute_observable(obs->source_, ctx, [&](Value v, ExecutionContext& ctx) -> bool {
        value_addref(v);  // keep a ref — xt consumes one, we may need to forward
        ctx.data_stack().push(v);
        bool ok = execute_xt(xt, ctx);
        if (!ok) { value_release(v); return false; }
        auto pred = ctx.data_stack().pop();
        if (!pred) { value_release(v); return false; }
        if (pred->type == Value::Type::Boolean && pred->as_bool()) {
            return observer(v, ctx);  // pass through — transfers our ref
        }
        value_release(v);  // filtered out — release our ref
        return true;        // continue upstream
    });
}

case Kind::Scan: {
    auto* xt = obs->operator_xt_;
    Value accum = obs->state_;
    value_addref(accum);  // working copy
    bool result = execute_observable(obs->source_, ctx, [&](Value v, ExecutionContext& ctx) -> bool {
        ctx.data_stack().push(accum);   // push accumulator
        ctx.data_stack().push(v);       // push value
        bool ok = execute_xt(xt, ctx);
        if (!ok) return false;
        auto new_accum = ctx.data_stack().pop();
        if (!new_accum) return false;
        accum = *new_accum;             // update accumulator (takes ownership)
        value_addref(accum);            // ref for downstream
        return observer(accum, ctx);    // emit current accumulator
    });
    value_release(accum);  // release working copy
    return result;
}
```

#### Limiting operators

```cpp
case Kind::Take: {
    int64_t remaining = obs->param_;
    return execute_observable(obs->source_, ctx, [&](Value v, ExecutionContext& ctx) -> bool {
        if (remaining <= 0) {
            value_release(v);
            return false;  // signal upstream to stop
        }
        --remaining;
        return observer(v, ctx);
    });
}

case Kind::Skip: {
    int64_t to_skip = obs->param_;
    return execute_observable(obs->source_, ctx, [&](Value v, ExecutionContext& ctx) -> bool {
        if (to_skip > 0) {
            --to_skip;
            value_release(v);
            return true;   // continue but don't forward
        }
        return observer(v, ctx);
    });
}
```

#### Combination operators

```cpp
case Kind::Concat: {
    // Run source_a to completion, then source_b
    bool ok = execute_observable(obs->source_, ctx, observer);
    if (!ok) return false;
    return execute_observable(obs->source_b_, ctx, observer);
}

case Kind::Merge: {
    // Synchronous interleave: alternate emissions from a and b
    // max_concurrent stored in param_ (0 = unlimited, currently synchronous)
    // Phase 1: simple sequential merge (a then b)
    // Phase 2 (async): true interleaving with libuv event loop
    bool ok = execute_observable(obs->source_, ctx, observer);
    if (!ok) return false;
    return execute_observable(obs->source_b_, ctx, observer);
}

case Kind::Zip: {
    // Collect all values from both sources, pair them
    std::vector<Value> vals_a, vals_b;
    execute_observable(obs->source_, ctx, [&](Value v, ExecutionContext&) -> bool {
        vals_a.push_back(v);
        return true;
    });
    execute_observable(obs->source_b_, ctx, [&](Value v, ExecutionContext&) -> bool {
        vals_b.push_back(v);
        return true;
    });
    size_t n = std::min(vals_a.size(), vals_b.size());
    for (size_t i = 0; i < n; ++i) {
        if (!ctx.tick()) {
            // Release remaining values
            for (size_t j = i; j < vals_a.size(); ++j) value_release(vals_a[j]);
            for (size_t j = i; j < vals_b.size(); ++j) value_release(vals_b[j]);
            return false;
        }
        // Create a 2-element array [a, b]
        auto* pair = new HeapArray();
        pair->push_back(vals_a[i]);
        pair->push_back(vals_b[i]);
        if (!observer(Value::from(pair), ctx)) {
            for (size_t j = i + 1; j < vals_a.size(); ++j) value_release(vals_a[j]);
            for (size_t j = i + 1; j < vals_b.size(); ++j) value_release(vals_b[j]);
            return false;
        }
    }
    // Release excess values from the longer source
    for (size_t j = n; j < vals_a.size(); ++j) value_release(vals_a[j]);
    for (size_t j = n; j < vals_b.size(); ++j) value_release(vals_b[j]);
    return true;
}
```

### Observable `merge` Concurrency Parameter

`obs-merge` takes a third parameter: `( obs-a obs-b max-concurrent -- obs )`.

- `max-concurrent = 0` — unlimited concurrency (all upstream sources run freely)
- `max-concurrent = N` — at most N sources emitting at a time

In Phase 1 (synchronous), this parameter is stored but not enforced — synchronous merge is inherently sequential (source_a runs to completion, then source_b). The parameter becomes meaningful in Phase 2 (async) when multiple sources can emit concurrently via the libuv event loop.

---

## TIL Word Inventory

### Creation (4 words)

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-from` | `( array -- obs )` | Emit each element of an array |
| `obs-of` | `( value -- obs )` | Emit a single value |
| `obs-empty` | `( -- obs )` | Complete immediately (no emissions) |
| `obs-range` | `( start end -- obs )` | Emit integers [start, end) |

### Transform (4 words)

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-map` | `( obs xt -- obs' )` | Transform each value: `( value -- value' )` |
| `obs-map-with` | `( obs xt ctx -- obs' )` | Transform with context: `( ctx value -- value' )` |
| `obs-filter` | `( obs xt -- obs' )` | Keep values where xt returns true: `( value -- bool )` |
| `obs-filter-with` | `( obs xt ctx -- obs' )` | Filter with context: `( ctx value -- bool )` |

### Accumulate (2 words)

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-scan` | `( obs xt init -- obs' )` | Running accumulation, emits each step: `( accum value -- accum' )` |
| `obs-reduce` | `( obs xt init -- result )` | Fold to single value (terminal): `( accum value -- accum' )` |

### Limiting (3 words)

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-take` | `( obs n -- obs' )` | First N emissions |
| `obs-skip` | `( obs n -- obs' )` | Skip first N emissions |
| `obs-distinct` | `( obs -- obs' )` | Suppress consecutive duplicates |

### Combination (3 words)

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-merge` | `( obs-a obs-b max-concurrent -- obs )` | Interleave (0=unlimited concurrency) |
| `obs-concat` | `( obs-a obs-b -- obs )` | Sequential: a then b |
| `obs-zip` | `( obs-a obs-b -- obs )` | Pair-wise: emits 2-element arrays |

### Terminal (4 words)

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-subscribe` | `( obs xt -- )` | Execute xt for each value: `( value -- )` |
| `obs-reduce` | `( obs xt init -- result )` | Fold to single value (listed above) |
| `obs-to-array` | `( obs -- array )` | Collect all emissions into an array |
| `obs-count` | `( obs -- n )` | Count total emissions |

### Introspection (2 words)

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs?` | `( value -- bool )` | Type check: is TOS an Observable? |
| `obs-kind` | `( obs -- string )` | Return the kind name as a string |

**Total: 22 words**

---

## TIL Usage Examples

### Basic pipeline

```forth
\ Square all elements, keep evens, sum them
array-new 1 array-push 2 array-push 3 array-push 4 array-push 5 array-push
obs-from
  ' dup* obs-map             \ square each: 1 4 9 16 25
  ' even? obs-filter         \ keep evens: 4 16
  ' + 0 obs-reduce .         \ sum: 20
```

Where `dup*` and `even?` are user-defined:
```forth
: dup* dup * ;
: even? 2 mod 0 = ;
```

### Context-carrying transform

```forth
\ Add a constant offset to each value
array-new 10 array-push 20 array-push 30 array-push
obs-from
  ' + 5 obs-map-with    \ 5 stored in node, pushed before +: 15 25 35
  ' . obs-subscribe     \ prints: 15 25 35
```

### Running accumulation (scan)

```forth
\ Running sum
array-new 1 array-push 2 array-push 3 array-push 4 array-push
obs-from
  ' + 0 obs-scan         \ emits: 1 3 6 10
  ' . obs-subscribe
```

### Concat and merge

```forth
\ Concat: all of A then all of B
array-new 1 array-push 2 array-push obs-from
array-new 10 array-push 20 array-push obs-from
obs-concat
  ' . obs-subscribe     \ prints: 1 2 10 20

\ Merge (phase 1 = sequential, same as concat)
array-new 1 array-push 2 array-push obs-from
array-new 10 array-push 20 array-push obs-from
0 obs-merge              \ 0 = unlimited concurrency
  ' . obs-subscribe     \ prints: 1 2 10 20 (sequential in phase 1)
```

### Zip into pairs

```forth
array-new 1 array-push 2 array-push 3 array-push obs-from
array-new s" a" array-push s" b" array-push s" c" array-push obs-from
obs-zip
  ' . obs-subscribe     \ prints: <array:2> <array:2> <array:2>
                         \ (each is [int, string] pair)
```

### Collect to array

```forth
array-new 1 array-push 2 array-push 3 array-push 4 array-push 5 array-push
obs-from
  ' dup* obs-map
  3 obs-take
obs-to-array             \ array [1, 4, 9]
```

### Nested observables (recursion-safe)

```forth
: process-item ( n -- )
    dup 1 swap obs-range         \ inner observable: range [1, n)
    ' . obs-subscribe ;          \ prints 1..n-1

array-new 3 array-push 5 array-push obs-from
  ' process-item obs-subscribe   \ outer emits 3, 5
                                  \ inner prints 1 2 then 1 2 3 4
```

Each inner `obs-range` creates independent `HeapObservable` nodes — no shared state with the outer pipeline.

---

## Implementation Stages

### Stage 0: Prerequisites — Array Iteration Primitives

Before Observables, add basic functional operations on arrays. These are independently useful and validate the Xt-as-callback pattern.

**4 new primitives** (~80 lines in a new section of `array_primitives.cpp`):

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `array-each` | `( arr xt -- )` | Execute xt for each element |
| `array-map` | `( arr xt -- arr' )` | Transform elements into new array |
| `array-filter` | `( arr xt -- arr' )` | Keep matching elements |
| `array-reduce` | `( arr xt init -- result )` | Fold to single value |

Uses the same `execute_xt` pattern as `mat-apply`.

**Files modified:**
- `src/core/array_primitives.cpp` — 4 new functions + registration
- `data/help.til` — 4 new entries
- `tests/unit/test_array_primitives.cpp` — new tests

**Version bump:** patch

### Stage 1: Core Observable — Synchronous Pipeline

The foundation: HeapObservable type, creation operators, transform/filter/take/skip, and terminal operators.

**New files:**
- `include/etil/core/heap_observable.hpp` — HeapObservable class
- `src/core/observable_primitives.cpp` — primitives + execution engine

**Files modified:**
- `include/etil/core/heap_object.hpp` — add `Kind::Observable`
- `include/etil/core/execution_context.hpp` — add `Value::Type::Observable`, helpers
- `src/core/primitives.cpp` — add Observable cases to `.`, `.s`, `dump`, `format_value`, `pop_as_string`, etc.
- `src/core/json_primitives.cpp` — add Observable case to `tool_get_stack`
- `src/CMakeLists.txt` — add new source file
- `data/help.til` — help entries for all new words
- `tests/unit/test_observable.cpp` — comprehensive tests

**Primitives in this stage (16 words):**
- Creation: `obs-from`, `obs-of`, `obs-empty`, `obs-range`
- Transform: `obs-map`, `obs-map-with`, `obs-filter`, `obs-filter-with`
- Accumulate: `obs-scan`, `obs-reduce`
- Limiting: `obs-take`, `obs-skip`, `obs-distinct`
- Terminal: `obs-subscribe`, `obs-to-array`, `obs-count`
- Introspection: `obs?`, `obs-kind`

**Version bump:** minor (new type)

### Stage 2: Combination Operators

Add merge, concat, zip once the core is stable.

**Primitives (3 words):**
- `obs-merge ( obs-a obs-b max-concurrent -- obs )`
- `obs-concat ( obs-a obs-b -- obs )`
- `obs-zip ( obs-a obs-b -- obs )`

**Files modified:**
- `src/core/observable_primitives.cpp` — 3 new primitives + execution cases
- `include/etil/core/heap_observable.hpp` — add combination factory methods
- `data/help.til` — 3 new entries
- `tests/unit/test_observable.cpp` — combination tests

**Version bump:** patch

### Stage 3: Async Observables (Future)

Add timer-based and event-driven sources using libuv. `obs-merge` concurrency parameter becomes meaningful.

**Primitives (planned):**
- `obs-interval ( ms -- obs )` — emit on timer
- `obs-timeout ( obs ms -- obs )` — cancel if no emission within duration

**Requires:**
- libuv timer integration in execution engine
- Cooperative await loop (same pattern as `http-get` and async file I/O)

**Not in initial implementation scope.**

---

## Type System Integration Checklist

Every new `Value::Type` requires updates in these locations:

- [ ] `format_value()` — debug string representation
- [ ] `prim_dot()` — `.` word output
- [ ] `prim_dot_s()` — `.s` word output
- [ ] `dump_value()` — `dump` word
- [ ] `pop_as_string()` — string coercion for `s+`
- [ ] `tool_get_stack()` — MCP JSON stack representation
- [ ] `is_heap_value()` — heap type check
- [ ] `make_heap_value()` — if applicable
- [ ] `value_addref()` / `value_release()` — refcounting
- [ ] `arith_binary()` — reject in arithmetic
- [ ] `compare_binary()` — comparison behavior
- [ ] `value_to_bson()` — MongoDB serialization (if applicable)
- [ ] `see` / `format_instruction()` — if new opcodes added

---

## Key Design Decisions

1. **All operators are lazy.** Pipeline construction (`obs-from`, `obs-map`, etc.) only builds the node chain. No values are emitted until a terminal operator (`obs-subscribe`, `obs-reduce`, `obs-to-array`, `obs-count`) is called.

2. **Execution respects `tick()`.** Every emission checks `ctx.tick()` for instruction budget, timeout, and cancellation. Long pipelines can be cancelled mid-execution.

3. **`obs-reduce` is both an operator and a terminal.** It builds a Reduce node *and* immediately executes the pipeline, returning the final value. This matches the common `obs-from ... obs-reduce .` pattern.

4. **`obs-zip` emits arrays.** Each emission is a 2-element `HeapArray` containing the paired values from the two sources. This avoids inventing a "tuple" type.

5. **`obs-merge` max-concurrent is stored but not enforced in Phase 1.** Synchronous merge is inherently sequential. The parameter exists from day one so the word signature doesn't change when async is added.

6. **No `obs-flat-map`.** RxJS's `flatMap`/`mergeMap` requires async concurrency. Deferred to Phase 3.

7. **`execute_xt` helper shared with `mat-apply`.** Extract the existing Xt dispatch logic from `prim_mat_apply` into a reusable `execute_xt(WordImpl*, ExecutionContext&)` function.

8. **Observer `false` return means "stop".** Downstream can signal upstream to stop emitting (e.g., `obs-take` returning `false` after N values). This is not an error — it's completion.

9. **Observable displayed as `<obs:Kind>`.** The `.` word prints `<obs:from-array>`, `<obs:map>`, etc. The `.s` word shows the same.

10. **No observable mutation.** Once created, an observable node is immutable. Operator methods return new nodes. The original observable can be shared (via `dup`) across multiple subscriptions.
