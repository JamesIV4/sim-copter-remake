"""Dump Win32 STRINGTABLE resources from SimCopter.exe."""
import struct, sys
from pathlib import Path

exe = Path(r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\SimCopter.exe")
d = exe.read_bytes()

pe = struct.unpack_from("<I", d, 0x3C)[0]
assert d[pe:pe+4] == b"PE\0\0"
nsec = struct.unpack_from("<H", d, pe + 6)[0]
opt_size = struct.unpack_from("<H", d, pe + 20)[0]
opt = pe + 24
magic = struct.unpack_from("<H", d, opt)[0]
nrva_off = opt + (92 if magic == 0x10b else 108)
nrva = struct.unpack_from("<I", d, nrva_off)[0]
dd = nrva_off + 4
res_rva, res_size = struct.unpack_from("<II", d, dd + 2 * 8)

secs = []
sh = opt + opt_size
for i in range(nsec):
    o = sh + i * 40
    name = d[o:o+8].rstrip(b"\0").decode()
    vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", d, o + 8)
    secs.append((name, vaddr, vsize, raddr, rsize))

def rva2off(rva):
    for name, vaddr, vsize, raddr, rsize in secs:
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return raddr + (rva - vaddr)
    return None

res_off = rva2off(res_rva)

def entries(dir_off):
    nnamed, nid = struct.unpack_from("<HH", d, dir_off + 12)
    out = []
    for i in range(nnamed + nid):
        o = dir_off + 16 + i * 8
        nid_or_name, off = struct.unpack_from("<II", d, o)
        out.append((nid_or_name, off))
    return out

results = {}
for type_id, type_off in entries(res_off):
    if type_id & 0x80000000:
        continue
    if type_id != 6:  # RT_STRING
        continue
    assert type_off & 0x80000000
    for name_id, name_off in entries(res_off + (type_off & 0x7FFFFFFF)):
        for lang_id, lang_off in entries(res_off + (name_off & 0x7FFFFFFF)):
            data_rva, size, cp, _ = struct.unpack_from("<IIII", d, res_off + lang_off)
            off = rva2off(data_rva)
            block = d[off:off + size]
            base = (name_id - 1) * 16
            p = 0
            for i in range(16):
                if p + 2 > len(block):
                    break
                ln = struct.unpack_from("<H", block, p)[0]
                p += 2
                s = block[p:p + ln * 2].decode("utf-16-le", "replace")
                p += ln * 2
                if s:
                    results.setdefault(lang_id, {})[base + i] = s

langs = sorted(results)
print("languages:", langs, file=sys.stderr)
lang = int(sys.argv[1]) if len(sys.argv) > 1 else langs[0]
for k in sorted(results[lang]):
    print(f"{k}\t{results[lang][k]!r}")
