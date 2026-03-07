#!/usr/bin/env python3
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
"""Parallel Randomized File I/O Fuzz Tests via HTTP Transport.

Hammers the MCP server with N concurrent sessions, each running the same 56
tests from test_file_io_stress.py in randomized section order, with requests
from different sessions interleaved on the wire.

Usage:
    ETIL_TEST_API_KEY="..." python3 tests/docker/test_file_io_fuzz.py

Configuration (env vars):
    ETIL_TEST_URL       MCP endpoint (default: http://localhost:8080/mcp)
    ETIL_TEST_API_KEY   Bearer token (required)
    ETIL_TEST_TIMEOUT   HTTP timeout in seconds (default: 30)
    ETIL_TEST_WORKERS   Concurrent workers (default: 4)
    ETIL_TEST_SEED      RNG seed for reproducibility (default: random)
"""

from __future__ import annotations

import dataclasses
import http.client
import json
import os
import random
import re
import sys
import time
import urllib.error
from concurrent.futures import ThreadPoolExecutor, as_completed

# ---------------------------------------------------------------------------
# Import reusable pieces from the sequential stress test
# ---------------------------------------------------------------------------

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_file_io_stress import (
    McpClient,
    PATTERN,
    gen_content,
    gen_content_offset,
    extract_output,
    extract_text,
    extract_file_content,
    is_error_response,
)


# ---------------------------------------------------------------------------
# Resilient MCP client — retries on transient HTTP errors
# ---------------------------------------------------------------------------

MAX_RETRIES = 3
RETRY_DELAY = 1.0  # seconds


class ResilientMcpClient(McpClient):
    """McpClient subclass that retries on transient HTTP errors."""

    def _post_raw(self, data: bytes, extra_headers: dict[str, str] | None = None) -> tuple[str, dict[str, str]]:
        for attempt in range(MAX_RETRIES):
            try:
                return super()._post_raw(data, extra_headers)
            except (http.client.IncompleteRead, urllib.error.URLError,
                    ConnectionError, OSError) as e:
                if attempt < MAX_RETRIES - 1:
                    time.sleep(RETRY_DELAY * (attempt + 1))
                else:
                    raise
            except urllib.error.HTTPError as e:
                # Retry on server-side errors; propagate client errors
                if e.code in (429, 500, 502, 503, 504) and attempt < MAX_RETRIES - 1:
                    time.sleep(RETRY_DELAY * (attempt + 1))
                else:
                    raise

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

BASE_URL = os.environ.get("ETIL_TEST_URL", "http://localhost:8080/mcp")
API_KEY = os.environ.get("ETIL_TEST_API_KEY", "")
TIMEOUT = int(os.environ.get("ETIL_TEST_TIMEOUT", "30"))
NUM_WORKERS = int(os.environ.get("ETIL_TEST_WORKERS", "4"))

if not API_KEY:
    print("FATAL: ETIL_TEST_API_KEY is required", file=sys.stderr)
    sys.exit(1)

# Seed: explicit or random
_seed_env = os.environ.get("ETIL_TEST_SEED", "")
if _seed_env:
    SEED = int(_seed_env)
else:
    SEED = random.randint(0, 2**31 - 1)


# ---------------------------------------------------------------------------
# Per-worker result collection
# ---------------------------------------------------------------------------

@dataclasses.dataclass
class WorkerResult:
    worker_id: int
    session_id: str
    section_order: list[int]
    passed: int
    failed: int
    log: list[str]       # all PASS/FAIL messages
    failures: list[str]  # FAIL messages only
    elapsed: float       # wall-clock seconds


# ---------------------------------------------------------------------------
# Result accumulator (thread-local per worker, no globals)
# ---------------------------------------------------------------------------

class ResultAccumulator:
    """Collects PASS/FAIL results without printing."""

    def __init__(self) -> None:
        self.log: list[str] = []
        self.failures: list[str] = []
        self.passed = 0
        self.failed = 0

    def pass_test(self, msg: str) -> None:
        self.log.append(f"PASS: {msg}")
        self.passed += 1

    def fail_test(self, msg: str, raw_resp: dict | None = None) -> None:
        line = f"FAIL: {msg}"
        self.log.append(line)
        if raw_resp is not None:
            diag = json.dumps(raw_resp)[:300]
            self.log.append(f"  DIAG: {diag}")
        self.failures.append(line)
        self.failed += 1


# ---------------------------------------------------------------------------
# Helper: output matching (same logic as sequential test)
# ---------------------------------------------------------------------------

def output_matches(output: str, expected: int) -> bool:
    return bool(re.search(rf"^{expected}\s*$|^{expected} $", output, re.MULTILINE))


# ---------------------------------------------------------------------------
# TIL helper word definitions (per-worker, per-session)
# ---------------------------------------------------------------------------

def define_helpers(client: McpClient) -> None:
    client.interpret(': read-len-sync read-file-sync if slength . else ." READFAIL" then ;')
    client.interpret(': read-len-async read-file if slength . else ." READFAIL" then ;')
    client.interpret(': verify-prefix-sync read-file-sync if 0 62 substr s= if ." MATCH" else ." MISMATCH" then else drop ." READFAIL" then ;')
    client.interpret(': verify-prefix-async read-file if 0 62 substr s= if ." MATCH" else ." MISMATCH" then else drop ." READFAIL" then ;')


# ---------------------------------------------------------------------------
# Section runners — adapted from test_file_io_stress.py
# Each takes (client, acc, written) and tracks files in `written`.
# ---------------------------------------------------------------------------

def run_section_1(client: McpClient, acc: ResultAccumulator, written: set[str]) -> None:
    """Power-of-2 sync round-trips (16 tests)."""
    for p in range(1, 17):
        size = 1 << p
        fname = f"stress_sync_{p}.dat"

        content = gen_content(size)
        write_resp = client.write_file(fname, content)
        written.add(fname)

        if is_error_response(write_resp):
            acc.fail_test(f"sync p={p} ({size} bytes): write_file failed")
            continue

        resp = client.interpret(f's" /home/{fname}" read-len-sync')
        output = extract_output(resp)

        if output_matches(output, size):
            acc.pass_test(f"sync p={p} ({size} bytes): slength = {size}")
        elif "READFAIL" in output:
            acc.fail_test(f"sync p={p} ({size} bytes): read-file-sync returned failure flag", raw_resp=resp)
        else:
            acc.fail_test(f"sync p={p} ({size} bytes): expected slength={size}, got output='{output}'", raw_resp=resp)


def run_section_2(client: McpClient, acc: ResultAccumulator, written: set[str]) -> None:
    """Power-of-2 async round-trips (16 tests)."""
    for p in range(1, 17):
        size = 1 << p
        fname = f"stress_async_{p}.dat"

        content = gen_content(size)
        write_resp = client.write_file(fname, content)
        written.add(fname)

        if is_error_response(write_resp):
            acc.fail_test(f"async p={p} ({size} bytes): write_file failed")
            continue

        resp = client.interpret(f's" /home/{fname}" read-len-async')
        output = extract_output(resp)

        if output_matches(output, size):
            acc.pass_test(f"async p={p} ({size} bytes): slength = {size}")
        elif "READFAIL" in output:
            acc.fail_test(f"async p={p} ({size} bytes): read-file returned failure flag", raw_resp=resp)
        else:
            acc.fail_test(f"async p={p} ({size} bytes): expected slength={size}, got output='{output}'", raw_resp=resp)


def run_section_3(client: McpClient, acc: ResultAccumulator, written: set[str]) -> None:
    """Interleaved multi-file I/O (4 tests)."""
    interleave_size = 1024
    interleave_count = 10

    # Test 3a: Write 10 files via interpreter (alternating sync/async)
    all_written = True
    for i in range(interleave_count):
        fname = f"stress_interleave_{i}.dat"
        content = gen_content_offset(interleave_size, i)
        written.add(fname)
        word = "write-file-sync" if i % 2 == 0 else "write-file"
        resp = client.interpret(f's" {content}" s" /home/{fname}" {word} drop')
        if is_error_response(resp):
            acc.fail_test(f"interleave write file {i}: {word} failed")
            all_written = False
    if all_written:
        acc.pass_test(f"interleave: wrote {interleave_count} files of {interleave_size} bytes each via interpreter")

    # Test 3b: Read all 10 in REVERSE order via async read-file, verify slength
    all_ok = True
    for i in range(interleave_count - 1, -1, -1):
        fname = f"stress_interleave_{i}.dat"
        resp = client.interpret(f's" /home/{fname}" read-len-async')
        output = extract_output(resp)
        if not output_matches(output, interleave_size):
            acc.fail_test(f"interleave async read file {i}: expected slength={interleave_size}, got '{output}'")
            all_ok = False
            break
    if all_ok:
        acc.pass_test(f"interleave: all {interleave_count} files read back in reverse, slength={interleave_size}")

    # Test 3c: Verify even-indexed content prefix via sync read
    even_ok = True
    for i in (0, 2, 4, 6, 8):
        prefix = gen_content_offset(62, i)
        fname = f"stress_interleave_{i}.dat"
        resp = client.interpret(f's" {prefix}" s" /home/{fname}" verify-prefix-sync')
        output = extract_output(resp)
        if "MATCH" not in output:
            acc.fail_test(f"interleave sync verify file {i}: expected MATCH, got '{output}'")
            even_ok = False
            break
    if even_ok:
        acc.pass_test("interleave: even-indexed files verified via verify-prefix-sync")

    # Test 3d: Verify odd-indexed content prefix via async read
    odd_ok = True
    for i in (1, 3, 5, 7, 9):
        prefix = gen_content_offset(62, i)
        fname = f"stress_interleave_{i}.dat"
        resp = client.interpret(f's" {prefix}" s" /home/{fname}" verify-prefix-async')
        output = extract_output(resp)
        if "MATCH" not in output:
            acc.fail_test(f"interleave async verify file {i}: expected MATCH, got '{output}'")
            odd_ok = False
            break
    if odd_ok:
        acc.pass_test("interleave: odd-indexed files verified via verify-prefix-async")


def run_section_4(client: McpClient, acc: ResultAccumulator, written: set[str]) -> None:
    """Byte array boundary stress (8 tests).

    Self-contained: writes its own 1024-byte file for test 4.2 instead of
    depending on section 1's stress_sync_10.dat.
    """
    # Reset interpreter state to clear any leaked stack values
    client.reset()
    define_helpers(client)

    # Write a 1024-byte file for test 4.2 (self-contained)
    s4_fname = "stress_s4_1024.dat"
    client.write_file(s4_fname, gen_content(1024))
    written.add(s4_fname)

    # Test 4.1: string->bytes on 8-char string -> bytes-length = 8
    resp = client.interpret('s" ABCDEFGH" string->bytes bytes-length .')
    output = extract_output(resp)
    if "8" in output:
        acc.pass_test("bytes 4.1: string->bytes 8-char -> bytes-length = 8")
    else:
        acc.fail_test(f"bytes 4.1: expected 8, got '{output}'", raw_resp=resp)

    # Test 4.2: Read 1024-byte file -> string->bytes -> bytes-length = 1024
    resp = client.interpret(f': t42 s" /home/{s4_fname}" read-file-sync if string->bytes bytes-length . else ." READFAIL" then ; t42')
    output = extract_output(resp)
    if "1024" in output:
        acc.pass_test("bytes 4.2: 1024-byte file -> string->bytes -> bytes-length = 1024")
    else:
        acc.fail_test(f"bytes 4.2: expected 1024, got '{output}'", raw_resp=resp)

    # Test 4.3: 0 bytes-new bytes-length -> 0
    resp = client.interpret('0 bytes-new bytes-length .')
    output = extract_output(resp)
    if output_matches(output, 0):
        acc.pass_test("bytes 4.3: 0 bytes-new bytes-length = 0")
    else:
        acc.fail_test(f"bytes 4.3: expected 0, got '{output}'", raw_resp=resp)

    # Test 4.4: out-of-bounds bytes-get -> server survives
    client.interpret('4 bytes-new 10 bytes-get')
    resp = client.interpret('42 .')
    output = extract_output(resp)
    if "42" in output:
        acc.pass_test("bytes 4.4: out-of-bounds bytes-get -> server survived")
    else:
        acc.fail_test("bytes 4.4: server unresponsive after out-of-bounds access")

    # Test 4.5: 8 bytes-new -> 4 bytes-resize -> bytes-length = 4
    resp = client.interpret('8 bytes-new 4 bytes-resize bytes-length .')
    output = extract_output(resp)
    if output_matches(output, 4):
        acc.pass_test("bytes 4.5: 8 bytes-new -> 4 bytes-resize -> bytes-length = 4")
    else:
        acc.fail_test(f"bytes 4.5: expected 4, got '{output}'")

    # Test 4.6: access shrunk index 7 -> server survives
    client.interpret('8 bytes-new 4 bytes-resize 7 bytes-get')
    resp = client.interpret('99 .')
    output = extract_output(resp)
    if "99" in output:
        acc.pass_test("bytes 4.6: access shrunk index 7 -> server survived")
    else:
        acc.fail_test("bytes 4.6: server unresponsive after shrunk bounds access")

    # Test 4.7: 65536 bytes-new bytes-length -> 65536
    resp = client.interpret('65536 bytes-new bytes-length .')
    output = extract_output(resp)
    if "65536" in output:
        acc.pass_test("bytes 4.7: 65536 bytes-new bytes-length = 65536")
    else:
        acc.fail_test(f"bytes 4.7: expected 65536, got '{output}'")

    # Test 4.8: empty string -> string->bytes -> bytes-length = 0
    resp = client.interpret('s" " string->bytes bytes-length .')
    output = extract_output(resp)
    if output_matches(output, 0):
        acc.pass_test("bytes 4.8: empty string -> string->bytes -> bytes-length = 0")
    else:
        acc.fail_test(f"bytes 4.8: expected 0, got '{output}'")

    # Clean up stack debris from intentional error tests (4.4, 4.6)
    client.reset()
    define_helpers(client)


def run_section_5(client: McpClient, acc: ResultAccumulator, written: set[str]) -> None:
    """Append and truncate stress (8 tests)."""
    # Test 5.1: Sync write+append -> slength = 10
    client.write_file("stress_append_sync.dat", "Hello")
    written.add("stress_append_sync.dat")
    resp = client.interpret('s" World" s" /home/stress_append_sync.dat" append-file-sync drop s" /home/stress_append_sync.dat" read-len-sync')
    output = extract_output(resp)
    if output_matches(output, 10):
        acc.pass_test("append 5.1: sync write+append -> slength = 10")
    else:
        acc.fail_test(f"append 5.1: expected 10, got '{output}'")

    # Test 5.2: Async write+append -> slength = 10
    client.write_file("stress_append_async.dat", "Hello")
    written.add("stress_append_async.dat")
    resp = client.interpret('s" World" s" /home/stress_append_async.dat" append-file drop s" /home/stress_append_async.dat" read-len-async')
    output = extract_output(resp)
    if output_matches(output, 10):
        acc.pass_test("append 5.2: async write+append -> slength = 10")
    else:
        acc.fail_test(f"append 5.2: expected 10, got '{output}'")

    # Test 5.3: Sync truncate -> slength = 0
    resp = client.interpret('s" /home/stress_append_sync.dat" truncate-sync drop s" /home/stress_append_sync.dat" read-len-sync')
    output = extract_output(resp)
    if output_matches(output, 0):
        acc.pass_test("truncate 5.3: sync truncate -> slength = 0")
    else:
        acc.fail_test(f"truncate 5.3: expected 0, got '{output}'")

    # Test 5.4: Async truncate -> slength = 0
    resp = client.interpret('s" /home/stress_append_async.dat" truncate drop s" /home/stress_append_async.dat" read-len-async')
    output = extract_output(resp)
    if output_matches(output, 0):
        acc.pass_test("truncate 5.4: async truncate -> slength = 0")
    else:
        acc.fail_test(f"truncate 5.4: expected 0, got '{output}'")

    # Test 5.5: 100x sync append loop -> slength = 100
    client.write_file("stress_append_loop.dat", "")
    written.add("stress_append_loop.dat")
    resp = client.interpret(': t55 100 0 do s" X" s" /home/stress_append_loop.dat" append-file-sync drop loop ; t55 s" /home/stress_append_loop.dat" read-len-sync')
    output = extract_output(resp)
    if output_matches(output, 100):
        acc.pass_test("append 5.5: 100x sync append -> slength = 100")
    else:
        acc.fail_test(f"append 5.5: expected 100, got '{output}'")

    # Test 5.6: exists-sync -> flag = -1
    resp = client.interpret('s" /home/stress_append_loop.dat" exists-sync .')
    output = extract_output(resp)
    if "-1" in output:
        acc.pass_test("exists 5.6: exists-sync -> flag = -1")
    else:
        acc.fail_test(f"exists 5.6: expected -1, got '{output}'")

    # Test 5.7: lstat-sync -> size = 100
    resp = client.interpret(': t57 s" /home/stress_append_loop.dat" lstat-sync if 0 array-get . then ; t57')
    output = extract_output(resp)
    if output_matches(output, 100):
        acc.pass_test("lstat 5.7: lstat-sync size = 100")
    else:
        acc.fail_test(f"lstat 5.7: expected 100, got '{output}'")

    # Test 5.8: rm-sync + exists-sync -> flag = 0
    resp = client.interpret('s" /home/stress_append_loop.dat" rm-sync drop s" /home/stress_append_loop.dat" exists-sync .')
    output = extract_output(resp)
    if output_matches(output, 0):
        acc.pass_test("rm 5.8: rm-sync + exists-sync -> flag = 0")
    else:
        acc.fail_test(f"rm 5.8: expected 0, got '{output}'")
    # stress_append_loop.dat already deleted by test 5.8
    written.discard("stress_append_loop.dat")


def run_section_6(client: McpClient, acc: ResultAccumulator, written: set[str]) -> None:
    """Cleanup (4 tests) — only deletes files tracked in `written`."""
    # Delete all tracked files
    for fname in sorted(written):
        try:
            client.interpret(f's" /home/{fname}" rm-sync drop')
        except Exception:
            pass

    # Test 6.1: Spot-check a sync file is gone
    resp = client.interpret('s" /home/stress_sync_1.dat" exists-sync .')
    output = extract_output(resp)
    if output_matches(output, 0):
        acc.pass_test("cleanup 6.1: sync test files deleted")
    else:
        acc.fail_test("cleanup 6.1: sync files still exist after rm-sync")

    # Test 6.2: Spot-check an async file is gone
    resp = client.interpret('s" /home/stress_async_1.dat" exists-sync .')
    output = extract_output(resp)
    if output_matches(output, 0):
        acc.pass_test("cleanup 6.2: async test files deleted")
    else:
        acc.fail_test("cleanup 6.2: async files still exist after rm-sync")

    # Test 6.3: Spot-check an interleave file is gone
    resp = client.interpret('s" /home/stress_interleave_0.dat" exists-sync .')
    output = extract_output(resp)
    if output_matches(output, 0):
        acc.pass_test("cleanup 6.3: interleaved + append test files deleted")
    else:
        acc.fail_test("cleanup 6.3: interleaved files still exist")

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
        acc.pass_test("cleanup 6.4: session home is clean (0 files)")
    else:
        acc.fail_test(f"cleanup 6.4: session home has {file_count} remaining files")

    written.clear()


# ---------------------------------------------------------------------------
# Section dispatch
# ---------------------------------------------------------------------------

SECTION_RUNNERS = {
    1: run_section_1,
    2: run_section_2,
    3: run_section_3,
    4: run_section_4,
    5: run_section_5,
    6: run_section_6,
}


# ---------------------------------------------------------------------------
# Worker function
# ---------------------------------------------------------------------------

def worker(worker_id: int, seed: int, base_url: str, api_key: str, timeout: int) -> WorkerResult:
    """Run all 56 tests in randomized section order on a dedicated session."""
    rng = random.Random(seed + worker_id)
    t0 = time.monotonic()

    client = ResilientMcpClient(base_url, api_key, timeout)
    session_id = client.initialize()
    define_helpers(client)

    # Shuffle sections 1-5, always end with 6
    sections = [1, 2, 3, 4, 5]
    rng.shuffle(sections)
    sections.append(6)

    acc = ResultAccumulator()
    written: set[str] = set()

    for s in sections:
        try:
            SECTION_RUNNERS[s](client, acc, written)
        except Exception as e:
            acc.fail_test(f"section {s}: unhandled exception: {e}")

    client.terminate()
    elapsed = time.monotonic() - t0

    return WorkerResult(
        worker_id=worker_id,
        session_id=session_id,
        section_order=sections,
        passed=acc.passed,
        failed=acc.failed,
        log=acc.log,
        failures=acc.failures,
        elapsed=elapsed,
    )


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    print("=== Parallel File I/O Fuzz Tests ===")
    print(f"URL: {BASE_URL}")
    print(f"Workers: {NUM_WORKERS}")
    print(f"Seed: {SEED}")
    print(f"Timeout: {TIMEOUT}s")
    print()

    t0 = time.monotonic()

    with ThreadPoolExecutor(max_workers=NUM_WORKERS) as pool:
        futures = [
            pool.submit(worker, i, SEED, BASE_URL, API_KEY, TIMEOUT)
            for i in range(NUM_WORKERS)
        ]
        results = [f.result() for f in as_completed(futures)]

    wall_time = time.monotonic() - t0

    # Sort by worker_id for deterministic output
    results.sort(key=lambda r: r.worker_id)

    # Print per-worker results
    for r in results:
        order_str = ",".join(str(s) for s in r.section_order)
        print(f"--- Worker {r.worker_id} [sections: {order_str}] (session: {r.session_id}) ---")
        for line in r.log:
            print(line)
        print(f"--- Worker {r.worker_id}: {r.passed} PASS, {r.failed} FAIL ({r.elapsed:.1f}s) ---")
        print()

    # Aggregate
    total_pass = sum(r.passed for r in results)
    total_fail = sum(r.failed for r in results)

    print("=== Aggregate Results ===")
    print(f"Workers: {NUM_WORKERS}")
    print(f"Total PASS: {total_pass}")
    print(f"Total FAIL: {total_fail}")
    print(f"Wall time: {wall_time:.1f}s")
    print(f"Seed: {SEED}")

    if total_fail > 0:
        print()
        print("=== Failures ===")
        for r in results:
            for f in r.failures:
                print(f"  Worker {r.worker_id}: {f}")
        sys.exit(1)


if __name__ == "__main__":
    main()
