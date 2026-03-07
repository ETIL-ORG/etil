# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
"""Named connection config stored in ~/.etil/connections.json."""

from __future__ import annotations

import json
import os
from dataclasses import dataclass

CONNECTIONS_PATH = os.path.expanduser("~/.etil/connections.json")


@dataclass
class ConnectionInfo:
    """A named remote MCP server connection."""

    name: str
    url: str
    api_key: str
    jwt: str = ""              # Cached ETIL JWT
    jwt_expires_at: str = ""   # ISO 8601 UTC expiry
    is_default: bool = False   # True if marked "default": true in JSON
    login_provider: str = ""   # OAuth provider (e.g. "github", "google")


def load_connections() -> dict[str, ConnectionInfo]:
    """Load connections from disk. Returns empty dict if file missing."""
    if not os.path.isfile(CONNECTIONS_PATH):
        return {}
    try:
        with open(CONNECTIONS_PATH) as f:
            raw = json.load(f)
    except (json.JSONDecodeError, OSError):
        return {}
    result: dict[str, ConnectionInfo] = {}
    for name, entry in raw.items():
        if isinstance(entry, dict) and "url" in entry and "api_key" in entry:
            result[name] = ConnectionInfo(
                name=name,
                url=entry["url"],
                api_key=entry["api_key"],
                jwt=entry.get("jwt", ""),
                jwt_expires_at=entry.get("jwt_expires_at", ""),
                is_default=bool(entry.get("default", False)),
                login_provider=entry.get("login_provider", ""),
            )
    return result


def get_connection(name: str) -> ConnectionInfo | None:
    """Look up a single named connection."""
    return load_connections().get(name)


def get_default_connection() -> ConnectionInfo | None:
    """Auto-select a default connection.

    - If exactly one connection exists, return it.
    - If multiple, return the one with ``"default": true``.
    - Otherwise return ``None``.
    """
    connections = load_connections()
    if not connections:
        return None
    if len(connections) == 1:
        return next(iter(connections.values()))
    # Multiple connections — look for explicit default
    for conn in connections.values():
        if conn.is_default:
            return conn
    return None


def list_connection_names() -> list[str]:
    """Return sorted list of available connection names."""
    return sorted(load_connections().keys())


def update_connection_jwt(name: str, jwt: str, jwt_expires_at: str) -> None:
    """Update cached JWT for a named connection."""
    connections = load_connections()
    conn = connections.get(name)
    if conn is None:
        return
    conn.jwt = jwt
    conn.jwt_expires_at = jwt_expires_at
    save_connections(connections)


def clear_connection_jwt(name: str) -> None:
    """Clear cached JWT for a named connection."""
    update_connection_jwt(name, "", "")


def save_connections(connections: dict[str, ConnectionInfo]) -> None:
    """Write connections to disk with 0600 permissions (API keys are sensitive)."""
    os.makedirs(os.path.dirname(CONNECTIONS_PATH), exist_ok=True)
    raw: dict[str, dict] = {}
    for name, conn in connections.items():
        entry: dict = {"url": conn.url, "api_key": conn.api_key}
        if conn.is_default:
            entry["default"] = True
        if conn.login_provider:
            entry["login_provider"] = conn.login_provider
        if conn.jwt:
            entry["jwt"] = conn.jwt
            entry["jwt_expires_at"] = conn.jwt_expires_at
        raw[name] = entry
    # Write to temp file then rename for atomicity
    tmp_path = CONNECTIONS_PATH + ".tmp"
    with open(tmp_path, "w") as f:
        json.dump(raw, f, indent=2)
        f.write("\n")
    os.chmod(tmp_path, 0o600)
    os.replace(tmp_path, CONNECTIONS_PATH)
