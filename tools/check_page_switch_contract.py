#!/usr/bin/env python3
"""Assert direct touch and software-only EN-reset page paths stay wired."""

from __future__ import annotations

from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    main_source = (PROJECT_ROOT / "src/main.cpp").read_text(encoding="utf-8")
    board_source = (PROJECT_ROOT / "include/board_config.h").read_text(
        encoding="utf-8"
    )
    state_source = (PROJECT_ROOT / "src/page_state_store.cpp").read_text(
        encoding="utf-8"
    )
    failures: list[str] = []

    expected_pins = (
        "constexpr int kPrimaryPageTouchPin = 9;",
        "constexpr int kSecondaryPageTouchPin = 3;",
        "constexpr bool kPageTouchActiveLow = true;",
    )
    for expected in expected_pins:
        if expected not in board_source:
            failures.append(f"missing board input contract: {expected}")

    for input_name in ("primary_page_touch", "secondary_page_touch"):
        branch_start = main_source.find(f"{input_name}.pollPressed()")
        if branch_start < 0:
            failures.append(f"{input_name} is not polled")
            continue
        branch = main_source[branch_start : branch_start + 260]
        if "advancePageAndRefresh();" not in branch:
            failures.append(f"{input_name} does not advance the page")

    refresh_start = main_source.find("void advancePageAndRefresh()")
    refresh_body = main_source[refresh_start : refresh_start + 500]
    if "advancePageAndRemember();" not in refresh_body:
        failures.append("page refresh does not persist the next page")
    if "showCurrentPage(true);" not in refresh_body:
        failures.append("page refresh does not fetch and render the target page")

    for marker in (
        "flash::kPageStateSlotA",
        "flash::kPageStateSlotB",
        "ESP_RST_POWERON",
        "ESP_RST_EXT",
        "choosePageOnBoot(",
    ):
        if marker not in state_source:
            failures.append(f"missing software-only EN reset bridge: {marker}")
    if "between EN and GND" not in board_source or "requires no wiring change" not in board_source:
        failures.append("unchanged EN switch hardware is not documented")
    if "page_state_store.begin(pages.count())" not in main_source or \
       "pages.select(page_startup.page_index);" not in main_source:
        failures.append("flash-backed boot page is not applied")

    if failures:
        print("PAGE_SWITCH_CONTRACT_FAIL")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print(
        "PAGE_SWITCH_CONTRACT_OK GPIO9+GPIO3 direct; EN/RST flash-backed next page"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
