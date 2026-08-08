# SimCopter passenger state display and person heads

*The seat window, the head every person wears, and the medevac EKG — decoded and ported 2026-07-31*

## The seat manifest

The helicopter carries a manifest at `DAT_005040d0+0x1d4`, built by `FUN_0048bf60`:

| offset | field |
| --- | --- |
| +0x00 | dirty flag (`FUN_0048bf30` reads, `FUN_0048bf40` sets, `FUN_0048bf50` clears) |
| +0x04 | seat capacity, from the model registry at `&DAT_005040e4 + type*0x5c` |
| +0x08 | seats occupied — `FUN_0048c1e0` / `FUN_0048bfe0` are both `[+4] - [+8]`, i.e. free seats |
| +0x14 | seats per row, **fixed at 5** |
| +0x1c | 16 records, stride 0x14 |

Record = five ints: **`+0x00` head image** (copied from `person+0x18e`), **`+0x04` face row**,
`+0x08` flags (always 0x200, never read back), `+0x0c` person id, `+0x10` display slot.

- `FUN_0048bff0` adds — refuses when capacity == occupied, fills the first free record, then
  repacks every record's display slot.
- `FUN_0048c120` removes by person id and repacks the same way.
- `FUN_0048c0c0` finds a record by person id; **`FUN_0048c0e0` writes `+0x04`** and is the whole
  body of people opcode 54 (`FUN_004ccb40`, which then marks the window dirty).
- `FUN_004c6250` is boarding: `{head = person+0x18e, face = 1, 0x200, person+0x12e}`.
  `FUN_004c62e0` is alighting.

## The blit — FUN_00453f70

The seat window widget is `FUN_00453840` (`seatwin2.bmp` + `people1.bmp`, vtable `0x004f2f78`);
`FUN_00453cb0` redraws it every fourth frame while the dirty flag is set.

```
clear:    every seat gets people1 cell (0, 0) — the empty chair
occupied: src x0 = (record[0] * 3 + 3) * 9   => column = head + 1, width 27
          src y0 =  record[1] * 0x21         => row    = the face,  height 33
          dst    = ((slot % 5) * 0x20 + 0x0e, (slot / 5) * 0x23 + 0x0a)
```

So the portrait **column is the passenger's own head** and the **row is the face opcode 54 set**.
Three rows of five at that pitch fill the 186x115 page exactly (10 + 3*35 = 115) and centre the
block in the printed well; the remake's earlier measured-by-eye 29/34 stride sat left of centre.
A record whose person id matches the one being dragged draws the empty cell instead — that is how
a portrait on the cursor leaves its seat looking vacant.

## Heads: FUN_004c71c0 and the bandage

`FUN_004c71c0` binds a **fixed head per behavior class** into `person+0x18e`, and only when that
field is still -1:

| class | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 |
| --- | - | - | - | - | - | - | - | - | - | - | -- | -- | -- | -- | -- | -- | -- | -- | -- | -- | -- | -- |
| head | 4 | 8 | 6 | 7 | 5 | 7 | 7 | 7 | 5 | 5 | 6 | 9 | 3 | 2 | 1 | 5 | 4 | 4 | 7 | 0 | 7 | 5 |

**No class claims head 10.** Its only writer is `FUN_004c7090`, the state setter: *state 6 — the
medevac victim — always gets head 10*, the bandaged one. The head table itself is `DAT_0058f0e0`
(filled by the privanim loader `FUN_004ceab0`): SIM3D.BMP image ids
`{4, 5, 0x2c, 0x2d, 0x2e, 0x41, 0x2f, 0x42, 0x30, 0x31, 0x43}`, so head 10 is image **0x43**.
`FUN_004c7f10` pushes it into the bound BODC figure node's `+0x30` on every draw.

**This was the remake's bug.** `BuildPedestrianFigure` picked the head with
`(hash / 3) % 11`, so roughly one pedestrian in eleven wore the casualty bandage while actual
medevac victims wore whatever came up. The seat window compounded it by inventing a face from
`EventId` and choosing the row from the passenger *kind*.

`FUN_004c71c0` also writes `person+0x178` (voice pitch offset: 500/400/700/0/-700/-200/-300/-100/
300/900/-500/-300/-300/-900/-1000/-8000/1000 by class) and `person+0x18c` (the person's own
looping voice event: 0x0e/0x28/0x29 footsteps, or one of the eight Elvis noises 0x2f..0x36 — always
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

## The EKG — FUN_004c5210, people opcodes 57 and 85

Opcode 57 (`FUN_004ccca0`) forwards the record's four args straight into `FUN_004c5210`
`(event, allocateSlot, nonPositional, force)`; opcode 85 (`FUN_004cc110`) is the same function
called as `(-1, 1, 1, 1)` — **stop talking and give the slot back**, not "ambient audio".

Gate: play when the sound is 2D, when forced, when the person is riding the player's cabin, or
when `DAT_00503aa0 == 3` — the mode the game enters when you **step out and walk the streets**
(`FUN_00484d20` sets it beside `FUN_004c0b10`, which places the player's own person).

Each speaker borrows one of the fourteen bank slots (`person+0x172`, sound id `+0x71`). If the
requested event is already loaded and playing **and** matches the person's own voice event
(`+0x18c`), the handler does not restart it — it calls the buffer's SetFrequency (vtable +0x68)
with an **absolute** rate:

- event 0x3a (EKG): `(health * 4 + 0x78) * 0x19` — **13000 Hz at full health, 3000 Hz at zero**;
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
  39 moves (a swoon through opcode 35 does that mid-life). Only the texture parameter changes — no
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

## Verification (2026-07-31)

- `RebuildUnrealCpp.bat` — `Result: Succeeded`.
- `Automation RunTests SimCopter` — **112 passed, 0 failed**, including three new tests:
  `SimCopter.Passengers.HeadImages`, `.VoiceRates` and `.FaceProgram` (the last runs the shipped
  BHAV 264 through the interpreter at each health/speed/written-off combination).
- Scratch tooling from this pass: `Docs/scratchpad/dump_vtable.py`, `find_op54.py`,
  `find_bhav_callers.py`, `probe_seatwin.py`, `probe_people1.py`.

Not verified on screen; project policy reserves foreground runs for what a build, the decoded data
and automation cannot settle. The things worth a look when someone is at the keyboard: bandages
only on casualties, the seat portraits changing row as you fly and as a patient fades, and the EKG
audibly slowing.

### Verification (2026-08-08 follow-up)

- `RebuildUnrealCpp.bat` — `Result: Succeeded`.
- `Automation RunTests SimCopter.City.PeopleRules` — 1 passed, including deterministic
  `FUN_004c7190` class-selection checks.
- `Automation RunTests SimCopter.Passengers` — 3 passed (face program, heads, voice rates).
- Passenger portrait rendering follow-up: `SimCopter.Passengers` — 4 passed, including the real
  PEOPLE1 crop/filter test; `SimCopter.Formats.MaxisTexture.ReferencePeopleWindowsBitmap` — 1
  passed, including zero-RGB chroma-key checks.
- The later runtime-observed impact-trauma follow-up passed UHT and `git diff --check`, but its C++
  rebuild was blocked by an active editor Live Coding session; rebuild/test it after closing the
  editor.
- Not verified in-game; the remaining visual/audio check is intentionally left for an attended run.

Related: [[simcopter-people-logic-next]], [[simcopter-paramedic-handoffs]], [[simcopter-sound]],
[[simcopter-population-rendering]], [[simcopter-ue-figure-component]], [[simcopter-checkup-menu]].
