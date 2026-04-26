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
│   └── run_e2e.sh                  — bash driver (curl + jq)
└── expected/
    ├── local_loopback.expected     — expected PASS lines for visual diff
    └── broker_loopback_nats.expected
```

## Prerequisites

- Bash, `curl`, `jq` on the driver host (the workstation running the tests).
- A reachable deployed ETIL MCP server with HTTP transport enabled.
- The MCP server's bearer token retrievable via a shell command (so the secret
  is never written to disk or shell history).
- For `broker_loopback_nats.til`: a deployed NATS broker reachable from the
  ETIL server's container, and a role on the server with the permissions
  described below.

## Role permissions required

Add an `e2e_test` role to `roles.json` (see `data/auth-config/roles.json.example`)
with at minimum:

```json
"e2e_test": {
  "max_sessions": 4,
  "instruction_budget": 10000000,
  "session_idle_timeout_seconds": 900,
  "interpret_execution_limit": 60,
  "session_execution_limit": 0,

  "lvfs_modify": false,
  "disk_quota": 0,

  "channels_enabled": true,
  "channels_route_admin": true,
  "channels_network_sink": true,
  "channel_grants": [
    { "pattern": "e2e.test.**",
      "actions": ["read","write","route","introspect"],
      "effect": "allow" }
  ]
}
```

`channels_route_admin` is required by `obs-loop-channels`. `channels_network_sink`
is required by `channel-tap-nats` / `channel-source-nats`. The grant is scoped
to `e2e.test.**` so the role cannot interfere with production channels.

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
3. Channel names must start with `e2e.test.` so the `e2e_test` role's grant
   matches.
4. Re-run via the harness to confirm no regressions.
