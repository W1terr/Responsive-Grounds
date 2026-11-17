r"""Writes the ash flake texture (BC7, compressed with texconv like make_variants.py):

  res/textures/ResponsiveGrounds/Ash.dds   the snow flakes (the shipped Snow.dds, 2 x 2 cells) in Solstheim's ash grey;
                                           the flakes share Snow_n.dds

The meshes over it are made by make_assets.py (AshFlake0-3).
usage: python make_ash.py [preview.png]"""
import sys

import numpy as np
from PIL import Image

from make_assets import TEXTURES, mips_plain_rgba
from make_variants import bc7

# the land's ash diffuse is a neutral grey (volcanic_ash_01..05 ~ 67-71, the lighter grains ~ 100); lit like the snow
# flakes (whose white stands for the snow's 80-98) the flakes come out a light warm grey
ASH_COLOR = np.array([160, 156, 150]) / 255.0
ASH_EDGE = np.array([112, 109, 105]) / 255.0


def ash():
    snow = np.asarray(Image.open(TEXTURES / "Snow.dds").convert("RGBA")).astype(np.float64) / 255.0
    lum = snow[..., :3] @ np.array([0.3, 0.59, 0.11])
    solid = snow[..., 3] > 0.5
    lo, hi = np.percentile(lum[solid], 5), np.percentile(lum[solid], 95)
    t = np.clip((lum - lo) / max(hi - lo, 1e-3), 0, 1)[..., None]
    rgb = ASH_EDGE + (ASH_COLOR - ASH_EDGE) * t
    rgba = np.concatenate([rgb, snow[..., 3:4]], axis=2)
    bc7(mips_plain_rgba(rgba), TEXTURES / "Ash.dds")
    return rgba


def main():
    rgba = ash()
    if len(sys.argv) > 1:
        a = rgba[..., 3:4]
        shown = np.array([0.27, 0.27, 0.26]) * (1 - a) + rgba[..., :3] * a
        Image.fromarray((np.clip(shown, 0, 1) * 255).astype(np.uint8)).save(sys.argv[1])


if __name__ == "__main__":
    main()
