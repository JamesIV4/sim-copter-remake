# SimCopter walking sounds

*Decoded and ported 2026-08-08.*

## Finding

The original does **not** use animation-frame footstep notifies. Walking audio is a long looping
person voice event, started and stopped by the same post-move selector that chooses `NoMo`, `1Wal`
or `1Run`.

The three files are voice events in `FUN_004c5210`:

| Event | File | Original use |
| --- | --- | --- |
| `0x0e` | `sound/people/xFtShoes.wav` | Default for most ordinary people |
| `0x28` | `sound/people/xFtHeels.wav` | Women / blonde figures |
| `0x29` | `sound/people/xFtBoots.wav` | Blue worker, fireman, cop, tuba player and pilot |

All three retail files are mono, unsigned 8-bit PCM at 11025 Hz. Shoes is 5.828 s, heels is
4.853 s, and boots is 5.881 s. `FUN_004c5210` marks each event as looping.

## Per-person footwear assignment: FUN_004c71c0

`FUN_004c71c0` assigns a persistent "own voice" event at `person+0x18c` when it chooses the
person's behavior/figure class. This is the footwear selection; the walking code does not inspect
terrain, animation frames, or a separate shoe field.

- Heels (`0x28`): classes 0 Blonde, 1 Woman, 2 2woman, 11 2blonde.
- Boots (`0x29`): classes 6 BLUE, 13 Fireman, 14 Kopp, 18 TubaExpert, 19 pilot.
- Shoes (`0x0e`): all remaining ordinary classes.
- Dog/cow can rarely replace shoes with an Elvis loop (1 in 200), Nessie and Elvis always use an
  Elvis loop, and the common tail can replace any class's own voice 1 in 65000. The post-move
  wiring treats those loops exactly like footsteps.

The player person is initialized as class 19 (`pilot`) by `FUN_004c7d10`; `FUN_004c1b50` also
restores class 19 during on-foot control. The normal player walking sound is therefore
**`xFtBoots.wav`**. The class-10 dog-player Easter egg is the only decoded exception.

## Start/stop wiring: FUN_004c6970

`FUN_004c6970` is the post-move result handler. For normal movement results 0 and 8 it does two
jobs together:

1. Select `NoMo` at speed 0, `1Wal` at speed 1..6, or `1Run` at speed 7+.
2. If person audio is enabled (`person+0x176 != 0`):
   - speed 0: stop the current sound, but only when it is the person's own voice event;
   - speed nonzero: request the person's own voice event at `person+0x18c`.

This is called after each player move by `FUN_004c65e0`, and after behavior-VM movement for NPCs.
The player's movement input is reduced to speed 0, 4, or 8 before the call, so the same switch
changes both walk/run animation and footstep playback.

## Playback and pacing: FUN_004c5210

The walking loop uses a person voice-bank slot and positional 3D playback at the person's world
position (`person+0x1cc`). Its initial frequency offset is:

```text
(MoveSpeed - 1) * 500 Hz
```

When the same own-voice loop is already playing, the call does not restart it. It sets an absolute
frequency instead:

```text
(MoveSpeed * 4 + 0x54) * 0x7d Hz
```

This is 10500 Hz at speed 0, 12500 Hz at walk speed 4, and 14500 Hz at run speed 8. The original
WAV headers are 11025 Hz, while the formulas are evidently centered on a nominal 11000 Hz.

The ordinary call is neither forced nor 2D. `FUN_004c5210` therefore accepts it only while the
game is in on-foot mode (`DAT_00503aa0 == 3`), while the person rides the player helicopter, or
when another call explicitly forces it. The player owns/reserves a voice slot when on-foot mode
starts (`FUN_004c0910`) and the post-move call is allowed to allocate one for person id 32000.
Ordinary NPC movement does not allocate a slot just to make footsteps; it can reuse a slot the
person already acquired for speech. This keeps city footsteps sparse instead of starting a loop
for every pedestrian.

## Remake implementation

The shared audio primitives provide:

- `SimCopterSoundTable.cpp` maps events 14/40/41 to the three files.
- `PlayPersonVoiceEvent` implements looping, 3D positioning, and both decoded rate formulas.
- `StartOriginalBehavior` assigns the decoded own-voice event to every ground agent.

The movement trigger is now wired in both actor paths:

- `ASimCopterGroundAgent::MoveStep` starts/stops each NPC's assigned shoes/heels/boots loop with
  its `NoMo` / `1Wal` / `1Run` movement result.
- `ASimCopterOnFootPawn` is a separate Unreal character rather than the original player-person
  object, so it drives the pilot's boots loop from its grounded velocity alongside
  `UpdateBodySprite`'s `NoMo` / `1Wal` selection.

The trigger still comes from movement state, not individual animation frames. Loops are attached
to each person's root component so their spatial origin follows the body directly. The player
keeps a 2400 cm radius (six remake city tiles). NPC footsteps use full volume with one tenth that
range (800 cm), and are driven from measured horizontal velocity rather than the BHAV speed
attribute alone. The component is retired on idle, blocking, leaving range,
boarding/unpossession, or destruction.

One deliberate mixer divergence is required by the remake's user-facing behavior: footsteps are
polyphonic components independent of the fourteen-slot people voice bank. The original reused a
person's one DirectSound voice buffer, so a line and footsteps could replace each other. In the
remake, dialogue/reactions and footwear loops can play simultaneously and neither cuts off the
other.

## Evidence

- `Docs/scratchpad/ghidra/out_people_voice_assignment_4c71c0.txt`
- `Docs/scratchpad/ghidra/out_people_post_move_audio_4c6970.txt`
- `Docs/scratchpad/ghidra/out_people_footsteps_4c5210.txt`

Status: **Confirmed** from executable decompilation and the three retail WAV headers, then
**Implemented** with the scoped mixer divergence above. Build and focused automation passed; not
verified in-game.
