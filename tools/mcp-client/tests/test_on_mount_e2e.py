"""End-to-end test of on_mount OAuth flow.

Tests the ACTUAL on_mount logic path by extracting and running the
same code sequence, with a fake transport that records all calls.
"""

import asyncio
from unittest.mock import patch

import pytest

from etil_mcp_client.config import ClientConfig
from etil_mcp_client.connections import ConnectionInfo, load_connections, save_connections
from etil_mcp_client.http_transport import HttpStreamableTransport


class RecordingTransport(HttpStreamableTransport):
    """Records every method call for post-mortem analysis."""

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.calls: list[str] = []

    async def start(self) -> None:
        self.calls.append("start")
        self._running = True

    async def send(self, data: dict) -> None:
        method = data.get("method", "?")
        self.calls.append(f"send:{method}")
        # Simulate success for initialize
        if self.on_message and method == "initialize":
            self.on_message({
                "jsonrpc": "2.0",
                "id": data.get("id"),
                "result": {
                    "protocolVersion": "2024-11-05",
                    "serverInfo": {"name": "test", "version": "0.0.1"},
                    "capabilities": {},
                },
            })

    async def auth_device(self, provider: str) -> dict:
        self.calls.append(f"auth_device:{provider}")
        return {
            "device_code": "DC",
            "user_code": "ABCD-1234",
            "verification_uri": "https://github.com/login/device",
            "interval": 5,
            "expires_in": 600,
        }

    async def auth_poll(self, provider: str, device_code: str) -> dict:
        self.calls.append(f"auth_poll:{provider}")
        return {"status": "pending"}

    async def shutdown(self) -> None:
        self.calls.append("shutdown")
        self._running = False


class TestOnMountE2E:
    """Simulate on_mount() logic with real config, verify auth_device is called."""

    @pytest.mark.asyncio
    async def test_wizard_flow_second_launch(self, tmp_path):
        """Simulate: wizard saved connection with login_provider=github,
        second launch loads it, on_mount should call auth_device."""
        path = str(tmp_path / "connections.json")
        with patch("etil_mcp_client.connections.CONNECTIONS_PATH", path):
            # Simulate what wizard saved on first run
            conn = ConnectionInfo(
                name="myserver",
                url="https://example.com/mcp",
                api_key="",
                login_provider="github",
                is_default=True,
            )
            save_connections({"myserver": conn})

        # Simulate what __main__.py does on second launch
        with patch("etil_mcp_client.connections.CONNECTIONS_PATH", path):
            from etil_mcp_client.__main__ import _apply_connection
            from etil_mcp_client.connections import get_default_connection

            loaded = get_default_connection()
            assert loaded is not None
            assert loaded.login_provider == "github", (
                f"BUG: login_provider not loaded from JSON! Got: {loaded.login_provider!r}"
            )

            config = ClientConfig()
            _apply_connection(config, loaded)

            assert config.login_provider == "github", (
                f"BUG: _apply_connection didn't set login_provider! Got: {config.login_provider!r}"
            )

        # Now simulate on_mount with this config
        transport = RecordingTransport(
            url=config.http_url,
            api_key=config.http_api_key,
            connection_name=config.connection_name,
        )

        # ----- Replicate on_mount logic exactly -----
        auth_user = ""  # No cached JWT on fresh start

        await transport.start()

        # This is the exact branch from on_mount:
        if (
            config.login_provider
            and not auth_user
            and isinstance(transport, HttpStreamableTransport)
        ):
            # LOGIN-FIRST PATH
            result = await transport.auth_device(config.login_provider)
            assert result["user_code"] == "ABCD-1234"
        else:
            # INITIALIZE-FIRST PATH (should NOT happen)
            pytest.fail(
                f"Took initialize-first path! "
                f"login_provider={config.login_provider!r}, "
                f"auth_user={auth_user!r}, "
                f"isinstance={isinstance(transport, HttpStreamableTransport)}"
            )

        assert "auth_device:github" in transport.calls, (
            f"auth_device never called! Calls: {transport.calls}"
        )
        # initialize should NOT have been called yet
        assert "send:initialize" not in transport.calls, (
            f"initialize was called before auth! Calls: {transport.calls}"
        )

    @pytest.mark.asyncio
    async def test_api_key_flow_no_oauth(self, tmp_path):
        """Connection with API key and no login_provider → direct initialize."""
        path = str(tmp_path / "connections.json")
        with patch("etil_mcp_client.connections.CONNECTIONS_PATH", path):
            conn = ConnectionInfo(
                name="apiserver",
                url="https://example.com/mcp",
                api_key="my-secret-key",
            )
            save_connections({"apiserver": conn})

        with patch("etil_mcp_client.connections.CONNECTIONS_PATH", path):
            from etil_mcp_client.__main__ import _apply_connection
            from etil_mcp_client.connections import get_default_connection

            loaded = get_default_connection()
            config = ClientConfig()
            _apply_connection(config, loaded)

        transport = RecordingTransport(
            url=config.http_url,
            api_key=config.http_api_key,
            connection_name=config.connection_name,
        )

        from etil_mcp_client.protocol import McpProtocol
        protocol = McpProtocol(transport)

        auth_user = ""
        await transport.start()

        if (
            config.login_provider
            and not auth_user
            and isinstance(transport, HttpStreamableTransport)
        ):
            pytest.fail("Should not take login path for API key connection")
        else:
            result = await protocol.initialize()
            assert "error" not in result

        assert "auth_device:github" not in transport.calls
        assert "send:initialize" in transport.calls
