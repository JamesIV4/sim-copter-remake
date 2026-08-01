# SimCopter heli tools models

*Helicopter tools/models decode (2026-07-24) - traps to re-check before trusting any tool or registry claim.*

*Recorded 2026-07-24; ported into the repo 2026-07-29.*

Canonical notes live in the repo: `Docs/scratchpad/ghidra/heli_tools_models_decode_20260724.md`
(hashes, tables, open items) and `Docs/HelicopterToolsAndModelsDecompilePlan.md`
(phase status table at the top). Only the traps are kept here.

**Traps that cost time and are easy to get backwards:**

- Tear gas people reaction is **interaction mode 5** (BHAV 907), delivered by the
  30 s gas cloud in `FUN_0048ed00` - NOT mode 7. Mode 7 = Apache machine gun,
  mode 3 = Apache missile, mode 0xe = the canister's physical hit. Emitter pool
  class flags are written once by `FUN_0048da50` and survive impact, which is why
  the spawn function `FUN_0048e0b0` never sets them.
- **Tear gas ported 2026-07-31** (`SimCopterTearGas.h` + `USimCopterTearGasPoolComponent`).
  The muzzle - the old open item, the consumer of `heli[0x57]` - is the tail of
  `FUN_00484d20`: launch point is the body node **+3.0 units up**, direction is the
  airframe's own forward axis (`DAT_004fa2e0` is `(0,0,1)` through the body matrix),
  speed is `heli[0x4e] + 50.0`. The launcher does **not** use the spotlight's aim.
  Two corrections to the table above: type 3 calls `FUN_0046cad1` (identity), not the
  orienting `FUN_00467d30`, so the canister's node never rotates; and `node+0x10 =
  0x30000` from `FUN_0048db20` is its **collision radius**, not a render scale.
  Traps: the burst zeroes the effect timer, so the pop and the first puff of gas land
  on the same frame; class flag `0x8` is in the reflect set `0x798` and *not* in the
  despawn set `0x4006`, so a canister bounces off walls and passes through people; and
  the drag `0x28f` is per **frame**, so the pool has to run on the 0.05 s clock like
  the water particles do. What ends a riot is BHAV 907 walking a rioter's agitation
  (`+0x150`) down two per puff until BHAV 311 retires them below three - all data, so
  the one thing that was missing was anything delivering mode 5 at all.
- `heli[0x70]`/`heli[0x71]` are **stowed** flags (1 = raised). `heli[0x6f]` counts
  **down** from 0x11 to 3 as rope pays out. Commands are +/-1 bucket, +/-2 harness.
  Proof: the assert string "bucket not raised - can ignore" in `FUN_004cccd0`.
- `FUN_00489250` is the **spotlight target service**, not a rotor downwash disc
  (the old label in `out_effects_DECODED.md` was wrong and is now corrected).
- Runtime helicopter type order != `heli.twk` section order != shop order. The
  shop permutation is `{4,0,1,8,3,5,6,7}` (`FUN_0042d840`); Apache (type 2) is
  never sold.
- Behaviour-VM opcode numbers are NOT the thunk-table order. Compute them as
  `(slotAddr - 0x58ef78) / 4` from `FUN_004c3010`'s assignments.
- **Megaphone traffic resolution is a vehicle interaction, not a mission hotkey.**
  `FUN_0049a4f0` routes cars to `FUN_0049fc10` -> `FUN_0049f680`; mode 2 calls
  `FUN_0049d7e0`, where message 0 clears that car's jam flag `0x200` and posts
  `EVT_CarCleared` (`0x1b`) for the car's event. The five-ring scan therefore
  clears only jammed cars around the spotlight target. `FUN_00424620` also owns
  one wrapping voice cursor per message, so `MG_00_*` through `MG_04_*` must not
  be pooled and randomized together.
- **The original starts with both the bucket and megaphone.** `FUN_004080c0`
  (new career) and `FUN_00407f30` (new user game) both write `0x03` to career
  equipment `+0x48`. That single career mask is displayed on every owned
  helicopter; it is not a per-model loadout. `FUN_0044ac80` also makes each
  F6-F10 message a synchronous select-and-broadcast command, so a cockpit popup
  choice must invoke the action immediately rather than pulse a frame latch.

- **Every tool fires from one muzzle, and the pivot-relative version is a trap.** `FUN_00484d20`
  launches all four emitters from the body node lifted 3.0 units. In the remake `ModelPivot` is the
  *capsule* centre with the fuselage pushed down beneath it, so that point lands up by the rotor
  mast - which is where tear gas canisters looked like they were coming from. One shared
  `ResolveToolMuzzle` now serves cannon/gas/missile/gun: CANNON's barrel tip, else the fuselage
  nose off the body mesh's bounds.
- **BRACKET (0x16c, `heli[0x31]`) is the harness's mount** and was unported. `FUN_00483c20` builds
  it for every helicopter and nothing in `.text` writes `heli + 0xc4` again, so like CANNON it is
  authored in the fuselage frame and rides the body - a triangular frame on the **right flank**,
  the side a winched Sim comes aboard. **Only the harness** hangs off it - the bucket keeps the
  belly point, because it is lowered straight down to scoop and to douse - and the anchor is
  the frame's **outboard tip**, standing off by one cable radius so the rope's outer edge lands
  on the tip rather than half the rope hanging past it. BRACKET is one GEO shared by all nine
  airframes, so its own tip is only wide enough for the small ones: **sample the BODY's own
  half-width at the bracket's height too** and take whichever is further out, or the cable runs
  through the hull on a Bell 212 or a Dauphin.
- **Full-rope camera framing is an overlay, not a player-zoom write.** In chase/view 1 only, when
  the player's zoom is already at its closest and the bucket or harness reaches the fully lowered
  node, the camera lerps an extra 0.10 zoom alpha out. Raising even one node lerps that overlay
  back to zero, so the exact zoom chosen by the player is preserved.
- **Apache armament ported 2026-08-01** (`USimCopterApachePoolComponent`). Missile: 10 slots, GEO
  0x0ae, `heli[0x4e] + 450`, cooldown shared with tear gas, mode 3, impact column scale 2 + sound
  7. Gun: 70 slots, a 3-point 0x17 card cycling palette 0x10..0x1f, `+600`, NO cooldown, mode 7,
  scale-1 column + sound 0x10 (or 8 into a body). **Both fly at constant speed with no drag and no
  gravity** - only the tear gas and debris pools arc - and both despawn on first contact (the
  0x4006 set). The 70 tracer visuals are rebuilt directly from those same live slots as three
  camera-facing 2x2-pixel points; do not clone them into the generic gravity-driven trajectory
  pool, which made copies stick at the helicopter or vanish. The visible impact now has a spherical
  mission-effect area: 24 original units / 150 cm for a missile and 8 units / 50 cm for a gun
  strike. Every car in that area can become the exact target of a car-fire record, and every person
  can become the exact state-6 victim of a player-caused medevac (reporting a casualty to any old
  mission first). The original direct mode-3/mode-7 reaction remains the fallback for the actor the
  projectile trace actually struck. Building/terrain fire remains missile-only and retains the
  existing direct-impact path; the sphere only broadens car/person mission selection.

Related: [[simcopter-heli-flight-model]], [[simcopter-people-logic-next]],
[[simcopter-water-gameplay]], [[simcopter-fire-water-fx]], [[simcopter-helicopter-collision]].
