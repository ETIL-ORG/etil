#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/word_impl.hpp"
#include "etil/core/compiled_body.hpp"
#include "etil/core/dictionary.hpp"

#include <random>

namespace etil::evolution {

struct MutationConfig {
    double instruction_swap_prob = 0.3;
    double constant_perturb_prob = 0.3;
    double instruction_insert_prob = 0.1;
    double instruction_delete_prob = 0.1;
    double constant_perturb_stddev = 0.1;
    size_t max_bytecode_length = 256;
};

/// Genetic operators for bytecode-level evolution.
class GeneticOps {
public:
    explicit GeneticOps(MutationConfig config = {});

    /// Clone a WordImpl: copies bytecode, increments generation,
    /// records parent ID. Returns null if parent has no bytecode.
    etil::core::WordImplPtr clone(const etil::core::WordImpl& parent,
                                   etil::core::Dictionary& dict);

    /// Mutate a bytecode sequence in-place.
    /// Returns true if any mutation was applied.
    bool mutate(etil::core::ByteCode& code);

    /// Crossover: create child from two parents' bytecode.
    /// Single-point crossover at a random instruction boundary.
    /// Returns null if either parent has no bytecode.
    etil::core::WordImplPtr crossover(
        const etil::core::WordImpl& parent_a,
        const etil::core::WordImpl& parent_b,
        etil::core::Dictionary& dict);

    const MutationConfig& config() const { return config_; }

private:
    MutationConfig config_;
    std::mt19937_64 rng_;

    static bool is_control_flow(etil::core::Instruction::Op op);
    void swap_instructions(etil::core::ByteCode& code);
    void perturb_constant(etil::core::ByteCode& code);
    void insert_instruction(etil::core::ByteCode& code);
    void delete_instruction(etil::core::ByteCode& code);
};

} // namespace etil::evolution
