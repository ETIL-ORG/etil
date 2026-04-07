# TIL Benchmarks

Benchmark scripts for evolution engine profiling and validation. **Not CI tests** — these run long (50-100+ generations) and are meant for interactive analysis.

## Running

```bash
# From workspace root, with debug REPL:
ETIL_REPL=~/workspace/build-debug/bin/etil_repl

# Run a benchmark:
echo 'include tests/til/bench/bench_tbbp_quad.til
/quit' | (cd evolutionary-til && "$ETIL_REPL" --quiet 2>/dev/null)

# Then analyze the evolve log:
ls /tmp/*-evolve.log | tail -1 | xargs less
```

## Benchmarks

### `bench_tbbp_quad.til`

TBBP on quadratic regression. Target: `f(x) = x² + 3x + 5` (Integer → Integer).

- Registers ~20 bridges
- Starting program: `: fn int->float dup * float->int ;` (already near-optimal, fitness ~1.0)
- 50 generations with TBBP enabled
- Logs all Bridge category events

**Expected finding:** Very sparse bridge signal (3-10 TBBP updates per run). Starting fitness is already high so most mutations produce reward=0.

### `bench_tbbp_cross_domain.til`

TBBP with cross-type target. Target: `f(x) = Float x²` (Integer input → Float output).

- Registers 8 focused bridges
- Starting program: `: fn dup * ;` (returns Integer, type mismatch with Float expected)
- 100 generations with TBBP enabled
- Logs all Bridge category events

**Expected finding:** Still sparse signal — reward signal is anti-correlated with TypeRepair firing. See `docs/claude-knowledge/20260405A-TBBP-Validation-Findings.md` for analysis.

### `bench_mce_monolithic.til`

Monolithic evolution baseline for MCE comparison. Target: `f(x) = x² + 3x + 5` (Integer → Integer).

- Single `target-fn` word, evolved monolithically
- 100 generations, RNG seed 42
- 9 test cases (x = -3, -1, 0, 1, 2, 3, 4, 5, 10)

### `bench_mce_quad.til`

MCE round-robin co-evolution of decomposed sub-concepts. Same target function.

- Decomposition: `square-term` / `linear-term` / `offset` / `target-fn`
- 100 rounds × 3 sub-concepts = 300 total `evolve-word` calls
- Same RNG seed (42) and test cases as monolithic for fair comparison

**MCE Phase Comparison:**

| Metric | Monolithic | MCE Chain |
|--------|-----------|-----------|
| Gen to ≥0.95 fitness | TBD | TBD |
| Final fitness | TBD | TBD |
| Weight variance (per sub-concept) | N/A | TBD |

## Adding New Benchmarks

Follow the naming pattern `bench_<feature>_<variant>.til`. Benchmarks should:
- Set log directory to `/tmp/`
- Enable granular logging
- Print generation progress
- Call `evolve-log-stop` at the end

They are NOT discovered by CTest — run them manually from a TIL session.
