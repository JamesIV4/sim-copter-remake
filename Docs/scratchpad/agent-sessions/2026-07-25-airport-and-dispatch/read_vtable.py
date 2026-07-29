import struct, sys

path = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\SimCopter.exe"
data = open(path, "rb").read()

e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
assert data[e_lfanew:e_lfanew+4] == b"PE\0\0"
coff = e_lfanew + 4
num_sections = struct.unpack_from("<H", data, coff + 2)[0]
size_opt = struct.unpack_from("<H", data, coff + 16)[0]
opt = coff + 20
image_base = struct.unpack_from("<I", data, opt + 28)[0]
sec_off = opt + size_opt

sections = []
for i in range(num_sections):
    o = sec_off + i * 40
    name = data[o:o+8].rstrip(b"\0").decode("ascii", "replace")
    vsize = struct.unpack_from("<I", data, o + 8)[0]
    vaddr = struct.unpack_from("<I", data, o + 12)[0]
    rawsize = struct.unpack_from("<I", data, o + 16)[0]
    rawptr = struct.unpack_from("<I", data, o + 20)[0]
    sections.append((name, vaddr, vsize, rawptr, rawsize))

print(f"image base 0x{image_base:08x}")
for s in sections:
    print(f"  {s[0]:8s} VA 0x{image_base+s[1]:08x}..0x{image_base+s[1]+s[2]:08x}  raw 0x{s[3]:08x}")

def va_to_off(va):
    rva = va - image_base
    for name, vaddr, vsize, rawptr, rawsize in sections:
        if vaddr <= rva < vaddr + vsize:
            return rawptr + (rva - vaddr)
    return None

# The three vehicle-class vtables named in the dispatch decode, plus the criminal car's.
vtables = {
    "criminal car PTR_FUN_004f4cd8": 0x004f4cd8,
    "ambulance  PTR_FUN_004f4d20":   0x004f4d20,
    "fire truck PTR_FUN_004f4d48":   0x004f4d48,
    "police car PTR_FUN_004f4db0":   0x004f4db0,
}
for label, va in vtables.items():
    off = va_to_off(va)
    if off is None:
        print(f"{label}: VA not mapped"); continue
    entries = struct.unpack_from("<8I", data, off)
    print(f"\n{label}  @0x{va:08x} (file 0x{off:x})")
    for i, e in enumerate(entries):
        note = ""
        if not (0x00401000 <= e < 0x00500000):
            note = "   <- not a code pointer, table probably ends before here"
        print(f"  [{i}] 0x{e:08x}{note}")
