#!/usr/bin/env python3
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
"""E2E File I/O Stress Tests via HTTP Transport (Python translation).

Exercises sync and async file I/O primitives through the live MCP endpoint.
Direct 1:1 translation of test_file_io_stress.sh — same 56 tests, same
PASS/FAIL output format.

Usage:
    ETIL_TEST_API_KEY="..." python3 tests/docker/test_file_io_stress.py

Configuration (env vars):
    ETIL_TEST_URL       MCP endpoint (default: http://localhost:8080/mcp)
    ETIL_TEST_API_KEY   Bearer token (required)
    ETIL_TEST_TIMEOUT   HTTP timeout in seconds (default: 30)
"""

from __future__ import annotations

import atexit
import json
import os
import re
import sys
import urllib.request
import urllib.error

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

BASE_URL = os.environ.get("ETIL_TEST_URL", "http://localhost:8080/mcp")
API_KEY = os.environ.get("ETIL_TEST_API_KEY", "")
TIMEOUT = int(os.environ.get("ETIL_TEST_TIMEOUT", "30"))

if not API_KEY:
    print("FATAL: ETIL_TEST_API_KEY is required", file=sys.stderr)
    sys.exit(1)

# ---------------------------------------------------------------------------
# Counters
# ---------------------------------------------------------------------------

PASS_COUNT = 0
FAIL_COUNT = 0


def pass_test(msg: str) -> None:
    global PASS_COUNT
    print(f"PASS: {msg}")
    PASS_COUNT += 1


def fail_test(msg: str) -> None:
    global FAIL_COUNT
    print(f"FAIL: {msg}")
    FAIL_COUNT += 1


# ---------------------------------------------------------------------------
# Content generators
# ---------------------------------------------------------------------------

PATTERN = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"


def gen_content(size: int) -> str:
    reps = (size // len(PATTERN)) + 1
    return (PATTERN * reps)[:size]


def gen_content_offset(size: int, offset: int) -> str:
    off = offset % len(PATTERN)
    shifted = PATTERN[off:] + PATTERN[:off]
    reps = (size // len(shifted)) + 1
    return (shifted * reps)[:size]


# ---------------------------------------------------------------------------
# MCP Client
# ---------------------------------------------------------------------------

class McpClient:
    """Encapsulates all MCP HTTP communication (stdlib only)."""

    def __init__(self, url: str, api_key: str, timeout: int) -> None:
        self._url = url
        self._api_key = api_key
        self._timeout = timeout
        self._session_id: str | None = None
        self._req_id = 0

    def _next_id(self) -> int:
        self._req_id += 1
        return self._req_id

    def _post_raw(self, data: bytes, extra_headers: dict[str, str] | None = None) -> tuple[str, dict[str, str]]:
        """POST raw bytes, return (body_text, response_headers)."""
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
            "Authorization": f"Bearer {self._api_key}",
        }
        if self._session_id:
            headers["Mcp-Session-Id"] = self._session_id
        if extra_headers:
            headers.update(extra_headers)

        req = urllib.request.Request(self._url, data=data, headers=headers, method="POST")
        with urllib.request.urlopen(req, timeout=self._timeout) as resp:
            body = resp.read().decode("utf-8", errors="replace")
            # Collect response headers into a simple dict
            resp_headers: dict[str, str] = {}
            for key in resp.headers:
                resp_headers[key.lower()] = resp.headers[key]
            return body, resp_headers

    def _parse_sse(self, body: str) -> str:
        """Extract last SSE data line, or return body as-is."""
        lines = body.split("\n")
        data_lines = [ln for ln in lines if ln.startswith("data: ")]
        if data_lines:
            return data_lines[-1][6:]  # strip "data: " prefix
        return body

    def _post_jsonrpc(self, payload: dict) -> dict:
        """POST JSON-RPC, parse SSE/JSON, return parsed dict."""
        raw_body, _ = self._post_raw(json.dumps(payload).encode("utf-8"))
        text = self._parse_sse(raw_body)
        return json.loads(text)

    def initialize(self) -> str:
        """Initialize MCP session, return session ID."""
        req_id = self._next_id()
        payload = json.dumps({
            "jsonrpc": "2.0",
            "id": req_id,
            "method": "initialize",
            "params": {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "file-io-stress-py", "version": "1.0"},
            },
        }).encode("utf-8")

        # Need to capture headers for session ID
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json",
            "Authorization": f"Bearer {self._api_key}",
        }
        req = urllib.request.Request(self._url, data=payload, headers=headers, method="POST")
        with urllib.request.urlopen(req, timeout=self._timeout) as resp:
            # Extract Mcp-Session-Id
            sid = resp.headers.get("Mcp-Session-Id", "")
            if not sid:
                body = resp.read().decode("utf-8", errors="replace")
                print(f"FATAL: Could not obtain session ID")
                print(body)
                sys.exit(1)
            self._session_id = sid
            # Consume body
            resp.read()

        # Send initialized notification
        notif = json.dumps({
            "jsonrpc": "2.0",
            "method": "notifications/initialized",
        }).encode("utf-8")
        try:
            self._post_raw(notif)
        except Exception:
            pass  # notification, best-effort

        return self._session_id

    def terminate(self) -> None:
        """DELETE session (best-effort cleanup)."""
        if not self._session_id:
            return
        try:
            headers = {
                "Authorization": f"Bearer {self._api_key}",
                "Mcp-Session-Id": self._session_id,
            }
            req = urllib.request.Request(self._url, headers=headers, method="DELETE")
            with urllib.request.urlopen(req, timeout=self._timeout) as resp:
                resp.read()
        except Exception:
            pass

    def interpret(self, code: str) -> dict:
        """Execute TIL code via tools/call interpret."""
        req_id = self._next_id()
        return self._post_jsonrpc({
            "jsonrpc": "2.0",
            "id": req_id,
            "method": "tools/call",
            "params": {
                "name": "interpret",
                "arguments": {"code": code},
            },
        })

    def write_file(self, path: str, content: str) -> dict:
        """Write file via tools/call write_file."""
        req_id = self._next_id()
        return self._post_jsonrpc({
            "jsonrpc": "2.0",
            "id": req_id,
            "method": "tools/call",
            "params": {
                "name": "write_file",
                "arguments": {"path": path, "content": content},
            },
        })

    def read_file(self, path: str) -> dict:
        """Read file via tools/call read_file."""
        req_id = self._next_id()
        return self._post_jsonrpc({
            "jsonrpc": "2.0",
            "id": req_id,
            "method": "tools/call",
            "params": {
                "name": "read_file",
                "arguments": {"path": path},
            },
        })

    def list_files(self) -> dict:
        """List files via tools/call list_files."""
        req_id = self._next_id()
        return self._post_jsonrpc({
            "jsonrpc": "2.0",
            "id": req_id,
            "method": "tools/call",
            "params": {
                "name": "list_files",
                "arguments": {},
            },
        })

    def reset(self) -> dict:
        """Reset interpreter via tools/call reset."""
        req_id = self._next_id()
        return self._post_jsonrpc({
            "jsonrpc": "2.0",
            "id": req_id,
            "method": "tools/call",
            "params": {
                "name": "reset",
                "arguments": {},
            },
        })


# ---------------------------------------------------------------------------
# Response extraction helpers
# ---------------------------------------------------------------------------

def extract_output(resp: dict) -> str:
    """result.content[0].text -> inner JSON -> output field."""
    try:
        text = resp["result"]["content"][0]["text"]
        inner = json.loads(text)
        return inner.get("output", "")
    except Exception:
        return ""


def extract_text(resp: dict) -> str:
    """result.content[0].text (raw)."""
    try:
        return resp["result"]["content"][0]["text"]
    except Exception:
        return ""


def is_error_response(resp: dict) -> bool:
    """Check if the response contains isError: true."""
    try:
        text = resp["result"]["content"][0]["text"]
        inner = json.loads(text)
        return inner.get("isError", False) is True
    except Exception:
        # Also check for top-level isError in result
        try:
            return resp.get("result", {}).get("isError", False) is True
        except Exception:
            return False


def extract_file_content(resp: dict) -> str:
    """Extract content field from read_file response."""
    try:
        text = resp["result"]["content"][0]["text"]
        inner = json.loads(text)
        return inner.get("content", "")
    except Exception:
        return ""


# ---------------------------------------------------------------------------
# TIL helper word definitions
# ---------------------------------------------------------------------------

def define_helpers(client: McpClient) -> None:
    """Define TIL helper words (if/else/then are compile-only)."""
    client.interpret(': read-len-sync read-file-sync if slength . else ." READFAIL" then ;')
    client.interpret(': read-len-async read-file if slength . else ." READFAIL" then ;')
    client.interpret(': verify-prefix-sync read-file-sync if 0 62 substr s= if ." MATCH" else ." MISMATCH" then else drop ." READFAIL" then ;')
    client.interpret(': verify-prefix-async read-file if 0 62 substr s= if ." MATCH" else ." MISMATCH" then else drop ." READFAIL" then ;')


# ---------------------------------------------------------------------------
# Section 1: Power-of-2 sync round-trips (16 tests)
# ---------------------------------------------------------------------------

def section_1_sync_roundtrips(client: McpClient) -> None:
    print("=== Section 1: Power-of-2 Sync Round-Trips ===")

    for p in range(1, 17):
        size = 1 << p
        fname = f"stress_sync_{p}.dat"

        content = gen_content(size)
        write_resp = client.write_file(fname, content)

        if is_error_response(write_resp):
            fail_test(f"sync p={p} ({size} bytes): write_file failed")
            continue

        resp = client.interpret(f's" /home/{fname}" read-len-sync')
        output = extract_output(resp)

        if re.search(rf"^{size}\s*$|^{size} $", output, re.MULTILINE):
            pass_test(f"sync p={p} ({size} bytes): slength = {size}")
        elif "READFAIL" in output:
            fail_test(f"sync p={p} ({size} bytes): read-file-sync returned failure flag")
        else:
            fail_test(f"sync p={p} ({size} bytes): expected slength={size}, got output='{output}'")

    print()


# ---------------------------------------------------------------------------
# Section 2: Power-of-2 async round-trips (16 tests)
# ---------------------------------------------------------------------------

def section_2_async_roundtrips(client: McpClient) -> None:
    print("=== Section 2: Power-of-2 Async Round-Trips ===")

    for p in range(1, 17):
        size = 1 << p
        fname = f"stress_async_{p}.dat"

        content = gen_content(size)
        write_resp = client.write_file(fname, content)

        if is_error_response(write_resp):
            fail_test(f"async p={p} ({size} bytes): write_file failed")
            continue

        resp = client.interpret(f's" /home/{fname}" read-len-async')
        output = extract_output(resp)

        if re.search(rf"^{size}\s*$|^{size} $", output, re.MULTILINE):
            pass_test(f"async p={p} ({size} bytes): slength = {size}")
        elif "READFAIL" in output:
            fail_test(f"async p={p} ({size} bytes): read-file returned failure flag")
        else:
            fail_test(f"async p={p} ({size} bytes): expected slength={size}, got output='{output}'")

    print()


# ---------------------------------------------------------------------------
# Section 3: Interleaved multi-file I/O (4 tests)
# ---------------------------------------------------------------------------

def section_3_interleaved(client: McpClient) -> None:
    print("=== Section 3: Interleaved Multi-File I/O ===")

    interleave_size = 1024
    interleave_count = 10

    # Test 3a: Write 10 files via interpreter (alternating sync/async)
    all_written = True
    for i in range(interleave_count):
        fname = f"stress_interleave_{i}.dat"
        content = gen_content_offset(interleave_size, i)
        word = "write-file-sync" if i % 2 == 0 else "write-file"
        resp = client.interpret(f's" {content}" s" /home/{fname}" {word} drop')
        if is_error_response(resp):
            fail_test(f"interleave write file {i}: {word} failed")
            all_written = False
    if all_written:
        pass_test(f"interleave: wrote {interleave_count} files of {interleave_size} bytes each via interpreter")

    # Test 3b: Read all 10 in REVERSE order via async read-file, verify slength
    all_ok = True
    for i in range(interleave_count - 1, -1, -1):
        fname = f"stress_interleave_{i}.dat"
        resp = client.interpret(f's" /home/{fname}" read-len-async')
        output = extract_output(resp)
        if not re.search(rf"^{interleave_size}\s*$|^{interleave_size} $", output, re.MULTILINE):
            fail_test(f"interleave async read file {i}: expected slength={interleave_size}, got '{output}'")
            all_ok = False
            break
    if all_ok:
        pass_test(f"interleave: all {interleave_count} files read back in reverse, slength={interleave_size}")

    # Test 3c: Verify even-indexed content prefix via sync read
    even_ok = True
    for i in (0, 2, 4, 6, 8):
        prefix = gen_content_offset(62, i)
        fname = f"stress_interleave_{i}.dat"
        resp = client.interpret(f's" {prefix}" s" /home/{fname}" verify-prefix-sync')
        output = extract_output(resp)
        if "MATCH" not in output:
            fail_test(f"interleave sync verify file {i}: expected MATCH, got '{output}'")
            even_ok = False
            break
    if even_ok:
        pass_test("interleave: even-indexed files verified via verify-prefix-sync")

    # Test 3d: Verify odd-indexed content prefix via async read
    odd_ok = True
    for i in (1, 3, 5, 7, 9):
        prefix = gen_content_offset(62, i)
        fname = f"stress_interleave_{i}.dat"
        resp = client.interpret(f's" {prefix}" s" /home/{fname}" verify-prefix-async')
        output = extract_output(resp)
        if "MATCH" not in output:
            fail_test(f"interleave async verify file {i}: expected MATCH, got '{output}'")
            odd_ok = False
            break
    if odd_ok:
        pass_test("interleave: odd-indexed files verified via verify-prefix-async")

    print()


# ---------------------------------------------------------------------------
# Section 4: Byte array boundary stress (8 tests)
# ---------------------------------------------------------------------------

def section_4_byte_boundary(client: McpClient) -> None:
    print("=== Section 4: Byte Array Boundary Stress ===")

    # Reset interpreter state to clear any leaked stack values
    client.reset()
    # Re-define helpers wiped by reset
    define_helpers(client)

    # Test 4.1: string->bytes on 8-char string -> bytes-length = 8
    resp = client.interpret('s" ABCDEFGH" string->bytes bytes-length .')
    output = extract_output(resp)
    if "8" in output:
        pass_test("bytes 4.1: string->bytes 8-char -> bytes-length = 8")
    else:
        fail_test(f"bytes 4.1: expected 8, got '{output}'")

    # Test 4.2: Read 1024-byte file -> string->bytes -> bytes-length = 1024
    resp = client.interpret(': t42 s" /home/stress_sync_10.dat" read-file-sync if string->bytes bytes-length . else ." READFAIL" then ; t42')
    output = extract_output(resp)
    if "1024" in output:
        pass_test("bytes 4.2: 1024-byte file -> string->bytes -> bytes-length = 1024")
    else:
        fail_test(f"bytes 4.2: expected 1024, got '{output}'")

    # Test 4.3: 0 bytes-new bytes-length -> 0
    resp = client.interpret('0 bytes-new bytes-length .')
    output = extract_output(resp)
    if re.search(r"^0\s*$|^0 $", output, re.MULTILINE):
        pass_test("bytes 4.3: 0 bytes-new bytes-length = 0")
    else:
        fail_test(f"bytes 4.3: expected 0, got '{output}'")

    # Test 4.4: out-of-bounds bytes-get -> server survives
    client.interpret('4 bytes-new 10 bytes-get')
    resp = client.interpret('42 .')
    output = extract_output(resp)
    if "42" in output:
        pass_test("bytes 4.4: out-of-bounds bytes-get -> server survived")
    else:
        fail_test("bytes 4.4: server unresponsive after out-of-bounds access")

    # Test 4.5: 8 bytes-new -> 4 bytes-resize -> bytes-length = 4
    resp = client.interpret('8 bytes-new 4 bytes-resize bytes-length .')
    output = extract_output(resp)
    if re.search(r"^4\s*$|^4 $", output, re.MULTILINE):
        pass_test("bytes 4.5: 8 bytes-new -> 4 bytes-resize -> bytes-length = 4")
    else:
        fail_test(f"bytes 4.5: expected 4, got '{output}'")

    # Test 4.6: access shrunk index 7 -> server survives
    client.interpret('8 bytes-new 4 bytes-resize 7 bytes-get')
    resp = client.interpret('99 .')
    output = extract_output(resp)
    if "99" in output:
        pass_test("bytes 4.6: access shrunk index 7 -> server survived")
    else:
        fail_test("bytes 4.6: server unresponsive after shrunk bounds access")

    # Test 4.7: 65536 bytes-new bytes-length -> 65536
    resp = client.interpret('65536 bytes-new bytes-length .')
    output = extract_output(resp)
    if "65536" in output:
        pass_test("bytes 4.7: 65536 bytes-new bytes-length = 65536")
    else:
        fail_test(f"bytes 4.7: expected 65536, got '{output}'")

    # Test 4.8: empty string -> string->bytes -> bytes-length = 0
    resp = client.interpret('s" " string->bytes bytes-length .')
    output = extract_output(resp)
    if re.search(r"^0\s*$|^0 $", output, re.MULTILINE):
        pass_test("bytes 4.8: empty string -> string->bytes -> bytes-length = 0")
    else:
        fail_test(f"bytes 4.8: expected 0, got '{output}'")

    print()


# ---------------------------------------------------------------------------
# Section 5: Append and truncate stress (8 tests)
# ---------------------------------------------------------------------------

def section_5_append_truncate(client: McpClient) -> None:
    print("=== Section 5: Append and Truncate Stress ===")

    # Test 5.1: Sync write+append -> slength = 10
    client.write_file("stress_append_sync.dat", "Hello")
    resp = client.interpret('s" World" s" /home/stress_append_sync.dat" append-file-sync drop s" /home/stress_append_sync.dat" read-len-sync')
    output = extract_output(resp)
    if re.search(r"^10\s*$|^10 $", output, re.MULTILINE):
        pass_test("append 5.1: sync write+append -> slength = 10")
    else:
        fail_test(f"append 5.1: expected 10, got '{output}'")

    # Test 5.2: Async write+append -> slength = 10
    client.write_file("stress_append_async.dat", "Hello")
    resp = client.interpret('s" World" s" /home/stress_append_async.dat" append-file drop s" /home/stress_append_async.dat" read-len-async')
    output = extract_output(resp)
    if re.search(r"^10\s*$|^10 $", output, re.MULTILINE):
        pass_test("append 5.2: async write+append -> slength = 10")
    else:
        fail_test(f"append 5.2: expected 10, got '{output}'")

    # Test 5.3: Sync truncate -> slength = 0
    resp = client.interpret('s" /home/stress_append_sync.dat" truncate-sync drop s" /home/stress_append_sync.dat" read-len-sync')
    output = extract_output(resp)
    if re.search(r"^0\s*$|^0 $", output, re.MULTILINE):
        pass_test("truncate 5.3: sync truncate -> slength = 0")
    else:
        fail_test(f"truncate 5.3: expected 0, got '{output}'")

    # Test 5.4: Async truncate -> slength = 0
    resp = client.interpret('s" /home/stress_append_async.dat" truncate drop s" /home/stress_append_async.dat" read-len-async')
    output = extract_output(resp)
    if re.search(r"^0\s*$|^0 $", output, re.MULTILINE):
        pass_test("truncate 5.4: async truncate -> slength = 0")
    else:
        fail_test(f"truncate 5.4: expected 0, got '{output}'")

    # Test 5.5: 100x sync append loop -> slength = 100
    client.write_file("stress_append_loop.dat", "")
    resp = client.interpret(': t55 100 0 do s" X" s" /home/stress_append_loop.dat" append-file-sync drop loop ; t55 s" /home/stress_append_loop.dat" read-len-sync')
    output = extract_output(resp)
    if re.search(r"^100\s*$|^100 $", output, re.MULTILINE):
        pass_test("append 5.5: 100x sync append -> slength = 100")
    else:
        fail_test(f"append 5.5: expected 100, got '{output}'")

    # Test 5.6: exists-sync -> flag = -1
    resp = client.interpret('s" /home/stress_append_loop.dat" exists-sync .')
    output = extract_output(resp)
    if "-1" in output:
        pass_test("exists 5.6: exists-sync -> flag = -1")
    else:
        fail_test(f"exists 5.6: expected -1, got '{output}'")

    # Test 5.7: lstat-sync -> size = 100
    resp = client.interpret(': t57 s" /home/stress_append_loop.dat" lstat-sync if 0 array-get . then ; t57')
    output = extract_output(resp)
    if re.search(r"^100\s*$|^100 $", output, re.MULTILINE):
        pass_test("lstat 5.7: lstat-sync size = 100")
    else:
        fail_test(f"lstat 5.7: expected 100, got '{output}'")

    # Test 5.8: rm-sync + exists-sync -> flag = 0
    resp = client.interpret('s" /home/stress_append_loop.dat" rm-sync drop s" /home/stress_append_loop.dat" exists-sync .')
    output = extract_output(resp)
    if re.search(r"^0\s*$|^0 $", output, re.MULTILINE):
        pass_test("rm 5.8: rm-sync + exists-sync -> flag = 0")
    else:
        fail_test(f"rm 5.8: expected 0, got '{output}'")

    print()


# ---------------------------------------------------------------------------
# Section 6: Cleanup (4 tests)
# ---------------------------------------------------------------------------

def section_6_cleanup(client: McpClient) -> None:
    print("=== Section 6: Cleanup ===")

    # Test 6.1: Delete all sync test files
    for p in range(1, 17):
        client.interpret(f's" /home/stress_sync_{p}.dat" rm-sync drop')
    resp = client.interpret('s" /home/stress_sync_1.dat" exists-sync .')
    output = extract_output(resp)
    if re.search(r"^0\s*$|^0 $", output, re.MULTILINE):
        pass_test("cleanup 6.1: sync test files deleted")
    else:
        fail_test("cleanup 6.1: sync files still exist after rm-sync")

    # Test 6.2: Delete all async test files
    for p in range(1, 17):
        client.interpret(f's" /home/stress_async_{p}.dat" rm-sync drop')
    resp = client.interpret('s" /home/stress_async_1.dat" exists-sync .')
    output = extract_output(resp)
    if re.search(r"^0\s*$|^0 $", output, re.MULTILINE):
        pass_test("cleanup 6.2: async test files deleted")
    else:
        fail_test("cleanup 6.2: async files still exist after rm-sync")

    # Test 6.3: Delete interleaved + append test files
    for i in range(10):
        client.interpret(f's" /home/stress_interleave_{i}.dat" rm-sync drop')
    for fname in ("stress_append_sync.dat", "stress_append_async.dat", "stress_append_loop.dat"):
        try:
            client.interpret(f's" /home/{fname}" rm-sync drop')
        except Exception:
            pass
    resp = client.interpret('s" /home/stress_interleave_0.dat" exists-sync .')
    output = extract_output(resp)
    if re.search(r"^0\s*$|^0 $", output, re.MULTILINE):
        pass_test("cleanup 6.3: interleaved + append test files deleted")
    else:
        fail_test("cleanup 6.3: interleaved files still exist")

    # Test 6.4: Verify session home is clean via MCP list_files
    list_resp = client.list_files()
    list_text = extract_text(list_resp)
    try:
        data = json.loads(list_text)
        files = data.get("files", [])
        file_count = len(files)
    except Exception:
        file_count = "unknown"
    if file_count == 0:
        pass_test("cleanup 6.4: session home is clean (0 files)")
    else:
        fail_test(f"cleanup 6.4: session home has {file_count} remaining files")

    print()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    print("=== E2E File I/O Stress Tests (Python) ===")
    print(f"URL: {BASE_URL}")
    print(f"Timeout: {TIMEOUT}s")
    print()

    # Initialize session
    print("=== Initialize ===")
    client = McpClient(BASE_URL, API_KEY, TIMEOUT)
    session_id = client.initialize()
    print(f"Session: {session_id}")
    print()

    # Register cleanup on exit
    atexit.register(lambda: _cleanup(client, session_id))

    # Define TIL helper words
    define_helpers(client)

    # Run all sections
    section_1_sync_roundtrips(client)
    section_2_async_roundtrips(client)
    section_3_interleaved(client)
    section_4_byte_boundary(client)
    section_5_append_truncate(client)
    section_6_cleanup(client)

    # Summary
    print("=== Results ===")
    print(f"PASS: {PASS_COUNT}")
    print(f"FAIL: {FAIL_COUNT}")

    if FAIL_COUNT > 0:
        sys.exit(1)


def _cleanup(client: McpClient, session_id: str) -> None:
    print()
    print("=== Cleanup ===")
    client.terminate()
    print(f"Session {session_id} terminated")


if __name__ == "__main__":
    main()
