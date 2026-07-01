"""READ-ONLY live-memory oracle for the privanim decode.

Finds SimCopter.exe, locates the people .data region via the DAT_0058f0e0
head-image table {4,5,0x2c,0x2d,0x2e,0x41,0x2f,0x42,0x30,0x31,0x43} (u16s,
written by FUN_004ceab0), then for live pedestrians validates:
  person+0x142 alive, +0x148 state, +0x14c frame, +0x160 clothes, +0x18e head,
  +0x21c figure node (->+0x1c name, +0x30 head img), +0x220 mnemonic,
  +0x224 clip node (->+0x1c clip name, +0x28 ARPP array: +0x18 recSize=8,
  +0x14 rows, +0x10 cols, +0xc buffer)
and compares the clip's in-memory ARPP records byte-for-byte with the file.

Ghidra .data deltas are non-uniform under SimCopterX -> everything is derived
from the anchor scan, never from absolute Ghidra addresses.
"""
import ctypes, ctypes.wintypes as wt, struct, subprocess, sys

k32 = ctypes.windll.kernel32
PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
MEM_COMMIT = 0x1000

class MBI(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_size_t), ("AllocationBase", ctypes.c_size_t),
                ("AllocationProtect", wt.DWORD), ("RegionSize", ctypes.c_size_t),
                ("State", wt.DWORD), ("Protect", wt.DWORD), ("Type", wt.DWORD)]

def find_pid():
    out = subprocess.check_output(["tasklist", "/FI", "IMAGENAME eq SimCopter.exe", "/FO", "CSV"],
                                  text=True)
    for line in out.splitlines()[1:]:
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) > 1:
            return int(parts[1])
    raise SystemExit("SimCopter.exe not found")

pid = find_pid()
h = k32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
if not h:
    raise SystemExit(f"OpenProcess failed for pid {pid}")
print(f"SimCopter.exe pid={pid} (read-only handle)")

def read(addr, n):
    buf = ctypes.create_string_buffer(n)
    got = ctypes.c_size_t()
    if not k32.ReadProcessMemory(h, ctypes.c_size_t(addr), buf, n, ctypes.byref(got)):
        return None
    return buf.raw[:got.value]

def regions():
    addr = 0
    mbi = MBI()
    while addr < 0x7fff0000:
        if not ctypes.windll.kernel32.VirtualQueryEx(h, ctypes.c_size_t(addr),
                                                     ctypes.byref(mbi), ctypes.sizeof(mbi)):
            break
        if mbi.State == MEM_COMMIT and mbi.Protect in (0x04, 0x08, 0x02, 0x20, 0x40):
            yield mbi.BaseAddress, mbi.RegionSize
        addr = mbi.BaseAddress + mbi.RegionSize

# --- 1. anchor scan: head-image table (11 u16s) --------------------------------
ANCHOR = struct.pack("<11H", 4, 5, 0x2C, 0x2D, 0x2E, 0x41, 0x2F, 0x42, 0x30, 0x31, 0x43)
hits = []
for base, size in regions():
    if size > 0x2000000:
        continue
    data = read(base, size)
    if not data:
        continue
    i = data.find(ANCHOR)
    while i >= 0:
        hits.append(base + i)
        i = data.find(ANCHOR, i + 1)
print(f"anchor hits: {[hex(x) for x in hits]}")
if not hits:
    raise SystemExit("head-image table not found - is a city loaded?")

# Ghidra: table at 0x0058f0e0; person ptr array DAT_0058e030; delta is uniform
# within the .data region, so live array = anchor + (0x58e030 - 0x58f0e0).
GH_ANCHOR = 0x58F0E0
for anchor in hits:
    arr = anchor + (0x58E030 - GH_ANCHOR)
    raw = read(arr, 500 * 4)
    if not raw:
        continue
    ptrs = struct.unpack("<500I", raw)
    live = [p for p in ptrs if 0x10000 < p < 0x7fff0000]
    print(f"\nperson array @ {arr:#x}: {len(live)} candidate pointers")
    if not live:
        continue

    u16 = lambda b, o: struct.unpack_from("<H", b, o)[0]
    u32 = lambda b, o: struct.unpack_from("<I", b, o)[0]
    shown = 0
    validated = None
    for p in live:
        pd = read(p, 0x250)
        if not pd or len(pd) < 0x250:
            continue
        alive = u16(pd, 0x142)
        if alive == 0:
            continue
        state = u16(pd, 0x148); frame = u16(pd, 0x14c)
        clothes = u16(pd, 0x160); head = u16(pd, 0x18e)
        fig = u32(pd, 0x21c); mnem = pd[0x220:0x224].decode("latin1", "replace")
        clip = u32(pd, 0x224)
        line = (f"person@{p:#x} state={state} frame={frame} clothes={clothes} "
                f"head={head} mnem={mnem!r} fig={fig:#x} clip={clip:#x}")
        if shown < 8:
            print("  " + line)
        shown += 1
        if validated is None and fig and clip:
            validated = (p, fig, clip, frame, mnem)
    print(f"  ({shown} alive persons)")

    if validated:
        p, fig, clip, frame, mnem = validated
        fd = read(fig, 0x40); cd = read(clip, 0x40)
        # 4-char names live as LE u32 of the big-endian chars -> reverse to read
        rev4 = lambda b: b[:4][::-1].decode("latin1", "replace")
        fig_name = rev4(fd[0x1c:0x20]) if fd else "?"
        head_img = u16(fd, 0x30) if fd else -1
        clip_name = rev4(cd[0x1c:0x20]) if cd else "?"
        arpp = u32(cd, 0x28) if cd else 0
        print(f"\n== oracle person @{p:#x}: figure {fig_name!r} headimg={head_img} "
              f"mnem={mnem!r} clip {clip_name!r} frame={frame}")
        if arpp:
            ad = read(arpp, 0x60)
            rec = u32(ad, 0x18); rows = u32(ad, 0x14); cols = u32(ad, 0x10)
            bufp = u32(ad, 0xc); rowtab = u32(ad, 4)
            print(f"ARPP array@{arpp:#x}: recSize={rec} rows={rows} cols={cols} "
                  f"buf={bufp:#x} rowtab={rowtab:#x}")
            if rec == 8 and 0 < rows <= 16 and 0 < cols <= 128:
                rt = read(rowtab, rows * 4)
                row0 = struct.unpack("<I", rt[:4])[0]
                mem = read(row0, rows * cols * 8)
                print(f"read {len(mem)} bytes of live ARPP records @ {row0:#x}")
                # compare with file
                sys.path.insert(0, r"s:\\Repos\\sim-copter-remake\\Tools")
                from privanim_extract import Privanim
                pa = Privanim(open(r"s:\\Repos\\sim-copter-remake\\Reference\\SimCopterOriginalGame\\X\\privanim.df", "rb").read())
                e = pa.entry("ARPP", clip_name[:3] + "i" + clip_name[4:])
                rs, frows, fcols, data = pa.record_array(e)
                fbytes = pa.d[data:data + frows * fcols * 8]
                print(f"file  {clip_name!r}: recSize={rs} rows={frows} cols={fcols} "
                      f"({len(fbytes)} bytes)")
                if rows == frows and cols == fcols and mem == fbytes:
                    print("*** ORACLE PASS: live ARPP records == file bytes (exact) ***")
                else:
                    same = sum(a == b for a, b in zip(mem, fbytes))
                    print(f"*** MISMATCH: {same}/{min(len(mem),len(fbytes))} bytes equal")
                    print("live:", mem[:32].hex(" "))
                    print("file:", fbytes[:32].hex(" "))
    break
