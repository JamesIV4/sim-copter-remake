# The "Check-up" repair/refuel menu — decoded

Ground truth for the missing repair/refuel dialog. Everything below is read off the decompile; no
inference. Addresses are `SimCopter.exe` VAs as Ghidra names them.

## Where the text lives

Win32 `RT_STRING` resources, English = language 1033 (same place as the hangar shell text — see
`Docs/memory/simcopter-hangar-shell.md`; these are NOT in the Ghidra `.rdata` export):

| id | string |
| --- | --- |
| 590 | `Check-up` (dialog title) |
| 591 | `Funds:` |
| 592 | `Total Cost:` |
| 593 | `Damage` (slider label) |
| 594 | `Fuel` (slider label) |
| 595 | `Teargas` (slider label) |
| 596 | `OK` |
| 597 | `Cancel` |
| 598 | `Cost:` |

## Dialog construction — `FUN_00443c20`

Builds the panel: title 590, then labels 591/592/593/594/595, three `slidchk.bmp` sliders
(`FUN_0040af00`, control ids 3, 4, 5) and two buttons (`FUN_0043b240`, ids 1 = OK / 596,
2 = Cancel / 597). Each slider's range is seeded from its own max function, then `FUN_00444690`
refreshes them and `FUN_004447e0` recomputes the totals.

**Slider units:** the Damage and Fuel sliders are denominated in DOLLARS. The Teargas slider is
denominated in CANISTERS, converted at `FUN_0048a570(n) = n * 50`, i.e. **$50 per canister**.

## Slider maxima

Per-type tuning is a 0x5c-byte stride table indexed by the helicopter's runtime type
(`*heli` = type index):

| symbol | meaning |
| --- | --- |
| `DAT_0050412c[type]` | max damage |
| `DAT_00504130[type]` | 16.16 **dollars per damage point** |
| `DAT_00504120[type]` | fuel tank capacity, 16.16 gallons |
| `DAT_00504134[type]` | 16.16 **dollars per gallon** |
| `heli[0x34]` | current accumulated damage (int) |
| `heli[0xcc]` | current fuel, 16.16 gallons |

`FUN_0046c49d(a,b)` is a 16.16 MULTIPLY (`a*b >> 16`); `FUN_0046c4bf` is the matching divide.

```
FUN_0048a380 (damage slider max, in dollars):
    n = Mul1616((maxDamage - damage) << 16, dollarsPerDamage) >> 16
    if (!AtAirport) n *= 3
    return n

FUN_0048a480 (fuel slider max, in dollars):
    n = Mul1616(capacity - fuel, dollarsPerGallon) >> 16
    if (!AtAirport) n *= 3
    return n

FUN_0048a560 (teargas slider max, in canisters) = 10       // flat cap of 10
```

`AtAirport` is `FUN_004823a0(heli.x, heli.y, 0xf6, 2) != 0` — the helicopter is standing on an
airport tile. XBLD 0xf6 is the airport stamp (see `Docs/memory/simcopter-airport-spawn.md`).
**Servicing anywhere other than the airport costs triple.**

`FUN_00444690` re-seeds the three sliders each refresh, clamping negatives to 0, and only enables
the teargas slider when the tool is fitted (`tools[0x48] & 8`), with max `10 - tools[0x54]`.

## Reading the sliders — `FUN_00444640`

```
out[0] = damageSlider     // dollars
out[1] = fuelSlider       // dollars
out[2] = FUN_0048a570(teargasSlider)   // canisters * 50 = dollars
```

`FUN_004447e0` shows `Total Cost:` as the sum of those three and `Funds:` from `FUN_00407a70()`.

## OK — `FUN_004385c0`

```
FUN_00444640(&cost);
if (cost.damage > 0) { funds -= cost.damage;  FUN_0048a3e0(heli, cost.damage); }
if (cost.fuel   > 0) { funds -= cost.fuel;    FUN_0048a4e0(heli, cost.fuel);   }
if (cost.gas    > 0) { funds -= cost.gas;     FUN_0048b130(cost.gas);          }
if (anyApplied) FUN_004c1ea0(appliedCount);   // feedback
```

`FUN_00407a90(-x)` is the funds adjustment.

```
FUN_0048a3e0 (apply repair):
    if (!AtAirport) dollars /= 3                     // <-- the triple price is undone here
    damage += Div1616(dollars << 16, dollarsPerDamage) >> 16
    if (damage > maxDamage) damage = maxDamage
    if (damage < 4)         damage = 0               // a near-complete repair snaps to zero

FUN_0048a4e0 (apply fuel):
    fuel += Div1616(dollars << 16, dollarsPerGallon)
    if (fuel > capacity) fuel = capacity
    // NOTE: no AtAirport division here, unlike the repair path. The original triples the fuel
    // slider's MAXIMUM off-airport but converts dollars to gallons at the base rate, so the
    // tripling is toothless for fuel - the player simply moves the slider less far. Port as-is.

FUN_0048b130 (apply teargas):
    tools[0x54] += dollars / 50
```

## When it is offered — `FUN_00444750`

Called every tick from `FUN_00449850` (twice, for two different input results from
`FUN_00412fa0()`); if it returns non-zero the game opens the dialog via `FUN_00438540`.

```
if (DAT_00503aa0 != 3) return 0;          // view mode 3 (DAT_00503aa0 is the VIEW mode, not a
                                          //  difficulty flag - see the flight-model memory note)
if (heli[0x330] == 0) return 1;
if (!AtAirport) return 0;                 // must be standing on airport XBLD 0xf6
if (damageCost < 0x15 && fuelCost < 0x15) {
    if (teargasFitted && teargasCount < 5) return 1;
    return 0;
}
return 1;                                 // >= $21 of repair or fuel is worth offering
```
