# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
"""Right panel: interpreter output display + command input with history."""

from __future__ import annotations

import json
import os
import time
from collections.abc import Callable

from rich.syntax import Syntax
from rich.text import Text
from textual.app import ComposeResult
from textual.timer import Timer
from textual.widget import Widget
from textual.widgets import Input, RichLog, Static

from ..completer import CompletionOverlay

_RESIZE_DEBOUNCE: float = 0.15  # seconds to wait for resize events to settle
_REFRESH_COOLDOWN: float = 0.3  # seconds to suppress re-renders after a refresh
_MAX_HISTORY: int = 500


class ServerIOPanel(Widget):
    """Primary panel showing interpreter results and command input."""

    def __init__(self, completer: CompletionOverlay | None = None, **kwargs) -> None:
        super().__init__(**kwargs)
        self._history: list[str] = []
        self._history_index: int = -1
        self._entries: list[tuple] = []
        self._last_width: int = 0
        self._refresh_timer: Timer | None = None
        self._cooldown_until: float = 0
        self.on_log_entry: Callable[[tuple], None] | None = None
        self._completer: CompletionOverlay | None = completer

        # Ctrl+R reverse history search state
        self._search_active: bool = False
        self._saved_input: str = ""
        self._search_matches: list[int] = []  # history indices, most recent first
        self._search_match_idx: int = -1

    def compose(self) -> ComposeResult:
        yield Static("Server I/O", classes="panel-title")
        yield RichLog(highlight=True, markup=True, wrap=True, id="server-log")
        yield Static("", id="search-hint")
        if self._completer is not None:
            yield self._completer
        yield Input(
            placeholder="etil> ",
            id="command-input",
        )

    @property
    def log_widget(self) -> RichLog:
        return self.query_one("#server-log", RichLog)

    @property
    def input_widget(self) -> Input:
        return self.query_one("#command-input", Input)

    @property
    def search_hint(self) -> Static:
        return self.query_one("#search-hint", Static)

    def _fire_log(self, entry: tuple) -> None:
        """Invoke the log callback if registered."""
        if self.on_log_entry is not None:
            self.on_log_entry(entry)

    # ── Token extraction and completion helpers ───────────────

    def _current_token(self) -> str:
        """Extract the last whitespace-delimited token from the input."""
        value = self.input_widget.value
        if not value:
            return ""
        stripped = value.rstrip()
        if not stripped:
            return ""
        last_space = stripped.rfind(" ")
        if last_space == -1:
            return stripped
        return stripped[last_space + 1:]

    def _apply_completion(self, word: str) -> None:
        """Replace the last token in the input with the completed word."""
        inp = self.input_widget
        value = inp.value
        stripped = value.rstrip()
        last_space = stripped.rfind(" ")
        if last_space == -1:
            inp.value = word
        else:
            inp.value = stripped[:last_space + 1] + word
        inp.cursor_position = len(inp.value)

    def _accept_completion(self) -> None:
        """Accept the highlighted completion from the popup."""
        if self._completer is None:
            return
        text = self._completer.selected_text()
        if text:
            self._apply_completion(text)
        self._completer.hide()

    # ── Persistent history ────────────────────────────────────

    def load_history(self, path: str) -> None:
        """Load command history from file. One command per line."""
        expanded = os.path.expanduser(path)
        try:
            with open(expanded, "r") as f:
                lines = [line.rstrip("\n") for line in f if line.strip()]
        except OSError:
            return

        # Deduplicate consecutive entries
        deduped: list[str] = []
        for line in lines:
            if not deduped or deduped[-1] != line:
                deduped.append(line)

        self._history = deduped[-_MAX_HISTORY:]

    def save_history(self, path: str) -> None:
        """Save command history to file. Silently ignores I/O errors."""
        try:
            expanded = os.path.expanduser(path)
            os.makedirs(os.path.dirname(expanded), exist_ok=True)

            # Deduplicate consecutive entries and cap
            deduped: list[str] = []
            for cmd in self._history:
                if not deduped or deduped[-1] != cmd:
                    deduped.append(cmd)
            deduped = deduped[-_MAX_HISTORY:]

            with open(expanded, "w") as f:
                for cmd in deduped:
                    f.write(cmd + "\n")
        except OSError:
            pass

    # ── Command submission ────────────────────────────────────

    def submit_command(self, command: str) -> None:
        """Echo command and record in history. Called by the App."""
        self._history.append(command)
        self._history_index = -1
        self._entries.append(("command", command))
        self._fire_log(self._entries[-1])
        self.log_widget.write(Text(f"etil> {command}", style="bold"))
        self.input_widget.value = ""

    # ── Key handling ──────────────────────────────────────────

    def on_key(self, event) -> None:
        """Handle Tab completion, popup navigation, history, and Ctrl+R."""
        inp = self.input_widget
        if not inp.has_focus:
            return

        # Ctrl+R: enter search mode or cycle to older match
        if event.key == "ctrl+r":
            event.prevent_default()
            event.stop()
            # Hide completion popup if visible
            if self._completer and self._completer.is_visible:
                self._completer.hide()
            if self._search_active:
                self._search_cycle_older()
            else:
                self._enter_search_mode()
            return

        # While in search mode, handle special keys
        if self._search_active:
            if event.key == "up" or event.key == "down":
                event.prevent_default()
                event.stop()
                self._exit_search_mode(accept=True)
                return
            if event.key == "escape":
                event.prevent_default()
                event.stop()
                self._exit_search_mode(accept=False)
                return
            # Let other keys (typing, backspace) through to update input
            return

        # Tab: trigger or accept completion
        if event.key == "tab" and self._completer is not None:
            if self._completer.is_visible:
                # Accept highlighted completion
                self._accept_completion()
                event.prevent_default()
                event.stop()
                return
            else:
                # Try to trigger completion
                token = self._current_token()
                if token:
                    matches = self._completer.get_matches(token)
                    if len(matches) == 1:
                        self._apply_completion(matches[0])
                        event.prevent_default()
                        event.stop()
                        return
                    elif len(matches) > 1:
                        self._completer.show_completions(matches)
                        event.prevent_default()
                        event.stop()
                        return
                # 0 matches or empty token: let Tab fall through (focus next)

        # When popup visible, intercept navigation keys
        if self._completer is not None and self._completer.is_visible:
            if event.key == "up":
                self._completer.move_highlight(-1)
                event.prevent_default()
                event.stop()
                return
            elif event.key == "down":
                self._completer.move_highlight(1)
                event.prevent_default()
                event.stop()
                return
            elif event.key == "enter":
                self._accept_completion()
                event.prevent_default()
                event.stop()
                return
            elif event.key == "escape":
                self._completer.hide()
                event.prevent_default()
                event.stop()
                return
            # Any other key: let it through (typing narrows via on_input_changed)
            return

        # Normal mode: Up/Down history navigation
        if not self._history:
            return

        if event.key == "up":
            event.prevent_default()
            if self._history_index == -1:
                self._history_index = len(self._history) - 1
            elif self._history_index > 0:
                self._history_index -= 1
            inp.value = self._history[self._history_index]
            inp.cursor_position = len(inp.value)

        elif event.key == "down":
            event.prevent_default()
            if self._history_index == -1:
                return
            if self._history_index < len(self._history) - 1:
                self._history_index += 1
                inp.value = self._history[self._history_index]
            else:
                self._history_index = -1
                inp.value = ""
            inp.cursor_position = len(inp.value)

    # ── Ctrl+R reverse history search ─────────────────────────

    def _enter_search_mode(self) -> None:
        """Activate reverse-i-search: save input, clear it, show hint."""
        if not self._history:
            return
        self._search_active = True
        self._saved_input = self.input_widget.value
        self._search_matches = []
        self._search_match_idx = -1

        self.input_widget.value = ""
        self.input_widget.placeholder = "reverse-i-search: "

        self._update_search_hint("")
        self.search_hint.add_class("visible")

    def _do_search(self, query: str) -> None:
        """Find history entries containing query (case-insensitive)."""
        if not query:
            self._search_matches = []
            self._search_match_idx = -1
            return

        q = query.lower()
        # Search from most recent to oldest
        self._search_matches = [
            i for i in range(len(self._history) - 1, -1, -1)
            if q in self._history[i].lower()
        ]
        # Reset to first (most recent) match
        self._search_match_idx = 0 if self._search_matches else -1

    def _update_search_hint(self, query: str) -> None:
        """Update the search hint static with current match state."""
        if self._search_match_idx >= 0 and self._search_match_idx < len(self._search_matches):
            idx = self._search_matches[self._search_match_idx]
            match_text = self._history[idx]
            self.search_hint.update(
                f"(reverse-i-search)'{query}': {match_text}"
            )
        elif query:
            self.search_hint.update(
                f"(failing reverse-i-search)'{query}: "
            )
        else:
            self.search_hint.update("(reverse-i-search)'': ")

    def _search_cycle_older(self) -> None:
        """Advance to next older match (wraps around)."""
        if not self._search_matches:
            return
        self._search_match_idx = (
            (self._search_match_idx + 1) % len(self._search_matches)
        )
        query = self.input_widget.value
        self._update_search_hint(query)

    def _exit_search_mode(self, accept: bool) -> None:
        """Leave search mode. If accept, put match in input; else restore."""
        self._search_active = False
        self.search_hint.remove_class("visible")
        self.search_hint.update("")

        self.input_widget.placeholder = "etil> "

        if accept and self._search_match_idx >= 0 and self._search_match_idx < len(self._search_matches):
            idx = self._search_matches[self._search_match_idx]
            self.input_widget.value = self._history[idx]
        else:
            self.input_widget.value = self._saved_input

        self.input_widget.cursor_position = len(self.input_widget.value)

        self._search_matches = []
        self._search_match_idx = -1
        self._saved_input = ""

    def on_input_changed(self, event: Input.Changed) -> None:
        """Update search results or re-filter completion popup as user types."""
        if event.input.id != "command-input":
            return

        # Reverse search mode: update search results
        if self._search_active:
            query = event.value
            self._do_search(query)
            self._update_search_hint(query)
            return

        # Completion popup visible: re-filter as user types
        if self._completer is not None and self._completer.is_visible:
            token = self._current_token()
            if token:
                matches = self._completer.get_matches(token)
                if matches:
                    self._completer.show_completions(matches)
                else:
                    self._completer.hide()
            else:
                self._completer.hide()

    def on_input_submitted(self, event: Input.Submitted) -> None:
        """Intercept Enter during search: accept match, don't auto-execute."""
        if not self._search_active:
            return
        if event.input.id != "command-input":
            return
        event.stop()
        self._exit_search_mode(accept=True)

    # ── Display methods ───────────────────────────────────────

    def append_result(
        self,
        output: str = "",
        errors: str = "",
        stack: str = "",
    ) -> None:
        """Display interpret results."""
        self._entries.append(("result", output, errors, stack))
        self._fire_log(self._entries[-1])
        self._write_result(output, errors, stack)

    def _write_result(
        self, output: str, errors: str, stack: str
    ) -> None:
        """Render a single result entry to the RichLog."""
        log = self.log_widget
        if output:
            log.write(Text(output.rstrip(), style="green"))
        if errors:
            log.write(Text(errors.rstrip(), style="bold red"))
        if stack:
            log.write(Text(stack, style="yellow"))

    def append_json(self, data: dict) -> None:
        """Display a JSON response with syntax highlighting."""
        self._entries.append(("json", data))
        self._fire_log(self._entries[-1])
        self._write_json(data)

    def _write_json(self, data: dict) -> None:
        """Render a single JSON entry to the RichLog."""
        pretty = json.dumps(data, indent=2)
        syntax = Syntax(pretty, "json", theme="monokai", word_wrap=True)
        self.log_widget.write(syntax)

    def append_info(self, text: str) -> None:
        """Display informational text."""
        self._entries.append(("info", text))
        self._fire_log(self._entries[-1])
        self.log_widget.write(Text(text, style="cyan"))

    def append_error(self, text: str) -> None:
        """Display an error message."""
        self._entries.append(("error", text))
        self._fire_log(self._entries[-1])
        self.log_widget.write(Text(text, style="bold red"))

    def _write_entry(self, entry: tuple) -> None:
        """Render one tagged entry to the RichLog."""
        tag = entry[0]
        if tag == "command":
            self.log_widget.write(Text(f"etil> {entry[1]}", style="bold"))
        elif tag == "result":
            self._write_result(entry[1], entry[2], entry[3])
        elif tag == "json":
            self._write_json(entry[1])
        elif tag == "info":
            self.log_widget.write(Text(entry[1], style="cyan"))
        elif tag == "error":
            self.log_widget.write(Text(entry[1], style="bold red"))

    def _refresh_display(self) -> None:
        """Re-render all entries from buffer with scroll stability."""
        log = self.log_widget
        saved_y = log.scroll_y
        was_at_end = log.scroll_y >= log.max_scroll_y

        log.clear()
        for entry in self._entries:
            self._write_entry(entry)

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
            if self._entries and time.monotonic() >= self._cooldown_until:
                if self._refresh_timer is not None:
                    self._refresh_timer.stop()
                self._refresh_timer = self.set_timer(
                    _RESIZE_DEBOUNCE, self._debounced_refresh
                )

    def clear_log(self) -> None:
        self._entries.clear()
        self.log_widget.clear()
