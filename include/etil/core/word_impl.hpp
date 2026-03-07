#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <unordered_set>

#include "etil/core/metadata.hpp"

namespace etil::core {

// Type signature for word inputs/outputs
struct TypeSignature {
    enum class Type {
        Unknown,
        Integer,
        Float,
        String,
        Array,
        Custom
    };
    
    std::vector<Type> inputs;
    std::vector<Type> outputs;
    
    bool matches(const TypeSignature& other) const;
};

// Hardware requirements
struct HardwareRequirements {
    bool requires_simd = false;
    uint32_t simd_width = 0;  // 128, 256, 512
    bool requires_gpu = false;
    size_t min_memory = 0;
    uint32_t min_threads = 1;
};

// Performance profile
struct PerfProfile {
    std::atomic<uint64_t> total_calls{0};
    std::atomic<uint64_t> total_duration_ns{0};
    std::atomic<uint64_t> total_memory_bytes{0};
    
    double mean_duration_ns() const {
        auto calls = total_calls.load(std::memory_order_relaxed);
        if (calls == 0) return 0.0;
        return static_cast<double>(
            total_duration_ns.load(std::memory_order_relaxed)
        ) / calls;
    }
    
    void record(std::chrono::nanoseconds duration, size_t memory) {
        total_calls.fetch_add(1, std::memory_order_relaxed);
        total_duration_ns.fetch_add(
            duration.count(), 
            std::memory_order_relaxed
        );
        total_memory_bytes.fetch_add(memory, std::memory_order_relaxed);
    }
};

// Mutation history for genetic tracking
struct MutationHistory {
    enum class MutationType {
        None,
        Inline,
        Vectorize,
        Reorder,
        Specialize,
        Memoize,
        Crossover
    };
    
    std::vector<MutationType> mutations;
    std::string description;
};

// Forward declarations
class ByteCode;
class ExecutionContext;

// Word implementation node
class WordImpl {
public:
    using ImplId = uint64_t;
    using FunctionPtr = bool(*)(ExecutionContext&);  // Native code pointer
    
    WordImpl(std::string name, ImplId id)
        : name_(std::move(name))
        , impl_id_(id)
        , generation_(0)
        , weight_(1.0)
    {}
    
    // Accessors
    ImplId id() const { return impl_id_; }
    const std::string& name() const { return name_; }
    uint64_t generation() const { return generation_; }
    double weight() const { return weight_.load(std::memory_order_relaxed); }
    
    void set_weight(double w) { weight_.store(w, std::memory_order_relaxed); }
    void set_generation(uint64_t gen) { generation_ = gen; }

    // Immediate flag (execute during compilation instead of compiling as Call)
    bool immediate() const { return immediate_; }
    void set_immediate(bool v) { immediate_ = v; }

    // Native code
    FunctionPtr native_code() const { return native_code_; }
    void set_native_code(FunctionPtr fn) { native_code_ = fn; }
    
    // Bytecode
    std::shared_ptr<ByteCode> bytecode() const { return bytecode_; }
    void set_bytecode(std::shared_ptr<ByteCode> bc) { bytecode_ = bc; }
    
    // Performance tracking
    PerfProfile& profile() { return profile_; }
    const PerfProfile& profile() const { return profile_; }
    
    void record_execution(
        std::chrono::nanoseconds duration,
        size_t memory,
        bool success
    ) {
        profile_.record(duration, memory);
        if (success) {
            success_count_.fetch_add(1, std::memory_order_relaxed);
        } else {
            failure_count_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    // Success metrics
    uint64_t success_count() const {
        return success_count_.load(std::memory_order_relaxed);
    }
    
    uint64_t failure_count() const {
        return failure_count_.load(std::memory_order_relaxed);
    }
    
    double success_rate() const {
        auto total = success_count() + failure_count();
        if (total == 0) return 1.0;
        return static_cast<double>(success_count()) / total;
    }
    
    // Type information
    const TypeSignature& signature() const { return signature_; }
    void set_signature(TypeSignature sig) { signature_ = std::move(sig); }
    
    // Hardware requirements
    const HardwareRequirements& hw_requirements() const { return hw_req_; }
    void set_hw_requirements(HardwareRequirements req) { hw_req_ = req; }
    
    // Genetic information
    const std::vector<ImplId>& parent_ids() const { return parent_ids_; }
    void add_parent(ImplId parent) { parent_ids_.push_back(parent); }
    
    const MutationHistory& mutations() const { return mutations_; }
    void add_mutation(MutationHistory::MutationType type, std::string desc) {
        mutations_.mutations.push_back(type);
        mutations_.description += desc + "; ";
    }
    
    // Dependencies
    const std::unordered_set<ImplId>& dependencies() const {
        return dependencies_;
    }
    
    void add_dependency(ImplId dep) {
        dependencies_.insert(dep);
    }

    // Metadata
    MetadataMap& metadata() { return metadata_; }
    const MetadataMap& metadata() const { return metadata_; }

    // --- Definition origin tracking ---

    // Mark how this implementation was defined (exactly one per impl)
    inline void mark_as_primitive() {
        metadata_.set(kMetaDefinitionType_, MetadataFormat::Text, "primitive");
    }
    inline void mark_as_include(const std::string& file, size_t line) {
        metadata_.set(kMetaDefinitionType_, MetadataFormat::Text, "include");
        metadata_.set(kMetaSourceFile_, MetadataFormat::Text, file);
        metadata_.set(kMetaSourceLine_, MetadataFormat::Text, std::to_string(line));
    }
    inline void mark_as_evaluate() {
        metadata_.set(kMetaDefinitionType_, MetadataFormat::Text, "evaluate");
    }
    inline void mark_as_interpret() {
        metadata_.set(kMetaDefinitionType_, MetadataFormat::Text, "interpret");
    }

    // Query definition origin
    inline std::optional<std::string> definition_type() const {
        auto e = metadata_.get(kMetaDefinitionType_);
        return e ? std::optional<std::string>(e->content) : std::nullopt;
    }
    inline std::optional<std::string> source_file() const {
        auto e = metadata_.get(kMetaSourceFile_);
        return e ? std::optional<std::string>(e->content) : std::nullopt;
    }
    inline std::optional<std::string> source_line() const {
        auto e = metadata_.get(kMetaSourceLine_);
        return e ? std::optional<std::string>(e->content) : std::nullopt;
    }

    // Reference counting for concurrent access
    void add_ref() { refcount_.fetch_add(1, std::memory_order_relaxed); }
    void release() {
        if (refcount_.fetch_sub(1, std::memory_order_release) == 1) {
            std::atomic_thread_fence(std::memory_order_acquire);
            delete this;
        }
    }

private:
    std::string name_;
    ImplId impl_id_;
    
    // Execution
    FunctionPtr native_code_ = nullptr;
    std::shared_ptr<ByteCode> bytecode_;
    
    // Characteristics
    TypeSignature signature_;
    HardwareRequirements hw_req_;
    PerfProfile profile_;
    
    // Immediate flag
    bool immediate_ = false;

    // Selection
    std::atomic<double> weight_;
    std::atomic<uint64_t> success_count_{0};
    std::atomic<uint64_t> failure_count_{0};
    
    // Evolution
    uint64_t generation_;
    std::vector<ImplId> parent_ids_;
    MutationHistory mutations_;
    
    // Dependencies
    std::unordered_set<ImplId> dependencies_;

    // Metadata
    MetadataMap metadata_;

    // Metadata key strings (hidden from callers)
    static constexpr const char* kMetaDefinitionType_ = "definition-type";
    static constexpr const char* kMetaSourceFile_     = "source-file";
    static constexpr const char* kMetaSourceLine_     = "source-line";

    // Concurrency
    std::atomic<uint64_t> refcount_{1};
};

// Smart pointer for WordImpl with intrusive ref counting
class WordImplPtr {
public:
    WordImplPtr() : ptr_(nullptr) {}
    
    // Adopts the existing refcount (starts at 1) — does not add_ref.
    explicit WordImplPtr(WordImpl* p) : ptr_(p) {}
    
    WordImplPtr(const WordImplPtr& other) : ptr_(other.ptr_) {
        if (ptr_) ptr_->add_ref();
    }
    
    WordImplPtr(WordImplPtr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }
    
    ~WordImplPtr() {
        if (ptr_) ptr_->release();
    }
    
    WordImplPtr& operator=(const WordImplPtr& other) {
        if (this != &other) {
            if (ptr_) ptr_->release();
            ptr_ = other.ptr_;
            if (ptr_) ptr_->add_ref();
        }
        return *this;
    }
    
    WordImplPtr& operator=(WordImplPtr&& other) noexcept {
        if (this != &other) {
            if (ptr_) ptr_->release();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }
    
    WordImpl* get() const { return ptr_; }
    WordImpl* operator->() const { return ptr_; }
    WordImpl& operator*() const { return *ptr_; }
    
    explicit operator bool() const { return ptr_ != nullptr; }

private:
    WordImpl* ptr_;
};

} // namespace etil::core
