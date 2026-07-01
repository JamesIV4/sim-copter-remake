#!/usr/bin/env python3
"""Faithful, deterministic extractor for SimCopter ``X/privanim.df``.

REWRITTEN 2026-07-01 after fully decompiling the reader. Every rule below cites
the decompiled function in ``SimCopter.exe`` (see
``Docs/OriginalGameFileFormats.md`` and ``Docs/scratchpad/ghidra/out_chunkfetch*.txt``,
``out_dirload*.txt``, ``out_nodemethods.txt``). The previous version of this
tool guessed the directory-entry layout and read coordinate streams from wrong
offsets; none of its "display rules" survive. This version is exact:

Container ("Doug" DF, all fields BIG-ENDIAN):
  header    FUN_004cd3e0:  @0 u32 dataBase (0x100; doubles as the version mark),
            @4 u32 dirOffset, @8 u32 (unused here), @0xc u32 dirSize.
  directory at dirOffset: 0x1c-byte header, u16 sectionCount-1, then a blob:
    sections  FUN_004cda40/FUN_004cdfe0: N x 8 bytes [4CC][u16 count][u16 entryOff]
    entries   FUN_004ce010: 12 bytes [u16 id][u16 nameOff][u8 flags][u24 chunkOff]
              [u32 scratch]; per section (count+1) entries at blob+entryOff-2
              (FUN_004cde50 / FUN_004cdf40; the +1 slot is a separator).
    names     FUN_004cdfa0: Pascal strings at blob + strTabOff + nameOff, where
              strTabOff = N*8 + totalEntries*12.
  chunk     FUN_004cdcb0: at dataBase+chunkOff: [u32 len][payload].
  record-array payload (ARCP/ARLU/ARPP)  FUN_004d1a00/FUN_004d1df0/FUN_004d1d70:
            [u16 recSize][u16 rows][u16 cols][2 pad][rows*4 rowptr slots]
            [rows*cols*recSize records];  len == 8 + rows*4 + rows*cols*recSize
            (validated 437/437 chunks in the shipped file).

Sections (loader FUN_004ceab0):
  BODC  one entry per figure (21: pilot..Woman); payload is empty filler.
        The figure's data is keyed by name: name[3]->'c' = ARCP, name[3]->'L' = ARLU
        (FUN_004cfed0 builds both; ANIP clips use name[3]->'i' for ARPP,
        FUN_004d18e0).
  ARCP  per figure: 1 row x nParts x 0x28-byte part records = the skeleton tree.
        Swap handler FUN_004d0090: BE u32 @+8 (4-char node name, root "New "),
        BE u32 @+0xc (4-char parent name), BE u32(float) @+0x1c,+0x20,+0x24
        (part dimensions, e.g. (10,7,0.5)). Bytes +0..+7 and +0x10..+0x1b raw:
        +0 type/color byte, +1 ref, +2 sequential part index, +3..+5 flags,
        +0x10 [u16 8][u16 8]"ARPP"[u32 1] = pose-record link (stride 8, 1/frame).
        Skeleton links resolved by name matching (FUN_004cf8b0).
  ARLU  per figure: 1 row x 18 x 8-byte records [4-char mnemonic][4-char clip
        name], e.g. "1Wal"->"101!", "Dead"->"110!" - the behavior-anim name ->
        clip binding (swap FUN_004d00e0 makes both halves readable u32 names).
  ANIP  one entry per clip (395, names "101!".."495!"); payload empty filler.
  ARPP  per clip: frames(rows) x nParts(cols) x 8-byte pose records, NO byte
        swap (handler FUN_004cea20 is empty). Record = one part's line segment
        for that frame: [s8 x0][s8 y0][s8 z0][u8 scratch][s8 x1][s8 y1][s8 z1]
        [u8 scratch] (z up; chained segments form the body wireframe).

Run:  python Tools/privanim_extract.py [path] [--json out.json] [--figure NAME]
"""
from __future__ import annotations
import argparse, json, struct
from pathlib import Path

DEFAULT = "Reference/SimCopterOriginalGame/X/privanim.df"


def _s8(b: int) -> int:
    return b - 256 if b > 127 else b


class DougFile:
    """Generic Maxis "Doug" DF container reader (privanim.df, people.df)."""

    def __init__(self, data: bytes):
        self.d = data
        self.N = len(data)
        self.data_base = self.u32(0)           # FUN_004cd3e0 -> dirObj+0x18
        self.dir_off = self.u32(4)
        self.dir_size = self.u32(12)
        self.blob = self.dir_off + 0x1c + 2
        n = self.u16(self.dir_off + 0x1c) + 1
        self.sections = []
        for i in range(n):
            o = self.blob + i * 8
            self.sections.append({
                "tag": self.d[o:o + 4].decode("latin1"),
                "count": self.u16(o + 4),
                "entry_off": self.u16(o + 6),
            })
        total = sum(s["count"] + 1 for s in self.sections)
        self.str_tab = self.blob + n * 8 + total * 12

    def u32(self, o): return struct.unpack_from(">I", self.d, o)[0]
    def u16(self, o): return struct.unpack_from(">H", self.d, o)[0]

    def name_at(self, name_off: int) -> str:
        o = self.str_tab + name_off
        ln = self.d[o]
        return self.d[o + 1:o + 1 + ln].decode("latin1", "replace")

    def entries(self, tag: str):
        for s in self.sections:
            if s["tag"] != tag:
                continue
            base = self.blob + s["entry_off"] - 2
            for k in range(s["count"]):
                o = base + k * 12
                yield {
                    "index": k + 1,                       # FUN_004cdf40 is 1-based
                    "id": self.u16(o),
                    "name": self.name_at(self.u16(o + 2)),
                    "flags": self.d[o + 4],
                    "chunk_off": (self.d[o + 5] << 16) | (self.d[o + 6] << 8) | self.d[o + 7],
                }

    def entry(self, tag: str, name: str):
        for e in self.entries(tag):
            if e["name"] == name:
                return e
        raise KeyError(f"{tag} entry {name!r} not found")

    def chunk(self, e):
        """Return (payload_offset, payload_len)."""
        at = self.data_base + e["chunk_off"]
        return at + 4, self.u32(at)

    def record_array(self, e):
        """Parse a record-array chunk -> (rec_size, rows, cols, data_offset)."""
        p, clen = self.chunk(e)
        rs, rows, cols = self.u16(p), self.u16(p + 2), self.u16(p + 4)
        expected = 8 + rows * 4 + rows * cols * rs
        if clen != expected:
            raise ValueError(f"{e['name']!r}: chunk len {clen:#x} != expected {expected:#x}")
        return rs, rows, cols, p + 8 + rows * 4


ANIM_KEY_POS = 3   # FUN_004cfed0/FUN_004d18e0 replace name[3] with 'c'/'L'/'i'


def _key(name: str, ch: str) -> str:
    return name[:ANIM_KEY_POS] + ch + name[ANIM_KEY_POS + 1:]


class Privanim(DougFile):
    def figures(self):
        return list(self.entries("BODC"))

    def skeleton(self, fig_name: str):
        """ARCP part records for a figure: the skeleton/part tree."""
        e = self.entry("ARCP", _key(fig_name, "c"))
        rs, rows, cols, data = self.record_array(e)
        assert rs == 0x28 and rows == 1, (rs, rows)
        parts = []
        for k in range(cols):
            r = data + k * rs
            b = self.d[r:r + rs]
            parts.append({
                "index": k,
                "type": b[0],                # 0x08/0x0b/0x0e/... draw/type byte
                "ref": b[1],
                "seq": b[2],
                "f3": list(b[3:8]),
                "name": b[8:12].decode("latin1"),
                "parent": b[12:16].decode("latin1"),
                "link": [self.u16(r + 0x10), self.u16(r + 0x12),
                         b[0x14:0x18].decode("latin1"), self.u32(r + 0x18)],
                "dims": [round(struct.unpack_from(">f", self.d, r + 0x1c + i * 4)[0], 4)
                         for i in range(3)],
            })
        return parts

    def clip_map(self, fig_name: str):
        """ARLU records: behavior mnemonic -> clip name (e.g. '1Wal' -> '101!')."""
        e = self.entry("ARLU", _key(fig_name, "L"))
        rs, rows, cols, data = self.record_array(e)
        assert rs == 8 and rows == 1, (rs, rows)
        out = {}
        for k in range(cols):
            r = data + k * rs
            out[self.d[r:r + 4].decode("latin1")] = self.d[r + 4:r + 8].decode("latin1")
        return out

    def clips(self):
        return list(self.entries("ANIP"))

    def clip_frames(self, clip_name: str):
        """ARPP pose records: frames x parts line segments [(x0,y0,z0),(x1,y1,z1)]."""
        e = self.entry("ARPP", _key(clip_name, "i"))
        rs, rows, cols, data = self.record_array(e)
        assert rs == 8, rs
        frames = []
        for f in range(rows):
            segs = []
            for k in range(cols):
                r = data + (f * cols + k) * rs
                b = self.d[r:r + 8]
                segs.append(((_s8(b[0]), _s8(b[1]), _s8(b[2])),
                             (_s8(b[4]), _s8(b[5]), _s8(b[6]))))
            frames.append(segs)
        return frames


def extract(path: Path, fig_filter: str | None = None):
    pa = Privanim(path.read_bytes())
    out = {"file": path.name, "size": pa.N, "data_base": pa.data_base,
           "sections": pa.sections, "figures": []}
    for fe in pa.figures():
        if fig_filter and fe["name"] != fig_filter:
            continue
        fig = {"name": fe["name"], "skeleton": pa.skeleton(fe["name"]),
               "clip_map": pa.clip_map(fe["name"]), "clips": {}}
        for mnem, clip in fig["clip_map"].items():
            fig["clips"][clip] = pa.clip_frames(clip)
        out["figures"].append(fig)
    return out


def summarize(pa: Privanim):
    print(f"sections: {[(s['tag'], s['count']) for s in pa.sections]}")
    for fe in pa.figures():
        parts = pa.skeleton(fe["name"])
        cmap = pa.clip_map(fe["name"])
        nframes = {c: len(pa.clip_frames(c)) for c in list(cmap.values())[:3]}
        print(f"  {fe['name']!r:13} parts={len(parts):3} clips={len(cmap)} "
              f"e.g. {dict(list(cmap.items())[:3])} frames={nframes}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("path", nargs="?", default=DEFAULT)
    ap.add_argument("--json", default=None)
    ap.add_argument("--figure", default=None)
    a = ap.parse_args()
    p = Path(a.path)
    if not p.exists():
        print(f"{p}: not found (original game data is not committed); skipping.")
        return 0
    pa = Privanim(p.read_bytes())
    summarize(pa)
    if a.json:
        Path(a.json).write_text(json.dumps(extract(p, a.figure), indent=1))
        print(f"wrote {a.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
