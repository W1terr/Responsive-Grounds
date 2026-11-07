r"""Writes the FOMOD's leaf texture options (BC7, compressed with texconv) into res/options:

  riften/orange, riften/yellow, riften/red   textures\ResponsiveGrounds\Leaf.dds: the aspen leaves (Leaf.dds layout of
                                             tools/make_assets.py) in four shades of one autumn color
  riften/mixed                               the 2.0 atlas (orange, golden, red, brown), kept as it is (not written here)
  green/broad                                the 2.0 green atlas + GreenLeaf_n.dds (a copy of Leaf_n.dds), not written here
  green/needles                              single pine needles (two shapes) in blue green, green, dark green and dry
                                             yellowish + their own normal map
  green/mixed                                broad leaves and pine needles together: the broad atlas' first row (its
                                             first leaf shape) over the needles' second row, in all four greens; the
                                             normal map put together the same way

The aspen leaf comes from the game (tools/gamefiles.py); texconv from SSEEdit/xLODGen (RG_TEXCONV). The riften ones are
only written when missing or with --riften.
usage: python make_variants.py [--riften] [preview.png]"""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

import gamefiles
from make_assets import CELL_H, CELL_W, COLS, FLIPBOOK, ROWS, ROOT, bleed, dds, height_normals, leaf_atlas, mips, mips_plain

OPTIONS = ROOT / "res" / "options"
TEXCONV = os.environ.get("RG_TEXCONV", r"D:\W1ter\Modding Tools\SSEEdit 4.1.5f\Edit Scripts\Texconvx64.exe")

# four shades of one color per option, (hue shift, saturation, value) on the orange aspen leaf (hue ~0.095)
RIFTEN = {
    "orange": [(0.0, 1.0, 1.0), (-0.012, 1.1, 0.94), (0.014, 1.0, 1.1), (-0.004, 0.86, 0.8)],
    "yellow": [(0.035, 1.05, 1.18), (0.042, 1.0, 1.24), (0.024, 1.0, 1.1), (0.018, 0.8, 0.95)],
    "red": [(-0.045, 1.1, 0.88), (-0.062, 1.15, 0.8), (-0.03, 1.1, 0.96), (-0.07, 1.0, 0.68)],
}

# single pine needles, drawn here: per atlas row one needle as (tip x, tip y, bend) in the cell's texels, from a little
# brown end at the bottom middle (BASE) where it came off the twig
NEEDLES = [
    (58, 6, 7.0),            # long, gently bowed
    (74, 22, -13.0),         # a bit shorter, more curved
]
BASE = (64.0, 249.0)
NEEDLE_WIDTH = 3.2        # half width in texels at the base (tapers to a point at the tip)
END_SIZE = (2.6, 5.0)     # half width, height of the brown end
# per atlas column: blue green (white pine), green, dark green, dry yellowish
NEEDLE_COLORS = [(86, 124, 104), (84, 132, 64), (54, 92, 52), (132, 128, 70)]
END_COLOR = (128, 98, 62)


def needle_cells():
    """one needle per row: a round needle lit across with a faint light line along it; returns per row
    (shade, alpha, height, brown end mask)"""
    h, w = CELL_H, CELL_W
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64) + 0.5
    bx, by = BASE
    rows = []
    for tx, ty, bend in NEEDLES:
        # a quadratic curve base -> tip, bowed sideways by bend
        mx, my = (bx + tx) / 2, (by + ty) / 2
        dx, dy = tx - bx, ty - by
        length = np.hypot(dx, dy)
        cx, cy = mx + bend * -dy / length, my + bend * dx / length
        t = np.linspace(0, 1, 600)
        px = (1 - t) ** 2 * bx + 2 * (1 - t) * t * cx + t ** 2 * tx
        py = (1 - t) ** 2 * by + 2 * (1 - t) * t * cy + t ** 2 * ty
        d2 = (xx[..., None] - px) ** 2 + (yy[..., None] - py) ** 2
        i = d2.argmin(axis=2)
        d = np.sqrt(np.take_along_axis(d2, i[..., None], axis=2)[..., 0])
        along = t[i]
        half = np.maximum(NEEDLE_WIDTH * np.clip((1 - along) / 0.12, 0, 1) ** 0.6 * (1 - 0.25 * along), 0.01)
        alpha = np.clip(half - d + 0.6, 0, 1) * (along < 0.999)
        across = np.clip(d / np.maximum(half, 0.3), 0, 1)
        shade = (0.72 + 0.32 * np.sqrt(1 - across ** 2) + 0.06 * (across < 0.3)) * (1.0 + 0.1 * (1 - along))
        height = np.sqrt(1 - across ** 2) * half
        # the brown end where the needle came off
        ew, eh = END_SIZE
        e = ((xx - bx) / ew) ** 2 + ((yy - (by - eh / 2)) / (eh / 2 + 0.5)) ** 2
        end_alpha = np.clip((1 - e) * 3, 0, 1)
        shade = np.where(end_alpha > 0.3, 0.8 + 0.25 * np.sqrt(np.clip(1 - e, 0, 1)), shade)
        height = np.maximum(height, np.sqrt(np.clip(1 - e, 0, 1)) * ew)
        alpha = np.maximum(alpha, end_alpha)
        rows.append((shade, alpha, height, end_alpha > 0.3))
    return rows


def needle_atlas():
    atlas = np.zeros((ROWS * CELL_H, COLS * CELL_W, 3))
    alpha = np.zeros((ROWS * CELL_H, COLS * CELL_W))
    height = np.zeros((ROWS * CELL_H, COLS * CELL_W))
    for r, (shade, a, hgt, end) in enumerate(needle_cells()):
        for c, color in enumerate(NEEDLE_COLORS):
            base = np.where(end[..., None], np.array(END_COLOR), np.array(color)) / 255.0
            rgb = np.clip(base * shade[..., None], 0, 1)
            ys, xs = slice(r * CELL_H, (r + 1) * CELL_H), slice(c * CELL_W, (c + 1) * CELL_W)
            atlas[ys, xs] = bleed(rgb, a)
            alpha[ys, xs] = a
            height[ys, xs] = hgt
    normals = np.concatenate([height_normals(height, 0.35), np.zeros(alpha.shape)[..., None]], axis=2)
    return atlas, alpha, normals


def half_mips(top, bottom):
    """one mip chain of two atlas rows made apart (each half keeps its own alpha tested coverage)"""
    levels = []
    for a, b in zip(mips(top), mips(bottom)):
        level = np.concatenate([a, b], axis=0)
        if levels and level.shape[0] != max(1, levels[-1].shape[0] // 2):
            break                        # the halves' 1x1 levels stacked (2x1): the 2x2 one averaged instead
        levels.append(level)
    if levels[-1].shape[:2] != (1, 1):
        levels.append(levels[-1].mean(axis=(0, 1), keepdims=True))
    return levels


def bc7(levels, path: Path):
    """the mip chain as BC7 (texconv keeps the given mips: the alpha tested coverage stays as mips() made it)"""
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        raw = Path(tmp) / path.name
        raw.write_bytes(dds(levels))
        out = Path(tmp) / "out"
        out.mkdir()
        subprocess.run([TEXCONV, "-nologo", "-y", "-f", "BC7_UNORM", "-o", str(out), str(raw)], check=True,
                       stdout=subprocess.DEVNULL)
        path.unlink(missing_ok=True)
        next(out.iterdir()).replace(path)   # texconv names it ".DDS"
    print(f"wrote {path.relative_to(ROOT)} ({path.stat().st_size // 1024} KB)")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    book = np.asarray(Image.open(gamefiles.get(FLIPBOOK)).convert("RGBA"))
    shown = []
    for name, grades in RIFTEN.items():
        atlas, alpha = leaf_atlas(book, grades)
        rgba = np.concatenate([atlas, alpha[..., None]], axis=2)
        path = OPTIONS / "riften" / name / "textures" / "ResponsiveGrounds" / "Leaf.dds"
        if "--riften" in sys.argv or not path.exists():
            bc7(mips(rgba), path)
        shown.append(rgba)
    atlas, alpha, normals = needle_atlas()
    rgba = np.concatenate([atlas, alpha[..., None]], axis=2)
    folder = OPTIONS / "green" / "needles" / "textures" / "ResponsiveGrounds"
    bc7(mips(rgba), folder / "GreenLeaf.dds")
    bc7(mips_plain(normals), folder / "GreenLeaf_n.dds")
    shown.append(rgba)
    # mixed: broad leaves (row 0 of the broad atlas, decoded from its BC7) over the needles' second row
    broad = OPTIONS / "green" / "broad" / "textures" / "ResponsiveGrounds"
    leaves = np.asarray(Image.open(broad / "GreenLeaf.dds").convert("RGBA")).astype(np.float64) / 255.0
    leaves_n = np.asarray(Image.open(broad / "GreenLeaf_n.dds").convert("RGBA")).astype(np.float64) / 255.0
    half = CELL_H
    folder = OPTIONS / "green" / "mixed" / "textures" / "ResponsiveGrounds"
    bc7(half_mips(leaves[:half], rgba[half:]), folder / "GreenLeaf.dds")
    bc7(mips_plain(np.concatenate([leaves_n[:half], normals[half:]], axis=0)), folder / "GreenLeaf_n.dds")
    shown.append(np.concatenate([leaves[:half], rgba[half:]], axis=0))
    if args:
        bg = np.array([0.16, 0.2, 0.15])
        tiles = [np.where(img[..., 3:4] > 0.5, img[..., :3], bg) for img in shown]
        Image.fromarray((np.clip(np.concatenate(tiles, axis=1), 0, 1) * 255).astype(np.uint8)).save(args[0])


if __name__ == "__main__":
    main()
