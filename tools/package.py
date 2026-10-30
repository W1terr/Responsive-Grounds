r"""Builds the mod archive "Responsive Grounds <version>.zip": SKSE\Plugins\ResponsiveGrounds.dll and the assets
(meshes\ResponsiveGrounds\Leaves.nif + Particles.nif, textures\ResponsiveGrounds\Leaf*.dds + Debris*.dds from
tools/make_assets.py). The version is the target's set_version in xmake.lua (a trailing ".0" is left out of the name).
usage: python package.py [build dir] [output dir]"""

import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build"
OUTPUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "dist"


def version(target: str) -> str:
    text = (ROOT / "xmake.lua").read_text(encoding="utf-8")
    match = re.search(r'target\("' + re.escape(target) + r'"\)\s*\n\s*set_version\("([^"]+)"\)', text)
    full = match.group(1) if match else "0.0.0"
    return re.sub(r"\.0$", "", full)


def main():
    plugin = BUILD / "windows" / "x64" / "releasedbg"
    res = ROOT / "res"
    files = {
        "SKSE/Plugins/ResponsiveGrounds.dll": plugin / "ResponsiveGrounds.dll",
        "meshes/ResponsiveGrounds/Leaves.nif": res / "meshes" / "ResponsiveGrounds" / "Leaves.nif",
        "textures/ResponsiveGrounds/Leaf.dds": res / "textures" / "ResponsiveGrounds" / "Leaf.dds",
        "textures/ResponsiveGrounds/Leaf_n.dds": res / "textures" / "ResponsiveGrounds" / "Leaf_n.dds",
        "meshes/ResponsiveGrounds/Particles.nif": res / "meshes" / "ResponsiveGrounds" / "Particles.nif",
        "textures/ResponsiveGrounds/Debris.dds": res / "textures" / "ResponsiveGrounds" / "Debris.dds",
        "textures/ResponsiveGrounds/Debris_n.dds": res / "textures" / "ResponsiveGrounds" / "Debris_n.dds",
    }
    for lang in sorted((res / "SKSE" / "Plugins" / "ResponsiveGrounds" / "lang").glob("*.json")):
        files[f"SKSE/Plugins/ResponsiveGrounds/lang/{lang.name}"] = lang
    missing = [str(p) for p in files.values() if not p.exists()]
    if missing:
        sys.exit("missing: " + ", ".join(missing) + " (assets: run tools/make_assets.py)")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    archive = OUTPUT / f"Responsive Grounds {version('ResponsiveGrounds')}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
        for entry, path in files.items():
            zf.write(path, entry)
    print(f"wrote {archive} ({archive.stat().st_size / 1024:.0f} KB, {len(files)} files)")


if __name__ == "__main__":
    main()
