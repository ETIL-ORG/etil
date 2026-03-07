#!/usr/bin/env bash
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
# E2E File I/O Stress Tests via HTTP Transport
# Exercises sync and async file I/O primitives through the live MCP endpoint.
# No Docker build — connects to an existing running server.
#
# Usage:
#   ETIL_TEST_API_KEY="..." bash tests/docker/test_file_io_stress.sh
#
# Configuration (env vars):
#   ETIL_TEST_URL       MCP endpoint (default: http://localhost:8080/mcp)
#   ETIL_TEST_API_KEY   Bearer token (required)
#   ETIL_TEST_TIMEOUT   curl --max-time seconds (default: 30)
set -uo pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
BASE_URL="${ETIL_TEST_URL:-http://localhost:8080/mcp}"
API_KEY="${ETIL_TEST_API_KEY:?ETIL_TEST_API_KEY is required}"
TIMEOUT="${ETIL_TEST_TIMEOUT:-30}"
AUTH_HEADER="Authorization: Bearer ${API_KEY}"

PASS_COUNT=0
FAIL_COUNT=0
REQ_ID=0
SESSION_ID=""

# ---------------------------------------------------------------------------
# Helpers: counters
# ---------------------------------------------------------------------------
pass() {
    echo "PASS: $1"
    PASS_COUNT=$((PASS_COUNT + 1))
}

fail() {
    echo "FAIL: $1"
    FAIL_COUNT=$((FAIL_COUNT + 1))
}

next_id() {
    REQ_ID=$((REQ_ID + 1))
    echo "$REQ_ID"
}

# ---------------------------------------------------------------------------
# Helpers: HTTP / SSE
# ---------------------------------------------------------------------------

# POST JSON-RPC, handle both plain JSON and SSE responses.
# Returns the final JSON-RPC response object (last SSE data line or plain body).
mcp_post_sse() {
    local data="$1"
    shift
    local raw
    raw=$(curl -s --max-time "$TIMEOUT" -X POST "$BASE_URL" \
        -H "Content-Type: application/json" \
        -H "Accept: application/json, text/event-stream" \
        -H "$AUTH_HEADER" \
        "$@" \
        -d "$data")
    # If the response contains SSE data: lines, extract the last one
    if echo "$raw" | grep -q '^data: '; then
        echo "$raw" | grep '^data: ' | tail -1 | sed 's/^data: //'
    else
        echo "$raw"
    fi
}

# Initialize session, extract Mcp-Session-Id from headers
init_session() {
    local id
    id=$(next_id)
    local full
    full=$(curl -s --max-time "$TIMEOUT" -D - -X POST "$BASE_URL" \
        -H "Content-Type: application/json" \
        -H "Accept: application/json" \
        -H "$AUTH_HEADER" \
        -d "{\"jsonrpc\":\"2.0\",\"id\":$id,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{},\"clientInfo\":{\"name\":\"file-io-stress\",\"version\":\"1.0\"}}}")
    SESSION_ID=$(echo "$full" | grep -i "^Mcp-Session-Id:" | tr -d '\r' | awk '{print $2}')
    if [ -z "$SESSION_ID" ]; then
        echo "FATAL: Could not obtain session ID"
        echo "$full"
        exit 1
    fi
    # Send initialized notification
    curl -s --max-time "$TIMEOUT" -X POST "$BASE_URL" \
        -H "Content-Type: application/json" \
        -H "$AUTH_HEADER" \
        -H "Mcp-Session-Id: $SESSION_ID" \
        -d '{"jsonrpc":"2.0","method":"notifications/initialized"}' >/dev/null
}

# Cleanup: DELETE session
cleanup() {
    echo ""
    echo "=== Cleanup ==="
    if [ -n "$SESSION_ID" ]; then
        curl -s --max-time "$TIMEOUT" -X DELETE "$BASE_URL" \
            -H "$AUTH_HEADER" \
            -H "Mcp-Session-Id: $SESSION_ID" >/dev/null 2>&1 || true
        echo "Session $SESSION_ID terminated"
    fi
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Helpers: MCP tool calls
# ---------------------------------------------------------------------------

# Send TIL code via interpret tool, return full JSON-RPC response
interpret() {
    local code="$1"
    local id
    id=$(next_id)
    local payload
    payload=$(python3 -c "
import json, sys
code = sys.argv[1]
print(json.dumps({
    'jsonrpc': '2.0',
    'id': int(sys.argv[2]),
    'method': 'tools/call',
    'params': {
        'name': 'interpret',
        'arguments': {'code': code}
    }
}))
" "$code" "$id")
    mcp_post_sse "$payload" -H "Mcp-Session-Id: $SESSION_ID"
}

# Extract output string from interpret response (robust: returns "" on any parse error)
# result.content[0].text -> inner JSON -> output field
extract_output() {
    local resp="$1"
    echo "$resp" | python3 -c "
import sys, json
try:
    r = json.load(sys.stdin)
    text = r['result']['content'][0]['text']
    inner = json.loads(text)
    print(inner.get('output', ''), end='')
except Exception:
    pass
" 2>/dev/null
}

# Extract the raw text field from a tool response (result.content[0].text)
extract_text() {
    local resp="$1"
    echo "$resp" | python3 -c "
import sys, json
try:
    r = json.load(sys.stdin)
    print(r['result']['content'][0]['text'], end='')
except Exception:
    pass
" 2>/dev/null
}

# Generate repeating alphanumeric content of exact size N
gen_content() {
    local size="$1"
    python3 -c "
import sys
n = int(sys.argv[1])
pattern = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789'
out = (pattern * ((n // len(pattern)) + 1))[:n]
print(out, end='')
" "$size"
}

# Generate content with offset pattern start for uniqueness
gen_content_offset() {
    local size="$1"
    local offset="$2"
    python3 -c "
import sys
n = int(sys.argv[1])
off = int(sys.argv[2])
pattern = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789'
shifted = pattern[off % len(pattern):] + pattern[:off % len(pattern)]
out = (shifted * ((n // len(shifted)) + 1))[:n]
print(out, end='')
" "$size" "$offset"
}

# Build and send write_file JSON-RPC request
write_file_request() {
    local path="$1"
    local content="$2"
    local id
    id=$(next_id)
    local payload
    payload=$(python3 -c "
import json, sys
print(json.dumps({
    'jsonrpc': '2.0',
    'id': int(sys.argv[1]),
    'method': 'tools/call',
    'params': {
        'name': 'write_file',
        'arguments': {'path': sys.argv[2], 'content': sys.argv[3]}
    }
}))
" "$id" "$path" "$content")
    mcp_post_sse "$payload" -H "Mcp-Session-Id: $SESSION_ID"
}

# Build and send read_file JSON-RPC request
read_file_request() {
    local path="$1"
    local id
    id=$(next_id)
    local payload
    payload=$(python3 -c "
import json, sys
print(json.dumps({
    'jsonrpc': '2.0',
    'id': int(sys.argv[1]),
    'method': 'tools/call',
    'params': {
        'name': 'read_file',
        'arguments': {'path': sys.argv[2]}
    }
}))
" "$id" "$path")
    mcp_post_sse "$payload" -H "Mcp-Session-Id: $SESSION_ID"
}

# Build and send list_files JSON-RPC request
list_files_request() {
    local id
    id=$(next_id)
    mcp_post_sse "{\"jsonrpc\":\"2.0\",\"id\":$id,\"method\":\"tools/call\",\"params\":{\"name\":\"list_files\",\"arguments\":{}}}" \
        -H "Mcp-Session-Id: $SESSION_ID"
}

# Build and send reset JSON-RPC request
reset_request() {
    local id
    id=$(next_id)
    mcp_post_sse "{\"jsonrpc\":\"2.0\",\"id\":$id,\"method\":\"tools/call\",\"params\":{\"name\":\"reset\",\"arguments\":{}}}" \
        -H "Mcp-Session-Id: $SESSION_ID"
}

# ===========================================================================
echo "=== E2E File I/O Stress Tests ==="
echo "URL: $BASE_URL"
echo "Timeout: ${TIMEOUT}s"
echo ""

# ---------------------------------------------------------------------------
# Initialize session
# ---------------------------------------------------------------------------
echo "=== Initialize ==="
init_session
echo "Session: $SESSION_ID"
echo ""

# ---------------------------------------------------------------------------
# Define TIL helper words (if/else/then are compile-only in TIL)
# ---------------------------------------------------------------------------
interpret ': read-len-sync read-file-sync if slength . else ." READFAIL" then ;' >/dev/null
interpret ': read-len-async read-file if slength . else ." READFAIL" then ;' >/dev/null
interpret ': verify-prefix-sync read-file-sync if 0 62 substr s= if ." MATCH" else ." MISMATCH" then else drop ." READFAIL" then ;' >/dev/null
interpret ': verify-prefix-async read-file if 0 62 substr s= if ." MATCH" else ." MISMATCH" then else drop ." READFAIL" then ;' >/dev/null

# ===========================================================================
# Section 1: Power-of-2 sync round-trips (16 tests)
# ===========================================================================
echo "=== Section 1: Power-of-2 Sync Round-Trips ==="

for p in $(seq 1 16); do
    size=$((1 << p))
    fname="stress_sync_${p}.dat"

    # Generate content and write via MCP write_file
    content=$(gen_content "$size")
    write_resp=$(write_file_request "$fname" "$content")

    if echo "$write_resp" | grep -q '"isError".*true'; then
        fail "sync p=$p ($size bytes): write_file failed"
        continue
    fi

    # Read back via TIL read-file-sync, verify slength
    resp=$(interpret "s\" /home/${fname}\" read-len-sync")
    output=$(extract_output "$resp")

    if echo "$output" | grep -q "^${size} \$\|^${size}\$"; then
        pass "sync p=$p ($size bytes): slength = $size"
    elif echo "$output" | grep -q "READFAIL"; then
        fail "sync p=$p ($size bytes): read-file-sync returned failure flag"
    else
        fail "sync p=$p ($size bytes): expected slength=$size, got output='$output'"
    fi
done
echo ""

# ===========================================================================
# Section 2: Power-of-2 async round-trips (16 tests)
# ===========================================================================
echo "=== Section 2: Power-of-2 Async Round-Trips ==="

for p in $(seq 1 16); do
    size=$((1 << p))
    fname="stress_async_${p}.dat"

    # Generate content and write via MCP write_file
    content=$(gen_content "$size")
    write_resp=$(write_file_request "$fname" "$content")

    if echo "$write_resp" | grep -q '"isError".*true'; then
        fail "async p=$p ($size bytes): write_file failed"
        continue
    fi

    # Read back via TIL read-file (async), verify slength
    resp=$(interpret "s\" /home/${fname}\" read-len-async")
    output=$(extract_output "$resp")

    if echo "$output" | grep -q "^${size} \$\|^${size}\$"; then
        pass "async p=$p ($size bytes): slength = $size"
    elif echo "$output" | grep -q "READFAIL"; then
        fail "async p=$p ($size bytes): read-file returned failure flag"
    else
        fail "async p=$p ($size bytes): expected slength=$size, got output='$output'"
    fi
done
echo ""

# ===========================================================================
# Section 3: Interleaved multi-file I/O (~4 tests)
# ===========================================================================
echo "=== Section 3: Interleaved Multi-File I/O ==="

INTERLEAVE_SIZE=1024
INTERLEAVE_COUNT=10

# Test 3a: Write 10 files via interpreter (alternating sync/async)
all_written=true
for i in $(seq 0 $((INTERLEAVE_COUNT - 1))); do
    fname="stress_interleave_${i}.dat"
    content=$(gen_content_offset "$INTERLEAVE_SIZE" "$i")
    if [ $((i % 2)) -eq 0 ]; then
        word="write-file-sync"
    else
        word="write-file"
    fi
    resp=$(interpret "s\" ${content}\" s\" /home/${fname}\" ${word} drop")
    if echo "$resp" | grep -q '"isError".*true'; then
        fail "interleave write file $i: ${word} failed"
        all_written=false
    fi
done
if [ "$all_written" = true ]; then
    pass "interleave: wrote $INTERLEAVE_COUNT files of $INTERLEAVE_SIZE bytes each via interpreter"
fi

# Test 3b: Read all 10 in REVERSE order via async read-file, verify slength
all_ok=true
for i in $(seq $((INTERLEAVE_COUNT - 1)) -1 0); do
    fname="stress_interleave_${i}.dat"
    resp=$(interpret "s\" /home/${fname}\" read-len-async")
    output=$(extract_output "$resp")
    if ! echo "$output" | grep -q "^${INTERLEAVE_SIZE} \$\|^${INTERLEAVE_SIZE}\$"; then
        fail "interleave async read file $i: expected slength=$INTERLEAVE_SIZE, got '$output'"
        all_ok=false
        break
    fi
done
if [ "$all_ok" = true ]; then
    pass "interleave: all $INTERLEAVE_COUNT files read back in reverse, slength=$INTERLEAVE_SIZE"
fi

# Test 3c: Verify even-indexed content prefix via sync read
even_ok=true
for i in 0 2 4 6 8; do
    fname="stress_interleave_${i}.dat"
    prefix=$(gen_content_offset 62 "$i")
    resp=$(interpret "s\" ${prefix}\" s\" /home/${fname}\" verify-prefix-sync")
    output=$(extract_output "$resp")
    if ! echo "$output" | grep -q "MATCH"; then
        fail "interleave sync verify file $i: expected MATCH, got '$output'"
        even_ok=false
        break
    fi
done
if [ "$even_ok" = true ]; then
    pass "interleave: even-indexed files verified via verify-prefix-sync"
fi

# Test 3d: Verify odd-indexed content prefix via async read
odd_ok=true
for i in 1 3 5 7 9; do
    fname="stress_interleave_${i}.dat"
    prefix=$(gen_content_offset 62 "$i")
    resp=$(interpret "s\" ${prefix}\" s\" /home/${fname}\" verify-prefix-async")
    output=$(extract_output "$resp")
    if ! echo "$output" | grep -q "MATCH"; then
        fail "interleave async verify file $i: expected MATCH, got '$output'"
        odd_ok=false
        break
    fi
done
if [ "$odd_ok" = true ]; then
    pass "interleave: odd-indexed files verified via verify-prefix-async"
fi
echo ""

# ===========================================================================
# Section 4: Byte array boundary stress (8 tests)
# ===========================================================================
echo "=== Section 4: Byte Array Boundary Stress ==="

# Reset interpreter state to clear any leaked stack values from prior sections
reset_request >/dev/null
# Re-define helpers wiped by reset (if/else/then are compile-only)
interpret ': read-len-sync read-file-sync if slength . else ." READFAIL" then ;' >/dev/null
interpret ': read-len-async read-file if slength . else ." READFAIL" then ;' >/dev/null

# Test 4.1: string->bytes on 8-char string -> bytes-length = 8
resp=$(interpret 's" ABCDEFGH" string->bytes bytes-length .')
output=$(extract_output "$resp")
if echo "$output" | grep -q "8"; then
    pass "bytes 4.1: string->bytes 8-char -> bytes-length = 8"
else
    fail "bytes 4.1: expected 8, got '$output'"
fi

# Test 4.2: Read 1024-byte file -> string->bytes -> bytes-length = 1024
# Use stress_sync_10.dat = 2^10 = 1024 bytes
resp=$(interpret ': t42 s" /home/stress_sync_10.dat" read-file-sync if string->bytes bytes-length . else ." READFAIL" then ; t42')
output=$(extract_output "$resp")
if echo "$output" | grep -q "1024"; then
    pass "bytes 4.2: 1024-byte file -> string->bytes -> bytes-length = 1024"
else
    fail "bytes 4.2: expected 1024, got '$output'"
fi

# Test 4.3: 0 bytes-new bytes-length -> 0 (zero-size allocation)
resp=$(interpret '0 bytes-new bytes-length .')
output=$(extract_output "$resp")
if echo "$output" | grep -q "^0 \$\|^0\$"; then
    pass "bytes 4.3: 0 bytes-new bytes-length = 0"
else
    fail "bytes 4.3: expected 0, got '$output'"
fi

# Test 4.4: 4 bytes-new 10 bytes-get -> graceful failure (out-of-bounds, server survives)
interpret '4 bytes-new 10 bytes-get' >/dev/null
resp=$(interpret '42 .')
output=$(extract_output "$resp")
if echo "$output" | grep -q "42"; then
    pass "bytes 4.4: out-of-bounds bytes-get -> server survived"
else
    fail "bytes 4.4: server unresponsive after out-of-bounds access"
fi

# Test 4.5: 8 bytes-new ... 4 bytes-resize bytes-length -> 4 (shrink)
resp=$(interpret '8 bytes-new 4 bytes-resize bytes-length .')
output=$(extract_output "$resp")
if echo "$output" | grep -q "^4 \$\|^4\$"; then
    pass "bytes 4.5: 8 bytes-new -> 4 bytes-resize -> bytes-length = 4"
else
    fail "bytes 4.5: expected 4, got '$output'"
fi

# Test 4.6: After shrink, access old index 7 -> graceful failure (server survives)
interpret '8 bytes-new 4 bytes-resize 7 bytes-get' >/dev/null
resp=$(interpret '99 .')
output=$(extract_output "$resp")
if echo "$output" | grep -q "99"; then
    pass "bytes 4.6: access shrunk index 7 -> server survived"
else
    fail "bytes 4.6: server unresponsive after shrunk bounds access"
fi

# Test 4.7: 65536 bytes-new bytes-length -> 65536 (large allocation)
resp=$(interpret '65536 bytes-new bytes-length .')
output=$(extract_output "$resp")
if echo "$output" | grep -q "65536"; then
    pass "bytes 4.7: 65536 bytes-new bytes-length = 65536"
else
    fail "bytes 4.7: expected 65536, got '$output'"
fi

# Test 4.8: s" " string->bytes bytes-length -> 0 (empty string)
resp=$(interpret 's" " string->bytes bytes-length .')
output=$(extract_output "$resp")
if echo "$output" | grep -q "^0 \$\|^0\$"; then
    pass "bytes 4.8: empty string -> string->bytes -> bytes-length = 0"
else
    fail "bytes 4.8: expected 0, got '$output'"
fi
echo ""

# ===========================================================================
# Section 5: Append and truncate stress (8 tests)
# ===========================================================================
echo "=== Section 5: Append and Truncate Stress ==="

# Test 5.1: Sync: write "Hello" + append "World" -> read back, slength = 10
write_file_request "stress_append_sync.dat" "Hello" >/dev/null
resp=$(interpret 's" World" s" /home/stress_append_sync.dat" append-file-sync drop s" /home/stress_append_sync.dat" read-len-sync')
output=$(extract_output "$resp")
if echo "$output" | grep -q "^10 \$\|^10\$"; then
    pass "append 5.1: sync write+append -> slength = 10"
else
    fail "append 5.1: expected 10, got '$output'"
fi

# Test 5.2: Async: write "Hello" + append "World" -> read back, slength = 10
write_file_request "stress_append_async.dat" "Hello" >/dev/null
resp=$(interpret 's" World" s" /home/stress_append_async.dat" append-file drop s" /home/stress_append_async.dat" read-len-async')
output=$(extract_output "$resp")
if echo "$output" | grep -q "^10 \$\|^10\$"; then
    pass "append 5.2: async write+append -> slength = 10"
else
    fail "append 5.2: expected 10, got '$output'"
fi

# Test 5.3: Sync truncate -> read back, slength = 0
resp=$(interpret 's" /home/stress_append_sync.dat" truncate-sync drop s" /home/stress_append_sync.dat" read-len-sync')
output=$(extract_output "$resp")
if echo "$output" | grep -q "^0 \$\|^0\$"; then
    pass "truncate 5.3: sync truncate -> slength = 0"
else
    fail "truncate 5.3: expected 0, got '$output'"
fi

# Test 5.4: Async truncate -> read back, slength = 0
resp=$(interpret 's" /home/stress_append_async.dat" truncate drop s" /home/stress_append_async.dat" read-len-async')
output=$(extract_output "$resp")
if echo "$output" | grep -q "^0 \$\|^0\$"; then
    pass "truncate 5.4: async truncate -> slength = 0"
else
    fail "truncate 5.4: expected 0, got '$output'"
fi

# Test 5.5: 100x sync append loop -> slength = 100
write_file_request "stress_append_loop.dat" "" >/dev/null
resp=$(interpret ': t55 100 0 do s" X" s" /home/stress_append_loop.dat" append-file-sync drop loop ; t55 s" /home/stress_append_loop.dat" read-len-sync')
output=$(extract_output "$resp")
if echo "$output" | grep -q "^100 \$\|^100\$"; then
    pass "append 5.5: 100x sync append -> slength = 100"
else
    fail "append 5.5: expected 100, got '$output'"
fi

# Test 5.6: exists-sync confirms file exists -> flag = -1
resp=$(interpret 's" /home/stress_append_loop.dat" exists-sync .')
output=$(extract_output "$resp")
if echo "$output" | grep -q "\-1"; then
    pass "exists 5.6: exists-sync -> flag = -1"
else
    fail "exists 5.6: expected -1, got '$output'"
fi

# Test 5.7: lstat-sync -> size field = 100
# lstat-sync returns ( path -- array? flag ), need colon def for if/then
resp=$(interpret ': t57 s" /home/stress_append_loop.dat" lstat-sync if 0 array-get . then ; t57')
output=$(extract_output "$resp")
if echo "$output" | grep -q "^100 \$\|^100\$"; then
    pass "lstat 5.7: lstat-sync size = 100"
else
    fail "lstat 5.7: expected 100, got '$output'"
fi

# Test 5.8: rm-sync + exists-sync -> flag = 0
resp=$(interpret 's" /home/stress_append_loop.dat" rm-sync drop s" /home/stress_append_loop.dat" exists-sync .')
output=$(extract_output "$resp")
if echo "$output" | grep -q "^0 \$\|^0\$"; then
    pass "rm 5.8: rm-sync + exists-sync -> flag = 0"
else
    fail "rm 5.8: expected 0, got '$output'"
fi
echo ""

# ===========================================================================
# Section 6: Cleanup (4 tests)
# ===========================================================================
echo "=== Section 6: Cleanup ==="

# Test 6.1: Delete all sync test files
for p in $(seq 1 16); do
    interpret "s\" /home/stress_sync_${p}.dat\" rm-sync drop" >/dev/null
done
resp=$(interpret 's" /home/stress_sync_1.dat" exists-sync .')
output=$(extract_output "$resp")
if echo "$output" | grep -q "^0 \$\|^0\$"; then
    pass "cleanup 6.1: sync test files deleted"
else
    fail "cleanup 6.1: sync files still exist after rm-sync"
fi

# Test 6.2: Delete all async test files
for p in $(seq 1 16); do
    interpret "s\" /home/stress_async_${p}.dat\" rm-sync drop" >/dev/null
done
resp=$(interpret 's" /home/stress_async_1.dat" exists-sync .')
output=$(extract_output "$resp")
if echo "$output" | grep -q "^0 \$\|^0\$"; then
    pass "cleanup 6.2: async test files deleted"
else
    fail "cleanup 6.2: async files still exist after rm-sync"
fi

# Test 6.3: Delete interleaved + append test files
for i in $(seq 0 $((INTERLEAVE_COUNT - 1))); do
    interpret "s\" /home/stress_interleave_${i}.dat\" rm-sync drop" >/dev/null
done
for fname in stress_append_sync.dat stress_append_async.dat stress_append_loop.dat; do
    interpret "s\" /home/${fname}\" rm-sync drop" >/dev/null 2>&1
done
resp=$(interpret 's" /home/stress_interleave_0.dat" exists-sync .')
output=$(extract_output "$resp")
if echo "$output" | grep -q "^0 \$\|^0\$"; then
    pass "cleanup 6.3: interleaved + append test files deleted"
else
    fail "cleanup 6.3: interleaved files still exist"
fi

# Test 6.4: Verify session home is clean via MCP list_files
list_resp=$(list_files_request)
list_text=$(extract_text "$list_resp")
file_count=$(echo "$list_text" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    files = data.get('files', [])
    print(len(files))
except Exception:
    print('unknown')
" 2>/dev/null)
if [ "$file_count" = "0" ]; then
    pass "cleanup 6.4: session home is clean (0 files)"
else
    fail "cleanup 6.4: session home has $file_count remaining files"
fi
echo ""

# ===========================================================================
# Summary
# ===========================================================================
echo "=== Results ==="
echo "PASS: $PASS_COUNT"
echo "FAIL: $FAIL_COUNT"

if [ "$FAIL_COUNT" -gt 0 ]; then
    exit 1
fi
