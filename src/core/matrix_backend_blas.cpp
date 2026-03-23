// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// BLAS/LAPACKE backend for native builds (OpenBLAS).

#include "etil/core/matrix_backend.hpp"

#include <cblas.h>
#include <lapacke.h>

#include <algorithm>
#include <vector>

namespace etil::core::backend {

void mat_multiply(const double* A, int lda,
                  const double* B, int ldb,
                  double* C, int ldc,
                  int M, int N, int K) {
    cblas_dgemm(CblasColMajor, CblasNoTrans, CblasNoTrans,
                M, N, K,
                1.0, A, lda,
                B, ldb,
                0.0, C, ldc);
}

bool mat_solve(double* A_work, double* x, int n, int nrhs) {
    std::vector<lapack_int> ipiv(n);
    lapack_int info = LAPACKE_dgesv(LAPACK_COL_MAJOR, n, nrhs,
                                     A_work, n, ipiv.data(),
                                     x, n);
    return info == 0;
}

bool mat_inverse(double* inv, int n) {
    std::vector<lapack_int> ipiv(n);
    lapack_int info = LAPACKE_dgetrf(LAPACK_COL_MAJOR, n, n, inv, n, ipiv.data());
    if (info != 0) return false;
    info = LAPACKE_dgetri(LAPACK_COL_MAJOR, n, inv, n, ipiv.data());
    return info == 0;
}

bool mat_determinant(double* LU_work, int n, double& det) {
    std::vector<lapack_int> ipiv(n);
    lapack_int info = LAPACKE_dgetrf(LAPACK_COL_MAJOR, n, n, LU_work, n, ipiv.data());
    if (info != 0) {
        det = 0.0;
        return false;
    }
    det = 1.0;
    for (int i = 0; i < n; ++i) {
        det *= LU_work[i + i * n];  // LU[i,i] in column-major
        if (ipiv[i] != i + 1) det = -det;  // row swap changes sign
    }
    return true;
}

bool mat_eigen_symmetric(double* evec, int n, double* w) {
    lapack_int info = LAPACKE_dsyev(LAPACK_COL_MAJOR, 'V', 'U', n,
                                     evec, n, w);
    return info == 0;
}

bool mat_eigen_general(double* A_work, int n, double* wr, double* wi, double* vr) {
    lapack_int info = LAPACKE_dgeev(LAPACK_COL_MAJOR, 'N', 'V', n,
                                     A_work, n,
                                     wr, wi,
                                     nullptr, 1,  // no left eigenvectors
                                     vr, n);
    return info == 0;
}

bool mat_svd(double* A_work, int m, int n,
             double* U, double* S, double* Vt) {
    int minmn = std::min(m, n);
    std::vector<double> superb(minmn > 1 ? minmn - 1 : 1);
    lapack_int info = LAPACKE_dgesvd(LAPACK_COL_MAJOR, 'S', 'S', m, n,
                                      A_work, m,
                                      S,
                                      U, m,
                                      Vt, minmn,
                                      superb.data());
    return info == 0;
}

bool mat_lstsq(double* A_work, int m, int n, double* x, int ldb, int nrhs) {
    lapack_int info = LAPACKE_dgels(LAPACK_COL_MAJOR, 'N', m, n, nrhs,
                                     A_work, m,
                                     x, ldb);
    return info == 0;
}

} // namespace etil::core::backend
