"""Dump SimCopter.exe's Win32 STRINGTABLE and search it.

Throwaway verification tool: the Ghidra export has no .rsrc strings, so nag/expiry
wording like "Transport no longer needed" can only be confirmed from the resource
segment of the retail executable.
"""
import struct
import sys

EXE = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\SimCopter.exe"

data = open(EXE, "rb").read()

# --- minimal PE walk: section table + resource directory -------------------
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
num_sections = struct.unpack_from("<H", data, e_lfanew + 6)[0]
opt_size = struct.unpack_from("<H", data, e_lfanew + 20)[0]
sect_off = e_lfanew + 24 + opt_size

sections = []
for i in range(num_sections):
    base = sect_off + i * 40
    name = data[base:base + 8].rstrip(b"\0").decode("ascii", "replace")
    vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, base + 8)
    sections.append((name, vaddr, vsize, raddr, rsize))


def rva_to_offset(rva):
    for name, vaddr, vsize, raddr, rsize in sections:
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return raddr + (rva - vaddr)
    return None


rsrc_rva = None
opt_hdr = e_lfanew + 24
# Data directory 2 = resource table (PE32)
rsrc_rva, rsrc_size = struct.unpack_from("<II", data, opt_hdr + 96 + 16)

strings = {}  # (block_id) -> list of (index_in_block, text)


def read_utf16z_count(buf, off):
    """Reads a count-prefixed UTF-16 string (STRINGTABLE format)."""
    (chars,) = struct.unpack_from("<H", buf, off)
    raw = buf[off + 2:off + 2 + chars * 2]
    return raw.decode("utf-16le", "replace"), off + 2 + chars * 2


def walk_dir(base, off, level=0, path=()):
    """`off` is relative to `base` (the .rsrc section start), per the PE spec."""
    chars, time, version, n_named, n_id = struct.unpack_from("<IIHHH", data, base + off)
    entries_off = base + off + 16
    for i in range(n_named + n_id):
        name_or_id, offset = struct.unpack_from("<II", data, entries_off + i * 8)
        ident = name_or_id & 0x7FFFFFFF
        high_bit = offset & 0x80000000
        sub_off = offset & 0x7FFFFFFF
        if high_bit:
            walk_dir(base, sub_off, level + 1, path + ((ident,)))
        else:
            dva, dsize, cp, resv = struct.unpack_from("<IIII", data, base + sub_off)
            fo = rva_to_offset(dva)
            yield path, ident, fo, dsize


root_off = rva_to_offset(rsrc_rva)
print("sections:", [(n, hex(v), hex(r)) for n, v, _, r, _ in sections])
print("rsrc_rva:", hex(rsrc_rva) if rsrc_rva else None)
root_off = rva_to_offset(rsrc_rva) if rsrc_rva else None
print("root_off:", hex(root_off) if root_off else None)
if root_off:
    chars, time, version, n_named, n_id = struct.unpack_from("<IIHHH", data, root_off)
    print(f"root dir: named={n_named} ids={n_id}")
    entries_off = root_off + 16
    for i in range(n_named + n_id):
        name_or_id, offset = struct.unpack_from("<II", data, entries_off + i * 8)
        print(f"  entry {i}: id={name_or_id & 0x7FFFFFFF} sub={hex(offset)}")
found_entries = []
for path, ident, fo, dsize in walk_dir(root_off, 0):
    print("leaf:", path, ident, fo, dsize)
    # path: (type, name, language). Type 6 == RT_STRING
    if len(path) == 3 and path[0] == 6:
        found_entries.append((path[1], ident, fo, dsize))

for block_name, lang, fo, dsize in sorted(found_entries):
    block_id = block_name
    buf = data[fo:fo + dsize]
    pos = 0
    idx = 0
    while pos + 2 <= len(buf):
        text, pos = read_utf16z_count(buf, pos)
        sid = (block_id << 4) + idx
        if text:
            strings[sid] = text
        idx += 1

print(f"total non-empty stringtable entries: {len(strings)}")
if strings:
    print(f"string id range: {min(strings)}..{max(strings)}")

# --- raw whole-file scan ----------------------------------------------------
# The Reference exe carries a .detour section and an empty .rsrc root, so don't
# trust the directory walk alone: scan every byte for UTF-16LE needles.
SECTION_OF = []
for name, vaddr, vsize, raddr, rsize in sections:
    SECTION_OF.append((raddr, raddr + rsize, name))


def section_of(off):
    for lo, hi, name in SECTION_OF:
        if lo <= off < hi:
            return name
    return "?"


def extract_utf16_run(off):
    start = off
    while start > 0:
        ch = data[start - 2:start]
        if len(ch) == 2 and 0x20 <= ch[0] < 0x7F and ch[1] == 0:
            start -= 2
        else:
            break
    end = off
    while end + 2 <= len(data):
        ch = data[end:end + 2]
        if 0x20 <= ch[0] < 0x7F and ch[1] == 0:
            end += 2
        else:
            break
    return data[start:end].decode("utf-16le", "replace")


# --- sequential recovery scan ------------------------------------------------
# The directory is zeroed, so recover every count-prefixed UTF-16 string in
# .rsrc linearly and align ids from the port's known anchors:
#   'Sim Rescued!' = 0x3a7, 'SOS!' = 0x3b3.
scan_start = 0x11E200
entries = []  # (offset, text)
pos = scan_start
while pos + 2 <= len(data):
    (chars,) = struct.unpack_from("<H", data, pos)
    if 0 < chars <= 250:
        raw = data[pos + 2:pos + 2 + chars * 2]
        if len(raw) == chars * 2:
            text = raw.decode("utf-16le", "replace")
            if all(0x20 <= ord(c) < 0x7F for c in text):
                entries.append((pos, text))
                pos += 2 + chars * 2
                continue
    pos += 2

print(f"recovered {len(entries)} strings")

anchor_sid = {"Sim Rescued!": 0x3A7}
idx_of_anchor = next(i for i, (_, t) in enumerate(entries) if t == "Sim Rescued!")

for delta in range(-40, 220):
    i = idx_of_anchor + delta
    if 0 <= i < len(entries):
        off, t = entries[i]
        print(f"0x{0x3A7 + delta:03x} (+{delta:+4d}) @ {hex(off)}: {t}")



for anchor in anchors:
    pat = anchor.encode("utf-16le")
    pos = data.find(pat)
    print(f"anchor {anchor!r}: {'FOUND at ' + hex(pos) + ' in ' + section_of(pos) if pos >= 0 else 'ABSENT'}")

for needle in needles:
    pat = needle.encode("utf-16le")
    start = 0
    count = 0
    while True:
        pos = data.find(pat, start)
        if pos < 0:
            break
        count += 1
        print(f"{needle!r} @ {hex(pos)} ({section_of(pos)}): {extract_utf16_run(pos)!r}")
        start = pos + 1
        if count > 10:
            break
    if count == 0:
        print(f"{needle!r}: absent (utf16)")

# Same hunt for ANSI encodings.
for needle in needles + anchors:
    pat = needle.encode("ascii")
    pos = data.find(pat)
    if pos >= 0:
        lo, hi = max(0, pos - 60), min(len(data), pos + 80)
        ctx = data[lo:hi]
        printable = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in ctx)
        print(f"ANSI {needle!r} @ {hex(pos)} ({section_of(pos)}): ...{printable}...")

