# SimCopter emergency dispatch

*SimCopter F2-F5 emergency dispatch decoded 2026-07-25 - where the evidence lives and the traps that cost time*

*Recorded 2026-07-25; ported into the repo 2026-07-29.*

Emergency dispatch (F2 fire, F3 ambulance, F4 police, F5 police-follows-spotlight; Shift+key
releases) was decoded and ported on 2026-07-25. Canonical note:
`Docs/scratchpad/ghidra/emergency_dispatch_decode_20260725.md`. Port:
`Source/SimCopterRemake/{Public,Private}/Ground/SimCopterDispatch.*` plus the dispatch runtime
inside `SimCopterTrafficSystemActor.cpp`.

**Why:** these traps are not visible in the decompiled C and each one silently produces
plausible-but-wrong behaviour.

**How to apply:**

- `FUN_004bc680` (the dispatch transaction) is `__thiscall` and **Ghidra mis-types it** - the
  decompiled argument list is scrambled and mixes two different out-parameters. Read
  `out_dispatch_alloc_asm_20260725.txt` instead. Real shape:
  `(manager, tileX, tileY, serviceType, initialState, vehArray, poolSize=5, int* out)`.
- The vehicle's "message id" at `veh+0x14` (`0x11c`/`0x11d`/`0x11f`) is **also its body's GEO
  object id** - `FUN_0049dbb0` calls `FUN_00470571(veh[0x14])`. Tables: `CARFIRET`, `CARPOLIC`,
  `CARAMBUL`, and `0x11e` `CARROBBR` "badguy" = the criminal car. The `0x121`/`0x122`/`0x123`
  objects the service constructors also load are `AICON`/`PICON`/`FICON`, the **dispatch pylon
  icons** on a hidden second node at `veh+0x13b`, NOT the bodies.
- `FUN_0042de60(1)` is not a tutorial gate: it reads `DAT_0051a078` = **Shift**. That is what
  turns F2-F5 into the clear-dispatch path.
- Service types 3 and 4 are both police and share one pool/manager; the argument that looks
  like a count (4 or 3) is the **initial vehicle state** at `veh+0x299`. State 3 = chase.
- The remake-only cockpit dispatch strip exposes chase as a fourth selector entry,
  `POLICE (CHASE)`, but it must resolve back to the real `Police` service with state 3. Do not
  extend `EService`: the adjacent `CLEAR` button uses the same Shift+F clear path, so both police
  entries release from the one shared police pool.
- `FUN_004bcc80` records stations at `(x+1, y+1)` because it clears a **3x3 footprint** around
  the matched corner - stations are 3x3 buildings and the record is the centre.
- Fire trucks have no extinguish call: they spawn **emitter type 6** water at the fire and the
  ordinary water-impact path does the dousing.
- **Ambulances do not use the criminal-car `(0x0f, 0x0d)` spawn.** Their vtable on-scene
  method is `FUN_004b8f60`, which deploys behavior class `0x0c`, person state `5`. That state
  runs BHAV 801/262, searches for a state-6 victim, totes them to behavior object class 10
  (the ambulance pool), and returns to the exact vehicle stored at `person+0x170`. See
  [[simcopter-paramedic-handoffs]].

Resolve GEO ids to names with the object-header id at `+0x78` (see
[[simcopter-ghidra-workflow]] for the bridge; the mesh reader is
`Private/Formats/MaxisMeshReader.cpp`).
