#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/heap_object.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace etil::core {

/// Dense matrix of doubles, column-major layout (LAPACK-native).
///
/// M[i,j] = data_[j * rows_ + i].
/// No per-element refcounting — stores raw doubles for zero-overhead
/// BLAS/LAPACK interop via data() pointer.
class HeapMatrix final : public HeapObject {
public:
    HeapMatrix(int64_t rows, int64_t cols)
        : HeapObject(Kind::Matrix), rows_(rows), cols_(cols),
          data_(static_cast<size_t>(rows * cols), 0.0) {}

    int64_t rows() const { return rows_; }
    int64_t cols() const { return cols_; }
    size_t  size() const { return data_.size(); }

    // Column-major access: M[i,j] = data_[j*rows_ + i]
    double  get(int64_t row, int64_t col) const { return data_[col * rows_ + row]; }
    void    set(int64_t row, int64_t col, double val) { data_[col * rows_ + row] = val; }

    double*       data()       { return data_.data(); }
    const double* data() const { return data_.data(); }

    // LAPACK leading dimension (column-major = rows)
    int lda() const { return static_cast<int>(rows_); }

private:
    int64_t rows_, cols_;
    std::vector<double> data_;  // column-major
};

} // namespace etil::core
