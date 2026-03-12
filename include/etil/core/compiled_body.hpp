#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include "etil/core/word_impl.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/heap_object.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace etil::core {

/// A single instruction in a compiled word body.
struct Instruction {
    enum class Op {
        Call,           // Execute a word by cached impl or name lookup
        PushInt,        // Push integer literal onto data stack
        PushFloat,      // Push float literal onto data stack
        PushBool,       // Push boolean literal onto data stack
        Branch,         // Unconditional jump to offset (int_val)
        BranchIfFalse,  // Pop boolean; jump to offset if false
        DoSetup,        // Pop limit and index, push to local return stack
        DoLoop,         // Increment index, branch to int_val if not done
        DoPlusLoop,     // Pop increment, add to index, branch if not done
        DoI,            // Push current loop index from local return stack
        PrintString,    // Print string stored in word_name field
        PushString,     // Create HeapString from word_name, push as heap value
        PushDataPtr,    // Push pointer to this ByteCode's data_field_
        SetDoes,        // Set does>-body on last CREATE'd word (int_val = start offset)
        PushXt,         // Look up word by name, push execution token onto data stack
        ToR,            // Pop data stack, push to local return stack
        FromR,          // Pop local return stack, push to data stack
        FetchR,         // Copy top of local return stack to data stack
        DoJ,            // Push outer loop index from local return stack
        DoLeave,        // Pop DO loop frame, branch to int_val (past LOOP)
        DoExit,         // Return from current word (early exit)
        PushJson,       // Parse JSON from word_name, create HeapJson, push as heap value
    };

    Op op;
    int64_t int_val = 0;        // For PushInt, branch target offsets, SetDoes offset
    double float_val = 0.0;     // For PushFloat
    std::string word_name;      // For Call (lookup key) or PrintString (text)
    WordImpl* cached_impl = nullptr;  // Non-owning cache (Dictionary owns the WordImpl)
    uint64_t cached_generation = 0;   // Dictionary generation when cached_impl was set
};

/// The ByteCode class that WordImpl already forward-declares.
/// Contains the compiled instruction sequence for a colon definition.
class ByteCode {
public:
    ByteCode() = default;

    ~ByteCode() {
        // Invalidate registry entry if registered (shared_ptr keeps registry alive)
        if (registry_ && registry_index_ >= 0) {
            registry_->invalidate(static_cast<uint32_t>(registry_index_));
        }
        for (auto& v : data_field_) {
            value_release(v);
        }
    }

    std::vector<Instruction>& instructions() { return instructions_; }
    const std::vector<Instruction>& instructions() const { return instructions_; }

    std::vector<Value>& data_field() { return data_field_; }
    const std::vector<Value>& data_field() const { return data_field_; }

    size_t size() const { return instructions_.size(); }

    void append(Instruction instr) {
        instructions_.push_back(std::move(instr));
    }

    /// Backpatch a branch instruction at 'offset' to target 'target'.
    void backpatch(size_t offset, int64_t target) {
        instructions_[offset].int_val = target;
    }

    /// Registry index (-1 = not registered)
    int64_t registry_index() const { return registry_index_; }
    void set_registry_index(int64_t idx) { registry_index_ = idx; }

    /// Registry backpointer (shared_ptr keeps registry alive past ExecutionContext)
    DataFieldRegistry* registry() const { return registry_.get(); }
    const std::shared_ptr<DataFieldRegistry>& registry_ptr() const { return registry_; }
    void set_registry(const std::shared_ptr<DataFieldRegistry>& r) { registry_ = r; }

private:
    std::vector<Instruction> instructions_;
    std::vector<Value> data_field_;  // Per-word data storage for CREATE

    int64_t registry_index_ = -1;                    // Index in DataFieldRegistry (-1 = unregistered)
    std::shared_ptr<DataFieldRegistry> registry_;     // Keeps registry alive for destructor invalidation
};

/// Execute a compiled word body within the given execution context.
/// Uses a local vector as the return stack for DO/LOOP parameters.
bool execute_compiled(ByteCode& code, ExecutionContext& ctx);

} // namespace etil::core
