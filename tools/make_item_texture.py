"""
Generates the item textures shipped in src/open_yoku_rando/files/items/.

A `.sim` is a header of nine little-endian u32 (magic, width, height, width and height padded to 4, format 7 = BC3,
0, uncompressed and compressed size) followed by one zlib stream holding the BC3 mip chain; see build_sim.

It needs Pillow, which the patcher must not depend on, so the finished bytes are committed and the patcher only
copies them. `--check` proves a committed file still matches a fresh build.

The source is Randovania's logo, `randovania/data/icons/rdv_logo_blue.ico` in a Randovania checkout (GPL-3.0 like
Randovania itself and like this repository).

Usage:
    uv run --with pillow python tools/make_item_texture.py --logo <rdv_logo_blue.ico>
    uv run --with pillow python tools/make_item_texture.py --logo <rdv_logo_blue.ico> --check
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

from PIL import Image

REPO_ROOT = Path(__file__).parent.parent
OUTPUT_DIR = REPO_ROOT / "src" / "open_yoku_rando" / "files" / "items"

SIM_MAGIC = 0x73696D0B  # 0x0b + "mis", the tag byte and the reversed extension
FORMAT_BC3 = 7

# Vanilla item textures are 90..450 px; wallet_x102.sim is 206x183.
ITEM_SIZE = 208


def pad4(value: int) -> int:
    return (value + 3) // 4 * 4


def mip_sizes(padded_width: int, padded_height: int) -> list[tuple[int, int]]:
    """Halve until a level would drop below 2 blocks in either direction."""
    levels = []
    width, height = padded_width, padded_height
    while True:
        levels.append((width, height))
        if min(width, height) <= 9:
            return levels
        width //= 2
        height //= 2


def _encode_alpha_block(alphas: list[int]) -> bytes:
    """Two endpoints plus 16 three-bit indices."""
    a_max, a_min = max(alphas), min(alphas)
    if a_max == a_min:
        # Endpoints equal selects the six-value palette, whose entry 0 is still exactly a_max.
        return bytes([a_max, a_min]) + bytes(6)

    palette = [a_max, a_min] + [((7 - i) * a_max + i * a_min) // 7 for i in range(1, 7)]
    bits = 0
    for i, alpha in enumerate(alphas):
        best = min(range(8), key=lambda p: abs(palette[p] - alpha))
        bits |= best << (3 * i)
    return bytes([a_max, a_min]) + bits.to_bytes(6, "little")


def _to_565(rgb: tuple[int, int, int]) -> int:
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def _from_565(value: int) -> tuple[int, int, int]:
    r, g, b = (value >> 11) & 0x1F, (value >> 5) & 0x3F, value & 0x1F
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


# Where each of the four BC1 palette entries sits between the two endpoints.
_BC1_WEIGHTS = (0.0, 1.0, 1.0 / 3.0, 2.0 / 3.0)


def _lerp(e0: tuple[int, int, int], e1: tuple[int, int, int], num: int) -> tuple[int, int, int]:
    """The BC1 palette entry `num`/3 of the way from e0 to e1."""
    return (
        ((3 - num) * e0[0] + num * e1[0]) // 3,
        ((3 - num) * e0[1] + num * e1[1]) // 3,
        ((3 - num) * e0[2] + num * e1[2]) // 3,
    )


def _bc1_palette(c0: int, c1: int) -> list[tuple[int, int, int]]:
    e0, e1 = _from_565(c0), _from_565(c1)
    return [e0, e1, _lerp(e0, e1, 1), _lerp(e0, e1, 2)]


def _assign(palette: list[tuple[int, int, int]], colours: list[tuple[int, int, int]]) -> list[int]:
    return [
        min(range(4), key=lambda p: sum((palette[p][c] - rgb[c]) ** 2 for c in range(3)))
        for rgb in colours
    ]


def _encode_colour_block(pixels: list[tuple[int, int, int, int]]) -> bytes:
    """
    Endpoints start from the bounding box of the visible colours and are then refined by a least-squares fit
    against the chosen indices, which matters on this logo's hard mint-on-navy edges.
    """
    visible = [i for i, p in enumerate(pixels) if p[3] > 0] or list(range(16))
    colours = [pixels[i][:3] for i in visible]
    lo = (min(c[0] for c in colours), min(c[1] for c in colours), min(c[2] for c in colours))
    hi = (max(c[0] for c in colours), max(c[1] for c in colours), max(c[2] for c in colours))

    c0, c1 = _to_565(hi), _to_565(lo)
    if c0 < c1:
        c0, c1 = c1, c0

    for _ in range(4):
        if c0 == c1:
            break
        indices = _assign(_bc1_palette(c0, c1), colours)
        # Least squares for palette[k] = e0 * (1 - w_k) + e1 * w_k over the assigned weights.
        ws = [_BC1_WEIGHTS[i] for i in indices]
        a = sum((1 - w) ** 2 for w in ws)
        b = sum((1 - w) * w for w in ws)
        cc = sum(w * w for w in ws)
        det = a * cc - b * b
        if abs(det) < 1e-6:
            break
        fit0: list[int] = []
        fit1: list[int] = []
        for ch in range(3):
            x = sum((1 - w) * col[ch] for w, col in zip(ws, colours, strict=True))
            y = sum(w * col[ch] for w, col in zip(ws, colours, strict=True))
            fit0.append(round(min(255, max(0, (cc * x - b * y) / det))))
            fit1.append(round(min(255, max(0, (a * y - b * x) / det))))
        new0 = _to_565((fit0[0], fit0[1], fit0[2]))
        new1 = _to_565((fit1[0], fit1[1], fit1[2]))
        if new0 < new1:
            new0, new1 = new1, new0
        if (new0, new1) == (c0, c1):
            break
        c0, c1 = new0, new1

    if c0 == c1:
        # A flat block: entry 0 already reproduces it. BC3 colour blocks are always four-colour, so equal
        # endpoints never select BC1's punch-through mode.
        return struct.pack("<HHI", c0, c1, 0)

    bits = 0
    for i, index in zip(visible, _assign(_bc1_palette(c0, c1), colours), strict=True):
        bits |= index << (2 * i)
    return struct.pack("<HHI", c0, c1, bits)


def encode_bc3(image: Image.Image) -> bytes:
    """A size that is not a multiple of 4 is covered by whole blocks whose overhang repeats the edge pixel."""
    width, height = image.size
    px = image.load()
    assert px is not None

    out = bytearray()
    for by in range(0, height, 4):
        for bx in range(0, width, 4):
            block = [
                px[min(bx + x, width - 1), min(by + y, height - 1)] for y in range(4) for x in range(4)
            ]
            out += _encode_alpha_block([p[3] for p in block])
            out += _encode_colour_block(block)
    return bytes(out)


def build_sim(image: Image.Image) -> bytes:
    width, height = image.size
    padded_width, padded_height = pad4(width), pad4(height)

    padded = Image.new("RGBA", (padded_width, padded_height), (0, 0, 0, 0))
    padded.paste(image, (0, 0))

    payload = bytearray()
    for level_width, level_height in mip_sizes(padded_width, padded_height):
        level = padded if (level_width, level_height) == padded.size else padded.resize(
            (level_width, level_height), Image.Resampling.LANCZOS
        )
        payload += encode_bc3(level)

    compressed = zlib.compress(bytes(payload), 9)
    header = struct.pack(
        "<9I",
        SIM_MAGIC,
        width,
        height,
        padded_width,
        padded_height,
        FORMAT_BC3,
        0,
        len(payload),
        len(compressed),
    )
    return header + compressed


def verify(data: bytes, source: Image.Image) -> None:
    """Re-reads our own file the way the game does and compares level 0 against the source."""
    magic, width, height, pw, ph, fmt, zero, usize, csize = struct.unpack_from("<9I", data)
    assert magic == SIM_MAGIC
    assert fmt == FORMAT_BC3
    assert zero == 0
    assert csize == len(data) - 0x24, (csize, len(data) - 0x24)
    raw = zlib.decompress(data[0x24:])
    assert len(raw) == usize, (len(raw), usize)

    expected = sum(pad4(w) // 4 * (pad4(h) // 4) * 16 for w, h in mip_sizes(pw, ph))
    assert expected == usize, (expected, usize)

    level0 = Image.frombytes("RGBA", (pw, ph), raw[: pw // 4 * (ph // 4) * 16], "bcn", (3,))
    level0 = level0.crop((0, 0, width, height))

    # Only visible pixels matter: the colour of a fully transparent texel is never sampled.
    alpha_diffs: list[int] = []
    colour_diffs: list[int] = []
    for want, got in zip(list(source.getdata()), list(level0.getdata()), strict=True):
        alpha_diffs.append(abs(want[3] - got[3]))
        if want[3] > 0:
            colour_diffs.extend(abs(want[c] - got[c]) for c in range(3))

    print(
        f"    {width}x{height} padded {pw}x{ph}, {len(mip_sizes(pw, ph))} mips, {usize} -> {csize} bytes"
    )
    print(
        f"    decoded alpha error max {max(alpha_diffs)} mean {sum(alpha_diffs) / len(alpha_diffs):.2f}; "
        f"visible colour error max {max(colour_diffs)} mean {sum(colour_diffs) / len(colour_diffs):.2f}"
    )
    # A block spanning the full alpha range cannot do better than a step of 36, so judge the fit by the mean
    # rather than by a single worst texel.
    assert sum(alpha_diffs) / len(alpha_diffs) <= 1.0, "alpha lost too much"
    assert sum(colour_diffs) / len(colour_diffs) <= 4.0, "visible colour lost too much"


def load_logo(path: Path, size: int) -> Image.Image:
    if not path.is_file():
        raise SystemExit(f"Randovania logo not found at {path}")
    ico = Image.open(path)
    largest = max(ico.ico.sizes())
    return ico.ico.getimage(largest).convert("RGBA").resize((size, size), Image.Resampling.LANCZOS)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--logo", type=Path, required=True, help="Randovania's rdv_logo_blue.ico")
    parser.add_argument("--check", action="store_true", help="fail if the committed bytes differ")
    args = parser.parse_args()

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    failures = 0
    for name, image in [("nothing", load_logo(args.logo, ITEM_SIZE))]:
        path = OUTPUT_DIR / f"{name}_x102.sim"
        print(f"{path.relative_to(REPO_ROOT)}:")
        data = build_sim(image)
        verify(data, image)
        if args.check:
            if not path.is_file() or path.read_bytes() != data:
                print("    MISMATCH: committed bytes differ from a fresh build")
                failures += 1
            else:
                print("    committed bytes match")
        else:
            path.write_bytes(data)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
