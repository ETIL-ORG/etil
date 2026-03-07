# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
"""MCP transport — abstract base class."""

from __future__ import annotations

from abc import ABC, abstractmethod
from collections.abc import Callable

# Callback type aliases
OnMessage = Callable[[dict], None]
OnDiagnostic = Callable[[str], None]
OnClose = Callable[[], None]


class McpTransport(ABC):
    """Abstract base for MCP transports."""

    def __init__(self) -> None:
        self.on_message: OnMessage | None = None
        self.on_diagnostic: OnDiagnostic | None = None
        self.on_close: OnClose | None = None

    @property
    @abstractmethod
    def running(self) -> bool: ...

    @abstractmethod
    async def start(self) -> None: ...

    @abstractmethod
    async def send(self, data: dict) -> None: ...

    @abstractmethod
    async def shutdown(self) -> None: ...

    @property
    def transport_label(self) -> str:
        """Human-readable label for the title bar."""
        return "Unknown"
