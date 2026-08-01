# Points bar, helicopter collision, impact FX and fire damage — decode (2026-08-01)

Four separate bugs, three of which turned out to share a root cause: a decoded rule that was
ported only half way. Raw dumps beside this file: `FUN_004529d0.txt` (dashboard child loader),
`FUN_004521a0.txt`, `FUN_004af100.txt` (impact column), `FUN_0048ad50.txt` (object collision),
`FUN_004a5c10.txt` (fire probe), `MANAGGE.png`.

---

## 1. The points bar is a three-state strip, not one block

`managge.bmp` is **15x13**, and — exactly like `watergge.bmp` — it is three 5x13 cells:

| cell | x | look | meaning |
| --- | --- | --- | --- |
| 0 | 0..5 | bright gold (`#98b4c8`-class highlights, idx 219/217) | filled |
| 1 | 5..10 | darker amber (idx 210/208) falling to near-black at its right edge | leading edge |
| 2 | 10..15 | flat `#1c1c1c`/`#2c2c2c` (idx 49/50) | empty |

The remake was stamping the **whole bitmap** five times at a 15px stride, so every "block" showed
all three states side by side and only five of them existed.

The repaint is at `0x004534f2`, inside the same unexported gap after `FUN_00454ee0` that the flap
methods live in — `ghidra-bridge decompile` produces incoherent output for this whole region, so
read the bytes:

```
004534f2  mov edi, 0x14                        ; dest cursor x = 20, steps +5
004534f7  call 0x407ac0 -> [esi+0x184]         ; score
00453502  call 0x407b30 -> [esi+0x188]         ; points needed
0045350d  if (score > needed) score = needed   ; clamped before dividing
00453528  lea ecx,[eax+eax*2]; lea eax,[ecx+ecx*4]
0045352f  idiv [esi+0x188]                     ; value = score * 15 / needed
0045353b  loop 1  value times   src (0,0)-(5,13)    full
00453562  loop 2  once, if n<15 src (5,0)-(10,13)   leading edge
0045358c  loop 3  15-n times    src (10,0)-(15,13)  empty
```

so **fifteen** 5x13 cells from page (20, 0x25=37), spanning x 20..95 — which is what fills the
second black well (measured x 19..96, y 36..51). The blit at vtable `+0xc` takes an *exclusive
source rect*, not a width/height, which is why all three cells come out 5 wide.

This is the same routine as flap0's water gauge (`0x00455700`), down to the loop shape, so the
rule now lives once in `UI/SimCopterSegmentedBar.h` and both bars supply their own geometry.

## 2. The original has ONE collision system and it never blocks

`FUN_0048ad50` is the helicopter's object collision, and it is an **overlap test, not a sweep**:

```
for obj in tile[heli.x][heli.z].objects:
    skip obj == heli body, obj flag 0x20 (unless 0x1000), obj flag 0x40
    AABB: |heli.x - obj.x| < obj.r  and  obj.y <= heli.y <= obj.y + obj.height
          and |heli.z - obj.z| < obj.r
    -> FUN_0049a4f0(0xc, heliBody, obj, -1, 0)      ; mode 0xc = BHAV 912, the airframe
                                                     ; running people and cars over
    -> returns 1 unless the object is flagged 0x28
```

and its caller (`FUN_00484d20`, after `LAB_00485605`) responds with damage, `FUN_004af100`, sound
1, random attitude kicks and `ClimbSpeed = ClimbRate * 4`. The other impact arm — the height test
`altitude < heli[0x59]` — responds with damage, the *directional* kick away from the motion
vector, `ClimbSpeed = ClimbRate * 4` and `heli[0x44] = 0x3333` (a 0.2 s control cut).

**Neither one ever stops the helicopter.** `FUN_00484d20` writes the simulated position into the
node unconditionally; contact is found afterwards and the response always throws the airframe
clear. That is why the original cannot wedge you against anything.

The remake substituted a swept capsule against real geometry for the tile-object boxes — a fair
trade, since the remake has real buildings — but it kept the sweep *blocking*, and it wrote the
blocked position back into the flight model. So the model believed it was inside the wall, kept
steering into it, and the capsule kept refusing: the aircraft was pinned with no damage, no
sound and no way out. Worse, the response was gated on `FMath::Abs(hit.Normal.Z) < 0.6`, which
- dropped every roof, slope and overhang, and
- treated a *ceiling* (normal.Z ≈ -1, abs = 1) as a floor, so coming up under something was the
  quietest way to get stuck.

Fixed by keeping the sweep as a **detector only**: any blocking hit whose impact normal is not
floor-like raises `NotifyObjectCollision`, and the position is then handed straight back to the
flight model. Floors stay excluded because `StepGroundImpact` already owns anything you can land
on, through `Env.SurfaceHeight`.

## 3. The missing crash sound and explosion

Both impact arms end with the same two calls the remake never made:

```
FUN_0042a2a0(1, 0)                                  ; sound 1 = EXPLODE, 2D
FUN_004af100(tile, x, y, z, 0x80000001, 0xffffffff) ; the impact column
```

The sound *was* wired — `PlayFlightEventAudio` plays `SND_EXPLODE` on `bGroundBounce ||
bPadBounce` — but `bPadBounce` was only ever raised by the collision path that almost never
fired, so in play there was silence. Fixing §2 restores it.

The column was simply absent: only `bCrashed`, `bGroundBounce` and `bSplashBounce` spawned
anything. `FUN_004af100` sizes itself `4 << (param5 & 0x1f)`, so `0x80000001` is a **scale-1**
burst (8 units), not the scale-4 splash a ditching throws; the high bit rides along into
`FUN_004af3b0`'s sub-particle ring, where `(int)0x80000001` being negative makes the ring sparse.
`SpawnSplashColumn(location, 1)` reproduces the size and the `0x140000 << 1` particle speed.

## 4. Fire damage never ran at all

`FSimCopterFlightModel::StepTurbulence` has the whole decoded fire arm — the damage accrual, the
`MaxFireAlt - delta` per-frame cost, the proximity shake — and it is correct. It is fed by
`FSimCopterFlightEnvironment::FireHeightDelta`, and **`BuildFlightEnvironment` never assigned that
field**, so it was always 0 and the arm was dead code. No fire damage, no shake near a fire, no
FIREDMG sound.

The input is `FUN_004a5c10(tileRecord, heliPos)`:

```
if (!(tile.flags & 0x20)) return 0                     ; tile is not burning
r = (1 - DAT_004f9740) * 0x80000 + DAT_00505f54        ; DAT_00505f54 = 0x180000 = 24.0 units
for flame in tile.fireList:
    if flame active
       and |heli.x - flame.x| < r
       and |heli.z - flame.z| < r:
        d = heli.y - flame.height - flame.y            ; height above the TOP of the flame
        return d != 0 ? d : 1                          ; 1, because 0 means "no fire"
return 0
```

- The gate is a **box**, not a circle, and it is tight on purpose: inside it `PerFrame = 61 - delta`
  hit points per frame at 20 fps, which kills a healthy airframe in a couple of seconds.
- `DAT_004f9740` ships as 2 and its meaning is not pinned, so the port takes the base 24.0 units
  and leaves the ±8.0 term out.
- The delta is measured from the flame's **top**. The port adds `FUN_004a47c0`'s 0x100000 render
  scale as the flame height, and takes the flame's drawn world position from the same helper the
  fire renderer uses, so the band the helicopter burns in is the fire the player can see.

## 4b. Three follow-ups, found on screen after the first pass

The first fix was necessary but not sufficient. Verified in-game and corrected the same day:

- **The normal threshold was still wrong at 0.6.** The right number is `LandingFlatNormalZ`
  (0.99) - the flight model's *own* landing test. At 0.6 a sloped roof or hillside was "floor",
  so the swept hit was ignored; and the height test did not fire either, because the point
  directly under the actor origin was still clear while the skids were already in the slope. Net
  result: hit a slope at speed, nothing at all happens. No speed gate was added - the original
  has none, and `BounceTimer` already rate-limits.
- **The impact sound could never have played.** `SimulateFlightStep` called
  `PlayFlightEventAudio` *before* `ApplyFlightModelToActor`, which is where the swept collider
  raises `bPadBounce`; `Step()` then clears the event block at the top of the next frame. The
  visual pass runs after `ApplyFlightModelToActor` and did see the flag, which is exactly why the
  symptom read as "explosion but no sound".
- **The burst was metres below the contact.** It was spawned at `GetActorLocation()` (the capsule
  centre), then `SpawnSplashColumn` dropped it another 32 units for the water-column effect, then
  the sub-particle ring fell away under gravity from there. Now: `Hit.ImpactPoint` with a new
  `bSubmergeOrigin = false`.

## 5. Still open

- `DAT_004f9740` — the ±8.0-unit term in the fire radius.
- `FUN_0048ad50`'s `0x1000` object flag, which sets `DAT_00504058` on a hit.
- The remake raises the *elevated-surface* response (directional kick + control cut) for swept
  wall hits rather than `FUN_0048ad50`'s random-kick response. For geometry that would have been
  a height column in the original this is the closer of the two, and the control cut is what
  stops the player steering straight back into the wall - but it is a deliberate divergence.
