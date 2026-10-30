"""Reads vanilla files out of the game's BSA archives (cached on disk, so each file is unpacked once)."""
import os
from pathlib import Path

from bsa_extract import entries, read

DATA = Path(os.environ.get("SKYRIM_DATA", r"D:\Games\steamapps\common\Skyrim Special Edition\Data"))
CACHE = Path(os.environ.get("RG_CACHE", r"D:\SkyrimBuild\ResponsiveGrounds\cache"))
ARCHIVES = ("Skyrim - Meshes0.bsa", "Skyrim - Meshes1.bsa", "Skyrim - Animations.bsa", "Skyrim - Misc.bsa",
            "Skyrim - Sounds.bsa", "Skyrim - Textures0.bsa", "Skyrim - Textures1.bsa", "Skyrim - Textures2.bsa",
            "Skyrim - Textures3.bsa", "Skyrim - Textures4.bsa", "Skyrim - Textures5.bsa", "Skyrim - Textures6.bsa",
            "Skyrim - Textures7.bsa", "Skyrim - Textures8.bsa")

_index = None


def _load_index():
    global _index
    if _index is None:
        _index = {}
        for archive in ARCHIVES:
            path = DATA / archive
            if not path.exists():
                continue
            flags, files = entries(str(path))
            for name, size, off in files:
                _index.setdefault(name, (str(path), flags, size, off))
    return _index


def get(name: str) -> Path:
    """Path of a cached copy of a game file, e.g. get(r"meshes\\dlc02\\traps\\apotentacletrap\\apotentacletrap01.nif")"""
    key = name.lower().replace("/", "\\")
    out = CACHE / key.replace("\\", "/")
    if out.exists():
        return out
    loose = DATA / key
    if loose.exists():
        return loose
    found = _load_index().get(key)
    if not found:
        raise FileNotFoundError(name)
    path, flags, size, off = found
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(read(path, flags, size, off))
    return out
