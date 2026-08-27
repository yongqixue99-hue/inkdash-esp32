"""Serve sanitized InkDash data and image packages over the local network."""

from __future__ import annotations

import argparse
from datetime import datetime, timedelta, timezone
import json
import logging
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Mapping

from codex_profile import CodexProfileClient, CodexProfileError
from codex_usage import CodexUsageReader


PROJECT_ROOT = Path(__file__).resolve().parents[1]
HONG_KONG = timezone(timedelta(hours=8))
IMAGE_BYTES = 96_028


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8767)
    parser.add_argument(
        "--sessions-root", type=Path, default=Path.home() / ".codex" / "sessions"
    )
    parser.add_argument(
        "--codex-auth-file", type=Path, default=Path.home() / ".codex" / "auth.json"
    )
    parser.add_argument("--disable-account-profile", action="store_true")
    parser.add_argument(
        "--server-json",
        type=Path,
        default=PROJECT_ROOT / "config" / "server-dashboard.json",
    )
    parser.add_argument(
        "--wallpaper",
        type=Path,
        default=PROJECT_ROOT / "artifacts" / "content" / "wallpaper.bin",
    )
    parser.add_argument(
        "--health-image",
        type=Path,
        default=PROJECT_ROOT / "artifacts" / "content" / "health.bin",
    )
    parser.add_argument("--log-level", default="INFO")
    return parser


def _local_payload(reader: CodexUsageReader, now: float | None = None) -> dict[str, object]:
    payload = reader.snapshot().to_payload()
    generated_at = int(payload["generated_at"])
    current = datetime.fromtimestamp(now or generated_at, HONG_KONG).date()
    first = current - timedelta(days=6)
    payload["daily_token_dates"] = [
        (first + timedelta(days=index)).isoformat() for index in range(7)
    ]
    payload["cumulative_tokens"] = 0
    payload["peak_tokens"] = max(payload["daily_tokens"], default=0)
    payload["snapshot_age_seconds"] = max(0, round(time.time()) - generated_at)
    return payload


def build_codex_payload(server: ThreadingHTTPServer) -> dict[str, object]:
    payload = _local_payload(server.codex_usage)
    if server.codex_profile is not None:
        try:
            payload = server.codex_profile.overlay(payload)
        except CodexProfileError as error:
            logging.getLogger("inkdash.codex").warning(
                "account statistics unavailable; using local session data: %s", error
            )
    generated_at = int(payload.get("generated_at", round(time.time())))
    payload["snapshot_age_seconds"] = max(0, round(time.time()) - generated_at)
    return payload


def unconfigured_server_payload(now: int | None = None) -> dict[str, object]:
    return {
        "name": "SERVER",
        "configured": False,
        "traffic_period": "MONTH",
        "traffic_used_centi_gb": 0,
        "traffic_limit_centi_gb": 0,
        "plan_limit_centi_gb": 0,
        "expiry_date": "",
        "expiry_days_remaining": 0,
        "traffic_reset_date": "",
        "cpu_percent": 0,
        "memory_percent": 0,
        "disk_percent": 0,
        "rx_centi_gb": 0,
        "tx_centi_gb": 0,
        "uptime_days": 0,
        "generated_at": now or round(time.time()),
        "snapshot_age_seconds": 0,
    }


def load_server_payload(path: Path, now: int | None = None) -> dict[str, object]:
    current = now or round(time.time())
    if not path.is_file():
        return unconfigured_server_payload(current)
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("server dashboard JSON could not be read") from error
    if not isinstance(payload, Mapping):
        raise ValueError("server dashboard root must be an object")
    if payload.get("configured") is not True:
        return unconfigured_server_payload(current)

    name = str(payload.get("name", "SERVER")).strip()
    dates = (str(payload.get("expiry_date", "")), str(payload.get("traffic_reset_date", "")))
    if not name or len(name.encode("utf-8")) > 15:
        raise ValueError("server name must fit in 15 UTF-8 bytes")
    for value in dates:
        try:
            datetime.strptime(value, "%Y-%m-%d")
        except ValueError as error:
            raise ValueError("server dates must use YYYY-MM-DD") from error

    integer_fields = (
        "traffic_used_centi_gb",
        "traffic_limit_centi_gb",
        "plan_limit_centi_gb",
        "cpu_percent",
        "memory_percent",
        "disk_percent",
        "rx_centi_gb",
        "tx_centi_gb",
        "uptime_days",
        "generated_at",
    )
    normalized: dict[str, int] = {}
    for field in integer_fields:
        value = payload.get(field)
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise ValueError(f"{field} must be a non-negative integer")
        normalized[field] = value
    for field in ("cpu_percent", "memory_percent", "disk_percent"):
        if normalized[field] > 100:
            raise ValueError(f"{field} must be between 0 and 100")
    limit = normalized["traffic_limit_centi_gb"]
    if limit <= 0 or normalized["plan_limit_centi_gb"] != limit:
        raise ValueError("traffic and plan limits must match and be positive")
    if normalized["generated_at"] <= 0:
        raise ValueError("generated_at must be a Unix timestamp")

    expiry = datetime.strptime(dates[0], "%Y-%m-%d").date()
    today = datetime.fromtimestamp(current, HONG_KONG).date()
    result: dict[str, object] = {
        "name": name,
        "configured": True,
        "traffic_period": "MONTH",
        "expiry_date": dates[0],
        "traffic_reset_date": dates[1],
        "expiry_days_remaining": max(0, (expiry - today).days),
        **normalized,
    }
    result["snapshot_age_seconds"] = max(0, current - normalized["generated_at"])
    return result


class DashboardServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class DashboardHandler(BaseHTTPRequestHandler):
    server_version = "InkDashLocal/1"

    def do_GET(self) -> None:
        if self.path == "/health":
            self._write_json(
                200,
                {
                    "status": "ok",
                    "account_profile": self.server.codex_profile is not None,
                    "wallpaper": self.server.wallpaper.is_file(),
                    "health_image": self.server.health_image.is_file(),
                },
            )
            return
        if self.path == "/dashboard":
            try:
                self._write_json(200, build_codex_payload(self.server))
            except Exception as error:
                logging.getLogger("inkdash.codex").exception("snapshot failed: %s", error)
                self._write_json(503, {"error": "dashboard_unavailable"})
            return
        if self.path == "/server-dashboard":
            try:
                self._write_json(200, load_server_payload(self.server.server_json))
            except ValueError as error:
                logging.getLogger("inkdash.server").warning("invalid config: %s", error)
                self._write_json(503, {"error": "server_dashboard_invalid"})
            return
        if self.path == "/wallpaper.bin":
            self._write_image(self.server.wallpaper)
            return
        if self.path == "/health.bin":
            self._write_image(self.server.health_image)
            return
        self._write_json(404, {"error": "not_found"})

    def log_message(self, message_format: str, *args: object) -> None:
        logging.getLogger("inkdash.http").info(
            "%s %s", self.client_address[0], message_format % args
        )

    def _write_json(self, status: int, payload: Mapping[str, object]) -> None:
        body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self._write_body(body)

    def _write_image(self, path: Path) -> None:
        try:
            body = path.read_bytes()
        except OSError:
            self._write_json(404, {"error": "image_not_configured"})
            return
        if len(body) != IMAGE_BYTES or body[:8] != b"INKWALL1":
            self._write_json(503, {"error": "image_package_invalid"})
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/vnd.inkdash.wallpaper")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self._write_body(body)

    def _write_body(self, body: bytes) -> None:
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionAbortedError, ConnectionResetError):
            return


def main() -> int:
    args = build_parser().parse_args()
    if not 1 <= args.port <= 65535:
        raise SystemExit("port must be between 1 and 65535")
    logging.basicConfig(
        level=getattr(logging, args.log_level.upper(), logging.INFO),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    server = DashboardServer((args.bind, args.port), DashboardHandler)
    server.codex_usage = CodexUsageReader(
        args.sessions_root, cache_seconds=120, timezone_offset_hours=8
    )
    server.codex_profile = None
    if not args.disable_account_profile:
        server.codex_profile = CodexProfileClient(args.codex_auth_file)
    server.server_json = args.server_json.resolve()
    server.wallpaper = args.wallpaper.resolve()
    server.health_image = args.health_image.resolve()
    logging.getLogger("inkdash.host").info(
        "listening on http://%s:%d", args.bind, args.port
    )
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
