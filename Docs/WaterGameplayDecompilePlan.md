# Water gameplay decompile plan

Scope: everything between "the player collects water" and "a fire goes out" —
rope, bucket, water cannon, water particles, their impact resolution, the
effects they spawn, and the UI/audio feedback around them.

Status: **core gameplay port completed 2026-07-24.** The remake now uses the
decoded pound-based load, conditioned terrain/class grids, 20-node rope,
type-5/type-6 fixed-point trajectories, particle-owned impact dousing, and the
water-cannon emitter. The follow-up pass also loads the authored `BUCKET` GEO
object, renders the rope from the fuselage attachment point, exposes
deploy/raise/lower/dump controls in the flight HUD, keeps fire seated only on
city geometry, and resolves water hits against the visible position of flames
that extend beyond a large building's anchor tile. Claims below remain the
evidence and acceptance criteria for that port; unresolved original behavior
is still recorded as a blocker rather than approximated.

Written 2026-07-24. Companion documents:
`Docs/scratchpad/ghidra/fire_simulation_decode_20260724.md` (the fire side,
already ported), `Docs/FireAndDemolitionGaps.md` (gap 7 is the entry point for
this work), `Docs/FireWaterDustBackwashPortPlan.md` (the effect *renderer*,
which this plan depends on but does not duplicate).

Implemented in:

- `Flight/SimCopterWaterGameplay.*` for fixed-point rates, motion, impact
  strength, fill gates, and terrain-triangle sampling;
- `ASimCity2000CityActor` for retained conditioned gameplay grids;
- `ASimCopterHelicopterPawn` for pounds, the rope, automatic filling, bucket
  emission, cannon emission/recoil, load coupling, the original `BUCKET` mesh,
  and the persistent water-control/status hint;
- `USimCopterParticleFXComponent` and
  `ASimCopterMissionSystemActor::ApplyWaterParticleImpact` for collision-owned
  splashes and footprint-aware dousing;
- `SimCopter.Water.*` automation for the verification gates that do not require
  a manual flight scenario.

Still blocked exactly as recorded in section 4: bucket-strike behavior IDs,
career ownership of capability bits, and exact gauge/message feedback. The
person-in-bucket loop in Tier 6 also awaits its two remaining decompiles.

---

## 1. Pre-port baseline (closed by the implementation above)

At the start of this plan,
`ASimCopterHelicopterPawn::UpdateRopeAndBucket` was a hand-written
approximation with no counterpart in the original:

- `BucketWaterFraction` is a 0..1 float advanced by `BucketFillPerSec *
  DeltaSeconds`. The original stores **pounds of water** in `heli[0x74]`,
  advanced by a flat `+= DAT_0050402c` **once per frame with no delta scaling**,
  capped at the helicopter's `Max Load` (heli.twk Ctrl8, 1548 lb on the Jet
  Ranger).
- Filling is gated on `ProbeBucketWater`, a downward line trace against
  `WaterFillWorldZ`. The original gates on two independent things: the bucket's
  Y against the interpolated water surface (`FUN_004ae7a0`) plus `0x20000`
  slack, **and** the terrain-class grid `DAT_005bde80` reading below 10 at the
  bucket's tile.
- Dumping calls `MissionSystem->DumpWaterAt(BucketWorld)`, which douses the
  tile under the bucket directly, once per frame, at a hardcoded `0x10000`
  strength. **The original never douses from the bucket.** The bucket emits a
  water *particle*; the particle flies, ages, and only calls the douse when it
  collides with a burning cell — at a strength equal to its own remaining life.
  That is the whole reason the current version feels wrong: there is no travel
  time, no miss, no spread, and no dependence on where the water lands.
- The rope is a scaled cylinder and the bucket a scaled cube hanging at a fixed
  offset. The original simulates a **20-node rope chain** whose sag scales with
  the water load, whose nodes collide with the world, and whose swing velocity
  is what throws the water sideways when it is dumped.
- There is no water cannon at all. The original has a second, entirely separate
  delivery system (see §3.2) that douses at **four times** the bucket's
  per-particle strength.

So the fix is not tuning. The bucket has to stop being a douse emitter and
become a particle emitter, and the particle system has to own the douse.

---

## 2. Ground already covered by this scoping pass

Read directly out of the decompiles named in §4. Recorded here so the port does
not re-derive it — **but each claim must still be re-checked against the
function it came from when that function is ported.** The implemented core now
follows these values.

### 2.1 Tuning: `[Heli Ropestuff]` (heli.twk, bound by `FUN_00489e20`)

| Ctrl | Label | Global | Shipped | Use |
| ---: | --- | --- | ---: | --- |
| 0 | Bucket Fill Rate | `DAT_0050402c` | 30 | pounds added per frame while dipped |
| 1 | Bucket Dump Rate | `DAT_00504030` | 21 | pounds removed per frame while dumping; the cannon uses `>> 1` |
| 2 | Rope Load Factor | `DAT_00504034` | 102 | scales rope sag with the water fraction |
| 3 | Rope Tension | `DAT_00504038` | 0.5 | per-node restoring term in the chain solve |
| 4 | Water Throw | `DAT_0050403c` | 49 | scales bucket swing into horizontal scatter of dumped water |
| 5 | Cannon Force | `DAT_00504040` | 128.6 | muzzle speed of cannon water |

Capacity is **not** in this section: it is `Max Load`, Ctrl8 of the
per-helicopter section (`DAT_005040e8 + heliType * 0x5c`).

### 2.2 The rope / attachment state machine (`FUN_00487bb0`, `FUN_00485f50`)

- `heli[0x6f]` is the index of the first *active* rope node, range **3..0x11**,
  in a 20-node chain (`FUN_0046ec60(0x14, 0x78, DAT_00504068)`). Higher = shorter.
  `0x11` is fully stowed; the rope and its end object are hidden there.
- `heli[0x72]` is the command: `+1`/`+2` raise, `-1`/`-2` lower, `0` idle. Two
  attachments exist: GEO object **`0x7b` (`BUCKET`)** and object **`0x16d`
  (`HARNESS`)**, selected by whether `heli[0x70]` or `heli[0x71]` is the
  deployed one. `heli[0x70] == 0` means the bucket is on the rope, and every
  water interaction tests it. The previously recorded `0x141`/`0x142` pair is
  the Agusta helicopter body/rotor pair, not the rope attachments.
- Input actions: **`0xb`/`0xc`** work the bucket rope, **`0xe`/`0xf`** the other
  attachment (gated on capability bit `DAT_00504060 & 4`), **`0xd`** dumps,
  **`0x10`** fires the cannon. All read through
  `FUN_0041c2a0` / `FUN_0041c2c0` / `FUN_0041c2e0` off the keymap at
  `DAT_004f9a0c + 0x128` / `+ 0xa18`.
- Sound `0x15` is the rope motor: started on any movement, volume forced to
  `0xa0` while raising, stopped when idle.

### 2.3 Filling (`FUN_00487bb0`)

All of these must hold, per frame:

1. the bucket is the deployed attachment (`heli[0x70] == 0`);
2. bucket Y `<= FUN_004ae7a0(bucketX, bucketZ, 0) + 0x20000`;
3. `DAT_005bde80[tile]` is below 10 — the terrain-class grid built by
   `FUN_004abce0` (already ported as the tmap conditioning pass) writes `0` and
   `5` for water/shore and `0x10`/`0x20`/`0x40`/`0x50`/`0x60` for land classes,
   so "below 10" is exactly "water or shoreline".

Then `heli[0x74] += DAT_0050402c`, clamped to `Max Load`. **While the bucket is
under water it also emits a class-3 tile puff every frame**, and while it is
still filling (not yet full) a class-8 puff plus looping sound `10`.

`FUN_004c0c40` is called on the same path: it scoops a **person** into the
bucket, held in `DAT_00506458`. `FUN_004c0cb0` releases them with behaviour
`0x125` at the moment the water crosses to zero during a dump.

### 2.4 Dumping (`FUN_00488060`)

Requires `heli[0x73] == 1` (dump held) and `heli[0x74] != 0`. Per frame:

- `heli[0x74] -= DAT_00504030`, clamped at 0; crossing to zero fires
  `FUN_004c0cb0`.
- Emits **one** type-6 particle from eight original units below the bucket,
  travelling along the negated rope-end direction at speed `0xf0000`.
- Horizontal scatter is `±1 * (previousBucketPos - currentBucketPos)` scaled by
  **Water Throw** — i.e. swinging the bucket throws the water sideways. The
  swing term `heli[0x6e]` is computed in `FUN_00487bb0` from the change in the
  *normalised* rope-end direction, clamped at zero so only upward swing counts.

`FUN_004883a0` additionally sets `heli[0x73] = 1` when the bucket strikes a
world object, so a collision spills the load.

### 2.5 The water cannon (`FUN_00484d20`, `FUN_00485f50`)

A second delivery system the remake has no trace of. Input `0x10`, gated on
capability bit `DAT_00504060 & 0x10` (read from the career record at `+0x48`):

- sets emitter `heli[0x57] = 5`, drains `heli[0x74] -= DAT_00504030 >> 1` per
  frame, and pitches the nose (recoil) by `fixmul(frameTime, CannonForce)`;
- emits a **type-5** particle from the fuselage at speed
  `heli[0x4e] + CannonForce`;
- with no water: message `0x2a7`, sound `0x80`, emitter cleared. With no cannon
  fitted: message `0x2a9`.

The same emitter slot carries the Apache's weapons (types 1/2/3), so this is one
mechanism, not two.

### 2.6 Water particles: life, motion, and the douse

Both water types live in the 70-slot pool `DAT_005d4f30` and are spawned by
`FUN_0048e0b0`:

| Type | Class flag | Emitter | Face | Douse strength at impact |
| ---: | ---: | --- | ---: | --- |
| 5 | `0x20` | water cannon | `0x1a` class 3 | `life` (full) |
| 6 | `0x40` | bucket | `0x1a` class 0 | `life >> 2` |

- Life (`p[1]`) starts at **`0x50000`** (5.0 s) for both and decays by the
  smoothed frame delta `DAT_005039a8`.
- Speed (`p[3]`) decays multiplicatively per frame — drag **`0x51e`** for the
  cannon, **`0x28f`** for bucket water (half) — and gravity subtracts
  `fixmul(0x280000, frameTime)` from the Y component before the direction is
  renormalised.
- `FUN_0048ed00` advances the particle, moves it between effect cells, and calls
  `FUN_00490690` for both the old and the new cell each step.
- `FUN_00490690` is where the fire actually goes out. On a hit it spawns a
  splash puff (class 8 if `life < 0x20000`, else class 9), plays sound `10`, and
  calls **`FUN_004a50c0(cell, position, strength)`** with the strengths in the
  table above. Landing on water instead (`DAT_005bde80 < 10`) is a class-8 puff
  and sound `0xf`, with no douse.

This is the single most important structural fact in this document:
**douse strength is the particle's remaining life, so water thrown from high up
or from far away extinguishes less.** The remake's fixed `0x10000` cannot
reproduce that, and neither can any per-frame call from the bucket.

---

## 3. Decompile targets

"In exports" means `.ghidra-exports` already has a decompile and the
`ghidra-bridge` main path can read it. "Fallback" means it is outside the 2,764
exported functions and needs `analyzeHeadless` + `ReverseExplore.java`
(see `Docs/DecompilationWorkflow.md`).

### Tier 1 — the bucket/cannon state machine

| Address | Role | What it answers | Source |
| --- | --- | --- | --- |
| `FUN_00485f50` | player input handler | action ids, rope command encoding, dump gating, cannon gating, all the failure messages | in exports |
| `FUN_00487bb0` | per-frame rope + bucket update | the fill test, the fill rate, the underwater effects, the swing term feeding Water Throw | in exports |
| `FUN_004883a0` | 20-node rope chain solve | node integration, Rope Tension / Rope Load Factor semantics, rope-vs-world collision, the spill-on-impact rule | in exports |
| `FUN_00488060` | bucket dump | drain rate, particle spawn position/direction/speed, scatter | in exports |
| `FUN_00484d20` | helicopter master update | call order of the above, the emitter dispatch (`heli[0x57]`), cannon speed and half-rate drain, and the **total-load → lift** calculation that couples water weight to flight | in exports |
| `FUN_00483c20` | rope construction | 20 nodes, segment length `0x78`, sag `DAT_00504068`, and end models `0x7b` (`BUCKET`) / `0x16d` (`HARNESS`) | in exports (decompiled 2026-07-24) |
| `FUN_00487740` | called immediately before `FUN_00487bb0` | unknown; must be read before the rope port is trusted | **needs decompile** |
| `FUN_0046ec60` | rope node allocator | node stride and initial layout | **needs decompile** |

### Tier 2 — particle life and impact (this is what puts fires out)

| Address | Role | What it answers | Source |
| --- | --- | --- | --- |
| `FUN_0048e0b0` | particle spawn | pool selection, class flags per type, life `0x50000`, sub-timer, per-type speed overrides, the spawn-time tile puff | in exports |
| `FUN_0048ed00` | master particle updater | drag constants, gravity, renormalisation, cell handoff, retirement, sub-particle emission | in exports |
| `FUN_00490690` | impact resolver | **the douse call sites and their strength terms**, splash class selection, sounds, water-landing branch, and the fire-particle ignition branch | in exports |
| `FUN_00491370` | ray/sphere test against cell objects | what counts as a hit and at what distance | **needs decompile** |
| `FUN_0046efe0` | geometry intersection returning a surface basis | how a particle reflects/stops on a face | **needs decompile** |
| `FUN_0049a4f0` | object interaction dispatch | routes bucket-strike modes `0x11`/`0x12` by object class — same dispatcher as the fire damage sweep | in exports |
| `FUN_004a50c0` | the douse itself | already decoded and ported; re-read only to confirm the strength parameter | **done** |

### Tier 3 — the water world

| Address | Role | What it answers | Source |
| --- | --- | --- | --- |
| `FUN_004ae7a0` | water surface height at (x, z) | bilinear interpolation over `DAT_005cde80`, plus the flat-cell flag the third parameter returns | in exports |
| `DAT_005cde80` | water height grid | stride from `DAT_00506170`, mask `DAT_0050616c` | resolve via loader |
| `DAT_005bde80` | terrain class grid | confirm `0`/`5` are the only sub-10 values the loader can produce; the remake's `FUN_004abce0` port must expose it | verify against ported code |

### Tier 4 — graphics

The *renderer* is already planned in `Docs/FireWaterDustBackwashPortPlan.md`;
this tier is only the water-specific spawners feeding it.

| Address | Role | Source |
| --- | --- | --- |
| `FUN_004af220` | tile splat/puff pool (100 slots), classes 0..10 with per-class rise speed and the `0x10` render flag; drives every water splash in this document | in exports |
| `FUN_004af100` | splash column pool (20 slots), scale `4 << mode` | in exports |
| `FUN_004af3b0` | third effect spawner — role unconfirmed, listed by the renderer plan | in exports, **role unverified** |
| `FUN_00496da0` + the `0x1a` kernels | how a water drop is actually rasterised: class **0** for bucket water, class **3** for cannon water | covered by the renderer plan |
| `FUN_0046e590`, `FUN_004704d1`, `FUN_0046f610`, `FUN_0047022b` | node scale, transform, visibility, model bind | **need decompile** (small) |

### Tier 5 — feedback the player reads

| Target | Role | Source |
| --- | --- | --- |
| consumer of `WATERGGE.BMP` (string `0x004f9824`) | the cockpit water gauge — the only readout of `heli[0x74]` | **fallback**: no exported function references it (`ghidra-bridge strings` finds the string with no owner) |
| `FUN_0048c4c0` + ids `0x2a7`, `0x2a9`, `0x2ac`, `0x2ad` | "bucket empty" / "no cannon" / etc. on-screen messages | **needs decompile** |
| `FUN_0042a1f0`, `FUN_0042a2a0`, `FUN_0042a310`, `FUN_0042a330`, `FUN_0042a3a0` | positional sound start / start-global / stop / set-volume / query | **need decompile** |
| the sound-id table | ids used here: `7`, `9`, `0xa`, `0xb`, `0xf`, `0x15`, `0x16`, `0x1f`, `0x80` — `WATERCAN.WAV` is in the shipped set | **needs the id → WAV mapping** |

### Tier 6 — the person in the bucket

A genuine feature, currently absent, and cheap once the fill path exists.

| Address | Role | Source |
| --- | --- | --- |
| `FUN_004c0c40` | grabs a nearby person while scooping, holds them in `DAT_00506458` | in exports |
| `FUN_004c0cb0` | releases them with behaviour `0x125` when the bucket empties | in exports |
| `FUN_004c3eb0` | the person search (`type 0x15`, radius `0xf`) | **needs decompile** |
| `FUN_004cea00` | the eligibility test on the candidate | **needs decompile** |

---

## 4. Blockers — recorded, not worked around

1. **Bucket-strike damage.** `FUN_004883a0` calls `FUN_0049a4f0` with modes
   `0x11`/`0x12` when the bucket hits an object. For people that routes to
   `FUN_004c1050`, which reads the new behaviour id from `(&DAT_0058d728)[mode]`
   — the *same* table that blocks fire damage (gap 1 in
   `Docs/FireAndDemolitionGaps.md`). The rope port can land without it; the
   damage cannot. Do not invent a behaviour id.
2. **The water gauge.** A 2026-07-24 `xrefsto 0x004f9824` pass reached
   `FUN_00455170`, which lazily loads `WATERGGE.BMP` into the owning object at
   `+0xbc`. That establishes the asset owner but not the gauge's drawing/value
   consumer; the relevant vtable draw path still needs to be named and exported
   before the original gauge can be ported. The remake now has a compact,
   persistent status/control panel with a numeric water readout and capacity
   bar shared by bucket and water-gun modes, plus rope length and the `R`, Page
   Up, Page Down, `G`, and left-click actions; that is an explicit gameplay
   affordance, not a claim that the original gauge has been recovered.
3. **Helicopter capability bits.** `DAT_00504060` comes from the career record
   at `+0x48`. Which helicopters carry a cannon (`& 0x10`) and which carry the
   second rope attachment (`& 4`) needs that record's layout, which the career
   loader port covers rather than this one. An explicitly labelled debug button
   can grant and select the water gun for testing: left click then fires it,
   while `G` still dumps the bucket. This does not assign the capability to a
   production helicopter or close the career-data blocker.
4. **Face `0x1a` class 0 vs class 3 (closed by the renderer port).** Bucket and
   cannon water use the existing decoded selector-kernel path with class 0 and
   class 3 respectively; no substitute radial quad was introduced.

---

## 5. Suggested order

Each step is independently testable, and each leaves the game in a working
state. Steps 1–3 are what make dousing correct; 4–6 are what make it feel like
the original.

1. **Particle-owned douse.** Tier 2 (`FUN_0048e0b0`, `FUN_0048ed00`,
   `FUN_00490690`) plus the two collision primitives. Move the douse call out of
   `DumpWaterAt` and into particle impact, with strength = remaining life.
   Closes gap 7 of `Docs/FireAndDemolitionGaps.md` properly rather than
   patching the strength term.
2. **Water world queries.** `FUN_004ae7a0` and the two grids, so "is the bucket
   in water" and "did the drop land in water" stop being line traces.
3. **Fill/dump in original units.** `FUN_00487bb0` and `FUN_00488060`: pounds,
   per-frame rates, `Max Load` cap, and the type-6 emission. Retire
   `BucketWaterFraction` as the source of truth (keep it as a derived display
   value if the HUD wants one).
4. **The rope.** `FUN_004883a0` and `FUN_00483c20`: 20 nodes, tension, load sag,
   collision, and the swing term that Water Throw scales.
5. **The cannon.** The `heli[0x57]` emitter path in `FUN_00484d20` and its input
   gate in `FUN_00485f50`.
6. **Feedback.** Tier 5, once the gauge's owner is recovered; Tier 6 last.

Tier 4's spawners come along with whichever step first needs them —
`FUN_004af220` is needed as early as step 1.

## 6. Verification gates

- `SimCopter.Water.*` automation covering, at minimum: fill rate and cap in
  pounds; the three-part fill gate; particle life decay and drag constants for
  both types; douse strength as a function of travel time; a bucket drop and a
  cannon drop producing a 4:1 strength ratio on the same flame; a drop landing
  on water producing a splash and *no* douse.
- A determinism check: the same emitter state and the same frame delta must
  produce the same particle path, so the fixed-point integration can be
  compared against the decompile by hand.
- Scenario pass: hover over water and fill; dump onto a fire from three
  different altitudes and confirm the extinguish count differs; dump while
  swinging and confirm the water scatters downwind of the swing; fire the cannon
  at a fire from range.
- The existing `SimCopter.Missions.FireDouse` must keep passing — the douse
  model itself is not changing, only who calls it and with what.
- `SimCopter.Missions.FireDouseAcrossFootprint` must prove that water landing on
  a visible outer flame of a multi-tile building reaches the flame record still
  owned by the building's anchor tile.
- `SimCopter.Formats.MaxisMesh.HelicopterModel` must resolve GEO id `0x7b` as
  `BUCKET` and build a non-empty mesh section.
