# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
"""Client configuration."""

from dataclasses import dataclass


@dataclass
class ClientConfig:
    """Configuration for the ETIL MCP client."""

    # HTTP transport fields
    http_url: str = ""
    http_api_key: str = ""
    connection_name: str = ""
    http_jwt: str = ""
    http_jwt_expires_at: str = ""

    # OAuth login on launch (set by --login)
    login_provider: str = ""

    # Buffer limits
    max_jsonrpc_messages: int = 500
    max_output_lines: int = 1000
    max_notifications: int = 100

    # Logging options
    auto_logs: bool = True       # Start logs on startup (--nologs sets False)
    auto_rotate: bool = True     # Rotate old logs on startup (--norotate sets False)

    # Script execution (--exec / --execux)
    exec_lines: list[str] | None = None   # Lines to execute
    exec_exit: bool = False                # True = exit after exec
