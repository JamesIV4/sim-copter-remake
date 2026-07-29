"""Every op2 expression in people.df that writes a given person attribute (scope 3)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dump_bhav import load, expr  # noqa: E402

want = {int(a, 0) for a in sys.argv[1:]} or {32}
for pid, (name, recs) in sorted(load().items()):
    for i, (op, tn, fn, args) in enumerate(recs):
        if op != 2:
            continue
        tscope = args[3] >> 8
        if tscope == 3 and args[0] in want:
            print(f"BHAV {pid:4d} '{name}' rec[{i:2d}]  {expr(args)}   T->{tn} F->{fn}")
