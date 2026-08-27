#!/usr/bin/env python3
"""Build a validated network wallpaper for the 800x480 tri-color panel."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

from PIL import Image


WIDTH = 800
HEIGHT = 480
PLANE_BYTES = WIDTH * HEIGHT // 8
MAGIC = b"INKWALL1"
FORMAT_VERSION = 1
HEADER = struct.Struct("<8sHHIIII")
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
RED = (255, 0, 0)


def fit_with_black_letterbox(source: Image.Image) -> Image.Image:
    source = source.convert("RGB")
    scale = min(WIDTH / source.width, HEIGHT / source.height)
    resized = source.resize(
        (round(source.width * scale), round(source.height * scale)),
        Image.Resampling.LANCZOS,
    )
    output = Image.new("RGB", (WIDTH, HEIGHT), BLACK)
    output.paste(resized, ((WIDTH - resized.width) // 2, (HEIGHT - resized.height) // 2))
    return output


def quantize_tricolor(source: Image.Image) -> Image.Image:
    palette_image = Image.new("P", (1, 1))
    palette = [*BLACK, *WHITE, *RED]
    palette.extend([0] * (768 - len(palette)))
    palette_image.putpalette(palette)
    quantized = source.quantize(
        palette=palette_image,
        dither=Image.Dither.FLOYDSTEINBERG,
    )
    return quantized.convert("RGB")


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
                raise ValueError(f"unexpected color after quantization: {color}")
    return bytes(black), bytes(red)


def build(source_path: Path, output_dir: Path) -> None:
    source_bytes = source_path.read_bytes()
    with Image.open(source_path) as source:
        fitted = fit_with_black_letterbox(source)
    preview = quantize_tricolor(fitted)
    black, red = image_to_planes(preview)
    black_crc = zlib.crc32(black)
    red_crc = zlib.crc32(red)
    header = HEADER.pack(
        MAGIC,
        WIDTH,
        HEIGHT,
        PLANE_BYTES,
        black_crc,
        red_crc,
        FORMAT_VERSION,
    )
    wallpaper = header + black + red

    output_dir.mkdir(parents=True, exist_ok=True)
    preview_path = output_dir / "wallpaper-preview.png"
    binary_path = output_dir / "wallpaper.bin"
    metadata_path = output_dir / "wallpaper.json"
    preview.save(preview_path, optimize=True)
    binary_path.write_bytes(wallpaper)
    metadata = {
        "format": "INKWALL1",
        "version": FORMAT_VERSION,
        "width": WIDTH,
        "height": HEIGHT,
        "header_bytes": HEADER.size,
        "plane_bytes": PLANE_BYTES,
        "total_bytes": len(wallpaper),
        "black_crc32": f"{black_crc:08x}",
        "red_crc32": f"{red_crc:08x}",
        "source_sha256": hashlib.sha256(source_bytes).hexdigest(),
        "wallpaper_sha256": hashlib.sha256(wallpaper).hexdigest(),
    }
    metadata_path.write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(metadata, ensure_ascii=False))
    print(preview_path)
    print(binary_path)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("artifacts") / "wallpaper",
    )
    args = parser.parse_args()
    build(args.source.resolve(), args.output_dir.resolve())


if __name__ == "__main__":
    main()
