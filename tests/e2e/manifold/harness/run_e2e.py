#!/usr/bin/env python3
# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
"""Manifold E2E driver for the deployed ETIL MCP server.

Sends a .til file to the server via tools/call interpret, parses the
captured stdout for "<name>: PASS" / "<name>: FAIL" markers, exits 0
only if every test emitted PASS and at least one test ran.

Handles both raw-JSON and text/event-stream responses (MCP 2025-03-26
streamable-HTTP). Stdlib only.

Required env / args:
    ETIL_MCP_URL      MCP HTTP endpoint, e.g. https://host/mcp
    ETIL_MCP_KEY_CMD  shell command that prints the bearer token to
                      stdout (so secrets never appear in process args
                      or shell history).
    ETIL_NATS_URL     substituted into the .til file in place of the
                      literal token NATS_URL (default: nats://nats:4222
                      so committed test source carries no broker host).

Exit:
    0  every test in FILE emitted PASS and at least one ran
    1  any FAIL marker, or harness/network/transport error
    2  argument / usage error
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any


# Match both labeled (`name: PASS`) verdicts emitted by test wrappers and
# bare (`PASS`) markers emitted by inner expect-* helpers. Without the bare
# form a multi-assert test that fails on its 2nd or 3rd assert is silently
# counted as passing — only the first labeled marker is matched.
_PASS_FAIL_LINE = re.compile(r'^(?:(\S+): )?(PASS|FAIL)$')


@dataclass(frozen=True)
class Config:
    url: str
    key_cmd: str
    nats_url: str
    til_path: Path


def parse_args(argv: list[str]) -> Config:
    p = argparse.ArgumentParser(
        prog='run_e2e.py',
        description='Manifold E2E driver — sends a .til file to a '
                    'deployed ETIL MCP server via tools/call interpret.',
    )
    p.add_argument('--url', default=os.environ.get('ETIL_MCP_URL'),
                   help='MCP HTTP endpoint (env: ETIL_MCP_URL)')
    p.add_argument('--key-cmd', default=os.environ.get('ETIL_MCP_KEY_CMD'),
                   help='shell command that prints the bearer token '
                        '(env: ETIL_MCP_KEY_CMD)')
    p.add_argument('--nats-url',
                   default=os.environ.get('ETIL_NATS_URL', 'nats://nats:4222'),
                   help='NATS broker URL substituted into the .til file '
                        'in place of the literal token NATS_URL '
                        '(env: ETIL_NATS_URL, default: nats://nats:4222)')
    p.add_argument('file', help='path to the .til test file')
    args = p.parse_args(argv)

    missing: list[str] = []
    if not args.url:
        missing.append('--url / ETIL_MCP_URL')
    if not args.key_cmd:
        missing.append('--key-cmd / ETIL_MCP_KEY_CMD')
    if missing:
        p.error('missing: ' + ', '.join(missing))

    til_path = Path(args.file)
    if not til_path.is_file():
        p.error(f'.til file not found: {til_path}')

    return Config(args.url, args.key_cmd, args.nats_url, til_path)


def fetch_bearer(key_cmd: str) -> str:
    """Run the user-supplied key-cmd; return its stdout as bare token.

    The token never lands in process args (it is passed via header).
    Stderr is suppressed in error messages to avoid leaking partial
    keys if the user's command misroutes them.
    """
    try:
        proc = subprocess.run(
            key_cmd, shell=True, capture_output=True, check=True, text=True,
        )
    except subprocess.CalledProcessError as e:
        sys.exit(
            f'fetch_bearer: key-cmd exit={e.returncode} '
            '(stderr suppressed to avoid leaking secrets); '
            'verify ETIL_MCP_KEY_CMD prints the bearer token to stdout'
        )
    token = proc.stdout.strip()
    if not token:
        sys.exit('fetch_bearer: key-cmd produced empty token')
    return token


def http_post_json(
    url: str, headers: dict[str, str], payload: bytes,
) -> tuple[dict[str, str], bytes]:
    """POST payload, return (response_headers_lowercased, body_bytes).

    Raises SystemExit on transport or HTTP-error status, with URL and
    status in the message. Never echoes the Authorization header or
    request body to avoid leaking secrets.
    """
    req = urllib.request.Request(url, data=payload, method='POST')
    for k, v in headers.items():
        req.add_header(k, v)
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            body = resp.read()
            hdrs = {k.lower(): v for k, v in resp.headers.items()}
            return hdrs, body
    except urllib.error.HTTPError as e:
        err_body = e.read().decode('utf-8', errors='replace')
        sys.exit(
            f'http_post: HTTP {e.code} {e.reason} on POST {url}\n'
            f'response: {err_body[:500]}'
        )
    except urllib.error.URLError as e:
        sys.exit(f'http_post: transport error on POST {url}: {e.reason}')


def parse_jsonrpc(body: bytes, content_type: str) -> dict[str, Any]:
    """Decode body as JSON-RPC payload, handling SSE framing.

    MCP 2025-03-26 streamable-HTTP emits one or more
    `data: <json>\\n\\n` events per response. For one-shot RPC there
    is one event; take the LAST data line so a multi-event stream
    still lands on the final result.
    """
    ct = content_type.lower()
    if ct.startswith('text/event-stream'):
        last_data: str | None = None
        for line in body.decode('utf-8', errors='replace').splitlines():
            if line.startswith('data: '):
                last_data = line[len('data: '):]
        if last_data is None:
            snippet = body[:200].decode('utf-8', errors='replace')
            sys.exit(f'parse_jsonrpc: no "data: " line in SSE body; '
                     f'head: {snippet!r}')
        return json.loads(last_data)
    try:
        return json.loads(body)
    except json.JSONDecodeError as e:
        snippet = body[:200].decode('utf-8', errors='replace')
        sys.exit(
            f'parse_jsonrpc: response is not valid JSON '
            f'(content-type={content_type!r}): {e.msg} at offset {e.pos}; '
            f'head: {snippet!r}'
        )


def initialize(cfg: Config, bearer: str) -> str:
    payload = json.dumps({
        'jsonrpc': '2.0', 'id': 1, 'method': 'initialize',
        'params': {
            'protocolVersion': '2024-11-05',
            'capabilities': {},
            'clientInfo': {'name': 'e2e-driver-py', 'version': '1.0'},
        },
    }).encode('utf-8')
    headers = {
        'Content-Type': 'application/json',
        'Accept': 'application/json, text/event-stream',
        'Authorization': f'Bearer {bearer}',
    }
    resp_hdrs, resp_body = http_post_json(cfg.url, headers, payload)
    sid = resp_hdrs.get('mcp-session-id', '').strip()
    if not sid:
        sys.exit(
            'initialize: server did not return Mcp-Session-Id header; '
            f'response headers: {sorted(resp_hdrs.keys())}'
        )
    rpc = parse_jsonrpc(resp_body, resp_hdrs.get('content-type', ''))
    if 'error' in rpc:
        sys.exit(f'initialize: JSON-RPC error: {rpc["error"]}')
    return sid


def call_interpret(
    cfg: Config, bearer: str, session_id: str, til_code: str,
) -> str:
    """Send tools/call interpret; return captured REPL stdout.

    The interpret tool wraps captured output in a JSON object
    {"errors":...,"output":...,"stack":...,"stackStatus":...} encoded
    as a string under result.content[0].text. We unwrap it and
    return `output`, with any non-empty `errors` appended so failures
    surface visibly.
    """
    payload = json.dumps({
        'jsonrpc': '2.0', 'id': 2, 'method': 'tools/call',
        'params': {
            'name': 'interpret',
            'arguments': {'code': til_code},
        },
    }).encode('utf-8')
    headers = {
        'Content-Type': 'application/json',
        'Accept': 'application/json, text/event-stream',
        'Authorization': f'Bearer {bearer}',
        'Mcp-Session-Id': session_id,
    }
    resp_hdrs, resp_body = http_post_json(cfg.url, headers, payload)
    rpc = parse_jsonrpc(resp_body, resp_hdrs.get('content-type', ''))
    if 'error' in rpc:
        sys.exit(f'call_interpret: JSON-RPC error: {rpc["error"]}')
    result = rpc.get('result')
    if not isinstance(result, dict):
        sys.exit('call_interpret: response missing result object')
    content = result.get('content')
    if not (isinstance(content, list) and content
            and isinstance(content[0], dict)):
        sys.exit(f'call_interpret: unexpected result.content shape: {content!r}')
    text = content[0].get('text')
    if not isinstance(text, str):
        sys.exit('call_interpret: result.content[0].text not a string')

    try:
        inner = json.loads(text)
    except json.JSONDecodeError:
        return text
    if not isinstance(inner, dict):
        return text
    output = inner.get('output', '') or ''
    errors = inner.get('errors', '') or ''
    if errors:
        return f'{output}\n=== REPL errors ===\n{errors}\n'
    return output


def tally(captured: str) -> tuple[list[tuple[str, str]], int, int]:
    """Return ([(name, verdict), ...], pass_count, fail_count)."""
    results: list[tuple[str, str]] = []
    for line in captured.splitlines():
        m = _PASS_FAIL_LINE.match(line.strip())
        if m:
            name = m.group(1) or '<expect>'
            results.append((name, m.group(2)))
    pass_n = sum(1 for _, v in results if v == 'PASS')
    fail_n = sum(1 for _, v in results if v == 'FAIL')
    return results, pass_n, fail_n


def main(argv: list[str]) -> int:
    cfg = parse_args(argv)
    code = cfg.til_path.read_text(encoding='utf-8').replace(
        'NATS_URL', cfg.nats_url
    )
    bearer = fetch_bearer(cfg.key_cmd)
    session_id = initialize(cfg, bearer)
    captured = call_interpret(cfg, bearer, session_id, code)

    print(f'── {cfg.til_path.name} ──')
    print(captured)
    print()

    results, pass_n, fail_n = tally(captured)
    print(f'Result: {pass_n} PASS, {fail_n} FAIL')
    if fail_n:
        print()
        print('Failures:')
        for name, verdict in results:
            if verdict == 'FAIL':
                print(f'  - {name}')
    if pass_n == 0 and fail_n == 0:
        print()
        print('Warning: no PASS/FAIL markers in captured output; '
              'check the .til file or REPL errors above.')

    if fail_n > 0 or pass_n == 0:
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
