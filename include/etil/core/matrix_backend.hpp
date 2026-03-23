#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// Backend interface for BLAS/LAPACKE-dependent matrix operations.
///
/// All functions operate on raw double* pointers in column-major layout
/// (matching HeapMatrix and LAPACK conventions). Callers handle all
/// HeapMatrix allocation and stack management.
///
/// Two implementations exist:
///   - matrix_backend_blas.cpp  (native build: OpenBLAS/LAPACKE)
///   - matrix_backend_eigen.cpp (WASM build: Eigen header-only)

namespace etil::core::backend {

/// Matrix multiply: C = A × B (column-major).
/// A is M×K, B is K×N, C is M×N (all pre-allocated).
void mat_multiply(const double* A, int lda,
                  const double* B, int ldb,
                  double* C, int ldc,
                  int M, int N, int K);

/// Solve linear system A·x = b via LU factorization.
/// A_work (n×n) is scratch space (pre-filled with copy of A, overwritten).
/// x (n×nrhs) contains b on input, solution on output.
/// Returns true on success, false if A is singular.
bool mat_solve(double* A_work, double* x, int n, int nrhs);

/// Invert a square matrix in-place.
/// inv (n×n) contains A on input, A^-1 on output.
/// Returns true on success, false if A is singular.
bool mat_inverse(double* inv, int n);

/// Compute determinant of a square matrix.
/// LU_work (n×n) is scratch space (pre-filled with copy of A, overwritten).
/// det receives the determinant value.
/// Returns true on success, false on factorization failure.
bool mat_determinant(double* LU_work, int n, double& det);

/// Eigendecomposition of a symmetric matrix.
/// evec (n×n) contains A on input, eigenvectors (columns) on output.
/// w (array of size n) receives eigenvalues in ascending order.
/// Returns true on success.
bool mat_eigen_symmetric(double* evec, int n, double* w);

/// Eigendecomposition of a general (non-symmetric) matrix.
/// A_work (n×n) is scratch space (pre-filled with copy of A, overwritten).
/// wr, wi (arrays of size n) receive real and imaginary parts of eigenvalues.
/// vr (n×n) receives right eigenvectors (columns).
/// Returns true on success.
bool mat_eigen_general(double* A_work, int n, double* wr, double* wi, double* vr);

/// Singular Value Decomposition (economy/thin).
/// A_work (m×n) is scratch space (pre-filled with copy of A, overwritten).
/// U (m×min(m,n)) receives left singular vectors.
/// S (array of size min(m,n)) receives singular values.
/// Vt (min(m,n)×n) receives right singular vectors transposed.
/// Returns true on success.
bool mat_svd(double* A_work, int m, int n,
             double* U, double* S, double* Vt);

/// Least squares solve: minimize ||A·x - b||.
/// A_work (m×n) is scratch space (pre-filled with copy of A, overwritten).
/// x (ldb×nrhs) contains b in first m rows on input, solution in first n rows on output.
/// ldb must be max(m, n).
/// Returns true on success.
bool mat_lstsq(double* A_work, int m, int n, double* x, int ldb, int nrhs);

} // namespace etil::core::backend
