#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include "etil/core/heap_object.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace etil::core {

/// Reference-counted raw byte buffer for I/O and binary data.
class HeapByteArray final : public HeapObject {
public:
    explicit HeapByteArray(size_t size)
        : HeapObject(Kind::ByteArray), bytes_(size, 0) {}

    ~HeapByteArray() override = default;

    size_t length() const { return bytes_.size(); }

    /// Bounds-checked get. Returns false if out of range.
    bool get(size_t idx, uint8_t& out) const {
        if (idx >= bytes_.size()) return false;
        out = bytes_[idx];
        return true;
    }

    /// Bounds-checked set. Returns false if out of range.
    bool set(size_t idx, uint8_t val) {
        if (idx >= bytes_.size()) return false;
        bytes_[idx] = val;
        return true;
    }

    /// Resize the buffer. New bytes are zero-initialized.
    void resize(size_t new_size) {
        bytes_.resize(new_size, 0);
    }

    /// Direct access to the underlying byte data.
    const uint8_t* data() const { return bytes_.data(); }
    uint8_t* data() { return bytes_.data(); }

    const std::vector<uint8_t>& bytes() const { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
};

} // namespace etil::core
