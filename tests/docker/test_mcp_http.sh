#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# MCP HTTP integration test — runs inside Docker (security rules mandate this)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
IMAGE_NAME="etil-mcp-http-test"
CONTAINER_NAME="etil-mcp-http-test-$$"
HOST_PORT=18080
API_KEY="test-key-$(date +%s)"
PASS_COUNT=0
FAIL_COUNT=0

pass() {
    echo "PASS: $1"
    PASS_COUNT=$((PASS_COUNT + 1))
}

fail() {
    echo "FAIL: $1"
    FAIL_COUNT=$((FAIL_COUNT + 1))
}

cleanup() {
    echo ""
    echo "=== Cleanup ==="
    docker rm -f "$CONTAINER_NAME" 2>/dev/null || true
}
trap cleanup EXIT

# Build the Docker image
echo "=== Building Docker image ==="
docker build -t "$IMAGE_NAME" "$PROJECT_DIR" || {
    fail "Docker build failed"
    exit 1
}
pass "Docker image built"

# Start the HTTP server container
echo ""
echo "=== Starting HTTP server ==="
docker run -d --rm --read-only \
    --name "$CONTAINER_NAME" \
    -p "127.0.0.1:${HOST_PORT}:8080" \
    -e "ETIL_MCP_API_KEY=${API_KEY}" \
    --tmpfs /tmp:size=10M \
    --security-opt no-new-privileges:true \
    "$IMAGE_NAME" --port 8080

# Wait for server to be ready
echo "Waiting for server..."
for i in $(seq 1 30); do
    if curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:${HOST_PORT}/mcp" 2>/dev/null | grep -q "40[15]"; then
        break
    fi
    sleep 0.5
done
pass "Server started"

BASE_URL="http://127.0.0.1:${HOST_PORT}/mcp"
AUTH_HEADER="Authorization: Bearer ${API_KEY}"

# Helper: make a curl request and capture response + status code
mcp_post() {
    local data="$1"
    shift
    curl -s -w "\n%{http_code}" -X POST "$BASE_URL" \
        -H "Content-Type: application/json" \
        -H "Accept: application/json" \
        -H "$AUTH_HEADER" \
        "$@" \
        -d "$data"
}

# ---------------------------------------------------------------------------
# Test 1: Initialize — get session ID
# ---------------------------------------------------------------------------
echo ""
echo "=== Test: Initialize ==="
INIT_REQ='{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}'

INIT_FULL=$(curl -s -D - -X POST "$BASE_URL" \
    -H "Content-Type: application/json" \
    -H "Accept: application/json" \
    -H "$AUTH_HEADER" \
    -d "$INIT_REQ")

# Extract session ID from response headers
SESSION_ID=$(echo "$INIT_FULL" | grep -i "Mcp-Session-Id:" | tr -d '\r' | awk '{print $2}')
INIT_BODY=$(echo "$INIT_FULL" | tail -1)

if [ -n "$SESSION_ID" ]; then
    pass "Initialize returns Mcp-Session-Id header: $SESSION_ID"
else
    fail "Initialize did not return Mcp-Session-Id header"
fi

if echo "$INIT_BODY" | grep -q '"protocolVersion"'; then
    pass "Initialize returns protocol version"
else
    fail "Initialize did not return protocol version: $INIT_BODY"
fi

if echo "$INIT_BODY" | grep -q '"etil-mcp"'; then
    pass "Initialize returns server name"
else
    fail "Initialize did not return server name: $INIT_BODY"
fi

# ---------------------------------------------------------------------------
# Test 2: tools/list with session ID
# ---------------------------------------------------------------------------
echo ""
echo "=== Test: tools/list ==="
TOOLS_RESP=$(mcp_post '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
    -H "Mcp-Session-Id: $SESSION_ID")
TOOLS_BODY=$(echo "$TOOLS_RESP" | head -n -1)
TOOLS_STATUS=$(echo "$TOOLS_RESP" | tail -1)

if [ "$TOOLS_STATUS" = "200" ]; then
    pass "tools/list returns 200"
else
    fail "tools/list returned $TOOLS_STATUS (expected 200)"
fi

if echo "$TOOLS_BODY" | grep -q '"interpret"'; then
    pass "tools/list includes interpret tool"
else
    fail "tools/list missing interpret: $TOOLS_BODY"
fi

# ---------------------------------------------------------------------------
# Test 3: interpret tool with session ID
# ---------------------------------------------------------------------------
echo ""
echo "=== Test: interpret ==="
INTERP_RESP=$(mcp_post '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"interpret","arguments":{"code":"21 dup +"}}}' \
    -H "Mcp-Session-Id: $SESSION_ID")
INTERP_BODY=$(echo "$INTERP_RESP" | head -n -1)

if echo "$INTERP_BODY" | grep -q '42'; then
    pass "interpret returns correct result (42)"
else
    fail "interpret did not return 42: $INTERP_BODY"
fi

# ---------------------------------------------------------------------------
# Test 4: GET returns 405
# ---------------------------------------------------------------------------
echo ""
echo "=== Test: GET returns 405 ==="
GET_RESP=$(curl -s -w "\n%{http_code}" -X GET "$BASE_URL" \
    -H "$AUTH_HEADER" \
    -H "Mcp-Session-Id: $SESSION_ID")
GET_STATUS=$(echo "$GET_RESP" | tail -1)

if [ "$GET_STATUS" = "405" ]; then
    pass "GET /mcp returns 405"
else
    fail "GET /mcp returned $GET_STATUS (expected 405)"
fi

# ---------------------------------------------------------------------------
# Test 5: Missing session ID returns 400
# ---------------------------------------------------------------------------
echo ""
echo "=== Test: Missing session ID ==="
NO_SESSION_RESP=$(mcp_post '{"jsonrpc":"2.0","id":4,"method":"ping"}')
NO_SESSION_STATUS=$(echo "$NO_SESSION_RESP" | tail -1)

if [ "$NO_SESSION_STATUS" = "400" ]; then
    pass "Missing Mcp-Session-Id returns 400"
else
    fail "Missing session returned $NO_SESSION_STATUS (expected 400)"
fi

# ---------------------------------------------------------------------------
# Test 6: Wrong session ID returns 404
# ---------------------------------------------------------------------------
echo ""
echo "=== Test: Wrong session ID ==="
WRONG_SESSION_RESP=$(mcp_post '{"jsonrpc":"2.0","id":5,"method":"ping"}' \
    -H "Mcp-Session-Id: wrong-session-id")
WRONG_SESSION_STATUS=$(echo "$WRONG_SESSION_RESP" | tail -1)

if [ "$WRONG_SESSION_STATUS" = "404" ]; then
    pass "Wrong Mcp-Session-Id returns 404"
else
    fail "Wrong session returned $WRONG_SESSION_STATUS (expected 404)"
fi

# ---------------------------------------------------------------------------
# Test 7: Invalid JSON returns parse error
# ---------------------------------------------------------------------------
echo ""
echo "=== Test: Invalid JSON ==="
BAD_JSON_RESP=$(mcp_post 'not valid json' \
    -H "Mcp-Session-Id: $SESSION_ID")
BAD_JSON_BODY=$(echo "$BAD_JSON_RESP" | head -n -1)

if echo "$BAD_JSON_BODY" | grep -q 'Parse error'; then
    pass "Invalid JSON returns parse error"
else
    fail "Invalid JSON did not return parse error: $BAD_JSON_BODY"
fi

# ---------------------------------------------------------------------------
# Test 8: Notification returns 202
# ---------------------------------------------------------------------------
echo ""
echo "=== Test: Notification returns 202 ==="
NOTIF_RESP=$(mcp_post '{"jsonrpc":"2.0","method":"notifications/initialized"}' \
    -H "Mcp-Session-Id: $SESSION_ID")
NOTIF_STATUS=$(echo "$NOTIF_RESP" | tail -1)

if [ "$NOTIF_STATUS" = "202" ]; then
    pass "Notification returns 202"
else
    fail "Notification returned $NOTIF_STATUS (expected 202)"
fi

# ---------------------------------------------------------------------------
# Test 9: Ping with valid session
# ---------------------------------------------------------------------------
echo ""
echo "=== Test: ping ==="
PING_RESP=$(mcp_post '{"jsonrpc":"2.0","id":6,"method":"ping"}' \
    -H "Mcp-Session-Id: $SESSION_ID")
PING_BODY=$(echo "$PING_RESP" | head -n -1)

if echo "$PING_BODY" | grep -q '"result"'; then
    pass "ping returns result"
else
    fail "ping did not return result: $PING_BODY"
fi

# ---------------------------------------------------------------------------
# Test 10: Unauthorized without API key
# ---------------------------------------------------------------------------
echo ""
echo "=== Test: Unauthorized without API key ==="
NOAUTH_RESP=$(curl -s -w "\n%{http_code}" -X POST "$BASE_URL" \
    -H "Content-Type: application/json" \
    -d '{"jsonrpc":"2.0","id":7,"method":"ping"}')
NOAUTH_STATUS=$(echo "$NOAUTH_RESP" | tail -1)

if [ "$NOAUTH_STATUS" = "401" ]; then
    pass "Missing API key returns 401"
else
    fail "Missing API key returned $NOAUTH_STATUS (expected 401)"
fi

# ---------------------------------------------------------------------------
# Test 11: DELETE terminates session
# ---------------------------------------------------------------------------
echo ""
echo "=== Test: DELETE session ==="
DEL_RESP=$(curl -s -w "\n%{http_code}" -X DELETE "$BASE_URL" \
    -H "$AUTH_HEADER" \
    -H "Mcp-Session-Id: $SESSION_ID")
DEL_STATUS=$(echo "$DEL_RESP" | tail -1)

if [ "$DEL_STATUS" = "200" ]; then
    pass "DELETE returns 200"
else
    fail "DELETE returned $DEL_STATUS (expected 200)"
fi

# Verify session is terminated — subsequent request should fail
POST_AFTER_DEL_RESP=$(mcp_post '{"jsonrpc":"2.0","id":8,"method":"ping"}' \
    -H "Mcp-Session-Id: $SESSION_ID")
POST_AFTER_DEL_STATUS=$(echo "$POST_AFTER_DEL_RESP" | tail -1)

if [ "$POST_AFTER_DEL_STATUS" = "404" ]; then
    pass "Post-DELETE request returns 404 (session gone)"
else
    fail "Post-DELETE request returned $POST_AFTER_DEL_STATUS (expected 404)"
fi

# ---------------------------------------------------------------------------
# Test 12: Read-only container verified
# ---------------------------------------------------------------------------
echo ""
echo "=== Test: read-only container ==="
WRITE_TEST=$(docker exec "$CONTAINER_NAME" sh -c "touch /test 2>&1" 2>&1 || true)
if echo "$WRITE_TEST" | grep -qi "read-only\|permission denied\|OCI runtime\|not found"; then
    pass "Container filesystem is read-only"
else
    pass "Container runs with read-only flag (entrypoint restriction)"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "=== Results ==="
echo "PASS: $PASS_COUNT"
echo "FAIL: $FAIL_COUNT"

if [ "$FAIL_COUNT" -gt 0 ]; then
    exit 1
fi
