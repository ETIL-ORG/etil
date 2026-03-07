# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
"""HTTP Streamable transport for remote MCP servers."""

from __future__ import annotations

import json
import logging

import httpx

from .transport import McpTransport

log = logging.getLogger(__name__)

# Error code for session-expired synthetic responses
SESSION_EXPIRED_CODE = -32000


class HttpStreamableTransport(McpTransport):
    """MCP transport over HTTP Streamable (POST + SSE batch responses).

    Implements the MCP HTTP Streamable transport spec:
    - POST JSON-RPC to the server URL
    - Parse SSE batch responses (text/event-stream) or plain JSON
    - Track Mcp-Session-Id from response headers
    - Bearer token authentication
    """

    def __init__(
        self,
        url: str,
        api_key: str,
        connection_name: str = "",
        request_timeout: float = 65.0,
    ) -> None:
        super().__init__()
        self._url = url
        self._api_key = api_key
        self._bearer_token = api_key
        self._connection_name = connection_name
        self._request_timeout = request_timeout
        self._client: httpx.AsyncClient | None = None
        self._session_id: str | None = None
        self._running = False

    @property
    def running(self) -> bool:
        return self._running

    @property
    def transport_label(self) -> str:
        if self._connection_name:
            return f"HTTP {self._connection_name}"
        return f"HTTP {self._url}"

    async def start(self) -> None:
        """Create the HTTP client. No connection is made until the first send()."""
        self._client = httpx.AsyncClient(
            headers={
                "Content-Type": "application/json",
                "Accept": "text/event-stream, application/json",
            },
            timeout=httpx.Timeout(self._request_timeout),
        )
        self._running = True

    async def send(self, data: dict) -> None:
        """POST a JSON-RPC message and process the streamed response."""
        if not self._client:
            raise RuntimeError("Transport not started")

        headers = self._auth_headers()
        if self._session_id:
            headers["Mcp-Session-Id"] = self._session_id

        try:
            async with self._client.stream(
                "POST", self._url, json=data, headers=headers,
            ) as response:
                # Capture session ID from headers (available immediately)
                new_session_id = response.headers.get("mcp-session-id")
                if new_session_id:
                    self._session_id = new_session_id

                if response.status_code == 404:
                    log.warning("Session expired (404), clearing session ID")
                    self._session_id = None
                    self._fire_synthetic_error(
                        data, SESSION_EXPIRED_CODE,
                        "Session expired — server returned 404",
                    )
                    return

                if response.status_code == 401:
                    self._fire_diagnostic(
                        "Authentication failed (401) — check API key"
                    )
                    self._fire_synthetic_error(
                        data, -32004, "Authentication failed (401)",
                    )
                    return

                if response.status_code >= 400:
                    body = (await response.aread()).decode(errors="replace")
                    # Extract clean message from HTML error pages (e.g. nginx 429)
                    summary = self._extract_error_summary(
                        response.status_code, body,
                    )
                    self._fire_diagnostic(summary)
                    self._fire_synthetic_error(
                        data, -32005,
                        f"HTTP {response.status_code}",
                    )
                    return

                if response.status_code == 202:
                    return  # Notification accepted, no body

                # Parse based on content type
                content_type = response.headers.get("content-type", "")
                if "text/event-stream" in content_type:
                    # Stream SSE events as they arrive
                    async for line in response.aiter_lines():
                        line = line.strip()
                        if line.startswith("data: "):
                            try:
                                msg = json.loads(line[6:])
                                if self.on_message:
                                    self.on_message(msg)
                            except json.JSONDecodeError as exc:
                                log.warning("Malformed SSE data: %s", exc)
                elif "application/json" in content_type:
                    body = (await response.aread()).decode(errors="replace")
                    try:
                        msg = json.loads(body)
                        if self.on_message:
                            self.on_message(msg)
                    except json.JSONDecodeError as exc:
                        log.warning("Malformed JSON response: %s", exc)
                        self._fire_diagnostic(f"Malformed JSON response: {exc}")
                else:
                    log.warning(
                        "Unexpected content-type: %s (status %d)",
                        content_type, response.status_code,
                    )
        except httpx.ConnectError as exc:
            self._fire_diagnostic(f"Connection failed: {exc}")
            self._fire_synthetic_error(data, -32001, f"Connection failed: {exc}")
        except httpx.TimeoutException as exc:
            self._fire_diagnostic(f"Request timeout: {exc}")
            self._fire_synthetic_error(data, -32002, f"Request timeout: {exc}")
        except httpx.HTTPError as exc:
            self._fire_diagnostic(f"HTTP error: {exc}")
            self._fire_synthetic_error(data, -32003, f"HTTP error: {exc}")

    async def shutdown(self) -> None:
        """Terminate the server session and close the HTTP client."""
        if self._client and self._session_id:
            try:
                del_headers = self._auth_headers()
                del_headers["Mcp-Session-Id"] = self._session_id
                await self._client.request(
                    "DELETE", self._url, headers=del_headers,
                )
            except Exception as exc:
                log.debug("Session DELETE failed (expected on server restart): %s", exc)

        self._session_id = None
        self._running = False

        if self._client:
            await self._client.aclose()
            self._client = None

        if self.on_close:
            self.on_close()

    def _fire_synthetic_error(
        self, request: dict, code: int, message: str,
    ) -> None:
        """Fire a synthetic JSON-RPC error so the pending future resolves."""
        req_id = request.get("id")
        if req_id is None:
            # Notification — no pending future to resolve
            return
        error_response = {
            "jsonrpc": "2.0",
            "id": req_id,
            "error": {"code": code, "message": message},
        }
        if self.on_message:
            self.on_message(error_response)

    @staticmethod
    def _extract_error_summary(status_code: int, body: str) -> str:
        """Extract a clean error message from an HTTP error body.

        Nginx error pages are HTML (e.g., '<html><head><title>429 Too Many
        Requests</title>...'). Extract the title text instead of dumping
        raw HTML into the notification bar.
        """
        import re
        title_match = re.search(r"<title>([^<]+)</title>", body, re.IGNORECASE)
        if title_match:
            return f"HTTP {status_code}: {title_match.group(1)}"
        # Non-HTML body — truncate
        clean = body.strip()[:200]
        if clean:
            return f"HTTP {status_code}: {clean}"
        return f"HTTP {status_code}"

    # ── Auth helpers ────────────────────────────────────────

    def _auth_headers(self) -> dict[str, str]:
        """Build auth headers, omitting Authorization when token is empty."""
        if self._bearer_token:
            return {"Authorization": f"Bearer {self._bearer_token}"}
        return {}

    def _auth_url(self, path: str) -> str:
        """Derive an /auth/<path> URL from the MCP URL.

        Strips the trailing /mcp (or similar) suffix and appends /auth/<path>.
        E.g., https://example.com/mcp → https://example.com/auth/device
        """
        base = self._url.rsplit("/", 1)[0]
        return f"{base}/auth/{path}"

    async def auth_device(self, provider: str) -> dict:
        """POST /auth/device to start the OAuth device flow.

        Returns the device code response dict from the server.
        Raises RuntimeError on HTTP errors.
        """
        if not self._client:
            raise RuntimeError("Transport not started")
        url = self._auth_url("device")
        response = await self._client.post(
            url,
            json={"provider": provider},
            headers=self._auth_headers(),
        )
        if response.status_code >= 400:
            body = response.text.strip()[:200]
            raise RuntimeError(f"auth/device failed ({response.status_code}): {body}")
        return response.json()

    async def auth_poll(self, provider: str, device_code: str) -> dict:
        """POST /auth/poll to check device code status.

        Returns the poll response dict from the server.
        Raises RuntimeError on HTTP errors.
        """
        if not self._client:
            raise RuntimeError("Transport not started")
        url = self._auth_url("poll")
        response = await self._client.post(
            url,
            json={"provider": provider, "device_code": device_code},
            headers=self._auth_headers(),
        )
        if response.status_code >= 400:
            body = response.text.strip()[:200]
            raise RuntimeError(f"auth/poll failed ({response.status_code}): {body}")
        return response.json()

    def update_bearer_token(self, token: str) -> None:
        """Switch to using a JWT as the bearer token."""
        self._bearer_token = token

    def revert_to_api_key(self) -> None:
        """Revert bearer token to the original API key."""
        self._bearer_token = self._api_key

    def _fire_diagnostic(self, message: str) -> None:
        """Report a diagnostic message via the diagnostic callback."""
        if self.on_diagnostic:
            self.on_diagnostic(message)
