"""Validate the Vita BC decoder/16-bit packing policy against Pillow's DDS decoder."""

from __future__ import annotations

import io
import os
import struct
import zipfile

from PIL import Image


ROOT = os.path.expandvars(r"%APPDATA%\Vita3K\Vita3K\ux0\app\FCRY00002\fcdata")
CASES = (
    ("Textures.pak", "Textures/controls.DDS"),       # BC1
    ("Textures.pak", "Textures/hud/hud.dds"),       # BC3 atlas
    ("Objects.pak", "Objects/characters/pmodels/hero/jc_head_dark2.dds"),
)


def rgb565(value: int) -> tuple[int, int, int]:
    r, g, b = value >> 11, (value >> 5) & 63, value & 31
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


def colors(block: bytes, dxt1: bool) -> list[tuple[int, int, int, int]]:
    c0, c1 = struct.unpack_from("<HH", block)
    p0, p1 = rgb565(c0), rgb565(c1)
    out = [(*p0, 255), (*p1, 255)]
    if dxt1 and c0 <= c1:
        out += [tuple((p0[i] + p1[i]) // 2 for i in range(3)) + (255,), (0, 0, 0, 0)]
    else:
        out += [tuple((2 * p0[i] + p1[i]) // 3 for i in range(3)) + (255,)]
        out += [tuple((p0[i] + 2 * p1[i]) // 3 for i in range(3)) + (255,)]
    return out


def decode(data: bytes, width: int, height: int, fourcc: bytes) -> bytes:
    block_size = 8 if fourcc == b"DXT1" else 16
    rgba = bytearray(width * height * 4)
    for by in range((height + 3) // 4):
        for bx in range((width + 3) // 4):
            block = data[(by * ((width + 3) // 4) + bx) * block_size:][:block_size]
            if fourcc == b"DXT1":
                alpha = [255] * 16
                table = colors(block, True)
                code = struct.unpack_from("<I", block, 4)[0]
            elif fourcc == b"DXT3":
                alpha = [((block[i // 2] >> (4 * (i & 1))) & 15) * 17 for i in range(16)]
                table = colors(block[8:], False)
                code = struct.unpack_from("<I", block, 12)[0]
            else:
                a0, a1 = block[0], block[1]
                at = [a0, a1]
                if a0 > a1:
                    at += [((8 - i) * a0 + (i - 1) * a1) // 7 for i in range(2, 8)]
                else:
                    at += [((6 - i) * a0 + (i - 1) * a1) // 5 for i in range(2, 6)] + [0, 255]
                bits = int.from_bytes(block[2:8], "little")
                alpha = [at[(bits >> (3 * i)) & 7] for i in range(16)]
                table = colors(block[8:], False)
                code = struct.unpack_from("<I", block, 12)[0]
            for py in range(4):
                for px in range(4):
                    x, y, i = bx * 4 + px, by * 4 + py, py * 4 + px
                    if x >= width or y >= height:
                        continue
                    r, g, b, a = table[(code >> (2 * i)) & 3]
                    rgba[(y * width + x) * 4:(y * width + x) * 4 + 4] = bytes((r, g, b, alpha[i] if fourcc != b"DXT1" else a))
    return bytes(rgba)


def quantize(pixel: tuple[int, int, int, int], fourcc: bytes) -> tuple[int, int, int, int]:
    r, g, b, a = pixel
    if fourcc == b"DXT1":
        return (r & 0xF8, g & 0xF8, b & 0xF8, 255 if a >= 128 else 0)
    return (r & 0xF0, g & 0xF0, b & 0xF0, a & 0xF0)


def main() -> None:
    for pak, name in CASES:
        with zipfile.ZipFile(os.path.join(ROOT, pak)) as archive:
            dds = archive.read(name)
        height, width = struct.unpack_from("<II", dds, 12)
        fourcc = dds[84:88]
        ours = decode(dds[128:], width, height, fourcc)
        reference = Image.open(io.BytesIO(dds)).convert("RGBA").tobytes()
        worst = 0
        for i in range(0, len(ours), 4):
            a = quantize(tuple(ours[i:i + 4]), fourcc)
            b = quantize(tuple(reference[i:i + 4]), fourcc)
            worst = max(worst, *(abs(a[j] - b[j]) for j in range(4)))
        # Different conforming BC decoders can round interpolation by one unit;
        # after 16-bit quantisation that is at most one output step.
        limit = 16 if fourcc != b"DXT1" else 8
        assert worst <= limit, (name, fourcc, worst)
        print(f"PASS {fourcc.decode()} {width}x{height} {name} worst={worst}")


if __name__ == "__main__":
    main()
