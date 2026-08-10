"""Check the hand-painted window masks before wiring them into the bake.

Two questions, both of which have to be answered from the data rather than from the file names:

  1. Which atlas page is each file? Tools/WindowLayoutEditor.html names its export after whatever
     is typed in its "Atlas page" box, and the pages arrive as SKY composite images 1, 2, 3 -> atlas
     pages 2, 39, 40 (FUN_004606d0). windows_page_1.png is therefore ambiguous on its face: page 1
     is not an atlas page the game uses at all, so it is either a mis-typed page id or a composite
     index. Correlating the mask against each candidate page's night art settles it.

  2. Are the channels what the material is about to assume? R = lit window, G = per-blob byte,
     B = per-row byte, and R must be strictly 0 or 255 for the shader's step() to behave.

    python Docs/scratchpad/agent-sessions/2026-08-06-night-windows/verify_window_masks.py
"""

import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
MASKS = ROOT / "Reference" / "Night windows"
BMP = ROOT / "Reference" / "SimCopterOriginalGame" / "BMP"
GEO = ROOT / "Reference" / "SimCopterOriginalGame" / "GEO"

# SKY/SKYDARK composite image index -> live atlas page. Only the first three are wall pages; 20 and
# 13 are terrain and water and have no windows on them at all.
IMAGE_TO_ATLAS_PAGE = {1: 2, 2: 39, 3: 40}


def read_png(path):
    """Minimal RGB/RGBA 8-bit PNG reader - enough for the painter's own export."""
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\x1a"[:8].replace(b"\x1a\x1a", b"\x1a\n"), "not a PNG"
    pos, idat, width, height, channels = 8, bytearray(), 0, 0, 0
    while pos < len(data):
        length = struct.unpack_from(">I", data, pos)[0]
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if tag == b"IHDR":
            width, height, depth, colour = struct.unpack_from(">IIBB", body, 0)
            assert depth == 8, f"{path.name}: {depth}-bit, expected 8"
            channels = {2: 3, 6: 4}[colour]
        elif tag == b"IDAT":
            idat += body
        elif tag == b"IEND":
            break
        pos += 12 + length

    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = bytearray(height * stride)
    prev = bytearray(stride)
    src = 0
    for y in range(height):
        filter_type = raw[src]
        src += 1
        line = bytearray(raw[src:src + stride])
        src += stride
        for x in range(stride):
            a = line[x - channels] if x >= channels else 0
            b = prev[x]
            c = prev[x - channels] if x >= channels else 0
            if filter_type == 1:
                line[x] = (line[x] + a) & 255
            elif filter_type == 2:
                line[x] = (line[x] + b) & 255
            elif filter_type == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif filter_type == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, channels, bytes(out)


def read_palette(path):
    d = path.read_bytes()
    off = struct.unpack_from("<I", d, 57)[0]
    return [tuple(d[off + i * 3: off + i * 3 + 3]) for i in range(256)]


def decode_composite(path):
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
            base = data_offset + row_offset
            idx[(h - 1 - row) * w:(h - 1 - row) * w + w] = d[base:base + w]
        images.append((w, h, bytes(idx)))
        cursor = data_offset + w * h
    return images


def luminance(rgb):
    r, g, b = rgb
    return (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0


def main():
    palette = read_palette(GEO / "sim3d1.max")
    lum = [luminance(c) for c in palette]
    night = decode_composite(BMP / "SKYDARK.BMP")
    day = decode_composite(BMP / "SKY.BMP")

    for path in sorted(MASKS.glob("windows_page_*.png")):
        w, h, channels, pixels = read_png(path)
        count = w * h
        r = pixels[0::channels]
        g = pixels[1::channels]
        b = pixels[2::channels]
        lit = [i for i in range(count) if r[i] > 127]

        print(f"=== {path.name}  {w}x{h}, {channels} channels ===")
        print(f"  R values      : {sorted(set(r))[:6]}{' ...' if len(set(r)) > 6 else ''}")
        print(f"  lit texels    : {len(lit)} ({100.0 * len(lit) / count:.2f}% of the page)")
        print(f"  distinct G    : {len(set(g[i] for i in lit))} over lit texels")
        print(f"  distinct B    : {len(set(b[i] for i in lit))} over lit texels")

        if not lit:
            print()
            continue

        # Which page is it? The painted texels should sit on the bright half of the night art of
        # exactly one candidate, and on the dark half of the other two.
        print("  correlation against each candidate night page:")
        for image_index, page in sorted(IMAGE_TO_ATLAS_PAGE.items()):
            nw, nh, ni = night[image_index]
            _dw, _dh, di = day[image_index]
            if (nw, nh) != (w, h):
                print(f"    atlas page {page:>2}: size mismatch {nw}x{nh}")
                continue
            inside = sum(lum[ni[i]] for i in lit) / len(lit)
            outside_ids = [i for i in range(count) if r[i] <= 127]
            outside = sum(lum[ni[i]] for i in outside_ids) / max(len(outside_ids), 1)
            brighter = sum(1 for i in lit if lum[ni[i]] > lum[di[i]]) / len(lit)
            print(f"    atlas page {page:>2}: night lum under mask {inside:.3f} vs "
                  f"{outside:.3f} elsewhere  (ratio {inside / max(outside, 1e-6):5.2f}x, "
                  f"{100.0 * brighter:5.1f}% brighter than day)")
        print()


if __name__ == "__main__":
    main()
