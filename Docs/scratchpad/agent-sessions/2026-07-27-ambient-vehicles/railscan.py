import struct, sys, os, glob

# Minimal SC2 reader: IFF-ish chunks, XBLD is 128*128 RLE-compressed bytes.
def read_chunks(path):
    b = open(path, "rb").read()
    assert b[0:4] == b"FORM"
    out = {}
    off = 12
    while off + 8 <= len(b):
        name = b[off:off+4].decode("ascii", "replace")
        size = struct.unpack_from(">I", b, off + 4)[0]
        out.setdefault(name, b[off+8:off+8+size])
        off += 8 + size
    return out

def decompress(data):
    out = bytearray()
    i = 0
    while i < len(data):
        n = data[i]; i += 1
        if n < 128:
            out += data[i:i+n]; i += n
        else:
            out += bytes([data[i]]) * (n - 127); i += 1
    return bytes(out)

def is_rail(xbld, xzon):
    v = xbld | ((xzon & 2) << 14)
    if v < 0x3b:  return v > 0x31 or (0x2b < v < 0x2e)
    if v < 0x4f:  return v > 0x4c or (0x44 < v < 0x49)
    if v < 0x805c: return v > 0x8059 or (0x59 < v < 0x5c)
    return False

for path in sorted(glob.glob(sys.argv[1])):
    try:
        c = read_chunks(path)
        xbld = decompress(c["XBLD"])
        xzon = decompress(c["XZON"])
    except Exception as e:
        print(f"{os.path.basename(path)}: {e}")
        continue
    rails = sum(1 for i in range(min(len(xbld), len(xzon))) if is_rail(xbld[i], xzon[i]))
    ids = sorted({xbld[i] for i in range(min(len(xbld), len(xzon))) if is_rail(xbld[i], xzon[i])})
    print(f"{os.path.basename(path):<16} rail tiles = {rails:5}   ids = {[hex(i) for i in ids]}")
