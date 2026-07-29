import struct, sys, os, glob

ROOT = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\GEO"

def rd32(b, o): return struct.unpack_from("<I", b, o)[0]
def rd16(b, o): return struct.unpack_from("<H", b, o)[0]
def rdstr(b, o, n):
    s = b[o:o+n]
    z = s.find(b"\0")
    if z >= 0: s = s[:z]
    return s.decode("ascii", "replace")

want = set(int(a, 0) for a in sys.argv[1:]) if len(sys.argv) > 1 else None

for path in sorted(glob.glob(os.path.join(ROOT, "*.MAX")) + glob.glob(os.path.join(ROOT, "*.max"))):
    b = open(path, "rb").read()
    geom = rd32(b, 24)
    if b[geom:geom+4] != b"GEOM":
        print(f"{path}: no GEOM"); continue
    entryCount = rd32(b, geom + 8)
    objCount = rd32(b, geom + 12)
    entryOff = rd32(b, geom + 16)
    for i in range(entryCount):
        o = entryOff + i * 53
        name = rdstr(b, o, 17)
        objOff = rd32(b, o + 17)
        if objOff <= 0 or objOff + 124 > len(b): continue
        if b[objOff:objOff+4] != b"OBJX": continue
        objName = rdstr(b, objOff + 24, 88)
        attrs = rd32(b, objOff + 12)
        oid = struct.unpack_from("<i", b, objOff + 120)[0]
        if want is None or oid in want:
            print(f"{os.path.basename(path):<12} tbl[{i:3}] id={oid:4} (0x{oid:03x}) attrs=0x{attrs:x} name='{name}' obj='{objName}'")
