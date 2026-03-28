#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/compiled_body.hpp"

#include <cmath>
#include <random>

namespace etil::evolution {

/// Copy an Instruction without cached_impl/cached_generation.
inline etil::core::Instruction copy_instruction(const etil::core::Instruction& src) {
    etil::core::Instruction dst;
    dst.op = src.op;
    dst.int_val = src.int_val;
    dst.float_val = src.float_val;
    dst.word_name = src.word_name;
    return dst;
}

/// Perturb a numeric constant (PushInt or PushFloat) with Gaussian noise.
///
/// For integers: guaranteed to change by at least ±1.  Small values (|v|<=10)
/// use a uniform ±1..3 step; larger values use Gaussian noise scaled to ~10%
/// of magnitude, clamped to a minimum step of ±1.
///
/// For floats: Gaussian noise at stddev fraction of magnitude (min scale 1.0).
inline void perturb_numeric(etil::core::Instruction::Op op,
                            int64_t& int_val, double& float_val,
                            double stddev, std::mt19937_64& rng) {
    if (op == etil::core::Instruction::Op::PushInt) {
        int64_t abs_val = std::abs(int_val);
        int64_t delta;
        if (abs_val <= 10) {
            // Small integers: uniform step ±1 to ±3
            std::uniform_int_distribution<int64_t> step(1, 3);
            delta = step(rng);
        } else {
            // Larger integers: Gaussian ~10% of magnitude, min ±1
            std::normal_distribution<double> noise(0.0, stddev);
            double scale = static_cast<double>(abs_val);
            delta = static_cast<int64_t>(std::round(noise(rng) * scale));
            if (delta == 0) delta = 1;
        }
        // Random sign
        std::bernoulli_distribution coin(0.5);
        if (coin(rng)) delta = -delta;
        int_val += delta;
    } else if (op == etil::core::Instruction::Op::PushFloat) {
        std::normal_distribution<double> noise(0.0, stddev);
        double scale = std::max(1.0, std::abs(float_val));
        float_val += noise(rng) * scale;
    }
}

} // namespace etil::evolution
