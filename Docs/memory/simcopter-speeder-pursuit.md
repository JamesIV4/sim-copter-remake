# SimCopter burglar getaway-car pursuit

*The searchlight is the CARROBBR-pursuit mechanic: police cannot stop an unlit getaway car; decoded and ported 2026-07-26, Speeder identity completed 2026-08-09.*

*Recorded 2026-07-26; ported into the repo 2026-07-29.*

**Correction, 2026-08-09:** mask `0x4000` is the retail **Burglar** mission, not the Speeder.
Its deployed arguments are behavior class `0xf`, person state `0xd`; state 13 selects BHAV 1303,
which commits a robbery and uses **opcode 61** to message the exact starting CARROBBR car. A
successful return restarts the driving/burglary cycle. The 120-second path is capture only when no
nonzero return message arrives.

Retail nevertheless has a distinct **ambient Speeder encounter**. `FUN_0049af00` and
`FUN_0049af70` set vehicle flag `0x800` on an already-live ordinary car; no mission record or
CARROBBR is created. `FUN_004a6e60` maintains that designation outside the mission scheduler.
`FUN_0049be50` posts `0x21` “Waiting For Cops!” every ten seconds while the stopped car has no
police nearby, then `0x26` “Speeder Caught!” when police reach it, both with event id `-1`.
See [the complete alignment note](simcopter-crime-rooftop-rescue.md).

Criminal cars and police pursuit, decoded and ported 2026-07-26. Notes in repo
`Docs/scratchpad/ghidra/criminal_car_and_pursuit_decode_20260725.md`; rules in
`Public/Ground/SimCopterCriminalCar.h`; tests `SimCopter.Crime.*`.

**The mechanic, which is not obvious from any one function:** `obj[0x11b]` on a vehicle is an
*illumination accumulator*, not a wanted flag. Interaction mode **1** is the searchlight
(`FUN_0049f680` case 1 -> `FUN_004a01f0`): +2 per tick to a cap of 10 while the object is inside
the band radius (48/72/96 units for bands 0/1/2, horizontal distance only), and **zeroed outright
when the light goes off**. It does two things: `FUN_0049d980` drops a fleeing car from 1.75x
speed to 1.05/1.32/1.52x, and `FUN_004b89a0` lets a police car force it to stop. **An unmarked
getaway car cannot be stopped by police at all.**

Traps:
- `FUN_004b89a0` (criminal car vtable[1]) is **not in the Ghidra export set** - it is folded into
  the tail of `FUN_004b8630`. Read `.rdata` at the vtable address and disassemble by hand.
  Vtables: criminal car `0x004f4cd8`, ambulance `0x004f4d20`, fire truck `0x004f4d48`, police
  `0x004f4db0`; the three services share `FUN_0049e0c0` as vtable[1].
- Ghidra renders the pull-over argument as `0xffffffff`; that is a *self-halt* call site. The
  real one is at `0x004b9f99` and pushes `[EDI+0x14]` = the police car's message id `0x11d`.
  Getting this wrong makes the police branch unreachable.
- `FUN_0049df60` in the pursuit is asked of the **target** ("may that car stop there"), not of
  the police car.
- The proximity gate on the on-scene path is `FUN_0049b000 < 3` (octile *tile steps*), not the
  `0x600000` world distance - that one belongs to the chase path.
- **The stop has two placement outcomes and they are easy to swap.** `FUN_004b8b60` posts
  `EVT_SetCategory` value 4 (`CAT_ExpireSilently`) *only when* `FUN_0049bd00(0xf, 0xd)` returns 0
  - nobody could be placed, so the record is retired with **no payout**. On success it posts
  nothing and state 3 waits for the burglar's BHAV.
- The successful deployment runs BHAV 1303 -> 1079. Record 9 is opcode 61 with argument 1, which
  reaches `FUN_0049aed0` and writes the return signal into `veh[8]` / `veh[0xc]`; record 10 is
  opcode 40. A nonzero message restarts state 0 with the next delay already drawn when the prior
  cruise expired. Only no return
  within 120 s reaches `FUN_004b8c90`'s `EVT_CriminalCaught` path.
- `FUN_004b8b60` and the nonzero-return arm of `FUN_004b8c90` both advance through `aDrOpen`
  (`0x6f`) and `aDrClose` (`0x70`) only after `SoundIsPlaying` reports each clip finished. The
  aligned port uses those same sound-slot gates rather than collapsing the transition.
- **The remake's traffic pass resets every vehicle's `TrafficSpeedScale` to 1.0 each frame**
  (`UpdateTrafficInteractions`). A stopped car must be pinned to 0 *every frame*, or it drives
  away while the burglar is still outside.

**Road speeds are authored, and the remake had them ~3x too fast.** `FUN_0049dbb0` gives every
vehicle its own speed at placement: `veh[0xc3] = (rand()&7)+0x24` (36..43) and
`veh[0xc7] = (rand()&7)+0x28` (40..47), in **original units per second** (`FUN_0049be50`
advances by `speed * frameDelta`). At a 400 cm tile that is 225..294 cm/s; the remake's
`VehicleSpeedCmPerSec` was 720 (115 u/s). It only became visible with speeders, because
1.75 x 115 = 201 u/s beat the helicopter's own 192 u/s ceiling (`MaxPitch` doubles as airspeed),
so nothing could follow one. Pinned by `SimCopter.Crime.SpeederSpeed`.

`TargetCount` (record `+0x94`) is set to 1 when the burglar mission is created
(`FUN_004a7a10`'s 0x4000 branch), and the port preserves that metadata. The retail lifecycle uses
a distinct burglar test, however: it stays open exactly while both `CriminalsCaught == 0` and
`Casualties == 0`; it does not compare this branch against `TargetCount`. Pinned by
`SimCopter.Missions.BurglarCarStaysOpen`.

Pool is five (`FUN_00479bb0` preallocates in one run of 5). Body is GEO `0x11e` = CARROBBR
"badguy" in `GEO\SIM3D2.MAX`. See [[simcopter-emergency-dispatch]] for the vehicle pool and
states, [[simcopter-heli-tools-models]] for the spotlight bands.
