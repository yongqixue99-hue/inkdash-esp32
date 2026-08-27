#!/usr/bin/env python3
"""Assert the device header contract without requiring target hardware."""

from __future__ import annotations

import re
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    template = (PROJECT_ROOT / "assets/templates/codex-live-template.svg").read_text(
        encoding="utf-8"
    )
    display_source = (PROJECT_ROOT / "src/display_controller.cpp").read_text(
        encoding="utf-8"
    )
    client_source = (PROJECT_ROOT / "src/dashboard_client.cpp").read_text(
        encoding="utf-8"
    )
    failures = []
    if "%H" in display_source or "%M" in display_source:
        failures.append("header/footer clock formatting is still present")

    if "header_glyphs" not in display_source or "drawBitmap" not in display_source:
        failures.append("Chinese header does not use deterministic bitmap glyphs")

    battery_x = re.search(r"kHeaderBatteryX\s*=\s*(\d+)", display_source)
    if not battery_x or int(battery_x.group(1)) < 680:
        failures.append("battery is not anchored at the far-right of the header")

    battery_y = re.search(r"kHeaderBatteryY\s*=\s*(\d+)", display_source)
    if not battery_y or not 20 <= int(battery_y.group(1)) <= 26:
        failures.append("battery is not vertically aligned with the date/weekday row")

    required_battery_fragments = (
        "kBatteryBodyWidth",
        "kBatteryBodyHeight",
        "kBatteryBorderWidth",
        "kInkWhite",
        "fillRect",
        "battery->percent",
    )
    missing_battery = [
        fragment
        for fragment in required_battery_fragments
        if fragment not in display_source
    ]
    if missing_battery:
        failures.append(
            f"outlined black/white proportional battery is incomplete: {missing_battery}"
        )

    battery_function = re.search(
        r"void drawBattery\(.*?\n\}", display_source, flags=re.DOTALL
    )
    if not battery_function:
        failures.append("drawBattery implementation was not found")
    else:
        battery_body = battery_function.group(0)
        first_outline = battery_body.find("fillRect")
        first_return = battery_body.find("return;")
        if first_return >= 0 and (
            first_outline < 0 or first_return < first_outline
        ):
            failures.append("invalid readings still hide the entire battery outline")
        if 'label = "USB"' in battery_body:
            failures.append("external power is still rendered as the literal USB label")
        if "fillTriangle" not in battery_body:
            failures.append("external power has no compact charging mark")

    if "kHeaderGlyphWidth" not in display_source:
        failures.append("header date/weekday has not been enlarged")
    if "电量" in template:
        failures.append("obsolete fixed battery label still occupies the header")
    if "chart_difference_tokens" not in client_source:
        failures.append("firmware does not cross-check seven-day bars against daily Token values")
    if "pair.today_tokens" not in display_source or "kInkRed" not in display_source:
        failures.append("today Token value is not explicitly rendered in red")
    if ">昨日<" not in template or ">今日<" not in template:
        failures.append("yesterday and today columns are not both present")
    if "header_glyphs::kYi" not in display_source:
        failures.append("Token values do not carry the deterministic Chinese 亿 unit")
    if "pair.today_available" not in display_source or 'String("--")' not in display_source:
        failures.append("missing current-day account data is not shown explicitly")
    if "data->cumulative_tokens" not in display_source or "账号累计" not in template:
        failures.append("account cumulative Token is not present on the Codex page")
    if "daily_day_of_month" not in display_source:
        failures.append("seven-day bars do not use account-provided dates")
    if "TOKEN" in template or "近7日 token" not in template:
        failures.append("Token labels are not compact lowercase text")
    if "measureTextWidth" not in display_source or "kQuotaCenterX" not in display_source:
        failures.append("weekly remaining value is not width-aware and centered")
    if "uint8_t quota_size = 4" not in display_source:
        failures.append("weekly remaining value does not start at the enlarged scale")
    if failures:
        print("HEADER_CONTRACT_FAIL")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print(
        "HEADER_CONTRACT_OK weekday=Chinese battery=aligned/outlined quota=centered token=yesterday+today"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
