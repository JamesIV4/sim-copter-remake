# Helicopter Tools and Model-Switching Decompile Plan

Status: Phases 0-3 complete, Phase 4 partially complete, Phases 5-7 pending.
See "Implementation status" below.

Written: 2026-07-24 - last updated 2026-07-24

## Implementation status

| Phase | State | Notes |
| --- | --- | --- |
| 0 - durable evidence pack | **Done** | `Docs/scratchpad/ghidra/heli_tools_models_decode_20260724.md` plus four raw decompile dumps; hashes recorded; `FUN_00489250` label corrected; harness opcode 87 resolved; `DAT_0058d728` dumped and name-checked against `people.df`. |
| 1 - model registry + live switching | **Done** | `SimCopterHelicopterRegistry` (nine records), prepare/validate/commit switching, authored tail-rotor mounts, debug-panel model row. |
| 2 - equipment state, selector, input | **Done** | `FSimCopterEquipmentState` (career / debug-grant / model layers), `StartPrimaryToolUse`/`StopPrimaryToolUse`, tool row + contextual rows, mission widget's water button removed. |
| 3 - spotlight + megaphone | **Done** | `SimCopterSpotlight` march/smoothing/bands, `FSimCopterToolTarget`, `SimCopterInteraction` spiral scan + reaction table, megaphone routed through the spotlight tile with all five messages. |
| 4 - rescue harness | **Partial** | Winch state machine, rope-end exchange, and the `HARNESS` mesh are ported. The behaviour-VM opcodes (48/53/58/59/82/86/87) and the BHAV 700/305/303 attachment loop are **not** implemented yet. |
| 5 - tear gas | **Not started** | Ammo, cooldown, and the refusal path are in; the projectile, gas cloud, and mode-5 reaction are not. |
| 6 - Apache weapons | **Not started** | Selector entries and `MODEL` availability are in; the emitters are not. |
| 7 - career/catalog integration | **Not started** | Prices, bits, and sell values are decoded and tabled, ready for the shop layer. |

Corrections this pass made to the sections below are listed in section 9 of the
decoded note; the two that change gameplay work are repeated inline where they
apply.

Scope: the remaining helicopter equipment, the shared targeting systems those
tools depend on, Apache-only armament, and debug UI for switching tools and
helicopter models during play.

Companion documents:

- `Docs/WaterGameplayDecompilePlan.md` is the source of truth for the already
  ported water bucket, rope simulation, water cannon, water capacity, and water
  impact loop.
- `Docs/FireWaterDustBackwashPortPlan.md` owns authentic rendering of the typed
  effect pools. This plan owns the gameplay meaning of tear gas and Apache
  projectiles, not their final raster appearance.
- `Docs/DecompilationWorkflow.md` indexes the Ghidra exports used below.

## Objective

Reach original-game parity for all five purchasable helicopter tools, then make
every tool easy to exercise on every compatible helicopter without editing
defaults or restarting the map.

The finished debug workflow must let a developer:

1. switch among all nine helicopter types known to the executable;
2. select or temporarily grant any normal helicopter tool;
3. see why a tool is available (`CAREER`, `DEBUG GRANT`, `MODEL`, or
   `UNAVAILABLE`);
4. operate the selected tool through the same runtime path used by normal
   input;
5. inspect relevant capacity, ammo, rope, target, and model state; and
6. switch models without losing flight state or partially replacing the
   helicopter when an asset fails to load.

Debug grants and model choices are session state. They must never silently
modify career ownership, money, ammunition, or the saved active helicopter.

---

## 1. Confirmed tool boundary

The original help in `Reference/SimCopterOriginalGame/help/English/08tut.htm`,
`31ref.htm`, and the individual equipment pages identifies five purchasable
tools. The water pair is already implemented; three normal tools remain.

| Equipment | Career bit (`career + 0x48`) | Current remake state | Work in this plan |
| --- | ---: | --- | --- |
| Water bucket | `0x01` | Core loop ported | Preserve and route through the common selector |
| Megaphone | `0x02` | Traffic-jam-only placeholder | Port spotlight-area targeting and five messages |
| Rescue harness | `0x04` | Missing | Port the original rope end, person attachment, and boarding loop |
| Tear gas launcher | `0x08` | Missing | Port ammo, projectile, impact reaction, and riot scoring |
| Water cannon | `0x10` | Core loop ported | Preserve and route through the common selector |

`FUN_00407a50` returns the career record. `FUN_0042d840` and
`FUN_0042d9f0` set and clear the ownership bits. Buying tear gas also initializes
`career + 0x54` to 10 rounds; selling it clears the ammunition.

This closes the ownership-mask uncertainty left in
`Docs/WaterGameplayDecompilePlan.md`: the mapping above is now confirmed and
must be captured in the new durable decompile artifact described in Phase 0.

### Supporting and special systems

- The **spotlight** is built into the helicopter and is not a sixth purchased
  tool. It supplies the ground target used by the megaphone and by
  spotlight-directed dispatch commands.
- The **Apache missile and machine gun** are model-specific action overrides,
  not career purchases. They should appear in the debug tool selector only when
  the Apache is active, and should be implemented after the five normal tools.
- Emergency dispatch (`F2` through `F5` in the original) is not part of this
  tool-parity milestone. The spotlight target service must nevertheless be
  reusable by a later dispatch port. **Done 2026-07-25**: the dispatch port
  consumes `GetSpotlightTarget().Tile` exactly as `FUN_0048a580` reads the
  spotlight node. See `Docs/scratchpad/ghidra/emergency_dispatch_decode_20260725.md`
  and `Source/SimCopterRemake/{Public,Private}/Ground/SimCopterDispatch.*`.

The original help also establishes that purchased equipment is installed on
every helicopter the player owns. Tool ownership therefore belongs to the
career/session, not to an individual model. Switching models must not clear or
recompute the equipment mask.

---

## 2. Current remake gaps

The current port has useful pieces, but they are wired too narrowly for the
remaining equipment:

- `ASimCopterHelicopterPawn` has a string `HelicopterTypeName`, separate mesh
  and tuning loaders, and duplicated name/stat tables. The mesh loader mutates
  live procedural-mesh sections as it loads, so failure can leave a partially
  replaced model. The tuning loader also refills fuel, which makes it unsafe as
  a live model-switch operation.
- The tail rotor mount is inferred from mesh bounds. The original executable
  has per-model authored offsets.
- The searchlight is a fixed `USpotLightComponent` that can be toggled. The
  original gameplay system keeps the spotlight active, lets the player aim it,
  ray-marches to a ground/object target, and broadcasts interactions around
  that target.
- `TryUseMegaphone` currently finds one nearby traffic jam from the helicopter's
  location. It does not use the spotlight, select a message, or dispatch
  reactions to people and vehicles.
- The water selector button lives in
  `ASimCopterMissionSystemActor::EnsureDebugButtonsWidget`. Tool/model testing
  consequently depends on a mission actor being present.
- Primary input is named around water (`StartPrimaryWaterUse` /
  `StopPrimaryWaterUse`) even though left click must become the common primary
  action for every selected tool.

The water HUD and capacity bar are working gameplay UI. They remain visible for
bucket and cannon modes and are not replaced by the debug panel.

---

## 3. Canonical helicopter registry

Create one definition table, for example
`FSimCopterHelicopterDefinition`, and make tuning, mesh loading, passenger
capacity, tool hardpoints, and the debug UI consume it.

The executable's internal order is not the order of sections in `heli.twk`.
`heli.twk` lists Bell and Schwiezer before Apache; the runtime type table puts
Apache at index 2. Never infer a runtime index from file order.

The help/catalog presents eight civilian helicopters. The debug selector
intentionally exposes all nine runtime records, including the hidden/special
Apache, so its action overrides can be tested.

| Runtime type | Display / tweak section | Body | Main rotor | Body shadow | Rotor shadow | Seats | Special |
| ---: | --- | --- | --- | ---: | ---: | ---: | --- |
| 0 | Jet Ranger | `0x076` `JETRANG` | `0x117` `JETRROTR` | `0x159` | `0x160` | 4 | - |
| 1 | Hughes 500 | `0x116` `HUGH500` | `0x078` `H500ROTR` | `0x158` | `0x15f` | 4 | - |
| 2 | Apache | `0x119` `APACHE` | `0x11a` `APACROTR` | `0x15b` | `0x162` | 0 | Missile / machine gun |
| 3 | Bell 212 | `0x124` `BELL212` | `0x126` `BELLROTR` | `0x156` | `0x15d` | 14 | - |
| 4 | Schwiezer 300 | `0x125` `SCWZR300` | `0x127` `SCWZROTR` | `0x15a` | `0x161` | 2 | Shipped spelling |
| 5 | Agusta | `0x141` `AGUSTA` | `0x142` `AGUSROTR` | `0x155` | `0x15c` | 7 | - |
| 6 | Dauphin | `0x153` `DAUPHIN` | `0x154` `DAUPROTR` | `0x157` | `0x15e` | 13 | - |
| 7 | MDEXPLORER | `0x170` `MDEXPLRR` | `0x172` `MDEXROTR` | `0x174` | `0x176` | 7 | NOTAR |
| 8 | MD520 | `0x171` `MD520` | `0x173` `MD52ROTR` | `0x175` | `0x177` | 4 | NOTAR |

The registry should contain at least:

- `InternalTypeIndex`, `DisplayName`, and exact `TweakSection`;
- body, main-rotor, body-shadow, and rotor-shadow object IDs and names;
- passenger capacity;
- authored tail-rotor offset or a NOTAR flag;
- an Apache-armament flag; and
- decoded hardpoints/transforms for the spotlight, bracket, cannon, rope, and
  model-specific weapons.

`FUN_00483c20` is the primary construction target. It builds the helicopter
render hierarchy and references these known shared objects:

| Object | GEO ID / name |
| --- | --- |
| Tail rotor | `0x083` `ROTORTL` |
| Water bucket | `0x07b` `BUCKET` |
| Spotlight | `0x118` `SPOTLITE` |
| Equipment bracket | `0x16c` `BRACKET` |
| Rescue harness | `0x16d` `HARNESS` |
| Water cannon | `0x16e` `CANNON` |

The per-model static block starts at
`DAT_005040e4 + InternalTypeIndex * 0x5c`. Confirm and port:

- seats at `+0x00`;
- tail-rotor mount components at `+0x2c`, `+0x30`, and `+0x34`; and
- the NOTAR flag at `+0x38`.

`FUN_00487740` consumes the tail data during rotor animation. The first
decompile pass must record the coordinate conversion and every child-node
transform from `FUN_00483c20`; do not replace those mounts with new bounds
heuristics.

---

## 4. Decompilation work packages

Every work package ends with a small, durable decoded note under
`Docs/scratchpad/ghidra/`, including function hashes and unresolved branches.
Raw Ghidra output alone is not the handoff.

### 4.1 Equipment ownership and runtime dispatch

Decode these together so the port has one authoritative equipment state:

| Targets | Question to answer |
| --- | --- |
| `FUN_00407a50`, `FUN_0042d840`, `FUN_0042d9f0` | Career record, purchase, sale, mask, and gas ammo |
| `FUN_0042cad0`, `FUN_0042d420` | Catalog availability and status |
| `FUN_0048b050`, `FUN_0048b0f0`, `FUN_0048b150`, `FUN_0048b070` | Equipment and helicopter price/value queries |
| `FUN_0048b130`, `FUN_00444690`, `FUN_00444750` | Tear-gas maintenance refill and UI limits |
| `FUN_00484d20` | Snapshot of the ownership mask into `DAT_00504060` and per-frame emitter dispatch |
| `FUN_004077f0`, `FUN_004127d0` | Shop/cockpit ownership indicators and exact capability gating |
| `FUN_00485f50`, `FUN_0044ac80` | Flight-control and global-command routing |

Required output:

- confirm all bit tests and missing-equipment messages;
- distinguish career state, the per-flight snapshot, and ammo;
- record which actions are held, edge-triggered, or cooldown-gated; and
- identify all audio/message IDs used on success and failure.

### 4.2 Spotlight target service

The original spotlight is gameplay, not just illumination.

`FUN_00489250`:

- updates the `SPOTLITE` render node at `heli + 0xc0`;
- ray-marches in steps of `0x200000`, for at most 16 steps, through
  `FUN_00489630`;
- smooths the hit distance in `DAT_00504430`;
- chooses one of four distance/effect bands;
- stores the ground tile and positions the light node; and
- calls `FUN_0048ae70(1, ...)` over a three-ring spiral for spotlight
  interactions.

`FUN_00489730` updates the aim globals `DAT_0050408c` and
`DAT_00504090`, clamps them to `+/-0x1f40000`, and rebuilds the direction in
`DAT_0057f230`. `FUN_00479060` maps input actions `0x2e` through `0x31` to that
aim update.

**Done 2026-07-24.** The stale "downwash disc" heading for `FUN_00489250` in
`Docs/scratchpad/ghidra/out_effects_DECODED.md` has been corrected; rotor wash
is `FUN_004881b0` and bucket drip is `FUN_00488060`.

Port the result as a reusable value such as `FHelicopterToolTarget`:

- valid-hit flag;
- world hit and normal;
- original tile;
- hit actor/object identity when present;
- spotlight range/effect band; and
- the interaction radius/rings used by the original scan.

Normal gameplay keeps the spotlight active. The Unreal light may be culled or
quality-scaled visually, but semantic targeting must keep running. Debug UI may
display or freeze the target for diagnosis; it must not redefine the normal
target.

### 4.3 Megaphone

The original exposes five distinct messages:

| Index | Original key | Message | Intended target |
| ---: | --- | --- | --- |
| 0 | `F6` | Report Traffic | Traffic jam |
| 1 | `F7` | Stop Criminal | Criminal or speeder |
| 2 | `F8` | Evacuate | People in the spotlight area |
| 3 | `F9` | Disperse | Riot |
| 4 | `F10` | Greet | People in the spotlight area |

Decode and port this complete path:

1. `FUN_0044ac80` receives command/action IDs `0x26` through `0x2a`, checks
   capability bit `0x02`, emits missing-equipment message `0x2aa` with sound
   `0x80`, calls `FUN_00424620(MessageIndex)`, then calls
   `FUN_0048a800(MessageIndex)`.
2. `FUN_0048a800` takes the tile from the spotlight ground node and invokes
   `FUN_0048ae70(2, Tile, HelicopterBody, -1, MessageIndex)`.
3. Mode 2 scans a five-ring spiral and routes each eligible object through
   `FUN_0049a4f0`.
4. `FUN_004c1050` stores the message index in a person field and pushes the
   mode-2 reaction from `DAT_0058d728[2]`.
5. Vehicle handling continues through `FUN_0049fc10` and
   `FUN_0049f680`; other object classes must be checked in the generic
   dispatcher.

`people.df` BHAV 901 is named `Rxn: Megaphone` and is the leading candidate for
the people reaction, but the port must dump and confirm
`DAT_0058d728[2]` rather than relying on the name alone. Record the
message-specific behavior for people, criminals, speeders, traffic jams, and
rioters, including point awards or misuse penalties.

Replace the current radius-from-helicopter shortcut only after the spotlight
target and the mode-2 dispatch tests are in place.

### 4.4 Rescue harness

The bucket and harness use one 20-node rope and exchange the rope-end asset.
The relevant helicopter state is:

| State | Location | Decoded meaning (2026-07-24) |
| --- | --- | --- |
| Active rope node | `heli[0x6f]` | Counts **down** from `0x11` (stowed) to `3` (fully lowered) |
| Bucket state | `heli[0x70]` | **Stowed** flag: 1 = raised, 0 = deployed |
| Harness state | `heli[0x71]` | **Stowed** flag: 1 = raised, 0 = deployed |
| Raise/lower command | `heli[0x72]` | `+1`/`-1` bucket, `+2`/`-2` harness, `0` idle |
| Rope-end render/master node | `heli + 0xbc` | Renders `heli[0x32]` (`BUCKET`) or `heli[0x33]` (`HARNESS`) |

`FUN_00485f50` maps actions `0x0e` and `0x0f` to state-dependent rope
commands, including the harness-specific `+2` / `-2` states, after checking
capability bit `0x04`. `FUN_00487bb0` advances the door/winch state, swaps the
rope-end object, and updates the chain.
`FUN_00483c20` constructs both `BUCKET` and `HARNESS`.

The person loop must use the shipped behavior graph:

- active init BHAV 700: `Rescue new initbhav`;
- BHAV 305: `Rescue try to get on heli or bucket`;
- BHAV 303: `Rescue try get off heli or bucket if appropriate`.

BHAV 1498 is explicitly the old rescue initializer and is not the source of
truth while BHAV 700 is active.

`FUN_004c6360` controls the person's master/attachment at person `+0x1a0`
and recognizes both the helicopter body (`DAT_005040d0 + 0xa4`) and rope end
(`+0xbc`). `FUN_004c3010` initializes a sparse opcode dispatch table; do not
infer opcode numbers from thunk order. Confirm and implement at least:

| Opcode | Target | Known purpose |
| ---: | --- | --- |
| 48 | thunk `0x004c8a00` -> `FUN_004cc900` | Set master to stack object and snap position |
| 53 | thunk `0x004c8aa0` -> `FUN_004cca60` | Near-helicopter query and stack-object selection |
| 58 | thunk `0x004c8b40` -> `FUN_004cccd0` | Transfer a rope-end rider to the helicopter when the harness is raised |
| 59 | thunk `0x004c8b60` -> `FUN_004cce30` | Master is helicopter body |
| 82 | thunk `0x004c8e80` -> `FUN_004ccad0` | Stack-object proximity within `0x18` original units |
| 86 | thunk `0x004c8b80` -> `FUN_004cceb0` | Master is rope end |
| 87 | thunk `0x004c8ba0` -> `FUN_004cce50` | **Resolved 2026-07-24:** the person's stored destination tile record equals its current tile record - the get-off condition in BHAV 303 record [7] |

BHAV 305 also uses opcodes 12 and 15 (`FUN_004ca940` and
`FUN_004cac70`). Decode their branches before implementing automatic pickup.
Decode opcode 87 and the entirety of BHAV 303 before implementing unloading.

Port the required opcodes into `FSimCopterBehaviorVM` and expose attachment
operations through `ISimCopterBehaviorWorld` /
`ASimCopterGroundAgent`. Do not replace the BHAV with a mission-only distance
check.

Acceptance behavior from the original help:

- the fully lowered harness hangs roughly 50 feet below the helicopter;
- an eligible Sim near it grabs it automatically;
- only one Sim rides at a time and cannot fall;
- raising the occupied harness transfers the Sim onboard; and
- passenger capacity and mission pickup/drop-off notifications remain
  authoritative.

The first decompile pass must resolve what happens when no passenger seat is
available and how the get-off graph chooses a safe release. Model switching is
blocked while a person is attached to the rope end so debug actions cannot
orphan an agent.

### 4.5 Tear gas launcher

Confirmed persistent state:

- ownership bit `0x08`;
- ammunition at `career + 0x54`;
- capacity 10;
- refill through maintenance rather than passive regeneration.

For a non-Apache helicopter, action 2 in `FUN_00485f50` checks ownership and
ammo, uses failure message `0x2ac` / sound `0x80`, and requests emitter type 3.

Decode the full projectile path:

| Targets | Required result |
| --- | --- |
| `FUN_0048e0b0(3, ...)` | Spawn transform, life, size, velocity, cooldown (`0x10000`), sound `0x17`, ten-slot pool `DAT_005d4bd0`, and ammo decrement/clamp |
| `FUN_00484d20`, `FUN_0048ed00` | Per-frame emitter and projectile update |
| `FUN_00490690` | Canister collision (class flag `0x8`) |
| `FUN_0049a4f0(5, ...)`, `FUN_004c1050` | Object-class routing and people reaction from `DAT_0058d728[5]` |
| `FUN_004a89c0` and mission handlers | Riot progression, correct tool order, rewards, and misuse penalties |
| `FUN_0048b130`, `FUN_00444690`, `FUN_00444750` | Maintenance refill and UI feedback |

**Corrected 2026-07-24.** The tear gas people reaction is **interaction mode 5**
(`DAT_0058d728[5]` = BHAV 907 `Rxn: Teargas`), applied by the 30-second gas cloud
in `FUN_0048ed00`, not mode 7 via `FUN_00490690`. Mode 7 is the Apache machine
gun (class flag `0x4` -> BHAV 915), mode 3 is the Apache missile (class flag
`0x2` -> BHAV 915), and a canister that physically strikes an object produces
mode `0xe` (BHAV 910 `Rxn: Debris stuff hit`) from its `0x8` class flag. The pool
class flags are written once by `FUN_0048da50` and survive impact, which is why
the spawn function never sets them.

The full life cycle: 5.0 s as a bouncing canister (class flag `0x798` makes
`FUN_00490690` reflect it off surfaces instead of destroying it), a smoke card
every 0.5 s, then detonation into a 30.0 s gas cloud that every 0.3 s drops a
puff within +/-20 units and applies mode 5 to every person on that tile. The
`0x10000` cooldown is shared with the Apache missile through `DAT_00504570`.

The table was dumped from `FUN_004c3010` and every id cross-checked against the
shipped `X/people.df` BHAV directory; see section 5 of the decoded note.

The implementation is not complete when a visible gas card spawns. It is
complete when the projectile consumes ammo, collides through the original
path, drives the correct person/riot behavior, respects the megaphone-water-gas
escalation order, penalizes misuse, and refills through maintenance.

### 4.6 Apache-only armament

Runtime helicopter type 2 changes the meaning of two normal actions in
`FUN_00485f50`:

- action 2 requests emitter type 1 instead of tear gas;
- action `0x10` requests emitter type 2 instead of the water cannon.

Current evidence identifies:

| Tool | Emitter | Pool | Speed | Sound |
| --- | ---: | --- | ---: | --- |
| Apache missile | 1 | 10 slots at `DAT_005d4900` | `0x1c20000` | ID 6, `MISSILE.WAV` |
| Apache machine gun | 2 | 70-slot pool at `DAT_005d4f30` | `0x2580000` | ID 5, `MACHGUN1.WAV` |

Decode their exact mounts, projectile/card types, held-versus-edge input,
cooldowns, collision modes, damage, scoring, and mission restrictions through
`FUN_0048e0b0`, `FUN_0048ed00`, `FUN_00490690`, and the object
interaction dispatch.

Emitter type 3 is tear gas, not a third Apache weapon. Keep the Apache entries
out of the career equipment mask and expose them as `MODEL` capabilities only
while the Apache is selected.

---

## 5. Runtime architecture

### 5.1 Tool identity and effective capability

Introduce a tool enum with normal and model-specific entries:

```text
WaterBucket
WaterCannon
Megaphone
RescueHarness
TearGas
ApacheMissile
ApacheMachineGun
```

Keep state in three explicit layers:

```text
CareerEquipmentMask
DebugGrantedEquipmentMask
ModelCapabilities
              |
              +--> Effective availability shown to input and UI
```

`DebugGrantedEquipmentMask` and debug tear-gas ammo are transient. The
debug panel may grant/revoke them, but career reads and writes continue through
their own interface. Selecting an unavailable tool should show
`UNAVAILABLE` and a `Grant for session` action rather than modifying the save
as a side effect.

The selected tool is independent of the selected helicopter. Switching among
civilian models preserves the selection and grants. When a model-specific tool
becomes invalid, retain the remembered selection but choose the first
available normal tool for active input and explain the fallback in the status
line.

### 5.2 Common primary-use path

Replace the water-named input boundary with a common pair such as:

```text
StartPrimaryToolUse()
StopPrimaryToolUse()
```

Both left click and the debug panel's `USE` button call this path:

- bucket: release water while held;
- water cannon: continuous stream while held;
- megaphone: one broadcast per press using the selected message;
- tear gas: one cooldown-gated round per press;
- harness: context deploy/stow action when safe, with raise/lower remaining
  explicit held controls;
- Apache machine gun/missile: exact held or edge behavior from the decompile.

Legacy keys may remain as aliases during development. The flight HUD must show
contextual hints for the active tool, including left click, winch controls,
message selection, ammo, and unavailable-state reasons.

Slate button pointer events must return `Handled` and suppress the same mouse
event from reaching flight input. One click on `USE` must never both press the
button and fire a second world action.

### 5.3 Shared interaction dispatch

Do not add one mission-specific radius query per tool. Extend the existing
interaction and behavior layers so they can carry:

- interaction mode;
- source object;
- target object/tile;
- message/tool subtype;
- impact strength or projectile state; and
- mission/scoring context.

Spotlight, megaphone, tear gas, water, and later dispatch then use one
object-class router corresponding to `FUN_0049a4f0`. People reactions continue
through `FSimCopterBehaviorVM`; vehicle, building, mission, and fire effects
remain with their owning systems.

### 5.4 Expected code ownership

Keep the port split along the existing runtime boundaries:

- a new Flight definition/state unit owns the model registry, effective
  equipment mask, tool identity, prepared model data, and switch transaction;
- `ASimCopterHelicopterPawn` owns input, hardpoints, rope/winch state, active
  emitters, and the application of prepared model data;
- a new private Slate widget owns helicopter/tool debug presentation and calls
  pawn/controller APIs rather than mission internals;
- `FSimCopterBehaviorVM` and `ASimCopterGroundAgent` own rescue and
  people-reaction opcodes;
- `USimCopterParticleFXComponent` owns tear-gas and Apache projectile
  lifecycle/collision;
- `ASimCopterMissionSystemActor` consumes interaction results for mission
  progress and scoring, while retaining only its mission-specific debug
  buttons; and
- focused tests live beside the existing flight, water, particle, mission, and
  behavior-VM automation.

---

## 6. Debug UI

Create a pawn-owned or player-controller-owned non-shipping widget, for example
`SSimCopterHelicopterDebugPanel`. It must work in a free-flight map with no
`ASimCopterMissionSystemActor` and must rebind when possession changes.

Leave the mission actor's `Force Fire` and `Force Car Fire` controls with the
mission system. Move `Switch Bucket / Water Gun` into the new panel and replace
it with the generic tool row.

A compact first version:

```text
HELICOPTER  [<] Jet Ranger [>]  TYPE 0
             JETRANG / JETRROTR  Seats 4  Tail rotor
             Model ready

TOOL        [<] Rescue Harness [>]  [USE]
             DEBUG GRANT  [Revoke]
             Rope: lowered  Rider: none  [LOWER] [RAISE] [STOW]

SPOTLIGHT    Target (123, 88)  Band 2  Valid
```

Context rows replace the rope line as needed:

- water: current pounds / capacity and fill/dump state;
- megaphone: `[<] Evacuate [>] [BROADCAST]`;
- tear gas: rounds / 10 and `[REFILL DEBUG]`;
- Apache: missile or machine-gun cooldown/projectile count;
- model: internal index, asset names/load result, seats, maximum load, NOTAR,
  and `SPECIAL` for Apache.

UI rules:

- model arrows switch immediately through the transaction in section 7;
- tool arrows enumerate all five normal tools, plus Apache tools only on the
  Apache;
- unimplemented entries remain visible but disabled with a concrete reason
  during staged development;
- debug refill/grant actions affect only transient state;
- the water capacity bar remains ordinary flight HUD in both water modes;
- the panel reports failed mesh/tuning loads and safety blocks without changing
  the displayed active model; and
- a build/runtime flag such as `bShowHelicopterDebugPanel` plus non-shipping
  guards keeps this out of the release UI.

---

## 7. Transactional live model switching

The current load functions cannot be called back-to-back safely. Replace them
with a prepare/validate/commit operation.

### Prepare

Build a temporary `FPreparedHelicopterModel` containing:

- the selected registry definition;
- parsed `heli.twk` values;
- all resolved body, rotor, shadow, and applicable equipment meshes;
- decoded transforms and hardpoints; and
- a complete error list.

Do not clear live procedural mesh sections during this step.

### Validate

Reject the switch, leaving the current helicopter untouched, when:

- the definition, tweak section, or required body/rotor asset is missing;
- the target has fewer seats than the current onboard passenger count;
- a person is attached to the harness rope end; or
- prepared mesh data or critical tuning is invalid.

Stop held primary input before commit. An empty bucket/harness can be stowed as
part of the transaction; an occupied harness is a hard block with a UI reason.

### Commit

Apply the prepared definition, tuning, mesh sections, hardpoints, and rotor
configuration as one logical operation. Preserve:

- actor transform;
- linear and angular velocity;
- engine/rotor state and flight-control inputs;
- camera mode;
- selected normal tool and transient debug grants; and
- mission association.

Preserve fuel and hit points as normalized fractions of the old maximum rather
than refilling them. Clamp current water pounds to the new model's maximum load
and report the clamp in debug status. Refresh passenger, capacity, tool, and
model UI after commit.

The first debug implementation may switch while airborne because rapid
cross-model testing is the purpose of the feature; it must preserve kinematics
and must not teleport or restart the pawn. Keep the existing collision root
until decoded evidence proves model-specific collision volumes are required.

If a late commit operation can fail, retain enough staged/live data to roll
back every changed component. A failed switch must never leave one model's
tuning on another model's mesh.

---

## 8. Implementation sequence

### Phase 0 - Durable evidence pack

Before gameplay code:

1. Export/hash the functions and tables listed in section 4.
2. Write focused decoded notes for:
   - equipment/career ownership;
   - helicopter render hierarchy and all nine model records;
   - spotlight/megaphone targeting;
   - rescue BHAV 700/305/303 and the sparse opcode mappings;
   - tear-gas projectile/reaction/scoring; and
   - Apache action overrides.
3. Dump and identify `DAT_0058d728[1]`, `[2]`, and `[7]`.
4. Resolve harness opcode 87 and full-seat/get-off behavior.
5. Correct the stale `FUN_00489250` downwash label.
6. Add the new artifacts and hashes to `Docs/DecompilationWorkflow.md`.

Exit gate: every behavior-affecting constant in the implementation backlog has
a function/table citation or is explicitly listed as unresolved.

### Phase 1 - Model registry and debug model selector

1. Add the nine-entry canonical registry.
2. Replace duplicated name/stat switches in the pawn.
3. Port authored tail/NOTAR data and equipment hardpoints.
4. Add prepare/validate/commit model switching.
5. Add the model row and status output to the new debug panel.

Exit gate: all nine models can be cycled in one flight session; a forced missing
asset and an occupied/over-capacity switch both leave the old model intact.

### Phase 2 - Common equipment state, selector, and input

1. Add tool identity, career mask view, transient debug grants, ammo view, and
   availability source.
2. Move the water debug control out of the mission widget.
3. Add the generic tool row, contextual controls, and HUD hints.
4. Route left click and `USE` through common primary-tool input.
5. Adapt bucket/cannon without changing their capacity or dousing behavior.

Exit gate: the working water modes pass their existing tests through the new
route, UI clicks do not double-fire, and the selector works without a mission
actor.

### Phase 3 - Spotlight and megaphone

1. Port spotlight aim, ray target, distance smoothing/bands, and interaction
   scan.
2. Render/aim the Unreal spotlight from the decoded hardpoint.
3. Add all five megaphone messages and original target routing.
4. Port the required people/vehicle reactions and scoring.
5. Add target/message debug readouts.

Exit gate: each message affects only appropriate objects around the spotlight
target, including targets offset from the helicopter.

### Phase 4 - Rescue harness

1. Generalize the existing rope to exchange bucket and harness ends.
2. Port exact harness/winch states and authored `HARNESS` mesh.
3. Implement the required behavior-VM opcodes and master attachments.
4. Integrate transfer to passenger seats, mission pickup/drop-off, safe unload,
   and model-switch guards.
5. Add rope/rider controls and status to the debug panel.

Exit gate: one eligible Sim automatically boards a fully lowered harness,
cannot fall, transfers onboard when raised, and unloads through the shipped
behavior path.

### Phase 5 - Tear gas

1. Add transient and career ammo sources with capacity 10.
2. Port emitter type 3 spawn/update/collision.
3. Route interaction mode 7 and BHAV reaction.
4. Port riot escalation, reward/misuse behavior, audio, and maintenance refill.
5. Add ammo/refill/cooldown status to the debug panel.

Exit gate: a round has physical travel and collision, decrements exactly once,
drives the original reaction/scoring path, and cannot fire at zero ammo.

### Phase 6 - Apache weapons

1. Add Apache-only selector entries and `MODEL` availability.
2. Port missile and machine-gun emitter, collision, damage, sound, and scoring.
3. Load decoded mounts and verify action overrides do not consume gas/water.

Exit gate: switching to Apache exposes both weapons; switching away removes
them from active input without changing career equipment.

### Phase 7 - Career/catalog integration

When the career/shop layer is ready, connect real purchases, sales,
maintenance refill, and cockpit indicators to the same equipment interface.
Debug grants remain an overlay and are excluded from serialization.

---

## 9. Verification matrix

### Automated checks

Add focused automation for:

- registry uniqueness, runtime indices, asset names/IDs, tweak sections, seats,
  and NOTAR flags for all nine definitions;
- successful switch state preservation and failed-switch rollback;
- passenger-capacity and occupied-harness switch guards;
- normalized fuel/hit-point preservation and water-load clamp;
- effective equipment mask and debug-grant non-persistence;
- tool/model selector wrap and compatibility fallback;
- common primary-input dispatch and Slate click consumption;
- tear-gas ammo/cooldown/impact routing;
- megaphone message-to-target routing and spotlight ring selection;
- rescue attachment, raised transfer, one-rider limit, and safe release; and
- Apache action overrides.

Retain all `SimCopter.Water.*` tests as regression gates.

### Manual model coverage

Cycle all nine models and verify body, main rotor, shadows, tail/NOTAR,
hardpoints, tuning, seats, and maximum load. Exercise the complete tool set on:

- Schwiezer 300: smallest passenger/load case;
- Bell 212: largest passenger case;
- MD520 and MDEXPLORER: both NOTAR cases;
- Jet Ranger: baseline civilian model; and
- Apache: special action overrides.

The equipment mask must remain identical across every civilian model.

### Manual tool coverage

- Bucket/cannon share water capacity, show the water bar, travel to the target,
  and douse fire without regression.
- Spotlight targeting follows aim and changes bands with range/altitude.
- All five megaphone messages affect the correct spotlight area and object
  classes.
- Harness lowers, accepts one valid Sim, retains the rider, raises into an
  available seat, and unloads safely.
- Tear gas starts at/refills to 10, decrements once per shot, reacts on impact,
  handles riots in the correct escalation order, and penalizes misuse.
- Apache missile/machine gun use their own pools and never consume normal tool
  capacity.
- Every mode shows accurate controls and unavailable reasons; clicking debug UI
  never leaks a primary-fire click into the world.

### Build and runtime gates

For each phase:

1. run `git diff --check`;
2. build the Unreal target containing the changed module;
3. run focused automation;
4. launch a free-flight PIE smoke test with no mission actor; and
5. run a mission-map smoke test to confirm mission debug controls and
   passenger/scoring integration remain intact.

---

## Definition of done

This plan is complete only when:

- all five purchasable tools have behavior-backed ports, not visual-only
  placeholders;
- the spotlight provides the shared semantic target used by megaphone and
  future dispatch work;
- the Apache's two special weapons are correctly model-gated;
- all nine executable helicopter models load through one decoded registry;
- live model switching is atomic and preserves flight/session state;
- tool/model debug UI works without a mission actor and never writes career
  state;
- left click and debug `USE` share one tool-dispatch path;
- the existing water capacity HUD remains correct for bucket and cannon; and
- the automated and manual matrices above pass with no known partial-load,
  attachment, ammo, or input-leak failures.
