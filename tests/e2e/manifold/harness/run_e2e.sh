#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Manifold E2E driver — sends a .til test file to a deployed ETIL MCP
# server via tools/call interpret, scrapes PASS/FAIL markers from the
# captured stdout.
#
# Usage:
#   run_e2e.sh [--url URL] [--key-cmd CMD] [--nats-url URL] FILE
#
# Required env / args:
#   URL          MCP HTTP endpoint, e.g.  http://127.0.0.1:8080/mcp
#                Default: $ETIL_MCP_URL
#   KEY_CMD      shell command that prints the bearer token to stdout
#                (so secrets do not appear in process args/history).
#                Default: $ETIL_MCP_KEY_CMD
#   NATS_URL     Substituted into the .til file in place of the literal
#                token NATS_URL (so the broker hostname is not baked
#                into committed test source).
#                Default: $ETIL_NATS_URL    or  nats://nats:4222
#   FILE         Path to the .til test file to send.
#
# Exit:
#   0  every test in FILE emitted PASS
#   1  any FAIL marker present, or harness/network/transport error
#
# Dependencies: bash, curl, jq, sed.
#
# This script is NOT wired into CTest. It is an out-of-band gate run
# manually before ITC implementation begins; see
# docs/claude-design/20260426C-Manifold-E2E-Validation-Plan.md.

set -euo pipefail

usage() {
    cat <<EOF >&2
Usage: $0 [--url URL] [--key-cmd CMD] [--nats-url URL] FILE
  --url URL         MCP HTTP endpoint  (env: ETIL_MCP_URL)
  --key-cmd CMD     command that prints bearer token  (env: ETIL_MCP_KEY_CMD)
  --nats-url URL    NATS broker URL substituted into FILE
                    (env: ETIL_NATS_URL, default: nats://nats:4222)
  FILE              .til test file path
EOF
    exit 2
}

# ── parse args ────────────────────────────────────────────────────
URL="${ETIL_MCP_URL:-}"
KEY_CMD="${ETIL_MCP_KEY_CMD:-}"
NATS_URL="${ETIL_NATS_URL:-nats://nats:4222}"
FILE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --url)      URL="$2"; shift 2 ;;
        --key-cmd)  KEY_CMD="$2"; shift 2 ;;
        --nats-url) NATS_URL="$2"; shift 2 ;;
        -h|--help)  usage ;;
        --)         shift; FILE="$1"; shift; break ;;
        -*)         echo "unknown option: $1" >&2; usage ;;
        *)          FILE="$1"; shift ;;
    esac
done

[[ -z "$URL" ]]      && { echo "missing --url or ETIL_MCP_URL" >&2; usage; }
[[ -z "$KEY_CMD" ]]  && { echo "missing --key-cmd or ETIL_MCP_KEY_CMD" >&2; usage; }
[[ -z "$FILE" ]]     && { echo "missing FILE arg" >&2; usage; }
[[ ! -f "$FILE" ]]   && { echo "FILE not found: $FILE" >&2; exit 2; }

command -v curl >/dev/null || { echo "curl not found" >&2; exit 2; }
command -v jq   >/dev/null || { echo "jq not found"   >&2; exit 2; }

# ── prepare temp files ────────────────────────────────────────────
TMPDIR_RUN=$(mktemp -d -t etil-e2e-XXXXXX)
trap 'rm -rf "$TMPDIR_RUN"' EXIT

HDR_FILE="$TMPDIR_RUN/hdr"
INIT_BODY="$TMPDIR_RUN/init.json"
CALL_BODY="$TMPDIR_RUN/call.json"
RESP_FILE="$TMPDIR_RUN/resp.json"
TIL_FILE="$TMPDIR_RUN/test.til"

# Substitute NATS_URL placeholder so the deployment hostname does not
# live in the committed test source.
sed -e "s|NATS_URL|${NATS_URL}|g" "$FILE" > "$TIL_FILE"

# ── fetch bearer token ────────────────────────────────────────────
BEARER=$(eval "$KEY_CMD")
[[ -z "$BEARER" ]] && { echo "key-cmd produced empty token" >&2; exit 1; }
AUTH="Authorization: Bearer $BEARER"
CT="Content-Type: application/json"

# ── 1. initialize MCP session ─────────────────────────────────────
cat > "$INIT_BODY" <<EOF
{"jsonrpc":"2.0","id":1,"method":"initialize",
 "params":{"protocolVersion":"2024-11-05","capabilities":{},
           "clientInfo":{"name":"e2e-driver","version":"1.0"}}}
EOF

curl -sS -D "$HDR_FILE" -o /dev/null \
     -X POST "$URL" -H "$CT" -H "$AUTH" --data-binary "@$INIT_BODY"

# Match anchored ^Mcp-Session-Id: to avoid hitting Access-Control-Expose-Headers.
SID=$(grep -i '^Mcp-Session-Id:' "$HDR_FILE" | tr -d '\r' | awk '{print $2}')
if [[ -z "$SID" ]]; then
    echo "FATAL: server did not return Mcp-Session-Id" >&2
    cat "$HDR_FILE" >&2
    exit 1
fi
SID_HDR="Mcp-Session-Id: $SID"

# ── 2. send the .til via tools/call interpret ─────────────────────
# Use jq to JSON-encode the file body safely (handles quotes,
# backslashes, newlines).
jq -n --rawfile code "$TIL_FILE" \
    '{jsonrpc:"2.0",id:2,method:"tools/call",
      params:{name:"interpret",arguments:{code:$code}}}' \
    > "$CALL_BODY"

curl -sS -o "$RESP_FILE" \
     -X POST "$URL" -H "$CT" -H "$AUTH" -H "$SID_HDR" \
     --data-binary "@$CALL_BODY"

# ── 3. extract captured stdout from the response ─────────────────
# Prefer .result.content[0].text; on JSON-RPC error report and exit.
ERR=$(jq -r '.error.message // empty' "$RESP_FILE")
if [[ -n "$ERR" ]]; then
    echo "FATAL: JSON-RPC error: $ERR" >&2
    jq . "$RESP_FILE" >&2 || cat "$RESP_FILE" >&2
    exit 1
fi

OUTPUT=$(jq -r '.result.content[0].text // empty' "$RESP_FILE")
if [[ -z "$OUTPUT" ]]; then
    echo "FATAL: no captured stdout in response" >&2
    jq . "$RESP_FILE" >&2 || cat "$RESP_FILE" >&2
    exit 1
fi

# ── 4. report and tally ──────────────────────────────────────────
echo "── $(basename "$FILE") ──"
echo "$OUTPUT"
echo

PASS_N=$(printf '%s\n' "$OUTPUT" | grep -c 'PASS' || true)
FAIL_N=$(printf '%s\n' "$OUTPUT" | grep -c 'FAIL' || true)

echo "Result: $PASS_N PASS, $FAIL_N FAIL"

if [[ "$FAIL_N" -gt 0 ]] || [[ "$PASS_N" -eq 0 ]]; then
    exit 1
fi
exit 0
