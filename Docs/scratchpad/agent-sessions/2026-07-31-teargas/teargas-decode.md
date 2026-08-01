# Tear gas - decode notes (2026-07-31)

Everything here was read out of `SimCopter.exe` this session. Raw dumps sit beside this file:
`FUN_0048e0b0.txt` (emitter spawn), `FUN_0048ed00.txt` (master emitter tick), `FUN_00490690.txt`
(projectile collision), `FUN_004af220.txt` (tile effect card), `FUN_00484d20.txt` (helicopter
master tick), `FUN_0048db20.txt` (pool construction), `FUN_004127d0.txt` / `FUN_00454ee0.txt`
(cockpit flaps), `flap_0x455170.asm` (flap child construction) and `FLAP*.png` / `FLAPBTN*.png`
(the artwork, nearest-neighbour upscaled).

Prior pass: `Docs/scratchpad/ghidra/heli_tools_models_decode_20260724.md` section 8. This note
adds what that pass left open and corrects two claims.

---

## 1. The muzzle - `FUN_00484d20` (was an open item)

`heli[0x57]` is the emitter request written by the control reader. Its consumer is the tail of
the helicopter master tick:

```
if (heli[0x57] != 0) {
    speedBonus = type 1 -> 0x1c20000 (450.0)   missile
                 type 2 -> 0x2580000 (600.0)   machine gun
                 type 3 -> 0x0320000 ( 50.0)   TEAR GAS
                 type 5/6 -> water, and heli[0x74] loses DAT_00504030 >> 1
    body = heli[0x29]
    pos  = { body.x, body.y + 0x30000, body.z }          // 3.0 units above the body node
    FUN_0046cb74(&DAT_004fa2e0, dir, body + 0x24)         // DAT_004fa2e0 = (0, 0, 1.0)
    FUN_0048e0b0(heli[0x57], &heli[6], &pos, dir, 1, heli[0x29], heli[0x4e] + speedBonus, -1)
}
```

So the launcher has **no aim of its own**: the canister leaves along the airframe's forward axis
at the helicopter's own forward speed plus 50 units/s, from three units above the body. Size
param is 1 (node scale 1.0) and the mission event id is -1.

`FUN_0048db20` writes `node + 0x10 = 0x30000` for the tear gas pool. That field is the
**collision radius** used by `FUN_00491370`, not a render scale - `FUN_0048e0b0` passes the render
scale separately as `slot[0x11] << 16`.

**Correction to the 2026-07-24 note:** type 3 calls `FUN_0046cad1` (identity), not `FUN_00467d30`
(orient from direction). The canister's node keeps an identity matrix for its whole flight; only
the missile is oriented.

## 2. The canister's flight - `FUN_0048ed00`

Per slot of `DAT_005d4bd0`, in this order:

1. `life -= dt`. At `life < 1`: if already burst, unlink and free; otherwise **burst** -
   `phase = 1`, `life = 0x1e0000` (30 s), `timer = 0`, `Play3D(0x18 TGPOP, node position)`.
2. Motion, shared with the debris pool: `speed -= Mul(0x28f, speed)`, `v = dir * speed`,
   `v.y -= Mul(0x280000, dt)`, then renormalise - the length becomes the new speed.
3. `timer -= dt`. At `timer < 0`:
   - phase 0: `FUN_004af220(tile, pos, 4)` smoke card, `timer = 0x8000` (0.5 s)
   - phase 1: offset `pos` by `(0x14 - rand() % 0x28) * 0x10000` on **X and Z**, drop
     `FUN_004af220(offsetTile, offsetPos, 9)`, then for **every person object on that offset
     tile** call `FUN_0049a4f0(5, gasNode, person, slot[0x10], 0)`; `timer = 0x4ccc` (0.3 s)
4. `FUN_00490690` against the old tile, then the new one if it changed. On a hit:
   `speed = Mul(0xc20c, speed)` (~76 %) and `Play3D(0x16 SOFTBMP2)` when what is left is over
   `0x140000` (20.0 units/s).
5. `posY = max(posY, FUN_004aea90(x, z))` - the canister is lifted back to the terrain rather
   than sinking through it.

Because the burst sets `timer = 0` and step 3 runs after it, **the pop and the first puff of gas
are the same frame**.

## 3. Collision - `FUN_00490690`

The canister's class flag is `0x8`.

- Object loop: flag 8 maps to interaction mode `0xe` (BHAV 910 `Rxn: Debris stuff hit`). The
  despawn set is `0x4006` (missile / machine gun), so a body it strikes takes the reaction and the
  canister **carries on through**.
- Mesh loop: `0x798` includes `0x8`, so the direction is reflected about the surface and the
  function returns 1 - it bounces rather than breaking.
- Terrain: class `< 10` (water / open country) with no `0x380` bits gives splash card 8 plus
  `Play3D(0x0f DOUSE)` and despawns it. Otherwise it reflects about the up normal, but **only
  while `dir.y < 0`** - a canister on the way up is left alone.

## 4. Why this is what ends a riot mission

`X/people.df`, BHAV **907 `Rxn: Teargas`** (mode 5's entry in `DAT_0058d728`):

```
[0] attr14 += 1                -> 19 'Fall off master' -> [1]
[1] state == 3 (rioter)?  T -> [18] 'rand 1 in 6?'  T -> [3] speed += 5
                                                    F -> [2] speed += -2
                          F -> [2] speed += -2
... threat probe, face away, 'Thro' ...
[10] local0 := rand(0..20)
[11] local0 == 0 ? T -> [13] CALL 906 'Rxn: Swoon'      (1 in 21: collapses)
                   F -> [16] save movespeed, := 20, CALL 287 'Rioter flee tree', restore
```

`speed` (+0x150) is a rioter's agitation. BHAV **311 `Rioter maybe leave riot`**, called every
loop of BHAV 850 `Riot!`:

```
[0] speed < 3 ?  F -> retT (stay)
                 T -> threat probe -> side-effect 4 (EVT_RioterDispersed)
                                   -> side-effect 5 (EVT_RioterCalmed)
                      then attr19 := 0 and deactivate
```

So gassing a crowd walks each rioter's agitation down by two a puff until it drops below three,
at which point they post the dispersal event and leave. All of that is data - the remake's
behaviour VM already runs it. The only missing link was that **nothing ever delivered
interaction mode 5**, because tear gas never spawned anything.

## 5. The cockpit counter - flap3

`FUN_004127d0` builds the flaps; the flap class's own methods are not in `.ghidra-exports`
(Ghidra folded them into the gap after `FUN_00454ee0`), so they were read out of the bytes.
Vtable `0x4f3150`:

| Slot | Address | What |
| --- | --- | --- |
| +0x04 | `0x00455140` | reset the two cached readouts (`this+0xc4`, `this+0xc8`) |
| +0x10 | `0x00455170` | build the button child from `flapbtn0/1/2.bmp` (+ `watergge.bmp` on flap0) |
| +0xd0 | `0x00455300` | **per-frame tick** |
| +0xdc | `0x00455400` | pressed-sprite rects per flap and button |

`0x00455300`, every fourth call:

- flap **0**: `value = heli[0x74] * 11 / maxLoad[type]`, and on a change call `0x00455700` - the
  eleven-step water bar printed at the bottom left of flap0, drawn from `watergge.bmp`.
- flap **3**: `value = career[0x54]`, and on a change call `0x00455790`.

`0x00455790` paints ten 4x4 dots:

```
x = 18 + 12 * (i % 5)
y = 12 + 13 * (i / 5)
i < 10 - rounds  ->  src (34,4)-(38,8)   dark   (spent)
otherwise        ->  src (34,0)-(38,4)   pale   (loaded)
```

both out of `flapbtn1.bmp`'s right-hand 4-pixel sliver (the strip past its two octagons, whose
purpose was previously unknown). `flap3.bmp` already prints all ten lamps **pale** - the pixels at
(18,12) are byte for byte the source sprite - so the original only ever paints the dark ones, and
the row empties from the left.

## 6. The water gauge - flap0

The counter's sibling, off the same tick and ported the same day.

`0x00455700` walks one dest cursor `x = 16`, stepping `+5`, all on `y = 0x2b` (43), blitting 5x10
cells from `watergge.bmp` - which is exactly **15x10**, i.e. three cells - through the child on
`this+0xbc` (`0x00455170` loads `watergge.bmp` there for flap0 only, the button sheet on
`this+0xc0`):

```
loop 1   while (level > n)   src (0,0)-(5,10)     full water
loop 2   once, if n < 11     src (5,0)-(10,10)    the meniscus
loop 3   11 - n times        src (10,0)-(15,10)   empty
```

Eleven cells always, so the row spans page x 16..71. Note the blit takes an exclusive source
**rect**, not a width/height - that is what makes all three cells 5 wide rather than 5, 10, 5.
The palette confirms the reading: cell 0 is pale blue (`#98b4c8` highlights), cell 1 falls to
`#082438` at its right edge, cell 2 is flat `#1c1c1c`/`#2c2c2c`.

**`flap0.bmp` ships the gauge EMPTY**, not full: its pixels at (16,43) are byte for byte the
*meniscus* cell and everything right of it is the empty cell. That is the opposite of flap3, and
it means a partly full tank must paint over the printed meniscus - this gauge cannot be drawn as
an overlay of a single state the way the canister lamps can.

The divisor `*(0x5040e8 + type * 0x5c)` is the per-type max load, which `heli.twk` overwrites, so
the port divides by the tuning value rather than the static table's.

## 7. Still not ported

- The Apache's own muzzle offsets. Section 1 covers all four weapons' launch point, but nothing
  in the executable moves it to a wingtip or a nose cannon, so there is nothing further to find.
