import struct, sys

exe = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\SimCopter.exe"
data = open(exe, "rb").read()
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
coff = e_lfanew + 4
nsec = struct.unpack_from("<H", data, coff + 2)[0]
sizeopt = struct.unpack_from("<H", data, coff + 16)[0]
opt = coff + 20
base = struct.unpack_from("<I", data, opt + 28)[0]
secoff = opt + sizeopt
sections = []
for i in range(nsec):
    o = secoff + i * 40
    sections.append((data[o:o+8].rstrip(b"\0").decode(),
                     base + struct.unpack_from("<I", data, o + 12)[0],
                     struct.unpack_from("<I", data, o + 8)[0],
                     struct.unpack_from("<I", data, o + 20)[0]))

target = int(sys.argv[1], 16)
needle = struct.pack("<I", target)

for name, va, vsize, raw in sections:
    if name not in (".rdata", ".data"):
        continue
    blob = data[raw:raw + vsize]
    pos = 0
    while True:
        pos = blob.find(needle, pos)
        if pos < 0:
            break
        if pos % 4 == 0:
            hit_va = va + pos
            # walk backwards to the start of the run of code pointers
            start = pos
            while start >= 4:
                prev = struct.unpack_from("<I", blob, start - 4)[0]
                if 0x00401000 <= prev < 0x004f0000:
                    start -= 4
                else:
                    break
            slot = (pos - start) // 4
            print(f"found at {hit_va:08x} in {name}: vtable base {va + start:08x}, slot [{slot}]")
            entries = []
            j = start
            while j + 4 <= len(blob):
                v = struct.unpack_from("<I", blob, j)[0]
                if not (0x00401000 <= v < 0x004f0000):
                    break
                entries.append(v)
                j += 4
            for k, e in enumerate(entries[:16]):
                mark = "  <<<" if e == target else ""
                print(f"    [{k:2d}] {e:08x}{mark}")
        pos += 4
