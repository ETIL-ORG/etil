# Copyright (c) 2026 Mark Deazley. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
"""Data models for JSON-RPC messages and notifications."""

from __future__ import annotations

import enum
from dataclasses import dataclass, field
from datetime import datetime


class Direction(enum.Enum):
    """Message direction."""
    OUTGOING = "outgoing"
    INCOMING = "incoming"


class NotificationType(enum.Enum):
    """Notification severity level."""
    INFO = "info"
    SUCCESS = "success"
    WARNING = "warning"
    ERROR = "error"
    CONNECTION = "connection"


@dataclass
class JsonRpcMessage:
    """A logged JSON-RPC message."""
    direction: Direction
    data: dict
    timestamp: datetime = field(default_factory=datetime.now)

    @property
    def is_error(self) -> bool:
        return "error" in self.data


@dataclass
class Notification:
    """A notification for the bottom bar."""
    message: str
    type: NotificationType = NotificationType.INFO
    timestamp: datetime = field(default_factory=datetime.now)
