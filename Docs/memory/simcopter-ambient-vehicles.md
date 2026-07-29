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
