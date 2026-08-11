# Pacing divergences: mission clock and arsonist cadence

*Decided 2026-08-11. Both are deliberate playability changes, both were made knowing the retail
values, and both are recorded here so nobody "fixes" them back by citing the decompile.*

The house rule is that this port reproduces the original. These two are the exceptions, and the
reason in each case is that faithful reproduction produced a mission the player cannot engage with.

## 1. Every difficulty tier gets tier 1's mission clock

`FSimCopterMissionSystem::UpdateSchedulerCadence` no longer scales `ScaledMissionTimer` by tier. The
retail scaling, preserved in the comment there:

| tier | mission timer | nag interval (`timer >> 3`) |
|---:|---:|---:|
| 1 | 600.0 s | 75.00 s |
| 2 | 450.0 s | 56.25 s |
| 3 | 400.0 s | 50.00 s |
| 4 | 300.0 s | 37.50 s |

Reproduced faithfully, the harder cities read as missions expiring almost as fast as they arrive —
because the tier ALSO raises the concurrent mission cap (`MaxEasy + tier`), so more jobs and less
time to reach each one compound. Every tier now runs on 600 s / 75 s.

Difficulty still escalates through everything it touches that is not the clock: concurrent mission
count, the fire target filter (`IsBuildingFireTargetAllowedByDifficulty` — tier 1 is 1x1 shacks,
tier 4 is occupied towers), victims per record, the crime mix, and the car-fire→medevac rolls. This
removes timer pressure only, not difficulty.

## 2. The arsonist throws on a timer, and an eligible site burns

Retail leaves it to BHAV 1078's `rand(1000) < 6`, once per walk cycle, then `1 in (8 - tier)` on the
burnout. Against the measured behaviour tick (see [[simcopter-people-logic-next]] — the VM runs on a
0.08 s accumulator, ~12 Hz, not per frame) that is roughly one throw every 7-8 minutes and a fire
every half hour. An Arsonist mission is supposed to be a race.

`ASimCopterGroundAgent::UpdateArsonistThrowSchedule` runs a clock. Every
`FRandRange(ArsonThrowIntervalMinSeconds, ArsonThrowIntervalMaxSeconds)` = **50-100 s** he plays the
`"Thro"` clip and `ASimCopterMissionSystemActor::StartArsonistFire` sets a nearby building alight.
No projectile, no 60 s burn, no ignition roll — the window is aimed at the Robber's recurring
`Burglary Committed!` (the 75 s nag interval, now uniform across tiers per divergence 1) and is
redrawn each time. A caught, carried or move-suspended arsonist is skipped.

**The whole firebomb chain is left intact and faithful** — `ThrowArsonistFirebomb`, the 60 s burn,
the `1 in (8 - tier)` roll and both of [[simcopter-burning-debris-spread]]'s arms. It simply is not
what an Arsonist mission depends on any more. BHAV 1078's own `rand(1000) < 6` can still throw one.

Two traps this hit on the way in, both worth remembering:

- **It must be scheduled above the behaviour-tick gate.** `UpdateOriginalBehavior` returns early
  when the accumulator has not reached a VM step, which at 60 fps is three frames in four. A
  wall-clock countdown fed `DeltaSeconds` from underneath that gate only sees a quarter of the
  elapsed time and takes four times as long as it says — which looked exactly like "the timer is up
  and nothing happens".
- **Bind the clip directly, not through `Context.PendingAnimMnemonic`.** That field is consumed
  inside the VM-tick path; a throw scheduled on an off-tick frame either never reaches the consumer
  or gets overwritten by the VM's own bind first. `RebuildFigureClip(TEXT("Thro"))` is the call.

Because the interval is the fire cadence directly, those two tunables are the only knob.

Related: [[simcopter-crime-rooftop-rescue]], [[simcopter-mission-system]],
[[simcopter-burning-debris-spread]], [[simcopter-people-logic-next]].
