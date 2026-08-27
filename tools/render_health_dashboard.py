#!/usr/bin/env python3
"""Render a real-data-only 800x480 tri-color health dashboard package."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
import zlib
from datetime import date, datetime
from pathlib import Path
from typing import Any

from PIL import Image, ImageDraw, ImageFont


WIDTH = 800
HEIGHT = 480
PLANE_BYTES = WIDTH * HEIGHT // 8
HEADER = struct.Struct("<8sHHIIII")
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
RED = (255, 0, 0)
METRICS = (
    "steps",
    "active_energy_kcal",
    "exercise_minutes",
    "stand_hours",
    "sleep_hours",
    "resting_heart_rate",
    "hrv_ms",
)
SUMMARY_FIELDS = (
    "steps_daily_avg",
    "active_energy_kcal_daily_avg",
    "exercise_minutes_daily_avg",
    "stand_hours_daily_avg",
    "sleep_hours_daily_avg",
    "resting_heart_rate_avg",
    "hrv_ms_avg",
    "steps_change_percent",
)
SUMMARY_DAILY_FIELDS = {
    "steps_daily_avg": "steps",
    "active_energy_kcal_daily_avg": "active_energy_kcal",
    "exercise_minutes_daily_avg": "exercise_minutes",
    "stand_hours_daily_avg": "stand_hours",
    "sleep_hours_daily_avg": "sleep_hours",
    "resting_heart_rate_avg": "resting_heart_rate",
    "hrv_ms_avg": "hrv_ms",
}


def font(size: int, *, bold: bool = True) -> ImageFont.FreeTypeFont:
    windows = Path("C:/Windows/Fonts")
    candidates = (
        windows / ("msyhbd.ttc" if bold else "msyh.ttc"),
        windows / "Noto Sans SC Bold (TrueType).otf",
        windows / ("arialbd.ttf" if bold else "arial.ttf"),
        Path(
            "/usr/share/fonts/opentype/noto/"
            + ("NotoSansCJK-Bold.ttc" if bold else "NotoSansCJK-Regular.ttc")
        ),
        Path(
            "/usr/share/fonts/truetype/dejavu/"
            + ("DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf")
        ),
    )
    for candidate in candidates:
        if candidate.is_file():
            return ImageFont.truetype(str(candidate), size)
    raise FileNotFoundError("a Chinese-capable Windows font is required")


def number_or_none(value: object, field: str) -> float | None:
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{field} must be a number or null")
    numeric = float(value)
    if not math.isfinite(numeric) or numeric < 0:
        raise ValueError(f"{field} must be finite and non-negative")
    return numeric


def signed_number_or_none(value: object, field: str) -> float | None:
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{field} must be a number or null")
    numeric = float(value)
    if not math.isfinite(numeric):
        raise ValueError(f"{field} must be finite")
    return numeric


def parse_iso_date(value: object, field: str) -> date:
    if not isinstance(value, str):
        raise ValueError(f"{field} must be YYYY-MM-DD")
    try:
        return date.fromisoformat(value)
    except ValueError as error:
        raise ValueError(f"{field} must be YYYY-MM-DD") from error


def load_snapshot(path: Path) -> dict[str, Any]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError("health snapshot must be a JSON object")
    start = parse_iso_date(payload.get("period_start"), "period_start")
    end = parse_iso_date(payload.get("period_end"), "period_end")
    if end < start or (end - start).days != 6:
        raise ValueError("health period must contain exactly seven dates")

    goals = payload.get("goals")
    if not isinstance(goals, dict):
        raise ValueError("goals must be an object")
    normalized_goals = {
        "move_kcal": number_or_none(goals.get("move_kcal"), "goals.move_kcal"),
        "exercise_minutes": number_or_none(
            goals.get("exercise_minutes"), "goals.exercise_minutes"
        ),
        "stand_hours": number_or_none(
            goals.get("stand_hours"), "goals.stand_hours"
        ),
    }
    for key, value in normalized_goals.items():
        if value == 0:
            raise ValueError(f"goals.{key} cannot be zero")

    daily = payload.get("daily")
    if not isinstance(daily, list) or len(daily) != 7:
        raise ValueError("daily must contain exactly seven entries")
    normalized_daily: list[dict[str, Any]] = []
    for index, raw in enumerate(daily):
        if not isinstance(raw, dict):
            raise ValueError(f"daily[{index}] must be an object")
        expected_date = start.fromordinal(start.toordinal() + index)
        actual_date = parse_iso_date(raw.get("date"), f"daily[{index}].date")
        if actual_date != expected_date:
            raise ValueError("daily dates must be consecutive and match the period")
        entry: dict[str, Any] = {"date": actual_date}
        for metric in METRICS:
            entry[metric] = number_or_none(
                raw.get(metric), f"daily[{index}].{metric}"
            )
        normalized_daily.append(entry)

    normalized_summary = {
        field: (
            signed_number_or_none(payload.get(field), field)
            if field == "steps_change_percent"
            else number_or_none(payload.get(field), field)
        )
        for field in SUMMARY_FIELDS
    }
    for summary_field, daily_field in SUMMARY_DAILY_FIELDS.items():
        explicit = normalized_summary[summary_field]
        values = [
            float(entry[daily_field])
            for entry in normalized_daily
            if entry[daily_field] is not None
        ]
        if explicit is None or not values:
            continue
        calculated = sum(values) / len(values)
        tolerance = max(0.02, abs(calculated) * 0.0001)
        if abs(explicit - calculated) > tolerance:
            raise ValueError(
                f"{summary_field} does not match the supplied daily values"
            )
    generated_at = payload.get("generated_at")
    if not isinstance(generated_at, str):
        raise ValueError("generated_at must be an ISO-8601 string")
    try:
        datetime.fromisoformat(generated_at)
    except ValueError as error:
        raise ValueError("generated_at must be an ISO-8601 string") from error

    has_measurement = any(
        value is not None for value in normalized_summary.values()
    ) or any(
        entry[metric] is not None
        for entry in normalized_daily
        for metric in METRICS
    )
    if not has_measurement:
        raise ValueError(
            "health snapshot contains no measurements; all metrics are null"
        )

    data_days_value = payload.get("data_days")
    if data_days_value is None:
        data_days = sum(
            any(entry[metric] is not None for metric in METRICS)
            for entry in normalized_daily
        )
    elif isinstance(data_days_value, bool) or not isinstance(data_days_value, int):
        raise ValueError("data_days must be an integer from 0 through 7")
    else:
        data_days = data_days_value
    if not 0 <= data_days <= 7:
        raise ValueError("data_days must be an integer from 0 through 7")

    return {
        "period_start": start,
        "period_end": end,
        "goals": normalized_goals,
        "daily": normalized_daily,
        "summary": normalized_summary,
        "data_days": data_days,
        "generated_at": generated_at,
    }


def average(snapshot: dict[str, Any], summary_field: str, daily_field: str) -> float | None:
    explicit = snapshot["summary"][summary_field]
    if explicit is not None:
        return float(explicit)
    values = [
        float(entry[daily_field])
        for entry in snapshot["daily"]
        if entry[daily_field] is not None
    ]
    return None if not values else sum(values) / len(values)


def daily_peak(snapshot: dict[str, Any], daily_field: str) -> float | None:
    values = [
        float(entry[daily_field])
        for entry in snapshot["daily"]
        if entry[daily_field] is not None
    ]
    return None if not values else max(values)


def exact_tricolor(image: Image.Image) -> Image.Image:
    converted = Image.new("RGB", image.size, WHITE)
    pixels: list[tuple[int, int, int]] = []
    rgb = image.convert("RGB")
    flattened = (
        rgb.get_flattened_data()
        if hasattr(rgb, "get_flattened_data")
        else rgb.getdata()
    )
    for red, green, blue in flattened:
        if red >= 150 and red > green * 1.5 and red > blue * 1.5:
            pixels.append(RED)
        elif red + green + blue < 570:
            pixels.append(BLACK)
        else:
            pixels.append(WHITE)
    converted.putdata(pixels)
    return converted


def draw_ring(
    draw: ImageDraw.ImageDraw,
    center: tuple[int, int],
    radius: int,
    width: int,
    color: tuple[int, int, int],
    value: float | None,
    target: float | None,
) -> None:
    cx, cy = center
    box = (cx - radius, cy - radius, cx + radius, cy + radius)
    # A thin full-circle track keeps the unfilled portion airy. Keep the broad
    # progress arc's native flat ends: extra circular caps become conspicuous
    # beads after conversion to the panel's one-bit black/red planes.
    draw.ellipse(box, outline=color, width=2)
    if value is None or target is None or target <= 0:
        return
    percent = max(0.0, min(1.0, value / target))
    if percent <= 0:
        return
    end = -90 + 360 * percent
    draw.arc(box, start=-90, end=end, fill=color, width=width)


def compact_value(value: float | None, decimals: int = 0) -> str:
    if value is None:
        return "--"
    if decimals == 0:
        return f"{round(value):,}"
    return f"{value:.{decimals}f}"


def value_goal(value: float | None, goal: float | None, decimals: int = 0) -> str:
    return f"{compact_value(value, decimals)}/{compact_value(goal, decimals)}"


def draw_bars(
    draw: ImageDraw.ImageDraw,
    values: list[float | None],
    *,
    left: int,
    top: int,
    right: int,
    bottom: int,
    bar_width: int,
) -> bool:
    available = [value for value in values if value is not None]
    if not available:
        return False
    maximum = max(available) or 1
    step = (right - left) / len(values)
    for index, value in enumerate(values):
        if value is None:
            continue
        height = max(3, round((bottom - top) * value / maximum))
        center = round(left + step * (index + 0.5))
        color = RED if index == len(values) - 1 else BLACK
        draw.rounded_rectangle(
            (center - bar_width // 2, bottom - height, center + bar_width // 2, bottom),
            radius=max(2, bar_width // 5),
            fill=color,
        )
    return True


def draw_line(
    draw: ImageDraw.ImageDraw,
    values: list[float | None],
    *,
    left: int,
    top: int,
    right: int,
    bottom: int,
) -> bool:
    available = [value for value in values if value is not None]
    if not available:
        return False
    minimum = min(available)
    maximum = max(available)
    spread = maximum - minimum or 1.0
    step = (right - left) / max(1, len(values) - 1)
    previous: tuple[int, int] | None = None
    for index, value in enumerate(values):
        if value is None:
            previous = None
            continue
        x = round(left + index * step)
        y = round(bottom - (bottom - top) * (value - minimum) / spread)
        if previous is not None:
            draw.line((*previous, x, y), fill=BLACK, width=3)
        color = RED if index == len(values) - 1 else BLACK
        draw.ellipse((x - 5, y - 5, x + 5, y + 5), fill=color)
        previous = (x, y)
    return True


def render(snapshot: dict[str, Any]) -> Image.Image:
    image = Image.new("RGB", (WIDTH, HEIGHT), WHITE)
    draw = ImageDraw.Draw(image)
    title_font = font(38)
    header_font = font(24)
    label_font = font(18)
    small_font = font(14)
    value_font = font(38)

    start: date = snapshot["period_start"]
    end: date = snapshot["period_end"]
    draw.text((24, 13), "健康圆环", font=title_font, fill=BLACK)
    draw.text(
        (446, 38),
        f"{start:%m.%d}—{end:%m.%d}",
        font=header_font,
        fill=BLACK,
        anchor="mm",
    )
    draw.text(
        (630, 38),
        f"有效记录 {snapshot['data_days']}/7",
        font=font(18),
        fill=BLACK,
        anchor="mm",
    )
    # Firmware overwrites this placeholder with the current device battery.
    draw.rounded_rectangle((726, 18, 776, 45), radius=4, outline=BLACK, width=3)
    draw.rectangle((777, 26, 782, 37), fill=BLACK)
    draw.line((0, 68, 800, 68), fill=BLACK, width=3)

    steps_avg = average(snapshot, "steps_daily_avg", "steps")
    move_avg = average(
        snapshot, "active_energy_kcal_daily_avg", "active_energy_kcal"
    )
    exercise_avg = average(
        snapshot, "exercise_minutes_daily_avg", "exercise_minutes"
    )
    stand_avg = average(snapshot, "stand_hours_daily_avg", "stand_hours")
    sleep_avg = average(snapshot, "sleep_hours_daily_avg", "sleep_hours")
    heart_avg = average(
        snapshot, "resting_heart_rate_avg", "resting_heart_rate"
    )
    hrv_avg = average(snapshot, "hrv_ms_avg", "hrv_ms")
    goals = snapshot["goals"]

    move_target = goals["move_kcal"] or daily_peak(
        snapshot, "active_energy_kcal"
    )
    exercise_target = goals["exercise_minutes"] or daily_peak(
        snapshot, "exercise_minutes"
    )
    stand_target = goals["stand_hours"] or daily_peak(snapshot, "stand_hours")
    center = (210, 226)
    draw_ring(draw, center, 135, 20, RED, move_avg, move_target)
    draw_ring(
        draw,
        center,
        109,
        16,
        BLACK,
        exercise_avg,
        exercise_target,
    )
    draw_ring(draw, center, 83, 12, BLACK, stand_avg, stand_target)

    change = snapshot["summary"]["steps_change_percent"]
    if change is not None:
        direction = "↑" if change >= 0 else "↓"
        draw.text((210, 207), "步数较前7日", font=font(13), fill=BLACK, anchor="mm")
        draw.text(
            (210, 240),
            f"{direction}{abs(change):.1f}%",
            font=font(27),
            fill=RED,
            anchor="mm",
        )

    draw.line((20, 386, 400, 386), fill=BLACK, width=2)
    draw.line((146, 400, 146, 461), fill=BLACK, width=1)
    draw.line((274, 400, 274, 461), fill=BLACK, width=1)
    rows = (
        (83, "活动", compact_value(move_avg), "千卡/日", RED),
        (210, "锻炼", compact_value(exercise_avg), "分钟/日", BLACK),
        (337, "站立", compact_value(stand_avg, 1), "小时/日", BLACK),
    )
    value_font_small = font(26)
    unit_font = font(11)
    for x, label, value, unit, color in rows:
        draw.text((x, 399), label, font=font(15), fill=color, anchor="ma")
        value_width = draw.textlength(value, font=value_font_small)
        unit_width = draw.textlength(unit, font=unit_font)
        group_width = value_width + 5 + unit_width
        start_x = x - group_width / 2
        draw.text((start_x, 424), value, font=value_font_small, fill=color)
        draw.text(
            (start_x + value_width + 5, 438),
            unit,
            font=unit_font,
            fill=BLACK,
        )

    draw.line((420, 88, 420, 462), fill=BLACK, width=3)
    draw.text((444, 101), "日均步数", font=label_font, fill=BLACK)
    draw.text((444, 143), compact_value(steps_avg), font=value_font, fill=BLACK)
    steps = [entry["steps"] for entry in snapshot["daily"]]
    steps_total = sum(value for value in steps if value is not None)
    draw.text(
        (444, 198),
        f"7日合计 {round(steps_total):,}",
        font=small_font,
        fill=BLACK,
    )
    draw.text((684, 101), "7日步数", font=label_font, fill=BLACK, anchor="mm")
    if not draw_bars(
        draw, steps, left=565, top=125, right=786, bottom=257, bar_width=22
    ):
        draw.text((676, 205), "逐日数据待补充", font=small_font, fill=BLACK, anchor="mm")
    for index, entry in enumerate(snapshot["daily"]):
        x = round(565 + (786 - 565) / 7 * (index + 0.5))
        color = RED if index == 6 else BLACK
        draw.text((x, 278), f"{entry['date'].day}", font=small_font, fill=color, anchor="mm")

    draw.line((420, 294, 790, 294), fill=BLACK, width=2)
    draw.line((613, 294, 613, 462), fill=BLACK, width=2)
    draw.text((444, 313), "睡眠记录", font=label_font, fill=BLACK)
    draw.text(
        (444, 348),
        compact_value(sleep_avg, 1),
        font=font(30),
        fill=RED if sleep_avg is not None and sleep_avg < 6 else BLACK,
    )
    draw.text((510, 365), "小时/日", font=small_font, fill=BLACK)
    sleep = [entry["sleep_hours"] for entry in snapshot["daily"]]
    if not draw_bars(
        draw, sleep, left=444, top=389, right=603, bottom=443, bar_width=12
    ):
        draw.text((523, 422), "暂无逐日记录", font=small_font, fill=BLACK, anchor="mm")
    mini_date_font = font(11)
    for index, entry in enumerate(snapshot["daily"]):
        x = round(444 + (603 - 444) / 7 * (index + 0.5))
        color = RED if index == 6 else BLACK
        if sleep[index] is None:
            draw.text((x, 430), "--", font=mini_date_font, fill=BLACK, anchor="mm")
        draw.text(
            (x, 460),
            f"{entry['date'].day}",
            font=mini_date_font,
            fill=color,
            anchor="mm",
        )

    draw.text((632, 313), "静息心率", font=label_font, fill=BLACK)
    draw.text((632, 348), compact_value(heart_avg), font=font(30), fill=BLACK)
    draw.text((680, 365), "次/分", font=small_font, fill=BLACK)
    draw.text(
        (632, 390),
        f"HRV {compact_value(hrv_avg)} 毫秒",
        font=small_font,
        fill=BLACK,
    )
    heart = [entry["resting_heart_rate"] for entry in snapshot["daily"]]
    if not draw_line(draw, heart, left=637, top=414, right=778, bottom=447):
        draw.text((704, 433), "暂无逐日趋势", font=small_font, fill=BLACK, anchor="mm")
    for index, entry in enumerate(snapshot["daily"]):
        x = round(637 + (778 - 637) / 6 * index)
        color = RED if index == 6 else BLACK
        draw.text(
            (x, 462),
            f"{entry['date'].day}",
            font=mini_date_font,
            fill=color,
            anchor="mm",
        )

    return exact_tricolor(image)


def image_to_planes(image: Image.Image) -> tuple[bytes, bytes]:
    black = bytearray([0xFF] * PLANE_BYTES)
    red = bytearray([0xFF] * PLANE_BYTES)
    pixels = image.load()
    for y in range(HEIGHT):
        row_offset = y * (WIDTH // 8)
        for x in range(WIDTH):
            color = pixels[x, y]
            if color == WHITE:
                continue
            byte_index = row_offset + x // 8
            bit = 0x80 >> (x % 8)
            if color == BLACK:
                black[byte_index] &= ~bit
            elif color == RED:
                red[byte_index] &= ~bit
            else:
                raise ValueError(f"non-tricolor pixel at {(x, y)}: {color}")
    return bytes(black), bytes(red)


def write_outputs(image: Image.Image, source: Path, output_dir: Path) -> None:
    black, red = image_to_planes(image)
    black_crc = zlib.crc32(black)
    red_crc = zlib.crc32(red)
    package = HEADER.pack(
        b"INKWALL1", WIDTH, HEIGHT, PLANE_BYTES, black_crc, red_crc, 1
    ) + black + red
    output_dir.mkdir(parents=True, exist_ok=True)
    preview = output_dir / "health-preview.png"
    binary = output_dir / "health.bin"
    metadata = output_dir / "health-package.json"
    image.save(preview, optimize=True)
    binary.write_bytes(package)
    metadata.write_text(
        json.dumps(
            {
                "format": "INKWALL1",
                "width": WIDTH,
                "height": HEIGHT,
                "bytes": len(package),
                "black_crc32": f"{black_crc:08x}",
                "red_crc32": f"{red_crc:08x}",
                "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                "package_sha256": hashlib.sha256(package).hexdigest(),
            },
            ensure_ascii=False,
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    print(preview)
    print(binary)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("artifacts") / "health",
    )
    args = parser.parse_args()
    source = args.source.resolve()
    snapshot = load_snapshot(source)
    image = render(snapshot)
    write_outputs(image, source, args.output_dir.resolve())


if __name__ == "__main__":
    main()
