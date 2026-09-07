# Advancing between career cities keeps the whole career block

*"`FUN_00408210` clears exactly one field. The remake was running `FUN_00407f30` instead, so
finishing a level took your money, your fleet and your fittings with it."*

*Recorded 2026-08-27.*

## Ground truth

`FUN_0044bf70`, the career-select page's message handler (control **0x7d7**), branches the OK on
the "new career" flag `app+0xb0` — set only by main-menu item 0:

```
city = screen[0x2a + screen[0x1d]];
if (app[0xb0] == 0) FUN_00408210(city);   // ADVANCING through a career
else                FUN_00407f30(city);   // a brand NEW career
FUN_0044ce50(city);                       // the cityride intro
```

Career block base **0x518d6c** (user-mode block 0x518cf8):

| offset | field | `FUN_00407f30` (new) | `FUN_00408210` (advance) |
| --- | --- | --- | --- |
| +0x40 | money | 1000 | **kept** |
| +0x44 | owned-helicopter mask | 0x10 (Schweizer, runtime type 4) | **kept** |
| +0x48 | equipment mask | 3 (bucket + megaphone) | **kept** |
| +0x50 | score | 0 | **0 — the only field it writes** |
| +0x54 | tear gas rounds | 0 | **kept** |

`FUN_00408210`'s tail is literally one store: `*(careerBase + 0x50) = 0`.

The aircraft is a separate matter. City entry runs `FUN_0047a240`, which walks the owned mask and
hands each airframe a pad through `FUN_00484790` — and that function writes
`heli[0x34] = DAT_0050412c[type]` (per-type **max hit points**), `heli[0xcc] = DAT_00504120[type]`
(**full tank**), `heli[0xcd] = 0` (the **depreciation** `FUN_0048b070` subtracts from the trade-in)
and `heli[0x74] = 0` (empty water). **So an aircraft always arrives in a new city serviced, and
fuel/damage/depreciation must NOT be carried over — only the airframe, its fittings and its ammo.**

## What was wrong

`ASimCopterMissionSystemActor::BeginSession` unconditionally ran both resets — `MissionSystem
.BeginSession()` ($1000, 0 points) and `Career->BeginCareer()` (books back to the Schweizer alone,
log cleared) — so every career city started as a new career. Completing a level and choosing the
next city wiped cash, the fleet, the equipment mask, the tear-gas magazine and the mission log, and
put the player back in a Schweizer. The `MoneyEarned` award paid at level complete was wiped by the
same call, seconds after it was granted.

## The port

The original leaves the block in static memory; the remake travels
`CityRender -> /Game/MainMenu -> CityRender`, which destroys the mission system actor (cash) and the
helicopter pawn (airframe, fittings, ammo). `FSimCopterCareerCityTransfer` on
`USimCopterCareerSubsystem` — a game-instance subsystem, so it outlives both travels — carries them:

- `ProcessLevelCompleteLanding` calls `CaptureCareerCityTransfer()` right before `OpenLevel`.
- `BeginSession` treats a pending transfer as `app[0xb0] == 0`: `FSimCopterMissionSystem::
  ContinueSession(cash)` instead of `BeginSession()`, and `ContinueCareerIntoNextCity()` (fleet and
  log kept, depreciation zeroed) instead of `BeginCareer()`.
- `ASimCopterGameMode::ApplyPendingAircraftRestores` applies the aircraft half **after** the pad
  pass, through `ApplyCareerCityTransfer` (type + equipment + rounds, then full fuel and hit points
  per `FUN_00484790`), and clears the transfer.
- `USimCopterSaveSubsystem::BeginNewGame()` and `LoadGame()` clear it — those are the remake's
  `app[0xb0] = 1`.

## Three traps in the same flow

1. **`SwitchHelicopterModel` carries the old fuel/damage *fractions* over** (`CommitHelicopterModel`
   normalises them on purpose), and it early-returns when the type already matches. Write full fuel
   and hit points *after* the switch, never before or instead.
2. **Cancel must not exist on an advancement.** `FUN_00457c90` gives Cancel only when
   `screen[0x2e] != 0`, i.e. a new career; an advancement gets one centred OK at x 380 and a dead
   Esc. The remake passed `AllowCancel(true)` unconditionally, so backing out of an advancement
   dropped the player at the main menu holding a career with no way back into it.
3. **City 29 (Metropolis, Final Level) has an all -1 successor trio**, so finishing it produces an
   empty choice list and the front end falls back to the new-career trio {0, 1, 2}. That is a new
   career, not a continuation: the transfer has to be dropped there or the ladder restarts carrying
   the last run's bank.

Also latched `bLevelCompleteAdvanceRequested`: `OpenLevel` only *queues* the travel, so the actor
can tick again, and the `MoneyEarned` award now survives — paying it twice would be a real
duplicate rather than something the old reset swallowed.

## Verified

Built clean with `RebuildUnrealCpp.bat`. `SimCopter.Career.CityTransition` and
`SimCopter.Career.FinalCityEndsTheLadder` cover both openings, the clamp at zero and the end of the
ladder; the full suite is 243 pass / 1 fail, that one being
`SimCopter.Formats.SimCity2000.ReferenceCity`, which fails identically on the unmodified tree
because this machine has no configured original-game root. **Not verified on screen** — the actual
level-complete -> career-select -> next-city round trip has not been flown.
