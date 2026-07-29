"""Reproduce the decoded type-6 water trajectory (FUN_0048ed00 + FUN_0048e0b0)
and report where a fire-truck jet lands, at several frame rates.

Fixed point is 16.16 throughout, matching the original.
"""
import math

ONE = 0x10000
GRAVITY = 0x280000      # 40.0 units/s^2   (FUN_0048ed00)
DRAG_T6 = 0x28f         # ~0.9994% per frame, type 6
LIFE = 0x50000          # 5.0 s
NOZZLE_UP = 0x1e0000    # 30.0 units above the vehicle (FUN_004a5ca0)
ELEV_STEP = 0x1999
ELEV_MAX = 0x40000      # building-flame bound


def fmul(a, b):
    return (a * b) >> 16


def normalize(v):
    x, y, z = v
    length = math.sqrt((x / ONE) ** 2 + (y / ONE) ** 2 + (z / ONE) ** 2)
    if length == 0.0:
        return (0, 0, 0), 0
    return (int(x / length), int(y / length), int(z / length)), int(length * ONE)


def flight(distance_units, elevation, speed_units, fps, target_height_units=0.0):
    """Returns (horizontal distance travelled, peak height) until the droplet
    falls back to target_height_units, or None if it never does within its life."""
    dt = int(ONE / fps)
    # FUN_004b9b10 normalises (target - vehicle); FUN_004a5ca0 then substitutes
    # the swept elevation for the Y component and renormalises.
    aim = (ONE, 0, 0)
    launch, _ = normalize((aim[0], elevation, aim[2]))
    direction = launch
    speed = int(speed_units * ONE)
    life = LIFE
    x = 0
    y = NOZZLE_UP
    peak = y
    while life > 0:
        life -= dt
        speed = max(0, speed - fmul(DRAG_T6, speed))
        vel = (fmul(direction[0], speed), fmul(direction[1], speed), fmul(direction[2], speed))
        vel = (vel[0], vel[1] - fmul(GRAVITY, dt), vel[2])
        direction, speed = normalize(vel)
        step = fmul(speed, dt)
        x += fmul(direction[0], step)
        y += fmul(direction[1], step)
        peak = max(peak, y)
        if y <= int(target_height_units * ONE) and direction[1] < 0:
            return x / ONE, peak / ONE
    return None, peak / ONE


def sweep(distance_units, fps):
    speed = distance_units / 2 + 50          # rand()%100 averages 50
    best = 0.0
    reach = []
    elevation = 0
    while elevation <= ELEV_MAX:
        landed, peak = flight(distance_units, elevation, speed, fps)
        if landed is not None:
            reach.append((elevation / ONE, landed, peak))
            best = max(best, landed)
        elevation += ELEV_STEP
    return speed, best, reach


for distance in (64, 128, 192, 320):
    print(f"\n=== target {distance} units away ({distance/64:.0f} tiles) ===")
    for fps in (15, 20, 30, 60, 120):
        speed, best, reach = sweep(distance, fps)
        hits = [e for (e, d, _) in reach if abs(d - distance) <= 16]
        print(f"  {fps:3d} fps  launch speed {speed:5.0f}  max reach {best:7.1f} units"
              f"  ({best/64:5.2f} tiles)  sweep angles landing on target: {len(hits)}/{len(reach)}")
