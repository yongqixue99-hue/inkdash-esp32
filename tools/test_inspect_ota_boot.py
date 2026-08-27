import struct
import tempfile
import unittest
from pathlib import Path

from inspect_ota_boot import APP_OFFSETS, FLASH_BYTES, OTADATA_OFFSET, SECTOR_BYTES
from inspect_ota_boot import inspect


def make_flash(path: Path, entries=()) -> None:
    image = bytearray(b"\xFF" * FLASH_BYTES)
    image[APP_OFFSETS[0]] = 0xE9
    image[APP_OFFSETS[1]] = 0xE9
    for sector, sequence, state, crc in entries:
        offset = OTADATA_OFFSET + sector * SECTOR_BYTES
        image[offset : offset + 32] = struct.pack(
            "<I20sII", sequence, b"\xFF" * 20, state, crc
        )
    path.write_bytes(image)


class InspectOtaBootTests(unittest.TestCase):
    def test_blank_otadata_defaults_to_ota_0(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "flash.bin"
            make_flash(path)
            self.assertEqual(inspect(path).offset, "0x10000")

    def test_valid_sequence_two_selects_ota_1(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "flash.bin"
            make_flash(path, [(0, 2, 2, 0x55F63774)])
            self.assertEqual(inspect(path).slot, "ota_1")

    def test_highest_valid_sequence_wins(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "flash.bin"
            make_flash(
                path,
                [(0, 2, 2, 0x55F63774), (1, 3, 2, 0xED4A5011)],
            )
            self.assertEqual(inspect(path).slot, "ota_0")

    def test_bad_crc_is_ignored(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "flash.bin"
            make_flash(path, [(0, 2, 2, 0x00000000)])
            self.assertEqual(inspect(path).slot, "ota_0")


if __name__ == "__main__":
    unittest.main()
