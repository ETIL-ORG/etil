#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include "etil/core/heap_object.hpp"

#include <cstddef>
#include <cstring>
#include <new>
#include <string_view>

namespace etil::core {

/// Immutable, reference-counted UTF-8 string.
///
/// Uses flexible array member pattern: a single allocation holds both the
/// HeapString object and the string data (plus null terminator).
/// Immutable after creation.
class HeapString final : public HeapObject {
public:
    /// Factory: allocate HeapString + string data in a single allocation.
    /// Returned object has refcount=1.
    static HeapString* create(std::string_view sv) {
        size_t alloc_size = sizeof(HeapString) + sv.size() + 1;
        void* mem = ::operator new(alloc_size);
        auto* hs = new (mem) HeapString(sv.size());
        std::memcpy(hs->data_, sv.data(), sv.size());
        hs->data_[sv.size()] = '\0';
        return hs;
    }

    /// Factory: create a tainted HeapString (for data from external sources).
    static HeapString* create_tainted(std::string_view sv) {
        auto* hs = create(sv);
        hs->set_tainted(true);
        return hs;
    }

    const char* c_str() const { return data_; }
    std::string_view view() const { return {data_, length_}; }
    size_t length() const { return length_; }

    ~HeapString() override = default;

    // Custom delete to match placement new
    void operator delete(void* ptr) { ::operator delete(ptr); }

private:
    explicit HeapString(size_t len)
        : HeapObject(Kind::String), length_(len) {}

    size_t length_;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    char data_[];  // flexible array member (intentional, single allocation)
#pragma GCC diagnostic pop
};

} // namespace etil::core
