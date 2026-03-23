// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// Eigen backend for WASM builds — replaces OpenBLAS/LAPACKE.
///
/// HeapMatrix uses column-major layout, which matches Eigen's default
/// (Eigen::MatrixXd = Eigen::Matrix<double, Dynamic, Dynamic, ColMajor>).
/// We use Eigen::Map<> for zero-copy wrapping of raw double* data.

#include "etil/core/matrix_backend.hpp"

#include <Eigen/Dense>

#include <algorithm>

namespace etil::core::backend {

using ColMajorMatrix = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic>;
using MapMatrix = Eigen::Map<ColMajorMatrix>;
using ConstMapMatrix = Eigen::Map<const ColMajorMatrix>;
using MapVector = Eigen::Map<Eigen::VectorXd>;

void mat_multiply(const double* A, int lda,
                  const double* B, int ldb,
                  double* C, int ldc,
                  int M, int N, int K) {
    ConstMapMatrix mapA(A, M, K);
    ConstMapMatrix mapB(B, K, N);
    MapMatrix mapC(C, M, N);
    mapC.noalias() = mapA * mapB;
}

bool mat_solve(double* A_work, double* x, int n, int nrhs) {
    MapMatrix mapA(A_work, n, n);
    MapMatrix mapX(x, n, nrhs);
    // Copy b before decomposition overwrites nothing — solve returns a temporary
    ColMajorMatrix b = mapX;
    auto qr = mapA.colPivHouseholderQr();
    if (!qr.isInvertible()) return false;
    mapX = qr.solve(b);
    return true;
}

bool mat_inverse(double* inv, int n) {
    MapMatrix mapInv(inv, n, n);
    ColMajorMatrix A = mapInv;  // copy before overwriting
    auto lu = A.partialPivLu();
    // Check for singular matrix via determinant (LU is always computed)
    if (std::abs(lu.determinant()) < 1e-15) return false;
    mapInv = lu.inverse();
    return true;
}

bool mat_determinant(double* LU_work, int n, double& det) {
    MapMatrix mapA(LU_work, n, n);
    det = mapA.determinant();
    return true;  // Eigen's determinant always succeeds
}

bool mat_eigen_symmetric(double* evec, int n, double* w) {
    MapMatrix mapA(evec, n, n);
    Eigen::SelfAdjointEigenSolver<ColMajorMatrix> solver(mapA);
    if (solver.info() != Eigen::Success) return false;
    MapVector(w, n) = solver.eigenvalues();
    mapA = solver.eigenvectors();
    return true;
}

bool mat_eigen_general(double* A_work, int n, double* wr, double* wi, double* vr) {
    ConstMapMatrix mapA(A_work, n, n);
    Eigen::EigenSolver<ColMajorMatrix> solver(mapA);
    if (solver.info() != Eigen::Success) return false;
    auto eigenvalues = solver.eigenvalues();
    for (int i = 0; i < n; ++i) {
        wr[i] = eigenvalues[i].real();
        wi[i] = eigenvalues[i].imag();
    }
    // Extract real parts of eigenvectors (matches LAPACKE_dgeev output for real eigenvalues)
    MapMatrix mapVr(vr, n, n);
    mapVr = solver.eigenvectors().real();
    return true;
}

bool mat_svd(double* A_work, int m, int n,
             double* U, double* S, double* Vt) {
    int minmn = std::min(m, n);
    ConstMapMatrix mapA(A_work, m, n);
    Eigen::JacobiSVD<ColMajorMatrix> svd(mapA, Eigen::ComputeThinU | Eigen::ComputeThinV);
    MapMatrix(U, m, minmn) = svd.matrixU();
    MapVector(S, minmn) = svd.singularValues();
    // LAPACK returns V^T, so transpose V
    MapMatrix(Vt, minmn, n) = svd.matrixV().transpose();
    return true;
}

bool mat_lstsq(double* A_work, int m, int n, double* x, int ldb, int nrhs) {
    ConstMapMatrix mapA(A_work, m, n);
    // Read b from first m rows of x
    ColMajorMatrix b = MapMatrix(x, ldb, nrhs).topRows(m);
    auto solution = mapA.colPivHouseholderQr().solve(b);
    // Write solution to first n rows of x
    MapMatrix(x, ldb, nrhs).topRows(n) = solution;
    return true;
}

} // namespace etil::core::backend
