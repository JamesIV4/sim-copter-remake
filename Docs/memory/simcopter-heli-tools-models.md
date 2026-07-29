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

Related: [[simcopter-heli-flight-model]], [[simcopter-people-logic-next]],
[[simcopter-water-gameplay]], [[simcopter-fire-water-fx]].
