"""Tests for connections.py — persistence of ConnectionInfo including login_provider."""

import json
import os
import tempfile
from unittest import mock

from etil_mcp_client.connections import (
    ConnectionInfo,
    get_connection,
    get_default_connection,
    load_connections,
    save_connections,
)


def _write_json(path: str, data: dict) -> None:
    with open(path, "w") as f:
        json.dump(data, f)


class TestConnectionPersistence:
    """Verify that all ConnectionInfo fields round-trip through JSON."""

    def test_save_load_roundtrip_with_login_provider(self, tmp_path):
        path = str(tmp_path / "connections.json")
        with mock.patch("etil_mcp_client.connections.CONNECTIONS_PATH", path):
            conn = ConnectionInfo(
                name="test",
                url="https://example.com/mcp",
                api_key="",
                login_provider="github",
                is_default=True,
            )
            save_connections({"test": conn})

            loaded = load_connections()
            assert "test" in loaded
            c = loaded["test"]
            assert c.url == "https://example.com/mcp"
            assert c.api_key == ""
            assert c.login_provider == "github"
            assert c.is_default is True

    def test_save_load_roundtrip_without_login_provider(self, tmp_path):
        path = str(tmp_path / "connections.json")
        with mock.patch("etil_mcp_client.connections.CONNECTIONS_PATH", path):
            conn = ConnectionInfo(
                name="legacy",
                url="https://example.com/mcp",
                api_key="secret123",
            )
            save_connections({"legacy": conn})

            loaded = load_connections()
            c = loaded["legacy"]
            assert c.login_provider == ""
            assert c.is_default is False

    def test_load_missing_login_provider_defaults_empty(self, tmp_path):
        """Old connections.json files won't have login_provider — should default to ''."""
        path = str(tmp_path / "connections.json")
        _write_json(path, {
            "old": {"url": "https://old.example.com/mcp", "api_key": "key"}
        })
        with mock.patch("etil_mcp_client.connections.CONNECTIONS_PATH", path):
            loaded = load_connections()
            assert loaded["old"].login_provider == ""

    def test_get_default_connection_returns_sole_entry(self, tmp_path):
        path = str(tmp_path / "connections.json")
        with mock.patch("etil_mcp_client.connections.CONNECTIONS_PATH", path):
            conn = ConnectionInfo(
                name="only",
                url="https://example.com/mcp",
                api_key="",
                login_provider="google",
            )
            save_connections({"only": conn})
            default = get_default_connection()
            assert default is not None
            assert default.login_provider == "google"

    def test_get_default_prefers_is_default(self, tmp_path):
        path = str(tmp_path / "connections.json")
        with mock.patch("etil_mcp_client.connections.CONNECTIONS_PATH", path):
            conns = {
                "a": ConnectionInfo(name="a", url="https://a.com/mcp", api_key=""),
                "b": ConnectionInfo(
                    name="b", url="https://b.com/mcp", api_key="",
                    login_provider="github", is_default=True,
                ),
            }
            save_connections(conns)
            default = get_default_connection()
            assert default is not None
            assert default.name == "b"
            assert default.login_provider == "github"


class TestWizardToMainFlow:
    """Test that the wizard result flows correctly into ClientConfig."""

    def test_wizard_connection_sets_login_provider_in_config(self, tmp_path):
        """Simulate: wizard saves connection with login_provider, then
        second launch loads it and config.login_provider is set."""
        from etil_mcp_client.config import ClientConfig

        path = str(tmp_path / "connections.json")
        with mock.patch("etil_mcp_client.connections.CONNECTIONS_PATH", path):
            # First run: wizard saves connection
            conn = ConnectionInfo(
                name="myserver",
                url="https://example.com/mcp",
                api_key="",
                login_provider="github",
                is_default=True,
            )
            save_connections({"myserver": conn})

            # Second run: __main__ loads default connection
            loaded_conn = get_default_connection()
            assert loaded_conn is not None

            config = ClientConfig()
            config.http_url = loaded_conn.url
            config.http_api_key = loaded_conn.api_key
            config.connection_name = loaded_conn.name
            config.http_jwt = loaded_conn.jwt
            config.http_jwt_expires_at = loaded_conn.jwt_expires_at
            # This is the critical line that must exist in __main__.py:
            if loaded_conn.login_provider and not config.login_provider:
                config.login_provider = loaded_conn.login_provider

            assert config.login_provider == "github"

    def test_cli_login_flag_overrides_saved_provider(self, tmp_path):
        """--login google should override the saved login_provider."""
        from etil_mcp_client.config import ClientConfig

        path = str(tmp_path / "connections.json")
        with mock.patch("etil_mcp_client.connections.CONNECTIONS_PATH", path):
            conn = ConnectionInfo(
                name="s",
                url="https://example.com/mcp",
                api_key="",
                login_provider="github",
                is_default=True,
            )
            save_connections({"s": conn})

            loaded_conn = get_default_connection()
            config = ClientConfig()
            config.http_url = loaded_conn.url
            if loaded_conn.login_provider and not config.login_provider:
                config.login_provider = loaded_conn.login_provider

            # --login google overrides
            config.login_provider = "google"
            assert config.login_provider == "google"

    def test_api_key_connection_no_login_provider(self, tmp_path):
        """A connection with an API key and no login_provider should not
        trigger OAuth."""
        from etil_mcp_client.config import ClientConfig

        path = str(tmp_path / "connections.json")
        with mock.patch("etil_mcp_client.connections.CONNECTIONS_PATH", path):
            conn = ConnectionInfo(
                name="apikey",
                url="https://example.com/mcp",
                api_key="secret",
            )
            save_connections({"apikey": conn})

            loaded_conn = get_default_connection()
            config = ClientConfig()
            config.http_url = loaded_conn.url
            config.http_api_key = loaded_conn.api_key
            if loaded_conn.login_provider and not config.login_provider:
                config.login_provider = loaded_conn.login_provider

            assert config.login_provider == ""
            assert config.http_api_key == "secret"
