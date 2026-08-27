from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import tempfile

import pytest


ROOT = Path(__file__).resolve().parents[1]


def test_native_persistent_layout_records() -> None:
    compiler = shutil.which("g++")
    if compiler is None:
        pytest.skip("g++ is unavailable")
    with tempfile.TemporaryDirectory() as temporary:
        executable = Path(temporary) / "persistent-layout-test.exe"
        result = subprocess.run(
            [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Iinclude",
                "tests/native_persistent_layout_test.cpp",
                "src/wifi_config_record.cpp",
                "src/firmware_journal_record.cpp",
                "src/wallpaper_cache_record.cpp",
                "-o",
                str(executable),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        assert result.returncode == 0, result.stdout + result.stderr
        executed = subprocess.run(
            [str(executable)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        assert executed.returncode == 0, executed.stdout + executed.stderr
