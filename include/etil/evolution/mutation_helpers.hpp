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
inline void perturb_numeric(etil::core::Instruction::Op op,
                            int64_t& int_val, double& float_val,
                            double stddev, std::mt19937_64& rng) {
    std::normal_distribution<double> noise(0.0, stddev);
    if (op == etil::core::Instruction::Op::PushInt) {
        double scale = std::max(1.0, std::abs(static_cast<double>(int_val)));
        int_val += static_cast<int64_t>(std::round(noise(rng) * scale));
    } else if (op == etil::core::Instruction::Op::PushFloat) {
        double scale = std::max(1.0, std::abs(float_val));
        float_val += noise(rng) * scale;
    }
}

} // namespace etil::evolution
