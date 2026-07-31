#!/usr/bin/env python3
"""Compare the two footprint-owner rules over the SimCopter career cities.

The city renderer claims building squares in row-major XBLD order
(FSimCopterCityGeometryRules::ClaimOriginalBuildingFootprint, FUN_0047c0c0).
The traffic system's pedestrian-node builder still used the old
"XZON high nibble owns the footprint" rule.  This prints how many buildings -
and specifically how many XBLD 0xD1 hospitals - the old rule loses.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "Tools"))
from sc2_probe import decode_rle, UNCOMPRESSED_CHUNKS  # noqa: E402

MAP = 128

FOOTPRINT_70 = (
    [1] * 28 + [2] * 4                      # 0x70..0x8f (0x8c..0x8f are 2)
    + [2] * 16                              # 0x90..0x9f
    + [2] * 14 + [3, 3]                     # 0xa0..0xaf
    + [3] * 16                              # 0xb0..0xbf
    + [3] * 6 + [1, 1, 1] + [4] * 7         # 0xc0..0xcf
    + [3] * 7 + [4] * 4 + [1] * 5           # 0xd0..0xdf
    + [1] * 11 + [2] * 5                    # 0xe0..0xef
    + [2] * 8 + [3] * 3 + [4] * 5           # 0xf0..0xff
)
assert len(FOOTPRINT_70) == 0x90


def footprint_size(building: int) -> int:
    if (0x49 <= building <= 0x50) or (0x61 <= building <= 0x6B):
        return 2
    if building < 0x70:
        return 1
    return FOOTPRINT_70[building - 0x70]


def load_grids(path: Path) -> tuple[bytes, bytes]:
    data = path.read_bytes()
    offset, grids = 12, {}
    while offset + 8 <= len(data):
        chunk = data[offset : offset + 4].decode("ascii", "replace")
        size = int.from_bytes(data[offset + 4 : offset + 8], "big")
        offset += 8
        payload = data[offset : offset + size]
        offset += size
        if chunk in ("XBLD", "XZON"):
            grids[chunk] = payload if chunk in UNCOMPRESSED_CHUNKS else decode_rle(payload)
    return grids["XBLD"], grids["XZON"]


def claim_owners(xbld: bytes) -> dict[tuple[int, int], int]:
    """Row-major scene-cell claim: the renderer's / the original's rule."""
    state = bytearray(MAP * MAP)  # 0 free, 1 claimed, 2 rejected
    owners: dict[tuple[int, int], int] = {}
    for y in range(MAP):
        for x in range(MAP):
            index = y * MAP + x
            if state[index] != 0:
                continue
            building = xbld[index]
            if building < 0x70:
                continue
            size = footprint_size(building)
            if x + size > MAP or y + size > MAP:
                state[index] = 2
                continue
            ok = True
            for dy in range(size):
                for dx in range(size):
                    candidate = (y + dy) * MAP + x + dx
                    if state[candidate] == 2 or xbld[candidate] != building:
                        ok = False
            if not ok:
                state[index] = 2
                continue
            for dy in range(size):
                for dx in range(size):
                    state[(y + dy) * MAP + x + dx] = 1
            owners[(x, y)] = size
    return owners


def zone_owners(xbld: bytes, xzon: bytes) -> dict[tuple[int, int], int]:
    """The old people-footprint rule: XZON high nibble gates, then same-XBLD square."""
    owners: dict[tuple[int, int], int] = {}
    for y in range(MAP):
        for x in range(MAP):
            index = y * MAP + x
            building = xbld[index]
            if building < 0x70:
                continue
            zone = xzon[index]
            if (zone & 0xF0) != 0xF0 and (zone & 0x80) == 0:
                continue
            size = footprint_size(building)
            if x + size > MAP or y + size > MAP:
                continue
            if all(
                xbld[(y + dy) * MAP + x + dx] == building
                for dy in range(size)
                for dx in range(size)
            ):
                owners[(x, y)] = size
    return owners


def main() -> int:
    root = Path("Reference/SimCopterOriginalGame/cities/career")
    print(f"{'city':10} {'claims':>7} {'zoneOK':>7} {'hosp':>5} {'hospOK':>7}  missing hospitals")
    for path in sorted(root.glob("city*.sc2"), key=lambda p: int(p.stem[4:])):
        xbld, xzon = load_grids(path)
        claimed = claim_owners(xbld)
        zoned = zone_owners(xbld, xzon)
        hospitals = {xy: s for xy, s in claimed.items() if xbld[xy[1] * MAP + xy[0]] == 0xD1}
        # A hospital is reachable by the pedestrian-node builder only if some tile of it
        # passes the old rule; the old rule scans from that tile, so it must be the origin.
        missing = [xy for xy in hospitals if xy not in zoned]
        print(
            f"{path.stem:10} {len(claimed):7} {len(zoned):7} {len(hospitals):5} "
            f"{len(hospitals) - len(missing):7}  {missing}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
