# AST-Level Evolution: Design Document

**Date:** 2026-03-19
**Status:** Proposed
**Replaces:** Bytecode-level mutation in `GeneticOps` (v1.3.0)
**Depends On:** Layer 2 (SelectionEngine), Layer 3 (Fitness, EvolutionEngine)

---

## Problem Statement

The current `GeneticOps` implementation (v1.3.0) mutates bytecode directly — swapping instructions, perturbing constants, inserting/deleting instructions. While safe against process crashes (the interpreter returns `false` on all error paths), this approach has a fundamental problem: **almost every mutation produces nonsense code.**

A word like `: layer-fwd X W mat* b mat-add-col mat-relu ;` compiles to a carefully ordered sequence of stack operations. Swapping two instructions almost always breaks the stack discipline. Inserting a random instruction almost always corrupts the data flow. The result: the fitness evaluator spends most of its time scoring mutants that immediately fail on the first test case with stack underflow or type mismatch.

This is like trying to improve a recipe by randomly rearranging the letters in the instructions. You might occasionally produce something edible, but the overwhelming majority of mutations are garbage.

## Proposed Solution

Decompile bytecode into an Abstract Syntax Tree (AST), perform mutations at the structural level, and recompile back to bytecode. This preserves control flow integrity and enables semantically meaningful transformations:

- **Block substitution**: Replace `mat-relu` with `mat-sigmoid` (same stack signature)
- **Block crossover**: Take the forward-pass computation from parent A and the activation from parent B
- **Parameterized mutation**: Change `PushFloat 0.1` to `PushFloat 0.15` (constant perturbation survives into the AST)
- **Structural crossover**: Merge the if/then branch from one implementation with the loop body from another

The key insight: **the AST makes the structure explicit, so mutations can respect it.**

---

## AST Node Types

```cpp
namespace etil::evolution {

enum class ASTNodeKind {
    Literal,              // PushInt, PushFloat, PushBool, PushString, PushJson
    WordCall,             // Call instruction (primitive or compiled)
    Sequence,             // Linear block of nodes
    IfThen,               // BranchIfFalse ... (no else)
    IfThenElse,           // BranchIfFalse ... Branch ...
    DoLoop,               // DoSetup ... DoLoop
    DoPlusLoop,           // DoSetup ... DoPlusLoop
    BeginUntil,           // ... BranchIfFalse (backward)
    BeginWhileRepeat,     // BranchIfFalse (forward) ... Branch (backward)
    BeginAgain,           // ... Branch (backward) — infinite loop
    PrintString,          // ." text"
    PushXt,               // ['] word or ' word
    ToR,                  // >r
    FromR,                // r>
    FetchR,               // r@
    DoI,                  // i (loop index)
    DoJ,                  // j (outer loop index)
    Leave,                // leave (exit DO loop)
    Exit,                 // exit (return from word)
    PushDataPtr,          // CREATE'd word data field
    SetDoes,              // does> body
};

struct ASTNode {
    ASTNodeKind kind;

    // Literal values (for Literal nodes)
    Instruction::Op literal_op;   // PushInt, PushFloat, PushBool, PushString, PushJson
    int64_t int_val = 0;
    double float_val = 0.0;
    std::string string_val;

    // Word name (for WordCall, PushXt, PrintString)
    std::string word_name;

    // Children (for Sequence, control flow nodes)
    std::vector<ASTNode> children;

    // Control flow specific
    // IfThenElse: children[0] = condition body (before the test),
    //             children[1] = then-body, children[2] = else-body
    // DoLoop: children[0] = loop body
    // BeginUntil: children[0] = loop body (including condition)
    // BeginWhileRepeat: children[0] = condition body, children[1] = loop body

    // Stack effect (populated by simulator, not by decompiler)
    struct StackEffect {
        int consumed = -1;    // -1 = unknown
        int produced = -1;
        bool valid = false;
    };
    StackEffect effect;

    // Source tracking (instruction index range in original bytecode)
    size_t source_ip_start = 0;
    size_t source_ip_end = 0;
};

} // namespace etil::evolution
```

---

## Component 0: Structure Marker Opcodes

### The Problem with Inferring Structure

Without markers, the decompiler must reverse-engineer control flow from branch targets:

```
BranchIfFalse @5    ← is this if/then? if/else/then? begin/while?
...
Branch @2           ← is this else? repeat? again?
```

Answering these questions requires analyzing branch direction, scanning for patterns before/after targets, and handling ambiguous cases. It's fragile, complex, and one new control flow construct breaks the heuristics.

### The Solution: Compiler-Emitted Markers

Add no-op marker opcodes that the compiler emits alongside the real instructions. The inner interpreter skips them (zero cost). The decompiler reads them directly — zero guesswork.

### New Opcodes

```cpp
// Add to Instruction::Op enum:
BlockBegin,     // Mark start of a control structure. int_val = BlockKind
BlockEnd,       // Mark end of a control structure. int_val = BlockKind
BlockSeparator, // Mark boundary within a structure (e.g., else). int_val = BlockKind
```

`int_val` carries the block kind, encoded as:

```cpp
enum class BlockKind : int64_t {
    IfThen         = 1,
    IfThenElse     = 2,
    DoLoop         = 3,
    DoPlusLoop     = 4,
    BeginUntil     = 5,
    BeginWhileRepeat = 6,
    BeginAgain     = 7,
};
```

### Compiler Emission Patterns

Today's compiler emits (for `if 1 + else 1 - then`):

```
[0] BranchIfFalse @4
[1] PushInt 1
[2] Call "+"
[3] Branch @6
[4] PushInt 1
[5] Call "-"
```

With markers:

```
[0] BlockBegin(IfThenElse)       ← NEW: no-op marker
[1] BranchIfFalse @6
[2] PushInt 1
[3] Call "+"
[4] BlockSeparator(IfThenElse)   ← NEW: marks the else boundary
[5] Branch @9
[6] PushInt 1
[7] Call "-"
[8] BlockEnd(IfThenElse)         ← NEW: no-op marker
```

All control flow patterns:

| Pattern | Markers Emitted |
|---------|----------------|
| `if ... then` | `BlockBegin(IfThen)` ... `BranchIfFalse` ... `BlockEnd(IfThen)` |
| `if ... else ... then` | `BlockBegin(IfThenElse)` ... `BranchIfFalse` ... `BlockSeparator(IfThenElse)` ... `Branch` ... `BlockEnd(IfThenElse)` |
| `do ... loop` | `BlockBegin(DoLoop)` ... `DoSetup` ... `DoLoop` ... `BlockEnd(DoLoop)` |
| `do ... +loop` | `BlockBegin(DoPlusLoop)` ... `DoSetup` ... `DoPlusLoop` ... `BlockEnd(DoPlusLoop)` |
| `begin ... until` | `BlockBegin(BeginUntil)` ... `BranchIfFalse` ... `BlockEnd(BeginUntil)` |
| `begin ... while ... repeat` | `BlockBegin(BeginWhileRepeat)` ... `BranchIfFalse` ... `BlockSeparator(BeginWhileRepeat)` ... `Branch` ... `BlockEnd(BeginWhileRepeat)` |
| `begin ... again` | `BlockBegin(BeginAgain)` ... `Branch` ... `BlockEnd(BeginAgain)` |

### Inner Interpreter Change

Three lines in `execute_compiled`:

```cpp
case Instruction::Op::BlockBegin:
case Instruction::Op::BlockEnd:
case Instruction::Op::BlockSeparator:
    ++ip;  // no-op: skip marker
    break;
```

### Cost Analysis

- **Runtime cost:** One switch case + increment per marker. Markers are encountered once per control structure entry/exit. For a typical word with 2-3 control structures, this adds 4-9 no-op instructions — negligible vs. the actual computation.
- **Memory cost:** Each `Instruction` is ~80 bytes (Op enum + int64 + double + string + pointer + uint64). Markers use only Op + int_val (~16 bytes effective). For a word with 20 instructions, adding 6 markers increases bytecode by ~30%. This is acceptable — bytecode is not the memory bottleneck.
- **Compilation cost:** The control flow handlers emit 1-3 extra `append()` calls per structure. Trivial.

### No Legacy Path

All ETIL source — `.til` files, REPL input, `evaluate` strings, MCP `interpret` calls — compiles through the same `:` ... `;` pipeline in `control_flow_handlers.cpp`. There is no pre-compiled bytecode format that persists across restarts. Once the marker opcodes are added to the compiler, **all** bytecode will carry markers. There is no need for a legacy fallback decompiler.

The `prim_see` disassembler should hide markers by default (they're noise for human readers) and show them with a verbose flag.

---

## Component 1: Bytecode-to-AST Decompiler

### Input
A `ByteCode` object (vector of `Instruction`s).

### Output
An `ASTNode` of kind `Sequence` representing the entire word body.

### Algorithm (With Markers)

The decompiler is now a simple recursive descent parser over the instruction stream. `BlockBegin` opens a new AST node, instructions accumulate as children, `BlockSeparator` separates branches, and `BlockEnd` closes the node.

```
function decompile(instructions, start, end) -> ASTNode(Sequence):
    nodes = []
    ip = start
    while ip < end:
        instr = instructions[ip]
        switch instr.op:

            case BlockBegin:
                kind = BlockKind(instr.int_val)
                // Find matching BlockEnd
                block_end = find_matching_block_end(instructions, ip, kind)
                // Recursively decompile the block interior
                node = decompile_block(instructions, ip + 1, block_end, kind)
                nodes.append(node)
                ip = block_end + 1   // skip past BlockEnd

            case BlockEnd, BlockSeparator:
                // Should only be encountered by decompile_block
                // If we see them here, the bytecode is malformed
                ip++

            case PushInt, PushFloat, PushBool:
                nodes.append(Literal(instr))
                ip++

            case PushString, PrintString, PushJson:
                nodes.append(corresponding node)
                ip++

            case Call:
                nodes.append(WordCall(instr.word_name))
                ip++

            case PushXt:
                nodes.append(PushXt(instr.word_name))
                ip++

            case DoI, DoJ, ToR, FromR, FetchR, DoExit:
                nodes.append(leaf node of corresponding kind)
                ip++

            // Skip the actual branch/loop instructions (structure is in markers)
            case Branch, BranchIfFalse, DoSetup, DoLoop, DoPlusLoop, DoLeave:
                ip++

    return Sequence(nodes)
```

Block interior parsing:

```
function decompile_block(instructions, start, end, kind) -> ASTNode:
    switch kind:
        case IfThen:
            // Skip BranchIfFalse, decompile body until BlockEnd
            body = decompile(instructions, start + 1, end)  // +1 skips BranchIfFalse
            return IfThen(body)

        case IfThenElse:
            // BranchIfFalse ... then-body ... BlockSeparator ... Branch ... else-body
            sep = find_separator(instructions, start, end, kind)
            then_body = decompile(instructions, start + 1, sep)     // skip BranchIfFalse
            else_body = decompile(instructions, sep + 2, end)       // skip Separator + Branch
            return IfThenElse(then_body, else_body)

        case DoLoop, DoPlusLoop:
            // DoSetup ... body ... DoLoop/DoPlusLoop
            body = decompile(instructions, start + 1, end - 1)      // skip DoSetup and DoLoop
            return DoLoop/DoPlusLoop(body)

        case BeginUntil:
            // body ... BranchIfFalse (backward)
            body = decompile(instructions, start, end - 1)          // skip final BranchIfFalse
            return BeginUntil(body)

        case BeginWhileRepeat:
            // condition ... BranchIfFalse ... BlockSeparator ... body ... Branch (backward)
            sep = find_separator(instructions, start, end, kind)
            condition = decompile(instructions, start, sep - 1)     // before BranchIfFalse
            body = decompile(instructions, sep + 1, end - 1)        // skip Separator, skip Branch
            return BeginWhileRepeat(condition, body)

        case BeginAgain:
            // body ... Branch (backward)
            body = decompile(instructions, start, end - 1)          // skip final Branch
            return BeginAgain(body)
```

### Finding Matching BlockEnd

```
function find_matching_block_end(instructions, begin_ip, kind):
    depth = 1
    ip = begin_ip + 1
    while ip < instructions.size():
        if instructions[ip].op == BlockBegin:
            depth++
        if instructions[ip].op == BlockEnd:
            depth--
            if depth == 0:
                return ip
        ip++
    return instructions.size()  // malformed
```


---

## Component 2: AST-to-Bytecode Compiler

### Input
An `ASTNode` (typically a `Sequence` representing a word body).

### Output
A `ByteCode` object with properly backpatched branch targets.

### Algorithm

The compiler walks the AST recursively, emitting instructions and using a fixup list for forward references:

```
function compile(node, bytecode):
    switch node.kind:
        case Literal:
            emit PushInt/PushFloat/PushBool/PushString/PushJson

        case WordCall:
            emit Call(node.word_name)

        case PrintString:
            emit PrintString(node.string_val)

        case PushXt:
            emit PushXt(node.word_name)

        case DoI, DoJ, ToR, FromR, FetchR, Exit, Leave:
            emit corresponding opcode

        case Sequence:
            for child in node.children:
                compile(child, bytecode)

        case IfThen:
            emit BranchIfFalse(placeholder)
            branch_ip = current_ip - 1
            compile(node.children[0], bytecode)    // then-body
            backpatch(branch_ip, current_ip)

        case IfThenElse:
            emit BranchIfFalse(placeholder)
            if_branch_ip = current_ip - 1
            compile(node.children[0], bytecode)    // then-body
            emit Branch(placeholder)
            else_branch_ip = current_ip - 1
            backpatch(if_branch_ip, current_ip)
            compile(node.children[1], bytecode)    // else-body
            backpatch(else_branch_ip, current_ip)

        case DoLoop:
            emit DoSetup
            loop_start = current_ip
            compile(node.children[0], bytecode)    // loop body
            emit DoLoop(loop_start)
            // backpatch any DoLeave instructions to current_ip

        case BeginUntil:
            loop_start = current_ip
            compile(node.children[0], bytecode)    // body + condition
            emit BranchIfFalse(loop_start)

        case BeginWhileRepeat:
            loop_start = current_ip
            compile(node.children[0], bytecode)    // condition
            emit BranchIfFalse(placeholder)
            while_branch_ip = current_ip - 1
            compile(node.children[1], bytecode)    // body
            emit Branch(loop_start)
            backpatch(while_branch_ip, current_ip)

        case BeginAgain:
            loop_start = current_ip
            compile(node.children[0], bytecode)    // body
            emit Branch(loop_start)
```

### Round-Trip Fidelity

The decompiler and compiler must satisfy:

```
decompile(compile(ast)) ≅ ast              // structure-preserving
execute(compile(decompile(bytecode))) ≡ execute(bytecode)  // semantics-preserving
```

Exact byte-for-byte identity is not required (instruction ordering within a basic block can vary), but execution semantics must be identical.

---

## Component 3: Stack Effect Simulator

### Purpose

Trace the stack effect of each AST node to determine:
1. How many values it consumes from the stack
2. How many values it produces
3. What types those values have (when determinable)

This information enables the genetic operators to identify **compatible substitution points** — places where one block can be replaced with another that has the same stack signature.

### Algorithm

```
function simulate(node, stack_depth_in) -> (consumed, produced, valid):
    switch node.kind:
        case Literal:
            return (0, 1, true)    // pushes one value

        case WordCall:
            sig = lookup_type_signature(node.word_name)
            if sig.valid:
                return (sig.inputs.size(), sig.outputs.size(), true)
            else:
                return (-1, -1, false)  // unknown word

        case Sequence:
            total_consumed = 0
            total_produced = 0
            depth = stack_depth_in
            for child in node.children:
                (c, p, v) = simulate(child, depth)
                if !v: return (-1, -1, false)
                depth = depth - c + p
                if depth < 0: return (-1, -1, false)  // underflow
                total_consumed += max(0, c - available_from_previous)
                total_produced = depth - stack_depth_in
            return (total_consumed, total_produced, true)

        case IfThen:
            // Condition consumes 1 boolean
            // Then-body must have net 0 effect (FORTH convention)
            (c, p, v) = simulate(node.children[0], stack_depth_in)
            return (c + 1, p, v)   // +1 for boolean consumed by BranchIfFalse

        case IfThenElse:
            // Both branches must have same net effect
            (c1, p1, v1) = simulate(node.children[0], stack_depth_in)
            (c2, p2, v2) = simulate(node.children[1], stack_depth_in)
            if v1 && v2 && (p1 - c1) == (p2 - c2):
                return (c1 + 1, p1, true)  // +1 for boolean
            return (-1, -1, false)

        case DoLoop, BeginUntil, BeginWhileRepeat:
            // Loop body effect is complex — conservatively mark unknown
            return (-1, -1, false)
```

### Type Inference at Compile Time

Every primitive has a `TypeSignature` registered via `make_primitive()`. That's ~315 base cases. Any colon definition composed of words with known signatures has a determinable signature — the compiler can infer it at definition time by tracing the stack effect through the instruction sequence.

For example, `: double dup + ;` calls:
- `dup`: `( a -- a a )` — consumes 1, produces 2 (same type)
- `+`: `( n n -- n )` — consumes 2 integers/floats, produces 1

Starting from an empty stack:
```
initial:  []
after dup: need 1 input → infer input [a]. Stack: [a, a]
after +:   consumes 2 → stack: [n]
result:    ( a -- n ) where a must be numeric
```

The inferred signature is stored on the `WordImpl` at definition time. Since colon definitions are built from primitives and other colon definitions, **every word in the dictionary can have a type signature** — the inference is recursive all the way down.

#### Signature Determinability Categories

**Category 1 — Fully determinable.** The input count, output count, and all types are known at compile time. This covers the vast majority of words: all 315 primitives and any colon definition composed entirely of Category 1 words.

```
: double dup + ;           → ( n -- n )         fully known
: layer-fwd mat* mat-relu ; → ( mat mat -- mat ) fully known
```

**Category 2 — Partially determinable.** Some parts of the signature are known, but the output count (or input count) depends on a runtime value. The *fixed* portion of the signature is still useful for type checking and substitution.

| Pattern | What's known | What's data-dependent |
|---------|-------------|----------------------|
| `: countdown 0 do i loop ;` | Input: `( n -- )` (one integer, the limit) | Output: 0 to N integers, where N = limit |
| `sprintf` | Input: format string is first arg | Remaining input count depends on `%` specifiers in format string |
| `: gather 0 do i array-get loop ;` | Input: `( array n -- )` | Output: N values of array element type |

For Category 2, the `TypeSignature` records the fixed prefix of the signature and marks the remainder as variable:

```cpp
struct TypeSignature {
    std::vector<Type> inputs;
    std::vector<Type> outputs;
    bool variable_inputs = false;   // true if input count is data-dependent
    bool variable_outputs = false;  // true if output count is data-dependent
};
```

The genetic operators can safely substitute words that match the fixed prefix. For example, two words both consuming `( array n -- )` with variable outputs are substitution-compatible for the input side, even though their output counts differ.

**Category 3 — Fully opaque.** Words containing `execute` or `evaluate` where the stack effect depends entirely on a runtime-provided execution token or code string.

```
: x execute ;        → consumes 1 xt, otherwise unknown
: run-code evaluate ; → consumes 1 string, otherwise unknown
```

For Category 3, the `TypeSignature` records only the known xt/string input. The genetic operators treat the word as an opaque block — no type-safe substitution, but constant perturbation and structural mutations still apply.

**Category 4 — Constrained opaque.** Words containing `execute` where the surrounding context constrains the xt's required stack effect.

```
: x execute swap execute + ;   → ( ... xt1 xt2 -- ... n )
```

Tracing backward from `+`:
- `+` needs `( n n -- n )` — two numerics on top
- The second `execute` must leave exactly 1 numeric (to feed `+`)
- `swap` requires at least 2 items, so the first `execute` must leave exactly 1 value
- Both xts are therefore constrained to `( -- n )` — produce 1 numeric, consume nothing

The inference engine can derive these **xt constraints** by working backward from known-signature words. This is more useful than Category 3's "fully opaque" classification because the genetic operators can verify that any xt substitution satisfies the constraint. For example, replacing `['] sin` with `['] cos` is valid (both are `( f -- f )`), but `['] dup` is invalid (it's `( a -- a a )`, wrong output count).

For initial implementation, Category 4 can be treated as Category 3 (opaque). Backward constraint inference is an optimization that improves mutation quality but isn't required for correctness — the fitness evaluator will catch constraint violations at runtime.

**The pathological case:** `: x execute evaluate execute ;` — two opaque operations interact through the stack, and `evaluate` can modify the dictionary, redefine words, or alter the execution environment. The stack state after `evaluate` is completely unknowable at compile time (it depends on the *content of a runtime string*). The final `execute` may or may not find its xt at the expected stack position. This is Category 3 — no inference is possible beyond "consumes at least 3 values." Critically, `evaluate` breaks the closed-world assumption that backward constraint inference (Category 4) depends on: backward inference from the final `execute` through `evaluate` is impossible because `evaluate` can push, pop, or rearrange any number of stack items.

**Design rule:** Any definition containing `evaluate` is unconditionally Category 3. Category 4 backward inference only applies when all opaque words are `execute` (where the stack effect is unknown but *fixed* per invocation — it depends on the xt, not on arbitrary runtime code).

Note: `mat-apply`, `array-each/map/filter/reduce`, and `obs-map/filter/scan` are effectively Category 4 — they are primitives with known signatures, and their xt parameter has a documented stack effect contract (e.g., `mat-apply` requires `( float -- float )`). These could be modeled as Category 1 with a constrained-xt annotation.

#### Compile-Time Type Error Detection

The inference engine should detect words where **no valid input signature exists**. Consider:

```
: x 0 swap obs-range obs-concat obs-map obs-subscribe ;
```

Tracing the only input that passes `obs-concat` — `( xt obs n )`:

```
Entry:        ( xt obs n )
0             ( xt obs n 0 )
swap          ( xt obs 0 n )
obs-range     ( xt obs obs_range )       — obs-range( 0 n -- obs )
obs-concat    ( xt obs_combined )        — concat( obs obs -- obs )
obs-map       ← needs ( obs xt ) with xt on TOS
                 stack has ( xt obs_combined ) with obs on TOS
                 TYPE MISMATCH
```

`obs-concat` leaves the observable on TOS. `obs-map` needs the xt on TOS. No input arrangement fixes this — the word is **statically provably invalid**.

For evolution, this is the first line of defense: the type inference engine can reject invalid mutants **at compile time** before they ever reach the fitness evaluator. A mutation that produces this word is immediately discarded with fitness 0.0, saving the cost of creating an `ExecutionContext`, pushing test inputs, executing, and cleaning up. With bytecode-level mutation, this kind of rejection is impossible — the invalidity only manifests at runtime.

The inference engine classifies a word as a type error when the simulated stack reaches a state where the next instruction requires a type at a position that holds an incompatible type, and no input prefix can resolve the conflict (because the conflicting values were both produced internally by the word's own instructions).

#### Stack Shuffling as Type Permutation

Stack manipulation words (`swap`, `rot`, `over`, `pick`, `roll`, `dup`, `drop`, `nip`, `tuck`) are not computational — they're **type permutation operators**. The simulator must apply the same permutation to its type tracking array that the instruction applies to the stack:

| Word | Stack permutation | Type array effect |
|------|------------------|-------------------|
| `swap` | `( a b -- b a )` | Exchange positions 0 and 1 |
| `rot` | `( a b c -- b c a )` | Rotate positions 0, 1, 2 |
| `over` | `( a b -- a b a )` | Duplicate type at position 1, push to 0 |
| `dup` | `( a -- a a )` | Duplicate type at position 0 |
| `drop` | `( a -- )` | Remove type at position 0 |
| `nip` | `( a b -- b )` | Remove type at position 1 |
| `tuck` | `( a b -- b a b )` | Insert copy of position 0 at position 2 |

Consider `: x 0 swap obs-range obs-concat swap obs-map swap obs-subscribe ;` with input `( xt xt obs n )`. The full type stack trace:

```
                        TOS →
Entry:        [ xt  xt  obs  n   ]
0:            [ xt  xt  obs  n  int ]
swap:         [ xt  xt  obs  int  n ]
obs-range:    [ xt  xt  obs  obs ]
obs-concat:   [ xt  xt  obs      ]
swap:         [ xt  obs  xt      ]     ← xt promoted to TOS for obs-map
obs-map:      [ xt  obs          ]
swap:         [ obs  xt          ]     ← xt promoted to TOS for obs-subscribe
obs-subscribe:[ ]
```

Without the two `swap` instructions (as shown in the previous section's type error example), `obs-map` receives an observable where it expects an xt. The `swap` instructions are **type-alignment infrastructure** — they exist solely to reorder the type vector so each consumer finds the right type at TOS.

**Implication for genetic operators:**

1. Stack shuffling words should almost never be deleted — removing one likely breaks type alignment downstream
2. Inserting a `swap` should be considered when the simulator detects a type mismatch at a call site (the needed type exists on the stack but at the wrong position)
3. The AST should distinguish **computational nodes** (Call, Literal) from **structural nodes** (swap, rot, dup, drop) so mutation operators treat them differently
4. A "repair" mutation can scan the type stack at an error point, find the needed type at a deeper position, and insert the minimal shuffling sequence to promote it to TOS

#### Inference at the Boundary

The key insight is that inference propagates from the leaves (primitives) upward. A definition is:

- **Category 1** if all called words are Category 1 and it contains no data-dependent loops
- **Category 2** if it contains a DO/LOOP that pushes to the stack inside the loop body, or calls a Category 2 word, but all other aspects are determinable
- **Category 3** if it contains `execute` or `evaluate`

A DO/LOOP that doesn't push to the data stack (e.g., `: sum 0 swap 0 do + loop ;`) is still Category 1 — the loop body's net stack effect is `( n n -- n )` per iteration, and the loop as a whole has a fixed effect regardless of the iteration count. The compiler can detect this by checking whether the loop body's net effect changes the stack depth.

In practice, **most computational words that evolution targets are Category 1**. Loop-based accumulators like `sum` are Category 1. Neural network words like `forward`, `backward`, `sgd-update` are Category 1. The Category 2 and 3 words tend to be infrastructure (I/O, meta-programming) rather than computation.

#### Implementation

The compiler already has access to the dictionary during colon definition compilation (`compile_token` does `dict_.lookup(token)`). Adding signature inference requires:

1. Maintain a `std::vector<TypeSignature::Type>` as the simulated stack during compilation
2. For each `Call` instruction, look up the callee's signature and apply it to the simulated stack
3. At `;`, record the net effect (initial stack depth vs. final stack depth, with types) as the new word's `TypeSignature`
4. If any callee has an unknown signature, mark the whole word as unknown

This is ~50 lines in `CompileHandlerSet::handle_semicolon()` or a new `infer_signature()` method on `Interpreter`.

#### Full Type Stack Tracking

Beyond just input/output signatures, the simulator can track the full type state at every instruction boundary:

```
stack_state = [Type::Integer, Type::Matrix, Type::Matrix]

After WordCall("mat*"):
    pop Matrix, pop Matrix, push Matrix
    stack_state = [Type::Integer, Type::Matrix]

After WordCall("mat-relu"):
    pop Matrix, push Matrix
    stack_state = [Type::Integer, Type::Matrix]
```

This enables the genetic operators to find words with **type-compatible** signatures at every substitution point, not just at word boundaries. For example, `mat-relu` and `mat-sigmoid` both have effect `(Matrix -- Matrix)`, so they are interchangeable. But `mat-sum` has effect `(Matrix -- Float)`, so substituting it would change the downstream type flow and the genetic operator knows not to do it.

---

## Component 4: Type-Directed Repair

### The Core Insight

Traditional genetic programming operates in two steps: mutate, then test. Most mutations produce type-invalid programs that fail immediately at runtime. The fitness evaluator wastes cycles discovering what the type checker already knows.

Type-directed repair adds a deterministic step between mutation and testing:

```
mutate structurally → type-check → repair mismatches → test only valid candidates
```

The repair step is not random. The type simulator knows exactly what type is needed at the mismatch point, exactly what types are on the stack, and can compute the minimal stack shuffling sequence to fix it:

- Type needed at TOS, found at position 1 → insert `swap`
- Type needed at TOS, found at position 2 → insert `rot`
- Type needed at TOS, found at position N → insert `N roll`
- Type needed at TOS, not on stack at all → mutation is genuinely invalid, discard

### Algorithm

```
function repair(ast, dictionary) -> bool:
    type_stack = []
    for node in ast.children:
        sig = lookup_signature(node)
        if !sig.valid:
            continue  // opaque node, skip

        // Check each input type requirement
        for i in 0..sig.inputs.size()-1:
            needed_type = sig.inputs[sig.inputs.size() - 1 - i]  // TOS first
            if type_stack.size() <= i:
                return false  // underflow — unrepairable

            actual_type = type_stack[type_stack.size() - 1 - i]
            if actual_type != needed_type:
                // Type mismatch at position i. Search deeper for the right type.
                found_pos = find_type_in_stack(type_stack, needed_type, i + 1)
                if found_pos < 0:
                    return false  // needed type not on stack — unrepairable

                // Insert shuffling to bring it to position i
                shuffling = compute_minimal_shuffle(found_pos, i)
                insert_before(ast, node, shuffling)
                apply_shuffle(type_stack, shuffling)

        // Apply the word's effect to the type stack
        for i in 0..sig.inputs.size()-1:
            type_stack.pop()
        for output_type in sig.outputs:
            type_stack.push(output_type)

    return true  // all mismatches repaired
```

### Shuffle Computation

```
function compute_minimal_shuffle(from_pos, to_pos) -> [ASTNode]:
    if from_pos == to_pos:
        return []
    if from_pos == 1 && to_pos == 0:
        return [WordCall("swap")]
    if from_pos == 2 && to_pos == 0:
        return [WordCall("rot")]
    // General case: roll brings position N to TOS
    return [Literal(from_pos), WordCall("roll")]
```

### What This Means for Evolution

Without repair, the mutation validity rate for structural mutations is ~5%. The fitness evaluator spends 95% of its time scoring immediate failures.

With repair, every mutation that has *any possible valid form* is transformed into that form. The mutation validity rate approaches 100% for all mutations where the needed types exist somewhere on the stack. Evolution spends all its cycles evaluating **meaningful semantic alternatives** — different activation functions, different computation orders, different constant values — not stack shuffling errors.

The repair phase is cheap: one linear pass over the AST with O(stack_depth) type lookups per node. For a typical 20-instruction word, this is microseconds. The fitness evaluation it replaces (create ExecutionContext, push inputs, execute, clean up) is milliseconds. The cost ratio is ~1000:1 in favor of repair.

---

## Component 5: AST-Level Genetic Operators

The genetic operators now have two phases: **creative** (structural change) and **repair** (type fixup). The creative phase may break type alignment; the repair phase fixes it deterministically. If repair fails (needed type doesn't exist on the stack), the mutation is discarded.

### 5.1 Block Substitution

Find a `WordCall` node in the AST. Look up all dictionary words with the same stack signature. Replace the call with a randomly selected compatible word. Run repair.

```
function substitute_call(ast, dictionary):
    candidates = find_all_wordcalls(ast)
    if candidates.empty(): return false

    target = random_choice(candidates)
    sig = lookup_signature(target.word_name)
    if !sig.valid: return false

    compatible = dictionary.find_words_with_signature(sig)
    if compatible.size() <= 1: return false  // no alternatives

    replacement = random_choice(compatible, excluding=target.word_name)
    target.word_name = replacement
    return true
```

**Example**: In `: layer X W mat* b mat-add-col mat-relu ;`, the simulator identifies `mat-relu` as `(Matrix -- Matrix)`. Compatible words: `mat-sigmoid`, `mat-tanh`. Substitution replaces `mat-relu` with `mat-sigmoid`.

### 4.2 Constant Perturbation (preserved from v1.3.0)

Find a `Literal` node. Apply Gaussian noise to its value. This operator survives unchanged from the bytecode-level implementation because literals are leaf nodes in the AST.

### 5.3 Block Crossover

Given two parent ASTs, identify structurally compatible subtrees and swap them. Run repair on the result.

```
function block_crossover(parent_a, parent_b):
    # Find all Sequence nodes in both parents
    seqs_a = find_all_sequences(parent_a)
    seqs_b = find_all_sequences(parent_b)

    # Find pairs with compatible stack effects
    for seq_a in seqs_a:
        for seq_b in seqs_b:
            if seq_a.effect.consumed == seq_b.effect.consumed
               && seq_a.effect.produced == seq_b.effect.produced:
                # Compatible! Swap the children
                child = deep_copy(parent_a)
                replace_subtree(child, seq_a, deep_copy(seq_b))
                if repair(child, dictionary):
                    return child

    # No exact match — try substitution with repair
    child = deep_copy(parent_a)
    random_block_swap(child, parent_b)
    if repair(child, dictionary):
        return child
    return null  // unrepairable
```

**Example**: Parent A has `: fwd-a X W mat* b mat-add-col mat-relu ;` and Parent B has `: fwd-b X W mat-hadamard mat-sigmoid ;`. The simulator identifies `[mat*, mat-add-col, mat-relu]` in A as `(-- Matrix)` and `[mat-hadamard, mat-sigmoid]` in B as `(-- Matrix)`. Crossover replaces A's chain with B's. If the types don't perfectly align (e.g., B's chain produces a differently-shaped intermediate), the repair phase inserts any necessary shuffling.

### 5.4 Functional Block Move

Identify a functional block (contiguous sequence with a known stack effect) and move it to a different position in the AST. Run repair to fix any type misalignment caused by the move.

```
function move_block(ast):
    blocks = identify_functional_blocks(ast)
    if blocks.size() < 2: return false

    source = random_choice(blocks)
    target_pos = random_position(ast, excluding=source)
    remove_block(ast, source)
    insert_block(ast, target_pos, source)
    return repair(ast, dictionary)  // repair fixes misalignment from the move
```

Note: without repair, this operator would need to restrict moves to positions with matching stack states (very few candidates). With repair, **any** move is attempted — the repair phase inserts `swap`/`rot`/`roll` as needed, or rejects the move if the types are fundamentally incompatible.

### 5.5 Control Flow Mutation

Wrap a block in a new control structure, or unwrap an existing one.

```
function mutate_control_flow(ast):
    choice = random(0, 3)
    switch choice:
        case 0:  // Wrap a sequence in if/then (conditional execution)
            block = random_choice(find_sequences(ast))
            condition = Literal(PushBool, true)  // always-true initially
            ast.replace(block, IfThen([condition, block]))

        case 1:  // Unwrap an if/then (remove conditional)
            ifthen = random_choice(find_ifthen_nodes(ast))
            ast.replace(ifthen, ifthen.then_body)

        case 2:  // Duplicate a block (redundant but can evolve away)
            block = random_choice(find_sequences(ast))
            if block.effect.consumed == 0:  // safe to repeat if no inputs
                ast.insert_after(block, deep_copy(block))

        case 3:  // Remove a block (if net effect is 0)
            block = random_choice(find_sequences(ast))
            if block.effect.consumed == block.effect.produced:
                ast.remove(block)
```

---

## Component 6: Semantic Annotations on AST Nodes

### Beyond Types: What Does the Word Mean?

Three words with identical signature `( matrix -- matrix )`:
- `mat-relu` — activation function, element-wise, shape-preserving
- `mat-transpose` — structural transform, changes shape
- `mat-sigmoid` — activation function, element-wise, shape-preserving

Type-compatible substitution would allow any of these to replace any other. But `mat-relu` → `mat-sigmoid` is a meaningful evolutionary experiment (comparing activation functions), while `mat-relu` → `mat-transpose` is nonsensical (a transposition in the middle of a forward pass changes the data geometry).

Semantic annotations on AST nodes enable the genetic operators to distinguish meaningful mutations from type-valid noise.

### Annotation Sources

**Category** — already exists in `help.til` metadata for every word. Values: `matrix`, `math`, `string`, `array`, `system`, etc. Accessible via `dict.get_concept_metadata(word, "category")`.

**Semantic tags** — new metadata keys attached via `meta!` in `help.til`. Multiple tags per word, stored as a space-separated string:

```forth
s" mat-relu" s" semantic-tags" s" text" s" activation element-wise shape-preserving" meta! drop
s" mat-sigmoid" s" semantic-tags" s" text" s" activation element-wise shape-preserving" meta! drop
s" mat-transpose" s" semantic-tags" s" text" s" structural shape-changing" meta! drop
s" mat-sum" s" semantic-tags" s" text" s" reducing scalar-output" meta! drop
s" mat-add-col" s" semantic-tags" s" text" s" broadcasting" meta! drop
s" mat*" s" semantic-tags" s" text" s" combining linear-algebra" meta! drop
s" mat-hadamard" s" semantic-tags" s" text" s" combining element-wise" meta! drop
```

### AST Node Extension

```cpp
struct ASTNode {
    // ... existing fields ...

    // Semantic annotation (populated from word metadata during decompilation)
    std::string category;
    std::vector<std::string> semantic_tags;
};
```

The decompiler populates these fields when creating `WordCall` nodes by querying the dictionary's concept metadata. This is a one-time lookup per word (cacheable in the `SignatureIndex`).

### Substitution Preference Levels

The genetic operators use a tiered substitution strategy:

| Level | Match criteria | Quality | Example |
|-------|---------------|---------|---------|
| 1 (best) | Same signature + same semantic tags | High | `mat-relu` → `mat-sigmoid` |
| 2 (good) | Same signature + overlapping tags | Medium | `mat-relu` → `mat-clip` (both element-wise) |
| 3 (valid) | Same signature only | Low | `mat-relu` → `mat-transpose` |
| 4 (repair) | Different signature, repaired by shuffle insertion | Speculative | `mat-relu` → `mat-sum` + repair |

The genetic operator weights substitutions by level: Level 1 mutations are attempted 60% of the time, Level 2 at 25%, Level 3 at 10%, Level 4 at 5%. This focuses evolution on semantically meaningful alternatives while still allowing occasional wild-card mutations that might discover unexpected improvements.

### Tag Vocabulary

An initial set of semantic tags for the matrix/MLP words:

| Tag | Meaning | Words |
|-----|---------|-------|
| `activation` | Neural activation function | mat-relu, mat-sigmoid, mat-tanh |
| `activation-deriv` | Activation derivative | mat-relu', mat-sigmoid', mat-tanh' |
| `element-wise` | Per-element operation, preserves shape | mat-relu, mat-sigmoid, mat-tanh, mat-scale, mat-clip, mat-hadamard |
| `shape-preserving` | Output has same dimensions as input | mat-relu, mat-sigmoid, mat-tanh, mat-scale, mat-clip |
| `reducing` | Collapses one or more dimensions | mat-sum, mat-mean, mat-col-sum |
| `broadcasting` | Expands/repeats along a dimension | mat-add-col |
| `combining` | Takes two matrices, produces one | mat*, mat+, mat-, mat-hadamard |
| `structural` | Changes matrix geometry | mat-transpose |
| `initializing` | Creates a new matrix | mat-randn, mat-new, mat-eye, mat-rand |
| `loss` | Measures prediction error | mat-mse, mat-cross-entropy |
| `classification` | Produces probability distribution | mat-softmax |
| `scalar-output` | Produces a scalar from a matrix | mat-sum, mat-mean, mat-det, mat-norm, mat-trace |

The tag vocabulary is extensible — new tags can be added to `help.til` without any C++ changes. The genetic operators read them at runtime.

---

## Component 7: Dictionary Signature Index

The genetic operators need to efficiently find words with compatible stack signatures. This requires an index on `TypeSignature`:

```cpp
class SignatureIndex {
public:
    // Build index from dictionary
    void rebuild(const Dictionary& dict);

    // Find all words with a given stack effect
    std::vector<std::string> find_compatible(
        int consumed, int produced) const;

    // Find all words with exact type signature match
    std::vector<std::string> find_exact(
        const TypeSignature& sig) const;

private:
    // Key: (consumed, produced) -> vector of word names
    std::map<std::pair<int,int>, std::vector<std::string>> by_effect_;
};
```

This index is rebuilt when the dictionary changes (tracked via `Dictionary::generation()`). For a dictionary with ~315 words, the index is small and fast to query.

---

## File Organization

| File | Lines (est.) | Purpose |
|------|-------------|---------|
| `include/etil/core/compiled_body.hpp` | +15 | BlockBegin/BlockEnd/BlockSeparator opcodes, BlockKind enum |
| `src/core/compiled_body.cpp` | +5 | No-op handler for marker opcodes |
| `src/core/control_flow_handlers.cpp` | +40 | Emit markers around control structures |
| `include/etil/evolution/ast.hpp` | 80 | ASTNode struct, ASTNodeKind enum |
| `include/etil/evolution/decompiler.hpp` | 30 | Decompiler class declaration |
| `include/etil/evolution/ast_compiler.hpp` | 30 | AST-to-bytecode compiler declaration |
| `include/etil/evolution/stack_simulator.hpp` | 40 | Stack effect simulator declaration |
| `include/etil/evolution/type_repair.hpp` | 40 | Type-directed repair declaration |
| `include/etil/evolution/ast_genetic_ops.hpp` | 50 | AST-level genetic operators declaration |
| `include/etil/evolution/signature_index.hpp` | 30 | Dictionary signature index declaration |
| `src/evolution/decompiler.cpp` | 150 | Marker-based bytecode-to-AST |
| `src/evolution/ast_compiler.cpp` | 200 | AST-to-bytecode with markers + backpatching |
| `src/evolution/stack_simulator.cpp` | 150 | Stack effect tracing |
| `src/evolution/type_repair.cpp` | 120 | Mismatch detection + shuffle insertion |
| `src/evolution/ast_genetic_ops.cpp` | 200 | 5 genetic operators (use repair) |
| `src/evolution/signature_index.cpp` | 60 | Signature indexing |
| `tests/unit/test_decompiler.cpp` | 150 | Round-trip fidelity tests |
| `tests/unit/test_ast_compiler.cpp` | 150 | Compilation correctness tests |
| `tests/unit/test_stack_simulator.cpp` | 100 | Effect calculation tests |
| `tests/unit/test_type_repair.cpp` | 150 | Repair + shuffle insertion tests |
| `tests/unit/test_ast_genetic_ops.cpp` | 200 | Mutation validity tests |
| **Total** | **~2090** | |

---

## Integration with Existing Architecture

### EvolutionEngine Changes

`EvolutionEngine::evolve_word()` changes from:

```cpp
// Old: bytecode-level
child = genetic_ops_.clone(*parent, dict_);
genetic_ops_.mutate(*child->bytecode());
```

To:

```cpp
// New: AST-level
auto ast = decompiler_.decompile(*parent->bytecode());
stack_simulator_.annotate(ast, dict_);
ast_genetic_ops_.mutate(ast, signature_index_);
auto new_bytecode = ast_compiler_.compile(ast);
child->set_bytecode(new_bytecode);
```

### Backward Compatibility

The old `GeneticOps` class remains available for simple constant perturbation (which doesn't need AST). The `EvolutionEngine` can use AST-level operators for structural mutations and bytecode-level operators for value mutations, combining both:

```cpp
// AST-level: structural change (swap activation function)
auto ast = decompiler_.decompile(*child->bytecode());
ast_genetic_ops_.substitute_call(ast, signature_index_);
child->set_bytecode(ast_compiler_.compile(ast));

// Bytecode-level: value change (perturb learning rate constant)
genetic_ops_.perturb_constant(*child->bytecode());
```

### SelectionEngine Integration

No changes needed. The `SelectionEngine` operates on `WordImpl` weight values, which are set by `EvolutionEngine` based on fitness scores. The AST-level changes are internal to the evolution pipeline.

---

## Testing Strategy

### Round-Trip Tests

For every control flow pattern in ETIL, verify:

```cpp
TEST(DecompilerTest, RoundTripIfThenElse) {
    // Compile a word with if/else/then
    interp.interpret_line(": test dup 0> if 1 + else 1 - then ;");
    auto impl = dict.lookup("test");
    auto& original_bc = *impl->get()->bytecode();

    // Decompile to AST
    auto ast = decompiler.decompile(original_bc);

    // Verify AST structure
    ASSERT_EQ(ast.kind, ASTNodeKind::Sequence);
    // ... check children ...

    // Recompile
    auto new_bc = compiler.compile(ast);

    // Verify identical execution
    // Push test inputs, execute original, save results
    // Push same inputs, execute recompiled, compare results
}
```

Test cases needed:
- Simple linear (no control flow)
- if/then (no else)
- if/else/then
- Nested if/else/then
- do/loop with i
- do/+loop
- Nested do/loop with i and j
- begin/until
- begin/while/repeat
- begin/again (with exit)
- Mixed: do/loop containing if/then
- leave inside do/loop
- exit inside nested calls
- >r / r> / r@ pairs

### Mutation Validity Tests

For each genetic operator, verify that the mutated AST:
1. Compiles without error
2. Produces a `ByteCode` that executes without crashing
3. Has the expected structural change (e.g., activation function actually changed)

### Semantic Preservation Tests

For constant perturbation (the one mutation that shouldn't change structure):
1. Decompile, perturb a constant, recompile
2. Verify the mutant produces *different* output (the constant changed)
3. Verify the mutant doesn't crash (structure preserved)

---

## Risks and Mitigations

### Risk: Some words have partially or fully undeterminable type signatures

**Mitigation:** Three categories exist. Category 1 (fully determinable) covers all primitives and most colon definitions — these are the primary evolution targets. Category 2 (partially determinable) has a known fixed prefix usable for substitution matching. Category 3 (fully opaque, only `execute`/`evaluate`) is treated as an opaque block — no type-safe substitution, but structural mutations still apply. The `variable_inputs`/`variable_outputs` flags on `TypeSignature` let the genetic operators make informed decisions about what's safe to mutate.

### Risk: AST-level mutations still produce semantically wrong code

**Mitigation:** This is expected and desirable. The goal is not to produce correct code — it's to produce *structurally valid* code that the fitness evaluator can meaningfully score. A mutation that replaces `mat-relu` with `mat-sigmoid` produces wrong output for a ReLU-trained network, but it's a *meaningful* wrong — the fitness score reflects the difference between activation functions, not a stack underflow.

### Risk: Round-trip compilation changes execution behavior

**Mitigation:** Comprehensive round-trip tests for every control flow pattern. The compiler and decompiler are inverse operations; any behavioral divergence is a bug to fix, not a design limitation. The `execute(compile(decompile(bc))) == execute(bc)` invariant is the primary acceptance criterion.

---

## Implementation Order

| Phase | Components | Deliverable |
|-------|-----------|------------|
| 0 | Marker opcodes in compiler + inner interpreter | Bytecode carries structural hints; all existing tests pass |
| A | AST types + Decompiler + Round-trip tests | Can decompile any ETIL word to AST |
| B | AST Compiler (emits markers) + Round-trip fidelity tests | Decompile → recompile produces identical behavior |
| C | Stack Simulator + Signature Index + Compile-time type inference | Can identify types at every instruction boundary |
| D | Type-Directed Repair | Deterministic fixup of type mismatches via shuffle insertion |
| E | AST Genetic Operators (use repair) + Mutation validity tests | Structurally valid, type-repaired mutations |
| F | EvolutionEngine integration | End-to-end AST-level evolution |

Phase 0 is a prerequisite — it modifies the existing compiler and inner interpreter, so it must land first and pass all existing tests. Phases A and B can then be developed in parallel (they're inverse operations, both reading/writing markers). Phase C requires A. Phase D requires C (needs the type stack). Phase E requires A, B, C, and D. Phase F requires all prior phases.

Phase 0 is intentionally small (~60 lines changed across 3 files) and has zero behavioral impact — the markers are no-ops. This makes it safe to merge to master early, so all subsequent bytecode is compiled with markers.

Phase D is the key innovation — it transforms the mutation validity rate from ~5% (random bytecode) to ~100% (type-repaired AST). Every subsequent phase benefits from this foundation.

---

## Success Criteria

1. **Round-trip fidelity:** `execute(compile(decompile(bc)))` produces identical results to `execute(bc)` for all 14 control flow patterns listed above.

2. **Mutation validity rate:** >95% of AST-level mutations produce bytecode that executes without stack underflow or type errors (vs. <5% for bytecode-level mutations).

3. **Evolution efficiency:** AST-level evolution reaches a given fitness target in fewer generations than bytecode-level evolution on a standard benchmark (e.g., evolving a word to compute `dup +` from `dup *`).

4. **No regressions:** All existing tests pass unchanged. The `EvolutionEngine` API is unchanged — callers don't need to know whether mutations are AST-level or bytecode-level.

## Author Notes: 20260318

I was dissatisfied with the prospect of the genetic-ops code randomly swapping opcodes... I could see that most will just fail.


This all resulted from me asking:
```text
"I wonder if instead of rejecting 'invalid' words we an take a higher level approach to these mutations. This involves parsing the word and reverse compiling it to build an AST.... Then identify what are the
 control structures .vs. computational code. Then apply crossover at the higher level via the AST and re-compile back the mutated AST to code. Furthermore apply code simulation off the AST to identify the
parameters being fed to calls to identify functional blocks that and be mutated (moved) as a group so as to avoid creating nonsense code."
```