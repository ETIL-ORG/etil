// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/evolution/genetic_ops.hpp"
#include "etil/evolution/mutation_helpers.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace etil::evolution {

using namespace etil::core;
using Op = Instruction::Op;

GeneticOps::GeneticOps(MutationConfig config)
    : config_(config)
    , rng_(std::chrono::steady_clock::now().time_since_epoch().count())
{}

bool GeneticOps::is_control_flow(Op op) {
    switch (op) {
        case Op::Branch:
        case Op::BranchIfFalse:
        case Op::DoSetup:
        case Op::DoLoop:
        case Op::DoPlusLoop:
        case Op::DoI:
        case Op::DoJ:
        case Op::DoLeave:
        case Op::DoExit:
        case Op::ToR:
        case Op::FromR:
        case Op::FetchR:
        case Op::SetDoes:
        case Op::PushDataPtr:
            return true;
        default:
            return false;
    }
}

WordImplPtr GeneticOps::clone(const WordImpl& parent, Dictionary& dict) {
    auto bc = parent.bytecode();
    if (!bc) return WordImplPtr();

    auto id = Dictionary::next_id();
    WordImplPtr child(new WordImpl(parent.name(), id));
    child->set_generation(parent.generation() + 1);
    child->add_parent(parent.id());
    child->set_weight(parent.weight());
    child->set_signature(parent.signature());

    // Deep-copy bytecode (instructions only, not data field or registry)
    auto child_bc = std::make_shared<ByteCode>();
    for (const auto& instr : bc->instructions()) {
        child_bc->append(copy_instruction(instr));
    }
    child->set_bytecode(child_bc);
    return child;
}

bool GeneticOps::mutate(ByteCode& code) {
    if (code.size() < 2) return false;

    bool mutated = false;
    std::uniform_real_distribution<double> coin(0.0, 1.0);

    if (coin(rng_) < config_.instruction_swap_prob) {
        swap_instructions(code);
        mutated = true;
    }
    if (coin(rng_) < config_.constant_perturb_prob) {
        perturb_constant(code);
        mutated = true;
    }
    if (coin(rng_) < config_.instruction_insert_prob) {
        insert_instruction(code);
        mutated = true;
    }
    if (coin(rng_) < config_.instruction_delete_prob) {
        delete_instruction(code);
        mutated = true;
    }
    return mutated;
}

WordImplPtr GeneticOps::crossover(
    const WordImpl& parent_a,
    const WordImpl& parent_b,
    Dictionary& dict) {
    auto bc_a = parent_a.bytecode();
    auto bc_b = parent_b.bytecode();
    if (!bc_a || !bc_b) return WordImplPtr();
    if (bc_a->size() < 2 || bc_b->size() < 2) return WordImplPtr();

    auto id = Dictionary::next_id();
    WordImplPtr child(new WordImpl(parent_a.name(), id));
    child->set_generation(
        std::max(parent_a.generation(), parent_b.generation()) + 1);
    child->add_parent(parent_a.id());
    child->add_parent(parent_b.id());
    child->set_weight((parent_a.weight() + parent_b.weight()) / 2.0);
    child->set_signature(parent_a.signature());
    child->add_mutation(MutationHistory::MutationType::Crossover, "single-point");

    // Single-point crossover
    std::uniform_int_distribution<size_t> dist_a(1, bc_a->size() - 1);
    std::uniform_int_distribution<size_t> dist_b(1, bc_b->size() - 1);
    size_t cut_a = dist_a(rng_);
    size_t cut_b = dist_b(rng_);

    auto child_bc = std::make_shared<ByteCode>();
    // Take first part from parent A
    for (size_t i = 0; i < cut_a; ++i) {
        child_bc->append(copy_instruction(bc_a->instructions()[i]));
    }
    // Take second part from parent B
    for (size_t i = cut_b; i < bc_b->size(); ++i) {
        child_bc->append(copy_instruction(bc_b->instructions()[i]));
    }

    // Enforce max length
    while (child_bc->size() > config_.max_bytecode_length) {
        child_bc->instructions().pop_back();
    }

    child->set_bytecode(child_bc);
    return child;
}

void GeneticOps::swap_instructions(ByteCode& code) {
    auto& instrs = code.instructions();
    // Find non-control-flow indices
    std::vector<size_t> candidates;
    for (size_t i = 0; i < instrs.size(); ++i) {
        if (!is_control_flow(instrs[i].op)) {
            candidates.push_back(i);
        }
    }
    if (candidates.size() < 2) return;

    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    size_t a = dist(rng_);
    size_t b = dist(rng_);
    while (b == a) b = dist(rng_);
    std::swap(instrs[candidates[a]], instrs[candidates[b]]);
}

void GeneticOps::perturb_constant(ByteCode& code) {
    auto& instrs = code.instructions();
    // Find PushInt or PushFloat instructions
    std::vector<size_t> candidates;
    for (size_t i = 0; i < instrs.size(); ++i) {
        if (instrs[i].op == Op::PushInt || instrs[i].op == Op::PushFloat) {
            candidates.push_back(i);
        }
    }
    if (candidates.empty()) return;

    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    size_t idx = candidates[dist(rng_)];
    auto& instr = instrs[idx];
    perturb_numeric(instr.op, instr.int_val, instr.float_val,
                    config_.constant_perturb_stddev, rng_);
}

void GeneticOps::insert_instruction(ByteCode& code) {
    auto& instrs = code.instructions();
    if (instrs.size() >= config_.max_bytecode_length) return;
    if (instrs.empty()) return;

    // Copy a random existing instruction to a random position
    std::uniform_int_distribution<size_t> dist(0, instrs.size() - 1);
    size_t src = dist(rng_);
    size_t dst = dist(rng_);

    // Only insert non-control-flow instructions
    if (is_control_flow(instrs[src].op)) return;

    Instruction copy;
    copy.op = instrs[src].op;
    copy.int_val = instrs[src].int_val;
    copy.float_val = instrs[src].float_val;
    copy.word_name = instrs[src].word_name;
    instrs.insert(instrs.begin() + static_cast<long>(dst), std::move(copy));
}

void GeneticOps::delete_instruction(ByteCode& code) {
    auto& instrs = code.instructions();
    if (instrs.size() <= 2) return;

    // Find a non-control-flow instruction to delete
    std::vector<size_t> candidates;
    for (size_t i = 0; i < instrs.size(); ++i) {
        if (!is_control_flow(instrs[i].op)) {
            candidates.push_back(i);
        }
    }
    if (candidates.empty()) return;

    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    size_t idx = candidates[dist(rng_)];
    instrs.erase(instrs.begin() + static_cast<long>(idx));
}

} // namespace etil::evolution
