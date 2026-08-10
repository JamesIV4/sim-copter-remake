"""Which edge of each RD31..RD34 road ramp is the high one?

Reads the asphalt (face type 15 / material 48) vertices straight out of the GEO container and
reports where the height sits against the two horizontal axes, then converts to the city's world
axes so the answer can be written into the terrain conditioning pass.

City placement (SimCity2000CityActor + MaxisMeshReader::ConvertMaxisVertexToUnreal, then the
global 180-degree yaw):  worldX = -Maxis.Z,  worldY = -Maxis.X,  worldZ = Maxis.Y
and +worldY is north (decreasing FileY), +worldX is east (increasing FileX).
"""

from dump_maxis_object import GEO_DIR, load_objects

FIXED = 65536.0


def main():
    catalog = {}
    for geo in sorted(GEO_DIR.iterdir()):
        if geo.suffix.lower() == ".max":
            catalog.update(load_objects(geo))

    for name, xbld in (("RD31", 0x1F), ("RD32", 0x20), ("RD33", 0x21), ("RD34", 0x22),
                       ("RD67H", 0x43), ("RD68H", 0x44)):
        obj = catalog[name]
        vs = obj["vertices"]
        idx = set()
        for f in obj["faces"]:
            if f["type"] == 15 and f["material"] == 48:
                idx.update(f["indices"])

        pts = [vs[i] for i in sorted(idx) if i < len(vs)]
        # world axes
        conv = [(-z / FIXED, -x / FIXED, y / FIXED) for (x, y, z) in pts]
        lo = min(p[2] for p in conv)
        hi = max(p[2] for p in conv)
        print(f"\n=== {name} (XBLD 0x{xbld:x})  asphalt Z {lo:.2f} .. {hi:.2f}")
        for wx, wy, wz in sorted(conv, key=lambda p: (-p[2], p[0], p[1])):
            print(f"    east(x)={wx:8.2f}  north(y)={wy:8.2f}  up(z)={wz:8.2f}")

        high = [p for p in conv if p[2] > (lo + hi) / 2.0]
        if high and hi - lo > 0.01:
            ex = sum(p[0] for p in high) / len(high)
            ny = sum(p[1] for p in high) / len(high)
            axis = "east" if ex > 0 else "west" if ex < 0 else ("north" if ny > 0 else "south")
            if abs(ny) > abs(ex):
                axis = "north" if ny > 0 else "south"
            print(f"    --> high edge faces {axis.upper()}")


if __name__ == "__main__":
    main()
