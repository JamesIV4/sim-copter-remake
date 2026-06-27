#!/usr/bin/env python3
"""Faithful, deterministic extractor for SimCopter ``X/privanim.df``.

This is a read-only structural extractor derived entirely from the decompiled
loader/reader (no heuristics, no guessed offsets). See
``Docs/OriginalGameFileFormats.md`` ("privanim.df") for the evidence trail.

Container model (all multi-byte fields are BIG-ENDIAN; Maxis "Doug"/IFF):

  * Header at 0x00: ``00 00 01 00`` version; length-prefixed root name "privanim".
  * Section directory: six 8-byte ``[4CC][BE u16 count][BE u16 nodeDirByteOff]``
    entries, located just before the node-dir marker.  Sections, in file order:
    ``SPR# ARPP ANIP ARCP ARLU BODC``.
  * Node-dir marker ``03 e8 ff ff``; the node directory begins 2 bytes into the
    marker (``node_base = marker + 2``).  Each section's entries start at
    ``node_base + nodeDirByteOff`` and there are ``count`` of them, 12 bytes each:
    ``[BE u32 fileOffset][BE u32 ?][BE u16 hash][BE u16 ?]``.  Sections are laid
    out ``count+1`` slots apart (a trailing separator slot).
  * Payloads (pointed to by ``fileOffset``) come in two shapes:
      - ARCP / ARPP / ANIP  -> a stream of 4-byte records ``[s8 x][s8 y][u8 z][u8 t]``
        (the articulated coordinate / per-frame pose stream; ``t`` steps by 8).
        Terminated by a run of 0xA3 filler bytes.
      - ARLU / BODC         -> 40-byte node-definition records:
        ``+0 [BE u16 0x12][BE u16 1]  +4 (x,y,z) BE floats  +0x10 idx[4]
          +0x14 flag[4]  +0x18 name[4]  +0x1c parent[4]  +0x20 tag[4]  +0x24 pad[4]``.

Record sizes/handlers come from the loader ``FUN_004ceab0`` registering
ARCP=0x28, ARLU=8, ARPP=8 with byte-swap handlers ``FUN_004d0090``/``FUN_004d00e0``.
The 76-clip inheritance tree lives in the ANIP node-def records (name<-parent).

Run:  python Tools/privanim_extract.py [path] [--json out.json]
"""
from __future__ import annotations
import argparse, json, struct
from pathlib import Path

DEFAULT = "Reference/SimCopterOriginalGame/X/privanim.df"
SECTION_ORDER = ("SPR#", "ARPP", "ANIP", "ARCP", "ARLU", "BODC")
NODEDEF_SECTIONS = ("ARLU", "BODC", "ANIP")   # 40-byte name/parent/float records
COORD_SECTIONS = ("ARCP", "ARPP")             # 4-byte coordinate/pose streams


class Privanim:
    def __init__(self, data: bytes):
        self.d = data
        self.N = len(data)

    def u16(self, o): return struct.unpack_from(">H", self.d, o)[0]
    def u32(self, o): return struct.unpack_from(">I", self.d, o)[0]
    def f32(self, o): return struct.unpack_from(">f", self.d, o)[0]
    def s8(self, o):  b = self.d[o]; return b - 256 if b > 127 else b
    def name(self, o): return self.d[o:o+4].decode("latin1", "replace")

    # --- directories -------------------------------------------------------
    def parse_sections(self):
        # The section dir is the 6 known 4CCs back-to-back, immediately before
        # the 03 e8 ff ff marker. Find the marker, then walk back 6*8 bytes.
        marker = self.d.find(b"\x03\xe8\xff\xff", self.N - 0x8000)
        if marker < 0:
            marker = self.d.rfind(b"\x03\xe8\xff\xff")
        secdir = marker - 6 * 8
        secs = {}
        for i in range(6):
            o = secdir + i * 8
            tag = self.name(o)
            secs[tag] = {"count": self.u16(o + 4), "nodeoff": self.u16(o + 6),
                         "dir_at": o}
        self.marker = marker
        self.node_base = marker + 2
        self.sections = secs
        return secs

    def node_entries(self, tag):
        s = self.sections[tag]
        base = self.node_base + s["nodeoff"]
        out = []
        for k in range(s["count"]):
            o = base + k * 12
            out.append({
                "i": k,
                "off": self.u32(o),
                "w1": self.u32(o + 4),
                "hash": self.u16(o + 8),
                "w3": self.u16(o + 10),
            })
        return out

    # --- payload readers ---------------------------------------------------
    def all_offsets_sorted(self):
        offs = set()
        for tag in SECTION_ORDER:
            for e in self.node_entries(tag):
                if 0 < e["off"] < self.N:
                    offs.add(e["off"])
        return sorted(offs)

    def payload_end(self, start, sorted_offs):
        # bound = next entry offset, or marker (start of directories)
        import bisect
        i = bisect.bisect_right(sorted_offs, start)
        nxt = sorted_offs[i] if i < len(sorted_offs) else self.marker
        return min(nxt, self.marker)

    def read_coord_stream(self, start, end):
        # ARCP/ARPP 4-byte records: [s8 x][s8 y][u8 z][u8 t], where every real
        # record has t == 4 (mod 8). That invariant cleanly bounds the stream:
        # stop at 0xA3 filler, an all-zero record, or the first t-invariant break.
        recs = []
        o = start
        while o + 4 <= end:
            b = self.d[o:o + 4]
            if b == b"\xa3\xa3\xa3\xa3" or b == b"\x00\x00\x00\x00":
                break
            if (b[3] & 7) != 4:        # t-invariant break = end of coord stream
                break
            recs.append([self.s8(o), self.s8(o + 1), self.d[o + 2], self.d[o + 3]])
            o += 4
        return recs

    def read_nodedefs(self, start, end):
        recs = []
        o = start
        while o + 40 <= end:
            marker = (self.u16(o), self.u16(o + 2))
            # node-def records start with (0x12, 1); stop when that breaks
            # (e.g. transition into an embedded name/string table).
            if marker != (0x12, 1):
                break
            nm = self.name(o + 0x18)
            if not all(32 <= ord(c) < 127 for c in nm):  # boundary/string-table record
                break
            recs.append({
                "marker": marker,
                "xyz": [round(self.f32(o + 4), 4), round(self.f32(o + 8), 4),
                        round(self.f32(o + 12), 4)],
                "idx": list(self.d[o + 0x10:o + 0x14]),
                "flag": list(self.d[o + 0x14:o + 0x18]),
                "name": self.name(o + 0x18),
                "parent": self.name(o + 0x1c),
                "tag": list(self.d[o + 0x20:o + 0x24]),
            })
            o += 40
        return recs, o   # o = where node-defs stopped (string table start)

    def parse_clip_table(self):
        """The animation-clip inheritance tree: a contiguous run of 40-byte
        records ``name[4] parent[4] [BE u16 a][BE u16 b] "ARPP" [BE u32 id]
        (x,y,z) BE floats  trailer[8]``.  Located as the run of 40-byte records
        carrying the literal ``ARPP`` tag at +0xc."""
        first = self.d.find(b"ARPP")
        if first < 0:
            return []
        start = first - 0xC          # record start = tag - 0xc
        clips = []
        o = start
        while o + 40 <= self.N and self.d[o + 0xC:o + 0x10] == b"ARPP":
            clips.append({
                "name": self.name(o), "parent": self.name(o + 4),
                "a": self.u16(o + 8), "b": self.u16(o + 0xA),
                "id": self.u32(o + 0x10),
                "xyz": [round(self.f32(o + 0x14), 4), round(self.f32(o + 0x18), 4),
                        round(self.f32(o + 0x1C), 4)],
                "trailer": list(self.d[o + 0x20:o + 0x28]),
            })
            o += 40
        return clips

    def read_name_table(self, start, end):
        """Embedded "<id>!<mnem>" table, e.g. 173!Inju 182!Dead 185!1Run."""
        raw = self.d[start:end]
        out = []
        # split on '!' boundaries: tokens are 4-char mnemonic then 3-digit id+'!'
        i = 0
        s = raw.decode("latin1", "replace")
        import re
        for m in re.finditer(r"(\d{2,4})!([ -~]{2,5}?)(?=\d{2,4}!|$)", s):
            out.append({"id": int(m.group(1)), "mnem": m.group(2).rstrip("\x00 ")})
        return out


def extract(path: Path):
    pa = Privanim(path.read_bytes())
    secs = pa.parse_sections()
    sorted_offs = pa.all_offsets_sorted()
    result = {
        "file": path.name, "size": pa.N,
        "node_base": pa.node_base, "marker_at": pa.marker,
        "sections": secs,
        "figures": [], "anim_clips": [], "name_table": [],
    }

    # Animation-clip inheritance tree (contiguous 40-byte ARPP-tagged run).
    result["anim_clips"] = pa.parse_clip_table()

    # BODC defines the 21 figures (parts) + an embedded name table.
    # Node-defs are parsed until the (0x12,1) marker breaks (string table /
    # next section), NOT bounded by the next global offset, because the 4-byte
    # coordinate streams interleave with these records in the file.
    for tag in ("BODC",):
        for e in pa.node_entries(tag):
            if not (0 < e["off"] < pa.N):
                continue
            defs, strstart = pa.read_nodedefs(e["off"], pa.marker)
            names = pa.read_name_table(strstart, min(strstart + 0x800, pa.marker))
            result["figures"].append({
                "node": e["i"], "off": e["off"], "hash": e["hash"],
                "parts": defs, "name_table_here": names,
            })
            if names and not result["name_table"]:
                result["name_table"] = names

    # ARCP coordinate streams (one per figure node).
    arcp = []
    for e in pa.node_entries("ARCP"):
        if not (0 < e["off"] < pa.N):
            continue
        end = pa.payload_end(e["off"], sorted_offs)
        recs = pa.read_coord_stream(e["off"], end)
        arcp.append({"node": e["i"], "off": e["off"], "nrec": len(recs),
                     "records": recs})
    result["arcp_streams"] = arcp
    return result


def summarize(r):
    print(f"{r['file']}  size=0x{r['size']:x}")
    print("sections:")
    for t in SECTION_ORDER:
        s = r["sections"][t]
        print(f"  {t:5} count={s['count']:5} nodeoff={s['nodeoff']:6} (0x{s['nodeoff']:x})")
    print(f"\nanim clips (inheritance tree): {len(r['anim_clips'])}")
    for c in r["anim_clips"][:14]:
        print(f"  {c['name']!r:8}<-{c['parent']!r:8} id={c['id']} xyz={c['xyz']} (a={c['a']},b={c['b']})")
    print(f"\nfigures (BODC): {len(r['figures'])}")
    for f in r["figures"][:3]:
        print(f"  fig node {f['node']} @0x{f['off']:x}: {len(f['parts'])} parts")
        for p in f["parts"][:6]:
            print(f"     {p['name']!r:8}<-{p['parent']!r:8} xyz={p['xyz']} idx={p['idx']} flag={p['flag']}")
    print(f"\nname table (id!mnem): {len(r['name_table'])}")
    print("  " + ", ".join(f"{n['id']}:{n['mnem']}" for n in r["name_table"][:24]))
    print(f"\nARCP coord streams: {len(r['arcp_streams'])}")
    for a in r["arcp_streams"][:3]:
        print(f"  node {a['node']} @0x{a['off']:x}: {a['nrec']} records; first 6:")
        for rec in a["records"][:6]:
            print(f"     x={rec[0]:4} y={rec[1]:4} z={rec[2]:3} t={rec[3]:3}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("path", nargs="?", default=DEFAULT)
    ap.add_argument("--json", default=None)
    a = ap.parse_args()
    p = Path(a.path)
    if not p.exists():
        print(f"{p}: not found (original game data is not committed); skipping.")
        return 0
    r = extract(p)
    summarize(r)
    if a.json:
        Path(a.json).write_text(json.dumps(r, indent=1))
        print(f"\nwrote {a.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
