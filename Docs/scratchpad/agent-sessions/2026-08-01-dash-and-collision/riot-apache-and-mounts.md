# Riots, the Apache armament, tool mounts and the fire rate — 2026-08-01 (second pass)

## 1. Riots resolved the instant they were created

Two separate faults, and the first is a clean decode correction.

**`FUN_004c9f10` is a `void` function — it cannot fail.** When its square scan finds nobody, or
the whole crowd is calm, it writes `bearing = 0xffff` and `mean = 0` but **still reports the head
count**, and its caller `FUN_004cb480` (opcode 24) returns `1` regardless. The *only* thing that
makes opcode 24 fail is `FUN_004a9230(0x1000)` finding no live riot record.

The port had `MeasureBehaviorCrowd` return false on `AgitationSum == 0`. That deadlocked every
riot:

```
rioter spawns with agitation 0
  -> BHAV 850 'Riot!' calls 852 'Refigure riot val and turn to it'
  -> 852 record [2] is opcode 24, which "failed" -> retF, skipping its `speed += 1`
  -> BHAV 850 then calls 311 'Rioter maybe leave riot'
  -> 311 [0] `speed < 3` is TRUE -> posts EVT_RioterDispersed -> deactivate
```

so all ~16 rioters retired on their first behaviour tick, `RiotersDispersed` reached `RiotSize`,
and the mission completed before the player saw it.

**The second fault is a gap I could not close from the executable.** Even with opcode 24 fixed,
852's servo only moves agitation one step per pass toward `count * mean / 15`, which is also zero
while the crowd is calm - and 311 runs on the same loop. So a rioter must *start* at 3 or above.
Where the original gets that first push is not pinned:

- not in `FUN_004a7a10`'s `0x1000` branch (it only loops `FUN_004c3eb0(-1, 3, tx, ty, event, 0, 0)`
  and counts the survivors),
- not in `FUN_004c3eb0`,
- not in `FUN_004c4e60` (join-a-riot posts event 0x0b and sets state 3, nothing else),
- and **person + 0x150 is never written by a literal offset anywhere in `.text`** - the field is
  only reachable through the behaviour VM's attribute table, so a byte scan finds nothing.

Ported as an explicit remake-side bootstrap, `SimCopterMissions::RioterSpawnAgitation = 3`: the
threshold 311 tests, which a crowd of ~15 sustains at riot value 3 so the servo holds rather than
fights it. Flagged in the header as unresolved.

## 2. Fire damage was three times too fast

`StepTurbulence`'s fire arm charged `SubstepFrameFraction(Dt, ReferenceFrameSeconds)` - and the
pawn ships `ReferenceFrameSeconds` at **1/60**, not the original's 1/20. `ReferenceFrameSeconds`
is a *feel* knob (the neutral-collective climb decay, the attitude EMA window) that the debug
panel dials in; how much a fire costs is fidelity. Pinned to `OriginalFrameSeconds`.

## 3. Tool muzzles

`FUN_00484d20` launches every emitter from **the body node lifted 3.0 units**, one point for all
of them. That is only right in the original's frame: the remake's `ModelPivot` is the *capsule*
centre and `LoadHelicopterMesh` pushes the fuselage down from it (`-CapsuleHalfHeight -
BodySection.LocalBounds.Min.Z`) so the skids meet the ground - so "pivot + 3 units up" comes out
level with the rotor mast. That is where tear gas canisters appeared to be thrown from.

One shared `ResolveToolMuzzle` now serves the water cannon, the tear gas launcher and both Apache
weapons: the CANNON barrel tip when the cannon is fitted, the fuselage nose off `BodySection`'s
own bounds otherwise, and the original's pivot-relative point only as a last resort.

## 4. BRACKET (0x16c) — the harness mount

`heli[0x31]`. `FUN_00483c20` builds it for every helicopter and **nothing in `.text` ever writes
`heli + 0xc4` again** (checked by scanning every `[reg + 0xc4]` in the whole section: ten hits,
none in the helicopter code). So like CANNON it is authored in the fuselage's own frame and simply
rides the body - a triangular frame on the right flank, which is the side a winched Sim comes
aboard. Ported as a body-parented mesh shown with the harness, and the rope now hangs off its
underside instead of a point under the belly.

## 5. Apache armament

| | Missile | Machine gun |
| --- | --- | --- |
| Pool | `DAT_005d4900`, 10 | `DAT_005d4f30`, 70 |
| Model | GEO `0x0ae` | 3-point `0x17` trajectory card, palette cycling `0x10..0x1f` (`DAT_00504558`) |
| Speed | `heli[0x4e] + 0x1c20000` (450 u/s) | `heli[0x4e] + 0x2580000` (600 u/s) |
| Cooldown | `0x10000`, **shared with tear gas** | none |
| Sound | 6 MISSILE, one shot | 5 MACHGUN1, a LOOP started once |
| Muzzle card | kind 1 | kind 4 |
| Reaction | mode 3 | mode 7 |
| Impact | `FUN_004af100(..., 2, ...)` + sound 7 | `FUN_004af100(..., 0x80000001, ...)` + sound `0x10`, or `8` into a body |

Both fly at a **constant speed with no drag and no gravity** - the tear gas and debris pools are
the ones that arc - and both are in `FUN_00490690`'s `0x4006` despawn set, so the first thing
either touches is the last.

The missile is the only projectile that starts fires: a terrain hit where `FUN_004a5f60` says the
tile will burn opens a fire object (`FUN_004a5080` + `FUN_004a5340`, event `0x35`), throws a
scale-4 column instead of the scale-2 air burst, and scatters `3 + rand % objectHeight` type-4
debris emitters at `0x640000`. Ported through the mission actor's existing
`CanIgniteCrashSite` + `CreateMissionAt(TYPE_BuildingFire)`, which is the same path a crashing
plane uses.

## Still open

- The rioter agitation seed (section 1).
- The debris count's `objectHeight` term - the remake has no per-object height at the impact, so
  it uses the same floor of 3 and a comparable spread.
- Apache mount points: section 3 covers all four weapons' launch point, but nothing in the
  executable moves a weapon to a wingtip or a nose cannon, so there is nothing further to find.
