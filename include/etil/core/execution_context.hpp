#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <istream>
#include <memory>
#include <ostream>
#include <vector>

namespace etil::lvfs { class Lvfs; }
namespace etil::fileio { class UvSession; }
namespace etil::net { struct HttpClientState; }
namespace etil::db { struct MongoClientState; }
namespace etil::mcp { struct RolePermissions; }
namespace etil::selection { class SelectionEngine; }

namespace etil::core
{
    // Forward declarations
    class Dictionary;
    class Interpreter;
    class WordImpl;
    class HeapObject;
    class HeapString;
    class HeapArray;
    class HeapByteArray;
    class HeapMap;
    class HeapJson;
    class HeapMatrix;
    class HeapObservable;

    // Value type on stack (union/variant)
    struct Value
    {
        enum class Type
        {
            Integer,
            Float,
            Boolean,
            String,
            DataRef,
            Array,
            ByteArray,
            Map,
            Json,
            Matrix,
            Observable,
            Xt
        };

        Type type;

        union
        {
            int64_t as_int;
            double as_float;
            void* as_ptr;
        };

        Value() : type(Type::Integer), as_int(0)
        {
        }

        explicit Value(int64_t i) : type(Type::Integer), as_int(i)
        {
        }

        explicit Value(double f) : type(Type::Float), as_float(f)
        {
        }

        explicit Value(bool b) : type(Type::Boolean), as_int(b ? 1 : 0)
        {
        }

        bool as_bool() const { return as_int != 0; }

        // Typed pointer accessors (caller must check type first)
        HeapString* as_string() const { return static_cast<HeapString*>(as_ptr); }
        HeapArray* as_array() const { return static_cast<HeapArray*>(as_ptr); }
        HeapByteArray* as_byte_array() const { return static_cast<HeapByteArray*>(as_ptr); }
        HeapMap* as_map() const { return static_cast<HeapMap*>(as_ptr); }
        HeapJson* as_json() const { return static_cast<HeapJson*>(as_ptr); }
        HeapMatrix* as_matrix() const { return static_cast<HeapMatrix*>(as_ptr); }
        HeapObservable* as_observable() const { return static_cast<HeapObservable*>(as_ptr); }
        WordImpl* as_xt_impl() const { return static_cast<WordImpl*>(as_ptr); }

        // DataRef member accessors
        uint32_t dataref_index() const {
            return static_cast<uint32_t>(static_cast<uint64_t>(as_int) >> 32);
        }
        uint32_t dataref_offset() const {
            return static_cast<uint32_t>(as_int & 0xFFFFFFFF);
        }

        // Static factories (declared here, defined in heap_object.hpp)
        static Value from(HeapObject* obj);
        static Value from_xt(WordImpl* impl);

        // Lifecycle (declared here, defined in heap_object.hpp)
        void release() const;
        void addref() const;
    };

    /// Create a DataRef value encoding registry index and slot offset.
    /// Packs index (high 32) + offset (low 32) into as_int.
    inline Value make_dataref(uint32_t index, uint32_t offset) {
        Value v;
        v.type = Value::Type::DataRef;
        v.as_int = (static_cast<int64_t>(index) << 32) |
                   static_cast<int64_t>(offset);
        return v;
    }

    /// Extract registry index from a DataRef value.
    inline uint32_t dataref_index(const Value& v) {
        return static_cast<uint32_t>(static_cast<uint64_t>(v.as_int) >> 32);
    }

    /// Extract slot offset from a DataRef value.
    inline uint32_t dataref_offset(const Value& v) {
        return static_cast<uint32_t>(v.as_int & 0xFFFFFFFF);
    }

    /// Create a Value holding an execution token (WordImpl*).
    /// The caller must ensure the WordImpl is kept alive (add_ref'd).
    inline Value make_xt_value(WordImpl* impl) {
        Value v;
        v.type = Value::Type::Xt;
        v.as_ptr = static_cast<void*>(impl);
        return v;
    }

} // namespace etil::core

// ValueStack requires complete Value type — include after Value is defined.
#include "etil/core/value_stack.hpp"

namespace etil::core {

    /// Registry of data field vectors for bounds-checked DataRef resolution.
    /// Entries are non-owning pointers to ByteCode::data_field_ vectors.
    class DataFieldRegistry
    {
    public:
        /// Register a data field vector, returning its index.
        uint32_t register_field(std::vector<Value>* field) {
            uint32_t idx = static_cast<uint32_t>(entries_.size());
            entries_.push_back(field);
            return idx;
        }

        /// Resolve a registry index to its data field vector.
        /// Returns nullptr for invalidated or out-of-range entries.
        std::vector<Value>* resolve(uint32_t index) const {
            if (index >= entries_.size()) return nullptr;
            return entries_[index];
        }

        /// Invalidate an entry (set to nullptr). Called when ByteCode is destroyed.
        void invalidate(uint32_t index) {
            if (index < entries_.size()) {
                entries_[index] = nullptr;
            }
        }

        /// Update an entry to point at a new vector (for does> transfer).
        void update(uint32_t index, std::vector<Value>* field) {
            if (index < entries_.size()) {
                entries_[index] = field;
            }
        }

        /// Total number of registry slots (including invalidated).
        size_t entry_count() const { return entries_.size(); }

        /// Number of live (non-null) entries.
        size_t live_count() const {
            size_t count = 0;
            for (auto* e : entries_) {
                if (e) count = count + 1;
            }
            return count;
        }

        /// Total cells across all live data fields.
        size_t total_cells() const {
            size_t count = 0;
            for (auto* e : entries_) {
                if (e) count += e->size();
            }
            return count;
        }

    private:
        std::vector<std::vector<Value>*> entries_;
    };

    // SIMD context (placeholder for AVX/NEON operations)
    struct SIMDContext
    {
        bool avx2_available = false;
        bool avx512_available = false;
        bool neon_available = false;

        SIMDContext();
    };

    // GPU context (placeholder for CUDA/OpenCL)
    struct GPUContext
    {
        bool cuda_available = false;
        int device_id = -1;

        GPUContext();
    };

    // Thread-local execution context
    class ExecutionContext
    {
    public:
        ExecutionContext(uint32_t thread_id)
            : thread_id_(thread_id)
              , data_field_registry_(std::make_shared<DataFieldRegistry>())
              , start_time_(std::chrono::high_resolution_clock::now())
        {
        }

        // Thread identification
        uint32_t thread_id() const { return thread_id_; }

        // Data stacks
        ValueStack& data_stack() { return data_stack_; }
        ValueStack& return_stack() { return return_stack_; }
        ValueStack& float_stack() { return float_stack_; }

        const ValueStack& data_stack() const { return data_stack_; }
        const ValueStack& return_stack() const { return return_stack_; }
        const ValueStack& float_stack() const { return float_stack_; }

        // Hardware contexts
        SIMDContext& simd() { return simd_; }
        const SIMDContext& simd() const { return simd_; }

        GPUContext* gpu() { return gpu_.get(); }
        const GPUContext* gpu() const { return gpu_.get(); }

        // Timing
        std::chrono::high_resolution_clock::time_point start_time() const
        {
            return start_time_;
        }

        void reset_timer()
        {
            start_time_ = std::chrono::high_resolution_clock::now();
        }

        std::chrono::nanoseconds elapsed() const
        {
            return std::chrono::high_resolution_clock::now() - start_time_;
        }

        // Dictionary access (non-owning)
        Dictionary* dictionary() const { return dictionary_; }
        void set_dictionary(Dictionary* dict) { dictionary_ = dict; }

        // Last CREATE word (non-owning, for , allot does>)
        WordImpl* last_created() const { return last_created_; }
        void set_last_created(WordImpl* w) { last_created_ = w; }

        // Input stream (non-owning, set by interpret_line for runtime parsing words)
        std::istream* input_stream() const { return input_stream_; }
        void set_input_stream(std::istream* s) { input_stream_ = s; }

        // Interpreter access (non-owning, for file-load and similar)
        Interpreter* interpreter() const { return interpreter_; }
        void set_interpreter(Interpreter* i) { interpreter_ = i; }

        // LVFS access (non-owning, for cwd/cd/ls/ll/lr/cat primitives)
        etil::lvfs::Lvfs* lvfs() const { return lvfs_; }
        void set_lvfs(etil::lvfs::Lvfs* l) { lvfs_ = l; }

        // UvSession access (non-owning, for async file I/O primitives)
        etil::fileio::UvSession* uv_session() const { return uv_session_; }
        void set_uv_session(etil::fileio::UvSession* s) { uv_session_ = s; }

        // SelectionEngine access (non-owning, for evolutionary word selection)
        etil::selection::SelectionEngine* selection_engine() const { return selection_engine_; }
        void set_selection_engine(etil::selection::SelectionEngine* e) { selection_engine_ = e; }

        // HTTP client state (non-owning, for http-get primitive)
        etil::net::HttpClientState* http_client_state() const { return http_client_state_; }
        void set_http_client_state(etil::net::HttpClientState* s) { http_client_state_ = s; }

        // MongoDB client state (non-owning, for mongo-* primitives)
        etil::db::MongoClientState* mongo_client_state() const { return mongo_client_state_; }
        void set_mongo_client_state(etil::db::MongoClientState* s) { mongo_client_state_ = s; }

        // Role permissions (non-owning, nullptr = standalone = all permitted)
        const etil::mcp::RolePermissions* permissions() const { return permissions_; }
        void set_permissions(const etil::mcp::RolePermissions* p) { permissions_ = p; }

        // Output/error streams (non-owning, defaults to std::cout/std::cerr)
        std::ostream& out() { return *out_; }
        std::ostream& err() { return *err_; }
        void set_out(std::ostream* s) { out_ = s; }
        void set_err(std::ostream* s) { err_ = s; }

        // Data field registry for bounds-checked DataRef resolution
        DataFieldRegistry& data_field_registry() { return *data_field_registry_; }
        const DataFieldRegistry& data_field_registry() const { return *data_field_registry_; }
        std::shared_ptr<DataFieldRegistry> data_field_registry_ptr() { return data_field_registry_; }

        // Memory tracking
        size_t memory_used() const
        {
            return data_stack_.size() * sizeof(Value) +
                return_stack_.size() * sizeof(Value) +
                float_stack_.size() * sizeof(Value);
        }

        // --- Execution limits ---

        /// Called per instruction.  Returns false if any limit is hit.
        /// Amortises wall-clock check every ~16K instructions via mask.
        inline bool tick() {
            ++instructions_executed_;
            if (instructions_executed_ > instruction_budget_) return false;
            if (cancelled_.load(std::memory_order_relaxed)) return false;
            if ((instructions_executed_ & 0x3FFF) == 0) {
                if (std::chrono::steady_clock::now() >= deadline_) return false;
            }
            return true;
        }

        /// Set all execution limits and compute deadline from now.
        /// Resets instruction counter and call depth.
        void set_limits(uint64_t budget, size_t stack_depth,
                        size_t call_depth, double timeout_seconds) {
            instruction_budget_ = budget;
            instructions_executed_ = 0;
            max_stack_depth_ = stack_depth;
            max_call_depth_ = call_depth;
            call_depth_ = 0;
            cancelled_.store(false, std::memory_order_relaxed);
            deadline_ = std::chrono::steady_clock::now() +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(timeout_seconds));
        }

        /// Restore unlimited defaults.
        void reset_limits() {
            instruction_budget_ = UINT64_MAX;
            instructions_executed_ = 0;
            max_stack_depth_ = SIZE_MAX;
            max_call_depth_ = SIZE_MAX;
            call_depth_ = 0;
            cancelled_.store(false, std::memory_order_relaxed);
            deadline_ = std::chrono::steady_clock::time_point::max();
        }

        /// Set the cancellation flag (thread-safe, for external timeout).
        void cancel() { cancelled_.store(true, std::memory_order_relaxed); }

        /// Check the cancellation flag.
        bool is_cancelled() const {
            return cancelled_.load(std::memory_order_relaxed);
        }

        // --- Program-level abort ---

        /// Request program-level abort. Sets abort fields and cancels execution.
        void request_abort(bool success, std::string msg = {}) {
            abort_requested_ = true;
            abort_success_ = success;
            abort_error_message_ = std::move(msg);
            cancel();
        }

        /// Clear abort state (call after extracting abort info, before reset_limits).
        void clear_abort() {
            abort_requested_ = false;
            abort_success_ = true;
            abort_error_message_.clear();
        }

        /// Check whether an abort was requested.
        bool abort_requested() const { return abort_requested_; }

        /// True if abort was a success abort (program completed normally).
        bool abort_success() const { return abort_success_; }

        /// Error message from an error abort.
        const std::string& abort_error_message() const { return abort_error_message_; }

        /// Increment call depth.  Returns false if max exceeded.
        bool enter_call() {
            if (call_depth_ >= max_call_depth_) return false;
            ++call_depth_;
            return true;
        }

        /// Decrement call depth.
        void exit_call() {
            if (call_depth_ > 0) --call_depth_;
        }

        /// Check whether the data stack is within the configured limit.
        bool check_stack_depth() const {
            return data_stack_.size() < max_stack_depth_;
        }

        /// Number of instructions executed since last set_limits/reset.
        uint64_t instructions_executed() const { return instructions_executed_; }

        /// Get the current execution deadline.
        std::chrono::steady_clock::time_point deadline() const { return deadline_; }

        // Notification queue (for sys-notification primitive → MCP response)
        void queue_notification(std::string msg) { notifications_.push_back(std::move(msg)); }
        std::vector<std::string> drain_notifications() {
            std::vector<std::string> result;
            result.swap(notifications_);
            return result;
        }

        // Real-time notification sender (optional, set by MCP server)
        using NotificationSender = std::function<void(const std::string&)>;
        void set_notification_sender(NotificationSender sender) { notification_sender_ = std::move(sender); }
        void clear_notification_sender() { notification_sender_ = nullptr; }
        const NotificationSender& notification_sender() const { return notification_sender_; }

        // Targeted notification sender (optional, set by MCP server for user-notification)
        using TargetedNotificationSender = std::function<bool(const std::string& user_id, const std::string& msg)>;
        void set_targeted_notification_sender(TargetedNotificationSender sender) { targeted_notification_sender_ = std::move(sender); }
        void clear_targeted_notification_sender() { targeted_notification_sender_ = nullptr; }
        const TargetedNotificationSender& targeted_notification_sender() const { return targeted_notification_sender_; }

    private:
        uint32_t thread_id_;

        // Stacks
        ValueStack data_stack_;
        ValueStack return_stack_;
        ValueStack float_stack_;

        // Dictionary
        Dictionary* dictionary_ = nullptr;

        // Last CREATE word
        WordImpl* last_created_ = nullptr;

        // Input stream (for runtime parsing words like create)
        std::istream* input_stream_ = nullptr;

        // Interpreter (for file-load etc.)
        Interpreter* interpreter_ = nullptr;

        // LVFS (for cwd/cd/ls/ll/lr/cat primitives)
        etil::lvfs::Lvfs* lvfs_ = nullptr;

        // UvSession (for async file I/O primitives)
        etil::fileio::UvSession* uv_session_ = nullptr;

        // SelectionEngine (for evolutionary word selection)
        etil::selection::SelectionEngine* selection_engine_ = nullptr;

        // HTTP client state (for http-get primitive)
        etil::net::HttpClientState* http_client_state_ = nullptr;

        // MongoDB client state (for mongo-* primitives)
        etil::db::MongoClientState* mongo_client_state_ = nullptr;

        // Role permissions (nullptr = standalone = all permitted)
        const etil::mcp::RolePermissions* permissions_ = nullptr;

        // Output/error streams
        std::ostream* out_ = &std::cout;
        std::ostream* err_ = &std::cerr;

        // Data field registry (shared_ptr so ByteCode backpointers keep it alive)
        std::shared_ptr<DataFieldRegistry> data_field_registry_;

        // Hardware
        SIMDContext simd_;
        std::unique_ptr<GPUContext> gpu_;

        // Timing
        std::chrono::high_resolution_clock::time_point start_time_;

        // Execution limits (defaults = unlimited)
        uint64_t instruction_budget_ = UINT64_MAX;
        uint64_t instructions_executed_ = 0;
        size_t   max_stack_depth_ = SIZE_MAX;
        size_t   max_call_depth_ = SIZE_MAX;
        size_t   call_depth_ = 0;
        std::atomic<bool> cancelled_{false};
        std::chrono::steady_clock::time_point deadline_ =
            std::chrono::steady_clock::time_point::max();

        // Program-level abort state (survives reset_limits, cleared by clear_abort)
        bool abort_requested_{false};
        bool abort_success_{true};
        std::string abort_error_message_;

        // Notification queue
        std::vector<std::string> notifications_;

        // Real-time notification sender
        NotificationSender notification_sender_;

        // Targeted notification sender (for user-notification)
        TargetedNotificationSender targeted_notification_sender_;
    };
} // namespace etil::core
