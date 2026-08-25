# SimCopter passenger-mission failure rules

*Can a rescue/transport mission fail while its people are in your chopper? Recorded 2026-08-25,
then **substantially corrected the same day** after re-verifying against the disassembly and the
shipped BHAV data. The first version of this note got the headline right and the two details that
actually matter wrong — read the corrections before citing it.*

## 1. The mission walker has no timeout. That is not the whole story.

`FUN_004a73e0` never closes a rescue/transport/medevac record on elapsed time. Verified four ways:

- The walker's only expiry arms are category 2 + traffic-jam mask `0x800` at `TimeAccum > 0x5a0000`
  (90 s in 16.16), and category 4 "retire silently".
- Creator `FUN_004a9a10`: the only non-zero category write in the function is `param_1 == 0x800`
  → 2. Passenger records are category 0. **Nothing in the exports writes category 4 or 8.**
- Event sink `FUN_004a89c0`: no category writes, no deactivation.
- Scheduler `FUN_004a6e60`: no eviction; at the cap it just stops spawning. `DAT_00505fc8` (the
  difficulty-scaled mission timer) feeds only `DAT_0057f998 = timer >> 3` and the boat/train rescue
  vehicle lifetimes (`FUN_004b1aa0` / `FUN_004b7fb0`).

Completion is full resolution and nothing else: rescue `delivered(+0x98) + casualties(+0xb4) ==
victims(+0x8c)`; transport `delivered(+0x9c) + casualties + lost(+0xbc) == passengers(+0x88)`;
medevac `delivered(+0xa0) + casualties == victims(+0x84)`. A person in the cabin is *picked up*,
never *resolved*.

**But "no timeout in the walker" is not "transports cannot fail on a clock" — see §3.** The
original version of this note drew exactly that wrong conclusion.

## 2. The nag STOPS once everybody is aboard

This is the correction that matters most, and the decompile's `else if` hides it. From the
disassembly:

```
004a7650 TEST byte ptr [ESI + 0x50],0x10   ; rescue
004a7656 MOV  EAX,[ESI + 0xa4]             ; pickedUp
004a765c ADD  EAX,[ESI + 0xb4]             ; + casualties
004a7662 CMP  EAX,[ESI + 0x8c]             ; vs victims
004a7668 JNZ  0x004a767a                   ; somebody still waiting -> nag check
004a766a MOV  [ESI + 0x28],-1              ; clear marker X
004a7671 MOV  [ESI + 0x2c],-1              ; clear marker Y
004a7678 JMP  0x004a76ab                   ; *** SKIP THE NAG ENTIRELY ***
```

Transport is the same shape at `004a7801`, with the unconditional `JMP` at `004a7829` and
`pickedUp + casualties + lost` vs `+0x88`.

So the nag gate is **pickedUp**, while completion is **delivered** — two different fields. Once the
last person is aboard the marker clears and the record goes silent. It stays open, costing nothing,
until you deliver.

| | nobody picked up | some waiting, some aboard | all aboard |
|---|---|---|---|
| Rescue `0x10` | nag, −10 / 75 s | nag, −10 / 75 s | silent, marker cleared |
| Transport `0x40` | nag, −10 / 75 s | nag, −10 / 75 s | silent, marker cleared |
| MedEvac `0x20` | silent | silent | silent, marker cleared |

**MedEvac has no nag arm at all** (mask `0x20` at `004a7611` never reads `0057f998` and never calls
`FUN_004a89c0`). The first version of this note claimed it nagged like the others. It does not.

Nag codes dock **points only** — every nag calls `FUN_00407b00` (career+0x50) and never
`FUN_00407a90` (career+0x40, money). The claim that nags also dock cash was wrong.

Record base is `DAT_0057f9dc`, 30 × 0xd4 — not `DAT_0057f990`.

## 3. Transport DOES have a give-up timer. It lives in the BHAV, not the walker.

`+0xbc` "passengers lost" is posted by event `0x1f`. Exactly one function in the executable posts
it — `FUN_004ccf50` with outcome code `0xb` — and across all 137 shipped programs exactly one site
uses that outcome:

```
BHAV 290 'Transport increment boredom, possibly disappear'
  [0] l1 := rand(5)           -> 5
  [5] l1 == 0                 -> 3   (else return; 1-in-5 per pass)
  [3] l0 := difficulty tier   -> 6
  [6] attr35 += 1             -> 2
  [2] attr35 += l0            -> 1
  [1] attr35 > 100            -> 4   (else return)
  [4] post mission outcome 11 -> 7   == EVT_PassengerLost
  [7] deactivate person
```

Called from `BHAV 291 'Transport go to avatar/get on heli'` rec[8], inside its wave → `Idle-5` →
`CALL 290` idle loop. **A waiting fare gets bored and walks off.** When every fare has resolved,
`delivered + casualties + lost == passengers` is satisfied, the walker completes the record, and
with nothing delivered `FUN_004aabf0` computes a negative award and plays failure voice `0x60`.

That is the transport timeout players remember. It is real; it simply runs through the people
system instead of the mission system. Rough pacing from the tick budget (boredom >100, `1 + tier`
per hit, 1-in-5, one roll per 5-tick `Idle-5` at the VM's 0.08 s tick): **~1.7 min at tier 1, ~42 s
at tier 4**. Estimate, not measured.

Per type:

- **Transport** — the only type with a give-up. BHAV 290 above.
- **Rescue** (BHAV 303/305/700/1498) posts only outcomes 0 and 1. No give-up; a victim waits
  indefinitely.
- **MedEvac** — BHAV 280 rec[14] calls `BHAV 312 'Die without falling first'` (outcome 10). The
  patient fails by *dying*, not by leaving.

**Boredom only runs while WAITING.** op12 returning true is what makes 291 return, which sends
BHAV 750 to `BHAV 292 'Transport wait to get off'` — a program with no boredom roll. So in retail a
fare in your cabin can never be lost. The property holds through program structure, not through the
absence of a clock.

## 4. Remake status — both bugs FIXED 2026-08-25

- **The nag was keyed on the wrong field.** `FSimCopterMissionSystem::UpdateRecords` gated both the
  nag and the marker clear on `RescueDelivered`/`TransportDelivered` where retail uses
  `VictimsPickedUp` — which was tracked and never read — so carrying survivors cost 10 points every
  75 s and left the marker burning. Each passenger type now runs the two tests separately, the way
  the walker does. **Transport clears the TERTIARY pair (+0x38/+0x3c), not the primary one**
  (`004a781b`); rescue and medevac clear the primary pair (`004a766a`, `004a762a`). MedEvac's
  marker clear was missing entirely and is now present; it still has no nag.
- **A boarded fare could re-enter the boredom loop, and this was the originally reported bug** —
  "a passenger gave up and got out in mid-flight". `PickUpMissionPeopleNear`
  (`SimCopterTrafficSystemActor.cpp`) seats people by calling `BoardCarrier` directly, and
  `BoardCarrier` claims the seat / hides / suspends movement but **never advances the behaviour
  program**. The fare stayed parked in BHAV 291's wave/`Idle-5` arm, which calls BHAV 290 every
  cycle. Its walk could never complete either: movement is suspended and the seat transform owns the
  position, while op12's height gate measures a *seated* body against the airframe underside across
  a 5-unit (~31 cm) window.
  **Fix:** `ASimCopterGroundAgent::StepTowardSelectedObject` now reports `Arrived` immediately when
  the selected object already *is* this person's carrier and the harness flag matches — mirroring
  `BoardCarrier`'s own early-out. op12 then calls `BoardSelection`, gets the existing seat back, and
  the program advances exactly as a walked-in boarding would: 291 returns, 750 posts the pickup and
  hands off to 292. **The harness half of the condition is load-bearing** — a cabin passenger whose
  program selects the rope end must NOT short-circuit, or the seat is torn down and rebuilt every
  tick (the doropn cue trap in [[simcopter-paramedic-handoffs]]).
  Deliberately fixed at the *program* end, not by restructuring boarding: `BoardCarrier` /
  `PickUpMissionPeopleNear` are shared by harness pickups, medevac, carried bodies and the on-foot
  pawn, and all of that works.
- Note this stalled-program state is probably common to every mission passenger type and is
  harmless everywhere else, because **transport is the only type with a boredom roll** — rescue and
  medevac would just loop. That is why only transports showed it.
- `SimCopter.Missions.PassengersAboardNeverFailMission` was rewritten: it had *failed* on the marker
  assertion while its passing assertions pinned the divergence as correct. It now pins the real
  contract (aboard = silent and free; waiting = −10 a period; medevac never nags) and passes.
  **236/237 of `SimCopter.*` pass**; the lone failure is `Formats.SimCity2000.ReferenceCity`, which
  fails identically with these changes stashed (pre-existing, ALTM sample expectations vs the
  reference city on disk). Not verified in a running game.

## 5. Traps hit on the way

- The Reference `SimCopter.exe` is a Detours-patched multilingual build: the `.rsrc` directory root
  is zeroed even though the RT_STRING blobs survive, so anything walking the resource directory
  finds nothing. Recover strings by scanning `.rsrc` linearly for count-prefixed UTF-16 entries and
  anchoring on a known pair (`'Sim Rescued!'` = 0x3a7 aligns 0x3a2..0x3c1). Script:
  `Docs/scratchpad/scan_stringtable.py`.
- **Read the disassembly for this walker.** Ghidra renders the pickedUp test and the nag as
  `if / else if`, which is correct but easy to read as two independent `if`s — and that misreading
  is exactly what produced the wrong version of this note. `dump-asm` makes the `JMP` unambiguous.
- Answering "does X time out?" from the mission layer alone is not enough. The people VM owns
  give-up behaviour, and `Docs/scratchpad/agent-sessions/2026-07-29-people-vm-opcodes/dump_bhav.py`
  plus `scan_op13.py` answer it in seconds.

Related: [[simcopter-mission-system]], [[simcopter-pacing-divergences]],
[[simcopter-people-logic-next]], [[simcopter-paramedic-handoffs]], [[simcopter-passenger-display]].
