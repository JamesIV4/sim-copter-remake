# 2026-08-06 — getting out of the helicopter, and the roof medic at the parapet

Evidence for two reports:

1. *"People get out of the helicopter too quickly, giving no time to land first."*
2. *"When paramedics are not picking up a patient, they go to the edge of the roof every single
   time. I thought they were supposed to be near the middle and walk around."*

## Dumps

    Tools/re-agent/.venv/Scripts/python.exe Tools/people_bhav_dump.py \
        Reference/SimCopterOriginalGame/X/people.df 750 291 > transport_bhav_750.txt
    ... 303 > rescue_bhav_303.txt
    ... 801 > bhav_801.txt

(Note the path: `people.df` is under `Reference/SimCopterOriginalGame/X/`, not the root.)

## 1. The alight is six units, and it is never tested every frame

**BHAV 292 'Transport wait to get off'**, the whole program:

    [0] local0 := 10        ->2
    [2] wait(local0--)      ->1        10 ticks
    [1] CALL 264            ->3        'Face vs. speed/health', whose 261 'idle a bit' is 3 more
    [3] op15 class 0, 4     T->5 F->0  the mission's own destination tile, within four tiles
    [5] op17 alight         T->4 F->0
    [4] op13 outcome 1      ->6        delivered
    [6] deactivate

So the shipped game probes about every **thirteenth tick — 0.87 s at the VM's 15 Hz** — and a failed
probe goes back to rec[0] for another ten. **BHAV 700** does the same for rescues: it loops
305 (get on) -> 303 (get off) -> 267 'random motion'.

Op 17 is `FUN_004cb190` -> **`FUN_004c9bc0`**, whose last line is the height gate:

```c
iVar4 = FUN_004c82c0(person+0x1cc, person+0x1d0, person+0x1d4);
return (short)((uint)(*(int *)(param_1 + 0x1d0) - iVar4) >> 0x10) < 6;   // six original units
```

Six units is 37.5 cm, and for a rider that is **the aircraft's own height**: `FUN_004c6450` copies
the carrier's position onto the person every tick.

```c
iVar1 = *(int *)(param_1 + 0x1a0);            // carrier
if (iVar1 != 0) {
  *(int *)(param_1 + 0x112) = *(int *)(iVar1 + 0x18);
  *(undefined4 *)(param_1 + 0x116) = *(undefined4 *)(iVar1 + 0x1c);
  *(undefined4 *)(param_1 + 0x11a) = *(undefined4 *)(iVar1 + 0x20);
}
```

And a **landed** helicopter is 1.2 units up, from the flight model's own park:
`Altitude = TerrainHeight + 0x13333` (`FUN_00487160`, ported in `StepGroundImpact`). So the shipped
allowance over a landed aircraft is 4.8 units, 30 cm.

Boarding is the looser of the pair: opcode 12's `FUN_004ca940` accepts while
`(objectY - personY) & 0xffff0000 < 0x50000` — five units over a walker who is themselves standing
`+0x30000` (three units, `FUN_004cb190`) above the ground, so **eight units, 50 cm**, of
aircraft-above-ground.

The remake had one gate for both at `GroundContactTolerance (28) + PassengerTransferClearanceCm (60)`
= **88 cm**, and the mission tick released passengers the first frame it was satisfied.

`GroundClearanceCm` needs no conversion: `ApplyFlightModelToActor` writes
`ActorZ = Altitude * 6.25 + CapsuleHalfHeight` and `UpdateGroundProbe` subtracts the same half
height back off, so it *is* the original's aircraft-above-ground figure in cm.

## 2. BHAV 801 walks exactly once — the remake replays it

    [0] bind 'NoMo' ->5   [5] attr32 := 916 ->2   [2] attr14 := 0 ->4
    [4] autoturn := 0 ->6
    [6] CALL 1015 'Walk-10'  ->7          <-- ENTRY PATH ONLY
    [7] CALL 1101 'Idle-10'  ->11
    [11] CALL 261 'idle a bit' ->10
    [10] CALL 272 'nearest emerg veh on stack' ->1
    [1] op25(209) T->3 F->9 ... [8] 263 ->11   [9] 262 ->11

The steady-state loop is **11 -> 10 -> 1 -> ... -> 11**. `Walk-10` is not in it. It is ten ticks at
`movespeed := 10`, which is `10/12 * 6.25 cm` per tick — about **52 cm, once, with autoturn off**.

In the original that is the whole life of the worker: 272 reaches 265 'Medevac disappear' within
seconds and it stops existing. The remake must keep the post staffed, so `UpdateOriginalBehavior`
refuses the despawn and `ResetToState`s — which re-enters at rec[0] and **re-runs the entry walk**,
in the same facing every time, roughly once a second. Eleven or so restarts is 5.7 m: the medic
marches in a straight line to the containment limit and stands at the parapet.
