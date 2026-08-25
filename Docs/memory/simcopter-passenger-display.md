# SimCopter passenger state display and person heads

*The seat window, the head every person wears, and the medevac EKG â€” decoded and ported 2026-07-31*

## The seat manifest

The helicopter carries a manifest at `DAT_005040d0+0x1d4`, built by `FUN_0048bf60`:

| offset | field |
| --- | --- |
| +0x00 | dirty flag (`FUN_0048bf30` reads, `FUN_0048bf40` sets, `FUN_0048bf50` clears) |
| +0x04 | seat capacity, from the model registry at `&DAT_005040e4 + type*0x5c` |
| +0x08 | seats occupied â€” `FUN_0048c1e0` / `FUN_0048bfe0` are both `[+4] - [+8]`, i.e. free seats |
| +0x14 | seats per row, **fixed at 5** |
| +0x1c | 16 records, stride 0x14 |

Record = five ints: **`+0x00` head image** (copied from `person+0x18e`), **`+0x04` face row**,
`+0x08` flags (always 0x200, never read back), `+0x0c` person id, `+0x10` display slot.

- `FUN_0048bff0` adds â€” refuses when capacity == occupied, fills the first free record, then
  repacks every record's display slot.
- `FUN_0048c120` removes by person id and repacks the same way.
- `FUN_0048c0c0` finds a record by person id; **`FUN_0048c0e0` writes `+0x04`** and is the whole
  body of people opcode 54 (`FUN_004ccb40`, which then marks the window dirty).
- `FUN_004c6250` is boarding: `{head = person+0x18e, face = 1, 0x200, person+0x12e}`.
  `FUN_004c62e0` is alighting.

## The blit â€” FUN_00453f70

The seat window widget is `FUN_00453840` (`seatwin2.bmp` + `people1.bmp`, vtable `0x004f2f78`);
`FUN_00453cb0` redraws it every fourth frame while the dirty flag is set.

```
clear:    every seat gets people1 cell (0, 0) â€” the empty chair
occupied: src x0 = (record[0] * 3 + 3) * 9   => column = head + 1, width 27
          src y0 =  record[1] * 0x21         => row    = the face,  height 33
          dst    = ((slot % 5) * 0x20 + 0x0e, (slot / 5) * 0x23 + 0x0a)
```

So the portrait **column is the passenger's own head** and the **row is the face opcode 54 set**.
Three rows of five at that pitch fill the 186x115 page exactly (10 + 3*35 = 115) and centre the
block in the printed well; the remake's earlier measured-by-eye 29/34 stride sat left of centre.
A record whose person id matches the one being dragged draws the empty cell instead â€” that is how
a portrait on the cursor leaves its seat looking vacant.

## Heads: FUN_004c71c0 and the bandage

`FUN_004c71c0` binds a **fixed head per behavior class** into `person+0x18e`, and only when that
field is still -1:

| class | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 |
| --- | - | - | - | - | - | - | - | - | - | - | -- | -- | -- | -- | -- | -- | -- | -- | -- | -- | -- | -- |
| head | 4 | 8 | 6 | 7 | 5 | 7 | 7 | 7 | 5 | 5 | 6 | 9 | 3 | 2 | 1 | 5 | 4 | 4 | 7 | 0 | 7 | 5 |

**No class claims head 10.** Its only writer is `FUN_004c7090`, the state setter: *state 6 â€” the
medevac victim â€” always gets head 10*, the bandaged one. The head table itself is `DAT_0058f0e0`
(filled by the privanim loader `FUN_004ceab0`): SIM3D.BMP image ids
`{4, 5, 0x2c, 0x2d, 0x2e, 0x41, 0x2f, 0x42, 0x30, 0x31, 0x43}`, so head 10 is image **0x43**.
`FUN_004c7f10` pushes it into the bound BODC figure node's `+0x30` on every draw.

**This was the remake's bug.** `BuildPedestrianFigure` picked the head with
`(hash / 3) % 11`, so roughly one pedestrian in eleven wore the casualty bandage while actual
medevac victims wore whatever came up. The seat window compounded it by inventing a face from
`EventId` and choosing the row from the passenger *kind*.

`FUN_004c71c0` also writes `person+0x178` (voice pitch offset: 500/400/700/0/-700/-200/-300/-100/
300/900/-500/-300/-300/-900/-1000/-8000/1000 by class) and `person+0x18c` (the person's own
looping voice event: 0x0e/0x28/0x29 footsteps, or one of the eight Elvis noises 0x2f..0x36 â€” always
for Nessie and Elvis, 1 in 200 for the dog and cow, and 1 in `DAT_0058dc3a` = 65000 for anyone).

## BHAV 264 'Face vs. speed/health'

The only writer of the face, called from 280 (medevac), 292 (transport), 1052 (cop ride):

```
[0] am I riding the player?  no -> idle and return
[8] head == 10 ?   yes -> [9]                       no -> [1] speed branch
[9] written off (attr15) ?  yes -> face 2           no -> [10]
[10] health < 1  -> face 2      [11] health < 50 -> face 1     else face 0
[1] op55 -> local0
[3] local0 > 250 -> face 2      [4] local0 > 125 -> face 0     else face 1
```

Two traps:

- **The speed edges are not monotonic.** `> 125` goes to face **0** and everything slower to face
  **1**; only `> 250` reaches face 2. A passenger sits at the middle face while you crawl.
- **Opcode 55 is not the airspeed.** `FUN_004ccb80` is
  `(heli[0x4e] >> 16) * MaxDamage / max(heli[0x34], 1)`, where `heli[0x34]` (byte offset 0xd0) is
  the machine's remaining **hit points** and MaxDamage the model's full complement
  (registry `+0x48`, the same field `FUN_0048a530` reads for the damage gauge in `FUN_00452f50`).
  The ratio is 1 in a pristine helicopter and grows as it is beaten up, so a wreck frightens its
  passengers at a much lower real speed. The result is clamped to 65535 and stored as the low 16
  bits of a signed value, so flying backwards wraps large and shows face 2.

Attribute numbering, for the record: attr 15 = `+0x15e` written-off, attr 28 = `+0x178` voice
pitch, attr 34 = `+0x184` medevac health (seeded 100), attr 38 = `+0x18c` voice set, attr 39 =
`+0x18e` head image.

## The EKG â€” FUN_004c5210, people opcodes 57 and 85

Opcode 57 (`FUN_004ccca0`) forwards the record's four args straight into `FUN_004c5210`
`(event, allocateSlot, nonPositional, force)`; opcode 85 (`FUN_004cc110`) is the same function
called as `(-1, 1, 1, 1)` â€” **stop talking and give the slot back**, not "ambient audio".

Gate: play when the sound is 2D, when forced, when the person is riding the player's cabin, or
when `DAT_00503aa0 == 3` â€” the mode the game enters when you **step out and walk the streets**
(`FUN_00484d20` sets it beside `FUN_004c0b10`, which places the player's own person).

Each speaker borrows one of the fourteen bank slots (`person+0x172`, sound id `+0x71`). If the
requested event is already loaded and playing **and** matches the person's own voice event
(`+0x18c`), the handler does not restart it â€” it calls the buffer's SetFrequency (vtable +0x68)
with an **absolute** rate:

- event 0x3a (EKG): `(health * 4 + 0x78) * 0x19` â€” **13000 Hz at full health, 3000 Hz at zero**;
- everything else: `(movespeed * 4 + 0x54) * 0x7d`, which paces footsteps to the walker.

**BHAV 800 rec[4] is `attr38 := 58`**, which is what makes the EKG a medevac victim's own voice
event and therefore what enables that re-tune. BHAV 302 'Medevac play sounds' then runs every
pass of BHAV 280:

```
[10] not written off -> [1] local0 := health -> /4 -> +2 -> rand(local0)
[5]  roll == 0 -> [2] sound 13 (achdie, 3D)          -> [11] Idle-10
[8]  riding the player ? [7] sound 58 (EKG, 2D, loops) : [9] opcode 85 (silence)
```

So the moan gets more frequent as health falls (1 in `health/4 + 2` per pass), the beep loops in
the cockpit and slows as they fade, and both stop the instant the patient leaves the helicopter.

## What the port does

- `FSimCopterPeopleCityRules::GetHeadImageIndexForBehaviorClass` / `MedevacVictimHeadImageIndex` /
  `GetVoicePitchDeltaForBehaviorClass` / `ChooseVoiceSetForBehaviorClass`.
- `FSimCopterPersonContext::ResetToState` carries `FUN_004c7090`'s state-6 head write, and
  `ASimCopterGroundAgent::RefreshHeadImageIndex` re-skins the figure's head texture when attribute
  39 moves (a swoon through opcode 35 does that mid-life). Only the texture parameter changes â€” no
  clip rebuild.
- `FSimCopterMissionPassengerSlot` gained `HeadImageIndex`, `PortraitState` and the passenger
  actor, so it is the original's record. `AddMissionPassengersForMission` /
  `RemoveMissionPassengersForMission` take the person; `SetMissionPassengerPortraitState` is
  `FUN_0048c0e0`.
- Opcodes 57 and 85 were successful no-ops and are now real, through
  `USimCopterAudioSubsystem::PlayVoiceEvent` (which gained 2D/loop) and the new `SetFrequencyHz`.

Two deliberate divergences, both at their call sites:

- **The seat window refreshes on change, not on a dirty flag.** The original re-blits two cells
  every fourth frame regardless; Slate rebuilds a widget tree, so the refresh is gated on the face
  actually moving.
- **A finished voice slot is handed straight back** instead of `FUN_004c5120`'s recycle-the-oldest.
  Fourteen slots for a whole city means one has to be released promptly either way.

Not reproduced: the player-avatar Elvis-voice easter egg in `FUN_004c71c0`'s
`person+0x12e == 32000 && shift` arm.

## Passenger portrait chroma key and scaling (2026-08-08)

`PEOPLE1.BMP` is an 8-bit 324x99 sheet of twelve 27x33 columns and three face rows. Palette
index 254 is cyan `(0,255,255)` and is the transparent chroma key. Setting only its alpha to zero
is insufficient for filtered Slate textures: the hidden cyan RGB is interpolated into neighboring
opaque pixels and appears as a teal outline around the head and shoulders.

The bitmap reader now replaces a keyed palette entry with transparent black, discarding both its
alpha and RGB. Passenger-slot subimages additionally request their own nearest-neighbor brush
variant; the cache key includes that sampling choice, and the shared loader continues to use
bilinear filtering for dashboard pages and other artwork. This preserves the exact original
palette pixels when each portrait is displayed at the dashboard scale and excludes the keyed
border completely.

## Mission class -1 and collision consequences (2026-08-08)

`FUN_004c3eb0` receives behavior class **-1** for ordinary mission people, including transport
fares. That is a sentinel, not class zero: `FUN_004c71c0` resolves it through `FUN_004c7190`,
which ordinarily selects class 0..9 (with the extremely rare `FUN_004c7170` celebrity arm). The
remake previously left the C++ field at its default zero, so every unspecified fare inherited the
same class-0 head, body and voice pitch. Mission spawn paths now call the decoded chooser before
`ConfigureAgent`, preserving the real actor that later occupies the seat record.

Static decompilation of `FUN_00484d20` shows no direct write to medevac attr34: it damages the
aircraft, while BHAV 264's damage-scaled opcode-55 speed supplies the ordinary passenger reaction.
However, observed original runtime behavior is that a damaging impact can worsen a patient and
immediately disturb the EKG. The remake therefore applies one **BHAV 281 deterioration quantum**
(`1 + difficulty tier`) on each rate-limited damaging bounce and re-runs `FUN_004c5210`'s existing
EKG retune path immediately. This provenance distinction matters: the amount is decoded, while the
impact-to-patient edge is runtime-observed rather than a recovered direct attr34 store.

The ordinary-passenger impact face is a **transient**, not a stored injury state. BHAV 292 waits
10 ticks, calls BHAV 264 (whose own tail idles another three), and repeats; at the default 15 Hz
behavior rate the displayed face is therefore reconsidered about every 13 ticks / 0.87 seconds.
The immediate collision flinch now carries that deadline and then yields back to BHAV 264's exact
damage-scaled-speed edges (`>250 -> 2`, `>125 -> 0`, otherwise `1`). This deadline is a backstop for
a temporarily stalled passenger behavior stack. Medevac passengers are excluded because their
BHAV 264 branch is health-driven and the collision really did lower their health.

The transition to the destroyed helicopter state calls `FUN_004c0ba0(1)`, which sets every
occupied person's written-off attribute, posts `EVT_PersonDied`, and removes them from the wreck
through `FUN_004bfb20`. The port now performs that write-off when `bStartedDying` fires, before the
aircraft can be repaired and returned to an airport with passengers still attached.

## Dragging a portrait out of the seat well (2026-08-09)

The passenger drag is a Slate `FDragDropOperation`. `GetDefaultDecorator` is the graphic that
follows the pointer; passing `.Portrait(nullptr)` to `SSimCopterSeatPortrait` creates a valid drag
with no decorator, so the drop logic works while the passenger appears to stay in the panel.

Pass the same cached PEOPLE1 brush used by the seat image into the operation. The source widget is
`Hidden` while dragging (not collapsed, so the seat layout does not move), and the decorator uses
the source geometry's exact scaled size. Its `OnDragged` keeps the original grab offset under the
pointer instead of using `FDragDropOperation`'s tooltip-style cursor offset. `SSimCopterSeatWell`
handles a passenger drop without changing the manifest, and the operation restores the hidden
source widget; an unhandled drop outside the well still calls `DropPassengerAtSlot`.

## Verification (2026-07-31)

- `RebuildUnrealCpp.bat` â€” `Result: Succeeded`.
- `Automation RunTests SimCopter` â€” **112 passed, 0 failed**, including three new tests:
  `SimCopter.Passengers.HeadImages`, `.VoiceRates` and `.FaceProgram` (the last runs the shipped
  BHAV 264 through the interpreter at each health/speed/written-off combination).
- Scratch tooling from this pass: `Docs/scratchpad/dump_vtable.py`, `find_op54.py`,
  `find_bhav_callers.py`, `probe_seatwin.py`, `probe_people1.py`.

Not verified on screen; project policy reserves foreground runs for what a build, the decoded data
and automation cannot settle. The things worth a look when someone is at the keyboard: bandages
only on casualties, the seat portraits changing row as you fly and as a patient fades, and the EKG
audibly slowing.

### Verification (2026-08-08 follow-up)

- `RebuildUnrealCpp.bat` â€” `Result: Succeeded`.
- `Automation RunTests SimCopter.City.PeopleRules` â€” 1 passed, including deterministic
  `FUN_004c7190` class-selection checks.
- `Automation RunTests SimCopter.Passengers` â€” 3 passed (face program, heads, voice rates).
- Passenger portrait rendering follow-up: `SimCopter.Passengers` â€” 4 passed, including the real
  PEOPLE1 crop/filter test; `SimCopter.Formats.MaxisTexture.ReferencePeopleWindowsBitmap` â€” 1
  passed, including zero-RGB chroma-key checks.
- The later runtime-observed impact-trauma follow-up passed UHT and `git diff --check`, but its C++
  rebuild was blocked by an active editor Live Coding session; rebuild/test it after closing the
  editor.
- Not verified in-game; the remaining visual/audio check is intentionally left for an attended run.

### Verification (2026-08-09 portrait drag follow-up)

- `RebuildUnrealCpp.bat` â€” `Result: Succeeded`.
- `Automation RunTests SimCopter.Passengers` â€” 5 passed.
- Not verified on screen; the cursor anchoring and return animation need an attended drag check.

### The doropn cue is a TRANSITION, not a call (2026-08-13)

`BoardCarrier`'s early-out only catches an *exactly matching* re-board (same carrier, same harness
flag). A caller that toggles the harness flag â€” opcode 12 (`BoardSelection`) selecting the rope end
on one tick and the cabin on the next â€” falls straight through it and tears the passenger seat down
and rebuilds it every tick: re-notifying the mission layer, resetting the seat portrait, and
**re-firing people voice event 60**. That is the "get-in sound played over and over when I boarded
with a patient carried" report; the audible cue was the only visible symptom of a per-tick seat
churn.

Two fixes, both in `ASimCopterGroundAgent`:

- the cue is gated on `!bAlreadyInThisCabin` â€” somebody already sitting in that cabin has not opened
  a door, however many times a caller re-runs the board;
- `BoardSelection` refuses a **harness** board for somebody already seated in the cabin. Nobody
  climbs out of the cabin onto the rope; `TransferFromHarnessToCabin` owns the only legitimate
  cabin/harness move, and it goes inward.

Related: [[simcopter-people-logic-next]], [[simcopter-paramedic-handoffs]], [[simcopter-sound]],
[[simcopter-population-rendering]], [[simcopter-ue-figure-component]], [[simcopter-checkup-menu]],
[[simcopter-replay-clips]].

### attr34 is SIGNED, and reading it unsigned healed dying patients (2026-08-25)

**Reported symptom: "a medevac patient's health resets when they board the chopper."** It did, and
only in the cabin.

`FUN_004c5210`'s "already saying this" arm clamps the health attribute **in place**, and both arms
read `person+0x184` as a `short`:

```
if (100 < *(short *)(p + 0x184)) *(short *)(p + 0x184) = 100;
if (*(short *)(p + 0x184) < 0)   *(short *)(p + 0x184) = 0;
```

BHAV 281 deteriorates in **two steps** - `attr34 -= 1`, then `attr34 -= difficulty tier` (rec[8]'s
op74 overwrites the 20/10/3 rate local before rec[7] reads it, so the quantum is always
`1 + tier`). It therefore steps straight *past* zero whenever 100 is not a multiple of `1 + tier`,
which is every tier except 1, 3 and 4. Retail floors that at **zero** and BHAV 280 rec[11]'s signed
`attr34 < 1` kills the patient on the same pass.

The port read the `uint16` attribute slot zero-extended, so the wrapped `-2` arrived as 65534 and
`FMath::Clamp(..., 0, 100)` clamped it **up to 100** - and then wrote it back. A patient one pass
from dying was silently restored to full health, the seat portrait snapped back to face 0 and the
EKG back to 13 kHz. **Only in the player's cabin**, because BHAV 302 rec[8] gates the EKG on "is my
carrier the player heli"; the same patient left on the pavement died correctly. Hence "it resets
when they board".

Fixed by `ASimCopterGroundAgent::ReadMedevacHealth` - one sign-extending accessor that all three
reads (the EKG re-tune clamp, the EKG start pitch, `ReactToCabinImpact`) now go through.

**The moral for the rest of the port: the attribute block is `uint16[]`, but the shipped programs
compare through `int16` (`ExecExpression` cases 0 and 1 already do). Any attribute an expression
can drive negative must be read `int32(int16(...))`, never `int32(...)`.**

### There is no "patient died" notification, and retail has none either (2026-08-25)

`FUN_004aa150` case 0x17 (`EVT_PersonDied`):

```
case 0x17:
  if (iVar1 != -1) goto default_break;   // iVar1 is the owning record index
  local_4 = 0x3ab;                       // "Sim Died!" - UNOWNED deaths only
  FUN_00407b00(-(count * DAT_00506050));
```

and the tail only calls the message poster `FUN_0048c4a0` when the points or cash total is
non-zero. So a death that belongs to a mission record - a medevac patient expiring in your cabin
included - posts **no message and no immediate penalty**. The cost lands at completion, where
`CompleteMission` subtracts `Casualties` from the medevac end award. The port matches this exactly
(`SimCopterMissionSystem.cpp` `EVT_PersonDied` + the `bPostUi` tail); do not "fix" it by posting a
toast.

The feedback retail *does* give is diegetic: BHAV 312 plays `achdie` three times (rec[1]/[5]/[7]),
rec[9] then sets attr15 so BHAV 302 rec[10] falls to op85 and the EKG stops, and BHAV 264 rec[9]
freezes the seat portrait on face 2.

### Opcode 37 is a RECYCLE, not a despawn - and the body stays in the seat (2026-08-25)

**Corrects an earlier reading in this file.** `FUN_004c4e40` is not a teardown, so "retail removes
the dead patient from the chopper" was wrong. It does not remove them at all.

BHAV 312 'Die without falling first' rec[2] is **op 37**. The chain:

```
op37  FUN_004cc530 -> FUN_004ca4b0() ; return 3
      FUN_004ca4b0:
          uVar1 = person+0x152                    ; save Visible
          if ((short)person+0x148 == 0) vtable+8()   ; state 0: just restart the walker
          else                          FUN_004c4e40()
          person+0x15e = 1                        ; written off
          person+0x152 = uVar1                    ; put Visible back
      FUN_004c4e40:  FUN_004c0df0(person, 0, -1) ; vtable+8()
      FUN_004c0df0:  if (+0x1a0 == 0) lift them above terrain if they are below it
                     population bucket moved; +0x142=0; FUN_004c7090(0); FUN_004c7080(-1); +0x142=1
      FUN_004c7090(0): Visible=1, state=0, program := DAT_0058de80[0], vtable+8 (walker reset),
                     +0x10a = -1 (owning record), +0x15c = 0 (cabin/reaction counter), +0x166 = 0
```

**Nothing clears the carrier +0x1a0 and nothing calls `FUN_004c62e0`, so the seat manifest keeps
its record.** The ground clamp is gated on `+0x1a0 == 0`, so a carried person is not moved either.
That is the entire point of the program's name: unlike BHAV 903 'Rxn: Die', 312 skips BHAV 309 /
opcode 66, so the patient stays exactly where they are - which for a medevac victim means **still
sitting in your cabin**, invisible, head still image 10, seat portrait still on face 2.

**What a written-off person becomes is decided by the state-0 program.** BHAV 600 'Ambient
initbhav' rec[1] branches on the flag op 37 just set:

```
[ 1] attr15 == 0 ?  no -> [26]
[26] am I riding something ?  yes -> [22]   no -> [27] op70 snap Z -> [22]
[22] attr39 := 10          the bandaged head
[21] op54 := 2             the casualty seat face
[18] bind-anim 'Dead'
[19] attr14 := 1
[17] l0 := 50   [16] wait l0--  -> [17]     an endless idle
```

So they are **not** recycled into a walker: they lie there as a corpse indefinitely, keeping their
seat, until opcode 40 or a medic physically removes them. (rec[0]'s op85 is a second guarantee the
EKG stops.) BHAV 273 'Gawk at corpse (inc. medevac)' exists because these accumulate.

**The hospital medic is meant to collect the body.** `FUN_004cc830` (op 84, BHAV 263 rec[0]/[31])
walks the seat manifest and accepts `+0x148 == 6 **|| +0x15e != 0**`. That second arm is not a
nicety - it is the *only* thing that still identifies a body, because op 37 has already moved them
off state 6. BHAV 263 then op47-drops, op44-totes, op39-pushes BHAV 802, and BHAV 279 sets them
down; `FUN_004c6360(0)` inside those is what finally runs `FUN_004c62e0` and frees the seat.

Only **op 40** (`FUN_004cc5d0`) actually removes a person: carrier := 0 (which frees the seat),
drop anyone carried, release the voice slot, `+0x142 = 0`, `vtable+0x1c`. Note it returns **2**,
not 3.

**op 37's four users** (`Docs/scratchpad/agent-sessions/2026-08-25-medevac-death/find_op.py`):
BHAV 312 (medevac death), **BHAV 282 'Medevac test for finished' rec[4] - the SUCCESSFUL delivery**,
BHAV 903 'Rxn: Die' (after 309/op 66), and BHAV 1173 'crim - run from copter' rec[13] (after
posting outcome 9, CriminalCaught). So in retail a delivered patient celebrates and then lies down
as a body outside the hospital about 97% of the time - 282 rolls 1-in-2 then 1-in-6 twice for the
op-40 arm instead - and a criminal caught from the air does the same where they stood.

**What the port had:** `case 37: bRequestDespawn = true; return Stop;` - so the tick's generic
despawn arm ran `AlightFromCarrier()` (ejecting the body from the cabin, possibly airborne), hid it
and `SetLifeSpan(1.0f)`. The body was gone before any medic could reach it, and
`FindMedevacPassengerAboard` tested state 6 only, so it could not have found one anyway. Together
those are the "the paramedic won't take them" report.

**Now:** `ISimCopterBehaviorWorld::LeaveTheMap` / `ASimCopterGroundAgent::LeaveTheMap` port
`FUN_004ca4b0` (recycle to state 0, clear `MissionEventId` and `ReactionDepth`, ground-snap only
when not carried, written-off := 1, Visible preserved), then apply BHAV 600's corpse branch
directly - head 10, seat face 2, `SetMissionDeadPose()`. `FSimCopterPersonContext::bProgramRestarted`
tells the agent tick that this result-3 ends the pass, not the person.
`FindMedevacPassengerAboard` now carries the `|| written off` arm.

Covered by `SimCopter.Behavior.VM.ActionDelegation` (op 37 stops without ever requesting a despawn).

Related: [[simcopter-people-logic-next]], [[simcopter-paramedic-handoffs]], [[simcopter-sound]],
[[simcopter-population-rendering]], [[simcopter-ue-figure-component]], [[simcopter-checkup-menu]],
[[simcopter-replay-clips]], [[simcopter-crime-rooftop-rescue]].

### Follow-ups from the first in-game run (2026-08-25)

Three things the build above got wrong on screen, all now fixed:

- **The seat portrait only moved when BHAV 264 happened to run.** `FUN_004c6250` seats every
  passenger at a flat face 1 and BHAV 280 reaches 264 about once every 1.5 s, so a patient who had
  been losing health in the street boarded showing the middle face regardless, and the 50/1 edges
  were crossed between polls. `ComputeMedevacPortraitStateFromHealth` now owns BHAV 264
  rec[10]/[11]'s rule (`<1 -> 2`, `<50 -> 1`, else 0) and is applied in three places: at boarding
  (replacing the shipped placeholder 1 for casualties only), on every agent tick via
  `UpdateMedevacSeatPortrait`, and by `ReactToCabinImpact`. A DELIBERATE DIVERGENCE - the faces are
  exactly the ones 264 would pick, without the polling latency. `SimCopter.Passengers.FaceProgram`
  asserts the two agree at 100/50/49/1/0/-2 by running the shipped program against the helper.
  **Note the granularity is only three rows in people1.bmp**; the continuous decay channel is the
  EKG rate, `(health * 4 + 0x78) * 0x19`, 13 kHz down to 3 kHz.

- **The EKG kept beeping after death.** In retail BHAV 302 rec[10] falls to op85 once attr15 is
  set, and BHAV 600 rec[0] is op85 again for the recycled person. The port's `LeaveTheMap` stops
  the VM (`SetMissionDeadPose`), so neither ever ran and the loop played on. `LeaveTheMap` now
  calls `StopPersonVoice()` itself, standing in for BHAV 600 rec[0].

- **The body stood up while the medic handled it.** `AlightFromCarrier` clears the forced clip and
  rights the actor, which is correct for a passenger climbing out under their own power but not
  for a corpse: op 47 pulling one from the cabin and op 51 setting it down both left it upright,
  which read as the patient getting up and celebrating its own delivery. It now re-applies
  `SetMissionDeadPose()` when `bMissionPatientDead` or attr15 is set, matching BHAV 600's corpse
  arm, which binds 'Dead' and never leaves it.

The medic coming for the body at all is correct and stays - `FUN_004cc830`'s `|| +0x15e` arm.

### 'Dead' is for the dead. A live casualty holds 'Inju' (corrected 2026-08-25)

**This supersedes a wrong claim written here earlier the same day**, that `'Dead'` was a generic
prone-casualty pose because `BHAV 800 'Medevac initbhav'` rec[0] binds it for a live victim. That
bind is only the spawn-time one and BHAV 280 overwrites it on its very first pass. The authority is
**`BHAV 310 'Medevac animate'`**, which BHAV 280 rec[13] runs every pass:

```
[0] CALL 261 'idle a bit'
[1] op63 am I riding something ?  yes -> [3]   no -> [2]
[2] bind-anim 'Inju'        waiting on the ground
[3] bind-anim 'Slum'        in the cabin
```

So the three casualty poses are distinct and mean different things:

| clip | vertical span | horizontal | used by |
| --- | --- | --- | --- |
| `1Wal` | 68 | 37 x 24 | standing (reference) |
| `Whoa` | 70 | 14 x 48 | standing, arms above the head - BHAV 282's ending, and the knockdown tumble |
| `Inju` | 34 | 57 x 33 | **down but not gone** - BHAV 310 rec[2], `BeginPassengerFall`, the knockdown's Prone phase |
| `Slum` | 37 | 23 x 62 | BHAV 310 rec[3] and BHAV 802, riding |
| `Dead` | **8** | **75** x 39 | fully prone. **The corpse, and nothing else.** |

(Spans measured from privanim's SUIT figure, frame 0; see `Docs/scratchpad/classify_arlu_clips.py`.)

### The op-37 ending, and the one place the remake overrules it

`BHAV 282 'Medevac test for finished'` once the medic has set the patient down (rec[6]'s `op63`
loops on Idle-20 while they are still being carried):

```
[17] rand 1 in 2 ? -> [12] : [8]
[12] rand 1 in 6 ? -> [13] : [8]
[13] rand 1 in 6 ? -> [14] : [8]
[14] 'Whoa' -> sound 59 -> Idle-20 -> [16] op40 tear down      1 in 72
[ 8] 'Whoa' -> sound  6 -> Idle-20 -> [ 4] op37 leave the map  71 in 72
```

Both arms bind 'Whoa', so the arms-up gesture always happens; only the ending is a roll. op 37 sets
attr15, and BHAV 600's written-off arm then gives **every** such person the bandaged head, seat
face 2 and `'Dead'`. In retail a patient you successfully delivered therefore cheers and then lies
down as a corpse. That is genuinely what the data does, and it is wrong on screen.

**`ASimCopterGroundAgent::LeaveTheMap` splits on how op 37 was reached**, which the shipped graphs
make unambiguous because op 37 sets attr15 itself and must be sampled before it runs:

- **died** - `BHAV 312 rec[9]` and `BHAV 903 rec[9]` are `attr15 := 1` and only then rec[2] op 37.
  Keeps BHAV 600's arm verbatim: head 10, seat face 2, `SetMissionDeadPose()` (`'Dead'`).
- **survived** - `BHAV 282` (delivery) and `BHAV 1173 rec[13]` (criminal caught from the air) never
  touch attr15. `SetMissionRetiredAlivePose()` binds **`'Inju'`** and leaves
  `bMissionPatientDead` false. DELIBERATE DIVERGENCE, and the reason is one line: a corpse pose on
  somebody who was just saved reads as them dying on the hospital step.

`SetMissionFinishedPose(clip, bDeceased)` is the shared body. `AlightFromCarrier` re-applies
whichever of the two applies when a written-off person is set down, because its clip clear and
upright rotation belong to a passenger getting out under their own power - without that the
casualty popped upright for the length of the medic's handoff.

Related: [[simcopter-vehicle-knockdown]] (the knockdown Prone phase picked `'Inju'` for exactly this
reason, and got there first).
