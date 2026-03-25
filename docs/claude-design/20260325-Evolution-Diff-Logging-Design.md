# Evolution Diff and AST Dump Logging

**Date:** 2026-03-25
**References:** `20260324-AST-Evolution-Grow-Shrink-Design.md` (Phase 0 logging)
**Prerequisites:** v1.8.0
**Status:** Design

---

## Problem

The evolution debug log shows mutation decisions (operator selected, word substituted, node inserted/removed) but doesn't show the **resulting code**. When debugging why evolution isn't converging, you need to see:

1. What the program looked like before the mutation
2. What it looked like after
3. What type repair changed
4. What the AST structure is at each stage

The current log says "Substituted '+' → '*'" but doesn't show the full program context. You can't tell if the mutation was sensible without seeing the surrounding code.

---

## Design

### Two New Independently Controllable Views

**Diff view** — Side-by-side columnar display of before and after code, annotated with mutation metadata:

```
┌─ MUTATION: substitute '+' → '*' ──────────────────────────────────
│ BEFORE                    │ AFTER                     │ ANNOTATION
│ dup                       │ dup                       │
│ +                         │ *                         │ ← substituted (L1 arithmetic)
│                           │                           │
├─ TYPE REPAIR ─────────────────────────────────────────────────────
│ (no repair needed)
├─ RESULT: success ─────────────────────────────────────────────────
```

A more complex example with grow + repair:

```
┌─ MUTATION: grow 'abs' at position 1 ──────────────────────────────
│ BEFORE                    │ AFTER                     │ ANNOTATION
│ dup                       │ dup                       │
│                           │ abs                       │ ← inserted (grow-word, pool)
│ +                         │ +                         │
│                           │                           │
├─ TYPE REPAIR ─────────────────────────────────────────────────────
│ AFTER REPAIR              │                           │ ANNOTATION
│ dup                       │                           │
│ abs                       │                           │ ← inserted by mutation
│ dup                       │                           │ ← inserted by repair (stack balance)
│ +                         │                           │
├─ RESULT: success ─────────────────────────────────────────────────
```

Failed mutations (optional, controlled by a flag):

```
┌─ MUTATION: shrink 'dup' at position 0 ────────────────────────────
│ BEFORE                    │ AFTER                     │ ANNOTATION
│ dup                       │                           │ ← removed (shrink)
│ +                         │ +                         │
│                           │                           │
├─ TYPE REPAIR ─────────────────────────────────────────────────────
│ FAILED: needed type Integer at position 0, not found on stack
├─ RESULT: rejected ────────────────────────────────────────────────
```

**AST dump view** — Tree-format dump at two stages:

```
┌─ AST DECOMPILED ──────────────────────────────────────────────────
│ Sequence
│ ├── WordCall: dup
│ ├── Literal: PushInt 3
│ ├── WordCall: *
│ ├── WordCall: swap
│ └── WordCall: +
│
┌─ AST AFTER MUTATION (substitute: '+' → '-') ─────────────────────
│ Sequence
│ ├── WordCall: dup
│ ├── Literal: PushInt 3
│ ├── WordCall: *
│ ├── WordCall: swap
│ └── WordCall: -          ← mutated
│
┌─ AST AFTER REPAIR ───────────────────────────────────────────────
│ Sequence
│ ├── WordCall: dup
│ ├── Literal: PushInt 3
│ ├── WordCall: *
│ ├── WordCall: swap
│ └── WordCall: -
│ (no repair needed)
```

### Category Bits

Two new bits in `EvolveLogCategory`, independent of Logical/Granular levels:

```cpp
Diff     = 1 << 14,  // Side-by-side before/after code diff
ASTDump  = 1 << 15,  // Tree-format AST dumps
```

These are orthogonal to levels — they can be enabled at any level:

```til
# Logical + diff only (no AST dumps, no granular detail)
1 0x4001 evolve-log-start    # Engine + Diff

# Granular + AST dumps only
2 0x8000 evolve-log-start    # ASTDump only

# Everything
2 0xFFFF evolve-log-start    # All categories + diff + dump
```

### Show Failed Mutations

By default, diff view shows only **successful** mutations (those that survive type repair and produce a child). A config flag enables showing **failed** mutations too:

```til
# Enable diff for failed mutations
1 evolve-log-show-failed
```

This is useful for debugging why mutations don't work, but produces much more output (>90% of mutations fail).

### Code Representation

The "code" in the diff columns is the **decompiled TIL source**, not bytecode. Each AST node is rendered as one line:

| AST Node | Rendered |
|---|---|
| `WordCall("dup")` | `dup` |
| `WordCall("mat*")` | `mat*` |
| `Literal(PushInt, 42)` | `42` |
| `Literal(PushFloat, 3.14)` | `3.14` |
| `Literal(PushBool, true)` | `true` |
| `IfThen` | `if ... then` (with indented body) |
| `DoLoop` | `do ... loop` (with indented body) |
| `PrintString("hello")` | `." hello"` |

Control flow nodes show their keyword and indent their body. This matches what the user would write in TIL.

---

## Implementation

### New Function: `format_ast_as_code()`

Renders an AST as a flat list of TIL tokens (one per line for diff, or inline for compact display):

```cpp
std::vector<std::string> format_ast_as_code(const ASTNode& ast);
```

Returns one string per "line" of TIL code. For a `Sequence` with 3 children: `["dup", "3", "*"]`.

### New Function: `format_mutation_diff()`

Produces the columnar diff output:

```cpp
std::string format_mutation_diff(
    const std::vector<std::string>& before,
    const std::vector<std::string>& after,
    const std::string& mutation_desc,
    const std::vector<std::string>& after_repair,  // empty if no repair needed
    const std::string& repair_desc,
    bool success);
```

### Integration into `ASTGeneticOps::mutate()`

The diff is captured at three points in the mutation pipeline:

```
1. Decompile → capture "before" code
2. Mutate → capture "after mutation" code + mutation description
3. Repair → capture "after repair" code + repair description
4. Log the diff (if Diff category enabled)
```

### AST Dump

Uses the existing `ast_to_string()` function (already implemented in `ast.cpp`), wrapped in the log category check:

```cpp
if (logger_ && logger_->enabled(EvolveLogCategory::ASTDump)) {
    logger_->log(EvolveLogCategory::ASTDump,
        "AST DECOMPILED:\n" + ast_to_string(ast));
}
```

---

## Modified Files

| File | Change |
|---|---|
| `include/etil/evolution/evolve_logger.hpp` | Add `Diff` and `ASTDump` to `EvolveLogCategory`, add `show_failed` flag |
| `src/evolution/evolve_logger.cpp` | Add category tags for new bits |
| `include/etil/evolution/ast.hpp` | Declare `format_ast_as_code()` |
| `src/evolution/ast.cpp` | Implement `format_ast_as_code()` and `format_mutation_diff()` |
| `src/evolution/ast_genetic_ops.cpp` | Capture before/after/repair code, emit diff and AST dump |
| `src/core/primitives.cpp` | Register `evolve-log-show-failed` |

---

## Estimated Effort

| Task | Solo Human | AI-Assisted |
|---|---|---|
| format_ast_as_code + format_mutation_diff | 3 hours | 1 hour |
| Wire into mutate() pipeline | 2 hours | 30 min |
| AST dump integration | 30 min | 10 min |
| TIL word + testing | 1 hour | 20 min |
| **Total** | **~7 hours** | **~2 hours** |
