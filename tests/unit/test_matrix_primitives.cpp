// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifdef ETIL_LINALG_ENABLED

#include "etil/core/primitives.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_matrix.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/interpreter.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <cmath>

using namespace etil::core;

class MatrixPrimitivesTest : public ::testing::Test {
protected:
    Dictionary dict;
    std::ostringstream out;
    Interpreter interp{dict, out};

    void SetUp() override {
        register_primitives(dict);
    }

    void run(const std::string& code) {
        out.str("");
        interp.interpret_line(code);
    }

    ExecutionContext& ctx() { return interp.context(); }
};

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

TEST_F(MatrixPrimitivesTest, MatNew) {
    run("3 4 mat-new");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Matrix);
    auto* mat = opt->as_matrix();
    EXPECT_EQ(mat->rows(), 3);
    EXPECT_EQ(mat->cols(), 4);
    // All zeros
    for (int64_t r = 0; r < 3; ++r)
        for (int64_t c = 0; c < 4; ++c)
            EXPECT_DOUBLE_EQ(mat->get(r, c), 0.0);
    mat->release();
}

TEST_F(MatrixPrimitivesTest, MatEye) {
    run("3 mat-eye");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* mat = opt->as_matrix();
    EXPECT_EQ(mat->rows(), 3);
    EXPECT_EQ(mat->cols(), 3);
    for (int64_t r = 0; r < 3; ++r)
        for (int64_t c = 0; c < 3; ++c)
            EXPECT_DOUBLE_EQ(mat->get(r, c), (r == c) ? 1.0 : 0.0);
    mat->release();
}

TEST_F(MatrixPrimitivesTest, MatFromArray) {
    // Create array [1.0 2.0 3.0 4.0 5.0 6.0], reshape as 2x3
    run("array-new 1.0 array-push 2.0 array-push 3.0 array-push 4.0 array-push 5.0 array-push 6.0 array-push 2 3 mat-from-array");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* mat = opt->as_matrix();
    EXPECT_EQ(mat->rows(), 2);
    EXPECT_EQ(mat->cols(), 3);
    // Row-major input: [1,2,3; 4,5,6]
    EXPECT_DOUBLE_EQ(mat->get(0, 0), 1.0);
    EXPECT_DOUBLE_EQ(mat->get(0, 1), 2.0);
    EXPECT_DOUBLE_EQ(mat->get(0, 2), 3.0);
    EXPECT_DOUBLE_EQ(mat->get(1, 0), 4.0);
    EXPECT_DOUBLE_EQ(mat->get(1, 1), 5.0);
    EXPECT_DOUBLE_EQ(mat->get(1, 2), 6.0);
    mat->release();
}

TEST_F(MatrixPrimitivesTest, MatDiag) {
    run("array-new 2.0 array-push 3.0 array-push 5.0 array-push mat-diag");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* mat = opt->as_matrix();
    EXPECT_EQ(mat->rows(), 3);
    EXPECT_EQ(mat->cols(), 3);
    EXPECT_DOUBLE_EQ(mat->get(0, 0), 2.0);
    EXPECT_DOUBLE_EQ(mat->get(1, 1), 3.0);
    EXPECT_DOUBLE_EQ(mat->get(2, 2), 5.0);
    EXPECT_DOUBLE_EQ(mat->get(0, 1), 0.0);
    mat->release();
}

TEST_F(MatrixPrimitivesTest, MatRand) {
    run("2 3 mat-rand");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* mat = opt->as_matrix();
    EXPECT_EQ(mat->rows(), 2);
    EXPECT_EQ(mat->cols(), 3);
    for (size_t i = 0; i < mat->size(); ++i) {
        EXPECT_GE(mat->data()[i], 0.0);
        EXPECT_LT(mat->data()[i], 1.0);
    }
    mat->release();
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

TEST_F(MatrixPrimitivesTest, MatGetSet) {
    run("2 2 mat-new 0 1 3.14 mat-set 0 1 mat-get");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_DOUBLE_EQ(opt->as_float, 3.14);
}

TEST_F(MatrixPrimitivesTest, MatRowsCols) {
    run("3 5 mat-new dup mat-rows swap mat-cols");
    auto opt_cols = ctx().data_stack().pop();
    auto opt_rows = ctx().data_stack().pop();
    ASSERT_TRUE(opt_rows.has_value());
    ASSERT_TRUE(opt_cols.has_value());
    EXPECT_EQ(opt_rows->as_int, 3);
    EXPECT_EQ(opt_cols->as_int, 5);
}

TEST_F(MatrixPrimitivesTest, MatRow) {
    run("array-new 1.0 array-push 2.0 array-push 3.0 array-push 4.0 array-push 2 2 mat-from-array 1 mat-row");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->type, Value::Type::Array);
    auto* arr = opt->as_array();
    EXPECT_EQ(arr->length(), 2u);
    Value v0, v1;
    arr->get(0, v0);
    arr->get(1, v1);
    EXPECT_DOUBLE_EQ(v0.as_float, 3.0);
    EXPECT_DOUBLE_EQ(v1.as_float, 4.0);
    arr->release();
}

TEST_F(MatrixPrimitivesTest, MatCol) {
    run("array-new 1.0 array-push 2.0 array-push 3.0 array-push 4.0 array-push 2 2 mat-from-array 0 mat-col");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* arr = opt->as_array();
    EXPECT_EQ(arr->length(), 2u);
    Value v0, v1;
    arr->get(0, v0);
    arr->get(1, v1);
    EXPECT_DOUBLE_EQ(v0.as_float, 1.0);
    EXPECT_DOUBLE_EQ(v1.as_float, 3.0);
    arr->release();
}

// ---------------------------------------------------------------------------
// Arithmetic
// ---------------------------------------------------------------------------

TEST_F(MatrixPrimitivesTest, MatMul) {
    // [1 2; 3 4] * [5 6; 7 8] = [19 22; 43 50]
    run("array-new 1.0 array-push 2.0 array-push 3.0 array-push 4.0 array-push 2 2 mat-from-array");
    run("array-new 5.0 array-push 6.0 array-push 7.0 array-push 8.0 array-push 2 2 mat-from-array");
    run("mat*");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* C = opt->as_matrix();
    EXPECT_DOUBLE_EQ(C->get(0, 0), 19.0);
    EXPECT_DOUBLE_EQ(C->get(0, 1), 22.0);
    EXPECT_DOUBLE_EQ(C->get(1, 0), 43.0);
    EXPECT_DOUBLE_EQ(C->get(1, 1), 50.0);
    C->release();
}

TEST_F(MatrixPrimitivesTest, MatAdd) {
    run("array-new 1.0 array-push 2.0 array-push 3.0 array-push 4.0 array-push 2 2 mat-from-array");
    run("array-new 10.0 array-push 20.0 array-push 30.0 array-push 40.0 array-push 2 2 mat-from-array");
    run("mat+");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* C = opt->as_matrix();
    EXPECT_DOUBLE_EQ(C->get(0, 0), 11.0);
    EXPECT_DOUBLE_EQ(C->get(1, 1), 44.0);
    C->release();
}

TEST_F(MatrixPrimitivesTest, MatSub) {
    run("array-new 10.0 array-push 20.0 array-push 2 1 mat-from-array");
    run("array-new 3.0 array-push 5.0 array-push 2 1 mat-from-array");
    run("mat-");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* C = opt->as_matrix();
    EXPECT_DOUBLE_EQ(C->get(0, 0), 7.0);
    EXPECT_DOUBLE_EQ(C->get(1, 0), 15.0);
    C->release();
}

TEST_F(MatrixPrimitivesTest, MatScale) {
    run("array-new 2.0 array-push 4.0 array-push 2 1 mat-from-array 3.0 mat-scale");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* C = opt->as_matrix();
    EXPECT_DOUBLE_EQ(C->get(0, 0), 6.0);
    EXPECT_DOUBLE_EQ(C->get(1, 0), 12.0);
    C->release();
}

TEST_F(MatrixPrimitivesTest, MatTranspose) {
    run("array-new 1.0 array-push 2.0 array-push 3.0 array-push 4.0 array-push 5.0 array-push 6.0 array-push 2 3 mat-from-array mat-transpose");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* T = opt->as_matrix();
    EXPECT_EQ(T->rows(), 3);
    EXPECT_EQ(T->cols(), 2);
    // Original [1,2,3; 4,5,6] → T = [1,4; 2,5; 3,6]
    EXPECT_DOUBLE_EQ(T->get(0, 0), 1.0);
    EXPECT_DOUBLE_EQ(T->get(0, 1), 4.0);
    EXPECT_DOUBLE_EQ(T->get(1, 0), 2.0);
    EXPECT_DOUBLE_EQ(T->get(2, 1), 6.0);
    T->release();
}

TEST_F(MatrixPrimitivesTest, MatMulDimensionMismatch) {
    run("2 3 mat-new 4 5 mat-new mat*");
    // Should fail — stack should be empty (both matrices released on error)
    EXPECT_EQ(ctx().data_stack().size(), 0u);
}

// ---------------------------------------------------------------------------
// Solvers
// ---------------------------------------------------------------------------

TEST_F(MatrixPrimitivesTest, MatSolve) {
    // Solve [2 1; 1 3]x = [5; 10] → x = [1; 3]
    run("array-new 2.0 array-push 1.0 array-push 1.0 array-push 3.0 array-push 2 2 mat-from-array");
    run("array-new 5.0 array-push 10.0 array-push 2 1 mat-from-array");
    run("mat-solve");
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->as_int, 0);  // success
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* x = opt->as_matrix();
    EXPECT_NEAR(x->get(0, 0), 1.0, 1e-10);
    EXPECT_NEAR(x->get(1, 0), 3.0, 1e-10);
    x->release();
}

TEST_F(MatrixPrimitivesTest, MatInv) {
    // Inverse of [1 2; 3 4] = 1/(1*4-2*3) * [4 -2; -3 1] = [-2 1; 1.5 -0.5]
    run("array-new 1.0 array-push 2.0 array-push 3.0 array-push 4.0 array-push 2 2 mat-from-array mat-inv");
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->as_int, 0);
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* inv = opt->as_matrix();
    EXPECT_NEAR(inv->get(0, 0), -2.0, 1e-10);
    EXPECT_NEAR(inv->get(0, 1), 1.0, 1e-10);
    EXPECT_NEAR(inv->get(1, 0), 1.5, 1e-10);
    EXPECT_NEAR(inv->get(1, 1), -0.5, 1e-10);
    inv->release();
}

TEST_F(MatrixPrimitivesTest, MatDet) {
    // det([1 2; 3 4]) = 1*4 - 2*3 = -2
    run("array-new 1.0 array-push 2.0 array-push 3.0 array-push 4.0 array-push 2 2 mat-from-array mat-det");
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->as_int, 0);
    auto det = ctx().data_stack().pop();
    ASSERT_TRUE(det.has_value());
    EXPECT_NEAR(det->as_float, -2.0, 1e-10);
}

// ---------------------------------------------------------------------------
// Decompositions
// ---------------------------------------------------------------------------

TEST_F(MatrixPrimitivesTest, MatEigenSymmetric) {
    // Symmetric: [2 1; 1 2] → eigenvalues [1, 3]
    run("array-new 2.0 array-push 1.0 array-push 1.0 array-push 2.0 array-push 2 2 mat-from-array mat-eigen");
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->as_int, 0);
    auto opt_evec = ctx().data_stack().pop();
    auto opt_eval = ctx().data_stack().pop();
    ASSERT_TRUE(opt_eval.has_value());
    ASSERT_TRUE(opt_evec.has_value());
    auto* eval = opt_eval->as_matrix();
    EXPECT_NEAR(eval->get(0, 0), 1.0, 1e-10);
    EXPECT_NEAR(eval->get(1, 0), 3.0, 1e-10);
    eval->release();
    opt_evec->release();
}

TEST_F(MatrixPrimitivesTest, MatSvd) {
    // SVD of [3 0; 0 4] → singular values [4, 3]
    run("array-new 3.0 array-push 0.0 array-push 0.0 array-push 4.0 array-push 2 2 mat-from-array mat-svd");
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->as_int, 0);
    auto opt_vt = ctx().data_stack().pop();
    auto opt_s = ctx().data_stack().pop();
    auto opt_u = ctx().data_stack().pop();
    ASSERT_TRUE(opt_s.has_value());
    auto* S = opt_s->as_matrix();
    // Singular values sorted descending
    EXPECT_NEAR(S->get(0, 0), 4.0, 1e-10);
    EXPECT_NEAR(S->get(1, 0), 3.0, 1e-10);
    S->release();
    opt_u->release();
    opt_vt->release();
}

TEST_F(MatrixPrimitivesTest, MatLstsq) {
    // Overdetermined: [1; 1; 1] x ≈ [1; 2; 3] → x = [2]
    run("array-new 1.0 array-push 1.0 array-push 1.0 array-push 3 1 mat-from-array");
    run("array-new 1.0 array-push 2.0 array-push 3.0 array-push 3 1 mat-from-array");
    run("mat-lstsq");
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->as_int, 0);
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* x = opt->as_matrix();
    EXPECT_NEAR(x->get(0, 0), 2.0, 1e-10);
    x->release();
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

TEST_F(MatrixPrimitivesTest, MatNorm) {
    // Frobenius norm of [3 4] = sqrt(9+16) = 5
    run("array-new 3.0 array-push 4.0 array-push 1 2 mat-from-array mat-norm");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_NEAR(opt->as_float, 5.0, 1e-10);
}

TEST_F(MatrixPrimitivesTest, MatTrace) {
    run("array-new 1.0 array-push 2.0 array-push 3.0 array-push 4.0 array-push 2 2 mat-from-array mat-trace");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_NEAR(opt->as_float, 5.0, 1e-10);
}

TEST_F(MatrixPrimitivesTest, MatPrint) {
    run("array-new 1.0 array-push 2.0 array-push 3.0 array-push 4.0 array-push 2 2 mat-from-array mat.");
    EXPECT_TRUE(out.str().find("<matrix 2x2>") != std::string::npos);
    EXPECT_TRUE(out.str().find("1") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

TEST_F(MatrixPrimitivesTest, MatGetOutOfBounds) {
    run("2 2 mat-new 5 0 mat-get");
    // Should fail, stack should be empty
    EXPECT_EQ(ctx().data_stack().size(), 0u);
}

TEST_F(MatrixPrimitivesTest, MatSolveNonSquare) {
    run("2 3 mat-new 2 1 mat-new mat-solve");
    EXPECT_EQ(ctx().data_stack().size(), 0u);
}

TEST_F(MatrixPrimitivesTest, MatInvIdentity) {
    // Inverse of identity is identity
    run("3 mat-eye mat-inv");
    auto flag = ctx().data_stack().pop();
    ASSERT_TRUE(flag.has_value());
    EXPECT_EQ(flag->as_int, 0);
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* inv = opt->as_matrix();
    for (int64_t i = 0; i < 3; ++i)
        for (int64_t j = 0; j < 3; ++j)
            EXPECT_NEAR(inv->get(i, j), (i == j) ? 1.0 : 0.0, 1e-10);
    inv->release();
}

TEST_F(MatrixPrimitivesTest, MatMulIdentity) {
    // A * I = A
    run("array-new 1.0 array-push 2.0 array-push 3.0 array-push 4.0 array-push 2 2 mat-from-array");
    run("2 mat-eye mat*");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto* C = opt->as_matrix();
    EXPECT_DOUBLE_EQ(C->get(0, 0), 1.0);
    EXPECT_DOUBLE_EQ(C->get(0, 1), 2.0);
    EXPECT_DOUBLE_EQ(C->get(1, 0), 3.0);
    EXPECT_DOUBLE_EQ(C->get(1, 1), 4.0);
    C->release();
}

// ---------------------------------------------------------------------------
// Type system integration
// ---------------------------------------------------------------------------

TEST_F(MatrixPrimitivesTest, DotPrintsMatrix) {
    run("2 3 mat-new .");
    EXPECT_TRUE(out.str().find("<matrix 2x3>") != std::string::npos);
}

TEST_F(MatrixPrimitivesTest, FormatValue) {
    run("2 3 mat-new");
    auto opt = ctx().data_stack().pop();
    ASSERT_TRUE(opt.has_value());
    auto s = Interpreter::format_value(*opt);
    EXPECT_TRUE(s.find("matrix") != std::string::npos);
    EXPECT_TRUE(s.find("2x3") != std::string::npos);
    opt->release();
}

#endif // ETIL_LINALG_ENABLED
