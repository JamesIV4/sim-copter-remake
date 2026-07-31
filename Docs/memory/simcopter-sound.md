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

## Not ported

- **The radio is impossible from this install**: every file under `sound/radio/` — 154 of them,
  all five stations, their DJs and all 79 commercials — is **zero bytes**. There is nothing to
  decode against. `sound/`, `sound/English/` and `sound/people/` are all intact.
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
