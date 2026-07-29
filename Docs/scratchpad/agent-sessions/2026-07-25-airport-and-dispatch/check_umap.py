import sys
p = r"S:\Repos\sim-copter-remake\SimCopterRemake\Content\Maps\CityRender.umap"
import glob, os
cands = glob.glob(r"S:\Repos\sim-copter-remake\SimCopterRemake\Content\**\CityRender.umap", recursive=True)
p = cands[0] if cands else p
data = open(p, "rb").read()
for name in [b"VehicleSpeedCmPerSec", b"SimCopterTrafficSystemActor", b"PedestrianSpeedCmPerSec", b"TileSize"]:
    print(f"{name.decode():30s} {'PRESENT' if name in data else 'absent'}")
print("file:", os.path.basename(p), len(data), "bytes")
