#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# MCP File I/O E2E test — write_file, list_files, include roundtrip
# Runs inside Docker with session volumes (security rules mandate this)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
IMAGE_NAME="etil-mcp-fileio-test"
CONTAINER_NAME="etil-mcp-fileio-test-$$"
HOST_PORT=18081
API_KEY="test-key-$(date +%s)"
PASS_COUNT=0
FAIL_COUNT=0
REQ_ID=0

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
    docker volume rm "etil-fileio-sessions-$$" 2>/dev/null || true
    docker volume rm "etil-fileio-library-$$" 2>/dev/null || true
}
trap cleanup EXIT

# Build the Docker image
echo "=== Building Docker image ==="
docker build -t "$IMAGE_NAME" "$PROJECT_DIR" || {
    fail "Docker build failed"
    exit 1
}
pass "Docker image built"

# Create test volumes
docker volume create "etil-fileio-sessions-$$" >/dev/null
docker volume create "etil-fileio-library-$$" >/dev/null

# Start the HTTP server container with session volumes
echo ""
echo "=== Starting HTTP server with volumes ==="
docker run -d --rm --read-only \
    --name "$CONTAINER_NAME" \
    -p "127.0.0.1:${HOST_PORT}:8080" \
    -e "ETIL_MCP_API_KEY=${API_KEY}" \
    -e "ETIL_SESSIONS_DIR=/data/sessions" \
    -e "ETIL_LIBRARY_DIR=/data/library" \
    -v "etil-fileio-sessions-$$:/data/sessions" \
    -v "etil-fileio-library-$$:/data/library:ro" \
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
pass "Server started with volumes"

BASE_URL="http://127.0.0.1:${HOST_PORT}/mcp"
AUTH_HEADER="Authorization: Bearer ${API_KEY}"

# Helper: POST with session ID
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

next_id() {
    REQ_ID=$((REQ_ID + 1))
    echo "$REQ_ID"
}

# Helper: call a tool, return response body (strips status code)
call_tool() {
    local session_id="$1"
    local tool_name="$2"
    local args="$3"
    local id
    id=$(next_id)
    local resp
    resp=$(mcp_post "{\"jsonrpc\":\"2.0\",\"id\":$id,\"method\":\"tools/call\",\"params\":{\"name\":\"$tool_name\",\"arguments\":$args}}" \
        -H "Mcp-Session-Id: $session_id")
    echo "$resp" | head -n -1
}

# Helper: extract text content from tool response
extract_content() {
    local resp="$1"
    echo "$resp" | python3 -c "
import sys, json
r = json.load(sys.stdin)
print(r['result']['content'][0]['text'])
"
}

# ---------------------------------------------------------------------------
# Initialize — get session ID
# ---------------------------------------------------------------------------
echo ""
echo "=== Initialize ==="
INIT_FULL=$(curl -s -D - -X POST "$BASE_URL" \
    -H "Content-Type: application/json" \
    -H "Accept: application/json" \
    -H "$AUTH_HEADER" \
    -d "{\"jsonrpc\":\"2.0\",\"id\":$(next_id),\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{},\"clientInfo\":{\"name\":\"fileio-test\",\"version\":\"1.0\"}}}")

SESSION_ID=$(echo "$INIT_FULL" | grep -i "^Mcp-Session-Id:" | tr -d '\r' | awk '{print $2}')
if [ -n "$SESSION_ID" ]; then
    pass "Session created: $SESSION_ID"
else
    fail "No session ID returned"
    exit 1
fi

# Send initialized notification
mcp_post '{"jsonrpc":"2.0","method":"notifications/initialized"}' \
    -H "Mcp-Session-Id: $SESSION_ID" >/dev/null

# ---------------------------------------------------------------------------
# Test 1: write_file — basic file
# ---------------------------------------------------------------------------
echo ""
echo "=== Test 1: write_file basic ==="
WRITE_RESP=$(call_tool "$SESSION_ID" "write_file" '{"path":"helper.til","content":": double dup + ;\n: triple dup dup + + ;\n"}')
if echo "$WRITE_RESP" | grep -q 'bytesWritten'; then
    pass "write_file returns bytesWritten"
else
    fail "write_file response: $WRITE_RESP"
fi

if echo "$WRITE_RESP" | grep -q 'helper.til'; then
    pass "write_file returns path"
else
    fail "write_file missing path: $WRITE_RESP"
fi

# ---------------------------------------------------------------------------
# Test 2: list_files — verify file appears
# ---------------------------------------------------------------------------
echo ""
echo "=== Test 2: list_files root ==="
LIST_RESP=$(call_tool "$SESSION_ID" "list_files" '{}')
LIST_CONTENT=$(extract_content "$LIST_RESP")

if echo "$LIST_CONTENT" | grep -q '"helper.til"'; then
    pass "list_files shows helper.til"
else
    fail "list_files missing helper.til: $LIST_CONTENT"
fi

if echo "$LIST_CONTENT" | grep -q '"file"'; then
    pass "list_files shows type:file"
else
    fail "list_files missing type: $LIST_CONTENT"
fi

# ---------------------------------------------------------------------------
# Test 3: include roundtrip — load file, use definitions
# ---------------------------------------------------------------------------
echo ""
echo "=== Test 3: include roundtrip ==="
INCL_RESP=$(call_tool "$SESSION_ID" "interpret" '{"code":"include helper.til\n7 double"}')
INCL_CONTENT=$(extract_content "$INCL_RESP")

if echo "$INCL_CONTENT" | grep -q '"14"'; then
    pass "include helper.til: 7 double = 14"
else
    fail "include roundtrip (double): $INCL_CONTENT"
fi

TRIPLE_RESP=$(call_tool "$SESSION_ID" "interpret" '{"code":"5 triple"}')
TRIPLE_CONTENT=$(extract_content "$TRIPLE_RESP")

if echo "$TRIPLE_CONTENT" | grep -q '"15"'; then
    pass "include helper.til: 5 triple = 15"
else
    fail "include roundtrip (triple): $TRIPLE_CONTENT"
fi

# ---------------------------------------------------------------------------
# Test 4: Nested includes
# ---------------------------------------------------------------------------
echo ""
echo "=== Test 4: Nested includes ==="

# Reset interpreter for clean state
call_tool "$SESSION_ID" "reset" '{}' >/dev/null

# Write base.til
call_tool "$SESSION_ID" "write_file" '{"path":"base.til","content":": square dup * ;\n"}' >/dev/null

# Write main.til that includes base.til
call_tool "$SESSION_ID" "write_file" '{"path":"main.til","content":"include base.til\n: cube dup square * ;\n"}' >/dev/null

NEST_RESP=$(call_tool "$SESSION_ID" "interpret" '{"code":"include main.til\n3 cube"}')
NEST_CONTENT=$(extract_content "$NEST_RESP")

if echo "$NEST_CONTENT" | grep -q '"27"'; then
    pass "Nested include: 3 cube = 27 (main.til includes base.til)"
else
    fail "Nested include: $NEST_CONTENT"
fi

# ---------------------------------------------------------------------------
# Test 5: write_file with subdirectory (auto-create parent dirs)
# ---------------------------------------------------------------------------
echo ""
echo "=== Test 5: Subdirectory write ==="
SUBDIR_RESP=$(call_tool "$SESSION_ID" "write_file" '{"path":"lib/math.til","content":": add3 + + ;\n"}')

if echo "$SUBDIR_RESP" | grep -q 'lib/math.til'; then
    pass "write_file creates subdirectory"
else
    fail "Subdirectory write: $SUBDIR_RESP"
fi

# ---------------------------------------------------------------------------
# Test 6: list_files with subdirectory path
# ---------------------------------------------------------------------------
echo ""
echo "=== Test 6: list_files subdirectory ==="

# Root listing should show lib as directory
ROOT_LIST=$(call_tool "$SESSION_ID" "list_files" '{}')
ROOT_CONTENT=$(extract_content "$ROOT_LIST")

if echo "$ROOT_CONTENT" | grep -q '"directory"'; then
    pass "list_files shows lib as directory"
else
    fail "Root listing missing directory: $ROOT_CONTENT"
fi

# Subdirectory listing
SUB_LIST=$(call_tool "$SESSION_ID" "list_files" '{"path":"lib"}')
SUB_CONTENT=$(extract_content "$SUB_LIST")

if echo "$SUB_CONTENT" | grep -q '"math.til"'; then
    pass "list_files lib/ shows math.til"
else
    fail "Subdirectory listing: $SUB_CONTENT"
fi

# ---------------------------------------------------------------------------
# Test 7: include from subdirectory
# ---------------------------------------------------------------------------
echo ""
echo "=== Test 7: Include from subdirectory ==="

# Reset for clean state
call_tool "$SESSION_ID" "reset" '{}' >/dev/null

SUBINCL_RESP=$(call_tool "$SESSION_ID" "interpret" '{"code":"include lib/math.til\n1 2 3 add3"}')
SUBINCL_CONTENT=$(extract_content "$SUBINCL_RESP")

if echo "$SUBINCL_CONTENT" | grep -q '"6"'; then
    pass "include lib/math.til: 1 2 3 add3 = 6"
else
    fail "Subdirectory include: $SUBINCL_CONTENT"
fi

# ---------------------------------------------------------------------------
# Test 8: Path traversal rejection (../)
# ---------------------------------------------------------------------------
echo ""
echo "=== Test 8: Path traversal rejection ==="
TRAV_RESP=$(call_tool "$SESSION_ID" "write_file" '{"path":"../escape.til","content":": evil 666 ;"}')

if echo "$TRAV_RESP" | grep -q '"isError".*true\|traversal'; then
    pass "Path traversal ../ rejected"
else
    fail "Path traversal not rejected: $TRAV_RESP"
fi

# ---------------------------------------------------------------------------
# Test 9: Absolute path rejection
# ---------------------------------------------------------------------------
echo ""
echo "=== Test 9: Absolute path rejection ==="
ABS_RESP=$(call_tool "$SESSION_ID" "write_file" '{"path":"/etc/passwd","content":"bad"}')

if echo "$ABS_RESP" | grep -q '"isError".*true\|absolute\|must be relative'; then
    pass "Absolute path rejected"
else
    fail "Absolute path not rejected: $ABS_RESP"
fi

# ---------------------------------------------------------------------------
# Test 10: Empty path rejection
# ---------------------------------------------------------------------------
echo ""
echo "=== Test 10: Empty path rejection ==="
EMPTY_RESP=$(call_tool "$SESSION_ID" "write_file" '{"path":"","content":"bad"}')

if echo "$EMPTY_RESP" | grep -q '"isError".*true\|empty'; then
    pass "Empty path rejected"
else
    fail "Empty path not rejected: $EMPTY_RESP"
fi

# ---------------------------------------------------------------------------
# Test 11: Session isolation — files are per-session
# ---------------------------------------------------------------------------
echo ""
echo "=== Test 11: Session isolation ==="

# Create a second session
INIT2_FULL=$(curl -s -D - -X POST "$BASE_URL" \
    -H "Content-Type: application/json" \
    -H "Accept: application/json" \
    -H "$AUTH_HEADER" \
    -d "{\"jsonrpc\":\"2.0\",\"id\":$(next_id),\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{},\"clientInfo\":{\"name\":\"fileio-test-2\",\"version\":\"1.0\"}}}")

SESSION_ID2=$(echo "$INIT2_FULL" | grep -i "^Mcp-Session-Id:" | tr -d '\r' | awk '{print $2}')

if [ -n "$SESSION_ID2" ]; then
    # Second session should have empty file listing
    ISO_LIST=$(call_tool "$SESSION_ID2" "list_files" '{}')
    ISO_CONTENT=$(extract_content "$ISO_LIST")

    if echo "$ISO_CONTENT" | grep -q '"files":\[\]'; then
        pass "Second session has empty file listing (isolated)"
    else
        fail "Session isolation broken: $ISO_CONTENT"
    fi

    # Second session cannot include files from first session
    ISO_INCL=$(call_tool "$SESSION_ID2" "interpret" '{"code":"include helper.til"}')
    if echo "$ISO_INCL" | grep -q '"isError".*true\|error\|Error'; then
        pass "Second session cannot include first session's files"
    else
        fail "Session isolation for include broken: $ISO_INCL"
    fi
else
    fail "Could not create second session"
fi

# ---------------------------------------------------------------------------
# Test 12: Reset preserves files (home dir persists across reset)
# ---------------------------------------------------------------------------
echo ""
echo "=== Test 12: Reset preserves files ==="

# Write a file, reset, verify file still exists
call_tool "$SESSION_ID" "write_file" '{"path":"persist.til","content":": persist-test 999 ;"}' >/dev/null
call_tool "$SESSION_ID" "reset" '{}' >/dev/null

PERSIST_LIST=$(call_tool "$SESSION_ID" "list_files" '{}')
PERSIST_CONTENT=$(extract_content "$PERSIST_LIST")

if echo "$PERSIST_CONTENT" | grep -q '"persist.til"'; then
    pass "Files persist across reset"
else
    fail "Files lost after reset: $PERSIST_CONTENT"
fi

# Verify the file can be included after reset
PERSIST_INCL=$(call_tool "$SESSION_ID" "interpret" '{"code":"include persist.til\npersist-test"}')
PERSIST_INCL_CONTENT=$(extract_content "$PERSIST_INCL")

if echo "$PERSIST_INCL_CONTENT" | grep -q '"999"'; then
    pass "Include works after reset: persist-test = 999"
else
    fail "Include after reset: $PERSIST_INCL_CONTENT"
fi

# ---------------------------------------------------------------------------
# Test 13: get_session_stats reflects activity
# ---------------------------------------------------------------------------
echo ""
echo "=== Test 13: Session stats ==="
STATS_RESP=$(call_tool "$SESSION_ID" "get_session_stats" '{}')
STATS_CONTENT=$(extract_content "$STATS_RESP")

if echo "$STATS_CONTENT" | grep -q '"interpretCallCount"'; then
    pass "get_session_stats returns interpret call count"
else
    fail "Session stats: $STATS_CONTENT"
fi

if echo "$STATS_CONTENT" | grep -q '"dictionaryConceptCount"'; then
    pass "get_session_stats returns dictionary concept count"
else
    fail "Session stats missing concepts: $STATS_CONTENT"
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
