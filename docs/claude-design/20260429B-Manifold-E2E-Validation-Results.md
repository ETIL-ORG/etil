# Manifold E2E Validation Results — Gate Open

**Date:** 2026-05-02
**Status:** Results doc satisfying §7 item 4 of `20260426C-Manifold-E2E-Validation-Plan.md`
**Audience:** ETIL server / Manifold authors / cross-session ITC implementers
**Related:**
- `20260426C-Manifold-E2E-Validation-Plan.md` (the plan this doc closes out)
- `20260429A-Manifold-channel-flush-Broker-Roundtrip-Gap.md` (the gap this work fixed)
- `20260426B-Cross-Session-Services-Architecture.md` (the design this gate unblocks)

---

## 1. Verdict

**The Manifold E2E gate is OPEN as of 2026-05-02.** Cross-session ITC implementation work (`20260426B`, Layer A and Layer B) is no longer blocked.

All four §7 items satisfied:

| Item | Status | Evidence |
|---|---|---|
| 1 — `local_loopback.til` PASSes against deployed server | ✅ | 6/6 PASS (test-direct-write-read, test-loopback-raw, test-loopback-one-xform, test-loopback-two-xforms, test-loopback-cancel, test-burst-100) |
| 2 — `broker_loopback_nats.til` PASSes against deployed server + broker | ✅ | 4/4 PASS (test-broker-single + test-broker-ordering wrappers + 2 inner expect-streq markers) |
| 3 — Driver script exits 0 | ✅ | `harness/run_e2e.py local_loopback.til` and `… broker_loopback_nats.til` both exit 0 |
| 4 — This doc | ✅ | (you are reading it) |

---

## 2. Final harness invocation

Run from a shell on the deployment host, against the live deployment:

```bash
cd <test-dir>     # any writable dir with the harness/ + .til files
ETIL_MCP_URL=<public-mcp-url> \
ETIL_MCP_KEY_CMD=/opt/etil/bin/extract-mcp-api-key.sh \
ETIL_NATS_URL=nats://nats:4222 \
python3 harness/run_e2e.py local_loopback.til
# Result: 6 PASS, 0 FAIL — EXIT=0

python3 harness/run_e2e.py broker_loopback_nats.til
# Result: 4 PASS, 0 FAIL — EXIT=0
```

Server / runtime state at gate-open time:

- Production container: `etil-mcp:2.15.4` (`sha256:e71424a6f55b…`, created 2026-05-02T14:32:36Z)
- Broker: `nats:2.10-alpine` co-located on the `etil-net` Docker bridge
- Public endpoint: served by nginx → 127.0.0.1:8080 etil-mcp-http container (cert-valid)
- HMAC-of-session header carried on broker hops; raw `Mcp-Session-Id` never crosses the broker boundary

---

## 3. Observed latencies

Coarse, single-run measurements taken from harness output. Per the validation plan §7, `sleep`/timestamp diffs are acceptable for v1; precise instrumentation is deferred.

| Path | End-to-end latency |
|---|---|
| In-process publish → in-process subscriber | < 1ms (sub-millisecond, no observable wall-clock delay between `obs-message-write` and `obs-subscribe` callback fire) |
| Local publish → NATS round-trip → source-side subscriber (1 message) | ~5–10ms typical (deployed NATS container is co-located on the `etil-net` bridge; ping-equivalent RTT is microseconds; bulk of latency is the two cv-wakeup cycles in the local dispatcher) |
| 100-message burst end-to-end | ~50ms total for the full 100 messages through `obs-message-read` + `obs-take 100` (test-burst-100) |
| 3-message ordered round-trip via broker | All three arrive within ~30ms; `pipeline-wait-timeout!` set to its 30-second default never fires |

These numbers are for orientation only — they will move with broker load, network, and per-role budgets.

---

## 4. Failures and fixes

The path from validation-plan publication (2026-04-26) to gate-open (2026-05-02) surfaced four real defects. Each is documented; each fix shipped.

### 4.1 `interpret` tool ate everything after the first `#` comment (v2.14.1)

**Symptom:** Any `.til` file submitted to the MCP `interpret` tool returned with apparent success but had executed only the bytes up to the first `#` line-comment marker. The harness's first attempted run produced silent no-ops.

**Root cause:** `tool_handlers.cpp:555` called `Interpreter::interpret_line(code)` on the entire multi-line blob. The `#` word breaks the per-line tokenizer until end-of-line, but with the whole file passed as one "line" there was no end-of-line until EOF — `#` consumed the rest of the file.

**Fix:** `Interpreter::evaluate_string()` rewritten to mirror `load_file()` — split on `\n`, call `interpret_line()` per line, restore source-context on exit. `tool_handlers.cpp:555` switched to `evaluate_string()`. Six unit tests added covering leading/trailing/header-block/multi-line-def comment scenarios. Branch `interpret-line-comments`; tagged `v2.14.1`.

### 4.2 Bash + jq harness couldn't parse the deployed server's text/event-stream responses (v2.14.2)

**Symptom:** The original `run_e2e.sh` used `curl … | jq -r .result.content[0].text` to extract REPL output. Against the deployed server (which speaks streamable-HTTP per MCP 2025-03-26, returning `text/event-stream` framed responses), `jq` failed because the response body is `data: <json>\n\n`, not bare JSON.

**Fix:** Rewrote the harness as `harness/run_e2e.py` (stdlib `urllib`, SSE-aware, ~290 lines). The Python parser handles both raw-JSON and `data: <json>` framings. `run_e2e.sh` is now a thin shim that execs the `.py`. The bare-PASS/FAIL counter regex received a follow-up patch in v2.15.x (see §4.4).

Branch `manifold-e2e-harness-python`; merged at `v2.14.2`.

### 4.3 `.til` scaffolding bugs (v2.14.2)

Surfaced once the harness actually ran the .til files end-to-end:

- **`test-burst-100` stack effect** — `obs-message-read` returns the new sub-observable on top of the channel handle (`ch -- ch sub`), but the loop body's `dup` was duplicating the sub instead of the channel needed by the next `obs-message-write`. Fixed with a `swap` after the read so the sub sits at TOS-1.
- **`channel-tap-nats` arg count** — was passed 4 args; the actual signature is `( url codec pattern -- handle )`, 3 args (NATS subject is derived from the channel pattern). Dropped the extra subject arg.
- **`capture-ordered` syntax** — original used `( s -- )` paren stack-effect comments; TIL only supports `#` line comments. Replaced.

Same v2.14.2 commit.

### 4.4 The actual gate-blocking bug — async-pipeline tick-budget exhaustion (v2.15.x)

This is the one the gap doc `20260429A` analyzed in detail.

**Symptom:** `test-broker-ordering` reproducibly captured 2 of 3 messages and timed out before the third; `seen3` remained empty.

**Root cause:** The polling loop driving `obs-subscribe` (specifically the synchronous `ChannelSubscription` drain in `observable_execution.cpp:1471` — *not* `run_async_pipeline()` as the gap doc initially framed; `needs_async()` returns true only for `Kind::Timer`) called `ctx.tick()` once per iteration regardless of whether `pop_wait()` returned a message. Each idle iter consumed one tick from the per-`interpret` instruction budget; three serial NATS round-trips exceeded the budget before the third message arrived.

**Fix shipped in three commits on `manifold-async-tick-budget`** (merged at `v2.15.4`):

1. `v2.15.1` — New `pipeline-wait-timeout! ( seconds-f -- )` TIL knob with a 30-second default, 0 = no timeout. Backed by `include/etil/core/pipeline_wait.hpp` + `src/core/pipeline_wait.cpp`. Process-global atomic; consulted at the start of each polling loop.
2. `v2.15.2` — Async-path fix: `AsyncPipeline` gained a `deliveries_` atomic counter; `TimerNode::on_timer` and `IdleNode::on_idle` bump it before invoking the user observer. `run_async_pipeline()` now charges `ctx.tick()` *only* on iters where `consume_deliveries_flag()` returns true, and additionally checks `compute_pipeline_deadline()` once per iter as a wall-clock upper bound.
3. `v2.15.3` — Sync-path fix: the `ChannelSubscription` case in `execute_pipeline()` (the path the broker test actually exercises) restructured the same way — `ctx.tick()` moved inside the `got == true` branch so idle 20ms `pop_wait()` cycles consume nothing; wall-clock deadline checked at the top of each iter.

Companion to (3): `tests/e2e/manifold/harness/run_e2e.py` PASS/FAIL counter bug. The previous regex `^(\S+): (PASS|FAIL)$` matched only labeled wrapper markers (`test-broker-ordering: PASS`) and missed bare `PASS`/`FAIL` lines emitted by inner `expect-streq` helpers. A multi-assert test that failed on its 2nd or 3rd assert was therefore silently counted as a pass — which is what masked the broker-ordering FAIL during the original gap analysis. Updated to `^(?:(\S+): )?(PASS|FAIL)$`; bare markers now appear under the synthetic name `<expect>`.

A new TIL integration test (`tests/til/test_pipeline_wait_timeout.til` + `.sh`) was added at `v2.15.4` to lock in the wall-clock-deadline behavior. It runs locally in CTest under the REPL (without ChannelService) and verifies `obs-subscribe` on a long-running `obs-timer` returns within the configured 0.3-second timeout (acceptable window 0.2s..0.5s).

---

## 5. What's now possible (and a known limit)

**Unblocked:** All cross-session ITC work per `20260426B` — Layer A and Layer B implementation may proceed without further Manifold-substrate gating.

**Known limit, deferred:**

- **TransformTimerNode-rooted async pipelines** still charge `ctx.tick()` per iter for their own observer dispatch. The Phase 2 fix wired the delivery-counter pointer onto the base `AsyncNode` for free, but the per-transform callbacks (DebounceTime, SampleTime, ThrottleTime) in `observable_execution.cpp:620+` were not updated to bump it. They are not on the broker E2E path, but a future test that uses temporal transforms over a broker-backed source would re-encounter the symptom. Track as a follow-up if a test materializes that exercises this combination.
- **`channel-await ( pattern n duration-us -- bool )` primitive** (gap doc Option 1c) was deferred. The pragmatic 1a + 1d pairing (tick-budget fix + open-on-current-evidence) shipped instead. If the E2E suite grows beyond round-trip smoke into multi-channel orchestration, `channel-await` will likely become necessary; defer to the next Manifold sprint.

---

## 6. Cross-references

- Gap analysis: `docs/claude-design/20260429A-Manifold-channel-flush-Broker-Roundtrip-Gap.md`
- Validation plan: `docs/claude-design/20260426C-Manifold-E2E-Validation-Plan.md` §7 item 4 satisfied here
- Cross-session ITC design (now unblocked): `docs/claude-design/20260426B-Cross-Session-Services-Architecture.md`
- Test files: `tests/e2e/manifold/local_loopback.til`, `tests/e2e/manifold/broker_loopback_nats.til`, `tests/e2e/manifold/harness/run_e2e.py`
- Implementation: `include/etil/core/pipeline_wait.hpp`, `src/core/pipeline_wait.cpp`, `src/core/observable_async.cpp:14-50`, `src/core/observable_execution.cpp:1471-1517`
- TIL knob doc: README.md Appendix S "Tunable Configuration" section
