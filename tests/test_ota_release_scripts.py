from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import tempfile

import pytest


ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT / "artifacts" / "ota" / "repository"
TEST_VERSION = "2026082302"


def powershell() -> str:
    executable = shutil.which("pwsh") or shutil.which("powershell")
    if executable is None:
        pytest.skip("PowerShell is unavailable")
    return executable


def require_local_release_artifacts() -> Path:
    firmware = REPOSITORY / "test" / TEST_VERSION / "firmware.bin"
    required = (
        firmware,
        ROOT / "artifacts" / "ota" / "inkdash-ota-p256-public.pem",
        ROOT / "artifacts" / "private" / "inkdash-ota-p256-private.pk8",
    )
    if not all(path.is_file() for path in required):
        pytest.skip("ignored local OTA release artifacts are unavailable")
    return firmware


def test_publisher_rejects_mislabeled_binary() -> None:
    firmware = require_local_release_artifacts()
    with tempfile.TemporaryDirectory() as raw_temporary:
        result = subprocess.run(
            [
                powershell(),
                "-NoProfile",
                "-File",
                str(ROOT / "scripts" / "Publish-InkDashOtaRelease.ps1"),
                "-VersionCode",
                "2026082303",
                "-Version",
                "1.0.2-test",
                "-Channel",
                "test",
                "-FirmwarePath",
                str(firmware),
                "-OutputRoot",
                raw_temporary,
                "-BaseUrl",
                "https://updates.example.invalid/firmware",
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
        assert result.returncode != 0
        assert "release identity" in result.stdout + result.stderr


def test_builder_rejects_conflicting_probe_modes() -> None:
    result = subprocess.run(
        [
            powershell(),
            "-NoProfile",
            "-File",
            str(ROOT / "scripts" / "Build-InkDashOtaRelease.ps1"),
            "-VersionCode",
            "2026082401",
            "-Version",
            "1.0.1-probe",
            "-RollbackProbe",
            "-CacheProbe",
            "-BaseUrl",
            "https://updates.example.invalid/firmware",
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    assert result.returncode != 0
    assert "mutually exclusive" in result.stdout + result.stderr
