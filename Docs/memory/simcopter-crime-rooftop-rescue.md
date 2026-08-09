# SimCopter crime, riot, and rooftop-rescue missions

*Decoded and aligned 2026-08-09. This note separates the scheduler's Burglar record from the
distinct ambient Speeder encounter.*

## Evidence map

The mission layer is split across these retail paths:

- `FUN_004a6e60`: scheduler bucket and difficulty-specific type selection.
- `FUN_004a92f0`: placement attempts around the camera.
- `FUN_004a7a10`: record creation, person/car activation, counts, and names.
- `FUN_004a73e0`: completion tests, reminder timers, and riot-marker recentering.
- `FUN_004a89c0`: mission-event counter writes.
- `FUN_004aa150`: incremental rewards, penalties, and HUD text.
- `FUN_004aabf0`: final reward and completion voices.
- `FUN_004ab480`: four-part dispatcher announcement.
- `DAT_0058de80` plus shipped `X/people.df`: person state to BHAV behavior.
- `FUN_004b8470` / `FUN_004b8540` / `FUN_004b8630`: the burglar's CARROBBR car.
- `FUN_0049af00` / `FUN_0049af70`: designate an ordinary ambient car as the Speeder.
- `FUN_0049be50`: stopped-Speeder waiting/capture events (`0x21` / `0x26`).
- `FUN_004c4190`: riot-person initialization; `FUN_004c9e20`: agitation-weighted centroid.

The raw lifecycle decompile is in
`Docs/scratchpad/ghidra/crime_roof_lifecycle_004a73e0.txt`. The shipped behavior graphs can be
reproduced with `Tools/people_bhav_dump.py`.

## Identities, titles, and markers

These names are pinned by Win32 STRINGTABLE ids 571-583, not inferred from the old enum names:

| mask | retail title / text id | initial object | record name | remake world marker |
|---|---|---|---|---|
| `0x200` | Robber / `0x247` (583) | one person, state 10, behavior class 9, BHAV 1300 | `Robber <eventId>` | `ROBBER` |
| `0x2000` | Arsonist / `0x245` (581) | one person, state 11, behavior class 9, BHAV 1301 | `Arsonist <eventId>` | `ARSONIST` |
| `0x20000` | Mugger / `0x246` (582) | one person, state 12, behavior class 9, BHAV 1302 | `Mugger <eventId>` | `MUGGER` |
| `0x4000` | Burglar / `0x244` (580) | one CARROBBR car; its person is state 13 / class 15 / BHAV 1303 | `Burglar <eventId>` | `BURGLAR`, follows the car while driving and the person while outside |
| `0x1000` | Riot / `0x23b` (571) | 9-30 requested rioters, state 3 | `Riot <eventId>` | `RIOT`, follows the crowd centroid |
| `0x80010` | Rooftop Rescue / `0x23c` (572) | `(rand % tier) + 1` people, state 2 / BHAV 700 | `Rooftop Rescue <eventId>` | `ROOFTOP` |

The original cockpit map uses icon 2 for all four crimes, icon 6 for a riot, and icon 4 for a
rooftop rescue. Crime missions share `DAT_0057f9a0` as their metadata serial and rescue missions
share `DAT_0057f9b0`; the visible name nevertheless uses the global event id. The old remake names
such as `Criminal A #0`, `Speeder #0`, and `Fire Rescue #0` were not retail mission-record names.

### Speeder is distinct, but is not a scheduled mission record

Retail Speeder is an ambient vehicle encounter, separate from the `0x4000` Burglar mission.
`FUN_0049af00` scans the ordinary 70-car pool for one of GEO ids `0x7a`, `0x7d`, `0x7e`, `0x12a`,
299, or 300, requiring an active ordinary car and rejecting the last selected slot. It raises
vehicle flag `0x800`; it does not call `FUN_004a7a10`, allocate an event id, create CARROBBR, or
publish a mission marker/title. `FUN_004a6e60` retries every tick until the first designation,
then calls the preserving/replacement scan `FUN_0049af70` once every 64 simulation ticks.

The stop flow is also independent. A stopped `0x800` car with no nearby CARPOLIC posts event
`0x21`, **“Waiting For Cops!”**, once every `0xa0000` (10 seconds), paying `Speeder Inc Points`
as cash. When police reach it, event `0x26`, **“Speeder Caught!”**, pays `Speeder End Money` and
`Speeder End Points`, both with event id `-1`; no mission record completes. The remake therefore
exposes Speeder as a separate debug button backed by the ambient traffic system, not as an alias
for the Burglar catalog entry.

The shipped D1008 “speeder report” audio is indeed distinct. However, the decoded spawn path does
not call the dispatcher, and `FUN_004ab480` has no announcement case for the scoring bit `0x2`.
Likewise, `FUN_004aabf0`'s voice 99 mapping for bit `0x2` belongs to mission-record completion,
whereas the live ambient path pays through `FUN_004aa150`. Do not invent a scheduled Speeder
record merely to make those otherwise separate/dead audio branches fire.

## When the scheduler selects them

The generic scheduler begins at `0xb40000` (180.0 s). A successful active mission resets it to
`Easy Interval`, shipped as `380.0 s`; a background record uses half. While the active count is
below `Max Easy + tier`, each pass applies the original fixed-point adjustment:

```
countdown = countdown - smoothedFrameDelta
          + (activeCount - (MaxEasy + tier) + 1) * 0.2
```

When it falls below zero and no modal shell is open, the career city's seven cumulative weights
select Fire, Crime, Rescue, Riot, Traffic, MedEvac, or Transport. The Crime bucket then uses:

One existing, explicit remake policy remains outside this alignment: a newly created user-city
session initializes its Crime weight to 0 instead of the retail default 90. Career-city weights are
retail data, and the City Settings Crime slider can still enable crimes in a user city immediately.

| difficulty tier | Robber | Arsonist | Mugger | Burglar |
|---|---:|---:|---:|---:|
| 1 | 1/5 | 1/5 | 1/5 | 2/5 |
| 2 | 1/2 | 1/2 | 0 | 0 |
| 3 | 1/5 | 1/5 | 1/5 | 2/5 |
| 4 | 1/8 | 2/8 | 1/8 | 4/8 |

The Rescue bucket selects rooftop/boat/train as follows: tier 1 is rooftop only; tier 2 is
3/4 rooftop and 1/4 boat; tier 3 is 1/8 rooftop, 6/8 boat, and 1/8 train; tier 4 is 1/5 rooftop,
3/5 boat, and 1/5 train. The Riot bucket always asks for a riot, but `FUN_004a7a10` refuses a
second live riot.

## Where and how many spawn

Every random mission tile starts around the camera. Its range is
`(consecutivePlacementFailures + 18) * tier + 8`; each axis takes the larger of two
`rand() % range` draws and gives it a random sign. An out-of-map result is replaced by a full-map
0-127 draw.

### On-foot crimes

Robber, Arsonist, and Mugger each get five placement attempts. The tile must have an XBLD building
id from `0x70` through `0xdb`, excluding `0xd1` hospital, `0xd2` police, and `0xd3` fire station.
Exactly one person is requested. If that person cannot be placed, record creation fails.

All three continuously publish their own tile as event 0, so both the retail map marker and the
remake world tag follow the person rather than remaining at the spawn building.

### Burglar

The five seed attempts are unfiltered. `FUN_004b8540` searches roads within radius 5 of that seed,
rejects bridge-deck placement for CARROBBR, and takes one of a fixed pool of five cars. Failure to
find a road or a free car fails the mission.

### Riot

The requested count is:

```
(rand() & 7) * (tier - 2) + 16
```

That yields tier 1: 9-16, tier 2: exactly 16, tier 3: 16-23, tier 4: 16-30. Creation aborts on the
sixth failed person attempt if no one has spawned, and rejects the record unless at least 11 were
placed. Every rioter starts with agitation 7. The seed marker is immediately replaced by the
agitation-weighted crowd centroid and refreshed every 13 lifecycle passes.

### Rooftop Rescue

There are two creation paths:

1. Scheduled Rescue-bucket placement tries five occupied-building tiles (XBLD property bit 2),
   excluding the three service buildings.
2. A live building fire can create one rescue per fire object on tier 2-4. It becomes eligible
   when the flame has less than `(tier * 5 + 15) * 4` seconds left and its building carries the
   occupied bit. This path is why rooftop rescues could still appear in retail.

The retail scheduled placer has a signed-char bug: every occupied XBLD id is at least `0x81`, is
sign-extended negative, and is rejected before its property lookup. The remake deliberately uses
the intended unsigned id so the scheduled mission type is not dead. This is a documented bug fix,
not a claim that the broken retail branch succeeded.

The creator requests `(rand() % tier) + 1` victims and keeps every successful spawn; at least one
must succeed. Secondary and tertiary coordinates remain `-1`. The old remake fabricated a delivery
building, which added a phase and marker that the retail record does not have.

Because remake buildings are not cell-for-cell copies of the original object system, state-2
people now use a rendered-geometry adaptation: resolve and cache the building footprint center,
trace the top blocking roof surface, sample separated points at that roof height, spawn 92 cm above
it, snap onto the roof, and confine the person to a conservative roof post until boarding. There is
no street/ground fallback for this mode. Boarding clears the post normally.

Each of the fourteen rooftop sample candidates must also have an upward surface-normal Z of at
least `0.99` (the same approximately eight-degree flatness threshold used for airframe landings).
A pitched roof triangle, ridge, or decoration is rejected and the golden-angle sampler tries the
next position. If no flat point validates, that person spawn fails; it never falls back to ground.

## Phases and player interactions

### Robber, Arsonist, and Mugger

The shared criminal graph watches for the helicopter, foot police, and police cars within four
tiles, then runs through BHAVs 1171-1173. A cop catches a criminal by selecting class 6, walking
within two tiles, and pushing BHAV 1060 `Rx: criminal-caught` onto that person. BHAV 1060 posts
outcome 9 = `EVT_CriminalCaught`, plays sound event 25, waits, walks to a police car, and leaves.
The helicopter can also catch one from BHAV 1173 when it is within three tiles and at most four
original units above ground; that branch wins its `rand(10) > 4` check before posting the same
outcome.

An airframe collision instead routes through BHAV 912 -> 903 and posts a casualty, not a caught
criminal. That still closes the job because the retail completion test is
`criminalsCaught + casualties >= 1`; the port no longer fabricates an additional caught outcome.

Their unspotted behavior differs:

- Robber: BHAV 1174 idles, turns, walks, and republishes the marker. The lifecycle's repeated
  consequence is the `Burglary Committed!` event; catching or killing the one target ends it.
- Arsonist: BHAV 1078 walks and rolls `rand(1000) < 6` per loop. Success throws opcode 60's type-4
  firebomb. It burns for 60 s after landing and then rolls `1 in (8 - tier)` to start a building
  fire if the tile is eligible and no nearby fire already owns it.
- Mugger: BHAV 1175 probes for an ambient civilian within two tiles, walks to them with ten tries,
  plays sound event 16, and pushes BHAV 903 `Rxn: Die` onto the victim. It otherwise turns and
  walks. Catching or killing the mugger ends the mission.

### Burglar recurrence

This is not a one-shot speeder mission. The car's exact retail state machine is:

| state | phase |
|---:|---|
| 0 | cruise; count down the burglary delay, or enter state 2 when spotlighted |
| 1 | timer expired; stop at the next valid non-intersection road position |
| 2 | illuminated/fleeing; after 20 s without the spotlight, return to state 0 |
| 5 | coast to a physical stop |
| 3 | burglar outside; wait up to 120 s for their vehicle message |
| 4 | leave after person placement failed; retire the mission silently |

The initial and every recurring delay are
`DAT_00506360 + (short)rand() % DAT_00506364`, where the initialized values are `0x640000`
(100.0 s) and `0x2580000` (600.0 s). MSVC `rand()` is only 0-32767, so the real delay is
100.0000-100.49998 s, not 100-700 s. The initial draw happens at placement. When a cruise timer is
already negative at the start of state 0, the car enters state 1 and immediately draws the delay
for the *following* cycle; that pre-armed value is untouched while the burglar is outside. The
spotlight-loss timer is `0x140000` (20.0 s), likewise tested before the frame delta is subtracted.

At a stop, `FUN_004b8b60` calls `FUN_0049bd00(behaviorClass=0xf, personState=0xd)`. The remake API
takes those arguments in the opposite order; the old port had swapped them and therefore did not
run BHAV 1303. The correct burglar runs BHAV 1079: run, `2Gab`, idle 80, play sound event 8, find
the exact service-3/getaway car within five tiles, and walk back. BHAV 1303 then executes opcode 61
with message 1 and opcode 40. The message writes `veh[8]=1`, `veh[0xc]=1`; state 3 sees the nonzero
message, returns to state 0, starts the pre-armed delay, and the same mission commits another
burglary.

Both transitions are sound-gated. `FUN_004b8b60` plays `aDrOpen` (`0x6f`), waits for that slot to
finish before placing the burglar, plays `aDrClose` (`0x70`), and enters state 3 only when it ends.
The nonzero-return arm of `FUN_004b8c90` runs the same open/close pair before resuming state 0. The
ported door subphase is persisted in ground-agent runtime saves so a mid-sequence save cannot skip
or repeat deployment.

If no nonzero return message arrives within `0x780000` (120.0 s), `FUN_004b8c90` posts
`EVT_CriminalCaught` and the mission completes. If the initial person deployment fails, category 4
retires it without reward. Merely stopping the car never completes or pays the mission.

The searchlight remains essential: its counter rises by 2 to 10 while lit, slows the 1.75x car to
1.05/1.32/1.52 by band, and permits the police pull-over order. An unmarked moving car ignores that
police order.

### Riot

Rioters leave through BHAV 311 once agitation falls below 3. If the helicopter is within six tiles,
outcome 4 posts `EVT_RioterDispersed` and pays +10 points/+10 cash; otherwise outcome 5 posts
`EVT_RioterCalmed` with no incremental reward. A riot therefore can finish without a final player
interaction. That is retail behavior, not itself the bug. The missing part was the reminder/erosion
path, which prevents a long-ignored riot from retaining its full end award.

Megaphone messages 2/3 reduce agitation by 1. Water reduces it by 1 with a 1-in-6 +8 backfire; tear
gas reduces it by 2 with a +5 backfire. Spotlight makes rioters flee/throw but does not calm them.
The riot completes when
`dispersed + casualties + criminalsCaught + calmed >= riotSize`.

### Rooftop Rescue

State 2 maps to BHAV 700. BHAV 305 looks for the player's helicopter or rescue bucket/harness,
moves to it, boards, and posts the pickup outcome. BHAV 303 handles release when appropriate and
posts rescue delivery before deactivation. The mission phase is therefore roof -> aboard/harness ->
safe release; there is no record-owned destination. It completes when
`RescueDelivered + Casualties == RescueVictims` and clears its primary marker.

The deployed rescue harness is a valid rooftop pickup even while the airframe is too high for
direct cabin boarding. BHAV 305 selects and boards the rope end; opcode 58 keeps the survivor on
it until the harness is raised, then transfers that same person into a free cabin seat. The
mission-side recovery path uses the rope-end position and the same harness-rider carrier state, so
it does not require `CanBoardMissionPassengers()` until the rider is wound into the cabin.

**Cabin exit placement (2026-08-09 follow-up):** `FUN_004c6450` keeps a rider at the carrier's
position and clearing their master in `FUN_004c6360` leaves them there. A literal port would put
them inside the remake's collision body, so the shared alight action adapts that point to one body
clearance outside the rendered fuselage bounds. It resolves the passenger's actual seat row before
returning the seat. The mission recovery path no longer adds its old alternating world-space
spread, which had moved later survivors progressively farther from the helicopter.

**Rooftop delivery guard (2026-08-09 follow-up):** BHAV 303 reaches opcode 17 before posting its
delivery outcome. Opcode 17 ends in `FUN_004c9bc0`, whose release test is strictly less than six
original units above the terrain under the passenger. The remake had replaced that with the
helicopter's generic landed-surface gate; because rendered building roofs are valid landing and
walking surfaces, a state-2 survivor could board and immediately step back out on the same or an
adjacent roof. Both the decoded opcode path and the mission recovery release now require dry,
terrain-level ground for Rescue and Transport passengers. State-6 MedEvac patients deliberately
bypass only the height restriction so BHAV 263 can still unload them on the D1 hospital roof; water
remains invalid for every passenger kind.

## Reminder timing and penalties

The reminder interval is the difficulty-scaled 600 s mission timer divided by eight:

| tier | interval |
|---:|---:|
| 1 | 75.0 s |
| 2 | 56.25 s |
| 3 | 50.0 s |
| 4 | 37.5 s |

For all four crimes and riots, the timer advances only when the player is on foot or the weighted
tile distance is greater than 12, where distance is
`min(abs(dx),abs(dy)) + 2 * max(abs(dx),abs(dy))`. Flying near the scene pauses it. Rescue timers
always advance.

| mission | event | HUD text id | immediate score |
|---|---:|---|---:|
| Robber | `0x2a` | `0x3b4` `Burglary Committed!` | -10 |
| Arsonist | `0x2b` | `0x3b5` `Arsonist On Loose!` | -10 |
| Mugger | `0x28` | `0x3b2` `Sim Mugged!` | -10 |
| Burglar | `0x2a` | `0x3b4` `Burglary Committed!` | -10 |
| Riot | `0x29` | `0x3b3` `SOS!` | -20 and increment elapsed-period count |
| Rooftop Rescue | `0x29` | `0x3b3` `SOS!` | -10 |

The `[Riot Miss]` tweak also binds `Riot Timer`, `End Money Penalty`, and `End Points Penalty`, but
those globals have no runtime references outside the binder. Applying them as a timeout would be an
invented rule.

## Rewards

- Every Robber, Arsonist, Mugger, or Burglar completion: +300 points, +$500; type voice 100.
- Riot completion: `(6 - elapsedPeriods) / 6` of 505 points and $725, integer arithmetic, and zero
  once elapsed periods reaches 6. This is in addition to each nearby dispersed rioter's +10/+10.
- Rooftop pickup: +$10 immediately (`0x3aa`, `Sim Picked Up!`).
- Rooftop delivery: +$50 immediately (`0x3a7`, `Sim Rescued!`).
- Rooftop completion: +100 points and +$200 per delivered person, -100/-$200 per casualty, plus one
  additional per-person end unit for every four pickup credits (`VictimsPickedUp >> 2`). A negative
  aggregate cash result is floored to zero before it reaches the wallet; negative points are not.

## Voice lines

Every announcement is intro `0x2f` at volume `0x96`, mission type at `0x32`, one of location
`0x42-0x4a` for the map's 3x3 sectors at `0x32`, then a detail at `0x32`.

| mission | type voice | detail selection |
|---|---:|---|
| Robber | `0x36` D1007 | roll 1: pool; 2: `0x58`; 3: `0x5a`; otherwise `0x4f` |
| Arsonist | `0x38` D1009 | roll 1: pool; 2: `0x57`; 3: `0x4f`; otherwise `0x52` |
| Mugger | `0x39` D1010 | roll 1: pool; 2: `0x58`; 3: `0x5a`; otherwise `0x4f` |
| Burglar | `0x36` D1007 | roll 1: pool; 2: `0x58`; otherwise `0x50` |
| Riot | `0x3e` or `0x3f` | six-entry pool |
| Rooftop Rescue | `0x40` D1017 | fixed `0x53` D2009 |

The six-entry detail pool is `0x4b, 0x4d, 0x4e, 0x51, 0x55, 0x5d`.

On positive completion, `FUN_004aabf0` plays the type-specific completion voice at volume `0x96`
(crime 100, riot `0x66`, land/roof rescue `0x67`) and then one random success tag from
`0x6e, 0x6d, 0x6c, 0x6b, 0x6a` at volume `0x32`. A non-positive result skips both and plays fixed
failure `0x60` at volume `0x96`. Consuming that success-pool PRNG draw is required for later mission
alignment.

## Runtime ownership and police deployment traps

- `FUN_004b9e40` deploys an officer through `FUN_004bd980(0x0e, personState)`. The first value is
  behavior class 14; it must not be passed as an unspecified/random class. The person state is 8
  for the robber foot chase (BHAV 1401 -> 1150) and `0x0e` for a fleeing-car stop (BHAV 1402).
- A `CARROBBR` actor must carry its event in both `CriminalEventId` (the decoded car-state field)
  and the remake's shared `MissionEventId` ownership field. Population pruning runs before the
  criminal-car update; without the shared field, a correctly spawned getaway car outside the
  ambient traffic radius is destroyed before its first state-machine tick. Because mission-owned
  actors bypass ambient culling, `UpdateCriminalCars` must explicitly destroy the car once its
  mission record is no longer active.
- `FUN_004aa150` emits STRINGTABLE ids rather than generic score labels. In particular event
  `0x2a` emits `0x3b4`, whose retail text is `Burglary Committed!`. The full decoded function is
  preserved at `Docs/scratchpad/ghidra/mission_incremental_text_004aa150.txt`; ids `0x3a2..0x3c1`
  are not a sequential set of the remake's former car/death labels and must be mapped directly.

## Port and verification anchors

- Core records/lifecycle/scoring/voice: `FSimCopterMissionSystem`.
- World spawning, rendered roof placement, recurring car state: `ASimCopterTrafficSystemActor`.
- BHAV return-to-car ownership and save v3 recurrence/door-phase fields: `ASimCopterGroundAgent`.
- Names and markers: `ASimCopterMissionSystemActor`, `SimCopterMissionCatalog.cpp`, and
  `SimCopterHangarShop.cpp`.
- Regression suites: `SimCopter.Missions.*`, `SimCopter.Crime.*`, and
  `SimCopter.Behavior.VM.Reference`.
