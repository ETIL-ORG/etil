# Manifold E2E Tests

Out-of-band, manual end-to-end validation of Manifold against a deployed
ETIL MCP server. **Not** wired into CTest. Gates the cross-session ITC
work — see `docs/claude-design/20260426C-Manifold-E2E-Validation-Plan.md`.

## Layout

```
tests/e2e/manifold/
├── README.md                       — this file
├── local_loopback.til              — E2E #1: local channel round-trip
├── broker_loopback_nats.til        — E2E #2: NATS broker round-trip
├── harness/
│   ├── run_e2e.py                  — driver (Python 3, stdlib urllib, SSE-aware)
│   └── run_e2e.sh                  — thin shim that execs run_e2e.py
└── expected/
    ├── local_loopback.expected     — expected PASS lines for visual diff
    └── broker_loopback_nats.expected
```

## Prerequisites

- Python 3.10+ on the driver host (stdlib only — no pip install).
- A reachable deployed ETIL MCP server with HTTP transport enabled.
- The MCP server's bearer token retrievable via a shell command (so the secret
  is never written to disk or shell history).
- For `broker_loopback_nats.til`: a deployed NATS broker reachable from the
  ETIL server's container.

## Role permissions

The tests use the `admin` role's existing grant pattern `etil.**`. All test
channels are namespaced under `etil.test.e2e.*`, which `admin` already covers
out of the box:

- `channels_enabled: true`
- `channels_route_admin: true` (required by `obs-loop-channels`)
- `channels_network_sink: true` (required by `channel-tap-nats` / `channel-source-nats`)
- `channel_grants` covering `etil.**` with read/write/route/introspect

If a non-admin role is used instead, it needs the same flags plus an explicit
grant for `etil.test.e2e.**` (or `etil.**`). No dedicated `e2e_test` role is
required — the namespace is the isolation mechanism.

## Running

```bash
# Point at your deployed server. Token retrieval is a shell command so
# the secret never lands in your shell history.
export ETIL_MCP_URL='https://your-deployment/mcp'
export ETIL_MCP_KEY_CMD='cat ~/.etil/e2e-token'
# For broker_loopback_nats.til only:
export ETIL_NATS_URL='nats://nats:4222'   # the deployment's broker URL

# E2E #1 — local channel round-trip
./harness/run_e2e.sh local_loopback.til

# E2E #2 — NATS broker round-trip
./harness/run_e2e.sh broker_loopback_nats.til
```

Exit 0 means every TIL test in the file emitted `PASS`. Exit 1 means at least
one `FAIL`, or a transport / JSON-RPC error.

The driver substitutes the literal token `NATS_URL` in the .til source with
the value of `--nats-url` / `$ETIL_NATS_URL` before sending. This keeps the
deployed broker hostname out of the committed test source.

## What these tests prove

- **`local_loopback.til`** validates the TIL surface against the in-process
  Manifold implementation: direct write/read, forwarding loops with 0/1/2
  transforms, transform-rejects-and-next-message-passes, and a 100-message
  burst that exercises the async dispatcher under intra-session contention.
- **`broker_loopback_nats.til`** validates that messages survive a complete
  round-trip through the deployed broker: local channel A → tap-nats → NATS
  subject → source-nats → local channel B → reader. Covers a single message
  and three-message ordering preservation.

These tests exist so that the cross-session ITC design
(`docs/claude-design/20260426B-Cross-Session-Services-Architecture.md`) is
built on a substrate that has been proven end-to-end, not on a substrate
that is merely covered by C++ unit tests.

## When to run

Per `20260426C-Manifold-E2E-Validation-Plan.md` §7: before any cross-session
ITC code is merged, and on every merge that touches Manifold sources or the
broker bridges.

## Adding a new test

1. Edit the appropriate `.til` file. Each test should print
   `<name>: PASS` on success and `<name>: FAIL` on failure (the inline
   `pass` / `fail` / `expect-eq` / `expect-streq` words handle this).
2. Append the expected `<name>: PASS` line to the matching `.expected`
   file for documentation; the harness does not enforce it directly.
3. Channel names must start with `etil.test.e2e.` so they fall under the
   `etil.**` grant pattern admin already has, while staying isolated from
   production `etil.*` channels.
4. Re-run via the harness to confirm no regressions.
