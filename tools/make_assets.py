"""Builds the leaf assets from the vanilla aspen leaf (the flip book the Rift's falling leaves use,
textures\\effects\\fxaspenleafanim.dds, 8 x 8 frames of one tumbling leaf):

  res/textures/ResponsiveGrounds/Leaf.dds       atlas 512 x 512: 4 x 2 cells of 128 x 256, one leaf each
  res/textures/ResponsiveGrounds/Leaf_n.dds     its normal map (veins from the leaf's own shading)
  res/textures/ResponsiveGrounds/GreenLeaf.dds  the same leaves green (summer forests), same layout and normal map
  res/meshes/ResponsiveGrounds/Leaves.nif       one small cupped leaf mesh per atlas cell (Leaf0 ... Leaf7), and
                                                the same over the green atlas (GreenLeaf0 ... GreenLeaf7, normal map
                                                GreenLeaf_n.dds)

The shipped Leaf.dds, GreenLeaf.dds and GreenLeaf_n.dds are FOMOD options in res/options (tools/make_variants.py):
Leaf.dds/GreenLeaf.dds here are the "mixed" and "broad" ones, GreenLeaf_n.dds of "broad" is a copy of Leaf_n.dds.

and the dirt and rock bits from vanilla effect textures (fxsnowsoftsub's soft puff shapes, fxdirtrockanim):

  res/textures/ResponsiveGrounds/Debris.dds   atlas 512 x 512, rows of 4: rock pebbles, dirt clods, 2 rock dust puffs (right half), dust puffs
  res/textures/ResponsiveGrounds/Debris_n.dds its normal map (domed pebbles and clods, flat puffs)
  res/meshes/ResponsiveGrounds/Particles.nif  RockPebble0-3, DirtClod0-3 (alpha tested), RockDust0-1, DirtDust0-3 (blended),
                                              SnowFlake0-3 (blended, over Snow.dds)

and the snow flakes, drawn here (soft little clumps of a few round blobs, bright in the middle, a little blue grey at
the edges):

  res/textures/ResponsiveGrounds/Snow.dds     atlas 256 x 256: 2 x 2 cells of 128, one flake each
  res/textures/ResponsiveGrounds/Snow_n.dds   its normal map (domed)

Cells: row 0 = frame 0 (flat, stem down), row 1 = frame 2 (flat, a little different light); columns = natural,
golden, red, dry brown (green atlas: green, yellow green, dark green, olive). Each mesh lies in the XY plane (front up = +Z), stem towards -Y, centered on the origin,
LEAF_W x LEAF_H units. Lit, double sided, alpha tested, no shadows cast.

usage: python make_assets.py [--textures] [preview.png]   (numpy, pillow, nifgen from the Blender Niftools addon)
       --textures: also write the (uncompressed) textures over the shipped BC7 ones
"""
import os
import struct
import sys
import warnings
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

import gamefiles

NIFGEN = os.environ.get("NIFGEN", str(Path(os.environ["APPDATA"]) / "Blender Foundation/Blender/4.4/scripts/addons/io_scene_niftools/dependencies"))
sys.path.insert(0, NIFGEN)
warnings.filterwarnings("ignore")
from nifgen.formats.nif import NifFile  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
TEXTURES = ROOT / "res" / "textures" / "ResponsiveGrounds"
MESHES = ROOT / "res" / "meshes" / "ResponsiveGrounds"
FLIPBOOK = r"textures\effects\fxaspenleafanim.dds"
TEMPLATE = r"meshes\architecture\riften\clutter\rtleaves01.nif"  # a lit, alpha tested BSTriShape with vertex colors
DIFFUSE_PATH = r"textures\ResponsiveGrounds\Leaf.dds"
NORMAL_PATH = r"textures\ResponsiveGrounds\Leaf_n.dds"
GREEN_PATH = r"textures\ResponsiveGrounds\GreenLeaf.dds"
GREEN_NORMAL_PATH = r"textures\ResponsiveGrounds\GreenLeaf_n.dds"   # its own: the pine needles option has another shape

FRAMES = [(0, 0), (0, 2)]          # (row, column) of the flip book frames used
CELL_W, CELL_H = 128, 256          # atlas cell
COLS, ROWS = 4, 2
LEAF_W, LEAF_H = 5.0, 10.0         # mesh size in game units (the cell's area, stem included)
CUP = 0.45                         # edges up this much
CURL = 0.55                        # tip and stem up this much
ALPHA_REF = 128                    # NiAlphaProperty threshold
BRIGHTNESS = 0.8                   # vertex colors (they multiply the textures): everything a bit darker than the textures

# The shipped textures are BC7 compressions of what this writes (smaller, done by hand), so the textures are only
# written with --textures; without it only the meshes are made.
WRITE_TEXTURES = "--textures" in sys.argv
ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]


def save_texture(name, data):
    if WRITE_TEXTURES:
        TEXTURES.mkdir(parents=True, exist_ok=True)
        (TEXTURES / name).write_bytes(data)


# ---------------------------------------------------------------- textures

def grade(rgb, hue, sat, val):
    """hue shift (turns), saturation and value multipliers on an (h, w, 3) float image 0..1"""
    from matplotlib.colors import hsv_to_rgb, rgb_to_hsv
    hsv = rgb_to_hsv(np.clip(rgb, 0, 1))
    hsv[..., 0] = (hsv[..., 0] + hue) % 1.0
    hsv[..., 1] = np.clip(hsv[..., 1] * sat, 0, 1)
    hsv[..., 2] = np.clip(hsv[..., 2] * val, 0, 1)
    return hsv_to_rgb(hsv)


GRADES = [
    (0.0, 1.0, 1.0),       # natural orange
    (0.035, 1.05, 1.18),   # golden
    (-0.045, 1.1, 0.88),   # red
    (0.0, 0.72, 0.62),     # dry brown
]

# the aspen leaf is orange (hue ~0.095): turned green, a little less saturated than the autumn ones
GREEN_GRADES = [
    (0.185, 0.78, 0.92),   # green
    (0.105, 0.82, 1.05),   # yellow green
    (0.225, 0.72, 0.68),   # dark green
    (0.075, 0.62, 0.74),   # olive
]


def frame_cell(book, row, col):
    """the frame cropped to the leaf (1:2), scaled to the cell, colors bled into the transparent part"""
    frame = book[row * 64:(row + 1) * 64, col * 64:(col + 1) * 64].astype(np.float64) / 255.0
    alpha = frame[..., 3]
    ys, xs = np.nonzero(alpha > 0.05)
    cx, cy = (xs.min() + xs.max() + 1) / 2, (ys.min() + ys.max() + 1) / 2
    h = max(ys.max() - ys.min() + 1, 2 * (xs.max() - xs.min() + 1)) + 4
    w = h / 2
    box = (cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2)
    # premultiplied, so the edges don't pick up the black of the empty texels
    pre = np.concatenate([frame[..., :3] * alpha[..., None], alpha[..., None]], axis=2)
    pad = 32
    big = np.zeros((64 + 2 * pad, 64 + 2 * pad, 4))
    big[pad:pad + 64, pad:pad + 64] = pre
    chans = []
    for c in range(4):
        img = Image.fromarray(big[..., c].astype(np.float32), mode="F")
        img = img.transform((CELL_W, CELL_H), Image.EXTENT, (box[0] + pad, box[1] + pad, box[2] + pad, box[3] + pad), Image.BICUBIC)
        chans.append(np.asarray(img, dtype=np.float64))
    pre = np.clip(np.stack(chans, axis=2), 0.0, 1.0)
    a = pre[..., 3]
    rgb = np.where(a[..., None] > 1e-3, pre[..., :3] / np.maximum(a[..., None], 1e-3), 0.0)
    return bleed(np.clip(rgb, 0, 1), a), a


def bleed(rgb, alpha, steps=24):
    """spreads the leaf's colors outward into the transparent texels (filtering and mips then stay leaf colored)"""
    known = alpha > 0.3
    rgb = rgb.copy()
    for _ in range(steps):
        acc = np.zeros_like(rgb)
        cnt = np.zeros(alpha.shape)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)):
            k = np.roll(np.roll(known, dy, 0), dx, 1)
            acc += np.roll(np.roll(rgb, dy, 0), dx, 1) * k[..., None]
            cnt += k
        grow = (~known) & (cnt > 0)
        rgb[grow] = acc[grow] / cnt[grow][:, None]
        known = known | grow
    rgb[~known] = rgb[known].mean(axis=0)
    return rgb


def mips(rgba, coverage_ref=None):
    """mip chain of an (h, w, 4) float image; alpha scaled per level to keep the alpha tested coverage"""
    levels = [rgba]
    ref = ALPHA_REF / 255.0
    base_cov = (rgba[..., 3] > ref).mean() if coverage_ref is None else coverage_ref
    cur = rgba
    while cur.shape[0] > 1 or cur.shape[1] > 1:
        h, w = max(1, cur.shape[0] // 2), max(1, cur.shape[1] // 2)
        a = cur[..., 3:4]
        pre = np.concatenate([cur[..., :3] * a, a], axis=2)
        pre = pre[:h * 2 if cur.shape[0] > 1 else 1, :w * 2 if cur.shape[1] > 1 else 1]
        if cur.shape[0] > 1:
            pre = (pre[0::2] + pre[1::2]) / 2
        if cur.shape[1] > 1:
            pre = (pre[:, 0::2] + pre[:, 1::2]) / 2
        a = pre[..., 3:4]
        rgb = np.where(a > 1e-4, pre[..., :3] / np.maximum(a, 1e-4), pre[..., :3])
        alpha = a[..., 0]
        if base_cov > 0 and alpha.size > 4:
            lo, hi = 0.25, 4.0  # scale so the share above the threshold matches the full size image
            for _ in range(20):
                mid = (lo + hi) / 2
                if (np.clip(alpha * mid, 0, 1) > ref).mean() < base_cov:
                    lo = mid
                else:
                    hi = mid
            alpha = np.clip(alpha * (lo + hi) / 2, 0, 1)
        cur = np.concatenate([np.clip(rgb, 0, 1), alpha[..., None]], axis=2)
        levels.append(cur)
    return levels


def dds(levels):
    """uncompressed B8G8R8A8 dds with the given mip chain"""
    h, w = levels[0].shape[:2]
    pixel_format = struct.pack("<II4sIIIII", 32, 0x41, b"\0\0\0\0", 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    header = struct.pack("<IIIIIII", 124, 0x2100F, h, w, w * 4, 0, len(levels)) + b"\0" * 44 + pixel_format
    header += struct.pack("<IIIII", 0x401008, 0, 0, 0, 0)
    body = bytearray()
    for lv in levels:
        px = np.clip(np.round(lv * 255), 0, 255).astype(np.uint8)
        body += px[..., [2, 1, 0, 3]].tobytes()
    return b"DDS " + header + bytes(body)


def normal_map(rgb, alpha):
    """tangent space normals from the leaf's brightness (veins and edge are lighter), flat where it's empty"""
    lum = rgb @ np.array([0.3, 0.59, 0.11])
    height = np.asarray(Image.fromarray((lum * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2)), dtype=np.float64) / 255.0
    height *= np.clip(alpha * 2, 0, 1)
    gx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * 2.5
    gy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * 2.5
    n = np.stack([-gx, gy, np.ones_like(gx)], axis=2)  # +V is down the image, the tangent frame's +Y is up
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    out = np.concatenate([n * 0.5 + 0.5, np.zeros(alpha.shape)[..., None]], axis=2)
    return out


def leaf_atlas(book, grades):
    atlas = np.zeros((ROWS * CELL_H, COLS * CELL_W, 3))
    alpha = np.zeros((ROWS * CELL_H, COLS * CELL_W))
    for r, (fr, fc) in enumerate(FRAMES):
        rgb, a = frame_cell(book, fr, fc)
        for c, (hue, sat, val) in enumerate(grades):
            ys, xs = slice(r * CELL_H, (r + 1) * CELL_H), slice(c * CELL_W, (c + 1) * CELL_W)
            atlas[ys, xs] = grade(rgb, hue, sat, val)
            alpha[ys, xs] = a
    return atlas, alpha


def build_textures():
    book = np.asarray(Image.open(gamefiles.get(FLIPBOOK)).convert("RGBA"))
    atlas, alpha = leaf_atlas(book, GRADES)
    rgba = np.concatenate([atlas, alpha[..., None]], axis=2)
    normals = normal_map(atlas, alpha)
    green, _ = leaf_atlas(book, GREEN_GRADES)
    green_rgba = np.concatenate([green, alpha[..., None]], axis=2)
    save_texture("Leaf.dds", dds(mips(rgba)))
    save_texture("Leaf_n.dds", dds(mips_plain(normals)))
    save_texture("GreenLeaf.dds", dds(mips(green_rgba)))
    return rgba, green_rgba


def mips_plain(img):
    levels = [img]
    cur = img
    while cur.shape[0] > 1 or cur.shape[1] > 1:
        if cur.shape[0] > 1:
            cur = (cur[0::2] + cur[1::2]) / 2
        if cur.shape[1] > 1:
            cur = (cur[:, 0::2] + cur[:, 1::2]) / 2
        n = cur[..., :3] * 2 - 1
        n /= np.maximum(np.linalg.norm(n, axis=2, keepdims=True), 1e-6)
        cur = np.concatenate([n * 0.5 + 0.5, cur[..., 3:4]], axis=2)
        levels.append(cur)
    return levels


# ---------------------------------------------------------------- debris (dirt and rocks)

DEBRIS = 128      # atlas cell; 4 x 4 cells, rows: rock pebbles, dirt clods, (empty 2) + rock dust (2), dirt dust
SOFT_PUFFS = r"textures\effects\fxsnowsoftsub.dds"    # 4 x 4 soft puffs (only their shapes are used)
DIRT_ROCKS = r"textures\effects\fxdirtrockanim.dds"   # 8 x 4 frames of a tumbling rock
# colors near the landscape's own diffuse (dirt02 ~ 43/42/35, dirtpath01 ~ 57/58/51), a bit
# lighter: the loose bits catch more light than the ground
DIRT_COLOR = np.array([58, 52, 42]) / 255.0
ROCK_COLOR = np.array([62, 64, 63]) / 255.0       # rocks01 ~ 40/43/43, tundrarocks01 ~ 61/59/47
ROCK_DUST_COLOR = np.array([104, 104, 100]) / 255.0
DUST_COLOR = np.array([96, 90, 78]) / 255.0


def load(name):
    return np.asarray(Image.open(gamefiles.get(name)).convert("RGBA")).astype(np.float64) / 255.0


def fit(img, box, size=DEBRIS, pad_frac=0.08):
    """the box (x0, y0, x1, y1) of an RGBA float image, squared up with a margin, scaled to size x size"""
    x0, y0, x1, y1 = box
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    half = max(x1 - x0, y1 - y0) / 2 * (1 + pad_frac)
    pre = np.concatenate([img[..., :3] * img[..., 3:4], img[..., 3:4]], axis=2)
    pad = int(half) + 4
    big = np.zeros((img.shape[0] + 2 * pad, img.shape[1] + 2 * pad, 4))
    big[pad:pad + img.shape[0], pad:pad + img.shape[1]] = pre
    chans = []
    for c in range(4):
        im = Image.fromarray(big[..., c].astype(np.float32), mode="F")
        im = im.transform((size, size), Image.EXTENT, (cx - half + pad, cy - half + pad, cx + half + pad, cy + half + pad), Image.BICUBIC)
        chans.append(np.asarray(im, dtype=np.float64))
    pre = np.clip(np.stack(chans, axis=2), 0, 1)
    a = pre[..., 3]
    rgb = np.where(a[..., None] > 1e-3, pre[..., :3] / np.maximum(a[..., None], 1e-3), 0.0)
    return np.clip(rgb, 0, 1), a


def height_normals(height, strength):
    gx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * strength
    gy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * strength
    n = np.stack([-gx, gy, np.ones_like(gx)], axis=2)
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    return n * 0.5 + 0.5


def blur(a, radius):
    from scipy import ndimage
    return ndimage.gaussian_filter(a.astype(np.float64), radius / 2.0, mode="constant")


def build_debris():
    size = 4 * DEBRIS
    rgb = np.zeros((size, size, 3))
    alpha = np.zeros((size, size))
    normal = np.zeros((size, size, 3)) + np.array([0.5, 0.5, 1.0])

    def put(row, col, c_rgb, c_a, c_n=None):
        ys, xs = slice(row * DEBRIS, (row + 1) * DEBRIS), slice(col * DEBRIS, (col + 1) * DEBRIS)
        rgb[ys, xs] = c_rgb
        alpha[ys, xs] = c_a
        if c_n is not None:
            normal[ys, xs] = c_n

    # rows 0 and 1: rock pebbles and dirt clods from the rock frames, colored, the rock's shading kept
    rocks = load(DIRT_ROCKS)
    for row, color, frames in ((0, ROCK_COLOR, (4, 13, 22, 31)), (1, DIRT_COLOR, (0, 9, 18, 27))):
        for col, frame in enumerate(frames):
            r, c = divmod(frame, 8)
            cell = rocks[r * 64:(r + 1) * 64, c * 64:(c + 1) * 64]
            ys, xs = np.nonzero(cell[..., 3] > 0.3)
            c_rgb, a = fit(cell, (xs.min(), ys.min(), xs.max() + 1, ys.max() + 1), pad_frac=0.25)
            lum = c_rgb @ np.array([0.3, 0.59, 0.11])
            mean = lum[a > 0.5].mean() if (a > 0.5).any() else max(lum.mean(), 1e-3)
            rel = lum / max(mean, 1e-3)
            put(row, col, bleed(np.clip(color * rel[..., None], 0, 1), a), a, height_normals(blur(a, 8) + lum * 0.3, 3.0))
    # rows 2 and 3: rock dust and dust puffs from the soft puffs (alpha strengthened, edges kept inside the cell)
    puffs = load(SOFT_PUFFS)
    radius = np.hypot(*np.meshgrid(np.linspace(-1, 1, DEBRIS), np.linspace(-1, 1, DEBRIS)))
    edge = np.clip((1.0 - radius) / 0.25, 0, 1)
    for col in range(4):
        cells = ((2, ROCK_DUST_COLOR, (col, col)),) if col >= 2 else ()  # row 2: two rock dust puffs on the right
        for row, color, (r, c) in cells + ((3, DUST_COLOR, (col, 3 - col)),):
            cell = puffs[r * 32:(r + 1) * 32, c * 32:(c + 1) * 32]
            big = np.asarray(Image.fromarray((cell[..., 3] * 255).astype(np.uint8)).resize((DEBRIS, DEBRIS), Image.BICUBIC), dtype=np.float64) / 255.0
            a = np.clip(blur(big, 2) * 1.6, 0, 1) * edge
            put(row, col, np.zeros((DEBRIS, DEBRIS, 3)) + color, a)

    rgba = np.concatenate([rgb, alpha[..., None]], axis=2)
    # the clump rows are alpha tested (coverage kept in the mips), the puff rows blended (plain mips)
    clump_levels = mips(rgba[: 2 * DEBRIS])
    puff_levels = mips_plain_rgba(rgba[2 * DEBRIS:])
    levels = [np.concatenate([a, b], axis=0) for a, b in zip(clump_levels, puff_levels)]
    levels = [lv for lv in levels if lv.shape[0] == lv.shape[1]]  # the halves' last levels don't stack square
    save_texture("Debris.dds", dds(levels))
    n = np.concatenate([normal, np.zeros((size, size, 1))], axis=2)
    save_texture("Debris_n.dds", dds(mips_plain(n)))
    return rgba


# ---------------------------------------------------------------- snow flakes

FLAKE = 128                                          # atlas cell, 2 x 2 cells
FLAKE_COLOR = np.array([236, 241, 248]) / 255.0      # lit by the scene: near white
FLAKE_EDGE = np.array([196, 206, 222]) / 255.0       # the thin edges a little blue grey
FLAKE_SIZE = 2.4                                     # mesh size in game units


def build_snow():
    rng = np.random.default_rng(7)
    size = 2 * FLAKE
    rgb = np.zeros((size, size, 3))
    alpha = np.zeros((size, size))
    normal = np.zeros((size, size, 3)) + np.array([0.5, 0.5, 1.0])
    yy, xx = np.mgrid[0:FLAKE, 0:FLAKE] / (FLAKE - 1) * 2 - 1
    for cell in range(4):
        # a clump of grains: many small soft dots in a loose cluster, its edge broken up by fine noise
        density = np.zeros((FLAKE, FLAKE))
        grains = 14 + 6 * cell
        spread = 0.22 + 0.04 * cell
        for _ in range(grains):
            cx, cy = np.clip(rng.normal(0, spread, 2), -0.6, 0.6)
            r = rng.uniform(0.06, 0.16)
            density += np.exp(-((xx - cx) ** 2 + (yy - cy) ** 2) / (2 * r * r))
        noise = blur(rng.random((FLAKE, FLAKE)), 3)
        noise = (noise - noise.mean()) / (noise.std() + 1e-6)
        density = density / max(density.max(), 1e-6) * 1.4
        density = np.clip(density + 0.12 * noise * np.clip(density * 4, 0, 1), 0, 1)  # noise only near the grains
        density *= np.clip((1.0 - np.hypot(xx, yy)) / 0.15, 0, 1)  # inside the cell
        a = np.clip((density - 0.18) * 2.0, 0, 1)
        a = blur(a, 1.5)
        shade = np.clip(density * 1.6, 0, 1)[..., None]
        c_rgb = FLAKE_EDGE + (FLAKE_COLOR - FLAKE_EDGE) * shade
        r, c = divmod(cell, 2)
        ys, xs = slice(r * FLAKE, (r + 1) * FLAKE), slice(c * FLAKE, (c + 1) * FLAKE)
        rgb[ys, xs] = bleed(c_rgb, a)
        alpha[ys, xs] = a
        normal[ys, xs] = height_normals(blur(density, 10), 6.0)
    rgba = np.concatenate([rgb, alpha[..., None]], axis=2)
    save_texture("Snow.dds", dds(mips_plain_rgba(rgba)))
    n = np.concatenate([normal, np.zeros((size, size, 1))], axis=2)
    save_texture("Snow_n.dds", dds(mips_plain(n)))
    return rgba


def mips_plain_rgba(img):
    levels = [img]
    cur = img
    while cur.shape[0] > 1 or cur.shape[1] > 1:
        a = cur[..., 3:4]
        pre = np.concatenate([cur[..., :3] * a, a], axis=2)
        if cur.shape[0] > 1:
            pre = (pre[0::2] + pre[1::2]) / 2
        if cur.shape[1] > 1:
            pre = (pre[:, 0::2] + pre[:, 1::2]) / 2
        a = pre[..., 3:4]
        rgb = np.where(a > 1e-4, pre[..., :3] / np.maximum(a, 1e-4), pre[..., :3])
        cur = np.concatenate([rgb, a], axis=2)
        levels.append(cur)
    return levels


# ---------------------------------------------------------------- mesh

def card_vertices(uv_box, width, height, cup, curl, grid_x, grid_y, rim_shade):
    """a card in the XY plane (front up = +Z) over the atlas rectangle uv_box (u0, v0, u1, v1), +Y = up the image,
    edges raised by cup (across) and curl (along)"""
    u0, v0, u1, v1 = uv_box
    verts = []
    for y in grid_y:
        for x in grid_x:
            p = np.array([x * width / 2, y * height / 2, cup * x * x + curl * y * y])
            dzdx = 2 * cup * x / (width / 2)
            dzdy = 2 * curl * y / (height / 2)
            n = np.array([-dzdx, -dzdy, 1.0])
            n /= np.linalg.norm(n)
            du = np.array([1.0, 0.0, dzdx])            # dP/du direction (u along +X)
            du -= n * du.dot(n)
            du /= np.linalg.norm(du)
            dv = np.cross(n, du) * -1.0                # dP/dv direction (v down the image = towards -Y)
            u = u0 + (x * 0.5 + 0.5) * (u1 - u0)
            v = v0 + (0.5 - y * 0.5) * (v1 - v0)
            edge = max(abs(x), abs(y))
            shade = int(round((255 - rim_shade * edge * edge) * BRIGHTNESS))
            verts.append((p, n, dv, du, (u, v), (shade, shade, shade, 255)))
    return verts


def card_triangles(nx, ny):
    tris = []
    for j in range(ny - 1):
        for i in range(nx - 1):
            a, b, c, d = j * nx + i, j * nx + i + 1, (j + 1) * nx + i, (j + 1) * nx + i + 1
            tris += [(a, b, d), (a, d, c)]
    return tris


LEAF_GRID_X = [-1.0, -0.5, 0.0, 0.5, 1.0]          # across the leaf (-1..1)
LEAF_GRID_Y = [-1.0, -0.6, -0.2, 0.2, 0.6, 1.0]    # along it, stem (-1) to tip (1)


def leaf_shapes():
    out = []
    tris = card_triangles(len(LEAF_GRID_X), len(LEAF_GRID_Y))
    for prefix, key in (("Leaf", "leaf"), ("GreenLeaf", "greenleaf")):
        for cell in range(COLS * ROWS):
            col, row = cell % COLS, cell // COLS
            box = (col / COLS, row / ROWS, (col + 1) / COLS, (row + 1) / ROWS)
            out.append((f"{prefix}{cell}", card_vertices(box, LEAF_W, LEAF_H, CUP, CURL, LEAF_GRID_X, LEAF_GRID_Y, 22), tris, key))
    return out


def debris_shapes():
    """RockPebble0-3, DirtClod0-3 (alpha tested, slightly domed), RockDust0-1, DirtDust0-3 (blended flat
    cards that the DLL turns to the camera)"""
    out = []
    g3 = [-1.0, 0.0, 1.0]
    clump_tris = card_triangles(3, 3)
    flat = [-1.0, 1.0]
    flat_tris = card_triangles(2, 2)
    for row, prefix, size, kind in ((0, "RockPebble", 2.0, "clump"), (1, "DirtClod", 2.4, "clump"), (2, "RockDust", 12.0, "puff"), (3, "DirtDust", 12.0, "puff")):
        for col in range(4):
            box = (col / 4, row / 4, (col + 1) / 4, (row + 1) / 4)
            if prefix == "RockDust":
                if col >= 2:  # the left half of the row is empty
                    out.append((f"RockDust{col - 2}", card_vertices(box, size, size, 0.0, 0.0, flat, flat, 0), flat_tris, "puff"))
                continue
            if kind == "clump":
                dome = -0.25 * size / 3  # the middle bulges up (edges down)
                out.append((f"{prefix}{col}", card_vertices(box, size, size, dome, dome, g3, g3, 10), clump_tris, "clump"))
            else:
                out.append((f"{prefix}{col}", card_vertices(box, size, size, 0.0, 0.0, flat, flat, 0), flat_tris, "puff"))
    # snow flakes: little flat cards over the snow atlas, slightly domed, tumbling like tiny leaves
    for cell in range(4):
        r, c = divmod(cell, 2)
        box = (c / 2, r / 2, (c + 1) / 2, (r + 1) / 2)
        out.append((f"SnowFlake{cell}", card_vertices(box, FLAKE_SIZE, FLAKE_SIZE, -0.08, -0.08, g3, g3, 0), clump_tris, "flake"))
    return out


def set_vec(v, values):
    for name, value in zip(("x", "y", "z", "w"), values):
        setattr(v, name, float(value))


def set_identity(m):
    m.m_11, m.m_21, m.m_31 = 1.0, 0.0, 0.0
    m.m_12, m.m_22, m.m_32 = 0.0, 1.0, 0.0
    m.m_13, m.m_23, m.m_33 = 0.0, 0.0, 1.0


def set_flags(field, value):
    t = type(field)
    return t.from_value(value) if hasattr(t, "from_value") else value


def template_shape():
    return next(b for b in NifFile.from_path(str(gamefiles.get(TEMPLATE))).blocks if type(b).__name__ == "BSTriShape")


# material keys -> (diffuse, normal, blended)
MATERIALS = {
    "leaf": (DIFFUSE_PATH, NORMAL_PATH, False),
    "greenleaf": (GREEN_PATH, GREEN_NORMAL_PATH, False),
    "clump": (r"textures\ResponsiveGrounds\Debris.dds", r"textures\ResponsiveGrounds\Debris_n.dds", False),
    "puff": (r"textures\ResponsiveGrounds\Debris.dds", r"textures\ResponsiveGrounds\Debris_n.dds", True),
    "flake": (r"textures\ResponsiveGrounds\Snow.dds", r"textures\ResponsiveGrounds\Snow_n.dds", True),
}


def make_material(shape, key):
    """lit, receives shadows, double sided, vertex colors; alpha tested and z writing, or blended without z writes"""
    diffuse, normal, blended = MATERIALS[key]
    shader, alpha = shape.shader_property, shape.alpha_property
    shader.shader_flags_1 = set_flags(shader.shader_flags_1, (1 << 8) | (1 << 31))
    shader.shader_flags_2 = set_flags(shader.shader_flags_2, (0 if blended else 1 << 0) | (1 << 3) | (1 << 4) | (1 << 5))
    shader.texture_set.textures[0] = diffuse
    shader.texture_set.textures[1] = normal
    shader.alpha = 1.0
    shader.glossiness = 10.0
    shader.specular_strength = 0.0
    set_vec(shader.emissive_color, (0, 0, 0))
    shader.emissive_multiple = 0.0
    if blended:
        alpha.flags = set_flags(alpha.flags, 0x12ED)  # blend src alpha / inv src alpha, discard (almost) empty texels
        alpha.threshold = 4
    else:
        alpha.flags = set_flags(alpha.flags, 0x12EC)  # alpha test (greater), no blending
        alpha.threshold = ALPHA_REF
    return shader, alpha


def write_nif(path, specs):
    """specs: (name, vertices, triangles, material key); one shared material per key"""
    nif = NifFile.from_path(str(gamefiles.get(TEMPLATE)))
    root = nif.roots[0]
    first = next(b for b in nif.blocks if type(b).__name__ == "BSTriShape")
    materials = {}
    shapes = []
    for name, verts, tris, key in specs:
        shape = first if not shapes else template_shape()
        if key not in materials:
            materials[key] = make_material(shape, key)
        shape.shader_property, shape.alpha_property = materials[key]
        shape.name = name
        shape.flags = 14
        set_identity(shape.rotation)
        set_vec(shape.translation, (0, 0, 0))
        shape.scale = 1.0
        shape.num_vertices = len(verts)
        shape.num_triangles = len(tris)
        shape.data_size = shape.num_vertices * 32 + shape.num_triangles * 6
        shape.reset_field("vertex_data")
        shape.reset_field("triangles")
        P = np.array([v[0] for v in verts])
        for i, (p, n, t, b, uv, color) in enumerate(verts):
            out = shape.vertex_data[i]
            set_vec(out.vertex, p)
            set_vec(out.normal, n)
            set_vec(out.tangent, t)
            out.bitangent_x, out.bitangent_y, out.bitangent_z = (float(x) for x in b)
            out.uv.u, out.uv.v = float(uv[0]), float(uv[1])
            out.vertex_colors.r, out.vertex_colors.g, out.vertex_colors.b, out.vertex_colors.a = color
        for i, tri in enumerate(tris):
            t = shape.triangles[i]
            t.v_1, t.v_2, t.v_3 = tri
        c = P.mean(axis=0)
        set_vec(shape.bounding_sphere.center, c)
        shape.bounding_sphere.radius = float(np.max(np.linalg.norm(P - c, axis=1)))
        shapes.append(shape)

    root.name = path.name
    root.flags = 14
    root.num_children = len(shapes)
    root.reset_field("children")
    for i, s in enumerate(shapes):
        root.children[i] = s
    adopt(nif)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as stream:
        nif.write(stream)
    return len(shapes)


def adopt(nif):
    """Blocks taken from other copies of the template still point at them as their context (refs are written through it)."""
    blocks, index, types, type_index = [], {}, [], {}
    nif.blocks = blocks
    for r in nif.roots:
        nif._makeBlockList(r, index, types, type_index)
    seen = set()
    block_ids = {id(b) for b in nif.blocks}

    def visit(obj, top=False):
        if id(obj) in seen or (not top and id(obj) in block_ids):
            return
        seen.add(id(obj))
        if not hasattr(obj, "_context") or not hasattr(type(obj), "_get_filtered_attribute_list"):
            return
        obj._context = nif
        try:
            fields = list(type(obj)._get_filtered_attribute_list(obj, include_abstract=False))
        except TypeError:
            return
        for name, *_ in fields:
            value = getattr(obj, name, None)
            if value is None or isinstance(value, (int, float, str, bytes, np.ndarray)):
                continue
            if type(value).__name__ == "Array" or isinstance(value, (list, tuple)):
                if hasattr(value, "_context"):
                    value._context = nif
                for item in value:
                    if item is not None and not isinstance(item, (int, float, str, bytes, np.ndarray)):
                        visit(item)
            else:
                visit(value)

    for b in list(nif.blocks):
        visit(b, top=True)


def main():
    rgba, green = build_textures()
    debris = build_debris()
    snow = build_snow()
    leaves = write_nif(MESHES / "Leaves.nif", leaf_shapes())
    particles = write_nif(MESHES / "Particles.nif", debris_shapes())
    print(f"textures {'-> ' + str(TEXTURES) if WRITE_TEXTURES else 'kept (no --textures)'}, {leaves} leaf meshes -> Leaves.nif, {particles} dirt/rock meshes -> Particles.nif")
    if ARGS:
        for img, out, blended in ((rgba, ARGS[0], False), (green, ARGS[0].replace(".png", "_green.png"), False),
                                  (debris, ARGS[0].replace(".png", "_debris.png"), True),
                                  (snow, ARGS[0].replace(".png", "_snow.png"), "all")):
            bg = np.zeros_like(img[..., :3]) + np.array([0.18, 0.22, 0.16])
            a = img[..., 3:4]
            shown = np.where(a > ALPHA_REF / 255.0, img[..., :3], bg)
            if blended == "all":
                shown = bg * (1 - a) + img[..., :3] * a
            elif blended:
                half = img.shape[0] // 2
                shown[half:] = bg[half:] * (1 - a[half:]) + img[half:, :, :3] * a[half:]
            Image.fromarray((np.clip(shown, 0, 1) * 255).astype(np.uint8)).save(out)


if __name__ == "__main__":
    main()
