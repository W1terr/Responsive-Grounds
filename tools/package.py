r"""Builds the FOMOD archive "Responsive Grounds <version>.zip". The version is the target's set_version in xmake.lua (a
trailing ".0" is left out of the name).

  00 Core           SKSE\Plugins\ResponsiveGrounds.dll, the translations, meshes\ResponsiveGrounds\Leaves.nif +
                    Particles.nif (tools/make_assets.py), textures\ResponsiveGrounds\Leaf_n, Debris, Snow (BC7) and
                    Sound\fx\ResponsiveGrounds\*.wav (tools/make_sounds.py)
  10 Riften Leaves  one of res\options\riften: Leaf.dds in orange, yellow, red or the 2.0 mix
  20 Green Leaves   one of res\options\green: GreenLeaf.dds + GreenLeaf_n.dds, broad (2.0), pine needles or both mixed

The installer shows the first row of each option's atlas (fomod\images); for green mixed two leaves of each row.
usage: python package.py [build dir] [output dir]"""

import io
import re
import sys
import zipfile
from pathlib import Path
from xml.sax.saxutils import escape

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
BUILD = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build"
OUTPUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "dist"
OPTIONS = ROOT / "res" / "options"

# (folder in the archive, group name, res\options folder, [(option folder, name, description)]); the first is the default
GROUPS = [
    ("10 Riften Leaves", "Riften leaves (autumn forests)", "riften", [
        ("orange", "Orange", "Orange autumn leaves in four shades."),
        ("yellow", "Yellow", "Golden yellow autumn leaves in four shades."),
        ("red", "Red", "Red autumn leaves in four shades."),
        ("mixed", "Mixed", "Orange, golden, red and brown leaves together (the 2.0 look). How much of each color can be set in the menu (Leaf mix page)."),
    ]),
    ("20 Green Leaves", "Green leaves (pine forests and other green land)", "green", [
        ("broad", "Broad", "Round green leaves (the 2.0 look)."),
        ("needles", "Pine needles", "Single long pine needles. A better match when the trees around are pines and spruces."),
        ("mixed", "Mixed", "Broad green leaves and pine needles together, in all four greens. How much of each can be set in the menu (Leaf mix page)."),
    ]),
]


def version(target: str) -> str:
    text = (ROOT / "xmake.lua").read_text(encoding="utf-8")
    match = re.search(r'target\("' + re.escape(target) + r'"\)\s*\n\s*set_version\("([^"]+)"\)', text)
    full = match.group(1) if match else "0.0.0"
    return re.sub(r"\.0$", "", full)


def preview(texture: Path, both_rows: bool = False) -> bytes:
    """the atlas' first row (4 leaves) over a dark forest floor color, as png; both_rows: the first two of row 1 and
    the last two of row 2 (an atlas with a different shape per row)"""
    img = np.asarray(Image.open(texture).convert("RGBA"))
    h, w = img.shape[0] // 2, img.shape[1] // 2
    row = np.concatenate([img[:h, :w], img[h:, w:]], axis=1) if both_rows else img[:h]
    bg = np.zeros_like(row[..., :3]) + np.array([38, 44, 34], dtype=np.uint8)
    shown = np.where(row[..., 3:4] > 128, row[..., :3], bg)
    out = io.BytesIO()
    Image.fromarray(shown).save(out, "PNG")
    return out.getvalue()


def option_xml(folder: str, image: str, name: str, description: str, default: bool) -> str:
    return f"""
                        <plugin name="{escape(name)}">
                            <description>{escape(description)}</description>
                            <image path="{escape(image)}"/>
                            <files>
                                <folder source="{escape(folder)}" destination="" priority="0"/>
                            </files>
                            <typeDescriptor>
                                <type name="{'Recommended' if default else 'Optional'}"/>
                            </typeDescriptor>
                        </plugin>"""


def module_config() -> str:
    groups = ""
    for archive_folder, group, source, options in GROUPS:
        plugins = "".join(option_xml(f"{archive_folder}\\{name}", f"fomod\\images\\{source}_{opt}.png", name, description, i == 0)
                          for i, (opt, name, description) in enumerate(options))
        groups += f"""
                <group name="{escape(group)}" type="SelectExactlyOne">
                    <plugins order="Explicit">{plugins}
                    </plugins>
                </group>"""
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<config xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="http://qconsulting.ca/fo3/ModConfig5.0.xsd">
    <moduleName>Responsive Grounds</moduleName>
    <requiredInstallFiles>
        <folder source="00 Core" destination=""/>
    </requiredInstallFiles>
    <installSteps order="Explicit">
        <installStep name="Leaves">
            <optionalFileGroups order="Explicit">{groups}
            </optionalFileGroups>
        </installStep>
    </installSteps>
</config>
"""


def info(ver: str) -> str:
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<fomod>
    <Name>Responsive Grounds</Name>
    <Author>W1ter</Author>
    <Version>{ver}</Version>
    <Description>Leaves, dust, pebbles and snow flakes kicked up from the ground by your steps, attacks, shouts, explosions and the wind, set up in SKSE Menu Framework.</Description>
    <Groups>
        <element>Visuals and Graphics</element>
    </Groups>
</fomod>
"""


def main():
    ver = version("ResponsiveGrounds")
    plugin = BUILD / "windows" / "x64" / "releasedbg"
    res = ROOT / "res"
    core = {
        "SKSE/Plugins/ResponsiveGrounds.dll": plugin / "ResponsiveGrounds.dll",
        "meshes/ResponsiveGrounds/Leaves.nif": res / "meshes" / "ResponsiveGrounds" / "Leaves.nif",
        "meshes/ResponsiveGrounds/Particles.nif": res / "meshes" / "ResponsiveGrounds" / "Particles.nif",
    }
    for texture in ("Leaf_n", "Debris", "Debris_n", "Snow", "Snow_n"):
        core[f"textures/ResponsiveGrounds/{texture}.dds"] = res / "textures" / "ResponsiveGrounds" / f"{texture}.dds"
    for sound in sorted((res / "Sound" / "fx" / "ResponsiveGrounds").glob("*.wav")):
        core[f"Sound/fx/ResponsiveGrounds/{sound.name}"] = sound
    for lang in sorted((res / "SKSE" / "Plugins" / "ResponsiveGrounds" / "lang").glob("*.json")):
        core[f"SKSE/Plugins/ResponsiveGrounds/lang/{lang.name}"] = lang
    files = {f"00 Core/{entry}": path for entry, path in core.items()}
    images = {}
    for archive_folder, _, source, options in GROUPS:
        for opt, name, _ in options:
            folder = OPTIONS / source / opt
            for path in sorted(folder.rglob("*")):
                if path.is_file():
                    files[f"{archive_folder}/{name}/" + path.relative_to(folder).as_posix()] = path
            atlas = next(folder.rglob("*Leaf.dds"), None)
            if atlas:
                images[f"fomod/images/{source}_{opt}.png"] = preview(atlas, (source, opt) == ("green", "mixed"))
    missing = [str(p) for p in files.values() if not p.exists()]
    if missing:
        sys.exit("missing: " + ", ".join(missing) + " (assets: tools/make_assets.py, tools/make_variants.py)")

    OUTPUT.mkdir(parents=True, exist_ok=True)
    archive = OUTPUT / f"Responsive Grounds {ver}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
        for entry, path in files.items():
            zf.write(path, entry)
        for entry, data in images.items():
            zf.writestr(entry, data)
        zf.writestr("fomod/info.xml", info(ver))
        zf.writestr("fomod/ModuleConfig.xml", module_config())
    print(f"wrote {archive} ({archive.stat().st_size / 1024:.0f} KB, {len(files) + len(images) + 2} files)")


if __name__ == "__main__":
    main()
