# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
"""Left panel: read-only JSON-RPC message log with syntax highlighting."""

from __future__ import annotations

import json
import time

from rich.syntax import Syntax
from rich.text import Text
from textual.app import ComposeResult
from textual.timer import Timer
from textual.widget import Widget
from textual.widgets import RichLog, Static

from ..models import Direction, JsonRpcMessage

_RESIZE_DEBOUNCE: float = 0.15  # seconds to wait for resize events to settle
_REFRESH_COOLDOWN: float = 0.3  # seconds to suppress re-renders after a refresh


class JsonRpcPanel(Widget):
    """Read-only panel displaying JSON-RPC traffic."""

    def __init__(self, **kwargs) -> None:
        super().__init__(**kwargs)
        self._messages: list[JsonRpcMessage] = []
        self._last_width: int = 0
        self._refresh_timer: Timer | None = None
        self._cooldown_until: float = 0

    def compose(self) -> ComposeResult:
        yield Static("JSON-RPC Messages", classes="panel-title")
        yield RichLog(highlight=True, markup=True, wrap=True, id="jsonrpc-log")

    @property
    def log_widget(self) -> RichLog:
        return self.query_one("#jsonrpc-log", RichLog)

    def append_message(self, msg: JsonRpcMessage) -> None:
        """Add a JSON-RPC message to the log."""
        self._messages.append(msg)
        self._write_message(msg)

    def _write_message(self, msg: JsonRpcMessage) -> None:
        """Render a single message to the RichLog."""
        log = self.log_widget
        ts = msg.timestamp.strftime("%H:%M:%S")

        if msg.direction == Direction.OUTGOING:
            header = Text(f"[{ts}] → ", style="bold cyan")
        elif msg.is_error:
            header = Text(f"[{ts}] ← ", style="bold red")
        else:
            header = Text(f"[{ts}] ← ", style="bold green")

        log.write(header)

        pretty = json.dumps(msg.data, indent=2)
        syntax = Syntax(pretty, "json", theme="monokai", word_wrap=True)
        log.write(syntax)
        log.write("")  # blank line separator

    def _refresh_display(self) -> None:
        """Re-render all messages from buffer with scroll stability."""
        log = self.log_widget
        saved_y = log.scroll_y
        was_at_end = log.scroll_y >= log.max_scroll_y

        log.clear()
        for msg in self._messages:
            self._write_message(msg)

        if was_at_end:
            log.scroll_end(animate=False)
        else:
            def _restore() -> None:
                log.scroll_y = saved_y
            self.call_after_refresh(_restore)

        self._cooldown_until = time.monotonic() + _REFRESH_COOLDOWN

    def _debounced_refresh(self) -> None:
        """Timer callback: perform the deferred refresh."""
        self._refresh_timer = None
        self._refresh_display()

    def on_resize(self, event) -> None:
        """Re-render content when panel width changes (debounced)."""
        if event.size.width != self._last_width:
            self._last_width = event.size.width
            if self._messages and time.monotonic() >= self._cooldown_until:
                if self._refresh_timer is not None:
                    self._refresh_timer.stop()
                self._refresh_timer = self.set_timer(
                    _RESIZE_DEBOUNCE, self._debounced_refresh
                )

    def clear_log(self) -> None:
        self._messages.clear()
        self.log_widget.clear()
