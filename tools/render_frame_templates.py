#!/usr/bin/env python3
"""Render editable SVG page templates into exact black/white/red PNGs."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path

from PIL import Image


WIDTH = 800
HEIGHT = 480
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
RED = (255, 0, 0)

TEMPLATES = (
    ("codex-live-template.svg", "codex-quota.png"),
    ("server-live-template.svg", "server-status.png"),
)


def find_browser() -> Path:
    candidates = (
        Path(r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"),
        Path(r"C:\Program Files\Microsoft\Edge\Application\msedge.exe"),
        Path(r"C:\Program Files\Google\Chrome\Application\chrome.exe"),
        Path(r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe"),
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    for executable in ("msedge", "chrome", "chromium"):
        resolved = shutil.which(executable)
        if resolved:
            return Path(resolved)
    raise FileNotFoundError("Edge, Chrome, or Chromium is required to render SVG templates")


def exact_tricolor(source: Path, destination: Path) -> None:
    image = Image.open(source).convert("RGB")
    if image.size != (WIDTH, HEIGHT):
        raise ValueError(f"browser returned {image.size}, expected {(WIDTH, HEIGHT)}")
    output = Image.new("RGB", image.size, WHITE)
    converted = []
    flattened = (
        image.get_flattened_data()
        if hasattr(image, "get_flattened_data")
        else image.getdata()
    )
    for red, green, blue in flattened:
        if red >= 150 and red > green * 1.5 and red > blue * 1.5:
            converted.append(RED)
        elif red + green + blue < 570:
            converted.append(BLACK)
        else:
            converted.append(WHITE)
    output.putdata(converted)
    destination.parent.mkdir(parents=True, exist_ok=True)
    output.save(destination, optimize=True)


def render(project_root: Path) -> None:
    browser = find_browser()
    template_dir = project_root / "assets" / "templates"
    output_dir = project_root / "assets" / "source"
    with tempfile.TemporaryDirectory(prefix="inkdash-svg-") as temp_dir:
        temporary = Path(temp_dir)
        profile = temporary / "browser-profile"
        for template_name, output_name in TEMPLATES:
            source = (template_dir / template_name).resolve()
            screenshot = temporary / output_name
            subprocess.run(
                (
                    str(browser),
                    "--headless=new",
                    "--disable-gpu",
                    "--hide-scrollbars",
                    "--force-device-scale-factor=1",
                    f"--user-data-dir={profile}",
                    f"--window-size={WIDTH},{HEIGHT}",
                    f"--screenshot={screenshot}",
                    source.as_uri(),
                ),
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=30,
            )
            exact_tricolor(screenshot, output_dir / output_name)
            print(f"rendered {template_name} -> assets/source/{output_name}")


if __name__ == "__main__":
    render(Path(__file__).resolve().parents[1])
