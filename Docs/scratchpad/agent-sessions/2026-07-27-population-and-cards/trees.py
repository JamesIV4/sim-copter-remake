import struct, sys, os
from collections import Counter
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dogstats import read_iff, unpack_rle, tile_class

for name in ['cape wells.sc2', 'city0.sc2']:
    p = os.path.join(r'S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\cities', name)
    if not os.path.exists(p):
        p = os.path.join(r'S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\cities\career', name)
    ch = read_iff(p)
    xbld = unpack_rle(ch['XBLD'])
    hist = Counter(b for b in xbld if 0x06 <= b <= 0x0C)
    total = sum(hist.values())
    print(name, 'tree tiles =', total)
    exp_spawn = 0.0
    for bid in sorted(hist):
        density = bid - 0x05          # 1..7
        cover = density / 7.0
        p_ok = 1.0 - cover ** 2       # original samples twice
        exp_spawn += hist[bid] * p_ok
        print('   0x%02X density %d: %5d tiles, cover %.2f, spawn-point found %.0f%%'
              % (bid, density, hist[bid], cover, 100 * p_ok))
    print('   -> forest tiles that would still yield a spawn point: %.0f of %d (%.0f%%)'
          % (exp_spawn, total, 100 * exp_spawn / max(1, total)))
    print()
