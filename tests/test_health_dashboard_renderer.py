from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import zlib

import pytest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "health_renderer", ROOT / "tools" / "render_health_dashboard.py"
)
assert SPEC is not None and SPEC.loader is not None
renderer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(renderer)


def snapshot(*, with_measurements: bool) -> dict[str, object]:
    daily = []
    for day in range(18, 25):
        daily.append(
            {
                "date": f"2026-08-{day:02d}",
                "steps": 3000 + day * 10 if with_measurements else None,
                "active_energy_kcal": 350 + day if with_measurements else None,
                "exercise_minutes": day - 10 if with_measurements else None,
                "stand_hours": 1 + (day - 18) / 10 if with_measurements else None,
                "sleep_hours": 5 + (day - 18) / 10 if with_measurements else None,
                "resting_heart_rate": 55 + (day - 18) if with_measurements else None,
                "hrv_ms": 45 + (day - 18) if with_measurements else None,
            }
        )
    return {
        "period_start": "2026-08-18",
        "period_end": "2026-08-24",
        "data_days": 7 if with_measurements else 0,
        "goals": {
            "move_kcal": None,
            "exercise_minutes": None,
            "stand_hours": None,
        },
        "steps_daily_avg": None,
        "active_energy_kcal_daily_avg": None,
        "exercise_minutes_daily_avg": None,
        "stand_hours_daily_avg": None,
        "sleep_hours_daily_avg": None,
        "resting_heart_rate_avg": None,
        "hrv_ms_avg": None,
        "steps_change_percent": None,
        "daily": daily,
        "generated_at": "2026-08-25T23:33:00+08:00",
    }


def test_all_null_snapshot_is_rejected_instead_of_fabricated() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary) / "health.json"
        path.write_text(json.dumps(snapshot(with_measurements=False)), encoding="utf-8")
        with pytest.raises(ValueError, match="all metrics are null"):
            renderer.load_snapshot(path)


def test_real_daily_values_render_a_valid_network_image() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source = root / "health.json"
        source.write_text(json.dumps(snapshot(with_measurements=True)), encoding="utf-8")
        parsed = renderer.load_snapshot(source)
        image = renderer.render(parsed)
        renderer.write_outputs(image, source, root / "output")
        package = (root / "output" / "health.bin").read_bytes()
        assert len(package) == 96_028
        magic, width, height, plane_bytes, black_crc, red_crc, version = struct.unpack(
            "<8sHHIIII", package[:28]
        )
        assert (magic, width, height, plane_bytes, version) == (
            b"INKWALL1",
            800,
            480,
            48_000,
            1,
        )
        assert zlib.crc32(package[28 : 28 + plane_bytes]) == black_crc
        assert zlib.crc32(package[28 + plane_bytes :]) == red_crc


def test_inconsistent_summary_is_rejected() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary) / "health.json"
        payload = snapshot(with_measurements=True)
        payload["steps_daily_avg"] = 1
        path.write_text(json.dumps(payload), encoding="utf-8")
        with pytest.raises(ValueError, match="steps_daily_avg does not match"):
            renderer.load_snapshot(path)
