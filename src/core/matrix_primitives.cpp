// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef ETIL_LINALG_ENABLED

#include "etil/core/heap_primitives.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_matrix.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/value_helpers.hpp"
#include "etil/core/word_impl.hpp"

#include <cblas.h>
#include <lapacke.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <string>

namespace etil::core {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Pop an integer from the data stack. Returns false on underflow/type mismatch.
static bool pop_int(ExecutionContext& ctx, int64_t& out) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type == Value::Type::Integer) {
        out = opt->as_int;
        return true;
    }
    if (opt->type == Value::Type::Float) {
        out = static_cast<int64_t>(opt->as_float);
        return true;
    }
    ctx.data_stack().push(*opt);
    return false;
}

/// Pop a double from the data stack.
static bool pop_double(ExecutionContext& ctx, double& out) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type == Value::Type::Float) {
        out = opt->as_float;
        return true;
    }
    if (opt->type == Value::Type::Integer) {
        out = static_cast<double>(opt->as_int);
        return true;
    }
    ctx.data_stack().push(*opt);
    return false;
}

/// Extract a numeric value from a Value as double. Returns false if not numeric.
static bool value_as_double(const Value& v, double& out) {
    if (v.type == Value::Type::Float) { out = v.as_float; return true; }
    if (v.type == Value::Type::Integer) { out = static_cast<double>(v.as_int); return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

// mat-new ( rows cols -- mat )
bool prim_mat_new(ExecutionContext& ctx) {
    int64_t cols, rows;
    if (!pop_int(ctx, cols)) return false;
    if (!pop_int(ctx, rows)) {
        ctx.data_stack().push(Value(cols));
        return false;
    }
    if (rows <= 0 || cols <= 0) {
        ctx.err() << "Error: mat-new requires positive dimensions\n";
        return false;
    }
    auto* mat = new HeapMatrix(rows, cols);
    ctx.data_stack().push(Value::from(mat));
    return true;
}

// mat-eye ( n -- mat )
bool prim_mat_eye(ExecutionContext& ctx) {
    int64_t n;
    if (!pop_int(ctx, n)) return false;
    if (n <= 0) {
        ctx.err() << "Error: mat-eye requires positive size\n";
        return false;
    }
    auto* mat = new HeapMatrix(n, n);
    for (int64_t i = 0; i < n; ++i) {
        mat->set(i, i, 1.0);
    }
    ctx.data_stack().push(Value::from(mat));
    return true;
}

// mat-from-array ( array rows cols -- mat )
bool prim_mat_from_array(ExecutionContext& ctx) {
    int64_t cols, rows;
    if (!pop_int(ctx, cols)) return false;
    if (!pop_int(ctx, rows)) {
        ctx.data_stack().push(Value(cols));
        return false;
    }
    auto* arr = pop_array(ctx);
    if (!arr) {
        ctx.data_stack().push(Value(rows));
        ctx.data_stack().push(Value(cols));
        return false;
    }
    if (rows <= 0 || cols <= 0) {
        arr->release();
        ctx.err() << "Error: mat-from-array requires positive dimensions\n";
        return false;
    }
    size_t expected = static_cast<size_t>(rows * cols);
    if (arr->length() < expected) {
        ctx.err() << "Error: array has " << arr->length() << " elements, need " << expected << "\n";
        arr->release();
        return false;
    }
    auto* mat = new HeapMatrix(rows, cols);
    // Array is in row-major order: arr[r*cols + c] → mat[r,c] (column-major)
    for (int64_t r = 0; r < rows; ++r) {
        for (int64_t c = 0; c < cols; ++c) {
            Value elem;
            double val = 0.0;
            if (arr->get(static_cast<size_t>(r * cols + c), elem)) {
                value_as_double(elem, val);
                value_release(elem);
            }
            mat->set(r, c, val);
        }
    }
    arr->release();
    ctx.data_stack().push(Value::from(mat));
    return true;
}

// mat-diag ( array -- mat )
bool prim_mat_diag(ExecutionContext& ctx) {
    auto* arr = pop_array(ctx);
    if (!arr) return false;
    int64_t n = static_cast<int64_t>(arr->length());
    if (n == 0) {
        arr->release();
        ctx.err() << "Error: mat-diag requires non-empty array\n";
        return false;
    }
    auto* mat = new HeapMatrix(n, n);
    for (int64_t i = 0; i < n; ++i) {
        Value elem;
        double val = 0.0;
        if (arr->get(static_cast<size_t>(i), elem)) {
            value_as_double(elem, val);
            value_release(elem);
        }
        mat->set(i, i, val);
    }
    arr->release();
    ctx.data_stack().push(Value::from(mat));
    return true;
}

// mat-rand ( rows cols -- mat )
bool prim_mat_rand(ExecutionContext& ctx) {
    int64_t cols, rows;
    if (!pop_int(ctx, cols)) return false;
    if (!pop_int(ctx, rows)) {
        ctx.data_stack().push(Value(cols));
        return false;
    }
    if (rows <= 0 || cols <= 0) {
        ctx.err() << "Error: mat-rand requires positive dimensions\n";
        return false;
    }
    auto* mat = new HeapMatrix(rows, cols);
    static thread_local std::mt19937_64 rng(std::random_device{}());
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (size_t i = 0; i < mat->size(); ++i) {
        mat->data()[i] = dist(rng);
    }
    ctx.data_stack().push(Value::from(mat));
    return true;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

// mat-get ( mat row col -- val )
bool prim_mat_get(ExecutionContext& ctx) {
    int64_t col, row;
    if (!pop_int(ctx, col)) return false;
    if (!pop_int(ctx, row)) {
        ctx.data_stack().push(Value(col));
        return false;
    }
    auto* mat = pop_matrix(ctx);
    if (!mat) {
        ctx.data_stack().push(Value(row));
        ctx.data_stack().push(Value(col));
        return false;
    }
    if (row < 0 || row >= mat->rows() || col < 0 || col >= mat->cols()) {
        ctx.err() << "Error: mat-get index out of bounds (" << row << "," << col
                  << ") for " << mat->rows() << "x" << mat->cols() << " matrix\n";
        mat->release();
        return false;
    }
    double val = mat->get(row, col);
    mat->release();
    ctx.data_stack().push(Value(val));
    return true;
}

// mat-set ( mat row col val -- mat )
bool prim_mat_set(ExecutionContext& ctx) {
    double val;
    if (!pop_double(ctx, val)) return false;
    int64_t col, row;
    if (!pop_int(ctx, col)) {
        ctx.data_stack().push(Value(val));
        return false;
    }
    if (!pop_int(ctx, row)) {
        ctx.data_stack().push(Value(col));
        ctx.data_stack().push(Value(val));
        return false;
    }
    auto* mat = pop_matrix(ctx);
    if (!mat) {
        ctx.data_stack().push(Value(row));
        ctx.data_stack().push(Value(col));
        ctx.data_stack().push(Value(val));
        return false;
    }
    if (row < 0 || row >= mat->rows() || col < 0 || col >= mat->cols()) {
        ctx.err() << "Error: mat-set index out of bounds (" << row << "," << col
                  << ") for " << mat->rows() << "x" << mat->cols() << " matrix\n";
        mat->release();
        return false;
    }
    mat->set(row, col, val);
    ctx.data_stack().push(Value::from(mat));
    return true;
}

// mat-rows ( mat -- n )
bool prim_mat_rows(ExecutionContext& ctx) {
    auto* mat = pop_matrix(ctx);
    if (!mat) return false;
    int64_t r = mat->rows();
    mat->release();
    ctx.data_stack().push(Value(r));
    return true;
}

// mat-cols ( mat -- n )
bool prim_mat_cols(ExecutionContext& ctx) {
    auto* mat = pop_matrix(ctx);
    if (!mat) return false;
    int64_t c = mat->cols();
    mat->release();
    ctx.data_stack().push(Value(c));
    return true;
}

// mat-row ( mat i -- array )
bool prim_mat_row(ExecutionContext& ctx) {
    int64_t i;
    if (!pop_int(ctx, i)) return false;
    auto* mat = pop_matrix(ctx);
    if (!mat) {
        ctx.data_stack().push(Value(i));
        return false;
    }
    if (i < 0 || i >= mat->rows()) {
        ctx.err() << "Error: mat-row index " << i << " out of bounds\n";
        mat->release();
        return false;
    }
    auto* arr = new HeapArray();
    for (int64_t c = 0; c < mat->cols(); ++c) {
        arr->push_back(Value(mat->get(i, c)));
    }
    mat->release();
    ctx.data_stack().push(Value::from(arr));
    return true;
}

// mat-col ( mat j -- array )
bool prim_mat_col(ExecutionContext& ctx) {
    int64_t j;
    if (!pop_int(ctx, j)) return false;
    auto* mat = pop_matrix(ctx);
    if (!mat) {
        ctx.data_stack().push(Value(j));
        return false;
    }
    if (j < 0 || j >= mat->cols()) {
        ctx.err() << "Error: mat-col index " << j << " out of bounds\n";
        mat->release();
        return false;
    }
    auto* arr = new HeapArray();
    for (int64_t r = 0; r < mat->rows(); ++r) {
        arr->push_back(Value(mat->get(r, j)));
    }
    mat->release();
    ctx.data_stack().push(Value::from(arr));
    return true;
}

// ---------------------------------------------------------------------------
// Arithmetic
// ---------------------------------------------------------------------------

// mat* ( mat1 mat2 -- mat )  — DGEMM
bool prim_mat_mul(ExecutionContext& ctx) {
    auto* B = pop_matrix(ctx);
    if (!B) return false;
    auto* A = pop_matrix(ctx);
    if (!A) {
        ctx.data_stack().push(Value::from(B));
        return false;
    }
    if (A->cols() != B->rows()) {
        ctx.err() << "Error: mat* dimension mismatch: "
                  << A->rows() << "x" << A->cols() << " * "
                  << B->rows() << "x" << B->cols() << "\n";
        A->release();
        B->release();
        return false;
    }
    int64_t M = A->rows(), N = B->cols(), K = A->cols();
    auto* C = new HeapMatrix(M, N);
    cblas_dgemm(CblasColMajor, CblasNoTrans, CblasNoTrans,
                static_cast<int>(M), static_cast<int>(N), static_cast<int>(K),
                1.0, A->data(), A->lda(),
                B->data(), B->lda(),
                0.0, C->data(), C->lda());
    A->release();
    B->release();
    ctx.data_stack().push(Value::from(C));
    return true;
}

// mat+ ( mat1 mat2 -- mat )
bool prim_mat_add(ExecutionContext& ctx) {
    auto* B = pop_matrix(ctx);
    if (!B) return false;
    auto* A = pop_matrix(ctx);
    if (!A) {
        ctx.data_stack().push(Value::from(B));
        return false;
    }
    if (A->rows() != B->rows() || A->cols() != B->cols()) {
        ctx.err() << "Error: mat+ dimension mismatch\n";
        A->release();
        B->release();
        return false;
    }
    auto* C = new HeapMatrix(A->rows(), A->cols());
    for (size_t i = 0; i < C->size(); ++i) {
        C->data()[i] = A->data()[i] + B->data()[i];
    }
    A->release();
    B->release();
    ctx.data_stack().push(Value::from(C));
    return true;
}

// mat- ( mat1 mat2 -- mat )
bool prim_mat_sub(ExecutionContext& ctx) {
    auto* B = pop_matrix(ctx);
    if (!B) return false;
    auto* A = pop_matrix(ctx);
    if (!A) {
        ctx.data_stack().push(Value::from(B));
        return false;
    }
    if (A->rows() != B->rows() || A->cols() != B->cols()) {
        ctx.err() << "Error: mat- dimension mismatch\n";
        A->release();
        B->release();
        return false;
    }
    auto* C = new HeapMatrix(A->rows(), A->cols());
    for (size_t i = 0; i < C->size(); ++i) {
        C->data()[i] = A->data()[i] - B->data()[i];
    }
    A->release();
    B->release();
    ctx.data_stack().push(Value::from(C));
    return true;
}

// mat-scale ( mat scalar -- mat )
bool prim_mat_scale(ExecutionContext& ctx) {
    double scalar;
    if (!pop_double(ctx, scalar)) return false;
    auto* A = pop_matrix(ctx);
    if (!A) {
        ctx.data_stack().push(Value(scalar));
        return false;
    }
    auto* C = new HeapMatrix(A->rows(), A->cols());
    for (size_t i = 0; i < C->size(); ++i) {
        C->data()[i] = A->data()[i] * scalar;
    }
    A->release();
    ctx.data_stack().push(Value::from(C));
    return true;
}

// mat-transpose ( mat -- mat )
bool prim_mat_transpose(ExecutionContext& ctx) {
    auto* A = pop_matrix(ctx);
    if (!A) return false;
    auto* T = new HeapMatrix(A->cols(), A->rows());
    for (int64_t r = 0; r < A->rows(); ++r) {
        for (int64_t c = 0; c < A->cols(); ++c) {
            T->set(c, r, A->get(r, c));
        }
    }
    A->release();
    ctx.data_stack().push(Value::from(T));
    return true;
}

// ---------------------------------------------------------------------------
// Solvers
// ---------------------------------------------------------------------------

// mat-solve ( A b -- x flag )  — DGESV
bool prim_mat_solve(ExecutionContext& ctx) {
    auto* b = pop_matrix(ctx);
    if (!b) return false;
    auto* A = pop_matrix(ctx);
    if (!A) {
        ctx.data_stack().push(Value::from(b));
        return false;
    }
    if (A->rows() != A->cols()) {
        ctx.err() << "Error: mat-solve requires square A\n";
        A->release();
        b->release();
        return false;
    }
    if (A->rows() != b->rows()) {
        ctx.err() << "Error: mat-solve dimension mismatch\n";
        A->release();
        b->release();
        return false;
    }
    int n = static_cast<int>(A->rows());
    int nrhs = static_cast<int>(b->cols());

    // DGESV overwrites A and b, so copy them
    auto* Acopy = new HeapMatrix(n, n);
    std::copy(A->data(), A->data() + A->size(), Acopy->data());
    auto* x = new HeapMatrix(n, nrhs);
    std::copy(b->data(), b->data() + b->size(), x->data());

    std::vector<lapack_int> ipiv(n);
    lapack_int info = LAPACKE_dgesv(LAPACK_COL_MAJOR, n, nrhs,
                                     Acopy->data(), n, ipiv.data(),
                                     x->data(), n);
    A->release();
    b->release();
    delete Acopy;

    ctx.data_stack().push(Value::from(x));
    ctx.data_stack().push(Value(info == 0));
    return true;
}

// mat-inv ( mat -- inv flag )  — DGETRF + DGETRI
bool prim_mat_inv(ExecutionContext& ctx) {
    auto* A = pop_matrix(ctx);
    if (!A) return false;
    if (A->rows() != A->cols()) {
        ctx.err() << "Error: mat-inv requires square matrix\n";
        A->release();
        return false;
    }
    int n = static_cast<int>(A->rows());
    auto* inv = new HeapMatrix(n, n);
    std::copy(A->data(), A->data() + A->size(), inv->data());
    A->release();

    std::vector<lapack_int> ipiv(n);
    lapack_int info = LAPACKE_dgetrf(LAPACK_COL_MAJOR, n, n, inv->data(), n, ipiv.data());
    if (info == 0) {
        info = LAPACKE_dgetri(LAPACK_COL_MAJOR, n, inv->data(), n, ipiv.data());
    }

    ctx.data_stack().push(Value::from(inv));
    ctx.data_stack().push(Value(info == 0));
    return true;
}

// mat-det ( mat -- det flag )  — determinant via LU
bool prim_mat_det(ExecutionContext& ctx) {
    auto* A = pop_matrix(ctx);
    if (!A) return false;
    if (A->rows() != A->cols()) {
        ctx.err() << "Error: mat-det requires square matrix\n";
        A->release();
        return false;
    }
    int n = static_cast<int>(A->rows());
    auto* LU = new HeapMatrix(n, n);
    std::copy(A->data(), A->data() + A->size(), LU->data());
    A->release();

    std::vector<lapack_int> ipiv(n);
    lapack_int info = LAPACKE_dgetrf(LAPACK_COL_MAJOR, n, n, LU->data(), n, ipiv.data());

    double det = 0.0;
    if (info == 0) {
        det = 1.0;
        for (int i = 0; i < n; ++i) {
            det *= LU->get(i, i);
            if (ipiv[i] != i + 1) det = -det;  // row swap changes sign
        }
    }
    delete LU;

    ctx.data_stack().push(Value(det));
    ctx.data_stack().push(Value(info == 0));
    return true;
}

// ---------------------------------------------------------------------------
// Decompositions
// ---------------------------------------------------------------------------

// Helper: check if matrix is symmetric
static bool is_symmetric(const HeapMatrix* mat) {
    if (mat->rows() != mat->cols()) return false;
    int64_t n = mat->rows();
    for (int64_t i = 0; i < n; ++i) {
        for (int64_t j = i + 1; j < n; ++j) {
            if (mat->get(i, j) != mat->get(j, i)) return false;
        }
    }
    return true;
}

// mat-eigen ( mat -- eigenvalues eigenvectors flag )
bool prim_mat_eigen(ExecutionContext& ctx) {
    auto* A = pop_matrix(ctx);
    if (!A) return false;
    if (A->rows() != A->cols()) {
        ctx.err() << "Error: mat-eigen requires square matrix\n";
        A->release();
        return false;
    }
    int n = static_cast<int>(A->rows());

    if (is_symmetric(A)) {
        // Symmetric: DSYEV returns real eigenvalues + orthonormal eigenvectors
        auto* evec = new HeapMatrix(n, n);
        std::copy(A->data(), A->data() + A->size(), evec->data());
        A->release();

        std::vector<double> w(n);
        lapack_int info = LAPACKE_dsyev(LAPACK_COL_MAJOR, 'V', 'U', n,
                                         evec->data(), n, w.data());

        // Pack eigenvalues into a column vector (n×1 matrix)
        auto* eval = new HeapMatrix(n, 1);
        for (int i = 0; i < n; ++i) eval->set(i, 0, w[i]);

        ctx.data_stack().push(Value::from(eval));
        ctx.data_stack().push(Value::from(evec));
        ctx.data_stack().push(Value(info == 0));
    } else {
        // General: DGEEV
        auto* Acopy = new HeapMatrix(n, n);
        std::copy(A->data(), A->data() + A->size(), Acopy->data());
        A->release();

        std::vector<double> wr(n), wi(n);
        auto* vr = new HeapMatrix(n, n);  // right eigenvectors

        lapack_int info = LAPACKE_dgeev(LAPACK_COL_MAJOR, 'N', 'V', n,
                                         Acopy->data(), n,
                                         wr.data(), wi.data(),
                                         nullptr, 1,  // no left eigenvectors
                                         vr->data(), n);
        delete Acopy;

        // Pack eigenvalues: real parts in column 0, imaginary in column 1
        auto* eval = new HeapMatrix(n, 2);
        for (int i = 0; i < n; ++i) {
            eval->set(i, 0, wr[i]);
            eval->set(i, 1, wi[i]);
        }

        ctx.data_stack().push(Value::from(eval));
        ctx.data_stack().push(Value::from(vr));
        ctx.data_stack().push(Value(info == 0));
    }
    return true;
}

// mat-svd ( mat -- U S Vt flag )
bool prim_mat_svd(ExecutionContext& ctx) {
    auto* A = pop_matrix(ctx);
    if (!A) return false;
    int m = static_cast<int>(A->rows());
    int n = static_cast<int>(A->cols());
    int minmn = std::min(m, n);

    auto* Acopy = new HeapMatrix(m, n);
    std::copy(A->data(), A->data() + A->size(), Acopy->data());
    A->release();

    auto* U = new HeapMatrix(m, minmn);
    auto* Vt = new HeapMatrix(minmn, n);
    std::vector<double> s(minmn);
    std::vector<double> superb(minmn - 1);

    lapack_int info = LAPACKE_dgesvd(LAPACK_COL_MAJOR, 'S', 'S', m, n,
                                      Acopy->data(), m,
                                      s.data(),
                                      U->data(), m,
                                      Vt->data(), minmn,
                                      superb.data());
    delete Acopy;

    // Pack singular values as column vector
    auto* S = new HeapMatrix(minmn, 1);
    for (int i = 0; i < minmn; ++i) S->set(i, 0, s[i]);

    ctx.data_stack().push(Value::from(U));
    ctx.data_stack().push(Value::from(S));
    ctx.data_stack().push(Value::from(Vt));
    ctx.data_stack().push(Value(info == 0));
    return true;
}

// mat-lstsq ( A b -- x flag )  — DGELS
bool prim_mat_lstsq(ExecutionContext& ctx) {
    auto* b = pop_matrix(ctx);
    if (!b) return false;
    auto* A = pop_matrix(ctx);
    if (!A) {
        ctx.data_stack().push(Value::from(b));
        return false;
    }
    if (A->rows() != b->rows()) {
        ctx.err() << "Error: mat-lstsq dimension mismatch\n";
        A->release();
        b->release();
        return false;
    }
    int m = static_cast<int>(A->rows());
    int n = static_cast<int>(A->cols());
    int nrhs = static_cast<int>(b->cols());

    // DGELS overwrites A and b; b must be max(m,n) × nrhs
    auto* Acopy = new HeapMatrix(m, n);
    std::copy(A->data(), A->data() + A->size(), Acopy->data());
    A->release();

    int ldb = std::max(m, n);
    auto* x = new HeapMatrix(ldb, nrhs);
    // Copy b into the larger x buffer
    for (int j = 0; j < nrhs; ++j) {
        for (int i = 0; i < m; ++i) {
            x->set(i, j, b->get(i, j));
        }
    }
    b->release();

    lapack_int info = LAPACKE_dgels(LAPACK_COL_MAJOR, 'N', m, n, nrhs,
                                     Acopy->data(), m,
                                     x->data(), ldb);
    delete Acopy;

    // Extract the n×nrhs solution from x (first n rows)
    auto* result = new HeapMatrix(n, nrhs);
    for (int j = 0; j < nrhs; ++j) {
        for (int i = 0; i < n; ++i) {
            result->set(i, j, x->get(i, j));
        }
    }
    delete x;

    ctx.data_stack().push(Value::from(result));
    ctx.data_stack().push(Value(info == 0));
    return true;
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

// mat-norm ( mat -- val )  — Frobenius norm
bool prim_mat_norm(ExecutionContext& ctx) {
    auto* mat = pop_matrix(ctx);
    if (!mat) return false;
    double sum = 0.0;
    for (size_t i = 0; i < mat->size(); ++i) {
        sum += mat->data()[i] * mat->data()[i];
    }
    mat->release();
    ctx.data_stack().push(Value(std::sqrt(sum)));
    return true;
}

// mat-trace ( mat -- val )
bool prim_mat_trace(ExecutionContext& ctx) {
    auto* mat = pop_matrix(ctx);
    if (!mat) return false;
    if (mat->rows() != mat->cols()) {
        ctx.err() << "Error: mat-trace requires square matrix\n";
        mat->release();
        return false;
    }
    double trace = 0.0;
    for (int64_t i = 0; i < mat->rows(); ++i) {
        trace += mat->get(i, i);
    }
    mat->release();
    ctx.data_stack().push(Value(trace));
    return true;
}

// mat. ( mat -- )  — pretty-print matrix
bool prim_mat_print(ExecutionContext& ctx) {
    auto* mat = pop_matrix(ctx);
    if (!mat) return false;
    ctx.out() << "<matrix " << mat->rows() << "x" << mat->cols() << ">\n";
    for (int64_t r = 0; r < mat->rows(); ++r) {
        ctx.out() << "  [";
        for (int64_t c = 0; c < mat->cols(); ++c) {
            if (c > 0) ctx.out() << ", ";
            ctx.out() << mat->get(r, c);
        }
        ctx.out() << "]\n";
    }
    mat->release();
    return true;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void register_matrix_primitives(Dictionary& dict) {
    using TS = TypeSignature;
    using T = TS::Type;

    auto make_word = [](const char* name, WordImpl::FunctionPtr fn,
                        std::vector<T> inputs, std::vector<T> outputs) {
        return make_primitive(name, fn, std::move(inputs), std::move(outputs));
    };

    // Constructors
    dict.register_word("mat-new", make_word("prim_mat_new", prim_mat_new,
        {T::Integer, T::Integer}, {T::Unknown}));
    dict.register_word("mat-eye", make_word("prim_mat_eye", prim_mat_eye,
        {T::Integer}, {T::Unknown}));
    dict.register_word("mat-from-array", make_word("prim_mat_from_array", prim_mat_from_array,
        {T::Array, T::Integer, T::Integer}, {T::Unknown}));
    dict.register_word("mat-diag", make_word("prim_mat_diag", prim_mat_diag,
        {T::Array}, {T::Unknown}));
    dict.register_word("mat-rand", make_word("prim_mat_rand", prim_mat_rand,
        {T::Integer, T::Integer}, {T::Unknown}));

    // Accessors
    dict.register_word("mat-get", make_word("prim_mat_get", prim_mat_get,
        {T::Unknown, T::Integer, T::Integer}, {T::Float}));
    dict.register_word("mat-set", make_word("prim_mat_set", prim_mat_set,
        {T::Unknown, T::Integer, T::Integer, T::Float}, {T::Unknown}));
    dict.register_word("mat-rows", make_word("prim_mat_rows", prim_mat_rows,
        {T::Unknown}, {T::Integer}));
    dict.register_word("mat-cols", make_word("prim_mat_cols", prim_mat_cols,
        {T::Unknown}, {T::Integer}));
    dict.register_word("mat-row", make_word("prim_mat_row", prim_mat_row,
        {T::Unknown, T::Integer}, {T::Array}));
    dict.register_word("mat-col", make_word("prim_mat_col", prim_mat_col,
        {T::Unknown, T::Integer}, {T::Array}));

    // Arithmetic
    dict.register_word("mat*", make_word("prim_mat_mul", prim_mat_mul,
        {T::Unknown, T::Unknown}, {T::Unknown}));
    dict.register_word("mat+", make_word("prim_mat_add", prim_mat_add,
        {T::Unknown, T::Unknown}, {T::Unknown}));
    dict.register_word("mat-", make_word("prim_mat_sub", prim_mat_sub,
        {T::Unknown, T::Unknown}, {T::Unknown}));
    dict.register_word("mat-scale", make_word("prim_mat_scale", prim_mat_scale,
        {T::Unknown, T::Float}, {T::Unknown}));
    dict.register_word("mat-transpose", make_word("prim_mat_transpose", prim_mat_transpose,
        {T::Unknown}, {T::Unknown}));

    // Solvers
    dict.register_word("mat-solve", make_word("prim_mat_solve", prim_mat_solve,
        {T::Unknown, T::Unknown}, {T::Unknown, T::Unknown}));
    dict.register_word("mat-inv", make_word("prim_mat_inv", prim_mat_inv,
        {T::Unknown}, {T::Unknown, T::Unknown}));
    dict.register_word("mat-det", make_word("prim_mat_det", prim_mat_det,
        {T::Unknown}, {T::Float, T::Unknown}));

    // Decompositions
    dict.register_word("mat-eigen", make_word("prim_mat_eigen", prim_mat_eigen,
        {T::Unknown}, {T::Unknown, T::Unknown, T::Unknown}));
    dict.register_word("mat-svd", make_word("prim_mat_svd", prim_mat_svd,
        {T::Unknown}, {T::Unknown, T::Unknown, T::Unknown, T::Unknown}));
    dict.register_word("mat-lstsq", make_word("prim_mat_lstsq", prim_mat_lstsq,
        {T::Unknown, T::Unknown}, {T::Unknown, T::Unknown}));

    // Utilities
    dict.register_word("mat-norm", make_word("prim_mat_norm", prim_mat_norm,
        {T::Unknown}, {T::Float}));
    dict.register_word("mat-trace", make_word("prim_mat_trace", prim_mat_trace,
        {T::Unknown}, {T::Float}));
    dict.register_word("mat.", make_word("prim_mat_print", prim_mat_print,
        {T::Unknown}, {}));
}

} // namespace etil::core

#endif // ETIL_LINALG_ENABLED
