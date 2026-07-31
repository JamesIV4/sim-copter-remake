"""Report format + duration of every WAV under the original game's sound tree."""
import os
import struct
import sys

ROOT = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\sound"


def info(path):
    with open(path, "rb") as fh:
        data = fh.read(4096)
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return None
    pos, fmt, datasz = 12, None, 0
    total = os.path.getsize(path)
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        csz = struct.unpack_from("<I", data, pos + 4)[0]
        if cid == b"fmt ":
            tag, ch, rate, _bps, _align, bits = struct.unpack_from("<HHIIHH", data, pos + 8)
            fmt = (tag, ch, rate, bits)
        elif cid == b"data":
            datasz = csz
            break
        pos += 8 + csz + (csz & 1)
    if not fmt:
        return None
    tag, ch, rate, bits = fmt
    secs = datasz / float(rate * ch * max(bits, 8) // 8) if rate else 0
    return tag, ch, rate, bits, datasz, secs, total


def main():
    rows = []
    for dirpath, _dirs, files in os.walk(ROOT):
        for f in files:
            if not f.lower().endswith(".wav"):
                continue
            p = os.path.join(dirpath, f)
            i = info(p)
            rel = os.path.relpath(p, ROOT)
            if i is None:
                rows.append((rel, "NOT-RIFF/WAVE"))
            else:
                tag, ch, rate, bits, dsz, secs, total = i
                rows.append((rel, f"tag={tag} ch={ch} {rate}Hz {bits}bit "
                                  f"data={dsz} {secs:6.2f}s file={total}"))
    fmts = {}
    for rel, d in rows:
        key = d.split(" data=")[0]
        fmts.setdefault(key, []).append(rel)
    print(f"# {len(rows)} wav files")
    for key, files in sorted(fmts.items(), key=lambda kv: -len(kv[1])):
        print(f"\n## {key}   x{len(files)}")
        print("   " + ", ".join(sorted(files)[:12]) + (" ..." if len(files) > 12 else ""))
    if len(sys.argv) > 1:
        print("\n# detail")
        for rel, d in sorted(rows):
            if any(a.lower() in rel.lower() for a in sys.argv[1:]):
                print(f"  {rel:40} {d}")


if __name__ == "__main__":
    main()
