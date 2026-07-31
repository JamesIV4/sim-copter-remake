"""Convert an 8-bit palettised Windows BMP to PNG, optionally nearest-neighbour scaled.

No Pillow in the venv, and the dashboard art is plain BI_RGB 8bpp, so hand-rolling is easier
than adding a dependency. Also draws optional guide lines so a candidate needle position can be
checked against the artwork.
"""
import struct
import sys
import zlib


def read_bmp(path):
    d = open(path, "rb").read()
    off, hsz, w, h, planes, bpp = struct.unpack_from("<IIiiHH", d, 10)
    compression = struct.unpack_from("<I", d, 30)[0]
    assert bpp == 8 and compression == 0, f"{bpp}bpp compression={compression} unsupported"
    pal_off = 14 + hsz
    pal = []
    for i in range(256):
        b, g, r, _a = d[pal_off + i * 4: pal_off + i * 4 + 4]
        pal.append((r, g, b))
    bottom_up = h > 0
    h = abs(h)
    stride = (w + 3) & ~3
    rows = []
    for y in range(h):
        src = y if not bottom_up else (h - 1 - y)
        start = off + src * stride
        rows.append(d[start:start + w])
    return w, h, pal, rows


def write_png(path, w, h, rgb_rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rgb_rows)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    scale = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    # "-" means "none"; PowerShell silently drops a bare "" argument, which quietly shifted the
    # crop into the guides slot the first time round.
    def arg(i):
        return sys.argv[i] if len(sys.argv) > i and sys.argv[i] not in ("", "-") else None

    guides = [int(x) for x in arg(4).split(",")] if arg(4) else []
    crop = [int(x) for x in arg(5).split(",")] if arg(5) else None

    w, h, pal, rows = read_bmp(src)
    print(f"{src}: {w}x{h}")
    if crop:
        cx, cy, cw, ch = crop
        rows = [r[cx:cx + cw] for r in rows[cy:cy + ch]]
        guides = [g - cx for g in guides]
        w, h = cw, ch
        print(f"  cropped to {cx},{cy} {cw}x{ch}")

    out = []
    for y in range(h):
        line = []
        for x in range(w):
            line.extend(pal[rows[y][x]])
        for _ in range(scale):
            out.append(list(line) if scale == 1 else
                       [v for x in range(w) for _ in range(scale) for v in pal[rows[y][x]]])
    # Guide columns in source pixels, drawn magenta.
    if guides:
        for y in range(len(out)):
            for gx in guides:
                for s in range(scale):
                    i = (gx * scale + s) * 3
                    if 0 <= i + 2 < len(out[y]):
                        out[y][i], out[y][i + 1], out[y][i + 2] = 255, 0, 255
    write_png(dst, w * scale, h * scale, out)
    print(f"wrote {dst} ({w*scale}x{h*scale})")


if __name__ == "__main__":
    main()
