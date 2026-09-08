#!/usr/bin/env python3
"""Validate the retail compass mask and Vita's baked-alpha equivalent."""

from __future__ import annotations

import io
import math
import zipfile
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
TEXTURES = ROOT / "engine_port" / "vita_kit" / "ux0_data" / "farcry" / "fcdata" / "Textures.pak"


def load_rgba(archive: zipfile.ZipFile, wanted: str) -> Image.Image:
    names = {name.lower(): name for name in archive.namelist()}
    return Image.open(io.BytesIO(archive.read(names[wanted.lower()]))).convert("RGBA")


def main() -> None:
    with zipfile.ZipFile(TEXTURES) as archive:
        compass = load_rgba(archive, "Textures/hud/compass.dds")
        mask = load_rgba(archive, "Textures/hud/compass_mask.dds")
        player = load_rgba(archive, "Textures/hud/RadarPlayer.dds")

    assert compass.size == mask.size == (256, 256)
    assert player.size == (64, 64)
    assert compass.getchannel("A").getextrema() == (255, 255)

    width, height = compass.size
    mask_alpha = mask.getchannel("A")
    assert mask_alpha.getpixel((0, 0)) >= 240
    assert mask_alpha.getpixel((width // 2, height // 2)) <= 48

    # The Vita stores the composed compass as RGBA4444.  Prove that standard
    # source-alpha blending with (1-mask) matches the desktop destination-alpha
    # equation to within one 4-bit alpha step everywhere.
    worst = 0
    visible = 0
    transparent_corners = 0
    for y in range(height):
        for x in range(width):
            authored = mask_alpha.getpixel((x, y))
            mask_a4 = authored >> 4
            baked_a4 = 15 - mask_a4
            baked = baked_a4 * 255 // 15
            desktop = 255 - authored
            worst = max(worst, abs(baked - desktop))
            visible += baked_a4 != 0
            if (x, y) in ((0, 0), (width - 1, 0), (0, height - 1), (width - 1, height - 1)):
                transparent_corners += baked_a4 == 0

    assert worst <= 17
    assert transparent_corners == 4
    assert 0 < visible < width * height

    # The player marker has real transparent padding; the half-texel inset in
    # GameRadar.cpp prevents bilinear edge bleed while retaining its arrow.
    alpha = player.getchannel("A")
    assert alpha.getextrema() == (0, 255)
    assert alpha.getpixel((player.width // 2, player.height // 2)) == 255

    print(
        "radar asset/mask contract: PASS "
        f"visible={visible}/{width * height} worst_alpha_error={worst}"
    )


if __name__ == "__main__":
    main()
