# ETIL Architecture

## Overview

ETIL (Evolutionary Threaded Interpretive Language) reimagines FORTH for modern hardware by replacing the traditional linear dictionary with a dynamic DAG structure that enables evolutionary optimization and parallel execution.

## Core Concepts

### 1. Word as Concept

In traditional FORTH, a word name maps to a single implementation. In ETIL, a **word concept** can have multiple implementations:

```
Concept: SORT
├── Implementation 1: quicksort_avx2 (weight: 0.7)
├── Implementation 2: mergesort_parallel (weight: 0.2)
└── Implementation 3: radix_sort_integers (weight: 0.1)
```

The system selects implementations based on:
- Context (input size, type, available hardware)
- Historical performance
- Learned patterns

### 2. Dynamic DAG Dictionary

```
┌─────────────────────────────────────┐
│  EvolutionaryDictionary             │
│  ┌───────────────────────────────┐  │
│  │ Concept: "SORT"               │  │
│  │  - Type signature             │  │
│  │  - Selection model            │  │
│  │  - Implementations:           │  │
│  │    ├─ WordImpl #1 (gen 0)    │  │
│  │    ├─ WordImpl #2 (gen 1)    │  │
│  │    └─ WordImpl #3 (gen 2)    │  │
│  └───────────────────────────────┘  │
│                                     │
│  Dependency Graph (DAG)             │
│  PROCESS → VALIDATE → NORMALIZE     │
│         └→ COMPUTE                  │
└─────────────────────────────────────┘
```

## Component Architecture

### Core Execution Engine

```
ExecutionContext (Thread-Local)
├── Data Stack (Lock-Free)
├── Return Stack (Lock-Free)
├── Float Stack (Lock-Free)
├── SIMD Context
└── Profiler

ExecutionEngine
├── Thread Pool
├── JIT Compiler (LLVM)
├── Metrics Collector
└── execute_word()
    ├── Select implementation
    ├── JIT compile if needed
    ├── Execute native code
    └── Update metrics
```

### Selection Engine

```
SelectionEngine
├── Feature Extraction
│   ├── Stack depth
│   ├── Input types
│   ├── Available memory
│   └── Hardware capabilities
│
├── Decision Tree
│   └── predict(features) → impl_id
│
└── Multi-Armed Bandit
    ├── Epsilon-greedy selection
    └── Reward updating
```

### Evolution Engine

```
EvolutionEngine
├── Genetic Operators
│   ├── Mutation
│   │   ├── Inline frequent calls
│   │   ├── Vectorize loops
│   │   ├── Reorder operations
│   │   └── Specialize for types
│   │
│   └── Crossover
│       └── Combine successful patterns
│
├── Fitness Evaluation
│   ├── Execution time
│   ├── Memory usage
│   ├── Success rate
│   └── Correctness (tests)
│
└── Population Management
    ├── Selection (tournament)
    ├── Reproduction
    └── Pruning (keep best N)
```

## Data Flow

### 1. Word Execution

```
User Input: "42 DUP +"
     ↓
Tokenize
     ↓
For each token:
  ├─ Number? → Push to stack
  └─ Word? → execute_word()
              ├─ Dictionary lookup
              ├─ Select implementation
              │   ├─ Extract features
              │   ├─ Query selection model
              │   └─ Choose impl_id
              ├─ JIT compile (if needed)
              ├─ Execute native code
              └─ Update metrics
                  ├─ Performance profile
                  ├─ Selection model
                  └─ Evolution fitness
```

### 2. Evolution Cycle

```
Background Thread (every N minutes):
     ↓
For each concept:
  ├─ Get implementations
  ├─ Select top performers (tournament)
  ├─ Generate offspring
  │   ├─ Mutate parents
  │   └─ Crossover pairs
  ├─ Validate offspring (tests)
  ├─ JIT compile successful ones
  ├─ Add to dictionary
  └─ Prune worst performers
```

## Memory Management

### Reference Counting

```cpp
// Intrusive reference counting for WordImpl
class WordImpl {
    std::atomic<uint64_t> refcount_{1};
    
    void add_ref() { refcount_.fetch_add(1); }
    void release() {
        if (refcount_.fetch_sub(1) == 1) {
            delete this;
        }
    }
};

// Smart pointer wrapper
WordImplPtr ptr(new WordImpl(...));  // refcount = 1
WordImplPtr ptr2 = ptr;              // refcount = 2
// ptr2 destroyed                    // refcount = 1
// ptr destroyed                     // refcount = 0, deleted
```

### ValueStack

```cpp
// Simple vector-backed stack for single-threaded ExecutionContext use.
// Every ExecutionContext is accessed by only one thread at a time
// (MCP sessions are mutex-serialized, REPL is single-threaded).
class ValueStack {
    std::vector<Value> data_;

    void push(Value v)           { data_.push_back(std::move(v)); }
    std::optional<Value> pop();  // move from back, pop_back
    const Value& operator[](size_t i) const;  // O(1) indexed access
};
```

## Concurrency Model

### Thread Safety

1. **Dictionary Access**
   - Concurrent hashmap (TBB or Abseil)
   - Read-mostly workload
   - Lock-free for reads

2. **Execution Contexts**
   - Thread-local (no sharing)
   - Per-thread stacks
   - No synchronization needed

3. **Metrics Collection**
   - Atomic counters in WordImpl
   - Relaxed ordering (performance)
   - Eventual consistency

4. **Evolution**
   - Background thread
   - No interference with execution
   - Copy-on-write for updates

### Parallelization Opportunities

```
User Code:
  : PROCESS-BATCH ( array -- results )
    MAP-PARALLEL [ item -- result ]
      VALIDATE
      COMPUTE
      FORMAT
    ;
  ;

Execution:
  ├─ Thread 1: item[0] → VALIDATE → COMPUTE → FORMAT
  ├─ Thread 2: item[1] → VALIDATE → COMPUTE → FORMAT
  ├─ Thread 3: item[2] → VALIDATE → COMPUTE → FORMAT
  └─ Thread 4: item[3] → VALIDATE → COMPUTE → FORMAT
```

## JIT Compilation

### LLVM Integration

```
ByteCode                    LLVM IR                    Native Code
────────                    ───────                    ───────────
DUP        ─────→  %1 = load ptr, %stack     ─────→  mov rax, [rsp]
           generate    store %1, %stack              push rax
                       increment %stack
SWAP       ─────→  %1 = load ptr, %stack+0   ─────→  mov rax, [rsp]
           generate    %2 = load ptr, %stack+1       mov rbx, [rsp+8]
                       store %2, %stack+0            mov [rsp], rbx
                       store %1, %stack+1            mov [rsp+8], rax
```

### Hot Path Detection

```cpp
if (impl->profile().total_calls > HOT_THRESHOLD &&
    !impl->native_code()) {
    // Compile to native
    impl->set_native_code(
        jit.compile(impl->bytecode())
    );
}
```

## Type System

### Type Signatures

```cpp
struct TypeSignature {
    enum class Type {
        Unknown,
        Integer,  // i64
        Float,    // f64
        String,   // pointer
        Array,    // pointer + length
        Custom    // user-defined
    };
    
    std::vector<Type> inputs;
    std::vector<Type> outputs;
};

// Example: DUP ( a -- a a )
TypeSignature dup_sig = {
    .inputs = {Type::Unknown},
    .outputs = {Type::Unknown, Type::Unknown}
};
```

### Runtime Type Inference

```
Initial: ( ? ? ? )  ← Unknown types

42 →     ( i64 )
3.14 →   ( i64 f64 )
DUP →    ( i64 f64 f64 )  ← Type propagation

JIT can specialize:
  DUP-int64  → fast integer copy
  DUP-float  → fast float copy
```

## Performance Characteristics

### Time Complexity

| Operation | Complexity | Notes |
|-----------|-----------|-------|
| Stack push/pop | O(1) | Lock-free, constant time |
| Dictionary lookup | O(1) | Hash table |
| Implementation selection | O(log N) | Decision tree |
| Evolution (background) | O(N·M) | N = implementations, M = tests |

### Space Complexity

- **Per word concept**: O(I) where I = number of implementations
- **Per implementation**: Constant (metadata + code pointer)
- **Execution context**: O(S) where S = stack depth
- **Global dictionary**: O(W·I) where W = word concepts, I = avg implementations

### Scalability

- **Execution**: Scales linearly with CPU cores (thread-local contexts)
- **Evolution**: Embarrassingly parallel (test implementations independently)
- **Memory**: Grows with number of implementations (bounded by pruning)

## Extension Points

### Custom Word Implementations

```cpp
// User-defined implementation
class MySort : public WordImpl {
public:
    MySort() : WordImpl("my-sort", generate_id()) {
        set_signature({
            .inputs = {Type::Array},
            .outputs = {Type::Array}
        });
    }
    
    // Will be called by execution engine
    static bool execute(ExecutionContext* ctx) {
        // Custom sort logic
        return true;
    }
};

// Register with dictionary
dictionary.register_implementation("SORT",
    std::make_shared<MySort>()
);
```

### Custom Mutations

```cpp
class InlineMutation : public Mutation {
public:
    WordImplPtr mutate(const WordImpl& parent) override {
        // Analyze call graph
        auto callgraph = analyze(parent.bytecode());
        
        // Find hottest call
        auto hottest = find_hottest(callgraph);
        
        // Inline it
        auto new_bytecode = inline_call(
            parent.bytecode(),
            hottest
        );
        
        return create_child(parent, new_bytecode);
    }
};
```

## Future Directions

1. **Distributed Execution**
   - Shard dictionary across machines
   - RPC for remote word execution
   - Consistent hashing for load balancing

2. **GPU Offload**
   - CUDA/OpenCL for data-parallel words
   - Automatic CPU↔GPU transfer
   - Fused operations

3. **Persistent Storage**
   - Memory-mapped dictionary
   - Incremental evolution checkpoints
   - Cross-session learning

4. **Neural Selection**
   - Replace decision trees with neural networks
   - Learn from usage patterns
   - Transfer learning across deployments

## References

- **Lock-Free Programming**: Herlihy & Shavit, "The Art of Multiprocessor Programming"
- **LLVM**: Lattner & Adve, "LLVM: A Compilation Framework for Lifelong Program Analysis"
- **Genetic Programming**: Koza, "Genetic Programming: On the Programming of Computers by Means of Natural Selection"
- **Multi-Armed Bandits**: Sutton & Barto, "Reinforcement Learning: An Introduction"
