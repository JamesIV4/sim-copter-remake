"""List every people.df BHAV id + name (read-only)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[0]))
sys.path.insert(0, r"S:\Repos\sim-copter-remake\Tools")
from privanim_extract import DougFile

df = DougFile(Path(sys.argv[1]).read_bytes())
rows = sorted(((e["id"], e["name"]) for e in df.entries("BHAV")))
for i, n in rows:
    print(f"{i}\t{n}")
