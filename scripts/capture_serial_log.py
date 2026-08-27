#!/usr/bin/env python3
"""Capture the ESP32-C3 native USB CDC stream without toggling reset lines."""

from __future__ import annotations

import argparse
from pathlib import Path
import time

import serial  # type: ignore  # noqa: E402


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--seconds", type=float, default=180)
    parser.add_argument("--log", type=Path, required=True)
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--restart-first", action="store_true")
    action.add_argument("--next-page-first", action="store_true")
    action.add_argument("--refresh-first", action="store_true")
    action.add_argument("--ota-check-first", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    log_path = args.log.resolve()
    log_path.parent.mkdir(parents=True, exist_ok=True)
    deadline = time.monotonic() + args.seconds
    handle = None
    action_sent = False
    with log_path.open("a", encoding="utf-8") as log:
        while time.monotonic() < deadline:
            if handle is None:
                try:
                    handle = serial.Serial()
                    handle.port = args.port
                    handle.baudrate = args.baud
                    handle.timeout = 0.2
                    handle.write_timeout = 0.2
                    handle.dtr = False
                    handle.rts = False
                    handle.open()
                    marker = f"\n[host {time.strftime('%Y-%m-%d %H:%M:%S')}] {args.port} opened\n"
                    log.write(marker)
                    log.flush()
                    if (
                        args.restart_first
                        or args.next_page_first
                        or args.refresh_first
                        or args.ota_check_first
                    ) and not action_sent:
                        if args.restart_first:
                            command = b"INKDASH_RESTART_V1\n"
                            marker = "[host] normal restart requested\n"
                        elif args.next_page_first:
                            command = b"INKDASH_PAGE_NEXT_V1\n"
                            marker = "[host] next page requested\n"
                        elif args.ota_check_first:
                            command = b"INKDASH_OTA_CHECK_V1\n"
                            marker = "[host] signed OTA check requested\n"
                        else:
                            command = b"INKDASH_REFRESH_V1\n"
                            marker = "[host] current page refresh requested\n"
                        handle.write(command)
                        handle.flush()
                        action_sent = True
                        log.write(marker)
                        log.flush()
                except (OSError, serial.SerialException):
                    if handle is not None:
                        try:
                            handle.close()
                        except Exception:
                            pass
                    handle = None
                    time.sleep(0.05)
                    continue
            try:
                data = handle.read(handle.in_waiting or 1)
                if data:
                    text = data.decode("utf-8", errors="replace")
                    log.write(text)
                    log.flush()
            except (OSError, serial.SerialException):
                try:
                    handle.close()
                except Exception:
                    pass
                handle = None
        if handle is not None:
            handle.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
