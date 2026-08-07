# SimCopter ambient vehicles

*"Planes/trains/boats + the plane-crash, train-crash, boat-rescue and train-rescue missions - decoded and ported 2026-07-27"*

*Recorded 2026-07-28; ported into the repo 2026-07-29.*

**Decoded + ported 2026-07-27.** Canonical notes:
repo `Docs/scratchpad/ghidra/planes_trains_boats_decode_20260727.md`. Port:
`Source/SimCopterRemake/{Public,Private}/Ground/SimCopterAmbientVehicles.*`
(`ASimCopterAmbientVehiclesActor`, spawned from the mission actor's BeginPlay).
This closed the last four stub hooks in [[simcopter-mission-system]].

Traps worth keeping:

- **Plane slot 1 is the UFO** (`DAT_00582910[1]`, GEO 0x17c, table name literally `UFO`), not a
  second aircraft. `FUN_004b2910` posts `EVT_UfoResolved` when it starts its dive - that is where
  `[General Miss] UFO Money/Points` is paid. The scheduler has no UFO bucket.
- **Boat slot 0 is `CAPBOAT1`** (GEO 0x163 = capsized boat) and is the boat-rescue boat; slots 1-2
  are ambient `BOAT1` (0x12f). Destroying an *ambient* boat creates a 0x90 rescue; destroying
  CAPBOAT1 kills the mission's people instead.
- **The UFO fight is `FUN_004b3ba0`**, reached from `FUN_00490690`'s `FUN_0049a4f0` call through the
  **class-0x100** arm (`FUN_004b3df0`). Ported 2026-08-06; before that it did not work at all, for
  **three independent reasons** — the ambient meshes are on `NoCollision`, so the Apache's
  `ECC_Visibility` trace passed straight through them; `ResolveImpact` only ever special-cased
  `ASimCopterGroundAgent`, so an aircraft would have counted as **terrain** (and opened a building
  fire in the street below); and `HitCount` was declared, saved, restored, tested at the retirement
  gate and reset on respawn **but never incremented anywhere**. The rule:
  - the switch has arms for **modes 3 (missile) and 7 (machine gun) only** — nothing else in the
    game can touch an aircraft;
  - **object 0x12e (PLANE1, the airliner) goes down in one hit from either weapon**, sets hit count
    1, clears its event id, and posts **event 0x30 = `EVT_CrashPenaltyC`, −100 pts / −$200**;
  - the **UFO** takes `+0x4c = 3` and has the sign bit cleared on every **face-type-11** card in its
    model — its running lights go out, which is the hit tell — and then **only the missile arm**
    reaches `+0x50 += 1` and the `9 < +0x50` retirement. **Machine-gun fire is cosmetic against the
    saucer.** The tenth missile downs it; `BeginPlaneCrash` then pays `EVT_UfoResolved` as it starts
    its dive, which is where the UFO money/points come from.
  The remake asks the ambient actor for a segment-vs-hull test (`FindPlaneHitBySegment`) instead of
  giving the meshes a Visibility responder — **a flying saucer that answered Visibility traces would
  become "the ground" under every downward probe in the game.** The lights-out visual is not ported;
  the missile's own detonation is the feedback. Covered by `SimCopter.Ambient.UfoHitCount`.
- **`Plane Crash($)/(pts)` and `Train Crash($)/(pts)` are dead tuning.** `FUN_004ab170` binds
  0x506008..0x506014 and nothing else in the exe reads them - `FUN_004aabf0` has no branch for
  type bit 0x4 or 0x100. A bare crash mission legitimately pays +0/+0.
- **A plane that ditches on water creates a boat rescue**, not a fire (`FUN_004b2cd0`, terrain
  class < 10), and retires its own record with `EVT_SetCategory` **4**. Category 4 = "expire
  silently": `FUN_004a73e0` deactivates it with no scoring (the port used to leave such records
  active forever).
- **`FUN_004b1950`'s parameters are only readable from the assembly** - Ghidra's decompile drops
  two of the four. Order is (tileX, tileY, eventId, timer).
- Rail tiles = `xbldId | ((xzon & 2) << 14)` in 0x2c-0x2d, 0x32-0x3a, 0x45-0x48, 0x4d-0x4e,
  0x5a-0x5b (+0x8000 variants). Career cities 0-3, 5, 6, 8, 10, 11, 13, 16 have **no rail at all**,
  so test the train in city 12 (522 rail tiles), not city 0.
- Original sound ids come from `FUN_00424b70`'s registration order (each entry writes its id at
  +0x9a): TRAIN1=0x19, CRSH2=0x1a, DIVE1=0x1b, CESSLP1=0x1c, CA_CHING=0x1e. Ambient-vehicle audio
  is **not** ported.
- The original's respawn probe walks only ~7 rings out from a point half the draw distance away,
  which only works because its draw distance was short. The port needs a nearest-usable-tile
  fallback or the vehicle is placed outside the keep-alive radius and dropped again next frame.

In-game driving: `SimStartMission <mask>` and `SimDumpAmbientVehicles` are Exec commands on both
the on-foot pawn and the helicopter pawn; `BugItGo X Y Z Pitch Yaw Roll` teleports the camera to a
position the dump reports. See [[simcopter-ingame-verification]].

## Capsizing, and standing in the water (2026-08-07)

**CAPBOAT1's GEO is modelled the right way up.** The hull sits below its own y 0 with a short mast
and a life ring above it (`Docs/scratchpad/render_maxis_object.py CAPBOAT1`), so the "cap" in the
name is a rotation the renderer applies, not something baked into the mesh - the boat-rescue hull
has to be rolled **180 degrees** about its keel line or it floats upright like an ambient BOAT1.
Every hull placement now goes through `SetBoatMeshTransform`, not `SetMeshTransform`: the mission
activation, the save restore and the per-tick update all set a boat's transform, and the roll used
to be missable at two of the three.

**A person on a water tile is IN the water.** `ASimCopterGroundAgent::UpdateWaterSubmersion` (and
the same routine on `ASimCopterOnFootPawn`) lerps the figure down over 0.25 s until the surface cuts
it at the waist - half a capsule height - and then adds `GetWaterWaveOffsetCm` so it heaves on the
same swell the boats do. Three things to know:

- It is **visual only**: it moves `VisualRoot` / the sprite component, never the capsule. Every
  height gate the sim measures against a person is a small decoded number (the 37.5 cm alight
  clearance, the 50 cm boarding band, the medic's contact box - see [[simcopter-paramedic-handoffs]])
  and sinking the collision body half a body-length would quietly break all of them.
- "On a water tile" is not enough - a bridge deck, a pier and a hover are all over water. The test
  is `IsWaterTerrainClass` **plus** feet within `WaterStandingClearanceCm` (40 cm) of the sea's rest
  plane, which clears the swell's crest but not a deck a terrain step up.
- `UpdateBoatRiders` therefore places the survivors on the **rest plane** now. It used to add the
  wave itself; leaving that in would heave them twice as far as the water they are floating in.

Because several places write `VisualRoot`'s relative location (poses, the carried-body layout, the
walk bob), the water offset is added by `SetVisualRootRelativeLocation`, which remembers the base
the animation asked for. Writing `VisualRoot->SetRelativeLocation` directly re-introduces the bug.
