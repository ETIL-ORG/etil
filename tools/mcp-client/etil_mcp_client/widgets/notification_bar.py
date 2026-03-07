# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
"""Bottom bar: sticky notifications, color-coded by type.

Notifications render in chronological order (oldest at top, newest at bottom).
RichLog auto_scroll keeps the newest visible.  Rate-limited display (0.25s
between each new notification) so rapid bursts don't flash by invisibly.
New notifications are appended incrementally — no clear+rewrite unless a
resize or capacity trim requires it.
"""

from __future__ import annotations

import time

from rich.text import Text
from textual.app import ComposeResult
from textual.timer import Timer
from textual.widget import Widget
from textual.widgets import RichLog

from ..models import Notification, NotificationType

_STYLE_MAP: dict[NotificationType, str] = {
    NotificationType.INFO: "cyan",
    NotificationType.SUCCESS: "bold green",
    NotificationType.WARNING: "bold yellow",
    NotificationType.ERROR: "bold red",
    NotificationType.CONNECTION: "bold magenta",
}

_DRIP_INTERVAL: float = 0.25  # seconds between displayed notifications
_RESIZE_DEBOUNCE: float = 0.15  # seconds to wait for resize events to settle
_REFRESH_COOLDOWN: float = 0.3  # seconds to suppress re-renders after a refresh


class NotificationBar(Widget):
    """Bottom bar showing color-coded notifications (chronological order)."""

    def __init__(self, max_notifications: int = 100, **kwargs) -> None:
        super().__init__(**kwargs)
        self._max = max_notifications
        self._notifications: list[Notification] = []
        self._pending_queue: list[Notification] = []
        self._display_timer: Timer | None = None
        self._last_width: int = 0
        self._resize_timer: Timer | None = None
        self._cooldown_until: float = 0
        self._autoscroll = True  # True = viewing newest, autoscroll on

    def compose(self) -> ComposeResult:
        yield RichLog(
            highlight=False,
            markup=True,
            wrap=True,
            auto_scroll=True,
            id="notification-log",
        )

    @property
    def log_widget(self) -> RichLog:
        return self.query_one("#notification-log", RichLog)

    def add_notification(self, notification: Notification) -> None:
        """Queue a notification for rate-limited display."""
        self._pending_queue.append(notification)
        if self._display_timer is None:
            # Display the first one immediately, then drip-feed the rest
            self._drip_one()
            if self._pending_queue:
                self._display_timer = self.set_interval(
                    _DRIP_INTERVAL, self._drip_one
                )

    def dismiss_latest(self) -> None:
        """Remove the most recent notification (newest = last in list)."""
        if self._notifications:
            self._notifications.pop()
            self._full_refresh()

    def scroll_to_newest(self) -> None:
        """Snap to newest notifications and re-enable autoscroll."""
        self._autoscroll = True
        log = self.log_widget
        log.auto_scroll = True
        log.scroll_end(animate=False)

    def toggle_scroll_position(self) -> None:
        """Toggle between newest (bottom, autoscroll) and oldest (top)."""
        log = self.log_widget
        if self._autoscroll:
            self._autoscroll = False
            log.auto_scroll = False
            log.scroll_home(animate=False)
        else:
            self._autoscroll = True
            log.auto_scroll = True
            log.scroll_end(animate=False)

    def _drip_one(self) -> None:
        """Move one notification from pending queue into the display list."""
        if not self._pending_queue:
            if self._display_timer is not None:
                self._display_timer.stop()
                self._display_timer = None
            return

        notification = self._pending_queue.pop(0)
        self._notifications.append(notification)

        # Trim oldest when over capacity
        trimmed = False
        while len(self._notifications) > self._max:
            self._notifications.pop(0)
            trimmed = True

        if trimmed:
            # Removed lines from the beginning — need full rewrite
            self._full_refresh()
        else:
            # Incremental: just append the new notification
            self._write_one(notification)

        # Stop timer if queue is now empty
        if not self._pending_queue and self._display_timer is not None:
            self._display_timer.stop()
            self._display_timer = None

    def _write_one(self, n: Notification) -> None:
        """Append a single notification to the log."""
        ts = n.timestamp.strftime("%H:%M:%S")
        style = _STYLE_MAP.get(n.type, "white")
        self.log_widget.write(Text(f"[{ts}] {n.message}", style=style))

    def _full_refresh(self) -> None:
        """Clear and re-render all notifications (used on resize and trim)."""
        log = self.log_widget
        saved_y = log.scroll_y

        # Suppress auto_scroll during rewrite to avoid intermediate jumps
        log.auto_scroll = False
        log.clear()
        for n in self._notifications:
            self._write_one(n)

        if self._autoscroll:
            log.auto_scroll = True
            log.scroll_end(animate=False)
        else:
            def _restore() -> None:
                log.scroll_y = saved_y
            self.call_after_refresh(_restore)

        self._cooldown_until = time.monotonic() + _REFRESH_COOLDOWN

    # Keep the old name as an alias — other code calls _refresh_display
    _refresh_display = _full_refresh

    def _debounced_refresh(self) -> None:
        """Timer callback: perform the deferred refresh."""
        self._resize_timer = None
        self._full_refresh()

    def on_resize(self, event) -> None:
        """Re-render notifications when panel width changes (debounced)."""
        if event.size.width != self._last_width:
            self._last_width = event.size.width
            if self._notifications and time.monotonic() >= self._cooldown_until:
                if self._resize_timer is not None:
                    self._resize_timer.stop()
                self._resize_timer = self.set_timer(
                    _RESIZE_DEBOUNCE, self._debounced_refresh
                )
