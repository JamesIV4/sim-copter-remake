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

Related: [[simcopter-heli-flight-model]], [[simcopter-people-logic-next]],
[[simcopter-water-gameplay]], [[simcopter-fire-water-fx]].
