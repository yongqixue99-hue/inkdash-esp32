from __future__ import annotations

from datetime import datetime, timezone
import json
from pathlib import Path
import sys

import pytest


HOST = Path(__file__).resolve().parents[1] / "host"
sys.path.insert(0, str(HOST))

import dashboard_server  # noqa: E402


def test_missing_server_config_returns_explicit_unconfigured_payload(tmp_path: Path) -> None:
    payload = dashboard_server.load_server_payload(tmp_path / "missing.json", now=100)
    assert payload["configured"] is False
    assert payload["traffic_period"] == "MONTH"
    assert payload["snapshot_age_seconds"] == 0


def test_configured_server_payload_is_validated_and_aged(tmp_path: Path) -> None:
    generated = int(datetime(2026, 8, 27, tzinfo=timezone.utc).timestamp())
    source = {
        "name": "HOME",
        "configured": True,
        "traffic_used_centi_gb": 250,
        "traffic_limit_centi_gb": 1000,
        "plan_limit_centi_gb": 1000,
        "expiry_date": "2026-09-30",
        "traffic_reset_date": "2026-09-01",
        "cpu_percent": 10,
        "memory_percent": 20,
        "disk_percent": 30,
        "rx_centi_gb": 100,
        "tx_centi_gb": 150,
        "uptime_days": 2,
        "generated_at": generated,
    }
    path = tmp_path / "server.json"
    path.write_text(json.dumps(source), encoding="utf-8")
    payload = dashboard_server.load_server_payload(path, now=generated + 90)
    assert payload["configured"] is True
    assert payload["traffic_period"] == "MONTH"
    assert payload["snapshot_age_seconds"] == 90
    assert payload["expiry_days_remaining"] == 34


def test_server_payload_rejects_mismatched_limits(tmp_path: Path) -> None:
    source = json.loads(
        (Path(__file__).resolve().parents[1] / "config" / "server-dashboard.example.json").read_text(encoding="utf-8")
    )
    source["plan_limit_centi_gb"] = 1
    path = tmp_path / "server.json"
    path.write_text(json.dumps(source), encoding="utf-8")
    with pytest.raises(ValueError, match="limits"):
        dashboard_server.load_server_payload(path)
