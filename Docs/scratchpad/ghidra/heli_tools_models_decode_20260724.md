# Helicopter Tools, Models, and Targeting - Decoded Notes

Date: 2026-07-24

Owner plan: `Docs/HelicopterToolsAndModelsDecompilePlan.md` (Phase 0 evidence pack).

Raw decompiles this note is derived from:

- `out_heli_tools_decompile_20260724.txt` - career/equipment ownership, catalog,
  cockpit indicators, global command dispatch (`FUN_0044ac80`), control reader
  (`FUN_00485f50`).
- `out_heli_tools_decompile2_20260724.txt` - spotlight (`FUN_00489250`), aim
  (`FUN_00489730`/`FUN_00479060`), spiral interaction scan (`FUN_0048ae70`),
  megaphone (`FUN_0048a800`), object-class router (`FUN_0049a4f0`), people
  reaction (`FUN_004c1050`), vehicle reaction (`FUN_0049f680`), model
  construction (`FUN_00483c20`), rotor/tail (`FUN_00487740`), winch
  (`FUN_00487bb0`), master tick (`FUN_00484d20`).
- `out_heli_tools_decompile3_20260724.txt` - emitter spawn (`FUN_0048e0b0`),
  emitter update (`FUN_0048ed00`), projectile collision (`FUN_00490690`),
  mission/score event (`FUN_004a89c0`), person master attach (`FUN_004c6360`),
  people runtime init incl. the reaction table (`FUN_004c3010`), harness
  opcode handlers.
- `out_heli_tools_decompile4_20260724.txt` - emitter pool class flags
  (`FUN_0048da50`) and pool construction/meshes (`FUN_0048db20`).
- `out_heli_statics.txt` - raw bytes of the per-type static block at
  `DAT_005040e4`.
- `out_vm_handlers.txt` - behavior-VM thunk table disassembly (used to resolve
  opcode 87).

Status keys follow `Docs/DecompilationWorkflow.md`: `Confirmed`, `Hypothesis`,
`Follow-up`.

---

## 0. Function hashes

SHA-256 (first 16 hex) of each `.ghidra-exports/<addr>.json` at the time of the
pass. Refresh after re-running `ghidra-bridge export decompiled`.

| Address | Hash |
| --- | --- |
| `0x00407a50` | `D9A1A1AC8E6FE1FB` |
| `0x00407a70` | `04ECA18CE1DEFBA0` |
| `0x00407a90` | `D5418CE98129A0F5` |
| `0x004077f0` | `5E77C7D6BF8158C8` |
| `0x004127d0` | `F31A7D93EBD09462` |
| `0x0042cad0` | `F067BFF670974F78` |
| `0x0042d420` | `434A8CBDE34AFBD0` |
| `0x0042d840` | `DBF8AC64C95A37A5` |
| `0x0042d9f0` | `0C4EC4CCCD4EE135` |
| `0x00444690` | `A43AC6B3046D0879` |
| `0x00444750` | `1F1CF6855B5A15A5` |
| `0x0044ac80` | `0405CF3483C99A42` |
| `0x00479060` | `2BCF19D5ACD01E02` |
| `0x00483c20` | `5600DC59649FA436` |
| `0x00485f50` | `67E0E4CB8A8DEDF2` |
| `0x00487740` | `6C53456FA17B17D6` |
| `0x00487bb0` | `C21CA9586DCBBAE8` |
| `0x00489250` | `3887496504870679` |
| `0x00489630` | `70E5A61BD1A1632F` |
| `0x00489730` | `98988F13AD10E9BA` |
| `0x0048a800` | `F8A8A596FFCFF9AA` |
| `0x0048ae70` | `B328A617F42A9008` |
| `0x0048b050` | `CECF4736EEEA67F3` |
| `0x0048b070` | `D1E4FF4AD4A42FDC` |
| `0x0048b0f0` | `685C5E69E0F6F090` |
| `0x0048b130` | `93C935FD134186E3` |
| `0x0048b150` | `ADDB96DC17B2CE71` |
| `0x0048b1a0` | `0F73DCB918D1B0A4` |
| `0x0048da50` | `41C43E254308B474` |
| `0x0048db20` | `F13A467F7ED5206D` |
| `0x0048e0b0` | `5D32471CC93D4587` |
| `0x0048ed00` | `3820EF017FA15898` |
| `0x00490690` | `878C2DADAA2FA722` |
| `0x0049a4f0` | `90523EECC5BA047A` |
| `0x0049f680` | `8B65C3D7072798D9` |
| `0x0049fc10` | `53509E7AED88EEC0` |
| `0x004a89c0` | `B419DDEFFBD5DBD7` |
| `0x004c1050` | `3C479E88BE749EE3` |
| `0x004c3010` | `56C8A41C4A01DCF3` |
| `0x004c6360` | `DAAE868C06836798` |
| `0x004ca940` | `324A28600359C98F` |
| `0x004cac70` | `1803B499DE4DE648` |
| `0x004cc900` | `8414F058D0DC70F3` |
| `0x004cca60` | `FE0F2C43567C3AC6` |
| `0x004ccad0` | `514B266375EFBA42` |
| `0x004cccd0` | `7FF2D4C28D8DB7A9` |
| `0x004cce30` | `ECDA25A893833F25` |
| `0x004cce50` | `181B3F5A36EAEC51` |
| `0x004cceb0` | `E71161B1648CF4E8` |

---

## 1. Career equipment ownership - Confirmed

`FUN_00407a50` returns the active career record: `&DAT_00518cf8` when
`DAT_00518d50 == 1`, otherwise `&DAT_00518d6c` (two save slots / two careers).

Career record fields used by the tool layer:

| Offset | Meaning | Evidence |
| --- | --- | --- |
| `+0x40` | Money (clamped at 0) | `FUN_00407a90` add/subtract, `FUN_00407a70` read |
| `+0x44` | Helicopter ownership bitmask, bit = runtime type index | `FUN_0042d840` / `FUN_0042d9f0` / `FUN_0042d420` |
| `+0x48` | Equipment ownership bitmask | same |
| `+0x54` | Tear gas rounds | `FUN_0042d840`, `FUN_0048b130`, `FUN_0048e0b0` |

### Equipment bit assignment - Confirmed

The shop's list order is not the bit order. `FUN_0042d840` maps catalog row
(`ui + 0x172`) to the equipment index with the literal table `{0, 1, 3, 4, 2}`,
then sets `career[0x48] |= 1 << equipIndex`.

| Equip index | Bit | Equipment | Buy price (`FUN_0048b0f0`) | Sell value (`FUN_0048b150` = 75%) |
| ---: | ---: | --- | ---: | ---: |
| 0 | `0x01` | Water bucket | 500 | 375 |
| 1 | `0x02` | Megaphone | 500 | 375 |
| 2 | `0x04` | Rescue harness | 800 | 600 |
| 3 | `0x08` | Tear gas launcher | 2500 | 1875 |
| 4 | `0x10` | Water cannon | 1500 | 1125 |

Buying equip index 3 also writes `career[0x54] = 10` (ten rounds); selling it
writes 0. Confirms the plan's mapping and closes the open question in
`Docs/WaterGameplayDecompilePlan.md`.

### Helicopter catalog order - Confirmed

`FUN_0042d840`/`FUN_0042d9f0`/`FUN_0042d420` map catalog row (`ui + 0x16e`) to
runtime type index with `{4, 0, 1, 8, 3, 5, 6, 7}` - eight civilian models,
Apache (type 2) intentionally absent from the shop. Helicopter price is
`*(int*)(&DAT_00504128 + type * 0x5c)`, i.e. `+0x44` of the per-type block.
Trade-in value (`FUN_0048b070`) is `price - depreciation`, floored at
`price / 2`.

`FUN_0048b1a0(type)` is the "take delivery" path: it sets `career[0x44] |= 1
<< type`, links the helicopter object at a free pad, and publishes it as the
active helicopter `DAT_005040d0`. Note this is a *helicopter* bit, not an
equipment bit - the two masks are separate.

### Snapshot into `DAT_00504060` - Confirmed

`FUN_00484d20` (the per-frame master tick) copies the career equipment mask into
`DAT_00504060` once per frame. **All in-flight capability tests read the
snapshot, not the career record.** Only the tear gas *ammo* test reads the
career record live (`FUN_00485f50` calls `FUN_00407a50()` and compares
`career[0x54] < 1`).

Port consequence: the remake needs three layers exactly as the plan says -
career mask (persistent), effective/flight mask (per-frame snapshot; the debug
grant overlay belongs here), and ammo.

### Missing-equipment messages and sounds - Confirmed

All failures use `FUN_0048c4c0(&msgBlock, 1, 0)` with message id in
`_DAT_00504014` and then `FUN_0042a1f0(0x80, heliPos, 0)` (or
`FUN_0042a2a0(0x80, 0)` from the global command dispatcher).

| Message id | Trigger |
| ---: | --- |
| `0x2a7` | Bucket/cannon selected but `heli[0x74]` (load) is 0 - out of water |
| `0x2a8` | Bucket controls used without bit `0x01` |
| `0x2a9` | Water cannon used without bit `0x10` |
| `0x2aa` | Megaphone command without bit `0x02` (`FUN_0044ac80`) |
| `0x2ab` | Harness controls used without bit `0x04` |
| `0x2ac` | Tear gas fired without bit `0x08`, **or** with `career[0x54] < 1` |

Sound id `0x80` is the shared "denied" cue.

### Cockpit and shop indicators - Confirmed

- `FUN_004127d0` builds four cockpit equipment flaps:
  `flap0.bmp` when `mask & 0x11` (bucket **or** cannon - the water flap is
  shared), `flap1.bmp` when `mask & 0x02`, `flap2.bmp` when `mask & 0x04`,
  `flap3.bmp` when `mask & 0x08`. Each is destroyed when its bit clears.
- `FUN_004077f0` draws per-helicopter shop rows and stamps the owned-equipment
  icons in the order harness (`0x04`), bucket (`0x01`), cannon (`0x10`),
  megaphone (`0x02`), gas (`0x08`).

### Maintenance refill - Confirmed

- `FUN_0048b130(dollars)`: `career[0x54] += dollars / 50`. Tear gas costs
  **$50 per round** at maintenance.
- `FUN_00444750` gates the maintenance option: requires `DAT_00503aa0 == 3`,
  a helipad/maintenance tile under the helicopter, and returns 1 when damage
  >= 0x15, fuel deficit >= 0x15, or (gas owned **and** `career[0x54] < 5`).
- `FUN_00444690` renders the bill: damage repair + fuel + `(10 - rounds)` gas.
  `FUN_0048a560` returns the 10-round capacity constant.

---

## 2. Canonical helicopter registry - Confirmed

### Render objects (`FUN_00483c20`)

The plan's table is confirmed byte-for-byte. Object ids are passed to
`FUN_00470571` (global object id -> mesh object).

| Type | Body | Main rotor | Body shadow | Rotor shadow |
| ---: | ---: | ---: | ---: | ---: |
| 0 | `0x076` | `0x117` | `0x159` | `0x160` |
| 1 | `0x116` | `0x078` | `0x158` | `0x15f` |
| 2 | `0x119` | `0x11a` | `0x15b` | `0x162` |
| 3 | `0x124` | `0x126` | `0x156` | `0x15d` |
| 4 | `0x125` | `0x127` | `0x15a` | `0x161` |
| 5 | `0x141` | `0x142` | `0x155` | `0x15c` |
| 6 | `0x153` | `0x154` | `0x157` | `0x15e` |
| 7 | `0x170` | `0x172` | `0x174` | `0x176` |
| 8 | `0x171` | `0x173` | `0x175` | `0x177` |

Any type outside 0..8 falls back to the Jet Ranger set.

### Scene node slots on the helicopter struct - Confirmed

`FUN_00483c20` allocates ten render nodes. Byte offsets are `4 * index`:

| Field | Offset | Contents | Sort class (`node+0xc`) |
| --- | ---: | --- | ---: |
| `heli[0x28]` | `+0xa0` | `CANNON` `0x16e` | `0x21` |
| `heli[0x29]` | `+0xa4` | Body (per type) | `5` |
| `heli[0x2a]` | `+0xa8` | Body shadow (per type), hidden at build | `0x21` |
| `heli[0x2b]` | `+0xac` | Main rotor (per type) | `0x21` |
| `heli[0x2c]` | `+0xb0` | Tail rotor `ROTORTL` `0x083` | `0x21` |
| `heli[0x2d]` | `+0xb4` | Rotor shadow (per type), hidden at build | `0x21` |
| `heli[0x2e]` | `+0xb8` | Rope chain object, scale `0x20000` | `0x21` |
| `heli[0x2f]` | `+0xbc` | Rope end (starts as `BUCKET` `0x07b`) | `0x21` |
| `heli[0x30]` | `+0xc0` | Spotlight `SPOTLITE` `0x118`, hidden at build | `0x21` |
| `heli[0x31]` | `+0xc4` | Equipment bracket `BRACKET` `0x16c` | `0x21` |

Two swappable rope-end *objects* are cached, not nodes:

- `heli[0x32]` (`+0xc8`) = `BUCKET` `0x07b`
- `heli[0x33]` (`+0xcc`) = `HARNESS` `0x16d`

The rope itself is `FUN_0046ec60(0x14, 0x78, DAT_00504068)` = **20 nodes**;
failure logs `"Could not create heli rope"`. `heli[0x62]` is the rope object,
`heli[0x64]` points at the last rope node's position and `heli[0x63]` at the
second-to-last; their difference is the rope-end direction used to orient the
bucket/harness (`FUN_00487bb0`).

### Per-type static block `DAT_005040e4 + type * 0x5c` - Confirmed

`DAT_005040e0 + type * 0x5c` (i.e. `-0x04`) is a per-type *instance counter*
incremented by `FUN_00483c20`; the tuning block proper starts at `+0x00`.

| Offset | Field | heli.twk control |
| ---: | --- | --- |
| `+0x00` | Passenger seats | (static only) |
| `+0x04` | Max load (lb) | Ctrl8 `Max Load` |
| `+0x08` | Max bank | Ctrl0 |
| `+0x0c` | Max slide | Ctrl1 |
| `+0x10` | Max pitch | Ctrl2 |
| `+0x14` | Max yaw rate | Ctrl9 |
| `+0x18` | Pitch rate | Ctrl3 |
| `+0x1c` | Yaw rate | Ctrl4 |
| `+0x20` | Roll rate | Ctrl5 |
| `+0x24` | Slide rate | Ctrl6 |
| `+0x28` | Climb rate | Ctrl7 |
| `+0x2c` | Tail rotor mount X | (static only) |
| `+0x30` | Tail rotor mount Y | (static only) |
| `+0x34` | Tail rotor mount Z | (static only) |
| `+0x38` | NOTAR flag (hide tail rotor) | (static only) |
| `+0x3c` | Fuel gallons | Ctrl13 |
| `+0x40` | Fuel rate | Ctrl10 |
| `+0x44` | New cost | Ctrl (New Cost) |
| `+0x48` | Max damage | Ctrl12 |
| `+0x4c` | Repair rate | Ctrl11 |
| `+0x50` | Fuel cost | Ctrl |
| `+0x54` | **Engine loop WAV name pointer** | (static only) |
| `+0x58` | unused / 0 | - |

Decoded static values (16.16 where noted; from `out_heli_statics.txt`):

| Type | Display | Seats | Tail X | Tail Y | Tail Z | NOTAR | Engine loop |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | Jet Ranger | 4 | 0.534 | 8.0 | -25.0 | 0 | `COPLOOP5.WAV` |
| 1 | Hughes 500 | 4 | 0.534 | 8.0 | -20.0 | 0 | `COPLOOP6.WAV` |
| 2 | Apache | 0 | 2.4 | 11.5 | -26.7 | 0 | `COPLOOP.WAV` |
| 3 | Bell 212 | 14 | -0.4 | 11.0 | -29.0 | 0 | `COPLOOP3.WAV` |
| 4 | Schwiezer 300 | 2 | -0.4 | 6.1 | -18.2 | 0 | `COPLOOP4.WAV` |
| 5 | Agusta | 7 | -0.4 | 10.0 | -23.0 | 0 | `COPLOOP6.WAV` |
| 6 | Dauphin | 13 | 0.0 | 5.5 | -22.11 | 0 | `COPLOOP.WAV` |
| 7 | MDEXPLORER | 7 | 0.0 | 5.5 | -22.11 | **1** | `COPLOOP2.WAV` |
| 8 | MD520 | 4 | 0.0 | 5.5 | -22.11 | **1** | `COPLOOP2.WAV` |

Seats and the NOTAR flags match the plan. `Follow-up`: the tail-rotor mount is
in original world units on the model's local axes; the exact axis convention is
inferred from `FUN_00487740` (which adds the offset to the body node position
before setting the tail node) and has not been visually validated per model.

Static defaults for the tweak-bound fields (max load 1000/2000, cost 50000,
damage 1000, etc.) are placeholders overwritten by `heli.twk`
(`FUN_00489e20`); the registry must not treat them as gameplay values.

---

## 3. Winch, rope end, and the bucket/harness exchange - Confirmed

Runtime state (int indices into the helicopter struct):

| Field | Offset | Meaning |
| --- | ---: | --- |
| `heli[0x6f]` | `+0x1bc` | Rope node cursor. **`0x11` (17) = fully stowed, `3` = fully lowered.** |
| `heli[0x70]` | `+0x1c0` | Bucket **stowed** flag (1 = up/stowed, 0 = deployed) |
| `heli[0x71]` | `+0x1c4` | Harness **stowed** flag (1 = up/stowed, 0 = deployed) |
| `heli[0x72]` | `+0x1c8` | Winch command: `+1` raise bucket, `-1` lower bucket, `+2` raise harness, `-2` lower harness, `0` idle |
| `heli[0x73]` | `+0x1cc` | Bucket dump held |
| `heli[0x74]` | `+0x1d0` | Load in pounds (bucket water) |
| `heli[0x57]` | `+0x15c` | Requested emitter type this frame (0 = none) |

`FUN_00487bb0` (winch step):

- command `< 0`: `heli[0x6f]--`, winch sound `0x15` starts. On reaching node
  `0x10` the rope node becomes visible, and **the rope-end object is swapped**:
  command `-1` -> `heli[0x70] = 0` and rope-end object = `heli[0x32]`
  (`BUCKET`); command `-2` -> `heli[0x71] = 0` and rope-end object =
  `heli[0x33]` (`HARNESS`).
- command `== 0`: stop winch sound.
- command `> 0`: `heli[0x6f]++`, winch sound `0x15` at pitch `0xa0`. On reaching
  node `0x11` both nodes are hidden and the matching stow flag is set
  (`+1` -> `heli[0x70] = 1`, `+2` -> `heli[0x71] = 1`).

The whole rope/rope-end update is skipped when both are stowed
(`heli[0x70] == 1 && heli[0x71] == 1`).

Bucket fill (unchanged from the water plan, restated for completeness): when the
rope end is within `0x20000` (2.0 units) of terrain height, the bucket is
deployed (`heli[0x70] == 0`) and the tile class is `< 10`, `heli[0x74] +=
DAT_0050402c` clamped to the per-type max load at `DAT_005040e8 + type*0x5c`.

### Control mapping (`FUN_00485f50`) - Confirmed

Capability gates read the per-frame snapshot `DAT_00504060`:

| Action ids | Gate | Behaviour |
| --- | --- | --- |
| `0x0b` / `0x0c` (+ analog pair) | `& 0x01` else msg `0x2a8` | `0x0b` = lower bucket: if the harness is out (`heli[0x71] == 0`) issue `+2` (stow harness first); else if `heli[0x6f] > 3` issue `-1`. `0x0c` = raise bucket: if bucket out and `heli[0x6f] < 0x11` issue `+1`. |
| `0x0e` / `0x0f` (+ analog pair) | `& 0x04` else msg `0x2ab` | `0x0e` = lower harness: if the bucket is out (`heli[0x70] == 0`) issue `+1` (stow bucket first); else if `heli[0x6f] > 3` issue `-2`. `0x0f` = raise harness: if harness out and `heli[0x6f] < 0x11` issue `+2`. |
| `0x0d` | `& 0x01` | Bucket dump. Refused when `heli[0x70] != 0` (bucket stowed) -> `heli[0x73] = 0`; msg `0x2a7` when `heli[0x74] == 0`; msg `0x2a8` when the bucket is not owned; else `heli[0x73] = 1`. |
| `0x02` | Apache override, else `& 0x08` + ammo | Type 2 -> `heli[0x57] = 1` (missile). Otherwise msg `0x2ac` when the bit is clear or `career[0x54] < 1`; else `heli[0x57] = 3` (tear gas). |
| `0x10` | Apache override, else `& 0x10` | Type 2 -> `heli[0x57] = 2` (machine gun). Otherwise msg `0x2a9` when the bit is clear; msg `0x2a7` when `heli[0x74] == 0`; else `heli[0x57] = 5` **and the pitch target takes a recoil kick**: `heli[0x47] -= Mul(dt, Mul(SlideRate, LoadFactor))` (halved in `DAT_00503aa0 != 0` mode). Releasing both sets `heli[0x57] = 0`. |

The bucket/harness "stow the other one first" cross-checks are the mechanism
behind the help text's "only one attachment at a time".

Actions `0x02` and `0x10` are level-triggered (held), not edge-triggered; the
cooldown is enforced downstream (`DAT_00504570`).

---

## 4. Spotlight target service - Confirmed (corrects a stale label)

`FUN_00489250` is the **spotlight**, not a rotor-downwash disc. The
"downwash disc" heading in `out_effects_DECODED.md` is wrong; rotor wash is
`FUN_004881b0` and bucket drip is `FUN_00488060`.

Per frame, when `heli[0x08] & 1`:

1. Show the `SPOTLITE` node (`heli+0xc0`) and copy the helicopter's orientation
   matrix (`heli+0x100`) into the node's matrix.
2. Transform the global aim vector `DAT_0057f230` by that matrix -> unit ray
   direction. Step vector = direction `<< 5` (32 units per step).
3. March at most `0x10` (16) steps; at each step test the current tile's object
   list and the next step's tile via `FUN_00489630` (object AABB/mesh ray test).
   Accumulate `0x200000` (32.0) per clear step. Maximum reach 512 units.
4. Clamp `> 0x1ffffff` to `0x1ff0000` (511.0).
5. Smooth while flying: `DAT_00504430 = (DAT_00504430 * 7 + raw) >> 3`.
6. Band from the smoothed distance:
   `<= 0x800000` (128.0) -> 0, `<= 0x1000000` (256.0) -> 1,
   `<= 0x1800000` (384.0) -> 2, else 3. Stored in `heli[0x150]`.
7. On band change, recolour every face of the spotlight cone from
   `(&DAT_005d91e0)[band]` (four palette entries fetched at construction via
   `FUN_0046cd20(DAT_005039ac, 8..0xb)`).
8. Place the light node at `body + dir * distance`; node scale =
   `max(0x4ccc, Div(distance, 0x2000000) * 10)` (min 0.3).
9. Ground tile = `(lightX + 0x20000000) >> 0x16`,
   `(0x20000000 - lightZ) >> 0x16`, published in `DAT_005d70f0/f4`.
10. `FUN_0048ae70(1, tile, spotlightNode, -1, band)` - the spotlight interaction
    scan.
11. In `DAT_00503aa0 == 3` (cockpit) the light node is hidden again after the
    target is computed - **targeting keeps running even when the cone is not
    drawn.**

Aim (`FUN_00489730`): `DAT_0050408c += dPitch`, `DAT_00504090 += dYaw`, both
clamped to `+/-0x1f40000` (`+/-500.0` tenth-degrees = `+/-50 deg`). The
direction is rebuilt as identity -> rotate X by `DAT_0050408c - 0x1680000`
(a fixed `-36 deg` base tilt) -> rotate Y by `DAT_00504090` -> transform
`DAT_004fa2e0`, storing `DAT_0057f230`. `FUN_00483c20` seeds the same chain with
zero aim, so the rest pose is 36 degrees below the helicopter's forward axis.

Aim input (`FUN_00479060`): actions `0x2e`/`0x2f` step pitch by
`-/+0x280000` (40.0 tenth-degrees = 4 deg) per frame, `0x30`/`0x31` step yaw the
same way; analog axes use the same magnitude. All spotlight aim input is
disabled in `DAT_00503aa0 == 3`.

### Spiral interaction scan (`FUN_0048ae70`) - Confirmed

`FUN_0048ae70(mode, tile, sourceObject, param4, param5)` walks a square spiral
over the `DAT_005d9200` tile grid: run length grows on the `-Y` and `+Y` legs,
terminating when it reaches **3 for mode 1 (spotlight)** and **5 for mode 2
(megaphone)**. Modes other than 1/2 return immediately. Each tile's object list
(`tileRecord + 0x10`) is walked; objects are skipped when they are the source,
or when `obj[0xc] & 4` (linked/child) or `obj[0xc] & 0x60` is set. Survivors go
to `FUN_0049a4f0(mode, sourceObject, obj, param4, param5)`.

---

## 5. Object-class router and reaction table - Confirmed

`FUN_0049a4f0(mode, source, target, param4, param5)` dispatches on
`target[0xc]`:

| Flag | Handler | Class |
| ---: | --- | --- |
| `0x20` | (ignored) | suppressed object |
| `0x04` | `FUN_0048b370` | helicopter |
| `0x08` | `FUN_004c1050` | person |
| `0x10` | `FUN_0049fc10` -> `FUN_0049f680` | vehicle |
| `0x80` | class 10 | (falls into the mode switch) |
| `0x100` | `FUN_004b3df0` | (building/service) |
| `0x200` | `FUN_004b2120` | (building/service) |
| `0x400` | class 0 | (falls into the mode switch) |

For classes 10 and 0 the mode switch only ever routes class `0xb` to the vehicle
handler, so those classes are inert for the tool modes in this plan.

### `DAT_0058d728[mode]` - Confirmed (dumped from `FUN_004c3010`)

`FUN_004c3010` fills 20 `u16` entries with `0xffff` then assigns:

| Mode | BHAV | `people.df` name |
| ---: | ---: | --- |
| 0 | 910 | `Rxn: Debris stuff hit` |
| 1 | 900 | `Rioter react to spotlight` |
| 2 | **901** | `Rxn: Megaphone` |
| 3 | 915 | `Rxn: Missile/bullet` |
| 4 | 908 | `Rxn: Water` |
| 5 | **907** | `Rxn: Teargas` |
| 6 | 913 | `Rxn: Fire/sparks hit` |
| 7 | 915 | `Rxn: Missile/bullet` |
| 8 | 911 | `Rxn: boat hit` |
| 9 | 912 | `Rxn: Large fast vehicle hit` |
| 10 | 912 | `Rxn: Large fast vehicle hit` |
| 11 | 912 | `Rxn: Large fast vehicle hit` |
| 12 | 912 | `Rxn: Large fast vehicle hit` |
| 13 | 914 | `Rxn: Person--civil, neutral` |
| 14 | 910 | `Rxn: Debris stuff hit` |
| 15 | 913 | `Rxn: Fire/sparks hit` |
| 16 | 909 | `Rxn: Fall` |
| 17-19 | `0xffff` | none |

Names verified against the shipped `X/people.df` BHAV directory
(`Tools/people_bhav_dump.py`).

`FUN_004c1050` (person reaction) overrides the table in two cases:

- mode 1 (spotlight): reaction is hard-coded BHAV **950** and only fires when
  `rng % DAT_0058dc3a == 0` (`DAT_0058dc3a` is written 65000 by
  `FUN_004c3010`; a smaller value is written elsewhere - `Follow-up`, confirm
  the write order before porting the probability).
- mode 13 with a non-zero `param5`: the reaction id is `param5` itself.

Acceptance tests before the reaction is pushed:

1. target is not the helicopter body (`DAT_005040d0 + 0xa4`);
2. `person+0x15e == 0`;
3. `person[0x52] != 6` (short);
4. `person[0x57] < 1` **or** the new reaction outranks the current one -
   ids 903 (`Rxn: Die`), 915 (`Rxn: Missile/bullet`), 912, 909 are the
   priority set that cannot be interrupted by a lower reaction;
5. `person+0x12e != 32000` (person is in a valid tile).

On success: `person[0x41] = DAT_00506448` (frame stamp), `person[0x69] =
source`, `person+0x158 = param5`, and **for mode 2 only** `person+0x15a =
param5` - this is where the megaphone message index is stored. The reaction id
goes to `person+0x17c` and the BHAV is pushed on the person's behaviour stack
(`FUN_004ce700`).

---

## 6. Megaphone - Confirmed

`FUN_0044ac80` (global command dispatcher) handles command ids `0x26`..`0x2a`:

1. Requires the in-cockpit context (`DAT_00503aa0 != 3 || DAT_0051ac74 != 0`).
2. `career[0x48] & 0x02` else message `0x2aa` + sound `0x80`.
3. `FUN_00424620(cmd - 0x26)` - plays the spoken message clip.
4. `FUN_0048a800(cmd - 0x26)` - broadcasts it.

Message index order (0..4) matches the plan: Report Traffic, Stop Criminal,
Evacuate, Disperse, Greet.

`FUN_0048a800(messageIndex)`:

- cockpit view (`DAT_00503aa0 == 3`): tile from the spotlight node position,
  `FUN_0048ae70(2, tile, bodyNode, -1, messageIndex)`.
- otherwise: **only when `heli[0x150] < 3`**, i.e. the spotlight target is
  inside 384 units. Tile comes from `FUN_004c1a10`/`FUN_004c1a20` and the source
  object from `FUN_004c1a30(-1, messageIndex)` (Ghidra dropped the trailing
  arguments to `FUN_0048ae70`; the call is the same 5-argument form).
  `Follow-up`: decode `FUN_004c1a10/20/30` to confirm the non-cockpit tile
  source.

So the megaphone is spotlight-directed and range-gated, with a 5-ring scan
(vs the spotlight's 3-ring). Per-message behaviour beyond storing
`person+0x15a` lives inside BHAV 901 and the mission/scoring layer -
`Follow-up`.

---

## 7. Rescue harness - Confirmed

Person-side entry points (verified names from `people.df`):

- BHAV **700** `Rescue new initbhav` - loop: 305 -> 267 (`random motion`) ->
  303 -> repeat.
- BHAV **305** `Rescue try to get on heli or bucket` (26 records).
- BHAV **303** `Rescue try get off heli or bucket if appropriate` (8 records).
- BHAV 1498 is the superseded initializer and must be ignored.

### Sparse opcode mapping - Confirmed

`FUN_004c3010` builds `DAT_0058ef78[op]`; opcode = `(slotAddr - 0x58ef78) / 4`.
The thunk table at `0x4c84e0` is contiguous but the opcode order is not.

| Opcode | Slot | Thunk | Handler | Meaning |
| ---: | --- | --- | --- | --- |
| 12 | `0x58efa8` | `0x4c85a0` | `FUN_004ca940` | Walk to the stack object; on arrival (and \|dy\| < 5.0) become its child via `FUN_004c6360`, snap position and tile |
| 15 | `0x58efb4` | `0x4c8600` | `FUN_004cac70` | Proximity/threat probe against the stack object (already ported as `threat-probe`) |
| 48 | `0x58f038` | `0x4c8a00` | `FUN_004cc900` | Set master to stack object, snap `person+0x1cc/1d0/1d4` |
| 53 | `0x58f04c` | `0x4c8aa0` | `FUN_004cca60` | Manhattan distance to the helicopter body `<= 0x18` (24 units) -> select it as stack object |
| 58 | `0x58f060` | `0x4c8b40` | `FUN_004cccd0` | `GetOnHeliIfHarnessRaised` (name from an assert string) |
| 59 | `0x58f064` | `0x4c8b60` | `FUN_004cce30` | Master is the helicopter body |
| 82 | `0x58f0c0` | `0x4c8e80` | `FUN_004ccad0` | Manhattan distance to the stack object `< 0x19` (25 units) |
| 86 | `0x58f0d0` | `0x4c8b80` | `FUN_004cceb0` | Master is the rope end (`DAT_005040d0 + 0xbc`) |
| **87** | `0x58f0d4` | `0x4c8ba0` | **`FUN_004cce50`** | **Resolved:** the person's stored destination tile record (`person+0x188/0x18a`) is the same tile record as its current tile (`person+0x12a/0x12c`) |

Opcode 87 was the plan's open item. It is the get-off condition: BHAV 303
record `[7]` runs it when `state == 2` and returns TRUE (release) only when the
helicopter has carried the Sim onto their destination tile.

### Attachment rules (`FUN_004c6360`) - Confirmed

`FUN_004c6360(person, newMaster)`:

- If the current master has `master[0xc] & 4`, play sound `0x3c`.
- `newMaster == heli+0xa4` (body): if `person+0x12e == 32000` call
  `FUN_004c1df0` (place into the world); else `FUN_004c6250()` must return
  non-zero, otherwise the attach is **refused**. This is the seat/capacity gate
  - `Follow-up`, decode `FUN_004c6250` to confirm it is the free-seat test.
- `newMaster == heli+0xbc` (rope end): refused when
  `person+0x148 != 0x0f` **and** (`heli[0x70] == 0` **or** `heli[0x71] != 0`),
  or when `FUN_004c6310(ropeEnd) != 0` (the rope end already has a rider).
  In words: a Sim may grab the rope end only when the **bucket is stowed and
  the harness is deployed**, unless the Sim is already in person-state 15.
- On success: `person+0x1a0 = master` and `person+0x112/0x116/0x11a` are set to
  the master's position.

### `GetOnHeliIfHarnessRaised` (`FUN_004cccd0`) - Confirmed

Assert strings recovered from `C:\Copter\Source\X\Y\Yobjsim.cpp` lines
0x94f/0x952/0x95c:

```
no master in GetOnHeliIfHarnessRaised
master is neither harness nor heli
bucket not raised - can ignore
```

Logic: if the master is not the rope end, return TRUE (already aboard). If the
harness is **not** stowed (`heli[0x71] == 0`), return TRUE (keep riding). When
the harness has been raised (`heli[0x71] != 0`), transfer the rider to the
helicopter body via `FUN_004c6360(person, heli+0xa4)` and return its result -
so a full cabin leaves the Sim on the rope end rather than dropping them.

This also independently confirms the `heli[0x70] == 1 means bucket stowed`
polarity ("bucket not raised" is logged when `heli[0x70] == 0`).

---

## 8. Emitters, tear gas, and Apache armament - Confirmed (corrects the plan)

### Pool class flags (`FUN_0048da50`) and meshes (`FUN_0048db20`)

Slots are `0x48` bytes (18 dwords). Bit 0 of `slot[0]` is "active"; the
remaining bits are a **persistent class tag** written once at init and never
cleared (impact only does `slot[0] &= ~1`).

| Pool | Slots | Class flag | Mesh / primitive | Node class | Scale |
| --- | ---: | ---: | --- | ---: | ---: |
| `DAT_005d4900` | 10 | `0x0002` | object `0x0ae` (Apache missile) | `0x2021` | `0x60000` |
| `DAT_005d4bd0` | 10 | `0x0008` | object `0x147` (tear gas canister) | `0x0821` | `0x30000` |
| `DAT_005d4ea0` | 2 | `0x0800` | object `0x07c` | `0x0021` | `0x60000` |
| `DAT_005d4f30` | 70 | `0x0004` | `FUN_0046edb0(3, 0x17)` tracer | `0x4021` | `0x140000` |
| `DAT_005d62e0` | 20 | `0x0100` | - | - | - |
| `DAT_005d6880` | 30 | `0x0010` | rotating debris set `DAT_00504560` | `0x0401` | `0x30000` |
| `DAT_005d41f0` | 25 | `0x0000` | - | - | - |

Slot layout used by the port:

| Index | Byte | Meaning |
| ---: | ---: | --- |
| `[0]` | `+0x00` | flags (bit 0 active, rest = class) |
| `[1]` | `+0x04` | life seconds (16.16) |
| `[2]` | `+0x08` | effect timer (16.16), starts `0x8000` |
| `[3]` | `+0x0c` | speed (16.16 units/s) |
| `[4..6]` | `+0x10` | unit direction |
| `[10]` | `+0x28` | render node |
| `[0xb],[0xc]` | `+0x2c` | tile x, y |
| `[0xd]` | `+0x34` | effect cycle index |
| `[0xe]` | `+0x38` | phase (0 = flying, 1 = detonated/gas) |
| `[0xf]` | `+0x3c` | owner object |
| `[0x10]` | `+0x40` | mission event id |
| `[0x11]` | `+0x44` | size/strength |

### `FUN_0048e0b0` spawn - Confirmed

| Emitter type | Pool | Sound | Life | Cooldown | Notes |
| ---: | --- | ---: | ---: | --- | --- |
| 1 Apache missile | `DAT_005d4900` | 6 | `0x50000` (5.0 s) | `DAT_00504570 = 0x10000` (1.0 s) | Refuses while the shared cooldown is > 0 |
| 2 Apache machine gun | `DAT_005d4f30` | 5 (looped, started once) | `0x50000` | none | Sets per-face palette `0x17` and cycling colour index `DAT_00504558` (16..31) |
| 3 Tear gas | `DAT_005d4bd0` | `0x17` | `0x50000` | `DAT_00504570 = 0x10000` | `career[0x54]--`, clamped at 0; `slot[0xe] = 0` |

The cooldown `DAT_00504570` is **shared between the missile and tear gas** and
is decremented by `DAT_005039a0` in `FUN_0048ed00`.

At the end of the spawn, every type sets `slot[2] = 0x8000`, `slot[1]` to its
type-specific life, copies direction and tile, and (for missile/gas) orients the
node from the direction vector. A muzzle effect card is registered at
`position + direction * 10` with kind `local_c8` (missile 1, machine gun 4,
tear gas 4).

### `FUN_0048ed00` update - Confirmed

Common motion for the tear gas and debris pools: `speed -= Mul(0x28f, speed)`
(1 % drag/frame), `dir.y -= Mul(0x280000, dt)` (40 units/s^2 gravity),
renormalise, advance by `Mul(speed, dt)`. The missile pool moves at constant
speed with no gravity.

Tear gas life cycle:

1. Phase 0 (0..5 s): flying canister. Every `0x8000` (0.5 s) drop an effect card
   of kind 4 at the canister. `FUN_00490690` collision applies (see below).
2. Life expiry with `slot[0xe] == 0`: **detonate** - `slot[0xe] = 1`,
   `slot[1] = 0x1e0000` (**30 s** of gas), `slot[2] = 0`, sound `0x18`.
3. Phase 1 (30 s): every `0x4ccc` (0.3 s), pick a random offset of
   `+/-20` units in X and Z, drop an effect card of kind 9 there, and for
   **every person object on that offset tile** call
   `FUN_0049a4f0(5, gasNode, person, missionEventId, 0)`.
4. Life expiry with `slot[0xe] == 1`: unlink and free.

**This is the plan's main correction: the tear gas people reaction is
interaction mode 5 (BHAV 907 `Rxn: Teargas`), not mode 7.** Mode 7 is what the
machine gun's class flag `0x4` produces in `FUN_00490690`, and mode 3 is the
missile's `0x2`. A tear gas canister that physically strikes an object produces
mode `0xe` (BHAV 910 `Rxn: Debris stuff hit`) from its `0x8` class flag.

### `FUN_00490690` collision - Confirmed

Class flag -> interaction mode:

| Slot flags | Mode | Meaning |
| ---: | ---: | --- |
| `& 0x900` | (scan skipped) | pools `0x100`/`0x800` never scan tile objects |
| `& 0x802` | 3 | missile |
| `& 0x004` | 7 | machine gun bullet |
| `& 0x4000` | `0x13` | (secondary machine-gun mode, emitter type `0xe`) |
| `& 0x008` | `0xe` | tear gas canister body |
| `& 0x410` | 0 | debris |
| `& 0x0e0` | 4 | water |
| `& 0x200` | `0xf` | (emitter type 9) |

Response by class:

- `& 0x802` (missile): `FUN_004af100(tile, dx, dy, dz, 2, eventId)` structural
  damage plus explosion sound 7. On a **terrain** hit it can start a fire
  (`FUN_004a5f60` says the tile is burnable -> `FUN_004a5080` +
  `FUN_004a5340`, and `FUN_004a89c0({0x35, -1, ..., 1, 0})` raises the mission
  event), then spawns `3 + rand % objectHeight` debris emitters (type 4) in
  random directions.
- `& 0x4004` (machine gun): sound `0x10` (or `8` against a person) plus
  `FUN_004af100(..., 0x80000001, eventId)`.
- `& 0x798` (includes the tear gas canister's `0x8`): the projectile is
  **reflected** off the surface it hit rather than destroyed, so canisters
  bounce until their fuse expires.
- Terrain hit over water/wilderness (`DAT_005bde80` tile class `< 10`) with no
  `0x380` bits: splash card 8, sound `0xf`, despawn.

Every path then calls `FUN_0049a4f0(mode, projectileNode, target, eventId, 0)`
so people/vehicles react.

### Apache action overrides - Confirmed

`FUN_00485f50` maps action `0x02` to emitter type 1 and action `0x10` to emitter
type 2 when `heli[0] == 2` (Apache). Neither path consumes tear gas ammo or
water: the ammo decrement lives in `FUN_0048e0b0` type 3 only, and the water
check/recoil live in the non-Apache branch of action `0x10`. Apache weapons are
therefore pure model capabilities and must never touch the career equipment
mask.

`Follow-up`: the exact Apache muzzle transform is not in `FUN_00485f50`; the
emitter caller (`FUN_00484d20` -> `heli[0x57]` consumer) has not been decoded in
this pass, so mount points are still unknown. The `CANNON` node `heli[0x28]`
is the water cannon's mount, not the Apache's.

---

## 9. Corrections to `Docs/HelicopterToolsAndModelsDecompilePlan.md`

1. Tear gas people reaction is interaction **mode 5** (BHAV 907), delivered by
   the 30-second gas cloud in `FUN_0048ed00`, not mode 7 via `FUN_00490690`.
   Mode 7 is the Apache machine gun; mode 3 the Apache missile; mode `0xe` the
   tear gas canister's physical impact.
2. Emitter type 3's ten-slot pool is `DAT_005d4bd0` (correct in the plan), but
   the tear gas cooldown constant `0x10000` is shared with the Apache missile
   through `DAT_00504570`.
3. `heli[0x70]`/`heli[0x71]` are **stowed** flags (1 = up), and the rope cursor
   `heli[0x6f]` counts **down** from `0x11` to `3` as the rope pays out.
   Command values are `+/-1` for the bucket and `+/-2` for the harness; the
   plan's "harness-specific +2/-2" is right but the polarity was unstated.
4. Harness opcode 87 = `FUN_004cce50` = "destination tile reached".
5. `FUN_00489250` is the spotlight; the "downwash disc" label in
   `out_effects_DECODED.md` is stale.
6. The equipment prices and the shop's index permutations were unknown and are
   now decoded (section 1).
7. The per-type static block has a tenth documented field: an engine loop WAV
   name pointer at `+0x54`.

## 10. Remaining unresolved items

- `FUN_004c1a10`/`FUN_004c1a20`/`FUN_004c1a30` - the non-cockpit megaphone tile
  and source-object source.
- `FUN_004c6250` - the presumed free-seat gate on boarding the helicopter body.
- `FUN_004c6310` - the presumed "rope end already occupied" test.
- ~~The consumer of `heli[0x57]` (emitter request) - muzzle transforms.~~
  **Resolved 2026-07-31**: it is the tail of `FUN_00484d20`. All four weapons launch
  from the body node lifted `+0x30000` along the airframe's forward axis
  (`FUN_0046cb74(&DAT_004fa2e0 = (0,0,1), dir, body + 0x24)`) at `heli[0x4e]` plus a
  per-type bonus - missile `0x1c20000`, machine gun `0x2580000`, tear gas `0x320000`.
  There are no per-weapon mount points. See
  `Docs/scratchpad/agent-sessions/2026-07-31-teargas/teargas-decode.md`, which also
  corrects two claims in section 8 below: type 3 uses the **identity** matrix builder
  `FUN_0046cad1`, not `FUN_00467d30`, and the `0x30000` `FUN_0048db20` writes to
  `node + 0x10` is the **collision radius**, not a scale.
- `DAT_0058dc3a` write order (65000 in `FUN_004c3010` vs. a smaller tuning
  value elsewhere) - needed for the spotlight reaction probability.
- Mission scoring/misuse penalties for megaphone messages and tear gas
  (`FUN_004a89c0` event kinds `0x35`, `0x20`, `0x08`, `0x07`).
