#!/usr/bin/env python3
"""Proton-based AMQP 1.0 subscriber for the Artemis E2E test.

Usage: amqp_subscriber.py <url> <address>

Emits received message subject, codec, and header details so the
harness can grep the output for Session-Hmac / Msg-Codec presence.
"""

import sys
import time

from proton.handlers import MessagingHandler
from proton.reactor import Container


class Subscriber(MessagingHandler):
    def __init__(self, url: str, address: str):
        super().__init__()
        self.url = url
        self.address = address

    def on_start(self, event):
        conn = event.container.connect(self.url)
        event.container.create_receiver(conn, self.address)
        print(f"connected to {self.url} address={self.address}", flush=True)

    def on_message(self, event):
        m = event.message
        subject = m.subject or m.address or "<none>"
        ct = m.content_type or "<none>"
        props = dict(m.properties or {})
        print(f"subject={subject} content-type={ct}", flush=True)
        for k in (
            "Session-Hmac",
            "Msg-Host",
            "Msg-Startup",
            "Msg-Seq",
            "Msg-OriginType",
            "Msg-Codec",
            "Msg-RouteTrace",
            "Msg-HopsLeft",
        ):
            if k in props:
                print(f"  {k}: {props[k]}", flush=True)
        print(f"  body: {m.body!r}", flush=True)


def main():
    if len(sys.argv) < 3:
        print("usage: amqp_subscriber.py <url> <address>", file=sys.stderr)
        return 1
    Container(Subscriber(sys.argv[1], sys.argv[2])).run()


if __name__ == "__main__":
    sys.exit(main() or 0)
