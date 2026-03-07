#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include <cstddef>
#include <optional>
#include <vector>

namespace etil::core {

// Forward declaration — Value is defined in execution_context.hpp.
// This header is included *by* execution_context.hpp after Value is defined.
struct Value;

/// Simple vector-backed stack for single-threaded ExecutionContext use.
///
/// Replaces LockFreeStack<Value> — every ExecutionContext is accessed by
/// only one thread at a time (MCP sessions are mutex-serialized, REPL is
/// single-threaded), so lock-free guarantees were never exercised.
///
/// Benefits over LockFreeStack:
///   - No heap allocation per push (amortised vector growth)
///   - O(1) indexed access (no drain-and-restore to read)
///   - No deferred reclamation overhead
class ValueStack {
public:
    void push(Value v) {
        data_.push_back(std::move(v));
    }

    std::optional<Value> pop() {
        if (data_.empty()) return std::nullopt;
        Value v = std::move(data_.back());
        data_.pop_back();
        return v;
    }

    std::optional<Value> top() const {
        if (data_.empty()) return std::nullopt;
        return data_.back();
    }

    bool empty() const { return data_.empty(); }

    size_t size() const { return data_.size(); }

    void clear() { data_.clear(); }

    /// Indexed access: 0 = bottom, size()-1 = top.
    const Value& operator[](size_t i) const { return data_[i]; }
    Value& operator[](size_t i) { return data_[i]; }

    /// Iterators (bottom to top).
    auto begin() const { return data_.begin(); }
    auto end() const { return data_.end(); }

private:
    std::vector<Value> data_;
};

} // namespace etil::core
