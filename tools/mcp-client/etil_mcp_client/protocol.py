# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
"""JSON-RPC 2.0 MCP client protocol."""

from __future__ import annotations

import asyncio
import logging
from collections.abc import Callable

from .models import Direction, JsonRpcMessage
from .transport import McpTransport
from .version import __version__

log = logging.getLogger(__name__)

# Callback for logging JSON-RPC messages to the panel
OnJsonRpc = Callable[[JsonRpcMessage], None]


class McpProtocol:
    """JSON-RPC 2.0 client with request correlation and MCP handshake."""

    def __init__(self, transport: McpTransport) -> None:
        self._transport = transport
        self._next_id = 1
        self._pending: dict[int | str, asyncio.Future] = {}

        # Wire up transport callback
        self._transport.on_message = self._handle_incoming

        # Logging callback
        self.on_jsonrpc: OnJsonRpc | None = None

        # Server notification callback (for notifications/message etc.)
        self.on_server_notification: Callable[[dict], None] | None = None

    def _alloc_id(self) -> int:
        rid = self._next_id
        self._next_id += 1
        return rid

    def _log(self, direction: Direction, data: dict) -> None:
        if self.on_jsonrpc:
            self.on_jsonrpc(JsonRpcMessage(direction=direction, data=data))

    async def request(self, method: str, params: dict | None = None) -> dict:
        """Send a JSON-RPC request and return the response."""
        rid = self._alloc_id()
        msg: dict = {"jsonrpc": "2.0", "id": rid, "method": method}
        if params is not None:
            msg["params"] = params

        future: asyncio.Future[dict] = asyncio.get_event_loop().create_future()
        self._pending[rid] = future

        self._log(Direction.OUTGOING, msg)
        await self._transport.send(msg)

        try:
            return await asyncio.wait_for(future, timeout=60.0)
        except asyncio.TimeoutError:
            self._pending.pop(rid, None)
            raise RuntimeError(f"MCP request '{method}' timed out after 60s")
        except asyncio.CancelledError:
            self._pending.pop(rid, None)
            raise

    async def notify(self, method: str, params: dict | None = None) -> None:
        """Send a JSON-RPC notification (no response expected)."""
        msg: dict = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            msg["params"] = params

        self._log(Direction.OUTGOING, msg)
        await self._transport.send(msg)

    async def initialize(self) -> dict:
        """Perform the full MCP handshake: initialize + notifications/initialized."""
        result = await self.request("initialize", {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {"name": "etil-mcp-client", "version": __version__},
        })
        await self.notify("notifications/initialized")
        return result

    async def call_tool(self, name: str, arguments: dict | None = None) -> dict:
        """Convenience: call tools/call with the given tool name and arguments."""
        params: dict = {"name": name}
        if arguments is not None:
            params["arguments"] = arguments
        return await self.request("tools/call", params)

    async def list_tools(self) -> dict:
        """List available MCP tools."""
        return await self.request("tools/list")

    async def read_resource(self, uri: str) -> dict:
        """Read an MCP resource by URI."""
        return await self.request("resources/read", {"uri": uri})

    def cancel_all(self) -> None:
        """Cancel all pending requests (called on shutdown)."""
        for future in self._pending.values():
            if not future.done():
                future.cancel()
        self._pending.clear()

    def _handle_incoming(self, data: dict) -> None:
        """Handle a message from the transport (called from reader task)."""
        self._log(Direction.INCOMING, data)

        # Match responses to pending requests
        msg_id = data.get("id")
        if msg_id is not None and msg_id in self._pending:
            future = self._pending.pop(msg_id)
            if not future.done():
                future.set_result(data)
        elif msg_id is not None:
            log.warning("Response for unknown request id: %s", msg_id)
        elif "method" in data and self.on_server_notification:
            # Server-initiated notification (no id, has method)
            self.on_server_notification(data)
