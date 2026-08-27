#!/usr/bin/env python3
"""Resolve the ESP-IDF OTA slot selected by a verified 4 MB flash backup."""

from __future__ import annotations

import argparse
import json
import struct
import zlib
from dataclasses import asdict, dataclass
from pathlib import Path


FLASH_BYTES = 0x400000
OTADATA_OFFSET = 0xD000
SECTOR_BYTES = 0x1000
ENTRY_BYTES = 32
APP_OFFSETS = (0x10000, 0x160000)
INVALID_STATES = (3, 4)


@dataclass(frozen=True)
class OtaEntry:
    sector: int
    sequence: int
    state: int
    stored_crc: int
    calculated_crc: int
    valid: bool


@dataclass(frozen=True)
class BootSelection:
    slot: str
    slot_index: int
    offset: str
    sequence: int | None
    reason: str
    entries: tuple[OtaEntry, OtaEntry]


def ota_crc(sequence: int) -> int:
    # ESP-IDF v4.4.7 uses esp_rom_crc32_le(UINT32_MAX, little-endian ota_seq, 4).
    return zlib.crc32(struct.pack("<I", sequence), 0xFFFFFFFF) & 0xFFFFFFFF


def parse_entry(data: bytes, sector: int) -> OtaEntry:
    sequence, _label, state, stored_crc = struct.unpack("<I20sII", data)
    calculated_crc = ota_crc(sequence)
    valid = (
        sequence != 0xFFFFFFFF
        and state not in INVALID_STATES
        and stored_crc == calculated_crc
    )
    return OtaEntry(
        sector=sector,
        sequence=sequence,
        state=state,
        stored_crc=stored_crc,
        calculated_crc=calculated_crc,
        valid=valid,
    )


def inspect(path: Path) -> BootSelection:
    if path.stat().st_size != FLASH_BYTES:
        raise ValueError("flash backup must be exactly 4194304 bytes")
    with path.open("rb") as stream:
        entries = []
        for sector in range(2):
            stream.seek(OTADATA_OFFSET + sector * SECTOR_BYTES)
            data = stream.read(ENTRY_BYTES)
            if len(data) != ENTRY_BYTES:
                raise ValueError("flash backup ended inside OTA data")
            entries.append(parse_entry(data, sector))

        valid_entries = [entry for entry in entries if entry.valid]
        if valid_entries:
            active = max(valid_entries, key=lambda entry: entry.sequence)
            sequence = active.sequence
            slot_index = ((sequence - 1) & 0xFFFFFFFF) % len(APP_OFFSETS)
            reason = f"valid otadata sector {active.sector}, sequence {sequence}"
        else:
            sequence = None
            slot_index = 0
            reason = "initial/invalid otadata; no factory partition, ESP-IDF defaults to ota_0"

        stream.seek(APP_OFFSETS[slot_index])
        if stream.read(1) != b"\xE9":
            other_index = 1 - slot_index
            stream.seek(APP_OFFSETS[other_index])
            if stream.read(1) == b"\xE9":
                slot_index = other_index
                reason += "; selected image lacks magic E9, bootloader fallback has E9"
            else:
                raise ValueError("neither OTA app slot begins with an ESP image header")

    return BootSelection(
        slot=f"ota_{slot_index}",
        slot_index=slot_index,
        offset=f"0x{APP_OFFSETS[slot_index]:X}",
        sequence=sequence,
        reason=reason,
        entries=(entries[0], entries[1]),
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("flash_backup", type=Path)
    args = parser.parse_args()
    selection = inspect(args.flash_backup.resolve())
    print(json.dumps(asdict(selection), separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
