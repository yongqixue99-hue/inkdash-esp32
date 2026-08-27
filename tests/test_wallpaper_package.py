from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile

from PIL import Image


PROJECT_ROOT = Path(__file__).resolve().parent.parent
BUILDER_PATH = PROJECT_ROOT / "tools" / "build_network_wallpaper.py"


def load_builder_module():
    spec = importlib.util.spec_from_file_location("inkdash_wallpaper_builder", BUILDER_PATH)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_wallpaper_builder_emits_exact_inkwall1_package() -> None:
    module = load_builder_module()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source = root / "source.png"
        Image.new("RGB", (800, 480), (255, 255, 255)).save(source)
        module.build(source, root / "output")
        payload = (root / "output" / "wallpaper.bin").read_bytes()
        assert len(payload) == 96_028
        assert payload[:8] == b"INKWALL1"
