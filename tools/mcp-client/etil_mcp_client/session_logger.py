# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
"""Session logging — plain-text and JSONL output for TUI sessions."""

from __future__ import annotations

import glob
import json
import logging
import os
from datetime import datetime
from typing import TextIO

log = logging.getLogger(__name__)


class SessionLogger:
    """Writes TUI session entries to plain-text and/or JSONL files."""

    def __init__(self) -> None:
        self._text_file: TextIO | None = None
        self._json_file: TextIO | None = None
        self._text_path: str | None = None
        self._json_path: str | None = None

    # ── Public API ────────────────────────────────────────────

    def toggle_text(self, path: str | None = None) -> tuple[bool, str]:
        """Toggle plain-text log. Returns (now_active, path)."""
        if self._text_file is not None:
            return self._close_text()
        return self._open_text(path or self._default_path("log"))

    def toggle_json(self, path: str | None = None) -> tuple[bool, str]:
        """Toggle JSONL log. Returns (now_active, path)."""
        if self._json_file is not None:
            return self._close_json()
        return self._open_json(path or self._default_path("json"))

    def toggle_both(self) -> list[tuple[bool, str]]:
        """Toggle both logs with a shared timestamp.

        If either is active, close all.  If both off, open both.
        """
        if self._text_file is not None or self._json_file is not None:
            results: list[tuple[bool, str]] = []
            if self._text_file is not None:
                results.append(self._close_text())
            if self._json_file is not None:
                results.append(self._close_json())
            return results

        text_path, json_path = self._shared_default_paths()
        return [
            self._open_text(text_path),
            self._open_json(json_path),
        ]

    def on_entry(self, entry: tuple) -> None:
        """Callback fired by ServerIOPanel for every appended entry."""
        ts = datetime.now().strftime("%Y-%m-%dT%H:%M:%S")
        if self._text_file is not None:
            try:
                self._write_text(ts, entry)
            except OSError:
                log.exception("Text log write failed, closing file")
                self._close_text()
        if self._json_file is not None:
            try:
                self._write_json(ts, entry)
            except OSError:
                log.exception("JSON log write failed, closing file")
                self._close_json()

    def close_all(self) -> list[str]:
        """Close all open log files. Returns list of closed paths."""
        closed: list[str] = []
        if self._text_file is not None:
            _, path = self._close_text()
            closed.append(path)
        if self._json_file is not None:
            _, path = self._close_json()
            closed.append(path)
        return closed

    def start_both(self) -> list[tuple[bool, str]]:
        """Unconditionally open both log files (for startup use).

        Unlike toggle_both(), this never closes — it always opens.
        """
        text_path, json_path = self._shared_default_paths()
        return [
            self._open_text(text_path),
            self._open_json(json_path),
        ]

    @staticmethod
    def rotate_logs(max_keep: int = 5) -> int:
        """Delete old TUI log files, keeping the most recent `max_keep` of each type.

        Returns the total number of files deleted.
        """
        deleted = 0
        for ext in ("log", "json"):
            files = sorted(glob.glob(f"/tmp/*-tui.{ext}"))
            if len(files) > max_keep:
                for old_file in files[:-max_keep]:
                    try:
                        os.remove(old_file)
                        deleted += 1
                    except OSError:
                        pass
        return deleted

    # ── Path helpers ──────────────────────────────────────────

    @staticmethod
    def _default_path(ext: str) -> str:
        ts = datetime.now().strftime("%Y%m%dT%H%M%S")
        return f"/tmp/{ts}-tui.{ext}"

    @staticmethod
    def _shared_default_paths() -> tuple[str, str]:
        ts = datetime.now().strftime("%Y%m%dT%H%M%S")
        return f"/tmp/{ts}-tui.log", f"/tmp/{ts}-tui.json"

    # ── Open / close ──────────────────────────────────────────

    def _open_text(self, path: str) -> tuple[bool, str]:
        self._text_file = open(path, "a", encoding="utf-8")
        self._text_path = path
        return (True, path)

    def _close_text(self) -> tuple[bool, str]:
        path = self._text_path or ""
        if self._text_file is not None:
            self._text_file.close()
            self._text_file = None
        self._text_path = None
        return (False, path)

    def _open_json(self, path: str) -> tuple[bool, str]:
        self._json_file = open(path, "a", encoding="utf-8")
        self._json_path = path
        return (True, path)

    def _close_json(self) -> tuple[bool, str]:
        path = self._json_path or ""
        if self._json_file is not None:
            self._json_file.close()
            self._json_file = None
        self._json_path = None
        return (False, path)

    # ── Writers ───────────────────────────────────────────────

    def _write_text(self, ts: str, entry: tuple) -> None:
        assert self._text_file is not None
        tag = entry[0]
        lines: list[str] = []

        if tag == "command":
            lines.append(f"{ts} > {entry[1]}")
        elif tag == "result":
            output, errors, stack = entry[1], entry[2], entry[3]
            if output:
                for ln in output.rstrip().split("\n"):
                    lines.append(f"{ts}   {ln}")
            if errors:
                for ln in errors.rstrip().split("\n"):
                    lines.append(f"{ts}   [error] {ln}")
            if stack:
                for ln in stack.split("\n"):
                    lines.append(f"{ts}   [stack] {ln}")
        elif tag == "info":
            for ln in entry[1].split("\n"):
                lines.append(f"{ts}   [info] {ln}")
        elif tag == "error":
            for ln in entry[1].split("\n"):
                lines.append(f"{ts}   [error] {ln}")
        elif tag == "json":
            compact = json.dumps(entry[1], separators=(",", ":"))
            lines.append(f"{ts}   [json] {compact}")

        for line in lines:
            self._text_file.write(line + "\n")
        self._text_file.flush()

    def _write_json(self, ts: str, entry: tuple) -> None:
        assert self._json_file is not None
        tag = entry[0]
        record: dict

        if tag == "command":
            record = {"ts": ts, "type": "command", "command": entry[1]}
        elif tag == "result":
            record = {
                "ts": ts,
                "type": "result",
                "output": entry[1],
                "errors": entry[2],
                "stack": entry[3],
            }
        elif tag == "info":
            record = {"ts": ts, "type": "info", "text": entry[1]}
        elif tag == "error":
            record = {"ts": ts, "type": "error", "text": entry[1]}
        elif tag == "json":
            record = {"ts": ts, "type": "json", "data": entry[1]}
        else:
            return

        self._json_file.write(json.dumps(record, separators=(",", ":")) + "\n")
        self._json_file.flush()
