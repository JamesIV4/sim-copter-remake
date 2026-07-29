# SimCopter helicopter flight model — decoded + ported (2026-07-02)

Canonical notes for the original helicopter physics/control/animation, decoded from
`SimCopter.exe` and ported as an exact 16.16 fixed-point simulation in
`Source/SimCopterRemake/{Public,Private}/Flight/SimCopterFlightModel.*`, driven by
`ASimCopterHelicopterPawn`. Evidence: `Docs/scratchpad/ghidra/out_heli_*.txt`; function
ledger in `Docs/DecompilationWorkflow.md`. Tests: `SimCopter.Flight.*` (8).

## Units (calibrated, do not re-derive)

- Distances in **world units = 1/64 city tile** as 16.16 (tile = 0x400000). Remake:
  `OriginalUnitToCm = TileSize 400 / 64 = 6.25`.
- Angles in **tenth-degrees** (full turn 3600.0; sin/cos via 901-entry quarter table,
  `FUN_0046c4dc`). heli.twk "10 = 1 deg" values map 1:1.
- `DAT_005039a8` frame delta is **seconds** (16.16) — calibrated via the fuel burn
  (`fuel -= 0x11 * FuelRate * dt` ≈ gal/hr) and cruise speed (Jet Ranger MaxPitch 192.3
  → 117 units/s ≈ 114 kt at 32 m tiles).

## The flight model in one paragraph

Keys **ramp** attitude *targets* (trim feel); joystick axes **seek** −axis×{3,6,2}
tenth-deg. Targets clamp to the twk maxima; all four smoothed values follow with one
shared EMA: `N = ((1000 − PitchRate)/500) × fps`, **fps capped at 20** (dt floored at
0.05 s in that formula only). **Forward speed = smoothed pitch** (tenth-degrees double
as units/s), chased at 1/32 per frame; slide contributes ×0.488; position +=
vel × dt × 0.610. Heading += smoothedYaw × **15** × dt. Display matrix uses the pitch
*target* + smoothed bank, and **bank state inherits the slide when |slide| > |bank|**
(persistent overwrite quirk).

## Key mechanics

- **Load factor** `[0xce] = (seats×120 + MaxLoad + 30 − (passengers×120 + load)) / cap`
  scales every control ramp rate and the climb cap.
- **Rotor** `[0x56]`: collective-up spools +100/s; **no lift until 300** (≈3 s), then
  state→Flying; tops at 360 in flight; −50/s parked, −200/s dying. Blade angle steps
  `min(rotor×32×dt, 39.1°)` per **frame** (deliberate strobe). Face-type-11 blur discs
  toggle **on at 300**. NOTAR types (MDEXPLORER, MD520; static flag +0x38) hide the
  tail rotor; tail offsets at +0x2c..0x34 of the 0x5c static block at `0x5040e4`
  (seats at +0x00: JR 4, H500 4, Apache 0, Bell 14, Schw 2, Agusta 7, Dauphin 13,
  MDX 7, MD520 4).
- **Climb** `[0x4d]`: up ramps +2×ClimbRate/s to cap 4×ClimbRate×load; neutral decays
  5%/frame up, 10%/frame down; descend ramps −2×MaxDescent/s, floor −4×; **ceiling
  `DAT_0050404c` = 800 units AGL** — above it any collective sinks at MaxDescent.
  Autorotation: out of fuel, decelerating (`speedDelta<0`) offsets the sink.
- **Which twk field ramps what** — the easiest thing to get wrong: pitch and slide keys
  ramp at Ctrl6 **"SlideRate"**, *not* Ctrl3 PitchRate. PitchRate does nothing but shape
  the shared EMA lag `N` above. Wiring the ramp to PitchRate looks plausible and flies
  wrong.
- **Pitch clamp extras**: positive climb impulse derates MaxPitch (down to ½); within
  150 units of ground a bonus up to MaxPitch/8 scales with height. Bank additionally
  clamps to |smoothedPitch| + 300 (30°).
- **Landing** (FUN_00487160): needs terrain **flat flag** (sampler `FUN_004ae7a0`:
  triangle corners within 9.0 units), altitude within ±1.0 of surface, and *targets*
  within Heli Landing twk (pitch 51.6, slide 43.3, speed 42.1, yspeed 20.1); snaps to
  surface + **1.2 units** and parks. Tile class < 10 with nothing built (density grid
  `DAT_005bde80`) = hostile: no landing, splash bounce.
- **Ground impact** (master tick): penetrating rough terrain → clamp + dust + sound,
  damage −4 (−4−4×CollisionSubtract out of fuel), random kicks ±(Max/4) via
  `1−rand%3`, bounce up 4×ClimbRate (water: ×2 and splash). Sinking below an elevated
  surface → damage −CollisionSubtract, kick pitch/slide by helicopter-local motion
  direction (pitch = −MaxPitch×fwdComponent), bounce 4×ClimbRate, **bounce timer
  0x3333 (0.2 s)** during which all control input is skipped and speed is forced to
  pitchTarget/8. Object AABB hits (`FUN_0048ad50`) → damage −4 + the same kick.
- **Turbulence** (FUN_00489800): per frame push `±(rand%amp)` tenth-deg into 9-sample
  ring buffers (pitch/slide/yaw); averages are **added to the targets** every frame.
  amp = 3 healthy; else `(250 − fireDist) + (MaxDamage − hp)/20`, min 1. Fire band
  [MinFireAlt −48, MaxFireAlt 61.1] burns `MaxFireAlt − dist` hp per frame.
- **Deaths**: hp < 0 → state 5 (tumble/fall, driven by a spawned crash effect in the
  original; remake integrates an equivalent spiral) → ground → state 6 → respawn at
  nearest pad (remake: repair-in-place for now).
- **Easy model** (`DAT_00503aa0 != 0`, cockpit view): half control ramps and pitch
  clamp, double speed-per-pitch, gentler decay. NOT ported (mode 0 only); the same
  global is also the view mode (1 hides the fuselage) and 3 = transition.

## Remake integration compromises (flagged in code)

- Terrain vs object-top heights both come from one down-trace (the ground-agent
  pattern); building **walls** use the capsule sweep → `NotifyObjectCollision`.
- Water gameplay uses the city actor's retained conditioned terrain vertices
  and terrain-class grid; class values below 10 are water.
- No fire system yet → `FireHeightDelta = 0`; no passengers yet → seats count only
  affects the weight budget.
- Axis map: sim Z→UE X, sim X→UE Y, UE yaw = Heading/10; slide+ = left so the
  bank-inherits-slide lean matches.
