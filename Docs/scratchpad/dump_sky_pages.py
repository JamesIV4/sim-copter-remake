"""Dump SKY.BMP / SKYDARK.BMP (and the SIM3D pages they overwrite) to PNG, and report which
texels differ between day and night.

SCHOOK: FUN_004606d0 0x004606d0. The original loads sky.bmp during the day and skydark.bmp at
night (chosen off renderer+0x4f, which FUN_00460690 sets from DAT_004f9720), blits image 0 as the
640x200 sky backdrop, and then copies images 1..5 straight over live atlas pages 2, 39, 40, 20 and
13. That page swap is the whole night-windows effect: the night pages carry the lit windows.

    python Docs/scratchpad/dump_sky_pages.py
"""

import os
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BMP = ROOT / "Reference" / "SimCopterOriginalGame" / "BMP"
GEO = ROOT / "Reference" / "SimCopterOriginalGame" / "GEO"
OUT = Path(__file__).resolve().parent / "sky-pages"

# FUN_004606d0's copy order: sky image index -> live atlas page id.
IMAGE_TO_ATLAS_PAGE = {1: 2, 2: 39, 3: 40, 4: 20, 5: 13}


def read_palette(path):
    d = path.read_bytes()
    off = struct.unpack_from("<I", d, 57)[0]
    return [tuple(d[off + i * 3: off + i * 3 + 3]) for i in range(256)]


def decode_composite(path):
    """Palette INDICES per image, so day/night pages can be diffed before palette lookup."""
    d = path.read_bytes()
    image_count = struct.unpack_from("<i", d, 8)[0]
    resolution_count = struct.unpack_from("<i", d, 12)[0]
    cursor = 16 + resolution_count * 12
    images = []
    for _ in range(image_count):
        w, h, _unk = struct.unpack_from("<iii", d, cursor)
        row_table = cursor + 12
        data_offset = row_table + h * 4
        idx = bytearray(w * h)
        for row in range(h):
            row_offset = struct.unpack_from("<i", d, row_table + row * 4)[0]
            dest = (h - 1 - row) * w
            base = data_offset + row_offset
            idx[dest:dest + w] = d[base:base + w]
        images.append((w, h, bytes(idx)))
        cursor = data_offset + w * h
    return images


def write_png(path, w, h, idx, palette):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            raw.extend(palette[idx[y * w + x]])
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def main():
    OUT.mkdir(exist_ok=True)
    palette = read_palette(GEO / "sim3d1.max")

    day = decode_composite(BMP / "SKY.BMP")
    night = decode_composite(BMP / "SKYDARK.BMP")
    sim3d = decode_composite(BMP / "SIM3D.BMP")

    print(f"SKY.BMP     {len(day)} images: {[(w, h) for w, h, _ in day]}")
    print(f"SKYDARK.BMP {len(night)} images: {[(w, h) for w, h, _ in night]}")
    print()

    for i, ((dw, dh, di), (nw, nh, ni)) in enumerate(zip(day, night)):
        write_png(OUT / f"sky_day_{i}.png", dw, dh, di, palette)
        write_png(OUT / f"sky_night_{i}.png", nw, nh, ni, palette)

        differing = sum(1 for a, b in zip(di, ni) if a != b)
        page = IMAGE_TO_ATLAS_PAGE.get(i)
        note = f"-> atlas page {page}" if page is not None else "-> 640x200 sky backdrop"
        print(f"image {i} {dw}x{dh} {note}: {differing}/{len(di)} texels differ "
              f"({100.0 * differing / len(di):.1f}%)")

        if page is not None and page < len(sim3d):
            sw, sh, si = sim3d[page]
            write_png(OUT / f"sim3d_page_{page}.png", sw, sh, si, palette)
            same_as_sim3d = (sw, sh) == (dw, dh) and si == di
            print(f"          SIM3D image {page} is {sw}x{sh}; "
                  f"identical to SKY day page: {same_as_sim3d}")

    # Per 32x32 cell breakdown of the pages that change, so the lit cells can be named.
    print("\n=== cells that change between day and night (8x8 grid, cell = col + row*8) ===")
    for i, page in sorted(IMAGE_TO_ATLAS_PAGE.items()):
        dw, dh, di = day[i]
        _nw, _nh, ni = night[i]
        changed = []
        rows = dh // 32
        for cell_row in range(rows):
            for col in range(8):
                # Cell indices count from the BOTTOM row, matching FMaxisTextureReader::ExtractAtlasTile.
                y0 = (rows - 1 - cell_row) * 32
                x0 = col * 32
                n = 0
                for y in range(y0, y0 + 32):
                    base = y * dw
                    for x in range(x0, x0 + 32):
                        if di[base + x] != ni[base + x]:
                            n += 1
                if n:
                    changed.append((cell_row * 8 + col, n))
        print(f"  atlas page {page:>2}: {len(changed)} of 64 cells change -> "
              f"{[c for c, _ in changed]}")

    print(f"\nPNGs written to {OUT}")


if __name__ == "__main__":
    main()
