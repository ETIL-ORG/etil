# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
"""Entry point: python -m etil_mcp_client"""

import argparse
import sys
import urllib.request

from .app import EtilMcpApp
from .config import ClientConfig
from .connections import (
    ConnectionInfo,
    get_connection,
    get_default_connection,
    list_connection_names,
    load_connections,
    save_connections,
)
from .wizard import health_check_url, run_setup_wizard


def _load_exec_source(source: str) -> list[str]:
    """Load lines from a file path or URL for --exec/--execux."""
    try:
        if "://" in source:
            with urllib.request.urlopen(source) as resp:
                text = resp.read().decode("utf-8")
            lines = text.splitlines()
        else:
            with open(source) as f:
                lines = f.read().splitlines()
    except (FileNotFoundError, OSError, urllib.error.URLError) as exc:
        print(f"Error loading exec source '{source}': {exc}", file=sys.stderr)
        sys.exit(1)
    return [line for line in lines if line.strip()]


def _apply_connection(config: ClientConfig, conn: ConnectionInfo) -> None:
    """Copy ConnectionInfo fields into ClientConfig."""
    config.http_url = conn.url
    config.http_api_key = conn.api_key
    config.connection_name = conn.name
    config.http_jwt = conn.jwt
    config.http_jwt_expires_at = conn.jwt_expires_at
    if conn.login_provider and not config.login_provider:
        config.login_provider = conn.login_provider


def main() -> None:
    parser = argparse.ArgumentParser(
        description="ETIL MCP Client — Terminal UI"
    )

    parser.add_argument(
        "--connect",
        metavar="NAME_OR_URL",
        help="Connect by saved name or ad-hoc URL (https://...)",
    )
    parser.add_argument(
        "--list-connections",
        action="store_true",
        help="List available named connections and exit",
    )
    parser.add_argument(
        "--login",
        nargs="?",
        const="github",
        default=None,
        metavar="PROVIDER",
        help="Start OAuth login on launch (default provider: github)",
    )
    parser.add_argument(
        "--nologs", action="store_true",
        help="Disable automatic log file creation on startup",
    )
    parser.add_argument(
        "--norotate", action="store_true",
        help="Disable log file rotation (keep all old logs)",
    )

    exec_group = parser.add_mutually_exclusive_group()
    exec_group.add_argument(
        "--exec", metavar="SOURCE",
        help="Execute a .til file or URL, then exit",
    )
    exec_group.add_argument(
        "--execux", metavar="SOURCE",
        help="Execute a .til file or URL, then stay interactive",
    )

    args = parser.parse_args()

    # --list-connections: print and exit
    if args.list_connections:
        names = list_connection_names()
        if names:
            print("Available connections:")
            for name in names:
                print(f"  {name}")
        else:
            print("No connections configured.")
            print("Run `tui` with no arguments to start the setup wizard.")
        sys.exit(0)

    config = ClientConfig()
    config.auto_logs = not args.nologs
    config.auto_rotate = not args.norotate

    if args.connect:
        if "://" in args.connect:
            # Ad-hoc URL: --connect https://x.y.com/mcp
            url = args.connect
            health_check_url(url)
            if args.login:
                # Save connection and proceed with login
                connections = load_connections()
                from urllib.parse import urlparse
                name = urlparse(url).hostname or "adhoc"
                is_default = len(connections) == 0
                conn = ConnectionInfo(
                    name=name, url=url, api_key="",
                    is_default=is_default,
                )
                connections[name] = conn
                save_connections(connections)
                config.http_url = conn.url
                config.http_api_key = conn.api_key
                config.connection_name = conn.name
                config.login_provider = args.login
            else:
                print(
                    "Use --login to authenticate with a new server.\n"
                    "Example: tui --connect https://x.y.com/mcp --login github",
                    file=sys.stderr,
                )
                sys.exit(1)
        else:
            # Named connection lookup
            conn = get_connection(args.connect)
            if conn is None:
                available = list_connection_names()
                msg = f"Unknown connection: '{args.connect}'"
                if available:
                    msg += f"\nAvailable: {', '.join(available)}"
                else:
                    msg += "\nNo connections configured in ~/.etil/connections.json"
                print(msg, file=sys.stderr)
                sys.exit(1)
            _apply_connection(config, conn)

    else:
        # No --connect → auto-select default connection or run wizard
        conn = get_default_connection()
        if conn is None:
            result = run_setup_wizard()
            if result is None:
                sys.exit(0)  # User cancelled
            conn, provider = result
            config.login_provider = provider
        _apply_connection(config, conn)

    # --login flag overrides saved login_provider
    if args.login is not None:
        config.login_provider = args.login

    exec_source = args.exec or args.execux
    if exec_source:
        config.exec_lines = _load_exec_source(exec_source)
        config.exec_exit = bool(args.exec)

    app = EtilMcpApp(config)
    app.run()


if __name__ == "__main__":
    main()
