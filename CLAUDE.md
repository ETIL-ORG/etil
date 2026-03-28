# ETIL Project - Claude Code Integration Guide

## Project Overview

**Evolutionary Threaded Interpretive Language (ETIL)** is a modern reimagining of FORTH that replaces the traditional linear dictionary with a dynamic DAG structure featuring probability-distributed weighted word selection, evolutionary optimization, and JIT compilation.

### Design Decisions — Departures from Traditional FORTH

**No single-cell / double-cell distinction.** Traditional FORTH distinguishes single-cell (16- or 32-bit) and double-cell operations (e.g., `UM/MOD` vs `MOD`, `D+` vs `+`). ETIL eliminates this duplication entirely. All integer values are `int64_t` / `uint64_t` and all floating-point values are `double`. There is one set of arithmetic primitives that operates on these types. The old FORTH double-cell words (`UM/MOD`, `M*/`, `D+`, `2DUP`, etc.) have no ETIL equivalents — their functionality is the default behavior.

**TUI is the primary interactive interface.** The MCP Client TUI (separate `etil-tui` repository) replaces the C++ REPL as the recommended way to interact with ETIL interactively. It connects to the MCP server via Docker, provides a rich UI with JSON-RPC logging, notifications, and a full-screen help browser (F1). The C++ REPL remains available for pipe-mode and direct testing.

**REPL is an I/O channel, not an interpreter.** The REPL must NOT implement any language semantics — no parsing words (`create`, `does>`, `forget`, `:`, `;`), no compile mode, no control flow handling. The REPL is strictly an I/O channel that feeds text to the interpreter and displays output. All language semantics (parsing, compilation, interpretation, defining words) belong in the interpreter/outer interpreter layer. This separation ensures the same interpreter can be driven by files, network sockets, embedded APIs, or any other I/O channel — not just the interactive REPL.

### Security Rules

The project follows strict security rules documented in `docs/claude-design/20260214A-ETIL-Server-Security-Rules.md`. These rules are **ABSOLUTE** — no exceptions.

**Network Access**: Any executable or script that exposes an active listening network interface (including `localhost` and loopback interfaces) MUST run in a sandboxed environment that minimizes access to the host system.

**MCP Server**: Any ETIL executable or script exposing any MCP transport or interface (including STDIO) MUST run in a sandboxed environment. This requirement extends to **all testing code, scripts, and environments** that exercise MCP functionality. This slows down testing but the rules are absolute.

**Docker Optimization**: To speed testing in layered containers, optimize Docker image layering so files that change most often are in the last layers.

**General**: Always review design and code changes to avoid vulnerabilities allowing malicious (or careless) actors to gain entry to the system. When in doubt, ask.

### Core Innovation

Instead of FORTH's single implementation per word, ETIL allows multiple implementations per word concept with runtime selection based on:
- Performance profiling
- Machine learning (decision trees, multi-armed bandits)
- Genetic evolution (mutation, crossover)
- Context (input types, hardware availability)

### Technology Stack

- **Language**: C++20 with GNU C++ compiler
- **Build System**: CMake 3.20+ with Ninja
- **JIT Compilation**: LLVM 18+ (planned)
- **Concurrency**: Lock-free data structures, Intel TBB
- **Async I/O**: libuv 1.48 (thread-pool file I/O with cooperative await)
- **Libraries**: Abseil C++, spdlog, nlohmann/json, replxx (REPL line editing), jwt-cpp 0.7.2 (JWT authentication)
- **Testing**: Google Test, Google Benchmark
- **Target Platforms**: Linux (primary), macOS, Windows/WSL2

**C++20 constraints**: `std::expected` is not available (C++23). Use `std::optional` for fallible lookups and `bool` return values for primitive success/failure. The keyword `concept` is reserved — do not use it as an identifier.

---

## Project Status

### Completed

- Core data structures (WordImpl, TypeSignature, PerfProfile)
- ValueStack (vector-backed stack for single-threaded contexts)
- Execution context with multiple stacks, dictionary pointer, `last_created_` pointer, configurable output/error streams (`out_`/`err_` routed from Interpreter), and `DataFieldRegistry` for bounds-checked DataRef resolution
- SIMD/GPU detection
- CMake build system with dependency management
- Unit test framework
- Benchmark framework
- **Dictionary** — Thread-safe `Dictionary` class with `absl::Mutex` / `absl::flat_hash_map`, `forget_word()` (removes latest impl, reveals previous), `forget_all()` (removes entire concept). `lookup()` returns the most recently registered implementation ("latest wins").
- **57 primitive words** — Arithmetic, stack, comparison, logic, I/O, memory, math, and system (now 189 with heap, input-reading, dictionary-op, metadata, help, debug, datafield diagnostics, time, Julian Date, notification, PRNG, map, JSON, execution-token, library, defining-word, type-conversion, dictionary-checkpoint, file-I/O-sync, and async-file-I/O primitives)
- **Compiled word bodies** — `ByteCode` class with `Instruction` vector, inner interpreter (`execute_compiled`), per-word data field with lazy `DataFieldRegistry` registration, and shared_ptr-based registry backpointer for safe invalidation
- **Colon definitions** — `: name ... ;` compiles tokens into `ByteCode`, registered in dictionary
- **Control flow** — `if`/`else`/`then`, `do`/`loop`/`+loop`/`i`, `begin`/`until`, `begin`/`while`/`repeat`
- **Defining words** — `create`, `does>`, `,`, `@`, `!`, `allot`; self-hosted: `variable`, `constant`
- **Interpreter class** — `Interpreter` in core library handles all language semantics (colon definitions, compile mode, control flow compilation, CREATE/DOES>, forget, `."`, number parsing, word dispatch). Dual output streams: `out_` for normal output, `err_` for errors. REPL is a thin I/O shell. Handler logic extracted into 3 handler set classes (`InterpretHandlerSet`, `CompileHandlerSet`, `ControlFlowHandlerSet`) with a `HandlerSetBase` abstract base, decoupling word-specific logic from the Interpreter via dependency injection (no bidirectional dependency).
- **Interactive REPL** — Thin I/O channel with meta commands (`/help`, `/quit`, `/clear`, `/words`, `/history`, `/dark`, `/light`), color themes (`--color=auto|always|never`), `--quiet` pipe-friendly mode (suppresses banner/prompt/stack/goodbye, infers `--color=never`), dual-stream error separation (errors→stderr, output→stdout). **replxx integration** provides line editing (arrow keys, Home/End, Ctrl-A/E/K), persistent command history (`~/.etil/repl/history.txt`), and tab-completion of dictionary words and meta commands. Piped input and `--quiet` fall back to `std::getline`. Delegates all language semantics to `Interpreter`.
- **Word metadata system** — `MetadataMap` class for attaching key-value metadata (text, markdown, HTML, code, JSON, JSONL) to both `WordConcept` (shared across implementations) and `WordImpl` (per-implementation). Thread-safe concept-level access through `Dictionary` API. JSON serialization via nlohmann/json. Six self-hosted words: `meta!`, `meta@`, `meta-del`, `meta-keys`, `impl-meta!`, `impl-meta@` — defined in `data/builtins.til` as simple aliases for the C++ primitives (`dict-meta-set`, `dict-meta-get`, etc.). All are stack-based and use conditional returns (success=-1, failure=0).
- **Heap objects (strings, arrays, bytes)** — `HeapObject` base class with thread-safe intrusive reference counting. `HeapString` (immutable UTF-8, flexible array member), `HeapArray` (dynamic value array with bounds checking), `HeapByteArray` (raw byte buffer). Value remains POD; refcounts managed explicitly in primitives (`dup` addrefs, `drop` releases). 30 new primitive words for string/array/byte operations. `s"` parsing word creates heap strings in both interpret and compile mode (`PushString` opcode).
- **Self-hosting builtins** — 10 words (`variable`, `constant`, `forget`, `forget-all`, `meta!`, `meta@`, `meta-del`, `meta-keys`, `impl-meta!`, `impl-meta@`) reimplemented in ETIL source (`data/builtins.til`), loaded on startup via `load_startup_files()`. Backed by 12 new low-level primitives (`word-read`, `string-read-delim`, `dict-forget`, `dict-forget-all`, `file-load`, `include`, `dict-meta-set`, `dict-meta-get`, `dict-meta-del`, `dict-meta-keys`, `impl-meta-set`, `impl-meta-get`). `InterpretHandlerSet` reduced from 12 to 3 C++ handlers (`words`, `."`, `s"`).
- **TIL integration tests** — End-to-end tests that load `.til` files into the REPL. Shared test harness (`tests/til/harness.til`) with `expect-eq`, `pass`, `fail`. Auto-discovered by CTest via `test_*.sh` glob. 322 tests pass clean under ASan (313 unit + 9 TIL integration).
- **Async file I/O via libuv** — 13 async file I/O primitives (`read-file`, `write-file`, `exists?`, etc.) using libuv's thread-pool I/O with cooperative `await_completion()` that polls `uv_run(UV_RUN_NOWAIT)` + `ctx.tick()`. Cancellable via execution limits (calls `uv_cancel()` + drain). Per-session `UvSession` owns `uv_loop_t`, wired to `ExecutionContext` as non-owning pointer. Falls back to sync `std::filesystem`/`fstream` when `uv_session()` is null. Sync primitives (`read-file-sync`, etc.) also use libuv (synchronous mode with `callback=NULL` and `thread_local uv_loop_t`).

### Implemented Primitives (289 words)

| Category | Words |
|----------|-------|
| Arithmetic | `+` `-` `*` `/` `mod` `/mod` `negate` `abs` `max` `min` |
| Stack | `dup` `drop` `swap` `over` `rot` `pick` `nip` `tuck` `depth` `?dup` `roll` |
| Comparison | `=` `<>` `<` `>` `<=` `>=` `0=` `0<` `0>` |
| Logic | `true` `false` `not` `bool` `and` `or` `xor` `invert` `lshift` `rshift` `lroll` `rroll` |
| I/O | `.` `.s` `cr` `emit` `space` `spaces` `words` |
| Memory | `create` `,` `@` `!` `allot` `immediate` |
| Math | `sqrt` `sin` `cos` `tan` `tanh` `asin` `acos` `atan` `atan2` `log` `log2` `log10` `exp` `pow` `ceil` `floor` `round` `trunc` `fmin` `fmax` `pi` `f~` `random` `random-seed` `random-range` |
| String | `type` `s.` `s+` `s=` `s<>` `slength` `substr` `strim` `sfind` `sreplace` `ssplit` `sjoin` `sregex-find` `sregex-replace` `sregex-search` `sregex-match` `staint` |
| Array | `array-new` `array-push` `array-pop` `array-get` `array-set` `array-length` `array-shift` `array-unshift` `array-compact` `array-reverse` |
| ByteArray | `bytes-new` `bytes-get` `bytes-set` `bytes-length` `bytes-resize` `bytes->string` `string->bytes` |
| Map | `map-new` `map-set` `map-get` `map-remove` `map-length` `map-keys` `map-values` `map-has?` |
| JSON | `json-parse` `json-dump` `json-pretty` `json-get` `json-length` `json-type` `json-keys` `json->map` `json->array` `map->json` `array->json` `json->value` `mat->json` `json->mat` |
| LVFS | `cwd` `cd` `ls` `ll` `lr` `cat` |
| System | `sys-semver` `sys-timestamp` `sys-datafields` `sys-notification` `user-notification` `abort` |
| Time | `time-us` `us->iso` `us->iso-us` `us->jd` `jd->us` `us->mjd` `mjd->us` |
| Input Reading | `word-read` `string-read-delim` |
| Dictionary Ops | `dict-forget` `dict-forget-all` `file-load` `include` `library` `evaluate` `marker` `marker-restore` |
| Metadata Ops | `dict-meta-set` `dict-meta-get` `dict-meta-del` `dict-meta-keys` `impl-meta-set` `impl-meta-get` |
| Help | `help` |
| Execution | `'` `execute` `xt?` `>name` `xt-body` |
| Conversion | `int->float` `float->int` `number->string` `string->number` |
| Debug | `dump` `see` |
| Parsing (Interpreter) | `."` `s"` `:` `;` (handled in outer interpreter) |
| Self-hosted (builtins.til) | `variable` `constant` `forget` `forget-all` `meta!` `meta@` `meta-del` `meta-keys` `impl-meta!` `impl-meta@` `time-iso` `time-iso-us` `time-jd` `time-mjd` `1+` `1-` `-rot` |
| File I/O (async) | `exists?` `read-file` `write-file` `append-file` `copy-file` `rename-file` `lstat` `readdir` `mkdir` `mkdir-tmp` `rmdir` `rm` `truncate` |
| File I/O (sync) | `exists-sync` `read-file-sync` `write-file-sync` `append-file-sync` `copy-file-sync` `rename-sync` `lstat-sync` `readdir-sync` `mkdir-sync` `mkdir-tmp-sync` `rmdir-sync` `rm-sync` `truncate-sync` |
| HTTP Client | `http-get` `http-post` |
| Matrix | `mat-new` `mat-eye` `array->mat` `mat-diag` `mat-rand` `mat-get` `mat-set` `mat-rows` `mat-cols` `mat-row` `mat-col` `mat*` `mat+` `mat-` `mat-scale` `mat-transpose` `mat-solve` `mat-inv` `mat-det` `mat-eigen` `mat-svd` `mat-lstsq` `mat-norm` `mat-trace` `mat.` `mat-relu` `mat-sigmoid` `mat-tanh` `mat-relu'` `mat-sigmoid'` `mat-tanh'` `mat-hadamard` `mat-add-col` `mat-clip` `mat-randn` `mat-sum` `mat-col-sum` `mat-mean` `mat-softmax` `mat-cross-entropy` `mat-apply` `mat->array` |
| Observable | `obs-from` `obs-of` `obs-empty` `obs-range` `obs-map` `obs-map-with` `obs-filter` `obs-filter-with` `obs-scan` `obs-reduce` `obs-take` `obs-skip` `obs-distinct` `obs-merge` `obs-concat` `obs-zip` `obs-subscribe` `obs-to-array` `obs-count` `obs?` `obs-kind` |
| Observable (Temporal) | `obs-timer` `obs-delay` `obs-timeout` `obs-debounce-time` `obs-throttle-time` `obs-buffer-time` `obs-timestamp` `obs-time-interval` `obs-audit-time` `obs-sample-time` `obs-take-until-time` `obs-delay-each` `obs-retry-delay` |
| Array Iteration | `array-each` `array-map` `array-filter` `array-reduce` |
| MongoDB | `mongo-find` `mongo-count` `mongo-insert` `mongo-update` `mongo-delete` |
| Selection | `select-strategy` `select-epsilon` `select-off` |
| Evolution | `evolve-register` `evolve-register-pool` `evolve-word` `evolve-all` `evolve-status` `evolve-tag` `evolve-untag` `evolve-bridge` `evolve-fitness-mode` `evolve-fitness-alpha` `evolve-instruction-budget` `evolve-mutation-weights` |
| Evolution Logging | `evolve-log-start` `evolve-log-stop` `evolve-log-dir` `evolve-log-show-failed` |
| Control (compile-only) | `if` `else` `then` `do` `loop` `+loop` `i` `j` `begin` `until` `while` `repeat` `again` `>r` `r>` `r@` `leave` `exit` `recurse` `does>` `[']` |
| Self-hosted (help.til) | `mat-xavier` `mat-he` `mat-mse` |
| MLP (library/mlp.til) | `make-layer` `make-network` `forward` `forward-cache` `backward` `sgd-update` `train-step` `train` `predict` |

### In Progress / Planned

1. **JIT Compiler** — LLVM integration, IR generation, code caching
3. **JIT Compiler** — LLVM integration, IR generation, code caching
4. **Evolution Engine** — Genetic operators, fitness evaluation, population management

---

## Coding Standards

### C++ Style

```cpp
// Namespace: etil::<component>
namespace etil::core {

// Class names: PascalCase
class ExecutionEngine {
public:
    // Public methods: snake_case
    void execute_word(const std::string& word, ExecutionContext& ctx);

    // Getters: no "get_" prefix
    size_t word_count() const { return word_count_; }

private:
    // Private members: snake_case with trailing underscore
    size_t word_count_;
    std::atomic<uint64_t> total_executions_{0};
};

// Type aliases: PascalCase or using directive
using ImplId = uint64_t;

// Constants: SCREAMING_SNAKE_CASE or constexpr
constexpr size_t MAX_STACK_DEPTH = 1024;
constexpr double DEFAULT_WEIGHT = 1.0;

// Enums: PascalCase for enum, PascalCase for values
enum class SelectionStrategy {
    Random,
    GreedyBest,
    EpsilonGreedy,
    UCB1
};

} // namespace etil::core
```

### Modern C++ Practices

```cpp
// C++20 — do NOT use C++23 features (std::expected, std::print, etc.)
// Use std::optional for fallible lookups
std::optional<WordImplPtr> lookup(const std::string& word) const;

// Primitives return bool: true = success, false = error (underflow, etc.)
bool prim_add(ExecutionContext& ctx);

// Use auto where type is obvious
auto result = dict.lookup("dup");

// Range-based for loops
for (const auto& impl : implementations) {
    process(impl);
}

// Intrusive reference counting via WordImplPtr (not std::shared_ptr)
WordImplPtr impl = make_intrusive<WordImpl>("test", id);

// Atomic operations with explicit memory ordering
count_.fetch_add(1, std::memory_order_relaxed);
```

### Concurrency Guidelines

```cpp
// Thread-local for execution contexts (no synchronization needed)
thread_local ExecutionContext ctx(get_thread_id());

// Atomic for counters and flags
std::atomic<uint64_t> total_calls{0};
std::atomic<bool> should_evolve{true};

// Lock-free data structures preferred (stacks are now vector-backed)
ValueStack stack;

// For dictionary: concurrent hashmap (TBB or Abseil)
absl::flat_hash_map<std::string, WordConcept> concepts_;
absl::Mutex concepts_mutex_; // Only when lock-free not possible

// RAII for locks
absl::MutexLock lock(&concepts_mutex_);

// Prefer relaxed ordering for performance counters
// Prefer acquire/release for synchronization
// Prefer seq_cst only when necessary
```

### Error Handling

```cpp
// Primitives: return false on error, restore stack on partial pop failure
bool prim_add(ExecutionContext& ctx) {
    auto b = ctx.data_stack().pop();
    if (!b) return false;  // underflow
    auto a = ctx.data_stack().pop();
    if (!a) { ctx.data_stack().push(*b); return false; }  // restore
    ctx.data_stack().push(result);
    return true;
}

// Dictionary lookups: return std::optional (nullopt if not found)
auto impl = dict.lookup(word);
if (!impl) { /* word not found */ }

// Use assertions for programmer errors
assert(impl_id != 0 && "Implementation ID cannot be zero");

// Log errors with spdlog
spdlog::error("Failed to compile bytecode for word '{}'", name);
spdlog::warn("No implementation found for word '{}', using default", word);
```

### Documentation

```cpp
/// Brief description of the class
///
/// Longer description explaining the purpose, usage patterns,
/// and any important invariants.
///
/// Example:
/// ```cpp
/// ExecutionEngine engine(&dictionary);
/// auto result = engine.execute_word("DUP", ctx);
/// ```
class ExecutionEngine {
public:
    /// Execute a word by concept name
    ///
    /// @param concept The word concept to execute (e.g., "DUP", "SORT")
    /// @param ctx The execution context containing stacks and state
    /// @return ExecutionResult with success status and metrics
    ///
    /// This method:
    /// 1. Looks up the concept in the dictionary
    /// 2. Selects the best implementation
    /// 3. JIT compiles if needed
    /// 4. Executes and collects metrics
    ExecutionResult execute_word(
        const std::string& concept,
        ExecutionContext& ctx
    );
};
```

---

## Architecture Deep Dive

### Key Data Structures

#### 1. WordImpl (Already Implemented)

```cpp
// Each word concept can have multiple implementations
WordImpl impl("quicksort_avx2", 101);
impl.set_generation(3);  // Evolved from generation 2
impl.set_weight(0.85);   // 85% selection probability
impl.add_parent(95);     // Parent implementation ID

// Track performance
impl.record_execution(
    std::chrono::nanoseconds(1500),
    4096,  // bytes
    true   // success
);

// Type signature
TypeSignature sig;
sig.inputs = {Type::Array};
sig.outputs = {Type::Array};
impl.set_signature(sig);
```

#### 2. HeapObject System (Implemented)

```cpp
// HeapObject: base class with intrusive refcounting (same as WordImpl)
// Subclasses: HeapString, HeapArray, HeapByteArray
auto* hs = HeapString::create("hello");  // refcount = 1
Value v = make_heap_value(hs);           // v.type = String, v.as_ptr = hs

// Value stays POD — refcounts managed explicitly:
value_addref(v);   // increment if heap type
value_release(v);  // decrement, delete if zero

// Refcount rules for primitives:
// - pop() gives caller 1 ref. Discard without re-push → value_release()
// - push() of newly-created HeapObject (refcount=1) is correct as-is
// - dup: value_addref() the copied value (1 value, 2 stack slots = 2 refs)
// - drop: value_release() the popped value
// - swap: refcount-neutral (2 pops + 2 pushes of same values)
// - over: value_addref() the duplicated value (2 pops + 3 pushes)
// - rot: refcount-neutral (3 pops + 3 pushes)

// HeapString: immutable, flexible array member (single allocation)
auto* s = HeapString::create("hello");
s->view();    // std::string_view
s->c_str();   // null-terminated
s->length();  // byte length

// HeapArray: dynamic vector of Values, bounds-checked
auto* arr = new HeapArray();
arr->push_back(Value(int64_t(42)));
arr->push_back(make_heap_value(HeapString::create("hi")));
// Destructor releases all heap-valued elements

// HeapByteArray: raw byte buffer for I/O
auto* ba = new HeapByteArray(256);
ba->set(0, 0xFF);
ba->resize(512);
```

#### 3. ExecutionContext (Implemented)

```cpp
// Thread-local execution environment
ExecutionContext ctx(thread_id);

// Stacks (vector-backed)
ctx.data_stack().push(Value(42));
auto val = ctx.data_stack().pop();

// Dictionary access (set once at initialization)
ctx.set_dictionary(&dict);
auto* d = ctx.dictionary();  // used by prim_words() etc.

// Last-created word tracking (for CREATE/DOES>, comma, allot)
ctx.set_last_created(impl);
auto* w = ctx.last_created();

// Output/error stream routing (defaults to std::cout/std::cerr)
// Interpreter wires its own streams so MCP can capture output
ctx.set_out(&my_ostream);
ctx.set_err(&my_errstream);
ctx.out() << "hello";  // used by all I/O primitives

// Hardware detection
if (ctx.simd().avx512_available) {
    // Use AVX-512 implementation
}
```

#### 4. ByteCode & Compiled Words (Implemented)

```cpp
// Instruction opcodes
Instruction::Op::Call          // Execute word by name (caches WordImplPtr)
Instruction::Op::PushInt       // Push integer literal
Instruction::Op::PushFloat     // Push float literal
Instruction::Op::Branch        // Unconditional jump
Instruction::Op::BranchIfFalse // Pop boolean, jump if false
Instruction::Op::PushBool      // Push boolean literal
Instruction::Op::DoSetup       // Pop limit and index for DO loop
Instruction::Op::DoLoop        // Increment index, branch if < limit
Instruction::Op::DoPlusLoop    // Pop increment, add to index, check boundary
Instruction::Op::DoI           // Push current loop index
Instruction::Op::PrintString   // Print string literal (for .")
Instruction::Op::PushDataPtr   // Push pointer to data field (CREATE)
Instruction::Op::SetDoes       // Set does>-body on last CREATE'd word

// ByteCode contains instruction vector + per-word data field
ByteCode code;
code.append(instruction);
code.backpatch(offset, target);
code.data_field();  // std::vector<Value> for CREATE'd words

// Inner interpreter
bool execute_compiled(ByteCode& code, ExecutionContext& ctx);
```

#### 5. Dictionary (Implemented)

```cpp
class Dictionary {
public:
    void register_word(const std::string& word, WordImplPtr impl);
    std::optional<WordImplPtr> lookup(const std::string& word) const;
    std::optional<std::vector<WordImplPtr>> get_implementations(const std::string& word) const;
    std::vector<std::string> word_names() const;
    size_t concept_count() const;
    static uint64_t next_id();

    // Concept-level metadata
    bool set_concept_metadata(word, key, format, content);
    std::optional<MetadataEntry> get_concept_metadata(word, key) const;
    bool remove_concept_metadata(word, key);
    std::vector<std::string> concept_metadata_keys(word) const;

private:
    mutable absl::Mutex mutex_;
    absl::flat_hash_map<std::string, WordConcept> concepts_ ABSL_GUARDED_BY(mutex_);
    static std::atomic<uint64_t> next_id_;
};
```

### Execution Flow

#### Interpret Mode
```
User: "42 dup +"

Tokenize → ["42", "dup", "+"]

For "42":
  std::stoll() → 42
  ctx.data_stack().push(Value(42))

For "dup":
  dict.lookup("dup") → WordImplPtr
  impl->native_code()(&ctx)  → prim_dup() executes

For "+":
  dict.lookup("+") → WordImplPtr
  impl->native_code()(&ctx)  → prim_add() executes

REPL prints: (1) 84
```

#### Compile Mode
```
User: ": double dup + ;"

':' → enter compile mode, read name "double"
"dup" → emit Instruction{Op::Call, word_name="dup"}
"+"   → emit Instruction{Op::Call, word_name="+"}
';'   → finalize: create WordImpl with ByteCode, register in dictionary

User: "21 double"

"21"     → push Value(21)
"double" → dict.lookup("double") → has bytecode
           execute_compiled(bytecode, ctx) → inner interpreter runs
           ip=0: Call "dup" → prim_dup()
           ip=1: Call "+"   → prim_add()

REPL prints: (1) 42
```

### Primitive Implementation Pattern

All primitives follow `bool func(ExecutionContext& ctx)`:
```cpp
bool prim_example(ExecutionContext& ctx) {
    auto val = ctx.data_stack().pop();
    if (!val) return false;  // underflow
    // ... operate on val ...
    ctx.data_stack().push(result);
    return true;
}
```

Key helpers in `primitives.cpp`:
- `pop_two()` — binary op helper with underflow restoration
- `arith_binary()` — template for `+`, `-`, `*` with type promotion
- `pop_for_division()` — division helper with zero-divisor check
- `compare_binary()` — template for comparisons returning Boolean values
- `pop_as_double()` — unary math helper (promotes int to double)
- `pop_two_as_double()` — binary math helper (promotes both to double)

Type promotion: Int op Int → Int, any Float → Float.
Booleans: Comparisons, predicates, and logical ops return `Value(bool)`. Control flow (`if`/`while`/`until`) requires Boolean on TOS. Arithmetic rejects Boolean operands.

---

## Implementation Priorities

### Phase 1: Basic Execution — COMPLETE

- Dictionary with thread-safe lookup, registration, and `forget_word()`
- 57 primitive words (arithmetic, stack, comparison, logic, I/O, memory, math, system)
- Interactive REPL with meta commands, comments
- Full test coverage (120+ unit tests)

### Phase 2: Compiled Words & Control Flow — COMPLETE

- Colon definitions (`: name ... ;`) with `ByteCode` compilation
- Control flow: `if`/`else`/`then`, `do`/`loop`/`+loop`/`i`, `begin`/`until`, `begin`/`while`/`repeat`
- Defining words: `create`, `does>`, `,`, `@`, `!`, `allot`
- Inner interpreter (`execute_compiled`) with instruction pointer and local return stack
- `forget` for removing words from dictionary
- 19 math primitives (`sqrt`, `sin`, `cos`, `tan`, trig inverses, `log`, `exp`, `pow`, etc.)
- `Interpreter` class extracted from REPL into core library (all language semantics in `interpreter.hpp`/`.cpp`)
- REPL refactored to thin I/O shell (~90 lines)
- Fixed `Instruction::cached_impl` reference cycle (was `WordImplPtr`, now non-owning `WordImpl*`)
- 153 tests pass clean under ASan (31 new interpreter tests)
- **Word metadata** — `MetadataMap` with `MetadataEntry` (key, format, content), integrated into `WordImpl` and `WordConcept`, concept-level methods on `Dictionary`, JSON serialization via nlohmann/json, 6 interpreter words (`meta!`, `meta@`, `meta-del`, `meta-keys`, `impl-meta!`, `impl-meta@`)
- 188 tests pass clean under ASan (25 new metadata tests)

### Phase 3: Heap Objects & Strings — COMPLETE

- **HeapObject base** — Thread-safe intrusive refcounting (same pattern as WordImpl), `Kind` enum for type dispatch
- **HeapString** — Immutable UTF-8 strings with flexible array member (single allocation), `s"` parsing word in interpret + compile mode (`PushString` opcode)
- **HeapArray** — Dynamic value array with bounds-checked get/set, push/pop, shift/unshift, destructor releases heap elements
- **HeapByteArray** — Raw byte buffer with bounds-checked access, resize, conversion to/from HeapString
- **31 new primitive words** — String (14): `type`, `s.`, `s+`, `s=`, `s<>`, `slength`, `substr`, `strim`, `sfind`, `sreplace`, `ssplit`, `sjoin`, `sregex-find`, `sregex-replace`; Array (10): `array-new`, `array-push`, `array-pop`, `array-get`, `array-set`, `array-length`, `array-shift`, `array-unshift`, `array-compact`, `array-reverse`; Byte (7): `bytes-new`, `bytes-get`, `bytes-set`, `bytes-length`, `bytes-resize`, `bytes->string`, `string->bytes`
- **System primitives** — `sys-semver` (print project SEMVER), `sys-timestamp` (print build timestamp). Build-time `version.hpp` generated by CMake custom target from `version.hpp.in`
- **Refcount management** in `dup` (addref), `drop` (release), `over` (addref), `.` (release); swap/rot are refcount-neutral
- **Value::Type extended** — `Array`, `ByteArray` added; `format_value()` updated for all heap types
- Regex primitives use `<regex>` (C++ standard library)
- 256 unit tests pass clean under ASan (68 new heap/string/array/byte tests)
- Version bumped to v0.4.0

### Phase 3b: TIL Integration Tests — COMPLETE

- **Test harness** (`tests/til/harness.til`) — Shared `expect-eq`, `pass`, `fail` words
- **CREATE test** (`tests/til/test_create.til`) — Tests `create`/`does>` inside compiled words (`my-constant`, `plus-one-constant`)
- **Bash launcher** (`tests/til/test_create.sh`) — Runs REPL with heredoc input, greps for FAIL
- **CTest integration** — `test_*.sh` auto-discovered via `file(GLOB)`, `ETIL_REPL` env var passes binary path
- 263 tests pass clean under ASan (256 unit + 7 TIL integration)

### Phase 3c: String TIL Integration Tests — COMPLETE

- **5 test suites** in `tests/til/string/` covering all 14 string primitives + `s"` parsing
  - `test_string_basic` (17 tests) — `s"`, `slength`, `s+`, `s=`, `s<>`
  - `test_string_substr_trim` (16 tests) — `substr`, `strim` with edge cases
  - `test_string_search` (15 tests) — `sfind`, `sreplace` with boundary conditions
  - `test_string_split_join` (11 tests) — `ssplit`, `sjoin`, round-trip tests
  - `test_string_regex` (12 tests) — `sregex-find`, `sregex-replace` with patterns
- **Interpreter shutdown** — `Interpreter::shutdown()` drains the data stack with `value_release()` on heap objects; called by the REPL before exit. Eliminates false ASan leak reports from stack contents at exit. Launchers use strict `set -euo pipefail` — ASan exit codes propagate naturally for real leak detection.
- **Lessons learned**: `#` is a token-level line comment (handled in the outer interpreter's token loop, not via pre-processing)

### Phase 4: REPL Line Editing, History & Dictionary Fixes — COMPLETE

- **replxx integration** — Line editing (arrow keys, Home/End, Ctrl-A/E/K), persistent history (`~/.etil/repl/history.txt`), tab-completion of dictionary words + meta commands. BSD-3-Clause, fetched via FetchContent (guarded by `ETIL_BUILD_EXAMPLES`). Only used in interactive non-quiet mode; piped input falls back to `std::getline`.
- **`completable_words()` method** — Returns sorted vector of all dictionary + interpreter + compile-only words. Used for tab-completion and `/words`. Refactored `print_all_words()` to use it.
- **`/history` meta command** — Prints command history via `replxx::Replxx::history_scan()`.
- **Dictionary lookup fix** — `lookup()` now returns the most recently registered implementation (`.back()`) instead of the first (`[0]`). Matches FORTH "latest definition wins" shadowing semantics.
- **`forget` semantics fix** — `forget_word()` now removes only the latest implementation, revealing the previous one. If it was the last implementation, the concept is erased.
- **`forget-all` word** — New interpreter parsing word. `forget_all()` removes entire concept with all implementations (old `forget_word` behavior).
- **`ReplConfig::history_file`** — Optional JSON config override for history file path.
- **Compile-time word check** — `compile_token()` now rejects unknown words during colon definitions (with self-reference exemption for recursion), preventing silent compilation of typos.
- 269 tests pass clean under ASan (262 unit + 7 TIL integration)
- **Handler set unit tests** — Direct tests for the 3 handler set classes (`InterpretHandlerSet`, `CompileHandlerSet`, `ControlFlowHandlerSet`), exercising dispatch, bytecode generation, control-flow backpatching, error paths, and callback invocation in isolation. 335 tests pass clean under ASan (325 unit + 10 TIL integration).

### Phase 4c: Self-Hosting Builtins — COMPLETE

- **12 new primitives** — `word-read`, `string-read-delim` (input reading); `dict-forget`, `dict-forget-all`, `file-load`, `include` (dictionary ops); `dict-meta-set`, `dict-meta-get`, `dict-meta-del`, `dict-meta-keys`, `impl-meta-set`, `impl-meta-get` (metadata ops). All follow `bool prim_xxx(void* ctx)` pattern.
- **Self-hosted words** — 8 words implemented in ETIL source (`data/builtins.til`): `forget`, `forget-all` (use `word-read` for token parsing), `meta!`, `meta@`, `meta-del`, `meta-keys`, `impl-meta!`, `impl-meta@` (simple aliases for the C++ stack-based primitives). `true`, `false`, `not` moved to C++ primitives in v0.7.5.
- **Startup file loading** — `Interpreter::load_startup_files()` with tilde expansion. REPL loads `startup-files` from `repl.json` config.
- **`include` as primitive** — Kept as a primitive (not self-hosted) to avoid bootstrapping issues: builtins.til itself must be loaded via `include`.
- **Interpreter pointer** — `ExecutionContext` gains non-owning `Interpreter*` for `file-load` and `include` primitives.
- **InterpretHandlerSet reduced** — From 12 to 3 C++ handlers (`words`, `."`, `s"`). 9 handlers replaced by self-hosted .til definitions.
- **`string-read-delim` semantics** — Skips to opening delimiter, then reads until closing delimiter. Supports `"content"` quoting pattern.
- 322 tests pass clean under ASan (313 unit + 9 TIL integration)

### Phase 4d: Built-In Help System — COMPLETE

- **`help` primitive** — Parsing word that reads next token from input, searches two sources: concept-level metadata (from `data/help.til` via `meta!`) and impl-level metadata fallback. Handler words (`:`, `;`, `does>`, `if`/`else`/`then`, `do`/`loop`/`+loop`/`i`, `begin`/`until`/`while`/`repeat`, `."`, `s"`, etc.) are registered as dictionary concepts with empty implementations via `register_handler_words()`, so their help metadata is attached via `meta!` in `help.til` like any other word. Output format: word name, description, stack effect, category.
- **`data/help.til`** — All help text for ~109 dictionary words via stack-based `meta!` (description, stack-effect, category, examples). Loaded on startup after `builtins.til`. Editable without recompiling.
- **REPL `/help [word]`** — Extended to accept optional word argument; delegates to `help` primitive via `interp.interpret_line()`. Without argument, shows meta-command list.
- **Default startup files** — REPL loads `data/builtins.til` and `data/help.til` by default when no config specifies startup files.
- 322 tests pass clean under ASan (313 unit + 9 TIL integration)

### Phase 4e: MCP Server — COMPLETE

- **MCP Server** — Model Context Protocol server enabling programmatic access to the ETIL interpreter, dictionary, and evolutionary features via structured JSON-RPC 2.0. HTTP transport architecture: `McpServer` handles protocol logic, `HttpTransport` handles I/O.
- **`etil_mcp` library** — Separate library linking to `etil` core + `nlohmann_json`. MCP dependencies don't leak into the base library.
- **JSON-RPC 2.0** — Full request parsing/validation (`JsonRpcRequest`, `parse_request()`), response/error formatting (`make_response()`, `make_error()`), with standard error codes (ParseError, InvalidRequest, MethodNotFound, InvalidParams, InternalError).
- **HTTP Transport** — MCP Streamable HTTP with real-time SSE streaming. cpp-httplib server on configurable host:port. POST `/mcp` uses chunked transfer encoding (`set_chunked_content_provider`) to stream SSE events in real-time — notifications are written directly to the socket via a `thread_local DataSink*` as they occur, followed by the response event. GET `/mcp` returns 405; DELETE `/mcp` terminates session. Session management via `Mcp-Session-Id` header (UUID v4). API key auth via `Authorization: Bearer <key>` (from `ETIL_MCP_API_KEY` env var). Origin validation for DNS rebinding protection. `std::mutex` serializes all handler calls (cpp-httplib thread pool → single-threaded interpreter). HTTP transport is built unconditionally (cpp-httplib always linked via FetchContent).
- **10 MCP Tools** — `interpret` (execute TIL code), `list_words` (dictionary listing with category filter), `get_word_info` (full introspection: metadata, implementations, profile, signature), `get_stack` (typed stack elements), `set_weight` (evolutionary weight setting), `reset` (clear state, reload startup files), `get_session_stats` (per-session CPU time, memory, and interpreter metrics), `write_file` (upload file to session home), `list_files` (list session home files), `read_file` (download file from session home).
- **4 MCP Resources** — `etil://dictionary` (all words with descriptions and impl counts), `etil://word/{name}` (full word details via `word_concept_to_json()`), `etil://stack` (current stack snapshot), `etil://session/stats` (per-session profiling counters).
- **Docker sandbox** — Multi-stage Dockerfile (builder → minimal runtime), layer-optimized for fast rebuilds. `docker-compose.yml` with security hardening (`read_only`, `no-new-privileges`). Per security rules, ALL MCP transports must run inside Docker.
- **ap1000 deployment** — nginx reverse proxy for TLS termination + API key check + rate limiting (30 req/min). Replaces iptables port redirects, also proxies wxlog traffic. Production docker-compose, deploy script with smoke test, iptables cleanup script.
- **Security rules** — Added to CLAUDE.md: network access sandboxing, MCP transport sandboxing (including STDIO), Docker optimization guidance.
- 405 tests pass clean under ASan (313 unit + 82 MCP + 10 TIL integration)
- Version bumped to v0.5.0

### Phase 4h: MCP Session Profiling — COMPLETE

- **SessionStats** — Per-session CPU and memory profiling struct in `mcp_server.hpp`. Tracks `interpretCallCount`, cumulative wall/CPU time, current/peak RSS, dictionary concept count, stack depth, session start time and uptime. Plain `uint64_t` fields (no atomics — MCP calls are mutex-serialized).
- **CPU timing** — `clock_gettime(CLOCK_PROCESS_CPUTIME_ID)` for process CPU time; `chrono::steady_clock` for wall time. Both accumulated across `tool_interpret()` calls.
- **RSS tracking** — Reads `/proc/self/statm` after each interpret call, maintains peak high-water mark.
- **Tool #7 `get_session_stats`** — Returns all profiling counters with derived fields (Ms, Mb, uptime).
- **Resource #4 `etil://session/stats`** — Same data exposed as MCP resource.
- **Reset integration** — `stats_.reset()` called in constructor and `tool_reset()`, zeroing all counters and recording new session start time.
- 430 tests pass clean under ASan (326 unit + 82 MCP + 12 HTTP + 10 TIL integration)
- Version bumped to v0.5.4

### Phase 4f: MCP HTTP Transport — COMPLETE

- **HTTP Transport** — `HttpTransport` class. Streamable HTTP with batch SSE for notifications. POST/GET/DELETE handlers, UUID v4 session management, API key auth, origin validation, mutex-serialized handler calls. Uses cpp-httplib (FetchContent, always linked).
- **Executable wiring** — `mcp_server.cpp` supports `--host`, `--port`, `ETIL_MCP_API_KEY` env var.
- **ap1000 deployment** — nginx config (TLS termination, API key check, rate limiting 30/min, CORS, wxlog proxying), production docker-compose, deploy script, iptables cleanup script. Replaces iptables port redirects for wxlog. Cert at `/etc/letsencrypt/live/www.alphapulsar.com/`.
- **Dockerfile Linode fix** — Ubuntu 24.04 uses DEB822 format (`/etc/apt/sources.list.d/ubuntu.sources`), not legacy `sources.list`. Dockerfile uses `ARG APT_MIRROR=mirrors.linode.com` with sed on the DEB822 file for both builder and runtime stages. Linode hosts cannot reach `archive.ubuntu.com`.
- **ap1000 deployment verified** — Docker image built locally and transferred via `docker save | gzip | scp | docker load` (ap1000 has only 961MB RAM, insufficient for Docker builds). Full public endpoint test passed: initialize, tools/list, interpret, auth enforcement, session management.
- 417 tests pass clean under ASan (313 unit + 82 MCP + 12 HTTP + 10 TIL integration)
- Version bumped to v0.5.2

### Phase 4g: MCP Output Capture Fix — COMPLETE

- **ExecutionContext streams** — Added `out_`/`err_` stream pointers (default `std::cout`/`std::cerr`) with accessors (`out()`, `err()`) and setters (`set_out()`, `set_err()`). Wired from `Interpreter` constructor so MCP server's `ostringstream` buffers are used by all I/O paths.
- **Primitive I/O routing** — All I/O primitives (`.`, `cr`, `emit`, `space`, `spaces`, `words`, `sys-semver`, `sys-timestamp`, `help`, `dump`, `see`, `type`) and the `PrintString` compiled opcode now write to `ec->out()`/`ctx.out()` instead of `std::cout`. Error messages in `compiled_body.cpp` route through `ctx.err()`.
- **MCP interpret fix** — The `interpret` tool now returns captured output (e.g., `42 . cr` → `output: "42 \n"`) instead of empty string.
- 423 tests pass clean under ASan (319 unit + 82 MCP + 12 HTTP + 10 TIL integration)
- Version bumped to v0.5.3

### Phase 4i: Heap Refcount Fixes & String Coercion — COMPLETE

- **`@` (prim_fetch) refcount fix** — `value_addref()` when copying heap values from data field to stack. Previously both locations shared a single ref, causing use-after-free when either was released (e.g., `CREATE`d words with heap objects accessed via `does> @`).
- **`!` (prim_store) refcount fix** — `value_release()` on old data field value before overwriting. Previously the old heap value was silently overwritten, leaking its reference.
- **ByteCode destructor** — Releases all heap values in `data_field_` on destruction. Previously `Value` objects were destroyed as POD without releasing heap references, leaking `CREATE`d heap objects.
- **`s+` auto-conversion** — `pop_as_string()` helper coerces non-string types before concatenation: Integer via `std::to_string`, Float via `ostringstream`, Array as `"array(N)"`, ByteArray as `"bytearray(N)"`, Pointer as `"pointer"`. Enables idiomatic `42 s" hello" s+` → `"42hello"`.
- **mjd-test-01.til** — Fixed test file to use non-destructive `pointx`/`pointy` for repeated access, reserving destructive `point@` (via `array-shift`) for consuming reads through `p.`.
- 435 tests pass clean under ASan (331 unit + 82 MCP + 12 HTTP + 10 TIL integration) → 438 after DataRef changes
- Version bumped to v0.5.5 → v0.5.6 after DataRef changes

### Phase 4j: DataRef Capability Model — COMPLETE

- **DataRef replaces raw pointers** — `Value::Type::Pointer` replaced with `Value::Type::DataRef`. DataRef packs a registry index (high 32 bits) + slot offset (low 32 bits) into `as_int`, keeping Value POD at 16 bytes. Helpers: `make_dataref()`, `dataref_index()`, `dataref_offset()`.
- **DataFieldRegistry** — New class in `execution_context.hpp`: `vector<vector<Value>*>` with O(1) index lookup. Methods: `register_field()`, `resolve()` (returns nullptr for invalidated/out-of-range), `invalidate()`, `update()`, `entry_count()`, `live_count()`, `total_cells()`. Stored as `shared_ptr<DataFieldRegistry>` in `ExecutionContext` so ByteCode backpointers keep it alive past context destruction.
- **Lazy registration** — `PushDataPtr` opcode registers data field on first execution via `ctx.data_field_registry_ptr()`, caches index in `ByteCode`. Subsequent executions reuse the cached index.
- **Bounds-checked `@` and `!`** — `prim_fetch` and `prim_store` now resolve DataRef through registry, bounds-check offset against field size, and properly manage refcounts (`value_addref` on fetch, `value_release` on store overwrite).
- **Safe invalidation** — `ByteCode` destructor invalidates its registry entry via shared_ptr backpointer. Stale DataRef values fail gracefully (resolve returns nullptr).
- **`does>` registry transfer** — `SetDoes` updates the registry pointer to the new data field vector after `std::move`, transfers registry index and shared_ptr to the new `ByteCode`, clears the old ByteCode's registry info.
- **`sys-datafields` diagnostic** — New primitive printing registry entry count (total/live), total cells, and total bytes.
- **`removed Value(void*)` constructor** — Heap types already use `make_heap_value()`; the removed constructor was unused.
- 438 tests pass clean under ASan (338 unit + 82 MCP + 12 HTTP + 10 TIL integration), 430 release
- Version bumped to v0.5.6

### Phase 4k: Date/Time Primitives — COMPLETE

- **3 new primitives** — `time-us` (push UTC microseconds since epoch as int64), `us->iso` (format microseconds → compact ISO 8601 HeapString `YYYYMMDDTHHMMSSZ`), `us->iso-us` (format microseconds → high-res ISO 8601 HeapString `YYYYMMDDTHHMMSS.ddddddZ`). Uses `std::chrono::system_clock::now()` for time acquisition and `gmtime_r()` + `strftime()` for UTC formatting. Floor-division handles negative (pre-epoch) microseconds correctly.
- **2 self-hosted convenience words** — `time-iso` (`: time-iso time-us us->iso ;`) and `time-iso-us` (`: time-iso-us time-us us->iso-us ;`) in `data/builtins.til`.
- **Help metadata** — All 5 time words have description, stack-effect, and category (`time`) entries in `data/help.til`.
- 445 tests pass clean under ASan (345 unit + 82 MCP + 12 HTTP + 10 TIL integration)
- Version bumped to v0.5.7

### Phase 4l: PEN Tests & Boundary Condition Testing — COMPLETE

- **4 new TIL test suites** — Penetration tests verifying the interpreter handles malicious/boundary inputs without crashing or corrupting memory. All run under ASan in the debug build.
- **Byte array PEN** (`test_pen_bytes`) — 22 tests: zero-size creation, negative/oversize index get/set (confirmed negative int64→SIZE_MAX wraps safely past bounds check), value overflow (256→0, -1→255), resize edge cases (negative clamped to 0, grow from zero), empty bytes↔string round-trips, 8 intentional-failure survival tests.
- **Array PEN** (`test_pen_arrays`) — 20 tests: empty array get/pop/shift (all fail gracefully), negative/oversize index get/set, drain-to-empty via pop and shift, compact on empty arrays, 9 intentional-failure survival tests.
- **String PEN** (`test_pen_strings`) — 31 tests: substr with negative start (clamped to 0), negative length (clamped to 0), start beyond end (empty result), length beyond end (clamped), empty string edge cases for strim/sfind/sreplace/ssplit/sjoin, s+ type coercion, split→join round-trip.
- **Complex/stress PEN** (`test_pen_complex`) — 32 tests: arrays of strings (store/get/overwrite/pop/shift), arrays of byte arrays (with data verification), nested 2D arrays (inner element access), mixed-type arrays (int/string/bytes), string↔bytes conversion round-trips, ssplit array access, compact preservation, stress loops (100× heap alloc/discard, 50× string concat, 100× byte alloc/discard), deep if nesting (10 levels), DO loop edge cases (same-limit runs once, 1000 iterations, 3-level nested), begin/until immediate exit.
- **PEN launcher design** — Intentional-failure tests run at top level (not in `: ;` definitions) because the interpreter continues after primitive failures in interpret mode. Launchers skip `grep -qi "error"` since error messages go to stderr (not captured by OUTPUT). Control flow tests use colon definitions (compile-only words).
- 449 tests pass clean under ASan (345 unit + 82 MCP + 12 HTTP + 14 TIL integration) → 458 after DoS mitigation
- Version bumped to v0.5.8

### Phase 4m: MCP STDIO Client TUI — COMPLETE

- **Python/Textual TUI** — Interactive terminal client for the ETIL MCP server (separate `etil-tui` repository). Triple-window layout: JSON-RPC traffic log (left, 25%), server I/O with command history (right, 75%), notification bar (bottom). Connects via Docker subprocess per security rules.
- **Transport** — `DockerStdioTransport` using `asyncio.create_subprocess_exec` to launch Docker with session volumes, library volume, tmpfs, and security options matching the HTTP production container. Async readline loops on stdout (JSON-RPC responses) and stderr (diagnostics). Clean shutdown: close stdin → wait 5s → kill.
- **Protocol** — `McpProtocol` JSON-RPC 2.0 client with auto-incrementing request IDs, `asyncio.Future`-based request correlation, MCP handshake (`initialize` + `notifications/initialized`), `call_tool()` convenience method.
- **Widgets** — `JsonRpcPanel` (RichLog with `rich.syntax.Syntax` JSON highlighting, direction indicators → cyan outgoing, ← green/red incoming, timestamps), `ServerIOPanel` (RichLog output + Input with up/down command history), `NotificationBar` (color-coded: info=cyan, success=green, warning=yellow, error=red, connection=magenta).
- **Command dispatch** — TIL code → `tools/call interpret`, meta-commands: `/stack`, `/reset`, `/stats`, `/info <word>`, `/load <path>` (load local `.til` file), `/verbose [on|off]` (toggle full JSON output), `/help`, `/clear`, `/quit`.
- **Keybindings** — Tab (cycle focus), Ctrl+Q (quit), Ctrl+D (dismiss notification), Ctrl+L (clear output), Escape (focus command input).
- **Themes** — Nord-inspired dark/light themes registered with Textual.
- **Setup** — `setup.sh` creates venv + installs `textual>=0.85.0`, `run.sh` launches via `python -m etil_mcp_client`. `--image` flag for custom Docker image name.
- Version bumped to v0.5.9

### Phase 4n: DoS Mitigation — COMPLETE

- **ExecutionContext limits** — 7 new fields (`instruction_budget_`, `instructions_executed_`, `max_stack_depth_`, `max_call_depth_`, `call_depth_`, `cancelled_` atomic, `deadline_`) with unlimited defaults (`UINT64_MAX`/`SIZE_MAX`). 8 new methods: `tick()` (inline, amortised clock check every ~16K instructions via `& 0x3FFF` mask), `set_limits()`/`reset_limits()`, `cancel()`/`is_cancelled()`, `enter_call()`/`exit_call()`, `check_stack_depth()`, `instructions_executed()`. Zero overhead in unlimited path (REPL/tests): one always-true comparison per tick.
- **Inner interpreter enforcement** (`compiled_body.cpp`) — `tick()` at top of main execution loop, `enter_call()`/`exit_call()` around recursive `execute_compiled()` calls, zero-increment `+LOOP` bug fix (was an infinite loop when increment is 0).
- **Outer interpreter enforcement** (`interpreter.cpp`) — `tick()` in `interpret_line()` token loop, `enter_call()`/`exit_call()` in `execute_word()` for bytecode execution.
- **MCP tool_interpret integration** (`tool_handlers.cpp`) — 7 limit constants: `MCP_INSTRUCTION_BUDGET` (10M), `MCP_TIMEOUT_SECONDS` (30s), `MCP_MAX_STACK_DEPTH` (100K), `MCP_MAX_CALL_DEPTH` (1K), `MCP_MAX_CODE_SIZE` (1MB code input), `MCP_MAX_DATA_SIZE` (10MB file data), `MCP_MAX_OUTPUT_SIZE` (10MB). Input size validation (reject > 1MB immediately), `set_limits()` before / `reset_limits()` after each `interpret_line()`, output truncation if > 10MB.
- **nginx hardening** (`deploy/ap1000/nginx-ap1000.conf`) — `limit_conn_zone` + `limit_conn mcp_conn 100` (matches `MAX_SESSIONS` in `mcp_server.hpp`), `client_max_body_size 1m`, `client_body_timeout 10s`, `client_header_timeout 10s`.
- **Docker resource caps** (`docker-compose.yml`, `deploy/ap1000/docker-compose.prod.yml`) — `mem_limit: 512m`, `memswap_limit: 512m`, `cpus: 1.0`, `pids_limit: 50`.
- **Defense layers**: nginx (network: request size, connection limits, timeouts), Docker (process: CPU/memory/PID caps), application (semantic: instruction budget, stack depth, call depth, execution deadline).
- 458 tests pass clean under ASan (352 unit + 82 MCP + 12 HTTP + 14 TIL integration)
- Version bumped to v0.5.12

### Phase 4o: Rich Help Browser (TUI) — COMPLETE

- **Help renderer** (`help_renderer.py`) — Pure Python module converting MCP tool JSON (`list_words`, `get_word_info`) into markdown strings. Three render functions: `render_main_index()` (categorized word table with `etil://` links), `render_category_index()` (single-category view), `render_word_help()` (manpage with implementations table, stack effect, type signature, see-also).
- **Help screen** (`screens/help_screen.py`) — Full-screen Textual `Screen` with `Markdown` widget. Intercepts `etil://word/X`, `etil://category/X`, and `etil://index` link clicks for in-browser navigation. History stack for back navigation. Caches `list_words` response per session (one MCP call). Falls back gracefully for handler words not in dictionary.
- **App integration** — F1 binding opens help index, `/help [word]` opens help browser (optionally at specific word), `/info <word>` opens help page instead of JSON dump. When not connected, `/help` and F1 show plain-text meta-command list.
- **Keybindings** — Escape/q=close, Backspace=back, i=jump to index.
- **WSL2 screenshot** — `deliver_screenshot()` override saves SVG to `~/workspace/screenshot/` instead of trying to open a browser.
- 483 tests pass clean under ASan (403 unit + 66 MCP + 14 TIL integration); E2E Docker tests: 7 STDIO + 18 HTTP + 8 stress + 23 file-I/O
- Version bumped to v0.5.15

### Phase 4p: Help Browser Phase 2 — COMPLETE

- **Breadcrumbs** — Navigation path at top of every page: `Index > Category > Word` with clickable `etil://` links. `render_breadcrumbs()` function in `help_renderer.py`.
- **Stack effects in tables** — Index and category word tables show Stack Effect column. C++ `tool_list_words()` includes `stackEffect` in JSON response.
- **Keyboard n/p navigation** — On word manpages, `n`/`p` keys navigate to next/previous word in same category (wraps around).
- **Search** — Press `/` to open search input. Real-time filtering by word name or description (case-insensitive substring). Escape closes search and restores previous page. `render_search_results()` function.
- **Live examples** — Word manpages auto-execute examples in a sandbox Docker container and display captured output/errors/stack. `extract_runnable_code()` strips `=> ` annotation lines. `_normalize_example_text()` converts `\n` and `\"` escapes.
- **Sandbox isolation** — Help browser lazily starts a second Docker container (`DockerStdioTransport` + `McpProtocol`) for running examples without polluting the user's interpreter session. Calls `reset` before each example. Shuts down on screen close. STDIO-only for now (HTTP transport support planned).
- **EXAMPLE_OVERRIDES** — Python-side dict for examples containing `s"` or `."` (which include quote characters that truncate `string-read-delim` in `help.til`). Covers string, comparison, and I/O examples with contextual output annotations (e.g., `equal? -1`, `found at index 6`, `quot=3 rem=1`).
- **Meta-commands in index** — Help index page includes TUI meta-command reference table at top.
- **CSS fixes** — HelpScreen overrides main grid layout with vertical layout; search input docked top with hidden class toggle.
- Version bumped to v0.5.16

### TUI Improvements

- **Panel re-render on width change** — All three TUI panels (JsonRpcPanel, ServerIOPanel, NotificationBar) now buffer their content and re-render from buffer when panel width changes. Triggered by F2 layout toggle and terminal resize. RichLog `Strip` objects are re-created at the new width so JSON blocks and text reflow correctly. Scroll position preserved across re-renders.
- **Debounced resize with cooldown** — All three panels use a 0.15s debounce timer + 0.3s post-refresh cooldown to prevent scrollbar oscillation (clear→no scrollbar→width change→resize→refresh→repeat). Fixes TUI lockup with large output.
- **16 MB StreamReader limit** — `DockerStdioTransport` now sets `limit=16*1024*1024` on `asyncio.create_subprocess_exec()`. The default 64KB limit caused `LimitOverrunError` when MCP responses exceeded 64KB, silently killing the reader task and eventually the server.
- **60s request timeout** — `McpProtocol.request()` now uses `asyncio.wait_for(future, timeout=60.0)`. Previously waited forever if the server crashed.
- **CancelledError handling** — All async command handlers in `app.py` catch `asyncio.CancelledError` to prevent crashes when the transport closes during a pending request.
- **F3 notification fullscreen** — Press F3 to toggle the notification bar between its normal bottom strip (max 6 lines) and fullscreen mode. Uses CSS class `notif-fullscreen` (same pattern as F2's `io-only`/`rpc-only`). Hides both I/O and JSON-RPC panels, switches to 1x1 grid. Composes independently with F2 layout state — F2 CSS classes are preserved and restored when F3 is toggled off. Panels refresh their content at the correct size on toggle.
- **F2/F3 focus fix** — Command input now automatically regains focus when the ServerIOPanel becomes visible after F2 layout cycling or F3 toggle-off. Previously, cycling F2 through `rpc-only` and back lost focus on the Input widget.
- **Non-blocking F2/F3 during server requests** — `on_input_submitted` now offloads command handling to a background task via `asyncio.create_task()`, so the Textual event loop stays free to process keybindings (F2 layout toggle, F3 notification fullscreen) while awaiting server responses. Input widget is disabled during flight to prevent double-submit. Focus restored on completion.
- **F4 notification scroll toggle** — Press F4 to toggle between newest (bottom, autoscroll on) and oldest (top, autoscroll off) in the notification log. Notifications now render chronologically (oldest first) with incremental appends — no clear+rewrite on each drip, eliminating flicker during rapid notification bursts.

### MCP Server Error Handling (v0.5.19)

- **SIGPIPE protection** — `main()` ignores SIGPIPE so broken pipes (client disconnects) produce EPIPE on write instead of killing the process silently.
- **Top-level try/catch** — `main()` wraps all server logic in try/catch with stderr diagnostics. Previously an unhandled exception called `std::terminate()` with no output.
- **`handle_message()` catch-all** — Wraps the entire dispatch logic in try/catch, shared by both STDIO and HTTP transports. Returns JSON-RPC InternalError with diagnostic message.
- **STDIO broken pipe detection** — `StdioTransport::send()` checks `out_.fail()` after writes. On failure, logs to stderr and signals shutdown instead of blocking forever.
- **STDIO handler catch** — `StdioTransport::run()` wraps handler calls in try/catch as defense in depth.
- **HTTP `dispatch()` method** — New private method on `HttpTransport` that acquires the handler mutex and wraps the handler call in try/catch. Both request paths (initialize + normal) use it.
- **MCP stress test suite** — New `tests/docker/test_mcp_stress.sh` with 7 standalone tests: small/medium/large/huge loops, flood output, sequential requests, computation stress. Validates server handles responses up to 10MB without crashing.
- Version bumped to v0.5.19

### v0.5.27: PRNG Primitives, Silent Forget, Memory Leak Fixes — COMPLETE

- **3 PRNG primitives** — `random` (push float in [0,1)), `random-seed` (seed the PRNG for deterministic sequences), `random-range` (push integer in [lo,hi)). Uses `thread_local std::mt19937_64` with `std::chrono::steady_clock` default seed, matching the project's thread-local `ExecutionContext` pattern. Registered under math category.
- **Silent forget/forget-all** — `forget` and `forget-all` no longer print any output ("Forgot ..." messages removed). Only internal exceptions (already handled) produce output. Updated `data/builtins.til` definitions and test expectations.
- **CappedStringBuf** — Custom `std::streambuf` replacing `std::ostringstream` in `McpServer` for interpreter output/error capture. Caps buffer at `MCP_MAX_OUTPUT_SIZE` (10MB) — since output is truncated to 10MB anyway, prevents unbounded buffer growth. Key fix for RSS leak: `std::ostringstream`'s incremental buffer doubling through glibc's `brk()` heap caused permanent RSS inflation (36MB stuck even after freeing); `CappedStringBuf` uses `std::string` which routes large allocations through `mmap()`, properly returned to OS when freed. `take()` moves the string out; `reset()` uses `swap()` for guaranteed deallocation.
- 499 tests pass clean under ASan (405 unit + 80 MCP + 14 TIL integration)
- Version bumped to v0.5.27

### v0.5.28: Hash Map Data Structure (HeapMap) — COMPLETE

- **HeapMap** — New heap-allocated hash map (`std::unordered_map<std::string, Value>`) with string keys and arbitrary values. Same refcounting pattern as HeapArray: map owns one ref per stored Value; `get()` addrefs before returning; `set()` releases old value; destructor releases all values. `HeapObject::Kind::Map` and `Value::Type::Map` added to type system.
- **8 new map primitives** — `map-new` (create empty map), `map-set` (set key/value, return map), `map-get` (get value by key, fails if missing), `map-remove` (remove key, fails if missing), `map-length` (entry count), `map-keys` (array of key strings), `map-values` (array of values), `map-has?` (existence check, returns flag). Registered in separate `map_primitives.cpp` file (matching string/array/byte pattern). Stack convention: mutating ops return the map, read ops consume it.
- **Full type system integration** — Map cases added to `format_value()` (`{map:N}`), `prim_dot()` (`<map>`), `dump_value()` (shows key/value pairs), `pop_as_string()` (`map(N)`), `tool_get_stack()` (MCP JSON), `is_heap_value()`, `make_heap_value()`.
- **Help & TUI** — Help metadata for all 8 words in `data/help.til`. Map examples with `s"` added to `EXAMPLE_OVERRIDES` in `help_renderer.py` (per the quotes-in-help.til pitfall). Map category added to `CATEGORY_ORDER` (between array and byte-array).
- 511 tests pass clean under ASan (417 unit + 80 MCP + 14 TIL integration)
- Version bumped to v0.5.28

### v0.5.29: Execution Tokens (Function References)

- **Value::Type::Xt** — New value type holding a `WordImpl*` with intrusive refcounting. `make_xt_value()` helper creates Xt values. `value_addref()`/`value_release()` updated to handle Xt via `WordImpl::add_ref()`/`release()`.
- **4 new primitive words** — `'` (tick: parse next token, push xt), `execute` (pop xt, run it), `xt?` (type check, returns flag), `>name` (extract word name as HeapString).
- **`[']` compile handler** — Compile-time tick: reads word name at compile time, emits `PushXt` instruction so xt is pushed at runtime. Added to `CompileHandlerSet` (now 5 handlers).
- **`PushXt` bytecode instruction** — New `Instruction::Op` with dictionary lookup and impl caching (same pattern as `Call`). Handled in `execute_compiled()`.
- **Full type system integration** — Xt cases added to `format_value()` (`<xt:name>`), `prim_dot()`, `dump_value()`, `pop_as_string()` (`xt(name)`), `tool_get_stack()` (MCP JSON), `see`/`format_instruction()` (`PushXt name`). Handler help/see tables updated for `[']`.
- **Help metadata** — Entries for `'`, `execute`, `xt?`, `>name` in `data/help.til` (category: execution).
- 525 tests pass clean under ASan (417 unit + 80 MCP + 11 TIL integration + 17 new xt tests)

### v0.5.30–v0.5.34: Value Refactor, TUI /load, Session Logging, Response Formatting

- **Workspace backup script** (`scripts/backup-workspace.sh`) — Timestamped `.tar.gz` of source files, excludes build artifacts, deps, secrets, IDE metadata (~6 MB vs 3.3 GB). `.claude/` added to `.gitignore`. (v0.5.30)
- **Value type system refactor** — Typed member accessors (`as_string()`, `as_array()`, `as_map()`, `as_byte_array()`, `as_xt_impl()`, `dataref_index()`, `dataref_offset()`), static factories (`Value::from()`, `Value::from_xt()`), lifecycle methods (`release()`, `addref()`). `FunctionPtr` signature changed from `bool(*)(void*)` to `bool(*)(ExecutionContext&)`. Centralized pop helpers in `value_helpers.hpp`. Eliminates all raw `static_cast<HeapType*>(v.as_ptr)` across 24 files. (v0.5.31)
- **TUI `/load` meta-command** — Reads a local `.til` file, uploads it and its dependencies via `write_file`, then runs `include`. Fallback mode sends raw content via `interpret`. No client-side comment stripping or line joining — the server interpreter handles `#` comments as a parsing token. (v0.5.32)
- **TUI session logging** — `SessionLogger` class writes plain-text (`.log`) and JSONL (`.json`) logs via callback hook on `ServerIOPanel`. Three new meta-commands: `/logfile [path]`, `/logjson [path]`, `/log` (toggles both). `run.sh` uses `PYTHONPATH` instead of `cd` to preserve working directory. (v0.5.33)
- **TUI server response formatting** — `_display_tool_result` unpacks server JSON responses (`output`, `errors`, `stack` keys) instead of displaying raw JSON. Output shown in green, errors in red (also sent to notification bar), stack in yellow. Non-JSON responses (e.g., `/stats`) fall through to existing plain-text display. Text log now records clean fields instead of raw JSON. (v0.5.34)
- Version bumped to v0.5.34

### v0.5.41: SSE Notification Delivery for HTTP Transport — COMPLETE

- **SSE notification delivery** — `HttpTransport::send()` now buffers notifications in a `thread_local` vector instead of discarding them. After request processing, if any notifications were emitted (e.g., via `sys-notification`), the POST response uses `text/event-stream` with SSE events (notifications first, then response). When no notifications are present, the response remains `application/json` (fully backward compatible).
- **`build_sse_body()`** — Static helper formats SSE body: each event is `data: <json>\n\n`, notifications followed by the response as the final event.
- **Thread-local isolation** — `thread_local pending_notifications_` ensures per-request isolation without locks. cpp-httplib's thread pool handles each request in its own thread, so concurrent sessions are naturally isolated.
- **All POST paths updated** — Legacy single-session route, multi-session initialize path, and normal session dispatch all use the clear/drain/SSE pattern.
- 578 tests pass (debug + release)
- Version bumped to v0.5.41

### v0.5.42: Replace LockFreeStack with ValueStack — COMPLETE

- **ValueStack** — New `std::vector<Value>`-backed stack class (`value_stack.hpp`) replacing `LockFreeStack<Value>` in all three `ExecutionContext` stacks (data, return, float). Every `ExecutionContext` is single-threaded (MCP sessions are mutex-serialized, REPL is single-threaded), so lock-free guarantees were never exercised.
- **Benefits** — No heap allocation per push (amortised vector growth), O(1) indexed access (eliminates drain-and-restore patterns), no deferred reclamation overhead.
- **Simplified stack access** — `tool_get_stack()`, `tool_interpret()`, `resource_stack()`, and `Interpreter::stack_status()` now use direct indexed iteration instead of drain-and-restore.
- **Removed** — `lock_free_stack.hpp`, `test_lock_free_stack.cpp`, `reclaim_stacks()` method, `reclaim_retired()` calls from `tick()` and `tool_interpret()`.
- 574 tests pass (debug + release)
- Version bumped to v0.5.42

### v0.5.43: Fix TUI /load — Session Home Directory & Include Path Resolution — COMPLETE

- **STDIO container Docker args** — `config.py` and `__main__.py` now launch Docker with the same volumes and env vars as the HTTP production container: `-v etil-sessions:/data/sessions`, `-v etil-library:/data/library:ro`, `-e ETIL_SESSIONS_DIR=/data/sessions`, `-e ETIL_LIBRARY_DIR=/data/library`, `--tmpfs /tmp:size=10M`, `--security-opt no-new-privileges:true`. Previously the STDIO container had no session directory, so `write_file` returned `isError: true` but the TUI didn't check it.
- **`isError` check in `/load`** — `_meta_load` now inspects every `write_file` response for `isError`. If the server reports failure (e.g., no session directory configured), falls back to `_meta_load_fallback` (direct interpret) instead of proceeding to an `include` that would fail.
- **`_scan_includes` CWD fallback** — When an include path isn't found relative to the parent file's directory, also tries relative to CWD. Fixes `include tests/til/harness.til` (relative to project root) when loading `tests/til/test_builtins.til` from the project root directory.
- Version bumped to v0.5.43

### v0.5.44: Replace EXAMPLE_OVERRIDES with s|/.| in help.til — COMPLETE

- **Pipe-delimited string literals** — `s|` and `.|` handler words as alternatives to `s"` and `."` that use `|` as the delimiter. Enables writing help examples containing double-quote characters directly in `help.til` without `string-read-delim` truncation issues.
- **EXAMPLE_OVERRIDES removed** — All Python-side example overrides moved to `help.til` using `s|`/`.|` syntax. Help examples are now a single source of truth.
- 574 tests pass (debug + release)
- Version bumped to v0.5.44

### v0.5.45: HTTP Streamable Transport for TUI MCP Client — COMPLETE

- **McpTransport ABC** — Abstract base class (`transport.py`) with `start()`, `send()`, `shutdown()`, `on_message`/`on_stderr`/`on_close` callbacks. `DockerStdioTransport` refactored to implement it.
- **HttpStreamableTransport** — New transport (`http_transport.py`) using `httpx.AsyncClient` for MCP Streamable HTTP. POST JSON-RPC, parse SSE batch or plain JSON responses, track `Mcp-Session-Id`, Bearer token auth. Synthetic JSON-RPC errors for connection/timeout/auth failures so pending futures always resolve.
- **`--connect NAME`** — TUI CLI flag to connect via a named connection from `~/.etil/connections.json`.
- **`--url URL + --api-key-file PATH`** — TUI CLI flags for direct HTTP connection without a named config.
- **Session auto-reconnect** — On session-expired (404), clears session ID so next request triggers fresh `initialize` handshake.
- 574 tests pass (debug + release)
- Version bumped to v0.5.45

### v0.5.46: Real-time SSE Streaming for Notifications — COMPLETE

- **Server chunked streaming** — `HttpTransport` POST handlers use `set_chunked_content_provider()` with `text/event-stream` content type. A `thread_local httplib::DataSink*` pointer (`active_sink`) enables `send()` to write each notification SSE event directly to the socket as it occurs, instead of buffering all notifications until the handler returns.
- **Client streaming** — `HttpStreamableTransport.send()` uses `httpx.AsyncClient.stream()` with `aiter_lines()` to process SSE events as they arrive. Each `data:` line fires `on_message` immediately, enabling real-time notification display in the TUI during long-running commands.
- **Removed `_parse_sse_batch()`** — Replaced by streaming line-by-line parsing inside the `stream()` context manager.
- **Backward compatible** — `build_sse_body()` and `drain_pending_notifications()` retained for tests. `send()` falls back to buffering when `active_sink` is null.
- 574 tests pass (debug + release)
- Version bumped to v0.5.46

### v0.5.53: Single Source of Truth for Handler Word Help — COMPLETE

- **Handler word registration** — `Dictionary::register_handler_word()` creates a `WordConcept` with empty `implementations` vector, enabling `set_concept_metadata()` to work for handler words. `Interpreter::register_handler_words()` registers all handler words (`:`, `if`, `else`, `."`, `s"`, `s|`, `.|`, etc.) from the 3 handler sets before `load_startup_files()`.
- **27 handler word help entries in `help.til`** — All handler words now have description, stack-effect, category, word-type, and examples metadata attached via `meta!` in `help.til`. The `word-type` key documents mode: `"compile-only"`, `"interpret-only"`, or `"both"`.
- **Deleted `help_handler_table()`** from `primitives.cpp` — removed the C++ inline help table (~30 lines) and simplified `print_help_for_word()` (2 sources instead of 3) and `prim_help()` (no handler table loop).
- **Deleted `HANDLER_WORDS`** from Python TUI — removed the 160-line dict and all fallback logic from `help_renderer.py` and `help_screen.py`. Handler words now flow through the same MCP `list_words`/`get_word_info` path as all other words.
- **Fixed `prim_see()` segfault** — handler words have concept entries with empty implementations vectors; added empty-check so they fall through to `see_handler_table()`.
- **Fixed `\ --- Map ---` comment** — pre-existing bug in `help.til` using `\` (FORTH comment) instead of `#` (ETIL comment).
- 673 tests pass (debug)
- Version bumped to v0.5.53

### v0.5.54: TUI Startup Logging, Log Rotation, and Version Notification — COMPLETE

- **Auto-start session logging** — TUI now opens both text and JSON log files on startup by default (before connecting). Log file paths are announced in the notification bar. `--nologs` CLI flag disables this behavior; `/log` toggle still works as before.
- **Log rotation** — On startup, old `/tmp/*-tui.{log,json}` files are rotated, keeping only the 5 most recent of each type. `--norotate` CLI flag disables rotation.
- **Version notification** — TUI announces its version (`ETIL MCP Client v{version}`) in the notification bar before connecting to the server.
- **`SessionLogger.start_both()`** — New method that unconditionally opens both log files (unlike `toggle_both()` which closes if already open). For startup use only.
- **`SessionLogger.rotate_logs(max_keep)`** — New static method that finds, sorts, and deletes old TUI log files beyond `max_keep` per type.
- **`ClientConfig` fields** — `auto_logs` (default `True`) and `auto_rotate` (default `True`) control startup behavior.
- Version bumped to v0.5.54

### v0.5.55: Security-Hardened `evaluate` Word — COMPLETE

- **`evaluate` primitive** — FORTH `evaluate` word (`( str -- )`) interprets a string as TIL code at runtime. Pops a string from the data stack, passes it to `interpret_line()` for execution. Registered under "dictionary-ops" category alongside `include` and `library`.
- **`evaluate_string()` method** — New safe entry point on `Interpreter` for string evaluation. Saves/restores `ctx_.input_stream()` around `interpret_line()` (fixes input stream corruption when `evaluate` is called mid-line). Detects unterminated definitions (unclosed `:`) and calls `abandon_definition()` to reset `compiling_` state.
- **Call depth tracking** — `prim_evaluate` calls `enter_call()`/`exit_call()` so recursive evaluate chains (e.g., `s" : bomb s\" bomb\" evaluate ; bomb" evaluate`) are visible to the `max_call_depth_` limit. Without this, recursive evaluate exhausted the C++ stack before the interpreter limit triggered.
- **MCP dictionary concept limit** — `MCP_MAX_CONCEPTS` (10,000) added to `tool_handlers.cpp`. After each `interpret` call, checks `dict.concept_count()` and emits error if exceeded. Prevents dictionary bomb attacks via repeated MCP calls.
- **PEN security tests** — 14 new tests in `test_pen_evaluate.til`: recursive bomb survival, input stream preservation, nested evaluate, unterminated definition recovery, evaluate in compiled words, define-and-use in single evaluate.
- **Unit tests** — 11 new interpreter tests + 2 new MCP tests covering all threat scenarios from the security analysis.
- 687 tests pass clean under ASan (debug) and release
- Version bumped to v0.5.55

### v0.5.56: Source Location Error Diagnostics — COMPLETE

- **Source metadata moved to impl-level** — `definition-type`, `source-file`, `source-line` metadata now stored on `WordImpl` (per-implementation) instead of `WordConcept` (shared). Multiple impls of the same word each track their own source location independently.
- **Collapsible implementation tree** — Help browser word manpage shows expandable tree of implementations with their individual source locations and definition types.
- **Flaky test fix** — `test_evolve` no longer uses time-based seed override that caused intermittent failures.
- 701 tests pass (debug + release)
- Version bumped to v0.5.56

### v0.5.57: WordImpl Metadata Convenience API — COMPLETE

- **Typed setter/getter API on `WordImpl`** — 4 setters (`mark_as_primitive()`, `mark_as_include(file, line)`, `mark_as_evaluate()`, `mark_as_interpret()`) and 3 getters (`definition_type()`, `source_file()`, `source_line()`) fully encapsulate the metadata key strings and value strings as private constants. No caller ever sees raw metadata key/value strings for definition origin tracking.
- **Bug fix: error trace source locations** — `compiled_body.cpp` was reading source metadata at concept level (`dict->get_concept_metadata`), which was empty after v0.5.56 moved metadata to impl level. Error traces silently lost source locations. Now uses `impl->source_file()` / `impl->source_line()` directly.
- **Refactored 10 files** — Eliminated 12 scattered `metadata().set("definition-type", ...)` calls across 6 source files, replaced with typed one-liner methods. Tests updated to use typed getters.
- 701 tests pass (debug + release)
- Version bumped to v0.5.57

### v0.5.58: LVFS (Little Virtual File System) — COMPLETE

- **Lvfs class** — Unified virtual filesystem with `/home` (writable, per-session) and `/library` (read-only, shared) under a virtual root `/`. Per-session CWD (default `/home`) enables relative path navigation. Path normalization resolves `.`/`..` (clamped at `/`). `map_to_filesystem()` routes `/home/...` → `home_dir_` and `/library/...` → `library_dir_`. `resolve_under()` prevents directory traversal attacks. New files: `include/etil/lvfs/lvfs.hpp`, `src/lvfs/lvfs.cpp`.
- **6 LVFS primitives** — `cwd` (print CWD), `cd` (change directory, no arg → `/home`), `ls` (list directory, alphabetical), `ll` (long format, mtime descending), `lr` (recursive long format), `cat` (print file contents). All use optional-parse pattern from `prim_help` except `cat` which requires an argument. New file: `src/lvfs/lvfs_primitives.cpp`.
- **Delegation pattern** — `Interpreter::resolve_*` methods delegate to `lvfs_` when non-null, fall back to local `home_dir_`/`library_dir_` when null (standalone REPL, unit tests without LVFS). `ExecutionContext` and `Interpreter` gain non-owning `Lvfs*` pointer. `Session` creates and owns `std::unique_ptr<Lvfs>`, passes to Interpreter via `set_lvfs()`.
- **Help metadata** — All 6 words have description, stack-effect, category, and examples in `data/help.til`.
- 739 tests pass (debug + release)
- Version bumped to v0.5.58

### v0.5.59: Fix LVFS mtime (file_clock epoch conversion) — COMPLETE

- **file_clock → system_clock epoch fix** — GCC's libstdc++ `file_clock` has a different epoch than `system_clock` (~204 years offset), causing `ll`/`lr` to show dates like `1822-02-23` instead of `2026-02-22`. Fixed by computing the epoch offset once at startup via `file_time_to_us()` helper and applying it to every file timestamp conversion.
- 739 tests pass (debug + release)
- Version bumped to v0.5.59

### v0.5.60–v0.5.64: Primitives, Source Location Fixes, Comment Handling — COMPLETE

- **`sprintf` primitive** — C-style string formatting. (v0.5.61)
- **`lroll`/`rroll` primitives** — Bitwise rotate operations. (v0.5.62)
- **Fix "defined at" line numbers** — Multi-line colon definitions now report the `:` line instead of the `;` line. Added `compiling_start_line_` field to `Interpreter`, saved when `:` is encountered, used in `finalize_definition()`. (v0.5.63)
- **ASan leak fixes** — Fixed `HeapString` leak in `InterpreterTest.EvaluateNested` (missing `release()` on popped value). Fixed `FileIOTest` fixture leak (missing `shutdown()` in `TearDown`). (v0.5.63)
- **Remove `strip_comment` from C++ interpreter** — `#` comments are now handled as a token-level line break in the outer interpreter's `while (iss >> word)` loop: when a token starts with `#`, the rest of the line is skipped. This preserves accurate `source_line_` tracking (no line content modification) and keeps `#` inside string literals intact. (v0.5.64)
- **Fix TUI `/load` fallback** — Removed comment stripping and line joining from `_meta_load_fallback`; raw content sent to server. Fixed `_scan_includes` to skip `#` comment lines (was matching `include` inside comments). (v0.5.64)
- **`evolve.til` moved** — `examples/evolve.til` → `examples/tui/evolve.til`. (v0.5.64)
- 840 tests pass clean under ASan (debug)
- Version bumped to v0.5.64

### v0.6.1: LVFS File Upload/Download for TUI — COMPLETE

- **`read_file` MCP tool** — Tool #10. Reads a file from the session home directory and returns `{path, content, sizeBytes}`. Validates path (non-empty, no leading `/`, traversal-safe via `resolve_home_path()`), checks file exists, is regular file, size <= `MCP_MAX_DATA_SIZE` (10 MB).
- **`MCP_MAX_DATA_SIZE`** — New 10 MB constant for file data upload/download limits. `write_file` and `read_file` use this instead of `MCP_MAX_CODE_SIZE` (1 MB), which remains the limit for `interpret` code input.
- **TUI `/upload` command** — `/upload <local-path> [remote-name]` reads a local file and uploads it to the server's LVFS via `write_file` without executing it. Like `/load` but without the `include` step.
- **TUI `/download` command** — `/download <remote-path> [local-path]` calls `read_file` to fetch a file from the server's LVFS and saves it locally.
- **Tab completion** — `/upload` and `/download` added to completer and help renderer meta-command lists.
- **`MAX_SESSIONS` raised to 100** — Supports higher concurrency for LVFS workloads (was 10).
- 898 tests pass clean under ASan (debug), 879 release
- Version bumped to v0.6.1

### v0.6.3: Unify Sync File I/O Under libuv — COMPLETE

- **Sync file I/O converted to libuv** — 11 of 13 `*-sync` words in `file_io_primitives.cpp` now use libuv synchronous mode (`callback=NULL`) instead of C++ stdlib (`std::filesystem`, `std::ifstream`, `std::ofstream`). Unifies all file I/O through libuv so both sync and async words share the same I/O layer.
- **`thread_local uv_loop_t`** — `sync_loop()` helper returns a per-thread loop initialized on first use. libuv sync mode requires a valid loop parameter but doesn't process events through it. No dependency on `UvSession` or `ctx.uv_session()`.
- **`SyncReq` RAII wrapper** — Lightweight struct wrapping `uv_fs_t` with destructor calling `uv_fs_req_cleanup()`. Simpler than async `FsRequest` (no completion tracking needed).
- **`write_file_sync_impl()` helper** — Shared implementation for `write-file-sync` and `append-file-sync` with flag parameter, mirroring the async `write_file_impl()` pattern.
- **`lstat-sync` mtime fix** — Now uses `uv_fs_lstat()` with POSIX `st_mtim` timespec (direct microsecond conversion), replacing `fs::status()` + `Lvfs::file_time_to_us()` workaround. Also correctly uses `lstat` semantics (no symlink follow) matching the word name and async counterpart.
- **Words staying stdlib** — `mkdir-sync` (recursive mkdir needs path iteration; `fs::create_directories()` is simpler) and `rm-sync` (libuv has no recursive remove; `fs::remove_all()` is the only option). Same choices made in async counterparts.
- **Includes cleaned up** — Removed `<fstream>`, `<sstream>`, `<cstdlib>`; added `<uv.h>`; kept `<filesystem>` for `mkdir-sync` and `rm-sync`.
- 898 tests pass clean under ASan (debug) and release
- Version bumped to v0.6.3

### v0.6.4: Python Translation of File I/O Stress Tests — COMPLETE

- **Python E2E stress tests** (`tests/docker/test_file_io_stress.py`) — 1:1 translation of the 56 bash file I/O stress tests into Python. Stdlib only (`urllib.request`, `json`, `re`) — zero external dependencies. `McpClient` class encapsulates all MCP HTTP communication (initialize, terminate, interpret, write_file, read_file, list_files, reset) with SSE response parsing. Identical PASS/FAIL output format for `diff` comparison with the bash original.
- **Bash wrapper** (`tests/docker/test_file_io_stress_py.sh`) — Minimal wrapper validating `ETIL_TEST_API_KEY` and invoking Python.
- **Test sections**: (1) power-of-2 sync round-trips (16 tests), (2) power-of-2 async round-trips (16 tests), (3) interleaved multi-file I/O (4 tests), (4) byte array boundary stress (8 tests), (5) append and truncate stress (8 tests), (6) cleanup (4 tests).
- Both bash and Python versions pass 56/56 against ap1000.
- Version bumped to v0.6.4

### v0.6.5: Fix current_session_ Data Race (4-Worker 404s) — COMPLETE

- **`current_session_` data race fix** — `Session* current_session_` was a plain member of `McpServer`, written and read by all HTTP worker threads concurrently. Each session had its own `session->mutex`, but since they were different mutexes, all threads could enter `handle_message()` simultaneously and race on the shared pointer. Thread A would set `current_session_ = session_A`, Thread B would overwrite it with `session_B`, then Thread A's tool handler would operate on session_B's interpreter without holding session_B's mutex — C++ undefined behavior → memory corruption → server crash → Docker restart → 404 for all subsequent requests. Fixed by changing to `static thread_local Session* current_session_` so each HTTP worker thread gets its own copy. Zero changes needed in the 14 tool/resource handler call sites.
- **`create_session()` performance fix** — Session construction (`register_primitives()` + `load_startup_files()`) was done under `sessions_mutex_`, serializing all concurrent session creation (~hundreds of ms each). Refactored to construct outside the lock using a check-construct-recheck pattern: lock briefly to check capacity, unlock to construct, re-lock to insert.
- 898 tests pass (debug + release)
- Version bumped to v0.6.5

### v0.6.6–v0.6.7: Outbound HTTP Client (`http-get`) — COMPLETE

- **`http-get` primitive** — `( url headers-map -- bytes status-code bool )` performs HTTP/HTTPS GET request with extra headers. The headers parameter is a HeapMap of string key→string value pairs (use `map-new` for no extra headers). Returns response body as `HeapByteArray` (opaque bytes — NOT a string, preventing code injection via `evaluate`), HTTP status code, and success flag (Boolean). On failure, pushes only `false` with error to `ctx.err()`.
- **Design doc** — `docs/claude-knowledge/20260227-interpreter-outbound-network-access.md` covers threat analysis, SSRF vectors, DNS covert channels, resource exhaustion, and settled design decisions.
- **URL validation** — `ParsedUrl` parser (http/https, IPv6 literals, ports, case normalization), SSRF blocklist (loopback 127/8, RFC1918 10/8+172.16/12+192.168/16, link-local 169.254/16, IPv6 ::1/fc00::/7/fe80::/10, IPv4-mapped IPv6), domain allowlist (exact match + `*.domain` wildcards), DNS resolution with all-address SSRF check.
- **Per-session budgets** — 10 fetches per interpret call, 100 per session lifetime, 1MB response cap, 10s request timeout. Configurable via env vars: `ETIL_HTTP_ALLOWLIST`, `ETIL_HTTP_TIMEOUT_MS`, `ETIL_HTTP_MAX_RESPONSE_BYTES`, `ETIL_HTTP_PER_INTERPRET_BUDGET`, `ETIL_HTTP_PER_SESSION_BUDGET`, `ETIL_HTTP_ALLOW_HTTP`.
- **WorkRequest pattern** — RAII wrapper for `uv_work_t` with `atomic<bool> done` flag and `void* user_data` pointer. `http_get_work()` runs blocking `httplib::Client::Get()` on libuv thread pool via `uv_queue_work()`. `await_work()` polls with `ctx.tick()` for execution budget enforcement and supports cancellation via `atomic<bool>` flag.
- **OpenSSL support** — Dockerfile adds `libssl-dev` (builder) and `libssl3t64 ca-certificates` (runtime). cpp-httplib auto-detects OpenSSL and enables `CPPHTTPLIB_OPENSSL_SUPPORT`.
- **CMake gating** — `ETIL_BUILD_HTTP_CLIENT` option (default OFF). When ON: adds `net/url_validation.cpp` and `net/http_primitives.cpp`, links `httplib::httplib`, defines `ETIL_HTTP_CLIENT_ENABLED`. Guards in `session.hpp/cpp`, `execution_context.hpp`, `tool_handlers.cpp`.
- **New files**: `include/etil/net/http_client_config.hpp`, `include/etil/net/url_validation.hpp`, `include/etil/net/http_primitives.hpp`, `src/net/url_validation.cpp`, `src/net/http_primitives.cpp`, `tests/unit/test_url_validation.cpp`
- **Bugs fixed during deploy**: (1) `WorkRequest::req.data` type confusion — overriding `req.data` broke `on_after_work` callback, fixed by adding `user_data` field; (2) uncaught `std::invalid_argument` from httplib when HTTPS unsupported — try/catch added; (3) OpenSSL not linked in Docker — added to Dockerfile.
- 932 tests pass (debug + release), 34 new URL validation tests
- Version bumped to v0.6.7

### v0.6.8: JWT Authentication (Phase 1) — COMPLETE

- **jwt-cpp integration** — jwt-cpp v0.7.2 fetched via FetchContent. RS256 algorithm for JWT signing/validation. CMake option `ETIL_BUILD_JWT` (default OFF). Gated throughout with `#ifdef ETIL_JWT_ENABLED`.
- **AuthConfig** — Role/permission/user mappings loaded from JSON config file (`ETIL_AUTH_CONFIG` env var). `RolePermissions` struct: `http_domains` (allowlist), `max_sessions`, `instruction_budget`, `file_io` gate. `role_for()` resolves user→role (falls back to `default_role`), `permissions_for()` resolves user→permissions.
- **JwtAuth** — Mint and validate ETIL JWTs (RS256). `mint_token(user_id, email)` creates JWT with iss/sub/iat/exp/email/role claims. `validate_token(token)` verifies signature, issuer, expiry; returns `std::optional<JwtClaims>`.
- **`/auth/token` endpoint** — Stub endpoint accepting `{"user_id":"...", "email":"..."}` and returning minted ETIL JWT. Future: exchange OAuth provider token for ETIL JWT.
- **Dual-mode auth** — HTTP transport tries JWT Bearer token first, falls back to API key. Fully backward compatible.
- **Session extension** — `user_id` and `role` fields on Session (under `#ifdef ETIL_JWT_ENABLED`). `apply_role_permissions()` creates per-role `HttpClientConfig` overriding server-wide HTTP allowlist.
- **Per-role permission enforcement** — `instruction_budget` override in `tool_interpret()`, `file_io` gate in `tool_write_file()`/`tool_read_file()`, role-specific HTTP domain allowlist.
- **23 unit tests** — AuthConfig loading, role resolution, JWT round-trip, expiry, signature validation, wrong key, wrong issuer, garbage tokens.
- **Docker** — `ETIL_BUILD_JWT=ON` in Dockerfile, `/etc/etil` mount point for auth config.
- **New files**: `include/etil/mcp/auth_config.hpp`, `include/etil/mcp/jwt_auth.hpp`, `src/mcp/auth_config.cpp`, `src/mcp/jwt_auth.cpp`, `tests/unit/test_jwt_auth.cpp`, `data/auth.json.example`
- 955 tests pass (debug), 932 with JWT OFF (no regressions)
- Version bumped to v0.6.8

### v0.6.9: OAuth Device Flow (Phase 2) — COMPLETE

- **`OAuthProvider` interface** — Abstract base class (`oauth_provider.hpp`) with `DeviceCodeResponse`, `PollResult` (6-variant status enum), `ProviderUserInfo` data structs. Three virtual methods: `request_device_code()`, `poll_device_code()`, `get_user_info()`.
- **`GitHubProvider`** — Full GitHub device flow implementation. Device code via `POST github.com/login/device/code`, token poll via `POST github.com/login/oauth/access_token` (with `Accept: application/json`), user info via `GET api.github.com/user` (with `/user/emails` fallback for null email). URL-encoded response parser for GitHub's non-JSON default. No `client_secret` needed (public client).
- **`GoogleProvider`** — Full Google device flow implementation. Device code via `POST oauth2.googleapis.com/device/code`, token poll via `POST oauth2.googleapis.com/token` (with `client_secret`), user info via `GET www.googleapis.com/oauth2/v3/userinfo`. Handles Google's `verification_url` field (not `verification_uri`).
- **`OAuthProviderConfig`** — New struct in `auth_config.hpp`: `enabled`, `client_id`, `client_secret`. `AuthConfig::providers` map parsed from `auth.json` `providers` section. Backward compatible — missing `providers` key produces empty map.
- **`POST /auth/device`** — Initiate device flow. Request: `{"provider":"github"}`. Response: `{device_code, user_code, verification_uri, expires_in, interval}`. Errors: 400 (bad provider), 502 (provider unavailable).
- **`POST /auth/poll`** — Poll device code status. Request: `{"provider":"github","device_code":"..."}`. Returns `{"status":"pending"}`, `{"status":"slow_down","interval":10}`, `{"status":"expired_token","error":"..."}`, `{"status":"access_denied","error":"..."}`, or on grant: validates via `get_user_info()`, formats `user_id` as `"github:12345"`, mints ETIL JWT.
- **`POST /auth/token`** — Exchange provider access token for ETIL JWT (replaces stub). Request: `{"provider":"github","access_token":"gho_..."}`. Calls `get_user_info()`, mints JWT.
- **`OPTIONS /auth/*`** — CORS preflight for all three auth endpoints.
- **McpServer wiring** — Constructor creates `GitHubProvider`/`GoogleProvider` from enabled `auth_config_->providers` entries, stores in `oauth_providers_` map. `run_http()` wires pointer into `HttpTransportConfig`.
- **21 new unit tests** — `OAuthConfigTest` (6 tests: GitHub/Google/multi/disabled/missing-client-id/backward-compat parsing), `MockProviderTest` (11 tests: all `PollResult::Status` variants, device code success/fail, user info success/fail), `OAuthUserIdTest` (2 tests: GitHub/Google ID formatting), `OAuthJwtMintTest` (2 tests: known/unknown user JWT minting after OAuth).
- **New files**: `include/etil/mcp/oauth_provider.hpp`, `include/etil/mcp/oauth_github.hpp`, `include/etil/mcp/oauth_google.hpp`, `src/mcp/oauth_github.cpp`, `src/mcp/oauth_google.cpp`, `tests/unit/test_oauth_provider.cpp`
- 976 tests pass (debug), 932 with JWT OFF (no regressions)
- Version bumped to v0.6.9

### v0.6.10: TUI OAuth Login Flow — COMPLETE

- **`/login [provider]`** — Start OAuth device flow (default: github). Calls `POST /auth/device` via transport, displays user code + verification URI in output panel and notification bar, spawns background `asyncio.Task` to poll `/auth/poll` until grant/deny/expiry. Non-blocking — TUI stays responsive (F2/F3/keybindings work) during polling.
- **`/logout`** — Cancel in-progress login poll, revert bearer token to API key, clear cached JWT from `connections.json`, re-initialize MCP session.
- **`/whoami`** — Display current auth state (OAuth user/role/email or "API key").
- **Mutable bearer token** — `HttpStreamableTransport._bearer_token` field. `Authorization` header moved from client-level (baked at `start()`) to per-request (in `send()`, `shutdown()`). `update_bearer_token(jwt)` / `revert_to_api_key()` switch auth mode.
- **REST auth methods** — `auth_device(provider)` and `auth_poll(provider, device_code)` POST to `/auth/device` and `/auth/poll` (derived from MCP URL via `_auth_url()`).
- **JWT caching in connections.json** — `ConnectionInfo` extended with `jwt` + `jwt_expires_at` fields. `update_connection_jwt()` / `clear_connection_jwt()` helpers. On startup, cached JWT is validated (5-min expiry margin); if valid, used automatically; if expired, cleared with warning.
- **JWT decode** — `_decode_jwt_payload()` base64-decodes JWT payload section (no verification — server minted it) for display. `_jwt_is_expired()` checks expiry with configurable margin.
- **STDIO guard** — `/login` in STDIO mode shows "OAuth login requires HTTP transport" error.
- **Modified files**: `connections.py`, `config.py`, `http_transport.py`, `app.py`, `completer.py`, `help_renderer.py`, `__main__.py`
- Version bumped to v0.6.10

### v0.6.11: OAuth Deploy Config + GitHub JSON Fix — COMPLETE

- **GitHub device code JSON fix** — `request_device_code()` in `oauth_github.cpp` now sends `Accept: application/json` header, matching `poll_device_code()`. Without this, GitHub returned form-urlencoded response and `parse_form_urlencoded()` didn't URL-decode values, causing `verification_uri` to be `https%3A%2F%2F...`.
- **Deploy config for OAuth** — `docker-compose.prod.yml`, `deploy-ap1000.sh`, and `nginx-ap1000.conf` updated to support OAuth/JWT infrastructure:
  - Volume mount: host `/opt/etil/oauth/` → container `/etc/etil/` (read-only) for RSA keys and auth.json
  - `group_add: ["1006"]` (etilkey group) for container to read host key files
  - `ETIL_AUTH_CONFIG=/etc/etil/auth.json` env var
  - nginx `/auth/` location block with rate limiting (burst=50), CORS, proxy to container
- **Deployed and verified** on ap1000 — `/auth/device` returns proper JSON with clean `verification_uri`
- Version bumped to v0.6.11

### v0.6.12: Enforce Per-User Session Quotas — COMPLETE

- **Per-user session quota enforcement** — `create_session()` in `mcp_server.cpp` now checks per-user session count inside `sessions_mutex_` before inserting a new session. Looks up `RolePermissions::max_sessions` via `auth_config_->permissions_for(user_id)`, defaults to 2 if role not found. Counts existing sessions for the user via O(N) iteration (MAX_SESSIONS=100, negligible cost). Returns empty string (→ 503) if quota exceeded. API-key sessions (empty `user_id`) skip per-user quota — bounded by global `MAX_SESSIONS` only.
- **Gated by `#ifdef ETIL_JWT_ENABLED`** — compiles away when JWT is disabled, no regressions.
- 976 tests pass (debug), 898 release
- Version bumped to v0.6.12

### v0.6.14: Taint Bit on HeapString and HeapByteArray — COMPLETE

- **Taint field on HeapObject** — `bool tainted_{false}` between `kind_` and `refcount_`, fitting in existing alignment padding (HeapObject stays 24 bytes). `is_tainted()`/`set_tainted()` accessors. Available on all heap types (HeapString, HeapArray, HeapByteArray, HeapMap).
- **`HeapString::create_tainted()`** — Factory method for creating pre-tainted strings from external sources.
- **`staint` primitive** — `( string -- bool )` queries the taint bit, returns Boolean. Registered under string category.
- **Taint sources** — `http-get` response bytes, `read-file`/`read-file-sync` content, `readdir`/`readdir-sync` filenames all marked tainted. Both async and sync-fallback paths covered (8 sites total).
- **Taint propagation** — `s+` (tainted if either operand tainted via new `pop_as_tainted_string()` helper), `substr`/`strim`/`sreplace` (inherit source taint), `sprintf` (tainted if format string or any `%s` arg tainted via `value_to_str_taint()`), `ssplit` (all elements inherit source taint), `sjoin` (any tainted element taints result), `bytes->string`/`string->bytes` (propagate across type boundary).
- **`sregex-replace` unconditionally untaints** — Regex validation is a sanitization step. `// TODO: gate on evaluate_tainted permission` for future phase.
- **21 new unit tests** in `test_taint.cpp` — HeapObject field, create_tainted, sizeof check, staint flags, s+ (3 combos), substr/strim/sreplace propagation, sregex-replace untaint, bytes<->string, ssplit/sjoin, interpreter integration.
- **9 new TIL integration tests** in `test_taint.til` — literal clean, s+/substr/strim/sreplace/sregex clean, number coercion, ssplit/sjoin clean.
- 997 tests pass (debug), 919 release
- Version bumped to v0.6.14

### v0.6.16: TUI `--exec` / `--execux` Script Execution — COMPLETE

- **`--exec <file|URL>`** — Read a `.til` file or URL, feed lines through the TUI pipeline as if typed at the console, then exit. Lines dispatched via `_handle_til_code` / `_handle_meta_command` (same path as interactive input). Each line echoed in output panel via `submit_command()` for log capture.
- **`--execux <file|URL>`** — Same as `--exec` but stays in interactive mode after EOF.
- **Mutually exclusive** — `argparse` mutually exclusive group prevents using both.
- **`_load_exec_source()`** — Helper in `__main__.py` loads from URL (via `urllib.request.urlopen`) or file path. Filters empty lines. Prints error to stderr and exits 1 on failure.
- **`_exec_script_lines()`** — Async method on `EtilMcpApp`. Executes lines sequentially (await each before next). Uses `_handle_til_code` / `_handle_meta_command` directly (bypasses `_command_in_flight` guard). Calls `action_quit()` at EOF when `exec_exit` is true.
- **`_maybe_start_exec()`** — Spawns `_exec_script_lines()` via `asyncio.create_task()`. Called from both the direct init path (after `_initialize_session()` in `on_mount`) and the OAuth path (after `_initialize_session()` in `_handle_login_success`).
- **`ClientConfig` fields** — `exec_lines: list[str] | None` and `exec_exit: bool`.
- **Modified files**: `config.py`, `__main__.py`, `app.py`
- Version bumped to v0.6.16

### v0.7.0: MongoDB Integration — COMPLETE

- **`etil::db::MongoClient`** — Generic MongoDB client with connection pooling (mongocxx), multi-connection config (JSON file or env vars), CRUD operations (find, insert, update, remove), `MongoConnectionsConfig` with named connections and `resolve()` priority chain (env vars override config file). User management and audit logging mixed into this class.
- **4 TIL primitives** — `mongo-find`, `mongo-insert`, `mongo-update`, `mongo-delete` (gated by `ETIL_MONGODB_ENABLED`). Per-role permission enforcement via `RolePermissions::mongo_access`.
- **CMake flag**: `ETIL_BUILD_MONGODB=ON` (default OFF). Requires mongocxx/bsoncxx static libraries.
- **Test counts**: 1044 (debug), 1044 (release)
- Version bumped to v0.7.0

### v0.7.1: Refactor MongoDB AAA into `etil::aaa` — COMPLETE

- **Separation of concerns** — Extracted Authentication, Authorization, and Auditing logic from `MongoClient` into dedicated `etil::aaa` namespace, leaving `MongoClient` as a pure CRUD + connection client.
- **`etil::aaa::UserStore`** — User management backed by `MongoClient` generic CRUD API. Methods: `find_by_email()`, `create()`, `record_login()`, `get_role()`, `ensure_indexes()`. `UserRecord` struct moved from `mongo_client.hpp`.
- **`etil::aaa::AuditLog`** — Typed audit logging with fire-and-forget semantics and internal null/connection guards. Methods: `log_permission_denied()`, `log_session_create()`, `log_session_destroy()`, `log_login()`, `log_user_created()`, `ensure_indexes()` (30-day TTL index).
- **`on_login_success()`** — Free function consolidating two identical 24-line OAuth login blocks from `http_transport.cpp` into a single call.
- **`MongoClient` additions** — `ensure_unique_index()` and `ensure_ttl_index()` generic infrastructure methods (not domain-specific).
- **`tool_handlers.cpp` cleanup** — 4 copy-pasted 7-line permission-denied audit blocks replaced with single-line `audit_log_->log_permission_denied()` calls.
- **Net result**: -242 lines (89 additions, 331 deletions across 10 files + 5 new files)
- **Test counts**: 1044 (debug), 1044 (release)
- Version bumped to v0.7.1

### v0.7.2: make_primitive(), MongoDB Local Build Fixes, Linker Maps — COMPLETE

- **`make_primitive()` free function** — Extracted from 10 identical 12-line `make_word` lambdas across 11 source files into a single reusable function in `primitives.hpp`/`primitives.cpp`. Creates a primitive `WordImpl` with name, native function, type signature, weight=1.0, generation=0, marked as primitive. Net -70 lines.
- **MongoDB local build fixes** — `ETIL_BUILD_MONGODB=ON` now compiles cleanly outside Docker. Fixes: missing `bsoncxx/builder/basic/array.hpp` include in `user_store.cpp`, `std::optional<Value>` dereference fixes in `test_mongo_client.cpp`, incomplete type `MongoClientState` in `tool_handlers.cpp`, mongo-c-driver internal fixture tests suppressed via `BUILD_TESTING` toggle + `EXCLUDE_FROM_ALL`.
- **Linker map generation** — `etil_add_link_map()` CMake function generates `.map` files for all 8 executables via `-Wl,-Map` linker option with per-target output paths.
- **Test counts**: 1044 (MongoDB OFF), 1082 (MongoDB ON)
- Version bumped to v0.7.2

### v0.7.3: mongo-query, mongo-count, Unlimited Query Quota — COMPLETE

- **`mongo-query` primitive** — `( collection filter-json options-json -- result-json flag )` — Find with query options. Options JSON supports `skip` (integer), `limit` (integer), and `sort` (object with `field:1` or `field:-1`). Uses `mongocxx::options::find` for server-side skip/limit/sort.
- **`mongo-count` primitive** — `( collection filter-json -- count flag )` — Count documents matching a filter. Returns integer count without transferring full document payloads.
- **`MongoClient::find()` overload** — New overload accepting `int64_t skip`, `int64_t limit`, `const std::string& sort_json` parameters.
- **Unlimited query quota** — `MongoClientState::can_query()` now treats `query_quota <= 0` as unlimited. Admin role in `auth.json.example` changed from 10000 to 0 (unlimited).
- **Test counts**: 1085 (debug + release, MongoDB ON)
- Version bumped to v0.7.3

### v0.7.4: Redesign MongoDB Primitives — Dual API (JSON + HeapMap) — COMPLETE

- **Unified API** — All 5 MongoDB words accept String, Json, or Map for filter/document/options parameters. `mongo-find` returns `HeapJson` directly (no string parsing needed).
- **`MongoQueryOptions` struct** — Unified options struct with owned `bsoncxx::document::value` fields (not views), eliminating the dangling BSON view class of bugs. Supports: `skip`, `limit`, `sort`, `projection`, `hint`, `collation`, `max_time_ms`, `batch_size`, `upsert`.
- **Two factory functions** — `options_from_json()` parses recognized keys from a JSON string; `options_from_map()` extracts recognized keys from a HeapMap. Both produce the same `MongoQueryOptions`.
- **`heap_map_to_bson()` recursive conversion** — Walks HeapMap recursively: Integer→BSON int64, Float→BSON double, String→BSON UTF-8, Map→BSON document, Array→BSON array.
- **`MongoClient` rewritten** — Methods now accept `bsoncxx::document::view` + `MongoQueryOptions` instead of JSON strings. New `count()` method uses `count_documents()` (server-side, O(index) not O(N)). All BSON views derived from owned `document::value` locals. `update()` supports upsert, hint, collation. `remove()` supports hint, collation.
- **`mongo-query` removed** — `mongo-find` with opts-json subsumes it.
- **`mongo-count` fixed** — Was O(N) (fetched ALL documents as JSON, parsed, counted). Now uses `count_documents()`.
- **Dangling BSON view bug fixed** — `opts.sort(bsoncxx::from_json(sort_json).view())` in old code destroyed the temporary, leaving a dangling view. Fixed by storing BSON documents as owned `document::value` in `MongoQueryOptions`.
- **Stack signature changes**: `mongo-find` now takes `opts-json` (was 2 args, now 3). `mongo-count` now takes `opts-json` (was 2 args, now 3). `mongo-update` now takes `opts-json` (was 3 args, now 4). `mongo-delete` now takes `opts-json` (was 2 args, now 3).
- **HeapMap `entries()` accessor** — New `const` accessor for read-only iteration (used by `heap_map_to_bson()`).
- **Test counts**: 1088 (debug + release, MongoDB ON)
- Version bumped to v0.7.4

### v0.7.5: First-Class Boolean Type — COMPLETE

- **`Value::Type::Boolean`** — New value type with `Value(bool)` constructor, `as_bool()` accessor. Stored as `as_int` (0/1) with `Type::Boolean` discriminator. Not a heap type (no refcounting needed).
- **4 new C++ primitives** — `true` (`( -- bool )`), `false` (`( -- bool )`), `not` (`( x -- bool )` logical negation with type coercion), `bool` (`( x -- bool )` explicit conversion). `true`/`false`/`not` moved from self-hosted `builtins.til` to C++ primitives.
- **All comparisons return Boolean** — `=`, `<>`, `<`, `>`, `<=`, `>=`, `0=`, `0<`, `0>` now push `Value(bool)` instead of FORTH flags (`-1`/`0`).
- **All predicates return Boolean** — `s=`, `s<>`, `staint`, `sregex-search`, `sregex-match`, `xt?`, `map-has?`, `word-read`, `string-read-delim`, `dict-forget`, `dict-forget-all`, `file-load`, `dict-meta-*`, `impl-meta-*`, `exists?`, `exists-sync`, all file I/O success flags, HTTP success flag.
- **Control flow requires Boolean** — `BranchIfZero` renamed to `BranchIfFalse`. `if`/`while`/`until` now require Boolean on TOS; Integer/Float causes a runtime error. Use `bool` or `0=`/`0<`/`0>` to convert integers to booleans.
- **`PushBool` instruction** — New bytecode opcode for boolean literals in compiled words.
- **Logical operator overload** — `and`/`or`/`xor`: both Boolean→logical (returns bool), both Integer→bitwise (returns int), mixed→error. `invert`: Boolean→logical NOT, Integer→bitwise NOT.
- **Arithmetic rejects Boolean** — `+`, `-`, `*`, `/`, `mod`, `/mod`, `negate`, `abs` all error on Boolean operands.
- **BSON native boolean** — `value_to_bson()` and `value_to_bson_array()` emit native BSON booleans, fixing the original motivation: `{"active": true}` vs `{"active": -1}` in MongoDB queries.
- **`.` prints `true`/`false`** — `prim_dot`, `prim_dot_s`, `dump_value`, `format_value`, `pop_as_string` all handle Boolean type.
- **Test counts**: 1088 (debug + release, MongoDB ON)
- Version bumped to v0.7.5

### v0.7.6: MongoDB x.509 TLS Deploy Config — COMPLETE

- **Deploy script x.509 support** — `deploy-ap1000.sh` mounts MongoDB client cert and CA into container (`-v /opt/mongodb/client-etil.pem:/etc/etil-mongo/client-etil.pem:ro`, `-v /opt/mongodb/ca.pem:/etc/etil-mongo/ca.pem:ro`). MongoDB URI read from `/opt/mongodb/etil-uri` file to avoid `$external` shell escaping through SSH heredoc + eval layers.
- **docker-compose.prod.yml** — Added cert volume mounts and default `ETIL_MONGODB_URI` with x.509 TLS params (`tls=true`, `tlsCertificateKeyFile`, `tlsCAFile`, `authMechanism=MONGODB-X509`, `authSource=$external`).
- **No C++ changes** — Deploy infrastructure only.
- Version bumped to v0.7.6

### v0.7.7: Fix Deploy Script $external Shell Escaping — COMPLETE

- **`--env-file` replaces `-e` flags** — Deploy script now writes env vars to a temp file and passes `--env-file` to `docker run` instead of building individual `-e` flags through `eval`. This avoids `$external` in the MongoDB URI being expanded as an unbound variable under `set -euo pipefail`.
- Version bumped to v0.7.7

### v0.8.0: HeapJson Type — First-Class JSON Values — COMPLETE

- **HeapJson** — New heap-allocated JSON container wrapping `nlohmann::json`. Same refcounting pattern as other heap types. `HeapObject::Kind::Json` and `Value::Type::Json` added to type system. Eliminates redundant JSON parsing — pre-parsed JSON stored on the stack and reused.
- **`j|` handler word** — JSON literal syntax: `j| {"key": "value"} |` parses text to closing `|` as JSON, pushes `HeapJson`. Works in both interpret and compile mode (`PushJson` bytecode instruction).
- **12 new JSON primitives** — `json-parse` (string→json), `json-dump` (json→compact string), `json-pretty` (json→indented string), `json-get` (object key lookup with type-appropriate return), `json-length` (array/object size), `json-type` (type name string), `json-keys` (object keys as array), `json->map` (recursive JSON→HeapMap), `json->array` (recursive JSON→HeapArray), `map->json` (recursive HeapMap→JSON), `array->json` (recursive HeapArray→JSON), `json->value` (auto-unpack to native ETIL type).
- **Full type system integration** — Json cases in `.`, `.s`, `dump`, `see`, `arith_binary` (reject), `pop_as_string`, `format_value`, `tool_get_stack` (MCP JSON), `value_to_bson` (MongoDB).
- **Dual-accept in mongo-\* words** — `mongo-find`, `mongo-count`, `mongo-insert`, `mongo-update`, `mongo-delete` now accept either String or Json on the stack for filter/document parameters. `pop_json_string()` helper handles the type check.
- **New files**: `include/etil/core/heap_json.hpp`, `include/etil/core/json_primitives.hpp`, `src/core/json_primitives.cpp`, `tests/unit/test_json_primitives.cpp`, `tests/til/test_json.til`, `tests/til/test_json.sh`
- **Test counts**: 1123 (debug + release)
- Version bumped to v0.8.0

### v0.8.1: mongo-find Returns HeapJson, Remove mongo-map-* Words — COMPLETE

- **`mongo-find` returns `HeapJson`** — Result is parsed into `nlohmann::json` and wrapped in `HeapJson` instead of returning a raw string. No more `json-parse` needed after `mongo-find`.
- **Removed 5 `mongo-map-*` words** — `mongo-map-find`, `mongo-map-count`, `mongo-map-insert`, `mongo-map-update`, `mongo-map-delete` removed. Redundant now that all `mongo-*` words accept String, Json, or Map via `pop_json_string()` and `pop_options()`. Use `map->json` to convert HeapMap before passing to `mongo-*`.
- **Deploy script GID fix** — Added `--group-add 110` (mongodb) to container so it can read `ca.pem`. Previously only had GID 1006 (etilkey) for `client-etil.pem`.
- **Test counts**: 1118 (debug + release, 5 fewer from removed mongo-map-* tests)
- Version bumped to v0.8.1

### v0.8.2: json-get Supports Array Index Access — COMPLETE

- **`json-get` accepts integer index** — In addition to string keys for objects, `json-get` now accepts an integer index for JSON arrays: `j| [10, 20, 30] | 1 json-get` → `20`. Enables direct element access on `mongo-find` results without `json->array` conversion. Bounds-checked with clear error messages.
- **6 new unit tests** — Array index, first element, nested object chain, out-of-range, negative index, integer-on-object error.
- **3 new TIL integration tests** — Array index 0, index 2, array→object chain.
- **Test counts**: 1124 (debug + release)
- Version bumped to v0.8.2

### v0.8.3: Session Idle Timeout, Heartbeat, TUI dpkg Packaging — COMPLETE

- **Session idle timeout with warnings** — Sessions idle for >30 minutes are automatically reaped by `cleanup_idle_sessions()` (runs on every `create_session()`). When a session that was idle for >25 minutes sends a request, a warning notification is emitted: "Session was idle — would have expired in N minutes. Timer reset."
- **TUI heartbeat keepalive** — TUI sends `get_session_stats` every 5 minutes to keep the server session alive. Active TUI sessions never get reaped. Heartbeat starts on connect, stops on quit/logout, restarts on reconnect.
- **TUI `.deb` packaging** — Moved to the `etil-tui` repository (`scripts/build-tui-deb.sh`). CI `tui-build` tool clones from the separate repo.
- **Google OAuth deployed** — Google device flow working end-to-end on ap1000. `google:*` users default to `beta-tester` role for RBAC testing.
- **Test counts**: 1124 (debug + release)
- Version bumped to v0.8.3

### v0.8.4: Split Auth Config, TUI Help Fixes — COMPLETE

- **Split auth config into 3 files** — `AuthConfig::from_directory()` reads `roles.json` (roles + default_role), `keys.json` (JWT keys, TTL, OAuth providers), and `users.json` (user→role mappings) from a directory. Missing files are silently skipped. `from_file()` retained for backward compat and tests. `ETIL_AUTH_CONFIG` env var now points to a directory instead of a file.
- **TUI help category fixes** — `.s` moved from 'Io' to 'Debug' category (eliminating the lone 'Io' category). 'Compiling' category moved before 'Control Flow' in the help index.
- **Deploy script migration** — `deploy-ap1000.sh` now rewrites existing `ETIL_AUTH_CONFIG` values to the directory path (handles upgrade from file path).
- **Test counts**: 1127 (debug + release, 3 new from_directory tests)
- Version bumped to v0.8.4

### v0.8.9: Pre-Built CI Dependencies, http-get Headers Parameter — COMPLETE

- **Pre-built FetchContent dependencies for CI** — All 12 FetchContent dependencies pre-built as static libraries into `/opt/etil-deps/v1/{debug,release}` on ap1000. `cmake/Dependencies.cmake` wraps each dep in a `find_package()` guard with FetchContent fallback. Target name aliases for libuv (`uv_a` ← `libuv::uv_a`) and mongo drivers (`mongocxx_static` ← `mongo::mongocxx_static`). CI passes `-DCMAKE_PREFIX_PATH` + `-DFETCHCONTENT_FULLY_DISCONNECTED=ON`. Eliminates ~4-6 min of git clone + compile per pipeline run and GitHub rate-limit flakiness.
- **`ci/deps/` superbuild** — New `ExternalProject_Add` superbuild (`CMakeLists.txt`), build wrapper (`build-deps.sh`), and version manifest (`manifest.json`). Mongo drivers built with `ninja -j1` to avoid OOM. All other deps built in parallel.
- **`http-get` headers parameter** — Changed from `( url -- bytes code flag )` to `( url headers-map -- bytes code flag )`. HeapMap entries with string values are converted to HTTP headers. Use `map-new` for no extra headers.
- **super-push.sh cleanup** — Removed `--no-deploy` flag (deployment handled by CI post-receive hook).
- **7 new unit tests** — `test_http_get.cpp`: underflow, wrong type, empty map, map with headers, multiple headers, string-instead-of-map.
- **Test counts**: 1134 (debug + release)
- Version bumped to v0.8.9

### v0.8.13: http-post, HeapMatrix Default ON, CI/Deploy Fixes — COMPLETE

- **`http-post` primitive** — `( url headers-map body-bytes -- bytes status-code flag )` performs HTTP/HTTPS POST request. Body is a `HeapByteArray` (use `string->bytes` to convert). Content-Type extracted from headers map (default: `application/octet-stream`). Same domain allowlist, fetch budgets, SSRF protection, and taint marking as `http-get`. Shares the same per-interpret (10) and per-session (100) budget pool.
- **HeapMatrix always-on** — HeapMatrix (25 linalg primitives) compiled unconditionally (ETIL_BUILD_LINALG gate removed in v0.8.23). Dockerfile runtime image includes `liblapacke`.
- **TUI help browser categories** — Added `json` (13 words), `matrix` (25 words), and `mongodb` (5 words) categories to `help_renderer.py`.
- **Deploy smoke test retry** — `scripts/deploy.sh` smoke test now retries up to 3 times with 5-second delays, fixing false failures when the server hasn't fully started.
- **CI pre-built deps** — CI `jobs.py` updated to use `CMAKE_PREFIX_PATH` + `FETCHCONTENT_FULLY_DISCONNECTED=ON` with pre-built deps at `/opt/etil-deps/v1/`. Clean build time reduced from ~22 min to ~11 min.
- **6 new unit tests** — `test_http_get.cpp` (renamed fixture to `HttpPrimitivesTest`): POST underflow, wrong body type, wrong headers type, empty body no state, body with headers no state, string-instead-of-bytes.
- **Test counts**: 1171 (debug + release)
- Version bumped to v0.8.13

### v0.8.14: LAPACK Words Return Boolean Flags — COMPLETE

- **Boolean flag convention** — 6 matrix solver/decomposition words (`mat-solve`, `mat-inv`, `mat-det`, `mat-eigen`, `mat-svd`, `mat-lstsq`) now push `Value(info == 0)` (Boolean `true`=success, `false`=failure) instead of `Value(static_cast<int64_t>(info))` (integer LAPACK info code). Aligns with ETIL's opaque Boolean convention established in v0.7.5.
- **Type signatures updated** — Registration changed from `T::Integer` to `T::Unknown` for flag outputs (TypeSignature enum has no Boolean variant).
- **Help metadata updated** — `data/help.til` descriptions changed from "Flag 0=success" to "Flag true=success" for `mat-solve`, `mat-inv`, `mat-det`.
- **TUI WORDS.md updated** — Matching flag description changes in `etil-tui/WORDS.md`.
- **README.md fix** — `mat-solve` example now includes `drop` to consume the success flag before `mat.`.
- **Test counts**: 1171 (debug + release)
- Version bumped to v0.8.14

### v0.8.15: Admin MCP Tools (Role/User Management) — COMPLETE

- **`role_admin` permission** — New boolean field on `RolePermissions` (default `false`). Gates all 8 admin tools. Parsed from `roles.json`.
- **Thread-safe config reload** — `auth_config_` changed from `unique_ptr<AuthConfig>` to `shared_ptr<const AuthConfig>`. Admin tools use copy-on-write: copy current config, mutate the copy, atomically swap via `std::atomic_store`. All readers (permission checks, role lookups) continue using the old config until the swap completes.
- **Serialization** — `roles_to_json()` and `users_to_json()` serialize config back to JSON. `write_json_atomic()` does temp-file + `std::filesystem::rename` for crash-safe persistence.
- **`parse_role_permissions()`** — Static method on `AuthConfig` for parsing a `RolePermissions` from a JSON object. Reuses the same field-reading helpers as the config loader.
- **8 admin MCP tools** (all gated by `role_admin` permission):
  - `admin_list_roles` — List all roles and default role
  - `admin_get_role` — Full permissions dump for a named role
  - `admin_set_role` — Create or update a role (persists to `roles.json`)
  - `admin_delete_role` — Delete a role (prevents deleting default role or roles with assigned users)
  - `admin_list_users` — List all user-to-role mappings
  - `admin_set_user_role` — Assign a role to a user (validates role exists, persists to `users.json`)
  - `admin_delete_user` — Remove a user mapping (user falls back to default role)
  - `admin_reload_config` — Reload auth configuration from disk
- **Test counts**: 1171 (debug + release)
- Version bumped to v0.8.15

### v0.8.16: Fix JwtAuth Use-After-Free, Auth Config Mount RW — COMPLETE

- **JwtAuth use-after-free fix** — `JwtAuth` held a raw `const AuthConfig*` pointer. When admin tools (`admin_set_role`, `admin_delete_role`, etc.) swapped the global `auth_config_` via `std::atomic_store`, the old `AuthConfig` was freed, leaving `JwtAuth` with a dangling pointer. All subsequent JWT validations were undefined behavior (typically returning failure → 401 for all JWT-authenticated requests). Fixed by making `JwtAuth` own copies of the PEM key strings and TTL. `mint_token()` now takes the role as a parameter (caller resolves via `auth_config->role_for()`).
- **Auth config mount read-write** — `/opt/etil/oauth:/etc/etil` bind mount changed from `:ro` to read-write in `deploy.sh` (local + remote paths) and `docker-compose.prod.yml`. Admin tools need to persist `roles.json` and `users.json` changes via atomic temp-file + rename.
- **Test counts**: 1171 (debug + release)
- Version bumped to v0.8.16

### v0.8.21: MLP Primitives — Neural Network Forward/Backward/Classification — COMPLETE

- **17 new matrix primitives** for building multilayer perceptrons in pure TIL, organized in 3 stages:
  - **Stage 1 (Forward Pass)**: `mat-relu`, `mat-sigmoid`, `mat-tanh` (activation functions); `mat-hadamard` (element-wise multiply); `mat-add-col` (broadcast-add column vector); `mat-randn` (standard normal distribution via Box-Muller using `prng_engine()`)
  - **Stage 2 (Backpropagation)**: `mat-relu'`, `mat-sigmoid'`, `mat-tanh'` (derivatives from pre-activation input); `mat-sum`, `mat-col-sum`, `mat-mean` (reduction operations); `mat-clip` (element-wise clamp)
  - **Stage 3 (Classification)**: `mat-softmax` (column-wise, numerically stable); `mat-cross-entropy` (cross-entropy loss); `mat-apply` (execute xt per element, supports native and bytecode words)
- **1 new scalar primitive**: `tanh` (`( x -- tanh(x) )`) in `primitives.cpp` alongside `sin`/`cos`/`tan`.
- **3 TIL-level convenience words** in `help.til`: `mat-xavier` (Xavier/Glorot initialization), `mat-he` (He initialization for ReLU), `mat-mse` (mean squared error loss).
- **`prng_engine()` exposed** in `primitives.hpp` — `mat-rand` refactored to use shared PRNG so `random-seed` controls all randomness for reproducible ML experiments.
- **Fixed pre-existing TIL test bugs** in `test_matrix.til` — compile-only words (`if`/`else`/`then`) were used at top level; solver flag comparisons updated from integer `0` to Boolean `true`.
- **Design document**: `docs/claude-design/20260310-ETIL-MLP-Primitives-Plan.md`
- **Test counts**: 1199 (debug + release)
- Version bumped to v0.8.21

### v0.8.22: Symmetrical Matrix Conversion + Interpreter Resilience — COMPLETE

- **3 new conversion words**: `mat->json` (serialize HeapMatrix to HeapJson with rows/cols/data keys), `json->mat` (deserialize HeapJson back to HeapMatrix with full validation), `mat->array` (convert HeapMatrix to HeapArray in row-major order, inverse of `mat-from-array`). All three provide symmetrical serialization paths for matrix data.
- **ASan leak fix**: `\d` regex patterns in `help.til` caused `read_escaped_string` to fail on unknown escape, orphaning HeapStrings on the stack. Fixed with `\\d` (literal backslash-d).
- **Interpreter resilience**: `line_had_error_` flag added to `Interpreter`. `interpret_line()` sets it at all error sites. `load_file()` snapshots stack depth before each line and cleans up orphaned values (with `release()`) only when the line errored and the stack grew. Prevents ASan leaks from any future startup file errors without breaking legitimate stack effects.
- **Repo cleanup**: Deleted `etil-ci.tar.gz` from ETIL repo, genericized ap1000 references in `super-push.sh`.
- **Test counts**: 1207 (debug + release)
- Version bumped to v0.8.22

### v0.8.24: Fix pop_* Value Leak, Improve `.`/`.s` Output — COMPLETE

- **Fix pop_* helpers leaking values on type mismatch** — All 6 `pop_*` helpers in `value_helpers.hpp` (`pop_string`, `pop_array`, `pop_byte_array`, `pop_map`, `pop_json`, `pop_matrix`) silently ate stack values when the type didn't match — the value was popped but never pushed back or released. This corrupted the stack, causing subsequent operations to interpret garbage as heap pointers (segfault). Fixed by pushing the value back on type mismatch. Affects ~85 call sites across the codebase.
- **Fix map primitive stack restoration** — `prim_map_set`, `prim_map_get`, `prim_map_remove`, `prim_map_has` now reconstruct the key string and push it back when `pop_map` fails, fully restoring the stack. Removed dead `pop_string_key` helper.
- **`.` prints string content** — `prim_dot` now prints the actual string value instead of `<string>`. Collections show their size: `<array:N>`, `<bytes:N>`, `<map:N>`.
- **`.s` shows string content** — `prim_dot_s` shows string content truncated to 64 characters with `...` suffix. Collections show size: `<array:N>`, `<bytes:N>`, `<map:N>`.
- **7 new type mismatch tests** — Verify stack is fully restored when a non-map value is on the stack where a map is expected.
- **Test counts**: 1214 (debug + release)
- Version bumped to v0.8.24

### v0.9.0: Observable Type System — RxJS-Style Reactive Pipelines — COMPLETE

- **HeapObservable** — New heap-allocated Observable node forming a linked-list pipeline. Each node represents one step in a lazy pipeline (creation, transform, accumulate, limit, combine). Nodes carry per-node state for closure emulation in the stack-based language. `HeapObject::Kind::Observable` and `Value::Type::Observable` added to type system.
- **Push-based execution engine** — `execute_observable()` recursive function handles all 16 operator kinds. Pipeline builds nodes lazily; terminal operators (subscribe, reduce, to-array, count) trigger recursive execution from source to terminal. Each emission checks `ctx.tick()` for instruction budget/timeout/cancellation.
- **21 new TIL primitives** — 4 creation (`obs-from`, `obs-of`, `obs-empty`, `obs-range`), 4 transform (`obs-map`, `obs-map-with`, `obs-filter`, `obs-filter-with`), 2 accumulate (`obs-scan`, `obs-reduce`), 3 limiting (`obs-take`, `obs-skip`, `obs-distinct`), 3 combination (`obs-merge`, `obs-concat`, `obs-zip`), 3 terminal (`obs-subscribe`, `obs-to-array`, `obs-count`), 2 introspection (`obs?`, `obs-kind`).
- **`-with` variants** — `obs-map-with` and `obs-filter-with` carry a per-node context value, enabling closure-like data binding without language-level closures.
- **4 array iteration primitives** — `array-each`, `array-map`, `array-filter`, `array-reduce` with `IterGuard` RAII for automatic cleanup. Prerequisite for observable pattern validation.
- **Full type system integration** — Observable cases added to: `prim_dot`, `prim_dot_s`, `dump_value`, `format_value`, `pop_as_string`, `tool_get_stack` (MCP JSON).
- **Help metadata** — All 21 observable words have description, stack-effect, category, and examples in `data/help.til`.
- **New files**: `include/etil/core/heap_observable.hpp`, `src/core/observable_primitives.cpp`, `tests/unit/test_observable_primitives.cpp`, `tests/til/test_observable.til`, `tests/til/test_observable.sh`
- **Test counts**: 1272 (debug + release)
- Version bumped to v0.9.0

### v0.9.1: Refactor Phases 1-3 + Security Hardening Phase 4 — COMPLETE

- **Refactor Phase 1** — DRY macros, templates, and DoS size limits across matrix, observable, and core primitives. Net -192 lines.
- **Refactor Phase 2** — Table-driven primitive registration, merge copy-paste pairs, eliminate `make_word` lambda pattern. Net -446 lines across 11 files.
- **Refactor Phase 3** — Fix compiled_body cache use-after-free and DNS rebinding in URL validation, consolidate file I/O into `file_io_helpers.hpp`, extract MCP server init methods. Net -160 lines.
- **Security Phase 4** — JWT validation hardening (reject `none` algorithm, enforce RS256), input size caps on async file I/O and file_io_primitives, SSRF domain blocklist expansion. 90 new JWT auth tests, 34 new URL validation tests.
- **Documentation overhaul** — Deleted `ARCHITECTURE.md`, massively expanded `README.md` with 12 appendix sections (H-S) and appendix index. Added ETIL logo images. Updated `ATTRIBUTION.md` and `BUILD_INSTRUCTIONS.md`.
- Version bumped to v0.9.1

### v0.9.2: OpenBLAS Build Fixes + Array Iteration Help — COMPLETE

- **FetchContent OpenBLAS build fix** — Enable all OpenBLAS features by default, fix LAPACKE link for system OpenBLAS in CI environment.
- **Help entries** — Added descriptions, stack effects, and categories for `array-each`, `array-map`, `array-filter`, `array-reduce`.
- Version bumped to v0.9.2

### v0.9.3: Matrix API Rename — COMPLETE

- **`mat-from-array` → `array->mat`** — Renamed to idiomatic Forth-style arrow notation. Now accepts nested 2D arrays (array of arrays). `mat->array` now returns nested 2D arrays (was flat row-major).
- Version bumped to v0.9.3

### v0.9.4: Per-Role Session Idle Timeouts — COMPLETE

- **`session_idle_timeout_seconds`** — New field on `RolePermissions` (default 1800s / 30 min). Parsed from `roles.json`. `cleanup_idle_sessions()` uses per-role timeout instead of global constant. Idle warning threshold scales proportionally.
- **`admin_set_default_role` tool** — Included in session info responses so clients know their resolved role.
- **85 new JWT auth tests** covering per-role idle timeout parsing and enforcement.
- Version bumped to v0.9.4

### v0.9.5: Temporal Observable Stage 1 — COMPLETE

- **13 temporal observable primitives** — `obs-timer` (single delayed emission), `obs-delay` (delay each emission), `obs-timeout` (error if no emission within deadline), `obs-debounce-time` (suppress rapid-fire), `obs-throttle-time` (rate limit), `obs-buffer-time` (collect into time windows), `obs-timestamp` (attach time metadata), `obs-time-interval` (emit elapsed time between items), `obs-audit-time` (sample latest after silence), `obs-sample-time` (periodic sampling), `obs-take-until-time` (complete after duration), `obs-delay-each` (per-item delay), `obs-retry-delay` (retry with backoff delay).
- **Design documents** — `docs/claude-design/20260315-observable-temporal-design.md` (534 lines) and `20260315-observable-temporal-plan.md` (98 lines).
- **318 new unit tests** in `test_observable_primitives.cpp`, **123 TIL integration tests** in `test_observable.til`.
- **New files**: Extended `include/etil/core/heap_observable.hpp` and `src/core/observable_primitives.cpp` with temporal operator kinds.
- Version bumped to v0.9.5

### v0.9.9: Per-Role Execution Time Limits — COMPLETE

- **`interpret_execution_limit`** — New `RolePermissions` field (default 30s). Per-interpret-call wall-clock timeout replacing the hardcoded `MCP_TIMEOUT_SECONDS`. Value of 0 means effectively unlimited (365-day timeout). Parsed from `roles.json`, serialized by `role_to_json()`.
- **`session_execution_limit`** — New `RolePermissions` field (default 0/unlimited). Cumulative wall-clock execution time across all interpret calls in a session. When exceeded, appends descriptive error to response and sets `force_terminate` flag on the session.
- **`Session::force_terminate`** — New bool field. Checked in `handle_message()` after releasing the session mutex; triggers `destroy_session()` so the error response is delivered before teardown.
- **`handle_message()` restructured** — Session lock moved into a scope block so `force_terminate` can be checked after the lock is released, avoiding mutex-destruction-while-held UB.
- **TUI `admin_formatter.py`** — Both fields added to `_SECTIONS` (System group) and `_PERM_SCHEMA` (23 keys total).
- **`roles.json.example`** — Admin: unlimited (0/0), researcher: 30s per-call / unlimited session, beta-tester: 30s per-call / 300s session.
- Version bumped to v0.9.9

### Phase 5: Selection Engine — COMPLETE (v1.2.0)

- 4 strategies: Latest, WeightedRandom, EpsilonGreedy, UCB1
- Wired into interpreter + bytecode execution paths
- 3 TIL primitives: `select-strategy`, `select-epsilon`, `select-off`

### Phase 6: Evolution — COMPLETE (v1.6.0)

- **MLP Library** (v1.1.0): feedforward neural networks in TIL, XOR training demo
- **Evolution Engine** (v1.3.0): GeneticOps, Fitness evaluation, population management
- **AST-Level Evolution** (v1.3.2-v1.5.0): 7-stage pipeline — marker opcodes, decompiler, AST compiler, stack simulator, type-directed repair, semantic tags, AST genetic operators
- **Stage 6 Completion** (v1.5.1-v1.6.0): remove_weakest fix, evolve-* TIL primitives, P0 bug fixes, DRY refactoring, block move/control flow mutation, validity tests, compile-time type inference
- 4 mutation operators: substitute (tiered by semantic tags), perturb (Gaussian noise), move (relocate WordCall), control flow (wrap/unwrap if/then)
- Compile-time type inference: every `:` definition gets a `TypeSignature` at `;`
- 1362 DT tests + 3 NDT tests (non-deterministic, run locally only)

### Phase 7: JIT

6. **Compiler.hpp** — LLVM integration
7. **IRGenerator.hpp** — ByteCode → LLVM IR
8. **CodeCache.hpp** — Cache compiled code

---

## Common Tasks

### Adding a New Primitive Word

```cpp
// 1. Declare in include/etil/core/primitives.hpp
bool prim_myword(ExecutionContext& ctx);

// 2. Implement in src/core/primitives.cpp
bool prim_myword(ExecutionContext& ctx) {
    auto val = ctx.data_stack().pop();
    if (!val) return false;
    // ... operate ...
    ctx.data_stack().push(result);
    return true;
}

// 3. Register in register_primitives() in src/core/primitives.cpp
//    make_primitive() creates a WordImpl with name, function, type signature
dict.register_word("myword", make_primitive("myword", prim_myword, {}, {}));

// 4. Add tests in tests/unit/test_primitives.cpp
TEST(PrimitivesTest, MyWord) { ... }
```

### Adding a New Source File

```cpp
// 1. Create header: include/etil/core/myfile.hpp
#pragma once
#include "etil/core/word_impl.hpp"
namespace etil::core { ... }

// 2. Create implementation: src/core/myfile.cpp
#include "etil/core/myfile.hpp"
namespace etil::core { ... }

// 3. Add to src/CMakeLists.txt ETIL_CORE_SOURCES
// 4. Add test to tests/unit/test_myfile.cpp and tests/CMakeLists.txt
```

### "Super Push" — Update Docs, Commit, and Push

When the user says **"super push"**, perform these steps in order:

1. **Update `CLAUDE.md`** — Reflect any new/changed files, primitives, test counts, phase status, or other project state changes from the current session.
2. **Update `README.md`** — Sync user-facing documentation (feature list, usage examples, build instructions) with the current project state.
3. **Run `scripts/super-push.sh`** — The script handles: build all → test all → detect code vs docs-only → version bump (CMakeLists.txt) → commit → tag → push. Draft the commit message and **show it to the user for approval** before running.

**Deployment is handled by CI on ap1000.** The `post-receive` hook triggers a full CI pipeline (build all → test all → deploy) when pushed to master. Do NOT run `deploy.sh` from WSL — let CI handle it.

### "Docker Janitor" — Clean Up Docker Images

When the user says **"docker janitor"**, clean up old Docker images on both local and ap1000. Keep only the **3 most recent tagged versions** plus `latest`.

#### 1. Audit both systems

```bash
# Local
docker images etil-mcp --format "table {{.Repository}}\t{{.Tag}}\t{{.ID}}\t{{.Size}}\t{{.CreatedAt}}"
docker ps -a --format "table {{.Names}}\t{{.Status}}\t{{.Image}}"

# ap1000
ssh -i /home/mdeazley/workspace/evolutionary-til/.ssh/claudetil.ed25519 \
  claudetil@ap1000.alphapulsar.com \
  'docker images --format "table {{.Repository}}\t{{.Tag}}\t{{.ID}}\t{{.Size}}" && echo "" && docker ps -a --format "table {{.Names}}\t{{.Status}}\t{{.Image}}" && echo "" && docker system df'
```

#### 2. Local cleanup

```bash
# Remove ghosted (exited) containers that aren't needed
docker rm <container_name> ...

# Remove old etil-mcp version tags (keep latest 3 + :latest)
for tag in <old_tags>; do
  docker rmi "etil-mcp:$tag"
done

# Remove stale test images (etil-mcp-test, etil-mcp-http-test, etil-mcp-fileio-test)
docker rmi etil-mcp-test:latest etil-mcp-http-test:latest etil-mcp-fileio-test:latest 2>/dev/null || true

# Remove etil-dev builder image and ubuntu base (pulled by multi-stage builds)
docker rmi etil-dev:latest ubuntu:24.04 2>/dev/null || true

# Prune all dangling images (intermediate builder layers)
docker image prune -f
```

#### 3. ap1000 cleanup

```bash
ssh -i /home/mdeazley/workspace/evolutionary-til/.ssh/claudetil.ed25519 \
  claudetil@ap1000.alphapulsar.com 'bash -s' <<'EOF'
set -euo pipefail

# Remove old etil-mcp version tags (keep latest 3 + :latest)
for tag in <old_tags>; do
  docker rmi "etil-mcp:$tag" 2>&1 || true
done

# Prune dangling images and ubuntu base
docker image prune -f
docker rmi ubuntu:24.04 2>/dev/null || true

# Show final state
docker images --format "table {{.Repository}}\t{{.Tag}}\t{{.ID}}\t{{.Size}}"
docker system df
EOF
```

#### What to keep
- The 3 most recent `etil-mcp:X.Y.Z` version tags + `etil-mcp:latest`
- The `ghcr.io/github/github-mcp-server` image (actively used by Claude Code)
- Docker volumes (`etil-sessions`, `etil-library`) — never prune these

#### What to remove
- All `etil-mcp:X.Y.Z` tags older than the 3 most recent
- Ghosted containers (status: Exited) — check they aren't needed first
- Stale test images (`etil-mcp-test`, `etil-mcp-http-test`, `etil-mcp-fileio-test`)
- `etil-dev` builder image and `ubuntu:24.04` base image
- All dangling `<none>` images (intermediate builder layers — these are the big space hogs, ~2.2 GB each)

### Writing Tests

```cpp
// tests/unit/test_primitives.cpp
#include "etil/core/primitives.hpp"
#include "etil/core/execution_context.hpp"
#include <gtest/gtest.h>

using namespace etil::core;

TEST(PrimitivesTest, AddIntegers) {
    ExecutionContext ctx(0);
    ctx.data_stack().push(Value(int64_t(3)));
    ctx.data_stack().push(Value(int64_t(4)));
    ASSERT_TRUE(prim_add(&ctx));
    auto result = ctx.data_stack().pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->as_int, 7);
}
```

---

## File Organization

### Header Files (`include/etil/`)

```
core/
  word_impl.hpp           ✅ Word implementation with profiling + MetadataMap member
  execution_context.hpp   ✅ Thread-local execution environment + dictionary pointer + last_created_ + out_/err_ streams + DataFieldRegistry + DataRef helpers + execution limits (tick/budget/deadline/call-depth/cancellation)
  dictionary.hpp          ✅ Thread-safe dictionary (absl::Mutex + flat_hash_map) + forget_word + concept metadata
  primitives.hpp          ✅ 120 primitive word declarations + register_primitives() + make_primitive() factory
  version.hpp.in          ✅ Template for build-time generated version header (SEMVER + timestamp)
  compiled_body.hpp       ✅ Instruction, ByteCode (with registry index/backpointer), execute_compiled declaration
  interpreter.hpp         ✅ Outer interpreter (all language semantics, evaluate_string)
  handler_set.hpp         ✅ HandlerSetBase + InterpretHandlerSet, CompileHandlerSet, ControlFlowHandlerSet
  metadata.hpp            ✅ MetadataFormat, MetadataEntry, MetadataMap
  metadata_json.hpp       ✅ JSON serialization for metadata, WordImpl, WordConcept
  heap_object.hpp         ✅ HeapObject base class + Value helpers (make_heap_value, value_addref, value_release)
  heap_string.hpp         ✅ Immutable HeapString with flexible array member
  heap_array.hpp          ✅ Dynamic HeapArray with bounds-checked access
  heap_byte_array.hpp     ✅ Raw byte buffer HeapByteArray
  heap_map.hpp            ✅ Hash map HeapMap (string keys → Value)
  heap_json.hpp           ✅ JSON container HeapJson (wraps nlohmann::json)
  heap_observable.hpp     ✅ HeapObservable class (linked-list pipeline nodes with per-node state, temporal operator kinds), pop_observable helper
  json_primitives.hpp     ✅ Registration declaration for 12 JSON primitives
  heap_primitives.hpp     ✅ Registration declarations for string/array/byte/map primitives
  value_stack.hpp         ✅ Vector-backed stack for single-threaded execution contexts

lvfs/
  lvfs.hpp                ✅ Lvfs class + LvfsEntry struct + register_lvfs_primitives() declaration

mcp/
  json_rpc.hpp            ✅ JSON-RPC 2.0 types, parsing, response/error builders
  http_transport.hpp      ✅ HTTP transport: HttpTransportConfig (with JWT auth + UserStore/AuditLog pointers), HttpTransport
  mcp_server.hpp          ✅ McpServer: protocol handler, tool/resource dispatch, owns Interpreter + AuthConfig + JwtAuth + UserStore + AuditLog
  session.hpp             ✅ Session: interpreter + stacks + LVFS + HTTP client state + JWT auth identity (user_id, role)
  auth_config.hpp         ✅ AuthConfig: role/permission/user mappings + OAuthProviderConfig, from_directory() + from_file() loaders (gated by ETIL_JWT_ENABLED)
  jwt_auth.hpp            ✅ JwtAuth: mint and validate RS256 JWTs (gated by ETIL_JWT_ENABLED)
  oauth_provider.hpp      ✅ OAuthProvider ABC + DeviceCodeResponse/PollResult/ProviderUserInfo data structs (gated by ETIL_JWT_ENABLED)
  oauth_github.hpp        ✅ GitHubProvider: GitHub device flow (gated by ETIL_JWT_ENABLED)
  oauth_google.hpp        ✅ GoogleProvider: Google device flow (gated by ETIL_JWT_ENABLED)

db/
  mongo_client.hpp        ✅ MongoQueryOptions, MongoClient (connect, CRUD with BSON views + options, count_documents, ensure_unique_index, ensure_ttl_index), MongoConnectionsConfig, MongoClientState (gated by ETIL_MONGODB_ENABLED)
  mongo_primitives.hpp    ✅ register_mongo_primitives() declaration (gated by ETIL_MONGODB_ENABLED)

aaa/
  user_store.hpp          ✅ UserRecord, UserStore (find_by_email, create, record_login, get_role), on_login_success() (gated by ETIL_MONGODB_ENABLED)
  audit_log.hpp           ✅ AuditLog (log_permission_denied, log_session_create, log_session_destroy, log_login, log_user_created) (gated by ETIL_MONGODB_ENABLED)

net/
  http_client_config.hpp  ✅ HttpClientConfig (server-wide, from env vars) + HttpClientState (per-session budgets)
  url_validation.hpp      ✅ ParsedUrl, parse_url, SSRF blocklist, domain allowlist, validate_url
  http_primitives.hpp     ✅ register_http_primitives() declaration
```

### Source Files (`src/core/`)

```
word_impl.cpp             ✅ WordImpl implementation
execution_context.cpp     ✅ ExecutionContext implementation
dictionary.cpp            ✅ Dictionary implementation + forget_word + concept metadata
primitives.cpp            ✅ All 113 primitives + make_primitive() factory function — map primitives in separate file
compiled_body.cpp         ✅ Inner interpreter (execute_compiled) + PushString opcode + PushDataPtr lazy registration + SetDoes registry transfer
interpreter.cpp           ✅ Outer interpreter (tokenizing, compilation, dispatch, s" parsing)
interpret_handlers.cpp    ✅ InterpretHandlerSet (3 handlers: words, .", s" — 9 handlers replaced by self-hosted builtins.til)
compile_handlers.cpp      ✅ CompileHandlerSet (5 handlers: ;, does>, .", s", recurse)
control_flow_handlers.cpp ✅ ControlFlowHandlerSet (18 handlers: if/else/then, do/loop/+loop/i/j, begin/until/while/repeat/again, >r/r>/r@, leave, exit)
metadata.cpp              ✅ MetadataMap implementation
metadata_json.cpp         ✅ JSON serialization (nlohmann/json)
string_primitives.cpp     ✅ 17 string primitives (type, s+, s=, slength, substr, strim, sfind, sreplace, ssplit, sjoin, sregex-find, sregex-replace, sregex-search, sregex-match, s., s<>, staint)
array_primitives.cpp      ✅ 10 array primitives + ssplit/sjoin implementations
byte_primitives.cpp       ✅ 7 byte array primitives (bytes-new, bytes-get, bytes-set, bytes-length, bytes-resize, bytes->string, string->bytes)
map_primitives.cpp        ✅ 8 map primitives (map-new, map-set, map-get, map-remove, map-length, map-keys, map-values, map-has?)
json_primitives.cpp       ✅ 14 JSON primitives (json-parse, json-dump, json-pretty, json-get, json-length, json-type, json-keys, json->map, json->array, map->json, array->json, json->value, mat->json, json->mat)
observable_primitives.cpp ✅ HeapObservable implementation + push-based execution engine + 34 TIL primitives (21 core + 13 temporal) + register_observable_primitives()
```

### Source Files (`src/db/`)

```
mongo_client.cpp          ✅ MongoClient: connection pooling, BSON-based CRUD (find/count/insert/update/remove with MongoQueryOptions), ensure_unique_index, ensure_ttl_index, MongoConnectionsConfig (gated by ETIL_BUILD_MONGODB)
mongo_primitives.cpp      ✅ 5 TIL primitives (mongo-find/count/insert/update/delete, accept String/Json/Map) + heap_map_to_bson() + options_from_json/map() (gated by ETIL_BUILD_MONGODB)
```

### Source Files (`src/aaa/`)

```
user_store.cpp            ✅ UserStore: find_by_email, create, record_login, get_role, ensure_indexes, on_login_success (gated by ETIL_BUILD_MONGODB)
audit_log.cpp             ✅ AuditLog: typed audit event logging, 30-day TTL index, fire-and-forget (gated by ETIL_BUILD_MONGODB)
```

### Source Files (`src/net/`)

```
url_validation.cpp        ✅ URL parsing, SSRF blocklist (IPv4/IPv6), domain allowlist, DNS resolution check
http_primitives.cpp       ✅ http-get, http-post primitives + register_http_primitives() (gated by ETIL_HTTP_CLIENT_ENABLED)
```

### Source Files (`src/lvfs/`)

```
lvfs.cpp                  ✅ Lvfs class: path normalization, CWD navigation, directory listing, file reading, file_clock→system_clock epoch conversion
lvfs_primitives.cpp       ✅ 6 LVFS primitives (cwd, cd, ls, ll, lr, cat) + register_lvfs_primitives()
```

### Source Files (`src/mcp/`)

```
json_rpc.cpp              ✅ JSON-RPC parsing/validation/formatting
http_transport.cpp        ✅ HTTP transport: routes, session mgmt, dual-mode auth (JWT+API key), mutex
mcp_server.cpp            ✅ Protocol handler, method routing, initialize handshake, AuthConfig/JwtAuth lifecycle
session.cpp               ✅ Session construction, role permission application
tool_handlers.cpp         ✅ 7 tool implementations (interpret, list_words, get_word_info, get_stack, set_weight, reset, get_session_stats) + SessionStats implementation + per-role permission enforcement + AuditLog integration
resource_handlers.cpp     ✅ 4 resource implementations (dictionary, word, stack, session/stats)
auth_config.cpp           ✅ AuthConfig JSON parser: from_directory() (3-file split) + from_file() (legacy), PEM key loading, role/permission resolution
jwt_auth.cpp              ✅ JWT minting (RS256) and validation using jwt-cpp
oauth_github.cpp          ✅ GitHub device flow provider (gated by ETIL_JWT_ENABLED)
oauth_google.cpp          ✅ Google device flow provider (gated by ETIL_JWT_ENABLED)
```

### Test Files (`tests/unit/`)

```
test_word_impl.cpp        ✅ WordImpl tests
test_dictionary.cpp       ✅ Dictionary tests
test_primitives.cpp       ✅ 100+ primitive tests (arithmetic, stack, comparison, logic, I/O, memory, math, forget, input-reading, dict-ops, metadata, help, dump, see, stream redirection, time)
test_compiled_body.cpp    ✅ 40 compiled word tests (colon defs, control flow, create/does>, loops, DataRef bounds/invalidation, sys-datafields, execution limits, >r/r>/r@, j, leave, exit, begin/again, recurse)
test_interpreter.cpp      ✅ 42 interpreter tests (parsing, definitions, control flow, errors, evaluate)
test_metadata.cpp         ✅ 25 metadata tests (MetadataMap, Dictionary, JSON, interpreter words)
test_heap_objects.cpp     ✅ HeapObject lifecycle, refcounting, Value helpers, HeapString/HeapArray/HeapByteArray
test_string_primitives.cpp ✅ String word tests (s", type, s+, s=, slength, substr, strim, sfind, sreplace, ssplit, sjoin, regex)
test_array_primitives.cpp ✅ Array word tests (new, push/pop, get/set, shift/unshift, nested, mixed types)
test_byte_primitives.cpp  ✅ Byte word tests (new, get/set, length, resize, round-trip conversion)
test_map_primitives.cpp   ✅ Map word tests (new, set/get, overwrite, missing key, remove, length, keys, values, has?, underflow)
test_json_primitives.cpp  ✅ JSON word tests (HeapJson, j| interpret/compile, json-parse, json-dump, json-pretty, json-get, json-length, json-type, json-keys, pack/unpack, round-trips, type switch coverage, mat->json/json->mat serialization)
test_observable_primitives.cpp ✅ 32 observable tests (creation, transform, accumulate, limiting, combination, terminal, introspection, pipeline composition)
test_handler_sets.cpp     ✅ Handler set tests (InterpretHandlerSet, CompileHandlerSet, ControlFlowHandlerSet)
test_url_validation.cpp   ✅ 34 URL validation tests (parsing, SSRF blocklist IPv4/IPv6, domain allowlist, DNS resolution)
test_http_get.cpp         ✅ 13 HTTP primitive tests (7 http-get + 6 http-post: underflow, wrong types, no http state, body handling)
test_lvfs.cpp             ✅ 38 LVFS tests (CWD, cd, ls, list recursive, read file, is_read_only, resolve, traversal rejection, primitives)
test_json_rpc.cpp         ✅ 20 JSON-RPC parsing/formatting tests
test_mcp_server.cpp       ✅ 25 MCP protocol-level tests (handle_message direct calls)
test_mcp_tools.cpp        ✅ 49 MCP tool/resource handler tests (including 7 session stats tests, 3 DoS mitigation tests, 2 evaluate tests)
test_http_transport.cpp   ✅ 19 HTTP transport tests (origin, API key, session ID, SSE notification buffering/formatting)
test_jwt_auth.cpp         ✅ 23 JWT auth tests (AuthConfig loading, role resolution, JWT round-trip, expiry, signatures) — gated by ETIL_BUILD_JWT
test_oauth_provider.cpp   ✅ 21 OAuth provider tests (config parsing, mock provider, poll states, user ID formatting, JWT minting) — gated by ETIL_BUILD_JWT
test_taint.cpp            ✅ 21 taint tests (HeapObject field, create_tainted, sizeof, staint flags, s+ propagation, substr/strim/sreplace, sregex-replace untaint, bytes<->string, ssplit/sjoin, interpreter)
test_mongo_client.cpp     ✅ MongoClient CRUD tests, config tests (resolve, from_file), primitive permission guards, live Atlas tests (gated by ETIL_BUILD_MONGODB + ETIL_MONGODB_TEST_URI)
test_aaa.cpp              ✅ UserStore disconnected guards (6), AuditLog disconnected guards (7), on_login_success null safety (2), live Atlas lifecycle (2) (gated by ETIL_BUILD_MONGODB + ETIL_MONGODB_TEST_URI)
test_file_io_primitives.cpp ✅ File I/O primitive tests
test_async_file_io.cpp    ✅ Async file I/O tests
test_permissions.cpp      ✅ Permission enforcement tests
```

### TIL Integration Tests (`tests/til/`)

```
harness.til               ✅ Shared test harness (expect-eq, pass, fail)
test_create.til           ✅ CREATE/DOES> tests (my-constant, plus-one-constant, testall)
test_create.sh            ✅ Bash launcher for CREATE tests
test_variable_constant.til ✅ Variable/constant defining word tests (scalar + heap objects, 15 tests)
test_variable_constant.sh ✅ Bash launcher for variable/constant tests
test_metadata.til         ✅ Metadata word tests — stack-based returns (27 assertions)
test_metadata.sh          ✅ Bash launcher for metadata tests
test_builtins.til         ✅ Self-hosted builtins integration tests (forget, metadata words)
test_builtins.sh          ✅ Bash launcher for builtins tests
test_help.til             ✅ Help system tests (primitive, handler word, self-hosted, unknown)
test_help.sh              ✅ Bash launcher for help tests
test_dump_see.til         ✅ Dump/see debug primitive tests (integer, string, array, see primitive/compiled/handler)
test_dump_see.sh          ✅ Bash launcher for dump/see tests
test_pen_bytes.til        ✅ PEN: byte array boundary conditions (22 tests)
test_pen_bytes.sh         ✅ Bash launcher for PEN byte tests
test_pen_arrays.til       ✅ PEN: array boundary conditions (20 tests)
test_pen_arrays.sh        ✅ Bash launcher for PEN array tests
test_pen_strings.til      ✅ PEN: string boundary conditions (31 tests)
test_pen_strings.sh       ✅ Bash launcher for PEN string tests
test_pen_complex.til      ✅ PEN: complex arrangements and stress (32 tests)
test_pen_complex.sh       ✅ Bash launcher for PEN complex tests
test_pen_evaluate.til     ✅ PEN: evaluate word security tests (14 tests)
test_pen_evaluate.sh      ✅ Bash launcher for PEN evaluate tests
test_taint.til            ✅ Taint bit integration tests (9 tests)
test_json.til             ✅ JSON type integration tests (22 tests)
test_observable.til       ✅ Observable integration tests (123 tests, core + temporal)
test_observable.sh        ✅ Bash launcher for observable tests
test_json.sh              ✅ Bash launcher for JSON tests
test_taint.sh             ✅ Bash launcher for taint tests

string/
  test_string_basic.til       ✅ s", slength, s+, s=, s<> (17 tests)
  test_string_basic.sh        ✅ Bash launcher
  test_string_substr_trim.til ✅ substr, strim edge cases (16 tests)
  test_string_substr_trim.sh  ✅ Bash launcher
  test_string_search.til      ✅ sfind, sreplace (15 tests)
  test_string_search.sh       ✅ Bash launcher
  test_string_split_join.til  ✅ ssplit, sjoin, round-trips (11 tests)
  test_string_split_join.sh   ✅ Bash launcher
  test_string_regex.til       ✅ sregex-find, sregex-replace (12 tests)
  test_string_regex.sh        ✅ Bash launcher
```

### Docker Integration Tests (`tests/docker/`)

```
test_mcp_http.sh              ✅ E2E: builds Docker image, starts HTTP container, curl-based tests (18 tests)
test_mcp_file_io.sh           ✅ E2E: file I/O with Docker volumes — write_file, list_files, include roundtrip, nested includes, subdirs, path traversal, session isolation (23 tests)
test_file_io_stress.sh        ✅ E2E: file I/O stress tests — power-of-2 sync/async round-trips, interleaved multi-file, byte array boundaries, append/truncate, cleanup (56 tests, bash)
test_file_io_stress.py        ✅ Python translation of file I/O stress tests — identical 56 tests, stdlib only (urllib.request, json, re), McpClient class
test_file_io_stress_py.sh     ✅ Bash wrapper: validates env vars, execs Python script
```

### Examples (`examples/`)

```
simple_repl.cpp           ✅ REPL with replxx line editing, history, tab-completion; delegates to Interpreter
benchmark.cpp             ✅ Performance benchmarks
mcp_server.cpp            ✅ MCP server executable: --host, --port, ETIL_MCP_API_KEY env var
evolve.til                        ✅ (1+1) Evolutionary Strategy prototype — evolves RPN expressions toward a target number using evaluate as genotype-to-phenotype mapping
matrix-mongodb-loop-test.til      ✅ Manual E2E test: matrix→JSON→MongoDB round-trip (requires MongoDB)
```

### Data Files (`data/`)

```
builtins.til              ✅ Self-hosted words: forget, forget-all (parsing), meta!/meta@/meta-del/meta-keys/impl-meta!/impl-meta@ (stack-based aliases)
help.til                  ✅ Built-in help metadata for all ~109 words via stack-based meta! (description, stack-effect, category, examples)
auth-config/
  roles.json.example      ✅ Example roles + default_role config
  keys.json.example       ✅ Example JWT keys, TTL, and OAuth provider config
  users.json.example      ✅ Example user→role mappings
```

### Docker Files (project root)

```
Dockerfile                ✅ Runtime-only (ubuntu:24.04), copies pre-built binary from .docker-stage/
.dockerignore             ✅ Excludes build dirs, IDE files, docs
docker-compose.yml        ✅ Security-hardened HTTP service (localhost binding, API key)
```

### MCP Client TUI (separate repository: `etil-tui`)

The TUI client has been split into its own repository at `~/workspace/etil-tui/` (ap1000 bare repo: `/home/claude/git/etil-tui`). It has independent versioning and its own `.deb` packaging script. See the `etil-tui` repo for file organization and development.

### Deployment Files (`deploy/ap1000/`)

```
nginx-ap1000.conf         ✅ nginx config: TLS termination, /mcp → ETIL, /* → wxlog, rate limiting, CORS
docker-compose.prod.yml   ✅ Production docker-compose (restart policy, log rotation)
.env.example              ✅ API key template (copy to .env for first deploy)
```

### CI Dependencies (`ci/deps/`)

```
CMakeLists.txt            ✅ ExternalProject superbuild for all 12 FetchContent dependencies
build-deps.sh             ✅ Build wrapper: builds debug + release into $PREFIX/{debug,release}
manifest.json             ✅ Version tracking: records all dep git tags for change detection
```

### Scripts (`scripts/`)

```
env.sh                    ✅ Common environment (sourced, not executed): auto-detected paths, version parsing, SSH/Docker constants, CMake flags, helpers
build.sh                  ✅ Build debug/release/all with auto-configure [--configure] [--clean]
test.sh                   ✅ Run tests debug/release/all [--filter PATTERN] [--parallel N]
clean.sh                  ✅ Remove build artifacts debug/release/all with space reporting
super-push.sh             ✅ Orchestrator: build → test → bump → commit → tag → push [--message] [--dry-run]
deploy-ap1000.sh          ✅ Automated deploy to ap1000: build, save, transfer, load, restart, smoke test (--skip-build, --dry-run)
backup-workspace.sh       ✅ Timestamped .tar.gz backup of source files (excludes build artifacts, deps, secrets)
```

---

## Testing Strategy

### Unit Tests
- Test each class in isolation
- Mock dependencies where needed
- Focus on correctness, edge cases
- Example: `test_dictionary.cpp`

### Integration Tests (TIL Tests)
- End-to-end tests that load `.til` files through the REPL
- Each test is a `.sh` + `.til` pair in `tests/til/`
- Shared harness (`tests/til/harness.til`) provides `expect-eq`, `pass`, `fail`
- Auto-discovered by CTest via `file(GLOB_RECURSE TIL_TEST_SCRIPTS ... test_*.sh)`
- To add a new test: create `test_<topic>.til` and `test_<topic>.sh` — CTest picks it up automatically
- **Pattern**: Parsing words (`create`, etc.) consume the next input token at runtime, so instances must be created at the top level, not inside colon definitions. Test words should only *use* the created words.
- **Bash launcher checks**: grep for FAIL, grep for Error, require at least one PASS

### Performance Tests
- Benchmark critical paths
- Lock-free operations
- Selection algorithms
- JIT compilation overhead
- Example: `benchmark.cpp`

### Stress Tests
- Concurrent access to dictionary
- Large number of implementations
- Deep call stacks
- Long-running evolution

---

## Build and Test Workflow

### Using Scripts (preferred)

```bash
# From any directory:
scripts/build.sh all              # Build debug + release
scripts/test.sh all               # Test debug + release (958/958)
scripts/clean.sh all              # Remove all build artifacts
scripts/build.sh debug --clean    # Clean rebuild debug
scripts/test.sh debug --filter Prim  # Run only matching tests

# Super push (build → test → bump → commit → tag → push → deploy)
scripts/super-push.sh --message "Add feature X" --no-deploy
```

### Manual Commands

```bash
# Configure (first time)
mkdir build && cd build
cmake -GNinja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..

# Build
ninja

# Run all tests
ctest --output-on-failure

# Run specific test
./etil_tests --gtest_filter=DictionaryTest.*

# Run with sanitizers (already enabled in Debug)
./etil_tests

# Run benchmarks
./bin/etil_benchmark

# Format code
cd ..
find src include -name "*.cpp" -o -name "*.hpp" | xargs clang-format-18 -i

# Lint code
cd build
find ../src ../include -name "*.cpp" | xargs clang-tidy-18 -p .
```

---

## Key Concepts to Remember

### 1. Word vs Implementation

- **Word Concept**: Abstract idea (e.g., "SORT")
- **Word Implementation**: Concrete code (e.g., "quicksort_avx2")
- One concept → Many implementations

### 2. DAG Not List

Traditional FORTH:
```
LATEST → DUP → SWAP → DROP → ...
         (single linked list)
```

ETIL:
```
Concept: DUP
├─ dup_v1 (gen 0)
├─ dup_v2 (gen 1, mutated from v1)
└─ dup_v3 (gen 1, crossover of v1 + swap_v2)

Concept: SWAP
├─ swap_v1 (gen 0)
└─ swap_v2 (gen 1)

Dependencies: DUP depends on nothing
              SWAP depends on nothing
              PROCESS depends on DUP, VALIDATE
```

### 3. Lock-Free Where Possible

- Execution contexts: Thread-local (no locks needed)
- Stacks: Vector-backed (single-threaded per ExecutionContext)
- Counters: Atomic with relaxed ordering
- Dictionary: Concurrent hashmap (TBB/Abseil)
- Only lock when absolutely necessary

### 4. File Extension Convention

ETIL source files use the `.til` extension (e.g., `include mylib.til`). The `include` word loads and interprets a file at the given path. The interpreter does not enforce a specific extension, but `.til` is the project standard for ETIL source files.

### 5. Conditional Return Convention (Stack-Based)

Words that can succeed or fail use a **FORTH-flag-at-TOS** convention:

- **Success**: TOS = `true` (Boolean), result values below at TOS-1, TOS-2, ...
- **Failure**: TOS = `false` (Boolean), no additional result values on stack

This allows callers to test the outcome with `if`/`then` without additional logic.

**Metadata word stack effects:**

| Word | Success | Failure | Notes |
|------|---------|---------|-------|
| `meta!` | `( word-str key-str fmt-str content-str -- true )` | `( word-str key-str fmt-str content-str -- false )` | Fails on unknown word or bad format |
| `meta@` | `( word-str key-str -- content-str true )` | `( word-str key-str -- false )` | Content as HeapString |
| `meta-del` | `( word-str key-str -- true )` | `( word-str key-str -- false )` | Fails if key/word not found |
| `meta-keys` | `( word-str -- array-of-strings true )` | `( word-str -- false )` | HeapArray of HeapStrings |
| `impl-meta!` | `( word-str key-str fmt-str content-str -- true )` | `( word-str key-str fmt-str content-str -- false )` | Fails on unknown word or bad format |
| `impl-meta@` | `( word-str key-str -- content-str true )` | `( word-str key-str -- false )` | Content as HeapString |

All six are **stack-based words** — simple aliases for the C++ primitives (`dict-meta-set`, `dict-meta-get`, etc.). They consume all arguments from the data stack.

### 6. Measure Everything

Every WordImpl tracks:
- Call count
- Total duration
- Memory usage
- Success/failure rate

This data drives:
- Selection (which implementation to use)
- Evolution (which implementations to keep)
- Optimization (where to focus JIT efforts)

---

## Common Pitfalls to Avoid

### Don't
```cpp
// Global mutable state without synchronization
static std::vector<WordImpl*> all_impls;  // Race condition!

// Exceptions for control flow
throw std::runtime_error("Stack underflow");  // Return false instead

// Use "concept" as an identifier (C++20 keyword)
void register(const std::string& concept);  // Won't compile!

// Use std::expected (C++23 only, project is C++20)
std::expected<Value, Error> pop_stack();  // Won't compile!

// Blocking locks in hot paths
std::mutex mutex;
std::lock_guard lock(mutex);  // Kills performance
```

### Do
```cpp
// Thread-local or concurrent data structures
thread_local ExecutionContext ctx(...);

// std::optional for fallible lookups, bool for primitive success/failure
std::optional<WordImplPtr> lookup(const std::string& word) const;
bool prim_add(ExecutionContext& ctx);  // false = underflow/error

// Intrusive reference counting via WordImplPtr
WordImplPtr impl = make_intrusive<WordImpl>("name", id);

// Vector-backed stacks (single-threaded per ExecutionContext)
ValueStack stack;

// absl::Mutex with ABSL_GUARDED_BY annotations
mutable absl::Mutex mutex_;
absl::flat_hash_map<std::string, WordConcept> concepts_ ABSL_GUARDED_BY(mutex_);
```

---

## Reference Documentation

- **ARCHITECTURE.md** - Detailed architecture and design
- **BUILD_INSTRUCTIONS.md** - Complete build guide
- **README.md** - Project overview and quick start
- **Forth 2012 Standard** - See `forth-2012.pdf` (if included)

---

## Questions to Ask During Development

### Before Implementing

1. **Thread Safety**: Will this be accessed from multiple threads?
   - If yes: Use atomic, lock-free, or concurrent data structure
   - If no: Document that it's single-threaded

2. **Performance Critical**: Is this a hot path?
   - If yes: Avoid locks, allocations, indirection
   - If no: Prioritize clarity over micro-optimization

3. **Error Handling**: What can go wrong?
   - Expected error: Return `bool` (false) or `std::optional` (nullopt)
   - Programmer error: Use `assert`
   - Unexpected error: Log and propagate

4. **Ownership**: Who owns this data?
   - Clear owner: `unique_ptr`
   - Shared: `shared_ptr`
   - Non-owning reference: Raw pointer or reference

5. **Testing**: How will I test this?
   - Write test cases before implementing
   - Consider edge cases
   - Plan for concurrent testing if needed

### During Code Review

1. Is the code thread-safe where it needs to be?
2. Are memory orders appropriate for atomics?
3. Is error handling comprehensive?
4. Are there tests for the new code?
5. Does it follow the project style?
6. Is performance acceptable? (Benchmark if unsure)

---

## Next Steps

### Short Term

1. **Recursion** — `recurse` word for self-referencing definitions
2. **File I/O** — `open-file`, `read-file`, `write-file`, `close-file` using HeapByteArray

### Medium Term

3. **Execution engine** — Formal engine with metrics, multiple implementation selection
4. **Selection engine** — Decision tree + multi-armed bandit
5. **Evolution engine** — Mutation, crossover, fitness

### Long Term

6. **JIT compiler** — LLVM integration
7. **GPU offload** — CUDA/OpenCL for data-parallel operations
8. **Distributed execution** — Multi-machine word execution

---

## Contact & Collaboration

This is a research project exploring the intersection of classic stack-based languages, modern C++, machine learning, and compiler technology.

Key areas for exploration:
- Lock-free data structures for dictionary access
- ML-based implementation selection strategies
- Genetic programming for code evolution
- JIT compilation optimizations
- Parallel execution models

Enjoy building the future of evolutionary programming! 🚀

---

## Appendix: ap1000 Docker Deployment

ap1000 has only 961MB RAM — Docker images must be built locally and transferred.

### Deploy script (from workspace/)

```bash
# Full build + deploy
evolutionary-til/scripts/deploy-ap1000.sh

# Reuse existing local image (skip Docker build)
evolutionary-til/scripts/deploy-ap1000.sh --skip-build

# Preview what would be done
evolutionary-til/scripts/deploy-ap1000.sh --dry-run
```

The script automates: version detection from CMakeLists.txt, Docker build (both version + latest tags), save/compress/transfer, env var extraction from running container (falls back to `deploy/ap1000/.env` for first deploy), image load + tag + container restart with full security/resource options, server readiness polling, and MCP smoke test (initialize + interpret `42 . cr`). EXIT trap cleans up local and remote tarballs.

### Container Security Monitoring (Falco)

Falco 0.43.0 runs as a privileged Docker container with eBPF-based syscall monitoring. Falcosidekick forwards WARNING+ alerts via Gmail SMTP.

**Containers on ap1000:**

| Container | Image | RAM | Purpose |
|-----------|-------|-----|---------|
| `falco` | `falcosecurity/falco:latest` | ~33 MB | eBPF runtime security monitoring |
| `falcosidekick` | `falcosecurity/falcosidekick:latest` | ~13 MB | Email alert forwarding (WARNING+) |

**Files on ap1000:**

| Path | Purpose |
|------|---------|
| `/etc/falco/rules.d/etil.yaml` | Custom ETIL rules + host noise suppression |
| `/var/log/falco/events.json` | Alert log (JSON), rotated daily, 14-day retention |
| `/etc/logrotate.d/falco` | Log rotation config |

**Custom rules** (`/etc/falco/rules.d/etil.yaml`):
- **Shell spawned in ETIL container** (WARNING) — any shell exec in `etil-mcp-http`
- **Unexpected network in ETIL container** (NOTICE) — outbound connections
- **File write in ETIL read-only filesystem** (WARNING) — writes outside `/tmp` and `/data`
- **"Read sensitive file untrusted" disabled** — host systemd PAM reads were flooding alerts (~50 emails/2min)

**Alert flow:**
```
syscalls → Falco (eBPF) → JSON log + HTTP → Falcosidekick → SMTP → mdeazley@gmail.com
```

**Key notes:**
- Falco 0.43.0 uses `modern_ebpf` driver by default — no `--modern-bpf` CLI flag (removed in 0.36+)
- Rule overrides use `override:` directive (Falco 0.43+), not full rule redefinition
- `docker logs falco` can have timing delays — check `/var/log/falco/events.json` for reliable event data
- Falcosidekick Gmail App Password stored as container env var
- Falco requires `--privileged --security-opt apparmor:unconfined --pid=host` for eBPF

**Restart Falco** (after rule changes):
```bash
ssh ap1000 'docker restart falco'
```

**Check alerts:**
```bash
ssh ap1000 'tail -5 /var/log/falco/events.json'
```

### Smoke test (single SSH command)

The HTTP transport requires `Mcp-Session-Id` on all requests after `initialize`. This one-liner does the full handshake in a single remote invocation:

```bash
ssh -i evolutionary-til/.ssh/claudetil.ed25519 claudetil@ap1000.alphapulsar.com 'bash -s' <<'SMOKE'
set -euo pipefail
API_KEY=$(docker inspect etil-mcp-http --format "{{range .Config.Env}}{{println .}}{{end}}" | grep ETIL_MCP_API_KEY | cut -d= -f2)
URL=http://127.0.0.1:8080/mcp
AUTH="Authorization: Bearer $API_KEY"
CT="Content-Type: application/json"

# Initialize: save headers to temp file, capture session ID
# Note: grep must use ^Mcp-Session-Id to avoid matching Access-Control-Expose-Headers line
curl -s -D /tmp/mcp_h.txt -o /dev/null -X POST "$URL" -H "$CT" -H "$AUTH" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"smoke","version":"1.0"}}}'
SID=$(grep '^Mcp-Session-Id:' /tmp/mcp_h.txt | tr -d '\r' | awk '{print $2}')
rm -f /tmp/mcp_h.txt
echo "Session: $SID"

HDR="Mcp-Session-Id: $SID"

echo "--- interpret ---"
curl -s -X POST "$URL" -H "$CT" -H "$AUTH" -H "$HDR" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"interpret","arguments":{"code":"42 . cr"}}}'
echo ""

echo "--- get_session_stats ---"
curl -s -X POST "$URL" -H "$CT" -H "$AUTH" -H "$HDR" \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"get_session_stats","arguments":{}}}'
echo ""
SMOKE
```

---

## TUI Screenshot Workflow

The TUI saves SVG screenshots via Ctrl+P → "Save Screenshot to SVG" (or programmatically via `deliver_screenshot()`). Screenshots are saved to `~/workspace/screenshot/`.

### When the user says "snapshot" or "snapshots"

Scan `~/workspace/screenshot/` for new SVG files. Convert all SVGs to PNGs in `/tmp/` before analysis — SVGs from Textual contain CSS and web fonts that cannot be viewed directly.

### SVG to PNG conversion

Use `rsvg-convert` (from `librsvg2-bin` package) — **not** ImageMagick `convert`, which produces terrible results with Textual SVGs.

```bash
# Convert a single SVG to PNG
rsvg-convert -o /tmp/screenshot.png ~/workspace/screenshot/ETIL_MCP_Client_v0_5_15_2026-02-16T12_00_00.svg

# Convert all SVGs in the screenshot directory to PNGs in /tmp/
for svg in ~/workspace/screenshot/*.svg; do
    png="/tmp/$(basename "${svg%.svg}.png")"
    rsvg-convert -o "$png" "$svg"
done
```

### Key rules

- **Always convert locally** before analysis — do not attempt to read SVG files directly
- **Default screenshot directory**: `~/workspace/screenshot/`
- **Conversion output directory**: `/tmp/`
- **Tool**: `rsvg-convert` (NOT ImageMagick)

---

## Appendix: Build and Test Commands

```bash
# From workspace/ directory (one level above evolutionary-til/):

# Debug build
ninja -C build-debug

# Release build
ninja -C build

# Run tests (debug)
ctest --test-dir build-debug --output-on-failure

# Run tests (release)
ctest --test-dir build --output-on-failure

# Run the REPL
./build-debug/bin/etil_repl

# Run benchmarks
./build-debug/bin/etil_benchmark

# Git remote
git -C evolutionary-til remote -v
# ap1000  ssh://ap1000.alphapulsar.com/home/claude/git/evolutionary-til
```

### REPL Usage Example

```
Evolutionary TIL REPL v0.4.0 (built 2026-02-12 16:48:07 UTC)
Type '/help' for commands, '/quit' to exit

> 42 dup +
(1) 84
> /clear
Stack cleared.
> 3 4 + .
7
(0)
> ." Hello, World!" cr
Hello, World!
(0)
> : double dup + ;
> 21 double .
42
(0)
> : absval dup 0< if negate then ;
> -5 absval .
5
(0)
> : sum-to 0 swap 0 do i + loop ;
> 10 sum-to .
45
(0)
> : countdown begin dup while dup . 1 - repeat drop ;
> 5 countdown
5 4 3 2 1
(0)
> create myvar 0 ,
> 42 myvar !
> myvar @ .
42
(0)
> : constant create , does> @ ;
> 99 constant bottles
> bottles .
99
(0)
> pi .
3.14159
(0)
> forget double
> /words
+ - * / mod /mod negate abs dup drop swap over rot ...
> /quit
Goodbye!
```
