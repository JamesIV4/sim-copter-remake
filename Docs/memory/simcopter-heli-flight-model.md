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
- **The surface push is gated on NOT-flat** (FUN_00487160 @ the `param_1[0x53] == 0`
  branch): with the collective neutral and the helicopter at or below the surface,
  ClimbSpeed is forced to `ClimbRate * 4` — but **only on ground it cannot land on**.
  `[0x53]` is the flat flag: 1 = flat/landable (the landing block below tests `== 1`),
  0 = rough, or cleared for a hostile tile. The remake had this inverted (`&&
  bTerrainFlat`) until 2026-07-30, which shoved the helicopter off exactly the flat
  ground it was settling onto, every frame it got there — landing was a coin flip
  undamaged and near-impossible once turbulence widened the attitude gate. Guard:
  `SimCopter.Flight.DamagedLanding`. Note the trap in *testing* this: feathering the
  collective against LandMaxYSpeed hides the bug entirely, because the descend-key
  frames skip the neutral branch. Repro with hold-descend-then-release.
- **Every per-frame rule is now converted to delta time** (2026-07-30). The
  original writes several rules per *frame* with no delta term, tuned at its ~20 fps
  frame; the remake substeps at up to 60 Hz and **shorter than that above 60 fps**
  (`MaxSubstepSeconds` is a max, so 144 fps gives dt = 1/144), so unconverted they
  ran 3x too fast at 60 and 7x at 144. All of them are now expressed against
  `FSimCopterFlightModel::OriginalFrameSeconds` (0.05 s) via two helpers in the .cpp,
  `DecayOverSubstep` (per-frame decay -> `1-(1-r)^(dt/0.05)`) and
  `SubstepFrameFraction` (`dt/0.05`). Converted: neutral-collective climb decay
  (5%/10%), forward-speed chase (`>>5`, `>>4` easy), **the attitude EMA**, fire-band
  burn, the rotor strobe step, and turbulence. Guard:
  `SimCopter.Flight.FrameRateIndependence` (20/30/60/144/240 Hz).
  - **The attitude EMA is the one to be careful with — it is NOT a free parameter.**
    `N = ((1000 - PitchRate)/500) * fps` and the filter removes `1/N` per frame, so
    `N/fps` — the lag in seconds — has the fps terms **cancel**. The original's
    attitude lag is already frame-rate independent by construction; the `* fps` IS
    its delta-time compensation, written the long way round. What breaks it is the
    exe's own floor on the delta (`DAT_005039a0 < 0xccd -> 0xccc`), which pins the fps
    term at 20 while the filter keeps running at the real rate — a 21-frame window at
    60 Hz settles in 0.35 s where the original took 1.02 s.
    Match the filter the original **ran**, not the one the algebra describes: retain
    `(N-1)/N` per 0.05 s. `N` is floored 21.89 -> 21, so the real lag is 1.02 s where
    the un-floored formula says 1.09 — a 7% difference, and the floored one is right.
    Measured original lag by rate: 0.90 s @5 fps, 0.95 @10, **1.02 @20 and above**.
  - Fire damage needed an accumulator (`FireDamageAccrued`): a substep owes a
    fraction of a hit point, so scaling alone would truncate to zero every frame.
  - **Audit of every arithmetic site** in the six flight functions (grep the two
    scratchpad decompiles for `DAT_005039a8`, the smoothed delta, and `DAT_005039a0`,
    the raw one). Sites that ALREADY carry a delta and must be left alone: all of
    `FUN_00485f50` (every control ramp and decay), heading integration, position
    integration, bounce timer, rotor spool rates, the proportional blade step, climb
    and descend ramps, altitude integration, fuel burn, flight timer, and the
    dying-state rotations. Sites with **no** delta term, which are the whole list of
    what needed converting: turbulence sample push + its injection into the targets,
    fire-band burn, the forward-speed chase, the neutral-collective climb decay, and
    the rotor's flat 39.1-degree strobe. Nothing else.
  - Checked and correctly left alone: the autorotation offset
    `ClimbSpeed += SpeedDelta * -2` has no delta term but is already rate-independent,
    because `SpeedDelta` is a per-frame *difference* — its sum over a second is the
    total speed change however often you slice it. Ground-impact damage and attitude
    kicks are one-shot events, not rates. Load factor is an instantaneous ratio.
  - Known benign divergence: the original feeds its sim an 8-frame EMA of the frame
    delta (`DAT_005039a8 = (DAT_005039a8*7 + DAT_005039a0) >> 3`, `FUN_0047a760`)
    because `GetTickCount` only has 10-16 ms resolution. The remake passes its
    substep straight through, which is already near-uniform.
  - Left alone deliberately: the control decays `(1-2*dt)` and `(1-4*dt)` in
    `FUN_00485f50` already carry a delta term. They are first-order approximations of
    `exp()`, so they drift ~10% between 20 fps and the high-rate limit, but that is
    the original's own arithmetic and converting it would change the trim feel.
  Turbulence in particular needs BOTH halves or it gets *worse*, not better:
  1. advance the 9-sample ring on a fixed `TurbulencePeriod` (0.05 s) clock and
     **lerp** `TurbPitch/Slide/Yaw` between ticks, so the noise sequence is the
     original's and nothing steps at 60 Hz;
  2. scale each injection in `StepAttitude` by `Div(Dt, TurbulencePeriod)` so the
     amount added per second is rate-independent. Interpolating alone would have
     made it *worse* — a held value is more correlated frame to frame, pushing the
     random walk from the `1/sqrt(dt)` noise case toward the `1/dt` constant-bias
     case. Excursion is `~T/(2*dt)` for correlated input, `~SD(T)/(2*sqrt(dt))` for
     white.
  **The reference is live-tunable from the debug panel** (REF FPS: TURB / SIM / ACCEL,
  and ROTOR SPIN x), persisted to `[SimCopter.FlightModel]` in GameUserSettings.ini.
  `FSimCopterFlightModel` defaults to the executable's own figures (20 fps, x1) so a
  default-constructed model reproduces the port and the tests assert fidelity; the
  **playable** starting values are set in `ASimCopterHelicopterPawn::BeginPlay` and
  overridden by the ini. Keep that split — putting feel defaults on the struct broke
  `SimCopter.Flight.PitchDrivesSpeed`, because doubled yaw turbulence walked the
  heading past its tolerance.
  - **Tuned defaults as of 2026-07-30: turbulence 20 fps, everything else 60, rotor
    x4.** The shake is the one thing that stays at the executable's own figure — 60
    makes it far too busy — while acceleration, the collective's coast and the fire
    burn all wanted the faster reference.
  - **Split three ways because one knob fights itself**: raising the reference
    sharpens acceleration (wanted) but also makes the airframe shake harder, arrest
    its descent faster and burn quicker in fire (not wanted). The shake and the chase
    pull in opposite directions, so each owns its own period.
  - Rotor spin is pinned to its own fixed 0.05 s, never the tunable reference: it is
    presentation, and a fidelity experiment must not silently change how fast the
    blades look.
  **20 fps is an inference, not a fact, for the five genuinely per-frame rules.** The
  original's frame delta is raw `GetTickCount` floored at 1.5 ms and capped at 0.5 s
  (`FUN_00449850`) — no fixed timestep, so the helicopter really did accelerate and
  shake harder on a faster machine. The single piece of evidence is the 0.05 s delta
  floor in `FUN_00486a30`, the only frame rate the executable ever names, so it is
  the reference used everywhere — except the forward-speed chase, deliberately
  overridden to `SpeedChaseFramePeriod` (1/60 s) because 20 fps acceleration felt
  sluggish. That knob is feel, not fidelity; the rest are fidelity.
  Measured peak pitch after the fix: 7.5/7.4/7.3 (healthy), 53.6/53.1/52.9 (half),
  77.5/76.6/75.9 (near-dead) at 20/30/60 Hz — within 2%. Before it was 140 at 60 Hz
  vs 77 at 20 Hz on a wreck. Guard: `SimCopter.Flight.TurbulenceFrameRate`.
  Costs one tick (50 ms) of lag; interpolation cannot reach a value it has not
  generated yet.
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
- **Easy model** — ported 2026-07-29 as `FSimCopterFlightModel::bEasyFlightModel`
  (default off), toggled from the helicopter debug panel's FLIGHT row and persisted
  to `[SimCopter.FlightModel] EasyModel` in GameUserSettings.ini. Tests:
  `SimCopter.Flight.EasyModel`. `DAT_00503aa0` is **not** a difficulty setting — it is
  the *view mode*, cycled by input action 0x15 in `FUN_004796c0`: 0 = external chase,
  1 = cockpit (fuselage hidden), 2 = second interior view, 3 = map/not-flying (set by
  `FUN_0047a240` on city load). Every flight-model branch tests `!= 0`, so the two
  interior views fly the easy model and the external view flies the standard one. The
  remake decouples that: it is an option, flyable from any camera. Exactly four
  divergences, and two are easy to get wrong:
  - `FUN_00485f50` computes `Mul(SlideRate, load)` **twice**; only the copy the pitch
    keys use gets `>> 1`. The slide keys re-read Ctrl6 unhalved — do not halve both.
  - `FUN_00485f50` pitch decay is `(1 - dt)` instead of `(1 - 2*dt)` per frame.
  - `FUN_00486a30` pitch limit is `(bonus >> 1) + (clamp >> 1)`, halved term by term.
  - `FUN_00486e90` speed target is `PitchSmoothed * 2`, and the *deceleration* shift
    becomes `>> 4` where acceleration stays `>> 5` — half the pitch authority but the
    same top speed, reached with a nose attitude that actually slows you when released.
  The `FUN_00485f50` action-0x10 water-cannon recoil reads the already-halved rate, so
  it is gentler under the easy model too.

## Remake integration compromises (flagged in code)

- Terrain vs object-top heights both come from one down-trace (the ground-agent
  pattern); building **walls** use the capsule sweep → `NotifyObjectCollision`.
- Water gameplay uses the city actor's retained conditioned terrain vertices
  and terrain-class grid; class values below 10 are water.
- No fire system yet → `FireHeightDelta = 0`; no passengers yet → seats count only
  affects the weight budget.
- Axis map: sim Z→UE X, sim X→UE Y, UE yaw = Heading/10; slide+ = left so the
  bank-inherits-slide lean matches.
