# The people move core's per-cell rules, and the guard that makes them survivable

*Decoded 2026-08-12 from `FUN_004c9470` (dumped to `Docs/scratchpad/ghidra/move_core_004c9470.txt`)
and `FUN_004c9cc0` / `FUN_004c92a0` (`water_cell_test_004c9cc0.txt`), after a Robber mission spawned
a criminal who stood on the spot and never did anything.*

## The one line everything hangs off

`FUN_004c9470` wraps its **entire** tile-class block in

```c
if (person+0x12a != newCellX || person+0x12c != newCellY) { ...all the per-cell rules... }
*(int*)(person+0x1cc) = newX; ... return 0;   // otherwise: written straight through
```

`+0x12a` / `+0x12c` are the walker's current cell. **A step that does not leave the cell it started
in is never tested at all.** That guard is not an optimisation, and reading it as one is the whole
bug: one behaviour tick moves `MoveSpeed / 12` original units — `Walk-30` sets 10, so **about 5 cm**
— against a **400 cm** tile. Every one of the eight facings normally resolves to the cell the walker
is already standing on.

Evaluate the rules per step instead and *"the cell I am standing on is not one my row allows"*
stops being a boundary you cannot cross and becomes a **permanent freeze**: all eight facings
refused, every tick, forever. `MoveStep` did exactly that, which is why:

- a **Robber / Arsonist / Mugger stood still and did nothing.** The on-foot crime placer wants an
  XBLD building tile, then `TrySpawnMissionPerson`'s outward ring search moves the person to
  wherever the capsule is clear of building geometry — up to `footprint + 2` cells away — and that
  can easily be bare ground (people class 2), a tree (3) or the small park (5). None of those is in
  a criminal's row `{13,11,10,12,7}`. They still ran BHAV 1300 -> 1174 the whole time: idle, random
  turn, `Walk-30` refused, repeat. Turning on the spot is exactly what it looks like.
- the arsonist "throws from the same place every time" note in
  [[simcopter-crime-rooftop-rescue]] is the same walker, same cause.
- the road rule needed its own `bAlreadyOnRoad` escape hatch (see
  [[simcopter-vehicle-knockdown]]) — that escape was this bug, found from the other end and patched
  for one class instead of fixed at the gate.

`ArePedestrianCellRulesEvaluated` is now the gate, and `SimCopter.People.MoveCoreCellRules` pins
both it and the arithmetic that makes it matter.

## What the rules actually are, in order

Inside that guard, for a person with no master (`person+0x1a0 == 0`):

1. **`FUN_004c9cc0` — the water test. Runs for everybody, ahead of the ambient split.**
   `FUN_004c92a0` is "terrain class < 10", which is exactly `IsWaterTerrainClass`. A water cell is
   refused (move result `0xb`) *unless* the walker is currently standing on something more than
   `0x140000` — **20 original units, 125 cm** — above the terrain beneath them: a bridge deck or a
   pier. Note it measures where the walker **is**, not where the step lands.
2. `FUN_004c9dc0` is a **no-op for a person** — it returns 1 on `param_2 != 0` before looking at
   anything. Do not port it as a rule.
3. **The `+0x168` (AmbientFlag) split, and this is the part the port had backwards.** The flag is
   written by the shipped DATA, not by any C++: `BHAV 600 'Ambient initbhav'` rec[6] sets it to 1,
   `BHAV 1401 'Cop foot'` rec[1] and `BHAV 1400 'Cop aerial'` rec[0] clear it, and `BHAV 287` saves
   and restores it around a rioter's flee. That closes the open question in
   [[simcopter-vehicle-knockdown]].
   - **ambient walkers** get the `DAT_0058ec00` row test, indexed by `person+0x146 * 10` (behaviour
     class, confirmed here), plus a per-class occupancy cap `DAT_0058d6d4[class*3]`, with a
     fallback that also passes if the walker's *old* class is in the row. Result 3 otherwise.
   - **everybody else gets NO tile-class test whatsoever.** The arm has one rule in it: leaving a
     road (`old class == 7 && new != 7`, gated on `+0x166`) returns `0xc`.

**`DAT_0058d750` — `FSimCopterBehaviorVM::GetAllowedTileClasses` — is not referenced by the move
core at all.** Using it here was a remake invention ("the safety net"), and it is what bound the
class rows to mission people who in retail have none. `IsAmbientStreetWalker()` now decides, and it
is the same population the road rule already used: no `MissionEventId`, not emergency crew, not
roof crew, not mid-BHAV-308 escape.

Because water and bare ground are **both people class 2**, the safety net had been keeping mission
walkers out of the sea *by accident*. Dropping it therefore required porting rule 1 for real —
`IsStandingOnRaisedDeck()` plus `IsWaterTile` — or criminals would walk into the ocean.

## The goto-object walk on top of it (`FUN_004ca940`, opcodes 12 and 38)

Same session, the *next* report: after an arrest the policeman and the robber set off for the
police car and then both froze mid-street. The walk itself is one record — `local0 := 100` then
op 38 — and its tail is the thing to know:

```c
facing = FUN_004c8430(selection);          // bearing to the object, re-taken EVERY tick
person.facing = (facing - 2) & 7;
moveResult = FUN_004c9300(moveSpeed, ...);
if (moveResult == 10 && |selection.Y - person.Y| < 0x50000) return 1;   // arrived
return -(moveResult == 0) & 2;             // 2 = keep going, 0 = FALSE
```

**Only a clean step continues the walk.** Any other move result — 1/2 climb or drop, 3 a cell it may
not enter, 4 an object, 5 another body — ends it on the spot and the caller's false edge decides
what happens next. The port returned "still moving" whatever `MoveStep` said, so a walker stopped by
the first thing in its path stood against it in silence for the rest of its budget: 100 ticks, near
seven seconds, of a program that thought it was walking.

And it is the *first* thing, because `FUN_004c9300`'s eight-facing retry loop is gated on
`person+0x16a` (autoturn), which is clear by default and which only four shipped programs ever set —
274, 291, 273 and 444. A goto-object walk therefore aims one facing straight at its target and takes
what it gets. The other person walking to the same car counts.

Two more remake-side faults in the same walk, both now fixed:

- **The arrival gate's vertical half was measured off a collision capsule.** The horizontal half had
  already been moved onto the rendered body (a car's traffic capsule is a 33.75 cm radius on a body
  three times as long); the vertical half was still `actorOrigin.Z - GetSimpleCollisionHalfHeight()`,
  and `InitCapsuleSize` clamps that half height *up* to the radius. For a vehicle whose origin sits
  on the road that puts the "doorsill" 33.75 cm underground, against a gate of 5 original units =
  31.25 cm. Off by two and a half centimetres, permanently, silently: the walk arrives, is told it
  has not, and stands at the door. `TryGetBodyBottomWorldZ` answers it from the drawn box now.
- **Outcome 9 is the fourth "unresolved mission person" trap.** BHAV 1060 posts it, walks the
  arrested criminal to the car and ends on op 40, and the despawn refusal would `ResetToState(10)`
  them into BHAV 1300 — whose rec[7] is `attr23 := 0`, *clearing* CriminalCaught. An arrested robber
  would stand back up uncaught. Being caught is that person's resolution; it sets the flag now, like
  a rioter's outcome 4/5.

**What is NOT a bug, so nobody re-fixes it:** when the walk does fail, both programs (1060 rec[7],
1150 rec[12]) fall into `is the player's helicopter within 10 tiles ? Idle-20 : op 40`. Standing
still while you hover and vanishing once you leave is shipped behaviour, not a stall — the original
gets away with it because the car it walks to is the one that dropped the officer off, a tile or two
away, and 100 ticks at movespeed 15 covers about two tiles. `log LogSimCopterGroundAgent Verbose`
prints the move result, the remaining gap and the height gate for every blocked goto-object step,
which is what to read before assuming anything else.

## Trees are no longer in anybody's way either

Same session, same report: the tree instances are now an object channel of their own so nothing on
foot collides with them, and they are out of the `ECC_Camera` probes that decide where a person may
walk. See the foliage section of [[simcopter-instanced-buildings]] — it matters here because a
trunk was one more thing that could refuse a facing.

## If a walker freezes again

The order to check, cheapest first:

1. Which cell are they on, and is it one their row admits? If the answer matters, the gate above has
   regressed — it should not be consulted at all while they stay put.
2. `IsPedestrianStepBlockedByGeometry` (an `ECC_Camera` body sweep) — a real wall, a pier, a
   parapet. This one *does* run every step, correctly: the original's object overlap and climb
   tests are outside the cell guard too.
3. The climb/drop gate against `TryGetWalkSurfaceZAt`.
4. The hospital roof post containment, if they are state 5.
