# Missions, Career, and the Maxis Tweak System

This document covers the original SimCopter text tuning files under `Reference/SimCopterOriginalGame/tweak/` and the mission/career model they encode. It is the missing companion to `Docs/OriginalGameFileCodeWalkthrough.md`, which already documents the C++ `FSimCopterTweakReader` parser and `heli.twk`. The focus here is on what the tuning _means_ for gameplay, especially the career progression and the nine mission types, because the remake intends to run the original missions.

Status legend (from `Docs/DecompilationWorkflow.md`):

- `Confirmed`: the value or structure is literally present in the shipped tweak files.
- `Hypothesis`: plausible interpretation of how the engine consumes the value, not yet cross-checked against `SimCopter.exe`.
- `Follow-up`: known missing reverse-engineering work.

The mission layer described here is now ported: see `SimCopterRemake/Source/SimCopterRemake/Public/Missions/SimCopterMissionSystem.h` (scheduler, scoring, fire model, career table) and `Docs/Milestone5SimulationPlan.md`. The session/career-shell half is decoded in `Docs/scratchpad/ghidra/session_modes_and_menu_20260724.md`; the remake exposes it through a debug main menu rather than the original's bitmap screens. Sections below are still the authoritative transcription of the shipped tuning data.

## The `.twk` File Format

`Confirmed.` Every `tweak/*.twk` file is line-oriented ASCII text. The shipped editor is `tweak/tweak.exe`, an in-house Maxis tool; the game reads the same files at load time. There are two kinds of file: **index files** that describe the tweak tree, and **leaf sections** that hold tunable controls.

### Lines

- A comment line begins with `#` or `%`. Shipped files use long runs of `%%%%%` (data files) or `#####` (the master index) as visual separators. `FSimCopterTweakReader::IsCommentOrBlank` already treats `#`, `;`, and `%` leading characters as comments.
- A section header is `[Section Name]`. Section names can contain spaces (`[Heli Ropestuff]`, `[General Miss]`).
- Everything else is `Key=Value`. The key is normalized (trimmed, lowercased) by `NormalizeTweakKey`; the value keeps its original spelling.

### Control blocks (leaf sections)

A leaf section declares a count and then a numbered block per control:

```text
[Fire Parms]
NumCtrl=6

Ctrl0_Label=Douse Points
Ctrl0_Type=Slider,10,128
Ctrl0_Value=37.3
Ctrl0_DataType=fxpt
```

Per-control keys:

- `Ctrl<i>_Label`: human-readable name. **This is the only documentation of intent the engine ships with**, so the labels are quoted verbatim throughout this doc.
- `Ctrl<i>_Type`: editor widget. Almost always `Slider,<min>,<max>`. The min/max are the editor clamp range, not necessarily an engine clamp.
- `Ctrl<i>_Value`: the actual tuned value.
- `Ctrl<i>_DataType`: one of `int`, `double`, or `fxpt`.

`NumCtrl` is the count of control blocks. Note `Docs/GameplayCodeWalkthrough.md` records that the helicopter loader deliberately reads controls _by label prefix_, not by trusting `NumCtrl`; the same caution applies to any future mission loader because some shipped sections have blank-line noise and slightly inconsistent counts.

### The `fxpt` data type

`Confirmed (present) / Hypothesis (semantics).` `fxpt` is the most important and least obvious type. The text stores a decimal such as `426.7`, `37.3`, `380.0`, or `0.2`, but the engine stores it as a fixed-point integer. SimCopter is a 1996 software-rendered title and uses fixed-point math throughout (the mesh vertices use 262144 units/meter; see `Docs/OriginalGameFileCodeWalkthrough.md`). The label `MaxBank (10 = 1 deg)` in `heli.twk` is the giveaway: a stored value of `426.7` means 42.67 degrees, i.e. the _displayed_ number is already scaled by 10 for the editor. The remake's `SimCopterHelicopterPawn` calls this `TweakAngleScale` and divides angle controls by 10.

Consequences for a future mission loader:

- `int` controls are plain integers (money, points, weights, counts).
- `fxpt` controls are decimals whose engine meaning depends on the field. Time fields (`TimeToLive (secs)`, `Riot Timer (secs)`) appear to be plain seconds. Angle/rate fields use the `10 = 1 deg` convention. Treat each `fxpt` field individually; do not assume a single global scale.
- `FSimCopterTweakReader` currently exposes `GetFloat`/`GetInt` only and does not model the `Ctrl<i>_*` grouping or the `fxpt`/`int`/`double` distinction. That is the main parser `Follow-up` for this system.

### Index files and the tweak tree

`Confirmed.` `sim3d.twk` is the master index. It uses a different key vocabulary:

- `[General Administration]` with `ReadFile=ALL` / `ReadSection=` is the loader directive header.
- `Prefix=<P>` plus `Num<P>=<n>` plus `<P>0..<P>(n-1)=<SectionName>` enumerates child nodes. For example the `[Class]` node has `Prefix=Class`, `NumClass=7`, `Class0=Heli` ... `Class6=Missions`.
- A child section either contains its own controls (a leaf) or contains `Redirect=<file>.twk`, meaning "this section lives in another file."

So the full tree is:

```text
sim3d.twk  [Class]
  Heli         -> [Heli] -> Heli Types/Landing/Ropestuff/Damage -> Redirect heli.twk
  Fire         -> [Fire] -> Fire Parms                          -> Redirect fire.twk
  Camera       -> [Camera] -> Camera Parms                      -> Redirect camera.twk
  Figure       -> [Figure] -> Figure Parms                      -> Redirect figure.twk
  Career       -> [Career] -> City0..City29                     -> Redirect career.twk
  AutoMissions -> [AutoMissions] -> AutoMission                 -> Redirect automssn.twk
  Missions     -> [Missions] -> 9 mission sections              -> INLINE in sim3d.twk
```

The `[Missions]` class is defined **inline** inside `sim3d.twk` rather than redirected. The shipped file even has the developer's comment explaining why: `# edg: for some reason I had problems with redirect into another file`. ("edg" is a Maxis engineer's initials.) Any tool that resolves the tweak tree must therefore read mission tuning from `sim3d.twk` itself, not from a `missions.twk`.

`sim3d.twk` also carries two flat (non-control) nodes that the editor tree does not redirect:

- `[Joystick]` (`Threshold=20`, `View Yaw Multiplier=1.0`, `View Pitch Multiplier=-1.0`, `View Roll Multiplier=1.0`). It appears twice in the file; the second copy only sets `Threshold`.

## Per-File Reference

### `heli.twk` - helicopter tuning

Covered in `Docs/GameplayCodeWalkthrough.md` and `Docs/OriginalGameFileCodeWalkthrough.md`. Summary for completeness: nine `[<HeliType>]` sections (Jet Ranger, Hughes 500, Apache, Bell 212, Schwiezer 300, Agusta, Dauphin, MDEXPLORER, MD520) each with 14 controls (MaxBank/MaxSlide/MaxPitch as `10 = 1 deg` fxpt, the four rates, ClimbRate, Max Load, Max YawRate, Fuel Rate gal/hr, New Cost $, Max Damage, Fuel gals), plus shared `[Heli Landing]`, `[Heli Ropestuff]`, and `[Heli Damage]` sections. The mission economy references the per-helicopter `New Cost ($)` when the player buys/replaces aircraft.

### `camera.twk` - chase camera

`Confirmed.` `[Camera Parms]`, 3 fxpt controls:

| Label         | Value | Meaning (Hypothesis)                                |
| ------------- | ----- | --------------------------------------------------- |
| Chase Maxdist | 233.1 | maximum chase-camera distance behind the helicopter |
| Chase MinDist | 8.8   | minimum chase distance (zoomed in)                  |
| Chase Height  | 29.7  | camera height above the helicopter                  |

Units are original world units (the same fixed-point space as mesh/city geometry). The remake's modern spring-arm camera is not driven by these yet (`Follow-up`).

### `fire.twk` - fire model

`Confirmed.` `[Fire Parms]`, 6 controls. This drives how fires burn and how the bucket douses them.

| Label                 | Type | Value | Meaning (Hypothesis)                                     |
| --------------------- | ---- | ----- | -------------------------------------------------------- |
| Douse Points          | fxpt | 37.3  | water "health" removed from a fire cell per douse        |
| Douse Mult            | int  | 21    | multiplier applied to douse effect                       |
| TimeToLive (secs)     | fxpt | 190.3 | how long an unfought fire cell burns before burning out  |
| SpreadInterval (secs) | fxpt | 34.7  | how often a fire tries to spread to a neighbor           |
| SpreadProb            | int  | 224   | spread probability, out of 1024 (range max is 1024)      |
| Fire Radius           | fxpt | 43.9  | physical radius of a fire cell for collision/douse tests |

`SpreadProb` having a slider max of `1024` strongly implies the engine rolls a random number against 1024 (a power-of-two mask, cheap in fixed point). The fire mission scoring (below) is separate, in `[Fire Miss]`.

### `figure.twk` - people LOD and simulation

`Confirmed.` `[Figure Parms]`, 11 controls. "Figure" is SimCopter's word for an articulated person (from `privanim.df`; see `Docs/OriginalGameFileFormats.md`). This section is the crowd LOD/sim-budget governor.

| #   | Label                           | Type   | Value | Meaning (Hypothesis)                                                                     |
| --- | ------------------------------- | ------ | ----- | ---------------------------------------------------------------------------------------- |
| 0   | Max random ambient              | int    | 55    | cap on ambient (background) figures                                                      |
| 1   | Max ambient period              | int    | 76    | spacing/period between ambient spawns                                                    |
| 2   | Far limit                       | int    | 1305  | distance past which figures are not drawn                                                |
| 3   | Far boundary                    | int    | 716   | far LOD switch distance                                                                  |
| 4   | Med boundary                    | int    | 111   | medium LOD switch distance                                                               |
| 5   | Near boundary                   | int    | 7     | near LOD switch distance                                                                 |
| 6   | Beaming rect radius             | int    | 8     | radius for "beam in" placement of new figures near the player                            |
| 7   | Master is slow when slower than | double | 0.1   | speed threshold below which a figure's animation "master" is considered slow/idle        |
| 8   | Don't sim past dist             | int    | 8     | beyond this tile distance, figures are not simulated (only the nearest 8 tiles are live) |
| 9   | Adjust feet by this vert dist   | int    | -18   | vertical offset so feet sit on the ground                                                |
| 10  | Consider this large             | int    | 4     | size threshold for treating a figure group as "large"                                    |

Control 9 (`Adjust feet by this vert dist = -18`) is independent confirmation of the ground-snap "feet on the ground" problem the remake hit with procedural pedestrians (see memory `simcopter-population-rendering`). Control 8 (`Don't sim past dist = 8`) explains why SimCopter crowds feel local: only an ~8-tile bubble around the player is actually simulated.

### `automssn.twk` - auto-mission assist

`Confirmed.` `[AutoMission]`, 2 int controls. This is the autopilot/auto-assignment helper, not a mission type.

| Label            | Range | Value | Meaning (Hypothesis)                                            |
| ---------------- | ----- | ----- | --------------------------------------------------------------- |
| Fire Scan (3-10) | 3..10 | 5     | radius/interval the auto-pilot scans for fires to assign        |
| Road Scan (3-10) | 3..10 | 5     | radius/interval the auto-pilot scans for traffic/road incidents |

## The Career: `career.twk`

`Confirmed.` The career is 30 cities, `City0`..`City29`, listed in `sim3d.twk` under `[Career]` (`NumCAREER=30`). Each city is an 11-control section in `career.twk`. The controls are identical in shape across all 30 cities; only the values change.

`Confirmed (exe).` The cities are **not** played in order. Each city's in-memory record (`DAT_00518dcc + city * 0x50`, built by `FUN_00408370`) carries up to three successor city indices at +0x24/+0x28/+0x2c, and the career-select screen offers exactly those; a new career starts with the choice {City0, City1, City2}. `career.twk` supplies only the first nine controls of the record (+0x00..+0x20) plus Points Needed (+0x44) and $ Earned (+0x48) - the successor graph and the map name are hardcoded in the exe. Full record layout in `Docs/scratchpad/ghidra/session_modes_and_menu_20260724.md`.

Per-city controls (order is fixed: `Ctrl0`..`Ctrl10`):

| Ctrl | Label              | Meaning                                                 |
| ---- | ------------------ | ------------------------------------------------------- |
| 0    | Difficulty (0-3)   | overall difficulty tier                                 |
| 1    | Fire (weight)      | relative spawn weight of Fire missions                  |
| 2    | Crime (weight)     | relative spawn weight of Criminal missions              |
| 3    | Rescue (weight)    | relative spawn weight of Rescue missions                |
| 4    | Riot (weight)      | relative spawn weight of Riot missions                  |
| 5    | Traffic (weight)   | relative spawn weight of Traffic-jam missions           |
| 6    | MedEvac (weight)   | relative spawn weight of Medevac missions               |
| 7    | Transport (weight) | relative spawn weight of Transport missions             |
| 8    | Day or Night       | 0 or 1 time-of-day flag                                 |
| 9    | Points Needed      | score required to complete the city and unlock the next |
| 10   | $ Earned           | money awarded for / carried into the city               |

`Hypothesis.` The seven `(weight)` controls are a relative distribution: when the random mission scheduler (see `[General Miss]` below) decides to spawn an event, it picks a mission type proportional to these weights. A weight of `0` disables that mission type in that city. This is why early cities have `Crime=0`, `Rescue=0`, `Riot=0` (only traffic/medevac/transport/light-fire), and why riots first appear at City21 (`Riot=5`) and dominate the final cities (`Riot=20`).

`Confirmed (exe).` `Points Needed` is the city win condition (it climbs 400 -> 3000): `FUN_00408c30` compares the session score against it and, once reached, stops the scheduler from creating further missions. `$ Earned` (+0x48, read by `FUN_00407b80` clamped to >= 1) _decreases_ 500 -> 100 as cities get harder, so later cities pay less and demand more. A session - career or single city - always opens with $1000 and 0 points (`FUN_00407f30` / `FUN_004080c0`).

`Hypothesis.` `Day or Night` 0/1 selects the lighting/time set; the value is copied to `DAT_004f9720` when a city is entered (and derived from `FUN_00448e80` in single-city mode). Which value means night is still a `Follow-up`.

### All 30 cities

`Confirmed` (values transcribed directly from `career.twk`). Columns: Difficulty, then the seven mission weights, then Day/Night, Points Needed, $ Earned.

| City | Diff | Fire | Crime | Rescue | Riot | Traffic | MedEvac | Transport | D/N | Points | $   |
| ---- | ---- | ---- | ----- | ------ | ---- | ------- | ------- | --------- | --- | ------ | --- |
| 0    | 0    | 10   | 0     | 0      | 0    | 30      | 30      | 30        | 1   | 400    | 500 |
| 1    | 0    | 10   | 0     | 0      | 0    | 30      | 30      | 30        | 0   | 400    | 500 |
| 2    | 0    | 10   | 0     | 0      | 0    | 30      | 30      | 30        | 1   | 400    | 500 |
| 3    | 0    | 10   | 10    | 5      | 0    | 30      | 20      | 25        | 0   | 700    | 500 |
| 4    | 0    | 10   | 10    | 5      | 0    | 30      | 20      | 25        | 1   | 700    | 500 |
| 5    | 0    | 10   | 10    | 5      | 0    | 30      | 20      | 25        | 0   | 700    | 500 |
| 6    | 0    | 15   | 20    | 10     | 0    | 20      | 20      | 15        | 0   | 1000   | 450 |
| 7    | 0    | 15   | 20    | 10     | 0    | 20      | 20      | 15        | 0   | 1000   | 450 |
| 8    | 0    | 15   | 20    | 10     | 0    | 20      | 20      | 15        | 1   | 1000   | 450 |
| 9    | 1    | 20   | 20    | 10     | 0    | 15      | 20      | 15        | 1   | 1200   | 400 |
| 10   | 0    | 20   | 20    | 10     | 0    | 15      | 20      | 15        | 0   | 1200   | 400 |
| 11   | 1    | 20   | 20    | 10     | 0    | 15      | 20      | 15        | 1   | 1200   | 400 |
| 12   | 1    | 20   | 20    | 15     | 0    | 15      | 15      | 15        | 0   | 1500   | 350 |
| 13   | 1    | 20   | 20    | 15     | 0    | 15      | 15      | 15        | 1   | 1500   | 350 |
| 14   | 1    | 20   | 20    | 15     | 0    | 15      | 15      | 15        | 0   | 1500   | 350 |
| 15   | 1    | 25   | 20    | 20     | 0    | 10      | 15      | 10        | 0   | 1800   | 300 |
| 16   | 1    | 25   | 20    | 20     | 0    | 10      | 15      | 10        | 1   | 1800   | 300 |
| 17   | 1    | 25   | 20    | 20     | 0    | 10      | 15      | 10        | 1   | 1800   | 300 |
| 18   | 2    | 25   | 20    | 20     | 0    | 10      | 15      | 10        | 1   | 2000   | 250 |
| 19   | 2    | 25   | 20    | 20     | 0    | 10      | 15      | 10        | 0   | 2000   | 250 |
| 20   | 2    | 25   | 20    | 20     | 0    | 10      | 15      | 10        | 0   | 2000   | 250 |
| 21   | 2    | 20   | 25    | 20     | 5    | 10      | 10      | 10        | 1   | 2200   | 200 |
| 22   | 2    | 20   | 25    | 20     | 5    | 10      | 10      | 10        | 0   | 2200   | 200 |
| 23   | 2    | 25   | 25    | 25     | 5    | 5       | 10      | 5         | 0   | 2400   | 150 |
| 24   | 2    | 25   | 25    | 25     | 5    | 5       | 10      | 5         | 1   | 2400   | 150 |
| 25   | 3    | 25   | 25    | 20     | 10   | 5       | 10      | 5         | 1   | 2500   | 100 |
| 26   | 3    | 25   | 25    | 20     | 10   | 5       | 10      | 5         | 0   | 2500   | 100 |
| 27   | 3    | 20   | 25    | 20     | 20   | 5       | 5       | 5         | 0   | 2700   | 100 |
| 28   | 3    | 20   | 25    | 20     | 20   | 5       | 5       | 5         | 1   | 2700   | 100 |
| 29   | 3    | 20   | 20    | 15     | 20   | 10      | 10      | 5         | 1   | 3000   | 100 |

Observed progression (useful for a remake difficulty curve):

- Difficulty tier climbs 0 (cities 0-8, plus 10) -> 1 (9-17) -> 2 (18-24) -> 3 (25-29).
- Crime/Rescue start at 0 and only switch on at City3; Riot stays 0 until City21.
- Traffic/MedEvac/Transport weights start high (the gentle intro missions) and shrink as Fire/Crime/Riot take over.
- Points Needed roughly doubles across the career while $ Earned drops 5x, so the money squeeze is the real difficulty ramp, not the point target alone.
- `career.twk` only defines the per-city tuning. `Confirmed (exe)`: the map comes from record +0x40 (`"city0"`.."city29"), with `".sc2"` appended and resolved against search-path class 7 (`"cities\career\"`), i.e. **career city N plays `cities\career\cityN.sc2`** - the 30 files shipped in `Reference/SimCopterOriginalGame/cities/career/`.

## The Mission Types: inline `[Missions]` in `sim3d.twk`

`Confirmed.` `[Missions]` lists nine sections (`NumMISN=9`): General Miss, Riot Miss, Transport Miss, Rescue Miss, Medevac Miss, Fire Miss, Criminal Miss, Speeder Miss, Traffic Miss. Each is the money/points rule set for that mission category. `(pp)` in a label means "per person"; `(*size)` means the value scales with fire size.

### `[General Miss]` - the random event scheduler

This is not a mission the player flies; it is the global cadence and the special UFO event.

| Label                | Type | Value | Meaning (Hypothesis)                              |
| -------------------- | ---- | ----- | ------------------------------------------------- |
| Max Easy             | int  | 2     | max simultaneous "easy" missions active           |
| Easy Interval (secs) | fxpt | 380.0 | base time between mission spawns                  |
| Interval Adj         | fxpt | 0.2   | fraction the interval shrinks as difficulty rises |
| UFO Money            | int  | 2000  | reward for the rare UFO event                     |
| UFO Points           | int  | 1000  | points for the UFO event                          |

`Hypothesis.` The scheduler waits ~`Easy Interval` seconds (scaled down by `Interval Adj` per difficulty tier) and then spawns a mission whose type is chosen by the current city's weight vector. `Max Easy` caps concurrent low-tier missions so the city does not flood.

### `[Riot Miss]`

| Label              | Type | Value | Meaning                        |
| ------------------ | ---- | ----- | ------------------------------ |
| Riot End Money     | int  | 725   | money for quelling the riot    |
| Riot End Points    | int  | 505   | points for quelling the riot   |
| End Money Penalty  | int  | 0     | money lost on failure          |
| End Points Penalty | int  | 250   | points lost on failure         |
| Riot Timer (secs)  | fxpt | 197.8 | time limit to resolve the riot |

### `[Transport Miss]` - per-person passenger transport

| Label            | Type | Value | Meaning                                     |
| ---------------- | ---- | ----- | ------------------------------------------- |
| End Money(pp)    | int  | 100   | money per delivered passenger at completion |
| End Points(pp)   | int  | 50    | points per delivered passenger              |
| Inc Trans (pp$)  | int  | 20    | incremental $ per transport step            |
| Inc Pickup (pp$) | int  | 10    | incremental $ per pickup                    |

### `[Rescue Miss]` - per-person rescue

| Label               | Type | Value | Meaning                           |
| ------------------- | ---- | ----- | --------------------------------- |
| Resc End Money(pp)  | int  | 200   | money per rescued person          |
| Resc End Points(pp) | int  | 100   | points per rescued person         |
| Resc Inc Money(pp)  | int  | 50    | incremental $ per rescue progress |

### `[Medevac Miss]` - per-person medical evacuation

| Label              | Type | Value | Meaning                            |
| ------------------ | ---- | ----- | ---------------------------------- |
| Med End Money(pp)  | int  | 200   | money per evacuated patient        |
| Med End Points(pp) | int  | 100   | points per evacuated patient       |
| Inc Medevac (pp$)  | int  | 30    | incremental $ per medevac progress |

### `[Fire Miss]` - the fire-fighting economy (20 controls)

The richest mission type. Money/points scale with the fire and there are many sub-events (plane crash, train crash, car fire, debris). Negative-point labels are penalties for letting things burn.

| #   | Label               | Type | Value | Meaning                                    |
| --- | ------------------- | ---- | ----- | ------------------------------------------ |
| 0   | End Money(\*size)   | int  | 150   | base completion money, scaled by fire size |
| 1   | End Points(\*size)  | int  | 100   | base completion points, scaled by size     |
| 2   | End Money Penalty   | int  | 0     | money lost on failure                      |
| 3   | End Points Penalty  | int  | 50    | points lost on failure                     |
| 4   | Plane Crash($)      | int  | 200   | bonus $ for handling a plane-crash fire    |
| 5   | Plane Crash(pts)    | int  | 100   | bonus points                               |
| 6   | Train Crash($)      | int  | 100   | bonus $ for a train-crash fire             |
| 7   | Train Crash(pts)    | int  | 100   | bonus points                               |
| 8   | Car Fire($)         | int  | 100   | $ for dousing a car fire                   |
| 9   | Car Fire(pts)       | int  | 50    | points for a car fire                      |
| 10  | Debris Fire($)      | int  | 50    | $ for dousing debris fire                  |
| 11  | Debris Fire(pts)    | int  | 50    | points for debris fire                     |
| 12  | Flame($)            | int  | 20    | $ per flame cell doused                    |
| 13  | Flame(neg pts)      | int  | 50    | points lost per flame that keeps burning   |
| 14  | Bldg Dest(neg pts)  | int  | 200   | points lost per building destroyed         |
| 15  | Bldg Saved($)       | int  | 150   | $ per building saved                       |
| 16  | New Debris(neg pts) | int  | 50    | points lost when new debris ignites        |
| 17  | Debris Doused($)    | int  | 20    | $ per debris doused                        |
| 18  | Car Doused($)       | int  | 30    | $ per car doused                           |
| 19  | Car Burned(neg pts) | int  | 20    | points lost per car that burns up          |

### `[Criminal Miss]`

| Label      | Type | Value | Meaning                          |
| ---------- | ---- | ----- | -------------------------------- |
| End Money  | int  | 500   | money for catching the criminal  |
| End Points | int  | 300   | points for catching the criminal |

### `[Speeder Miss]`

| Label         | Type | Value | Meaning                           |
| ------------- | ---- | ----- | --------------------------------- |
| End Money     | int  | 200   | money for stopping the speeder    |
| End Points    | int  | 100   | points for stopping the speeder   |
| Incmtl Points | int  | 5     | incremental points while pursuing |

Note `Speeder` is not one of the seven career weight categories; it is likely a sub-case of `Crime`/`Traffic` enforcement (`Follow-up`).

### `[Traffic Miss]` - traffic jams

| Label            | Type | Value | Meaning                                         |
| ---------------- | ---- | ----- | ----------------------------------------------- |
| End Jam Money    | int  | 100   | money for clearing the jam                      |
| End Jam Points   | int  | 50    | points for clearing the jam                     |
| Jam Timer (secs) | fxpt | 60.0  | time before an unresolved jam fails / penalizes |

The remake's traffic system already models jams (`ESimCopterTrafficFlowMode`, see `Docs/GameplayCodeWalkthrough.md`); these are the original scoring/timer values that a mission layer would attach to a jam.

## How This Maps to the Remake

Done: the scheduler cadence, the weight-to-probability conversion (`FUN_004a6d20`), the `Interval Adj` formula, the per-`[* Miss]` scoring tables and the `fire.twk` fire model are ported in `FSimCopterMissionSystem`, which parses `career.twk` itself (`LoadCareerData`). Which city and session run is chosen in the front-end main menu (`SSimCopterMainMenu` in the `/Game/MainMenu` map) and handed to the city level through `USimCopterSessionSubsystem`.

Still open:

1. A tweak-tree loader that follows `sim3d.twk` `Class`/`Redirect`/`Prefix`/`Num*` directives and exposes structured `Ctrl<i>_*` controls with their `int`/`double`/`fxpt` types. `FSimCopterTweakReader` reads flat sections but does not model the tree or the control grouping.
2. The career successor graph (record +0x24/+0x28/+0x2c, hardcoded in `FUN_00408370`) and per-city map swapping. `AdvanceCareerCity` walks the list sequentially instead, and the remake plays whichever `.sc2` the city actor loaded.
3. The day/night flag mapping (which value is night).
4. Session block fields +0x44 (0x10) and +0x48 (3); nothing ported reads them yet.
5. Ambient-vehicle audio. The plane, train and boat sound ids are decoded (`TRAIN1.WAV` 0x19, `CRSH2.WAV` 0x1a, `DIVE1.WAV` 0x1b, `CESSLP1.WAV` 0x1c, from `FUN_00424b70`'s registration order) but the remake has no loader for positional original clips, so the ambient vehicles are silent.

`Plane Crash($)/(pts)` and `Train Crash($)/(pts)` above are a curiosity: `FUN_004ab170` binds all four, and then **nothing in the executable reads them**. `FUN_004aabf0` has no branch for type bit `0x4` or `0x100`, so a crash mission's own completion pays zero. What a crash is worth is whatever it starts - a building fire, a boat rescue for a plane that ditches in the water - plus the doubled per-person rescue award a train crash gives a train rescue (`0x110 | 0x100`).

### Plane crash, train crash, boat rescue and train rescue (ported 2026-07-27)

All four now place: `ASimCopterAmbientVehiclesActor` (`Source/SimCopterRemake/Public/Ground/SimCopterAmbientVehicles.h`) owns the original's three ambient vehicle pools - two planes (`DAT_00582910`, GEO `PLANE1` 0x12e and, in slot 1, the `UFO` 0x17c), three boats (`DAT_00582840`, GEO `CAPBOAT1` 0x163 in slot 0 and `BOAT1` 0x12f in slots 1-2) and one three-car train (`DAT_00582afc`, GEO `TRAIN1/2/3` 0x12d/0x14c/0x14d) - and implements the four mission hooks on them. Full decode notes: `Docs/scratchpad/ghidra/planes_trains_boats_decode_20260727.md`.

Two things that fall out of that decode and are worth having here:

- The "second plane" is the **UFO**. `FUN_004b2910` posts `EVT_UfoResolved` when a non-`PLANE1` plane starts its dive, which is what pays `[General Miss] UFO Money/Points`. The scheduler never rolls a UFO mission; the flying object is the source of that award.
- A plane that goes down **over water** creates a boat rescue (`FUN_004a7a10(tile, 0x90)`) rather than a fire, and retires its own record silently.
