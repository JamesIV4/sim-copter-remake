# Why passengers board instantly — the shipped timings vs. the remake

Dumps: `passenger_bhavs.txt` (750, 291, 290, 700, 305, 303), `medic_bhavs.txt`.
State -> program table: `FPeopleBehaviorModel::GetStateProgramIds` (DAT_0058de80, FUN_004c3010).
State 4 transport -> **750**, states 1/2 rescue -> **700**, state 6 medevac -> **800**, state 5 medic -> **801**.
VM tick rate: `ASimCopterGroundAgent::BehaviorTickRate` = **15 Hz**, matching the original.

## What the shipped transport program actually does

**BHAV 750 'Transport initbhav'**

    [0] l0 := 40  ->  [6] op0 wait l0--   ->  [1] bind 'NoMo' -> [2] attr14 += 1 -> [4] CALL 291

`op0` decrements once per VM tick, so that is **40 ticks = 2.67 s standing still** before the
passenger looks for anyone at all.

**BHAV 291 'Transport go to avatar/get on heli'** — the loop:

    [0] movespeed := 16      -> [10] autoturn := 1 -> [11] l3 := 0 -> [21] failcount := 0
    [1] l0 := rand(40)       -> [9] l0 += 30          ; 30..69 step budget, RE-ROLLED EVERY LOOP
    [2] op15 class 2 (player helicopter) within 1 TILE   T->[5] op12 walk AND board
                                                        F->[3]
    [3] op15 class 9 (player avatar)     within 4 tiles  T->[7] op38 walk toward (no board)
                                                        F->[19] fail counter
    [6] op13 outcome 2 (tertiary coords) -> [4] bind 'WvNo' -> [18] CALL 1100 'Idle-5'
                                          -> [8] CALL 290 boredom -> [15] l3 > 20 ? autoturn 1 : 0
                                          -> back to [1]

Two ranges matter and they are different: **boarding is gated at one tile**; the four-tile arm only
walks toward the player and **waves** (`WvNo`), idles 5 ticks and increments boredom.

`movespeed := 16` is 16/12 original units per tick = 1.33 units = 8.33 cm at 15 Hz:

> **125 cm/s**, in one of 8 octant directions, re-faced each tick, with autoturn retries.

## What the remake does instead

`ASimCopterMissionSystemActor::ProcessPassengerTransfers` runs **every mission tick** and calls:

- `GuideMissionPeopleToLocation(EventId, CabinMidpoint, CabinMidpoint, Seats,
  **PassengerPickupRadiusCm = 780**, PassengerTransferMaxVerticalDeltaCm,
  **PassengerBoardGuidanceSeconds = 0.45**)`  -> `ASimCopterGroundAgent::SetGuidanceMoveTarget`
- `BoardMissionPeopleTouching(..., **PassengerBoardTouchRadiusCm = 130**, ...)`

And in `ASimCopterGroundAgent::UpdateMovement` (~line 4443):

```cpp
const bool bUsingGuidanceTargetAtStart = IsGuidanceMoveTargetActive();
if (bBehaviorActive && Pedestrian && !IsAvoidanceMoveActive() && !bUsingGuidanceTargetAtStart)
{
    ... the BHAV per-tick constant-velocity walk ...
    return;
}
```

**A live guidance target skips the BHAV movement branch entirely.** The agent then falls through to
the generic seek-the-target mover, which runs at `MovementSpeedCmPerSec` — set from
`ASimCopterTrafficSystemActor::PedestrianSpeedCmPerSec` = **230 cm/s** (clamped 130..520).

## The gap, in numbers

| | shipped | remake | ratio |
|---|---|---|---|
| delay before the passenger reacts at all | 2.67 s (BHAV 750) | unchanged, but irrelevant — guidance moves them regardless | — |
| range that starts the approach | **400 cm** (1 tile, BHAV 291 rec[2]) | **780 cm** | 1.95x |
| approach speed | **125 cm/s** (`movespeed := 16`) | **230 cm/s** | **1.84x** |
| path | 8 octants, re-faced per tick, autoturn retries | straight-line beeline | — |
| step budget | `rand(40) + 30`, re-rolled each loop | none | — |
| loiter between attempts | `Idle-5` + wave (`WvNo`) + boredom (BHAV 290) | none — guidance refreshed every tick | — |
| the board itself | op 12 walk-and-board on contact | auto-board within **130 cm** | — |

So the report "they get in before I can set down" is three separate things at once: the trigger
range is doubled, the walk is 1.84x too fast and dead straight, and every randomised/idle arm of
BHAV 291 is skipped because guidance pre-empts the VM's own movement.

## Note on the vertical gate

Separately, `CanTransferMissionPassengers` was relaxed on 2026-08-05 (by request) from "Parked" to
`GroundClearance <= tolerance + PassengerTransferClearanceCm (60 cm)`. That is intended and is a
different axis from the timing above, but it is part of why boarding begins before touchdown.
