# SimCopter sound DECODED

*The whole sound subsystem decompiled and ported: the 130-slot table, the nine-call API, the
attenuation law, and the event -> id map. Recorded 2026-07-30.*

## The shape of it

The original owns **one DirectSound buffer per registered sound id**. `FUN_00424b70` runs once at
startup and registers 130 of them into the manager at **`0x0055b1a8`** (the pointer array starts at
`0x0055b1ac`, so a slot is `*(int*)(0x55b1ac + id*4)`). Every gameplay system then talks to that
array through nine free-function wrappers that all index by id:

| wrapper | what it is | port |
| --- | --- | --- |
| `FUN_0042a2a0(id, flags)` | Play2D, volume = master | `Play2D` |
| `FUN_0042a1f0(id, pos, flags)` | Play3D, culled then positioned | `Play3D` |
| `FUN_0042a310(id)` | Stop | `Stop` |
| `FUN_0042a3a0(id)` | IsPlaying | `IsPlaying` |
| `FUN_0042a360(id, adj)` | volume = adj + 10000 | `SetVolumeAdjust` |
| `FUN_0042a2f0(id, pos)` | -> `FUN_004247c0`, the attenuation law | `SetPosition` |
| `FUN_0042a330(id, delta)` | `SetFreq(base + delta)` | `AddFrequency` |
| `FUN_0042a100(id, name)` | swap the WAV in a slot | `SetFile` |
| `FUN_0042a3b0(_, id, tag)` | queued play + completion list | `Play2D` |

Ported as `USimCopterAudioSubsystem` (`Public/Audio/`), with the table in
`SimCopterSoundTable.h/.cpp`. Tests: `SimCopter.Sound.*` (4).

## Traps

**One voice per id, and a Play on a playing slot is a NO-OP.** `FUN_0042a2a0` turns the flag word
into the buffer's play mode: bit1 set means mode 1 (rewind), clear means mode 2 ("if playing,
return"). *Every shipped call site passes 0 or 1*, so mode is always 2. This is load-bearing, not
an accident: it is why hundreds of water droplets landing per second produce one continuous hiss
instead of a machine-gun, and why a second explosion never stacks on the first. The many call
sites written `if (!IsPlaying(id)) Play(id)` are belt-and-braces on top of it. A port that gives
each emitter its own voice sounds nothing like the original.

**`AddFrequency` is absolute, not cumulative.** vtable +0x60 is `return this[0x5c]` — the clip's
*own* recorded rate — so `FUN_0042a330` is `SetFreq(base + delta)`. Calling it every frame with the
same argument is a no-op. Reading it as cumulative makes the rotor run away.

**The attenuation is far flatter than it looks** (`FUN_004247c0`):
`index = 10000 + (FixedMul(FixedDiv(dist, 1920.0), -4000.0) >> 16)`. That is 0 dB at the listener
falling *linearly* to **-40 dB at the 1920-unit cull edge** — not to silence. The index is
DirectSound's own unit, hundredths of a dB below unity, clamped to [0, 10000] by `FUN_0041de20`.
1920 units x 6.25 cm = a **120 m** audible radius.

**The audibility test is an octagon, not a sphere.** `FUN_00468220` is `largest + (other two >> 2)`,
which under-estimates a true length off-axis (125 vs 141.4 on the diagonal). A sound cuts out at
1920 units straight ahead but stays audible to ~2172 diagonally.

**Panning is nearly absent.** `FUN_004247c0` sets pan to `(10000 * localX) / 8`, i.e. +/-12.5% of
DirectSound's range. The port instead spatialises through UE and keeps only the volume law — a
deliberate divergence, see below.

**Sirens have no direction at all.** `FUN_004a1d50` plays each of the four emergency loops at
`listener + (0, 0, distance)` — a synthetic point whose only job is to be the right distance away —
then overrides the volume with that same distance anyway. There is one voice per *service*, driven
by the nearest vehicle of that kind and nothing else. Ported as Play2D + the same volume law.

**CESSLP1 is the plane's engine; DIVE1 is the dive.** `FUN_004b23e0` plays 0x1c whenever the plane
is in range and layers 0x1b on top *only* while +0x07 (the crashing flag) is set. DIVE1 is not "the
other aircraft".

**0x11 and 0x20 are both `ambsrn11.wav`, deliberately.** A slot is a voice: 0x11 is the ambulance
driving to a call and 0x20 is the crash-rescue flyby, and they must be able to sound at once.
Collapsing them is a regression; the table test guards it.

**The rotor loop is per helicopter model.** `FUN_00488fd0` calls `FUN_00429ff0(0, model + 0x58)`
(SetFile) before each start, from the 0x5c-byte record at `DAT_005040e0`: COPLOOP5, COPLOOP6,
COPLOOP, COPLOOP3, COPLOOP4, COPLOOP6, COPLOOP, COPLOOP2, COPLOOP2 for runtime types 0..8. The
registry's `EngineLoopSound` already carried these — nothing had ever played them. Its two laws:

```
AddFrequency(0, (rpm * 4 - 0x5a0) * 0xf)   // 0x5a0 = 4*360: unity at 360 rpm, ~0.67x at the 300 gate
SetVolumeAdjust(0, (rpm - 0x168) / 4)      // about -80 across the whole range - under a decibel
```

The loop's audible character is **pitch**, not volume. It runs on every seventh 0.05 s frame
(`DAT_00504064`), which the port keeps as a 0.35 s accumulator rather than a frame count.

**`heli[8] & 1` gates every 2D helicopter sound** — it marks the aircraft the player is flying, so
an AI helicopter across the city cannot put CHOPSTAR in your ears. Ported as `IsLocallyControlled`.

**`heli[0xcc]` is fuel.** The rotor loop needs it above zero, and touchdown plays SOFTBMP2 when dry
versus CHOPSTOP when not.

**Ghidra spells `#` in a symbol as `_`.** The people table carries `trbnc_` / `trptf_` / `tubaf_`
for files that are `trbnc#.WAV` etc. on disk. The loader retries a trailing `_` as `#`.

## The table

130 ids, `0x00..0x81`, transcribed in `SimCopterSoundTable.cpp`. Directory index 2 = `sound\`,
3 = `sound\` + language. Runs worth knowing: `0x2f..0x41` D1000-D1018, `0x42..0x4a` L001-L009,
`0x4b..0x5e` D2001-D2020, `0x5f..0x6e` **DIS053-DIS068**, `0x71..0x7e` the people voice bank
(seeded with xWhoa, swapped per speaker), `0x7f/0x80/0x81` BLIP1/NOEQUIP/FIRESTAR.

The dispatcher run is where the remake was **playing the wrong sound**: the old loader mapped ids
`0x5f, 0x60, 0x61, 0x65..0x68, 99, 100` onto `D2###` filenames by arithmetic. They are the DIS0xx
lines. The mission system had always computed the right ids; only the loader was wrong.

`0x1d` HELP1 registers against the language folder although retail ships `help1.wav` in `sound\`.
The loader searches language-then-root for every slot, which absorbs that.

## People voices

`FUN_004c5210(person, event, gender, b2D)` is a `switch (event)` over **62 voice events** naming
122 clips in `sound\people\`; each picks uniformly with `FUN_004cea00(N)`, `SetFile`s the winner
into the speaker's bank slot, applies a per-person `AddFrequency`, and plays it 3D at
`person[0x1cc]`. Person `[0x172]` holds the bank index and every caller recomputes the id as
`+ 0x71`. `FUN_004c4ee0` re-positions it each frame within ~3 tiles and stops it beyond.
Extracted by `Docs/scratchpad/sound/extract_people_voices.py`; the table and the bank allocator
are ported, and events 56/62 name clips (MORITURI, REPAIR) that retail does not ship.

## The radio (decoded 2026-07-31, not yet ported)

**The assets exist — check the right copy.** Every file under
`Reference/SimCopterOriginalGame/sound/radio/` is zero bytes, but that is a bad copy: the real
install at `D:\Downloads\SimCopter\Extracted\SIMCOPTER\sound\radio\` has all 154 (about 185 MB).
Only the five `.id` files are legitimately empty — they are markers whose *filename* is the
station's call sign. Do not conclude "no assets" from the reference tree alone.

**Playlists are discovered by globbing, not from a table.** The radio's own string table is at
`0x004f8f40` (`classic\`, `rock\`, `techno\`, `country\`, `jazz\`, `oldies\`, `rb\`, `easy\`,
`talk\`, `radio\`, `commercl\`, `stations\`, `music\`, `dj\`, `jingle\`, `*.wav`, `*.id*`, `KMIX`).
`FUN_004302b0` globs `*.id*` under `stations\` to find the stations; the loader then globs `*.wav`
per station under `music\` and `dj\<lang>\`, and globally under `commercl\<lang>\`. The exe knows
**nine** genres (`FUN_00430e90` validates against that list); retail ships four plus `mix`.

**It is a shuffle bag, not a per-pick random draw.** Two Fisher-Yates shuffles, both using
`FUN_00455d70(n)` — a **subtractive lagged-Fibonacci** generator (55-word state at `DAT_0055af08`,
two cursors advanced mod 55) returning `state % n`. That is *not* MSVC `rand`, and not the people
LFSR either; the radio is the third RNG in the game.

- `FUN_0042ff00` (vtable +0x18) shuffles a file list in place: flatten the linked list to an array,
  Fisher-Yates it, rebuild. `FUN_0042fa20` calls it **once per category** at load.
- `FUN_00430070` (vtable +0x1c) shuffles the **slot-type pattern array** (`this+8`/`this+0xc`) at
  the end of every cycle, then applies an anti-repeat rule: if the newly shuffled first element
  equals the previous cycle's last, swap first and last so the same category cannot straddle the
  seam.

Within a category, playback **walks the shuffled list in order and wraps** — cursor at
`+0x58/+0x6c/+0x80/+0x94` against the list end at `+0x50/+0x64/+0x78/+0x8c`, reset to the head on
reaching it (`FUN_0042f3d0`). So every track plays once per cycle before any repeats.

**Then probability gates on top**, in the scheduler `FUN_0042f160`, and *these* use MSVC
`rand() % 100`:

| type | roll | content | directory |
| --- | --- | --- | --- |
| 0 | always | music | `stations\<x>\music\` |
| 1 | `< 0x14` = **20%** | DJ | `stations\<x>\dj\<lang>\` |
| 2 | `< 0x5a` = **90%** | commercial | `commercl\<lang>\` |
| 3 | `< 0x14` = **20%** | jingle | `stations\<x>\jingle\` — **empty in retail, never fires** |

That mapping is settled by three independent things, not by the order alone: the path builder loads
`music\` (0x4310f7), `dj\` (0x431148), `commercl\` (0x43125c), `jingle\` (0x4314c9) into base-path
members at ascending offsets 0x48 / 0x60 / 0x78 / 0x8c; type 0 takes the plain direct-play path
while **types 2 and 3 walk a multi-part sub-index** (`entry+0xc`, starting at -1) and type 1 does
not — and the only multi-part files that ship are commercials (`ad013a`/`ad013b`,
`ad050a`/`ad050b`), while every DJ line is a single file. The `jingle` folders exist under all four
genre stations but are empty, so type 3 is inert; `commercl\german\` is an empty localisation slot.

plus a **10%** chance (`rand()%100 < 10`) to play the music slot *again* without advancing the
cursor — back-to-back tracks. A failed roll skips that slot silently; the cursor has already moved.

Between items the scheduler waits for the current sound to stop and then for **4000 timer units**
(`if (uVar4 < 4000) return`) before picking again — timed by `QueryPerformanceCounter` or
`timeGetTime`, selected by `this[8]`. Types 2 and 3 additionally walk a sub-index inside one entry
(`entry+0xc`, starting at -1) — multi-part items played in sequence, which is what the shipped
`ad013a`/`ad013b` and `ad050a`/`ad050b` pairs are.

## The dash tuner (ported 2026-07-31)

The radio lives in **dash4**, the 455x43 strip above the instrument panel: an FM head unit whose
lit scale is printed 88 / 92 / 96 / 104 / 108. (Those are evenly spaced in *pixels* but not in
frequency - the sequence skips 100. It is an art quirk in the original; left alone.)

`FUN_00451980` builds that widget and stores a rectangle **(20, 15, 86, 38)** plus `[0x24] = -1`
for "no station yet". The rectangle bounds the whole head unit, **not** the lit scale, so the
needle geometry came from measuring `DASH4.BMP` instead — a luminance profile
(`Docs/scratchpad/sound/radio/measure_tuner.py`) puts:

- the lit band at **rows 24..30, columns 17..92**;
- the printed label centres at **x = 22, 38, 53, 69, 84**.

The port therefore hangs the needle's end detents on the outermost printed labels (22 and 84) and
divides evenly between, which lands the five shipped stations at 22.0, 37.5, 53.0, 68.5, 84.0 —
within half a pixel of every label. `Docs/scratchpad/sound/radio/preview_needle.py` composites the
result onto the real artwork so this can be re-checked without launching the game.

Radio state reaches the dash over a **message bus**, not by polling: `FUN_00430950` publishes a
7-dword struct under the id `0x5245494f` ("REIO") and `FUN_00430890` reads it back, falling back
to `PTR_DAT_004f8f8c` — the literal **"KMIX"** — when there is no saved state. That is why the
radio opens on KMIX. The struct is {volume, ?, trackIndex, ?, ?, ?, ?}; volume defaults to 10000
(`FUN_004306e0`).

**Remake choices**, because the original's input path for tuning was not pinned down: stations are
sorted by call sign for a stable dial order; left-click on the head unit tunes to the nearest
station, right-click is power; `SimRadio [next|prev|on|off|<callsign>|<index>]` does the same from
the console. The needle is drawn one page pixel wide and dims when the set is off.

Watch out for `radio.bmp` — it is a UI **radio button**, nothing to do with this. The dashboard art
is `dash4.bmp`; `radiotv.bmp` / `station.bmp` belong to the options screen.

## There are two audio quality sets, and it changes the rotor

The retail install ships **22050 Hz / 16-bit mono** versions of many effects; a partial or
low-quality copy has **11025 Hz / 8-bit**. Some files (DOUSE, BLIP1, the D1xxx voice lines) are
byte-identical in both. On 2026-07-31 the repo's `Reference/` tree was the low-quality set with a
zero-byte radio; it was refreshed from the user's full install (`robocopy`, no `/MIR`, so the 13
reference-only files survived) and is now 525 files / 209 MB.

**This is not cosmetic.** `AddFrequency` adds a Hz delta to *the clip's own rate*, so the rotor's
pitch depth depends on which set is installed — and the original behaves the same way, because it
reads the rate off the buffer:

| COPLOOP rate | 300 rpm (lift gate) | where UE's 0.4 pitch clamp starts biting |
| --- | --- | --- |
| 11025 Hz | 7425 Hz = **0.67x** | about 250 rpm |
| 22050 Hz | 18450 Hz = **0.84x** | about 150 rpm |

The port needs no change for this (it computes `Base + DeltaHz` from the loaded clip), but do not
"fix" a rotor that sounds shallower than a video of the game — check the asset rate first.

## Corrections to earlier notes in this file

Three claims here were made against the incomplete `Reference/` copy and were wrong:

- The radio assets are **not** missing. See above.
- `help1.wav` **is** shipped in `sound\English\`, exactly where id 0x1d's dir-3 registration says.
  The language-then-root search still earns its keep, but not for this.
- `MORITURI.WAV` and `REPAIR.WAV` (voice events 56 and 62) **are** shipped in `sound\people\`.

The full install also carries `ambsiren.wav`, `blast1.wav`, `explode1.wav`, `firelg.wav`,
`firemd.wav`, `firesm.wav`, which the low-quality copy lacks and which no registration references —
unused or cut.

**Do not copy the executable from a full install**: it is usually SimCopterX-patched (1558528 bytes
versus the original 1521664), and SimCopterX relocates `.text`, so every address in these notes
would be wrong. Audio only.

## Not ported
- **The front-end sounds are not table slots.** Each screen builds its own standalone sound object
  (`FUN_0041d4c0` + a direct `Play`). Decoded: `hangar.wav` (FUN_00449cb0), `career.wav` +
  `carsel.wav` (FUN_00457c90), `menu.wav` (FUN_0045e920 / FUN_0043c6d0), plus `menuback.wav`,
  `button.wav`, `MBoxCht.wav`/`MBoxCht1.wav`, `blast.wav`, `gradnot.wav` whose owning screens are
  not yet attributed. The hangar and career ones are wired; the rest wait on knowing their screen.
  They play **unlooped** — the hangar's is `Play(0, 1)`.
- 0x0c MEVAC is registered and has no shipped call site.
- 0x22 BOO, 0x18 TGPOP, 0x0b FIREMIS2, 0x08 PUNCH3, 0x09 KAPOW4, 0x24 LASER, 0x2b TSCREECH,
  0x6f/0x70 the vehicle door pair, and 0x2d/0x2e the passenger screams are decoded with their call
  sites but hang off agent state the remake models differently; they are listed in
  `Docs/scratchpad/sound/callsites.txt` with the id at every one of the 242 sites.

## Gameplay mappings added 2026-08-08

- Firework mortar launch uses root sound slot `0x17`, `TGSHWH.WAV`, at the ground launch point;
  the later apex detonation remains slot `0x07`, `BOOM1.WAV`.
- Player boarding uses slot `0x25`, `DOROPN.WAV`, while player exit uses slot `0x26`,
  `DORCLS.WAV`. Saved-game possession is intentionally silent. This is a requested one-cue split:
  the original `FUN_0048a580` command `0x1a` queues both `0x25` and `0x26` during dismount.
- The player-board cue must run **after possession**. `GetHelicopterAudio()` deliberately rejects
  an aircraft that is not locally controlled, so calling it before `Possess()` silently drops
  `DOROPN`; save restore remains silent through the separate no-blend guard.
- Passenger carrier changes use people voice event 60 (`doropn`) rather than the root-table door
  pair. `FUN_004c6360` calls `FUN_004c5210(0x3c,1,0,1)` when assigning the player helicopter and
  when leaving a door-bearing carrier. The event has one clip, so passenger boarding **and**
  alighting both play `doropn`; there is no `dorcls` people event and no random choice.
- Career level completion is also fixed, not randomized: vtable entry `0x0044bed0` calls
  `FUN_0042a3b0(0,0x69,100)`, so it queues language slot `0x69`, `DIS063.WAV`, exactly once.
  `DIS064` through `DIS068` are adjacent registered assets but this completion path never chooses
  them and consumes no RNG.

## Deliberate divergences

1. **Spatialisation over the original's pan.** The original panned +/-12.5% and did all its
   distance work by volume. The port keeps the volume law exactly and lets UE spatialise for
   direction, with a runtime `USoundAttenuation` that has `bAttenuate` **off** so nothing is
   applied twice.
2. **Pitch is clamped to UE's [0.4, 2.0].** The original clamps frequency to [100, 100000] Hz, far
   wider. Below roughly 250 rotor rpm the spool-up is higher-pitched here than in the original.
3. **Looping uses a procedural-wave underflow refill.** `USoundWaveProcedural` ignores `bLooping`,
   so a looping slot binds `OnSoundWaveProceduralUnderflow` to re-queue an immutable shared copy of
   the samples (the delegate fires on the audio render thread and must not touch subsystem state).
   One-shots retire on a duration deadline because a procedural wave never reports finishing.

## Assets

Nearly every effect is **11025 Hz, 8-bit unsigned mono PCM**; the loader widens 8-bit to signed 16.
The stereo exceptions are the three 13.70 s front-end beds (`menuback`/`career`/`hangar`), plus
`blast` and `explode`. `bldexpl.wav` is 11111 Hz and `firealrt.wav` is 8000 Hz.

See [[simcopter-heli-flight-model]] for the rotor/fuel fields the loop reads,
[[simcopter-ambient-vehicles]] for the plane/train slots, [[simcopter-emergency-dispatch]] for the
services the sirens follow, and [[simcopter-people-logic-next]] for the behaviour layer that will
raise the voice events.
