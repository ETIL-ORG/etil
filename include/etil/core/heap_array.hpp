#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include "etil/core/heap_object.hpp"
#include "etil/core/execution_context.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace etil::core {

/// Dynamic, reference-counted array of Values.
///
/// The destructor releases all heap-valued elements.
/// Bounds-checked get/set. push_back/pop_back for stack-like usage.
class HeapArray final : public HeapObject {
public:
    HeapArray() : HeapObject(Kind::Array) {}

    ~HeapArray() override {
        for (auto& v : elements_) {
            value_release(v);
        }
    }

    size_t length() const { return elements_.size(); }

    /// Bounds-checked get. Returns false if out of range.
    /// On success, addref's the returned value (caller owns it).
    bool get(size_t idx, Value& out) const {
        if (idx >= elements_.size()) return false;
        out = elements_[idx];
        value_addref(out);
        return true;
    }

    /// Bounds-checked set. Releases old value, takes ownership of new value's ref.
    bool set(size_t idx, const Value& val) {
        if (idx >= elements_.size()) return false;
        value_release(elements_[idx]);
        elements_[idx] = val;
        return true;
    }

    /// Append element. Takes ownership of caller's ref.
    void push_back(const Value& val) {
        elements_.push_back(val);
    }

    /// Remove and return last element. Transfers ref to caller.
    /// Returns false if empty.
    bool pop_back(Value& out) {
        if (elements_.empty()) return false;
        out = elements_.back();
        elements_.pop_back();
        return true;
    }

    /// Remove and return first element. Transfers ref to caller.
    bool shift(Value& out) {
        if (elements_.empty()) return false;
        out = elements_.front();
        elements_.erase(elements_.begin());
        return true;
    }

    /// Prepend element. Takes ownership of caller's ref.
    void unshift(const Value& val) {
        elements_.insert(elements_.begin(), val);
    }

    /// Shrink internal storage to fit.
    void compact() {
        elements_.shrink_to_fit();
    }

    /// Reverse element order in-place.
    void reverse() {
        std::reverse(elements_.begin(), elements_.end());
    }

    /// Direct read access to elements.
    const std::vector<Value>& elements() const { return elements_; }

private:
    std::vector<Value> elements_;
};

} // namespace etil::core
