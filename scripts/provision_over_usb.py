#!/usr/bin/env python3
"""Provision InkDash over USB CDC using a saved Windows WLAN profile.

The WPA key is read locally, sent directly to the ESP32, and never printed.
"""

from __future__ import annotations

import argparse
from getpass import getpass
import json
import re
import subprocess
import time
from pathlib import Path

import serial  # type: ignore  # noqa: E402


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--baud", type=int, default=115200)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--profile")
    action.add_argument("--ssid")
    action.add_argument("--restart", action="store_true")
    action.add_argument("--status", action="store_true")
    action.add_argument("--refresh", action="store_true")
    action.add_argument("--next-page", action="store_true")
    action.add_argument("--battery", action="store_true")
    action.add_argument("--ota-check", action="store_true")
    parser.add_argument("--timeout", type=float, default=70)
    parser.add_argument("--log", type=Path)
    return parser.parse_args()


def saved_windows_key(profile: str) -> str:
    result = subprocess.run(
        ["netsh", "wlan", "show", "profile", f"name={profile}", "key=clear"],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(f"Windows WLAN profile is unavailable: {profile}")
    for encoding in ("utf-8", "gb18030", "mbcs"):
        try:
            output = result.stdout.decode(encoding)
            break
        except (UnicodeDecodeError, LookupError):
            continue
    else:
        output = result.stdout.decode("utf-8", errors="replace")
    match = re.search(
        r"(?mi)^\s*(?:Key Content|关键内容)\s*:\s*(.+?)\s*$", output
    )
    if not match:
        raise RuntimeError(f"Windows WLAN profile has no readable WPA key: {profile}")
    return match.group(1).strip()


def open_port(port: str, baud: int, timeout: float):
    deadline = time.monotonic() + timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        handle = serial.Serial()
        handle.port = port
        handle.baudrate = baud
        handle.timeout = 0.2
        handle.write_timeout = 2
        handle.dtr = False
        handle.rts = False
        try:
            handle.open()
            return handle
        except (OSError, serial.SerialException) as error:
            last_error = error
            try:
                handle.close()
            except Exception:
                pass
            time.sleep(0.25)
    raise RuntimeError(f"USB serial port did not become available: {port}") from last_error


def main() -> int:
    args = parse_args()
    if args.restart:
        request = bytearray(b"INKDASH_RESTART_V1\n")
    elif args.status:
        request = bytearray(b"INKDASH_STATUS_V1\n")
    elif args.refresh:
        request = bytearray(b"INKDASH_REFRESH_V1\n")
    elif args.next_page:
        request = bytearray(b"INKDASH_PAGE_NEXT_V1\n")
    elif args.battery:
        request = bytearray(b"INKDASH_BATTERY_V1\n")
    elif args.ota_check:
        request = bytearray(b"INKDASH_OTA_CHECK_V1\n")
    else:
        ssid = args.profile or args.ssid
        password = saved_windows_key(args.profile) if args.profile else getpass(
            f"Wi-Fi password for {ssid}: "
        )
        request = bytearray(
            (
                "INKDASH_WIFI_V1 "
                + json.dumps(
                    {
                        "cmd": "wifi.configure",
                        "ssid": ssid,
                        "password": password,
                    },
                    ensure_ascii=False,
                    separators=(",", ":"),
                )
                + "\n"
            ).encode("utf-8")
        )
        password = ""

    log_handle = None
    handle = None
    try:
        if args.log:
            log_path = args.log.resolve()
            log_path.parent.mkdir(parents=True, exist_ok=True)
            log_handle = log_path.open("a", encoding="utf-8")
        handle = open_port(args.port, args.baud, 12)
        handle.write(request)
        handle.flush()
        for index in range(len(request)):
            request[index] = 0
        if args.restart:
            print(f"Sent normal restart request over {args.port}", flush=True)
        elif args.status:
            print(f"Sent persistent-config status request over {args.port}", flush=True)
        elif args.refresh:
            print(f"Sent immediate refresh request over {args.port}", flush=True)
        elif args.next_page:
            print(f"Sent page-next request over {args.port}", flush=True)
        elif args.battery:
            print(f"Sent forced battery-sample request over {args.port}", flush=True)
        elif args.ota_check:
            print(f"Sent signed OTA check request over {args.port}", flush=True)
        else:
            print(
                f"Sent Wi-Fi profile {ssid} over {args.port} "
                "(credential redacted)",
                flush=True,
            )

        deadline = time.monotonic() + args.timeout
        received = bytearray()
        while time.monotonic() < deadline:
            data = handle.read(handle.in_waiting or 1)
            if not data:
                continue
            received.extend(data)
            while b"\n" in received:
                raw_line, _, remainder = received.partition(b"\n")
                received = bytearray(remainder)
                line = raw_line.decode("utf-8", errors="replace").strip()
                if log_handle and line:
                    log_handle.write(line + "\n")
                    log_handle.flush()
                if line.startswith("USB provisioning succeeded"):
                    print(line, flush=True)
                    return 0
                if line.startswith("USB provisioning saved"):
                    print(line, flush=True)
                    return 0
                if line.startswith("Restart request accepted"):
                    print(line, flush=True)
                    return 0
                if line.startswith("Persistent Wi-Fi config status"):
                    print(line, flush=True)
                    return 0
                if line.startswith("Refresh request accepted"):
                    print(line, flush=True)
                    return 0
                if line.startswith("Page-next request accepted"):
                    print(line, flush=True)
                    return 0
                if line.startswith("Battery-sample request accepted"):
                    print(line, flush=True)
                    continue
                if line.startswith("OTA check request accepted"):
                    print(line, flush=True)
                    return 0
                if line.startswith("Battery sample:") or line.startswith(
                    "Battery sample invalid:"
                ):
                    print(line, flush=True)
                    return 0
                if line.startswith("USB provisioning failed") or line.startswith(
                    "USB provisioning rejected"
                ):
                    raise RuntimeError(line)
        raise RuntimeError("Timed out waiting for USB provisioning result")
    finally:
        for index in range(len(request)):
            request[index] = 0
        if handle is not None:
            handle.close()
        if log_handle is not None:
            log_handle.close()


if __name__ == "__main__":
    raise SystemExit(main())
