# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
"""First-run setup wizard — runs before the TUI launches."""

from __future__ import annotations

import sys
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen

from .connections import (
    ConnectionInfo,
    load_connections,
    save_connections,
)

DEFAULT_URL = ""

# Identity providers offered during setup
PROVIDERS = [
    ("github", "GitHub"),
    ("google", "Google"),
]


def _health_check(url: str) -> bool:
    """Probe the server with OPTIONS. Returns True if reachable.

    A 401 counts as success — it means the URL is routed to the ETIL
    server (nginx requires auth but the location block is active).
    """
    try:
        req = Request(url, method="OPTIONS")
        urlopen(req, timeout=10)
        return True
    except HTTPError as exc:
        if exc.code in (401, 403, 405):
            return True
        print(f"  Server returned HTTP {exc.code}")
        return False
    except URLError as exc:
        print(f"  Cannot reach server: {exc.reason}")
        return False
    except Exception as exc:
        print(f"  Connection error: {exc}")
        return False


def _prompt_url() -> str | None:
    """Ask the user for a server URL. Returns None on cancel."""
    if DEFAULT_URL:
        print(f"\nServer URL (default: {DEFAULT_URL})")
    else:
        print("\nServer URL (e.g. https://your-server.example.com/mcp)")
    try:
        raw = input("> ").strip()
    except (EOFError, KeyboardInterrupt):
        return None
    if not raw:
        if DEFAULT_URL:
            return DEFAULT_URL
        print("A server URL is required.")
        return _prompt_url()
    parsed = urlparse(raw)
    if not parsed.scheme or not parsed.netloc:
        print("Invalid URL — must include https://")
        return _prompt_url()
    return raw


def _prompt_provider() -> str | None:
    """Ask the user to pick an identity provider. Returns None on cancel."""
    print("\nIdentity provider:")
    for i, (_, label) in enumerate(PROVIDERS, 1):
        print(f"  [{i}] {label}")
    try:
        raw = input("> ").strip()
    except (EOFError, KeyboardInterrupt):
        return None
    if not raw:
        return PROVIDERS[0][0]  # default: github
    try:
        idx = int(raw) - 1
        if 0 <= idx < len(PROVIDERS):
            return PROVIDERS[idx][0]
    except ValueError:
        # Try matching name directly
        lower = raw.lower()
        for key, label in PROVIDERS:
            if key == lower or label.lower() == lower:
                return key
    print("Invalid choice.")
    return _prompt_provider()


def _prompt_connection_name(url: str) -> str | None:
    """Ask for a connection name. Returns None on cancel."""
    default = urlparse(url).hostname or "default"
    print(f"\nConnection name (default: {default})")
    try:
        raw = input("> ").strip()
    except (EOFError, KeyboardInterrupt):
        return None
    return raw if raw else default


def run_setup_wizard() -> tuple[ConnectionInfo, str] | None:
    """Interactive first-run setup.

    Returns (connection, provider_name) or None if cancelled.
    """
    print("=" * 50)
    print("  ETIL MCP Client — First-Time Setup")
    print("=" * 50)

    # 1. Server URL
    url = _prompt_url()
    if url is None:
        return None

    # 2. Health check
    print(f"\nChecking {url} ...", end=" ", flush=True)
    if _health_check(url):
        print("OK")
    else:
        print()
        try:
            raw = input("Server unreachable. Continue anyway? [y/N] ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            return None
        if raw not in ("y", "yes"):
            return None

    # 3. Identity provider
    provider = _prompt_provider()
    if provider is None:
        return None

    # 4. Connection name
    name = _prompt_connection_name(url)
    if name is None:
        return None

    # 5. Save
    connections = load_connections()
    is_default = len(connections) == 0  # First connection becomes default
    conn = ConnectionInfo(
        name=name,
        url=url,
        api_key="",
        is_default=is_default,
        login_provider=provider,
    )
    connections[name] = conn
    save_connections(connections)
    print(f"\nConnection '{name}' saved to ~/.etil/connections.json")

    return conn, provider


def health_check_url(url: str) -> bool:
    """Public wrapper for health check (used by --connect URL path)."""
    print(f"Checking {url} ...", end=" ", flush=True)
    ok = _health_check(url)
    if ok:
        print("OK")
    else:
        print()
    return ok
