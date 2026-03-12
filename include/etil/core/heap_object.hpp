#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include "etil/core/execution_context.hpp"
#include "etil/core/word_impl.hpp"

#include <atomic>
#include <cstdint>

namespace etil::core {

/// Base class for heap-allocated, reference-counted objects.
///
/// Thread-safe intrusive reference counting (same pattern as WordImpl).
/// Subclasses: HeapString, HeapArray, HeapByteArray.
///
/// Value remains POD — refcounts are managed explicitly in primitives
/// and interpreter code, not via Value copy/destructor.
class HeapObject {
public:
    enum class Kind { String, Array, ByteArray, Map, Json, Matrix, Observable };

    explicit HeapObject(Kind kind) : kind_(kind) {}
    virtual ~HeapObject() = default;

    Kind kind() const { return kind_; }

    void add_ref() { refcount_.fetch_add(1, std::memory_order_relaxed); }
    void release() {
        if (refcount_.fetch_sub(1, std::memory_order_release) == 1) {
            std::atomic_thread_fence(std::memory_order_acquire);
            delete this;
        }
    }

    uint64_t refcount() const { return refcount_.load(std::memory_order_relaxed); }

    bool is_tainted() const { return tainted_; }
    void set_tainted(bool t) { tainted_ = t; }

private:
    Kind kind_;
    bool tainted_{false};
    std::atomic<uint64_t> refcount_{1};
};

/// Create a Value holding a HeapObject pointer with the appropriate type tag.
inline Value make_heap_value(HeapObject* obj) {
    Value v;
    switch (obj->kind()) {
    case HeapObject::Kind::String:   v.type = Value::Type::String;    break;
    case HeapObject::Kind::Array:    v.type = Value::Type::Array;     break;
    case HeapObject::Kind::ByteArray: v.type = Value::Type::ByteArray; break;
    case HeapObject::Kind::Map:      v.type = Value::Type::Map;       break;
    case HeapObject::Kind::Json:     v.type = Value::Type::Json;      break;
    case HeapObject::Kind::Matrix:   v.type = Value::Type::Matrix;    break;
    case HeapObject::Kind::Observable: v.type = Value::Type::Observable; break;
    }
    v.as_ptr = static_cast<void*>(obj);
    return v;
}

/// Check if a Value holds a heap-allocated object.
inline bool is_heap_value(const Value& v) {
    return v.type == Value::Type::String ||
           v.type == Value::Type::Array ||
           v.type == Value::Type::ByteArray ||
           v.type == Value::Type::Map ||
           v.type == Value::Type::Json ||
           v.type == Value::Type::Matrix ||
           v.type == Value::Type::Observable;
}

/// Extract the HeapObject pointer from a Value. Caller must ensure is_heap_value().
inline HeapObject* as_heap_object(const Value& v) {
    return static_cast<HeapObject*>(v.as_ptr);
}

/// Add a reference if the value is heap-allocated or an Xt. No-op otherwise.
inline void value_addref(const Value& v) {
    if (is_heap_value(v) && v.as_ptr) {
        as_heap_object(v)->add_ref();
    } else if (v.type == Value::Type::Xt && v.as_ptr) {
        v.as_xt_impl()->add_ref();
    }
}

/// Release a reference if the value is heap-allocated or an Xt. No-op otherwise.
inline void value_release(const Value& v) {
    if (is_heap_value(v) && v.as_ptr) {
        as_heap_object(v)->release();
    } else if (v.type == Value::Type::Xt && v.as_ptr) {
        v.as_xt_impl()->release();
    }
}

// Deferred Value member definitions (depend on functions above)
inline Value Value::from(HeapObject* obj) { return make_heap_value(obj); }
inline Value Value::from_xt(WordImpl* impl) { return make_xt_value(impl); }
inline void Value::release() const { value_release(*this); }
inline void Value::addref() const { value_addref(*this); }

} // namespace etil::core
