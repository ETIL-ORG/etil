#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include "etil/core/heap_object.hpp"
#include "etil/core/execution_context.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace etil::core {

/// Hash map from string keys to arbitrary Values.
///
/// The destructor releases all heap-valued stored Values.
/// Keys are std::string (copied from HeapString views).
class HeapMap final : public HeapObject {
public:
    HeapMap() : HeapObject(Kind::Map) {}

    ~HeapMap() override {
        for (auto& [key, val] : entries_) {
            value_release(val);
        }
    }

    size_t size() const { return entries_.size(); }

    bool has(const std::string& key) const {
        return entries_.count(key) > 0;
    }

    /// Lookup by key. Returns false if not found.
    /// On success, addrefs the returned value (caller owns it).
    bool get(const std::string& key, Value& out) const {
        auto it = entries_.find(key);
        if (it == entries_.end()) return false;
        out = it->second;
        value_addref(out);
        return true;
    }

    /// Set key to val. Releases old value if key exists.
    /// Takes ownership of caller's ref on val.
    void set(const std::string& key, const Value& val) {
        auto it = entries_.find(key);
        if (it != entries_.end()) {
            value_release(it->second);
            it->second = val;
        } else {
            entries_.emplace(key, val);
        }
    }

    /// Remove key. Returns false if not found.
    /// Releases the stored value.
    bool remove(const std::string& key) {
        auto it = entries_.find(key);
        if (it == entries_.end()) return false;
        value_release(it->second);
        entries_.erase(it);
        return true;
    }

    /// Return all keys.
    std::vector<std::string> keys() const {
        std::vector<std::string> result;
        result.reserve(entries_.size());
        for (const auto& [key, val] : entries_) {
            result.push_back(key);
        }
        return result;
    }

    /// Return all values. Each value is addref'd (caller owns them).
    std::vector<Value> values() const {
        std::vector<Value> result;
        result.reserve(entries_.size());
        for (const auto& [key, val] : entries_) {
            Value copy = val;
            value_addref(copy);
            result.push_back(copy);
        }
        return result;
    }

    /// Read-only access to the underlying map (for serialization, BSON conversion, etc.)
    const std::unordered_map<std::string, Value>& entries() const { return entries_; }

private:
    std::unordered_map<std::string, Value> entries_;
};

} // namespace etil::core
