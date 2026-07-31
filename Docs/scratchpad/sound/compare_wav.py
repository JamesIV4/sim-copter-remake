"""Compare WAV format between the repo's reference copy and the user's full install."""
import os
import struct
import sys

REF = r"S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame"
SRC = r"D:\Downloads\SimCopter\Extracted\SIMCOPTER"


def fmt(path):
    try:
        with open(path, "rb") as fh:
            data = fh.read(2048)
    except OSError:
        return "missing"
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return "not-wave"
    pos = 12
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        csz = struct.unpack_from("<I", data, pos + 4)[0]
        if cid == b"fmt ":
            tag, ch, rate, _bps, _al, bits = struct.unpack_from("<HHIIHH", data, pos + 8)
            return f"tag={tag} ch={ch} {rate}Hz {bits}bit"
        pos += 8 + csz + (csz & 1)
    return "no-fmt"


rels = sys.argv[1:] or [
    "sound/accel2.WAV", "sound/coploop.wav", "sound/chopstar.wav", "sound/douse.wav",
    "sound/people/grunt1.WAV", "sound/English/D1000.WAV", "sound/BLIP1.WAV",
]
for rel in rels:
    r = os.path.join(REF, rel.replace("/", os.sep))
    s = os.path.join(SRC, rel.replace("/", os.sep))
    rs = os.path.getsize(r) if os.path.exists(r) else -1
    ss = os.path.getsize(s) if os.path.exists(s) else -1
    print(f"{rel}\n    ref {rs:>9}  {fmt(r)}\n    src {ss:>9}  {fmt(s)}")
