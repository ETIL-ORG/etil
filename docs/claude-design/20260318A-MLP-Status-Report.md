# MLP Design Analysis: Status Report — Sections 2.3 through 6

**Date:** 2026-03-18
**Reference:** `docs/claude-design/20260310-ETIL-MLP-Design-Analysis.md`
**Current Version:** ETIL v1.0.0

---

## Executive Summary

**Sections 2.3–3.1 (the primitives): 100% complete.** All 17 C++ primitives and all 3 TIL-level convenience words from the plan are implemented, tested, and deployed.

**Sections 3.2–5 (the TIL-level MLP architecture): 0% implemented.** The vocabulary proposed in sections 3.2 (network words), 4 (forward pass), and 5 (backpropagation) exists only as pseudocode in the design document. None of the higher-level words (`make-layer`, `make-mlp`, `forward-pass`, `backprop-layer`, `sgd-update`, `layer-forward`) have been written.

**Section 6 (the evolutionary angle): structurally possible but has a critical gap.** The infrastructure to *store* multiple implementations per word exists, but the infrastructure to *select* between them at runtime does not.

---

## Section 2.3 — Weight Initialization: DONE

- `mat-randn` — Box-Muller standard normal, uses shared `prng_engine()`
- `mat-xavier` — TIL word in `help.til`
- `mat-he` — TIL word in `help.til`

## Section 2.4 — In-place Operations: NOT DONE (by design)

The document recommended deferring `mat+!`, `mat-!`, `mat-scale!` as an optimization. They remain unimplemented. Every matrix operation allocates a new `HeapMatrix`. This is correct but will create significant allocation pressure in training loops (thousands of iterations x multiple layers x forward + backward + update). The document called this out: "get the MLP working first with functional operations, then add in-place variants where profiling shows it matters."

**Assessment**: Right call to defer. Not a blocker for getting a working MLP. Will become a blocker for *practical* training on anything beyond toy problems.

## Section 2.5 — Broadcasting: DONE

- `mat-add-col` implemented with full dimension checking

## Section 2.6 — Loss Functions: DONE

- `mat-mse` — TIL word
- `mat-softmax` — C++ primitive (column-wise, numerically stable)
- `mat-cross-entropy` — C++ primitive

## Section 2.7 — Matrix Slicing: NOT DONE

`mat-col-vec ( mat j -- mat )` returning an (n x 1) `HeapMatrix` was proposed but never implemented. `mat-col` still returns a `HeapArray`. The workaround is `mat-col` followed by `array->mat`, but that's a copy through an intermediate representation that loses column-vector shape information (you get a 1xn row instead of an nx1 column without manual reshaping).

**Assessment**: Minor gap. Could be worked around in the MLP words, but it would make the code uglier. Worth implementing before building the MLP layer words.

---

## Section 3.2 — ETIL-Level Network Words: NOT IMPLEMENTED

These words exist only as pseudocode in the document:

| Word | Purpose | Status |
|------|---------|--------|
| `layer-forward` | `( X W b xt -- Y )` | Not written |
| `sgd-update` | `( W grad lr -- W' )` | Not written |
| `make-layer` | `( fan_in fan_out xt -- map )` | Not written |
| `make-mlp` | `( in h1 h2 out -- array )` | Not written |

All the primitives these words need exist. This is pure TIL development — no C++ needed.

## Section 3.3 — Network as Data Structure: NOT IMPLEMENTED

The design proposed layers as `HeapMap`s with `"W"`, `"b"`, `"act"` keys, and networks as `HeapArray`s of layer maps. This is a good design that leverages existing infrastructure (maps, arrays, execution tokens). Nothing has been built.

## Section 4 — Forward Pass: NOT IMPLEMENTED

The `forward-pass` word (iterating layers, caching activations) is pseudocode only.

## Section 5 — Backpropagation: NOT IMPLEMENTED

The `backprop-layer` word is pseudocode only, and the document itself noted it was incomplete ("... scale by 1/m, compute db, compute dA_prev").

---

## Section 6 — Evolutionary Angle: THE INTERESTING PART

This is where the document gets genuinely exciting, and it's also where the biggest gap lies.

### What Exists Today

1. **Multiple implementations per word** — `Dictionary` stores a `WordConcept` with a `vector<WordImplPtr>`. You can register multiple `: activate mat-relu ;` definitions. This works.

2. **Per-implementation metadata** — `weight_` (atomic double), `PerfProfile` (call count, total duration, memory, success/failure), `MutationHistory`, `parent_ids_`, `generation_`. All present on `WordImpl`.

3. **`set_weight` MCP tool** — Can manually adjust implementation weights via MCP.

4. **`get_implementations()` API** — Dictionary exposes all implementations for a concept.

5. **`evolve.til` proof of concept** — A working (1+1) evolutionary strategy that evolves RPN expressions, demonstrating `evaluate` as a genotype-to-phenotype mapping.

### What Does NOT Exist — The Critical Gap

**`lookup()` always returns `.back()` — the most recently registered implementation.** There is no selection engine. Period. The `weight_` field on every `WordImpl` is written but never read by the execution path. When the interpreter executes a word, it calls `dict.lookup(word)` which returns the *last* registered implementation unconditionally. The weights, performance profiles, and generation metadata are inert data.

This means the Section 6 vision — "Register multiple implementations of `activate`, and the selection engine picks which one based on performance metrics" — is **architecturally supported** (the data structures are there) but **functionally disconnected** (nothing uses them for selection).

To make Section 6 real, you need:

1. **A `select()` method on Dictionary** (or a separate `SelectionEngine`) that:
   - Takes a word name
   - Gets all implementations
   - Applies a selection strategy (weighted random, epsilon-greedy, UCB1, etc.)
   - Returns the selected implementation

2. **Wire `select()` into the execution path** — Replace the `lookup()` call in `Interpreter::execute_word()` and the `Call` instruction in `execute_compiled()` with a `select()` call. This is a ~5 line change in two files, but it's the *most consequential* change because it means every word execution could potentially pick a different implementation.

3. **A fitness evaluation loop** — Something that runs candidate implementations against test cases, measures their `PerfProfile`, and updates weights. `evolve.til` demonstrates this pattern at the TIL level with `evaluate`, but for evolving compiled word implementations, you'd want a C++ `EvolutionEngine` that can:
   - Clone a `WordImpl` (its bytecode)
   - Mutate the bytecode (swap instructions, change constants)
   - Evaluate fitness (run test cases, measure accuracy + speed)
   - Update weights based on fitness

The `src/evolution/` and `src/selection/` directories are **empty** — just `.gitkeep` placeholders.

---

## The Path to Section 6

**Layer 1 — TIL-level MLP (no C++ needed):**
Write `make-layer`, `make-mlp`, `forward-pass`, `backprop-layer`, `sgd-update` in TIL. Train an XOR network or MNIST digit classifier as proof of concept. All primitives exist. This is maybe a focused session of work.

**Layer 2 — Selection engine (~200-300 lines of C++):**
Implement a `SelectionEngine` with at least weighted-random selection. Wire it into `lookup()` path with a feature flag. This turns the inert `weight_` fields into live selection pressure.

**Layer 3 — Evolution engine (~500+ lines of C++):**
`GeneticOps` (mutation/crossover on bytecode), `Fitness` (evaluation framework), `EvolutionEngine` (background loop). This is the meaty work — it's Phases 5 and 6 in the CLAUDE.md roadmap.

**Layer 4 — The Section 6 vision (DEFERRED):**
With layers 1-3 in place, you can evolve activation functions, network topologies, and learning rate schedules as described in the document. The `WordConcept` model naturally represents the search space; the evolution engine explores it; the selection engine exploits the best findings.

---

## Bottom Line

You're at the top of the pyramid looking down at a completed foundation. All the bricks (primitives) are laid. The mortar (TIL-level MLP words) hasn't been mixed yet — but it's straightforward work. The real frontier is the selection and evolution engines, which are the *raison d'etre* of the entire project and remain empty directories.
