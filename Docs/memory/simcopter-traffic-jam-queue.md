# SimCopter traffic jams: cars queue, they do not manoeuvre

*"A blocked car does not move, and only same-direction traffic blocks."*

*Recorded 2026-08-06.*

Ported as `SimCopterTrafficJam` (`Public/Ground/SimCopterTrafficJam.h`) plus
`ASimCopterTrafficSystemActor::ApplyTrafficJamQueue`. Tests: `SimCopter.Traffic.JamBlockerDirection`,
`SimCopter.Traffic.JamQueueSpacing`, `SimCopter.Traffic.JamHornThresholds`.

## The two facts the whole thing rests on

`FUN_0049be50` is the per-tick vehicle update; the step it wants to take goes to **`FUN_0049ee30`**,
the move core, which answers with a result code and the blocker.

1. **A blocked car does not move.** Any result but 0 skips the advance outright. `FUN_0049be50`
   accumulates the held time at `veh+0xaf` and does *nothing else* until 20 s (`0x140001`), when it
   reroutes (`FUN_0049ea70`) or — for the emergency pools only — rolls
   `1-in-(0x40 >> difficulty)` for a jam mission. **There is no nudge, no lane change and no
   reverse anywhere in it.** Cars stack nose to tail behind whatever stopped.

2. **Only same-direction traffic blocks.** `FUN_0049ee30` takes the dot product of the two cars'
   16.16 heading vectors and uses it twice: the effective blocking radius is scaled by
   `(1 + dot) / 2`, and the forward probe also requires `veh[0x127] * 0x2666 + 0x8000 <= dot`
   (i.e. `dot >= 0.5 + 0.15 * yieldCount`). **An oncoming car has a blocking radius of zero and is
   ignored entirely**, so a queue down one side never stops the other side — and two cars sharing a
   heading can never resolve each other away and swap places.

Other numbers off the same pair:

* Probe point = `pos + heading * (radius + 5 units)`; the tile lookup takes 8 more. So the resting
  centre-to-centre gap is `myRadius + 5 units + leaderRadius` — **98.75 cm** for two stock cars at
  the remake's 33.75 cm capsule and 6.25 cm/unit, comfortably clear of the ~70 cm rendered body.
* `veh+0x127` is an **intersection yield ticket**: a car that defers increments it, which loosens
  its own blocker test. That is the original's entire anti-deadlock mechanism.
* Horn: `veh+0xaf > 0x50000` (5 s held) rolls `rand() & 0x3f`; a car flying the **jam flag 0x200**
  takes its own branch at the top of the function and rolls `rand() & 0xf` every update with no
  time gate. The remake had only the second one.

## What was wrong in the remake

Nothing *created* the pile-up on purpose — three separate systems did between them:

* `ApplyVehicleFollowing` never reaches zero. Inside the stop distance it returns **0.18** (or 0.12
  in the slow band), so a queue creeps into the car in front until every car is standing in the
  same place. It is the only speed rule ordinary traffic has, and it cannot express a stop.
* `ResolveVehicleOverlaps` split the separation push between both cars **and added a velocity
  impulse**, which walked the stopped lead car down the road and conveyor-belted the jam with it.
* `UpdateVehicleBlockageRecovery` (`bLongBlockedInJam` — the name says it) backs a car up, offsets
  it laterally and drives it round the blocker. That is a remake invention with no counterpart in
  `FUN_0049be50`, and it is what let cars pass each other in a jam.
* The **stoplight queue** (`ApplyTrafficLights`) is the wrong shape for a jam even when it works:
  it drives every car on an approach to a slot measured off one shared stop line, so they converge
  on the same few metres instead of trailing back from where the jam actually is.

## The port

`ApplyTrafficJamQueue` runs **first** in `UpdateTrafficInteractions`, in both AI modes, and owns the
cars in a jam for the whole frame. Everything after it only ever *lowers* a speed scale, so its
braking survives; the passes that would move a car sideways all check `IsVehicleHeldInTrafficJam`.

1. Early-out if no `bMissionJammed` car exists — **a tick with no jam costs one map scan and changes
   nothing**, which is what keeps this scoped to jams.
2. Find each car's blocker: nearest car ahead within `TrafficJamQueueLookAheadCm`, gated on the dot
   cone and on a lane width scaled by `GetHeadingBlockScale`. Also gated on height, so a jam on a
   bridge deck cannot stop the road underneath.
3. Breadth-first from the jammed cars over the reverse edges. Each car has at most one blocker, so
   the relation is a forest and the walk reaches exactly the queue.
4. Each follower brakes against **the car in front of it**, not against the jam. That distinction is
   the whole fix: measuring against the jam's own position makes every car want the same road.

Supporting changes: `ApplyVehicleFollowing` skips `bJamQueued` cars entirely (its creep is what
closed the queue up); `ResolveVehicleOverlaps` pushes only the **follower** and adds no impulse when
either car is held; `UpdateVehicleBlockageRecovery` and `ApplyTrafficLights` both skip held cars; a
car joining the queue has any bypass in flight cancelled.

**The one divergence is deliberate and named in `GetQueueSpeedScale`:** the original is physics-less
and stops dead, but the remake's cars carry velocity, so the approach is a quadratic ramp over
`TrafficJamQueueSlowDistanceCm` instead. It reaches zero at the original's own spacing.

## Traps

* **`TrafficFlowMode` is never assigned at runtime** — `ESimCopterTrafficFlowMode::TrafficJam` is
  dead. It is an editor-only toggle, and `TrafficAiMode` defaults to `Original`, where the stoplight
  queue and blockage recovery never ran in the first place. Do not read a jam's behaviour off it.
* `bMissionOnFire` also sets `bMissionJammed`, so **a burning car forms a proper queue too**. That
  is correct.
* Emergency vehicles are not in `VehicleAgents` (they are the dispatch pools), so the jam queue does
  not include them and they still drive through. Pre-existing, unchanged, and worth knowing before
  chasing "the ambulance ignored my jam".

Related: [[simcopter-people-logic-next]] (the traffic AI port), [[simcopter-emergency-dispatch]],
[[simcopter-sound]] (the horn ids).
