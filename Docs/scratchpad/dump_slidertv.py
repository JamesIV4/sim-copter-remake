"""Scratch: blow up SLIDERTV.BMP, the Check-up slider's thumb, and report its colour-key usage.

FUN_0040af00 leaves the thumb argument null for the Check-up dialog, so a vertical slider falls
back to this bitmap (SLIDERTH.BMP is the horizontal twin). Palette index 254 is the game's
transparency key.
"""
import struct
import zlib

ROOT = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP"
SCALE = 20


def load(path):
    d = open(path, "rb").read()
    off = struct.unpack_from("<I", d, 10)[0]
    hdr = struct.unpack_from("<I", d, 14)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    pal_off = 14 + hdr
    pal = [tuple(d[pal_off + i * 4: pal_off + i * 4 + 3][::-1]) for i in range(256)]
    stride = (w + 3) & ~3
    idx = [[d[off + (h - 1 - y) * stride + x] for x in range(w)] for y in range(h)]
    return w, h, pal, idx


for name in ("SLIDERTV", "SLIDERTH"):
    w, h, pal, idx = load("%s\\%s.BMP" % (ROOT, name))
    used = {}
    for row in idx:
        for i in row:
            used[i] = used.get(i, 0) + 1
    print("%s %dx%d  distinct=%d  key254=%d  corner=%d"
          % (name, w, h, len(used), used.get(254, 0), idx[0][0]))
    print("   top 6 indices:", sorted(used.items(), key=lambda kv: -kv[1])[:6])

    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            row += bytes(pal[idx[y][x]]) * SCALE
        rows.extend([bytes(row)] * SCALE)
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    out = r"S:\Repos\sim-copter-remake\Docs\scratchpad\art-%s-big.png" % name
    open(out, "wb").write(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w * SCALE, h * SCALE, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    print("   wrote", out)
