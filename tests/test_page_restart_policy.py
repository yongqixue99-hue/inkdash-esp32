from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import tempfile

import pytest


PROJECT_ROOT = Path(__file__).resolve().parent.parent


@pytest.mark.parametrize(
    ("source", "extra_source"),
    (
        ("tests/native_page_restart_policy_test.cpp", None),
        ("tests/native_page_state_record_test.cpp", "src/page_state_record.cpp"),
        ("tests/native_wallpaper_format_test.cpp", "src/wallpaper_format.cpp"),
    ),
)
def test_native_page_restart_contract(source: str, extra_source: str | None) -> None:
    compiler = shutil.which("g++")
    if compiler is None:
        pytest.skip("g++ is unavailable")
    with tempfile.TemporaryDirectory() as temporary:
        executable = Path(temporary) / "page-restart-test.exe"
        command = [
            compiler,
            "-std=c++11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Iinclude",
            source,
        ]
        if extra_source is not None:
            command.append(extra_source)
        command.extend(("-o", str(executable)))
        compiled = subprocess.run(
            command,
            cwd=PROJECT_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        assert compiled.returncode == 0, compiled.stdout + compiled.stderr
        executed = subprocess.run(
            [str(executable)],
            cwd=PROJECT_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        assert executed.returncode == 0, executed.stdout + executed.stderr
