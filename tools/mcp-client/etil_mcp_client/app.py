# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
"""Main Textual application — wires transport, protocol, and widgets."""

from __future__ import annotations

import asyncio
import base64
import json
import logging
import os
from datetime import datetime, timezone

from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.widgets import Input

from .completer import CompletionOverlay
from .config import ClientConfig
from .connections import update_connection_jwt, clear_connection_jwt
from .version import __version__
from .http_transport import HttpStreamableTransport, SESSION_EXPIRED_CODE
from .models import JsonRpcMessage, Notification, NotificationType
from .protocol import McpProtocol
from .screens import HelpScreen
from .session_logger import SessionLogger
from .themes import ETIL_DARK, ETIL_LIGHT
from .transport import McpTransport
from .widgets.jsonrpc_panel import JsonRpcPanel
from .widgets.notification_bar import NotificationBar
from .widgets.server_io_panel import ServerIOPanel

log = logging.getLogger(__name__)

# Meta-commands handled locally (not sent to interpreter)
HELP_TEXT = """\
Meta-commands:
  /stack        Show current stack state
  /reset        Reset interpreter state
  /stats        Show session statistics
  /info <word>  Open help page for a word
  /load <path>  Load a local .til file into the interpreter
  /upload <local> [remote]  Upload a file to the server (no execute)
  /download <remote> [local]  Download a file from the server
  /login [provider]  OAuth login (default: github)
  /logout       Clear JWT, revert to API key auth
  /whoami       Show current authentication state
  /logfile [path]  Toggle plain-text session log
  /logjson [path]  Toggle JSON session log
  /log             Toggle both text and JSON logs
  /verbose [on|off]  Toggle verbose JSON in server panel
  /help [word]  Open help browser (optionally at word)
  /clear        Clear server output panel
  /quit         Exit the client

Press F1 to open the help browser.\
"""


HISTORY_PATH = "~/.etil/tui/history.txt"


def _decode_jwt_payload(token: str) -> dict:
    """Base64-decode JWT payload section (no verification — server minted it)."""
    try:
        parts = token.split(".")
        if len(parts) != 3:
            return {}
        # Add padding for base64url
        payload_b64 = parts[1]
        padding = 4 - len(payload_b64) % 4
        if padding != 4:
            payload_b64 += "=" * padding
        payload_bytes = base64.urlsafe_b64decode(payload_b64)
        return json.loads(payload_bytes)
    except Exception:
        return {}


def _jwt_is_expired(jwt_expires_at: str, margin_seconds: int = 300) -> bool:
    """Check if a cached JWT is expired or within margin of expiry."""
    if not jwt_expires_at:
        return True
    try:
        expiry = datetime.fromisoformat(jwt_expires_at.replace("Z", "+00:00"))
        now = datetime.now(timezone.utc)
        remaining = (expiry - now).total_seconds()
        return remaining < margin_seconds
    except (ValueError, TypeError):
        return True


class EtilMcpApp(App):
    """ETIL MCP Client TUI."""

    TITLE = f"ETIL MCP Client v{__version__}"
    CSS_PATH = "styles.tcss"
    BINDINGS = [
        Binding("f1", "show_help", "Help", show=True),
        Binding("f2", "toggle_layout", "Layout", show=True),
        Binding("f3", "toggle_notifications", "Notifs", show=True),
        Binding("f4", "notif_scroll_toggle", "Top/Bot", show=True),
        Binding("ctrl+q", "quit", "Quit", show=True),
        Binding("shift+tab", "focus_previous", "Prev Panel", show=False),
        Binding("ctrl+d", "dismiss_notification", "Dismiss", show=True),
        Binding("ctrl+l", "clear_output", "Clear", show=True),
        Binding("escape", "focus_command", "Command", show=True),
    ]

    def __init__(self, config: ClientConfig) -> None:
        super().__init__()
        self._config = config
        self._transport = self._create_transport(config)
        self._protocol = McpProtocol(self._transport)
        self._connected = False
        self._verbose = False
        self._layout_mode = 1  # 0=both, 1=io-only, 2=rpc-only
        self._notif_fullscreen = False
        self._command_in_flight = False
        self._session_logger = SessionLogger()
        self._completer = CompletionOverlay()

        # OAuth / JWT auth state
        self._auth_provider: str = ""
        self._auth_user: str = ""
        self._auth_role: str = ""
        self._auth_email: str = ""
        self._login_task: asyncio.Task | None = None
        self._heartbeat_task: asyncio.Task | None = None

        # Register themes
        self.register_theme(ETIL_DARK)
        self.register_theme(ETIL_LIGHT)
        self.theme = "etil-dark"

    @staticmethod
    def _create_transport(config: ClientConfig) -> McpTransport:
        """Create the HTTP transport from config."""
        return HttpStreamableTransport(
            url=config.http_url,
            api_key=config.http_api_key,
            connection_name=config.connection_name,
        )

    def compose(self) -> ComposeResult:
        yield ServerIOPanel(completer=self._completer, id="server-io-panel")
        yield JsonRpcPanel(id="jsonrpc-panel")
        yield NotificationBar(
            max_notifications=self._config.max_notifications,
            id="notification-bar",
        )

    @property
    def jsonrpc_panel(self) -> JsonRpcPanel:
        return self.query_one("#jsonrpc-panel", JsonRpcPanel)

    @property
    def server_io(self) -> ServerIOPanel:
        return self.query_one("#server-io-panel", ServerIOPanel)

    @property
    def notification_bar(self) -> NotificationBar:
        return self.query_one("#notification-bar", NotificationBar)

    # ── Lifecycle ──────────────────────────────────────────────

    async def on_mount(self) -> None:
        """Connect transport, run handshake, focus command input."""
        # Start with Server I/O full width (JSON-RPC panel hidden)
        self.screen.add_class("io-only")

        # Dynamic title based on transport
        self.title = f"ETIL MCP Client v{__version__} [{self._transport.transport_label}]"

        # Load persistent history
        self.server_io.load_history(HISTORY_PATH)

        # Wire callbacks
        self.server_io.on_log_entry = self._session_logger.on_entry
        self._protocol.on_jsonrpc = self._on_jsonrpc_message
        self._protocol.on_server_notification = self._on_server_notification
        self._transport.on_diagnostic = self._on_diagnostic
        self._transport.on_close = self._on_transport_close

        # Rotate old log files
        if self._config.auto_rotate:
            deleted = SessionLogger.rotate_logs(max_keep=5)
            if deleted:
                log.info("Rotated %d old log file(s)", deleted)

        # Auto-start session logging
        if self._config.auto_logs:
            for active, fpath in self._session_logger.start_both():
                if active:
                    self._notify(NotificationType.INFO, f"Logging to {fpath}")

        # Version announcement
        self._notify(NotificationType.INFO, f"ETIL MCP Client v{__version__}")

        # Restore cached JWT if valid
        if (
            self._config.http_jwt
            and isinstance(self._transport, HttpStreamableTransport)
        ):
            if _jwt_is_expired(self._config.http_jwt_expires_at):
                self._notify(
                    NotificationType.WARNING,
                    "Cached JWT expired, using API key",
                )
                if self._config.connection_name:
                    clear_connection_jwt(self._config.connection_name)
            else:
                self._transport.update_bearer_token(self._config.http_jwt)
                payload = _decode_jwt_payload(self._config.http_jwt)
                self._auth_user = payload.get("sub", "")
                self._auth_email = payload.get("email", "")
                self._auth_role = payload.get("role", "")
                self._auth_provider = self._auth_user.split(":")[0] if ":" in self._auth_user else ""
                provider_label = self._auth_provider.capitalize() or "OAuth"
                email_part = f" {self._auth_email}" if self._auth_email else ""
                self._notify(
                    NotificationType.SUCCESS,
                    f"Logged in using {provider_label}{email_part}",
                )

        self._notify(NotificationType.CONNECTION, "Connecting to MCP server...")

        try:
            await self._transport.start()

            # OAuth login-first: if login requested and no token yet,
            # run the device flow before initialize() so we have a JWT.
            if (
                self._config.login_provider
                and not self._auth_user
                and isinstance(self._transport, HttpStreamableTransport)
            ):
                await self._meta_login(self._config.login_provider)
                # _meta_login spawns _poll_device_code as a background task.
                # Don't call initialize() yet — _handle_login_success will
                # call it after the JWT is obtained.
            else:
                await self._initialize_session()
                self._maybe_start_exec()

        except FileNotFoundError:
            self._notify(NotificationType.ERROR, "Connection failed")
            self.server_io.append_error("Connection failed (unexpected error)")
        except Exception as exc:
            self._notify(NotificationType.ERROR, f"Connection failed: {exc}")
            self.server_io.append_error(f"Connection error: {exc}")

        # Focus command input
        self.server_io.input_widget.focus()

    async def _initialize_session(self) -> None:
        """Run MCP initialize and set up the session on success."""
        result = await self._protocol.initialize()

        if "error" in result:
            err = result["error"]
            self._notify(
                NotificationType.ERROR,
                f"Initialize failed: {err.get('message', err)}",
            )
            self.server_io.append_error(
                f"MCP initialize failed: {err.get('message', err)}"
            )
        else:
            self._connected = True
            server_info = result.get("result", {}).get("serverInfo", {})
            name = server_info.get("name", "unknown")
            version = server_info.get("version", "?")
            self._notify(
                NotificationType.SUCCESS,
                f"Connected: {name} v{version}",
            )
            await self._fetch_word_list()
            self._start_heartbeat()

    async def _exec_script_lines(self) -> None:
        """Feed exec_lines as if typed at the console, one at a time."""
        lines = self._config.exec_lines or []
        for line in lines:
            line = line.strip()
            if not line:
                continue
            self.server_io.submit_command(line)
            if line.startswith("/"):
                await self._handle_meta_command(line)
            else:
                await self._handle_til_code(line)
        if self._config.exec_exit:
            await self.action_quit()

    def _start_heartbeat(self) -> None:
        """Start a background heartbeat to keep the session alive."""
        if self._heartbeat_task and not self._heartbeat_task.done():
            return  # already running
        self._heartbeat_task = asyncio.create_task(self._heartbeat_loop())

    def _stop_heartbeat(self) -> None:
        """Cancel the heartbeat task."""
        if self._heartbeat_task and not self._heartbeat_task.done():
            self._heartbeat_task.cancel()
            self._heartbeat_task = None

    async def _heartbeat_loop(self) -> None:
        """Send periodic get_session_stats to keep the server session alive."""
        try:
            while True:
                await asyncio.sleep(300)  # 5 minutes
                if not self._connected:
                    break
                try:
                    await self._protocol.call_tool("get_session_stats", {})
                except Exception:
                    pass  # heartbeat is best-effort
        except asyncio.CancelledError:
            pass

    def _maybe_start_exec(self) -> None:
        """If --exec/--execux was given, start executing lines."""
        if self._config.exec_lines and self._connected:
            asyncio.create_task(self._exec_script_lines())

    async def action_quit(self) -> None:
        """Clean shutdown."""
        self._stop_heartbeat()
        if self._login_task and not self._login_task.done():
            self._login_task.cancel()
        self.server_io.save_history(HISTORY_PATH)
        for path in self._session_logger.close_all():
            self._notify(NotificationType.INFO, f"Log file {path} closed")
        self._protocol.cancel_all()
        await self._transport.shutdown()
        self.exit()

    # ── Command dispatch ──────────────────────────────────────

    async def on_input_submitted(self, event: Input.Submitted) -> None:
        """Handle Enter from the command input."""
        command = event.value.strip()
        if not command:
            return
        if self._command_in_flight:
            return

        # Echo and record history in the panel
        self.server_io.submit_command(command)

        self._set_command_in_flight(True)
        asyncio.create_task(self._run_command(command))

    async def _run_command(self, command: str) -> None:
        """Run a command as a background task so the event loop stays free."""
        try:
            if command.startswith("/"):
                await self._handle_meta_command(command)
            else:
                await self._handle_til_code(command)
        finally:
            self._set_command_in_flight(False)

    def _set_command_in_flight(self, in_flight: bool) -> None:
        """Toggle command-in-flight state and input widget availability."""
        self._command_in_flight = in_flight
        self.server_io.input_widget.disabled = in_flight
        if not in_flight:
            self.server_io.input_widget.focus()

    async def _handle_til_code(self, code: str) -> None:
        """Send TIL code to the interpreter via tools/call."""
        if not self._connected:
            self.server_io.append_error("Not connected to MCP server.")
            return

        try:
            response = await self._protocol.call_tool("interpret", {"code": code})
            # Session expired — try auto-reconnect once
            if self._is_session_expired(response) and await self._reconnect():
                response = await self._protocol.call_tool("interpret", {"code": code})
            self._display_tool_result(response)
        except asyncio.CancelledError:
            self.server_io.append_error("Request cancelled (server disconnected)")
        except Exception as exc:
            self.server_io.append_error(f"Error: {exc}")

    async def _handle_meta_command(self, cmd: str) -> None:
        """Handle meta-commands."""
        parts = cmd.split(None, 1)
        verb = parts[0].lower()
        arg = parts[1] if len(parts) > 1 else ""

        if verb == "/quit":
            await self.action_quit()
        elif verb == "/help":
            if not self._connected:
                self.server_io.append_info(HELP_TEXT)
            elif arg:
                self.push_screen(HelpScreen(self._protocol, config=self._config, initial_word=arg))
            else:
                self.push_screen(HelpScreen(self._protocol, config=self._config))
        elif verb == "/verbose":
            self._meta_verbose(arg)
        elif verb == "/logfile":
            self._meta_logfile(arg)
        elif verb == "/logjson":
            self._meta_logjson(arg)
        elif verb == "/log":
            self._meta_log()
        elif verb == "/login":
            await self._meta_login(arg.strip() or "github")
        elif verb == "/logout":
            await self._meta_logout()
        elif verb == "/whoami":
            self._meta_whoami()
        elif verb == "/clear":
            self.server_io.clear_log()
        elif not self._connected:
            self.server_io.append_error("Not connected to MCP server.")
        elif verb == "/stack":
            await self._meta_stack()
        elif verb == "/reset":
            await self._meta_reset()
        elif verb == "/stats":
            await self._meta_stats()
        elif verb == "/info":
            if arg:
                self.push_screen(HelpScreen(self._protocol, config=self._config, initial_word=arg))
            else:
                self.server_io.append_error("Usage: /info <word>")
        elif verb == "/load":
            if arg:
                await self._meta_load(arg)
            else:
                self.server_io.append_error("Usage: /load <path>")
        elif verb == "/upload":
            if arg:
                await self._meta_upload(arg)
            else:
                self.server_io.append_error("Usage: /upload <local-path> [remote-name]")
        elif verb == "/download":
            if arg:
                await self._meta_download(arg)
            else:
                self.server_io.append_error("Usage: /download <remote-path> [local-path]")
        else:
            self.server_io.append_error(f"Unknown command: {verb}")

    async def _meta_stack(self) -> None:
        try:
            response = await self._protocol.call_tool("get_stack")
            if self._is_session_expired(response) and await self._reconnect():
                response = await self._protocol.call_tool("get_stack")
            self._display_tool_result(response)
        except asyncio.CancelledError:
            self.server_io.append_error("Request cancelled (server disconnected)")
        except Exception as exc:
            self.server_io.append_error(f"Error: {exc}")

    async def _meta_reset(self) -> None:
        try:
            response = await self._protocol.call_tool("reset")
            if self._is_session_expired(response) and await self._reconnect():
                response = await self._protocol.call_tool("reset")
            self._display_tool_result(response)
            self._notify(NotificationType.INFO, "Interpreter reset")
            # Re-fetch word list (builtins may have changed)
            await self._fetch_word_list()
        except asyncio.CancelledError:
            self.server_io.append_error("Request cancelled (server disconnected)")
        except Exception as exc:
            self.server_io.append_error(f"Error: {exc}")

    async def _meta_stats(self) -> None:
        try:
            response = await self._protocol.call_tool("get_session_stats")
            if self._is_session_expired(response) and await self._reconnect():
                response = await self._protocol.call_tool("get_session_stats")
            self._display_tool_result(response)
        except asyncio.CancelledError:
            self.server_io.append_error("Request cancelled (server disconnected)")
        except Exception as exc:
            self.server_io.append_error(f"Error: {exc}")

    def _scan_includes(self, file_path: str, base_dir: str,
                        uploaded: set[str],
                        cwd: str | None = None) -> list[tuple[str, str]]:
        """Recursively scan a file for 'include' statements.

        Returns a list of (relative_name, absolute_path) pairs for files
        that need uploading, in dependency order (deepest first).

        Tries to resolve include paths relative to the parent file's
        directory first, then falls back to cwd (if provided).
        """
        results: list[tuple[str, str]] = []
        try:
            with open(file_path, "r") as f:
                content = f.read()
        except OSError:
            return results

        import re
        for line in content.splitlines():
            # Skip comment lines
            stripped = line.lstrip()
            if stripped.startswith('#'):
                continue
            # Strip inline comment before matching
            comment_pos = line.find(' #')
            if comment_pos >= 0:
                line = line[:comment_pos]
            match = re.search(r'\binclude\s+(\S+)', line)
            if not match:
                continue
            inc_name = match.group(1)
            if inc_name in uploaded:
                continue
            # Resolve include relative to the same directory as the parent file
            inc_path = os.path.join(base_dir, inc_name)
            if not os.path.isfile(inc_path) and cwd:
                # Fallback: try relative to CWD (e.g. include tests/til/harness.til)
                inc_path = os.path.join(cwd, inc_name)
            if os.path.isfile(inc_path):
                uploaded.add(inc_name)
                # Recurse first (depth-first) so dependencies are uploaded first
                results.extend(self._scan_includes(
                    inc_path, os.path.dirname(inc_path), uploaded, cwd))
                results.append((inc_name, inc_path))

        return results

    async def _meta_load(self, path: str) -> None:
        """Load a local .til file and upload it + includes to the server."""
        expanded = os.path.expanduser(path)
        if not os.path.isfile(expanded):
            self.server_io.append_error(f"File not found: {expanded}")
            return

        base_dir = os.path.dirname(os.path.abspath(expanded))
        basename = os.path.basename(expanded)

        try:
            with open(expanded, "r") as f:
                main_content = f.read()
        except OSError as exc:
            self.server_io.append_error(f"Cannot read file: {exc}")
            return

        if not main_content.strip():
            self.server_io.append_info(f"Loaded {expanded} (empty)")
            return

        # Scan for include dependencies
        cwd = os.getcwd()
        uploaded: set[str] = {basename}
        includes = self._scan_includes(expanded, base_dir, uploaded, cwd)

        # Upload included files first (depth-first order), then the main file
        upload_count = 0
        try:
            for inc_name, inc_path in includes:
                with open(inc_path, "r") as f:
                    inc_content = f.read()
                resp = await self._protocol.call_tool(
                    "write_file", {"path": inc_name, "content": inc_content})
                if resp.get("result", {}).get("isError", False):
                    self.server_io.append_info(
                        "write_file failed (server has no session directory), "
                        "loading via interpret")
                    return await self._meta_load_fallback(expanded, main_content)
                upload_count += 1

            # Upload the main file
            resp = await self._protocol.call_tool(
                "write_file", {"path": basename, "content": main_content})
            if resp.get("result", {}).get("isError", False):
                self.server_io.append_info(
                    "write_file failed (server has no session directory), "
                    "loading via interpret")
                return await self._meta_load_fallback(expanded, main_content)
            upload_count += 1
        except asyncio.CancelledError:
            self.server_io.append_error("Request cancelled (server disconnected)")
            return
        except Exception as exc:
            # write_file not available — fall back to direct interpret
            self.server_io.append_info(
                f"write_file not available ({exc}), loading via interpret")
            return await self._meta_load_fallback(expanded, main_content)

        self.server_io.append_info(
            f"Uploaded {upload_count} file(s), executing {basename}")

        # Execute the main file via include
        try:
            response = await self._protocol.call_tool(
                "interpret", {"code": f"include {basename}"})
            self._display_tool_result(response)
        except asyncio.CancelledError:
            self.server_io.append_error("Request cancelled (server disconnected)")
        except Exception as exc:
            self.server_io.append_error(f"Error: {exc}")

    async def _meta_load_fallback(self, expanded: str, content: str) -> None:
        """Fallback /load: send code directly via interpret (no file upload).

        Sends raw content without comment stripping or line joining —
        the server's interpreter handles # comments as a parsing token.
        """
        if not content.strip():
            self.server_io.append_info(f"Loaded {expanded} (empty)")
            return

        self.server_io.append_info(f"Loading {expanded} (fallback mode)")
        try:
            response = await self._protocol.call_tool(
                "interpret", {"code": content})
            self._display_tool_result(response)
        except asyncio.CancelledError:
            self.server_io.append_error("Request cancelled (server disconnected)")
        except Exception as exc:
            self.server_io.append_error(f"Error: {exc}")

    async def _meta_upload(self, arg: str) -> None:
        """Upload a local file to the server without executing it."""
        parts = arg.split(None, 1)
        local_path = os.path.expanduser(parts[0])
        remote_name = parts[1] if len(parts) > 1 else os.path.basename(local_path)

        if not os.path.isfile(local_path):
            self.server_io.append_error(f"File not found: {local_path}")
            return

        try:
            with open(local_path, "r") as f:
                content = f.read()
        except OSError as exc:
            self.server_io.append_error(f"Cannot read file: {exc}")
            return

        try:
            resp = await self._protocol.call_tool(
                "write_file", {"path": remote_name, "content": content})
            if self._is_session_expired(resp) and await self._reconnect():
                resp = await self._protocol.call_tool(
                    "write_file", {"path": remote_name, "content": content})
            if resp.get("result", {}).get("isError", False):
                self._display_tool_result(resp)
            else:
                self.server_io.append_result(
                    output=f"Uploaded {local_path} -> {remote_name} ({len(content)} bytes)")
        except asyncio.CancelledError:
            self.server_io.append_error("Request cancelled (server disconnected)")
        except Exception as exc:
            self.server_io.append_error(f"Error: {exc}")

    async def _meta_download(self, arg: str) -> None:
        """Download a file from the server to a local path."""
        parts = arg.split(None, 1)
        remote_path = parts[0]
        local_path = parts[1] if len(parts) > 1 else os.path.basename(remote_path)
        local_path = os.path.expanduser(local_path)

        try:
            resp = await self._protocol.call_tool(
                "read_file", {"path": remote_path})
            if self._is_session_expired(resp) and await self._reconnect():
                resp = await self._protocol.call_tool(
                    "read_file", {"path": remote_path})
        except asyncio.CancelledError:
            self.server_io.append_error("Request cancelled (server disconnected)")
            return
        except Exception as exc:
            self.server_io.append_error(f"Error: {exc}")
            return

        # Check for server error
        result = resp.get("result", {})
        if result.get("isError", False):
            self._display_tool_result(resp)
            return

        # Parse the response to get the file content
        content_list = result.get("content", [])
        combined = ""
        for item in content_list:
            text = item.get("text", "")
            if text:
                combined = text
                break

        try:
            data = json.loads(combined)
            file_content = data.get("content", "")
        except (json.JSONDecodeError, TypeError):
            self.server_io.append_error("Unexpected response format from read_file")
            return

        # Write to local file
        try:
            with open(local_path, "w") as f:
                f.write(file_content)
            size = len(file_content)
            self.server_io.append_result(
                output=f"Downloaded {remote_path} -> {local_path} ({size} bytes)")
        except OSError as exc:
            self.server_io.append_error(f"Cannot write file: {exc}")

    def _meta_verbose(self, arg: str) -> None:
        """Toggle or set verbose JSON mode for server I/O output."""
        arg = arg.strip().lower()
        if arg == "on":
            self._verbose = True
        elif arg == "off":
            self._verbose = False
        else:
            self._verbose = not self._verbose
        state = "on" if self._verbose else "off"
        self.server_io.append_info(f"Verbose JSON: {state}")

    def _meta_logfile(self, arg: str) -> None:
        """Toggle plain-text session log."""
        path = arg.strip() or None
        active, fpath = self._session_logger.toggle_text(path)
        if active:
            msg = f"Logging to {fpath}"
        else:
            msg = f"Log file {fpath} closed"
        self._notify(NotificationType.INFO, msg)
        self.server_io.append_info(msg)

    def _meta_logjson(self, arg: str) -> None:
        """Toggle JSONL session log."""
        path = arg.strip() or None
        active, fpath = self._session_logger.toggle_json(path)
        if active:
            msg = f"Logging to {fpath}"
        else:
            msg = f"Log file {fpath} closed"
        self._notify(NotificationType.INFO, msg)
        self.server_io.append_info(msg)

    def _meta_log(self) -> None:
        """Toggle both text and JSON logs."""
        results = self._session_logger.toggle_both()
        for active, fpath in results:
            if active:
                msg = f"Logging to {fpath}"
            else:
                msg = f"Log file {fpath} closed"
            self._notify(NotificationType.INFO, msg)
            self.server_io.append_info(msg)

    # ── OAuth login/logout/whoami ─────────────────────────────

    async def _run_auto_login(self) -> None:
        """Auto-login wrapper for --login flag (runs as background task)."""
        await self._meta_login(self._config.login_provider)

    async def _meta_login(self, provider: str) -> None:
        """Start OAuth device flow login."""
        if not isinstance(self._transport, HttpStreamableTransport):
            self.server_io.append_error(
                "OAuth login requires HTTP transport"
            )
            return

        if self._login_task and not self._login_task.done():
            self.server_io.append_error(
                "Login already in progress. Use /logout to cancel."
            )
            return

        try:
            result = await self._transport.auth_device(provider)
        except Exception as exc:
            self.server_io.append_error(f"Login failed: {exc}")
            return

        user_code = result.get("user_code", "???")
        verification_uri = result.get("verification_uri", "")
        device_code = result.get("device_code", "")
        interval = result.get("interval", 5)
        expires_in = result.get("expires_in", 600)

        self.server_io.append_info(
            f"OAuth Device Flow ({provider})\n"
            f"  Code: {user_code}\n"
            f"  URL:  {verification_uri}\n"
            f"Open the URL in your browser and enter the code."
        )
        self._notify(
            NotificationType.INFO,
            f"Login: enter code {user_code} at {verification_uri}",
        )

        # Spawn background poll task
        self._login_task = asyncio.create_task(
            self._poll_device_code(provider, device_code, interval, expires_in)
        )

    async def _poll_device_code(
        self,
        provider: str,
        device_code: str,
        interval: int,
        expires_in: int,
    ) -> None:
        """Background task: poll for device code grant."""
        assert isinstance(self._transport, HttpStreamableTransport)
        elapsed = 0
        poll_interval = max(interval, 5)
        try:
            while elapsed < expires_in:
                await asyncio.sleep(poll_interval)
                elapsed += poll_interval

                try:
                    result = await self._transport.auth_poll(provider, device_code)
                except Exception as exc:
                    self._notify(NotificationType.ERROR, f"Poll error: {exc}")
                    return

                status = result.get("status", "")

                if status == "pending":
                    self._notify(
                        NotificationType.INFO,
                        f"Waiting for authorization... ({elapsed}s)",
                    )
                    continue

                if status == "slow_down":
                    new_interval = result.get("interval", poll_interval + 5)
                    poll_interval = max(new_interval, poll_interval + 5)
                    self._notify(
                        NotificationType.INFO,
                        f"Slowing down (interval={poll_interval}s)",
                    )
                    continue

                if status == "expired_token":
                    self._notify(NotificationType.ERROR, "Login expired. Try /login again.")
                    self.server_io.append_error("Device code expired.")
                    return

                if status == "access_denied":
                    self._notify(NotificationType.ERROR, "Login denied by user.")
                    self.server_io.append_error("Authorization was denied.")
                    return

                # Grant: result has "token" key
                if "token" in result:
                    await self._handle_login_success(
                        provider,
                        result["token"],
                        result.get("role", ""),
                        result.get("expires_in", 3600),
                    )
                    return

                # Unknown status
                self._notify(
                    NotificationType.WARNING,
                    f"Unknown poll status: {status}",
                )
                return

            # Timed out
            self._notify(NotificationType.ERROR, "Login timed out.")
            self.server_io.append_error("Device code expired (timed out).")

        except asyncio.CancelledError:
            self._notify(NotificationType.INFO, "Login cancelled.")

    async def _handle_login_success(
        self,
        provider: str,
        token: str,
        role: str,
        expires_in: int,
    ) -> None:
        """Process a successful OAuth login."""
        assert isinstance(self._transport, HttpStreamableTransport)

        # Decode JWT payload for display
        payload = _decode_jwt_payload(token)
        self._auth_user = payload.get("sub", "")
        self._auth_email = payload.get("email", "")
        self._auth_role = role or payload.get("role", "")
        self._auth_provider = provider

        # Switch transport to JWT
        self._transport.update_bearer_token(token)

        # Cache JWT in connections.json (if named connection)
        if self._config.connection_name:
            exp = payload.get("exp", 0)
            if exp:
                expires_at = datetime.fromtimestamp(
                    exp, tz=timezone.utc
                ).isoformat()
            else:
                expires_at = ""
            update_connection_jwt(
                self._config.connection_name, token, expires_at
            )

        # (Re)connect MCP session with new JWT
        self._stop_heartbeat()
        self._session_id = None
        if hasattr(self._transport, '_session_id'):
            self._transport._session_id = None
        self._connected = False
        try:
            await self._initialize_session()
            self._maybe_start_exec()
        except Exception as exc:
            self._notify(NotificationType.ERROR, f"Reconnect failed: {exc}")

        display = self._auth_email or self._auth_user
        self._notify(
            NotificationType.SUCCESS,
            f"Logged in as {display} (role: {self._auth_role})",
        )
        self.server_io.append_info(
            f"Authenticated: {display}\n"
            f"  Provider: {provider}\n"
            f"  Role: {self._auth_role}\n"
            f"  User ID: {self._auth_user}"
        )

    async def _meta_logout(self) -> None:
        """Clear JWT and revert to API key auth."""
        self._stop_heartbeat()
        # Cancel in-progress login
        if self._login_task and not self._login_task.done():
            self._login_task.cancel()
            self._login_task = None

        if isinstance(self._transport, HttpStreamableTransport):
            self._transport.revert_to_api_key()

        # Clear auth state
        was_logged_in = bool(self._auth_user)
        self._auth_provider = ""
        self._auth_user = ""
        self._auth_role = ""
        self._auth_email = ""

        # Clear cached JWT
        if self._config.connection_name:
            clear_connection_jwt(self._config.connection_name)

        if was_logged_in:
            # Reconnect with API key
            if hasattr(self._transport, '_session_id'):
                self._transport._session_id = None
            self._connected = False
            try:
                result = await self._protocol.initialize()
                if "error" not in result:
                    self._connected = True
                    await self._fetch_word_list()
                    self._start_heartbeat()
            except Exception as exc:
                self._notify(NotificationType.ERROR, f"Reconnect failed: {exc}")

            self._notify(NotificationType.INFO, "Logged out, reverted to API key")
            self.server_io.append_info("Reverted to API key authentication.")
        else:
            self._notify(NotificationType.INFO, "Not logged in")
            self.server_io.append_info("No active OAuth session.")

    def _meta_whoami(self) -> None:
        """Display current authentication state."""
        if self._auth_user:
            display = self._auth_email or self._auth_user
            self.server_io.append_info(
                f"Authenticated via OAuth ({self._auth_provider})\n"
                f"  User: {display}\n"
                f"  Role: {self._auth_role}\n"
                f"  User ID: {self._auth_user}"
            )
        else:
            self.server_io.append_info("Authentication: API key")

    # ── Result display ────────────────────────────────────────

    def _display_tool_result(self, response: dict) -> None:
        """Extract and display tool results from a JSON-RPC response."""
        if self._verbose:
            self.server_io.append_json(response)
            return

        if "error" in response:
            err = response["error"]
            self.server_io.append_error(
                f"JSON-RPC error {err.get('code', '?')}: {err.get('message', '')}"
            )
            return

        result = response.get("result", {})
        content_list = result.get("content", [])
        is_error = result.get("isError", False) if isinstance(result, dict) else False

        parts: list[str] = []
        for item in content_list:
            text = item.get("text", "")
            if text:
                parts.append(text)

        combined = "\n".join(parts) if parts else ""

        # Try to unpack server interpret response JSON
        try:
            data = json.loads(combined)
            if isinstance(data, dict) and "output" in data:
                output = data.get("output", "")
                errors = data.get("errors", "")
                stack = data.get("stack", [])
                stack_str = json.dumps(stack) if stack else ""
                self.server_io.append_result(
                    output=output, errors=errors, stack=stack_str,
                )
                if errors:
                    self._notify(NotificationType.ERROR, errors)
                return
        except (json.JSONDecodeError, TypeError):
            pass

        # Fallback: plain text (non-JSON responses from other tools)
        if is_error:
            self.server_io.append_result(errors=combined)
        else:
            self.server_io.append_result(output=combined)

    # ── Callbacks ─────────────────────────────────────────────

    def _on_server_notification(self, data: dict) -> None:
        """Called by protocol for server-initiated notifications."""
        method = data.get("method", "")
        if method == "notifications/message":
            params = data.get("params", {})
            message = str(params.get("data", ""))
            if message:
                self._notify(NotificationType.INFO, message)

    def _on_jsonrpc_message(self, msg: JsonRpcMessage) -> None:
        """Called by protocol for every JSON-RPC message (in/out)."""
        self.jsonrpc_panel.append_message(msg)

    def _on_diagnostic(self, line: str) -> None:
        """Called by transport for diagnostic messages."""
        self._notify(NotificationType.WARNING, line)

    def _on_transport_close(self) -> None:
        """Called when the transport closes."""
        self._connected = False
        self._protocol.cancel_all()
        self._notify(
            NotificationType.CONNECTION,
            "HTTP session closed",
        )

    # ── Actions ───────────────────────────────────────────────

    def action_toggle_layout(self) -> None:
        """Cycle layout: both → interpreter only → JSON-RPC only → both."""
        self.screen.remove_class("io-only", "rpc-only")
        self._layout_mode = (self._layout_mode + 1) % 3
        if self._layout_mode == 1:
            self.screen.add_class("io-only")
        elif self._layout_mode == 2:
            self.screen.add_class("rpc-only")
        # Restore command input focus when ServerIOPanel is visible
        if self._layout_mode != 2:
            self.server_io.input_widget.focus()

    def action_toggle_notifications(self) -> None:
        """Toggle notification bar between bottom strip and fullscreen."""
        self._notif_fullscreen = not self._notif_fullscreen
        if self._notif_fullscreen:
            self.screen.add_class("notif-fullscreen")
        else:
            self.screen.remove_class("notif-fullscreen")
        self.notification_bar._refresh_display()
        if self._notif_fullscreen:
            # Enter fullscreen — snap to newest with autoscroll
            self.notification_bar.scroll_to_newest()
        else:
            self.server_io._refresh_display()
            self.jsonrpc_panel._refresh_display()
            self.server_io.input_widget.focus()

    def action_notif_scroll_toggle(self) -> None:
        """Toggle notification log between top (newest, autoscroll) and bottom (oldest)."""
        self.notification_bar.toggle_scroll_position()

    def action_show_help(self) -> None:
        """Open the help browser."""
        if self._connected:
            self.push_screen(HelpScreen(self._protocol, config=self._config))
        else:
            self.server_io.append_info(HELP_TEXT)

    def deliver_screenshot(
        self,
        filename: str | None = None,
        path: str | None = None,
        time_format: str | None = None,
    ) -> str | None:
        """Save screenshot to ~/.etil/screenshots/ (WSL2-friendly, no browser)."""
        screenshot_dir = os.path.expanduser("~/.etil/screenshots")
        os.makedirs(screenshot_dir, exist_ok=True)
        try:
            saved = self.save_screenshot(
                filename=filename, path=screenshot_dir, time_format=time_format,
            )
            self._notify(NotificationType.SUCCESS, f"Screenshot: {os.path.basename(saved)}")
            return saved
        except Exception as exc:
            self._notify(NotificationType.ERROR, f"Screenshot failed: {exc}")
            return None

    def action_dismiss_notification(self) -> None:
        self.notification_bar.dismiss_latest()

    def action_clear_output(self) -> None:
        self.server_io.clear_log()

    def action_focus_command(self) -> None:
        if self.server_io._search_active:
            self.server_io._exit_search_mode(accept=False)
        else:
            self.server_io.input_widget.focus()

    # ── Session resilience ─────────────────────────────────────

    @staticmethod
    def _is_session_expired(response: dict) -> bool:
        """Check if a response indicates an expired HTTP session."""
        err = response.get("error")
        if isinstance(err, dict) and err.get("code") == SESSION_EXPIRED_CODE:
            return True
        return False

    async def _reconnect(self) -> bool:
        """Re-initialize the MCP session after expiry. Returns True on success."""
        self._notify(NotificationType.CONNECTION, "Session expired, reconnecting...")
        try:
            await self._initialize_session()
            return self._connected
        except Exception as exc:
            self._notify(NotificationType.ERROR, f"Reconnect failed: {exc}")
            self._connected = False
            return False

    # ── Helpers ───────────────────────────────────────────────

    async def _fetch_word_list(self) -> None:
        """Fetch dictionary words from MCP server and update the completer."""
        try:
            response = await self._protocol.call_tool("list_words")
            result = response.get("result", {})
            content_list = result.get("content", [])
            for item in content_list:
                text = item.get("text", "")
                if not text:
                    continue
                data = json.loads(text)
                if isinstance(data, dict) and "words" in data:
                    names = [w["name"] for w in data["words"] if "name" in w]
                    self._completer.set_words(names)
                    return
        except Exception:
            pass  # Silently ignore — completion just won't work

    def _notify(self, ntype: NotificationType, message: str) -> None:
        self.notification_bar.add_notification(
            Notification(message=message, type=ntype)
        )
