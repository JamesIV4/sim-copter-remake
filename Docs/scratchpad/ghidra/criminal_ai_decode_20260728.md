# Criminals, cops and the arrest - decode notes (2026-07-28)

Answer to "can the catching-the-criminal code be decoded?": **yes, completely.** It is not engine
code at all - the whole thing is shipped BHAV data in `X/people.df`, driven by five behaviour
opcodes the remake had left as stubs. Nothing here is inferred from gameplay; every claim below
cites the function or the record it came from.

Tools: `ghidra-bridge` over `.ghidra-exports`, capstone for the jump-table bodies Ghidra dropped as
unreachable, and `Tools/people_bhav_dump.py` for the shipped programs.

## 1. The programs

`FPeopleBehaviorModel::GetStateProgramIds` (DAT_0058de80) binds person state -> program:

| state | BHAV | name |
|-------|------|------|
| 7  | 1400 | Cop aerial |
| 8  | 1401 | Cop foot |
| 10 | 1300 | Criminal Robber   (`TYPE_CriminalA`, spawn mode 10) |
| 11 | 1301 | Criminal Arsonist (`TYPE_SpeederEvent`, spawn mode 11) |
| 12 | 1302 | Criminal Mugger   (`TYPE_CriminalC`, spawn mode 12) |
| 13 | 1303 | Criminal Car |
| 14 | 1402 | Cop speeder |

Sub-programs: `1170 crim - cops or heli nearby`, `1171 crim - run from cop`,
`1172 crim - run from cop car`, `1173 crim - run from copter`, `1174 crim - walk unspotted`,
`1175 crim - try to mug`, `1078 crim - arsonist unspotted`, `1079 crim - do robbery`;
`1051 cop - wait at station`, `1052 cop - ride on copter`, `1053 cop - get out and chase`,
`1054 cop - return to copter`, `1055 cop - get out at station`; `1150 copf - chase criminal`,
`1151 copf - follow heli`, `1152 copf - return to cop car`; and `1060 Rx: criminal-caught`.

## 2. The marker following the criminal

`FUN_004caac0` (opcode 13) calls `FUN_004ccf50(record.args[0], person+0x10a)`, which maps a program
outcome code onto a mission event posted through `FUN_004a89c0`:

| outcome | event | notes |
|---------|-------|-------|
| 0  | 0x13 `EVT_VictimPickedUp` | |
| 1  | 0x10 / 0x15 / 0x11 / 0x12 | chosen by person+0x148 (state): 1/2/0x13 -> rescue, 3 -> rioter calmed, 4 -> transport, 6 -> medevac |
| 2  | 0x1e `EVT_SetTertiaryCoords` | carries person+0x12a/+0x12c; skips the value store |
| 4  | 0x14 `EVT_RioterDispersed` | |
| 5  | 0x15 `EVT_RioterCalmed` | |
| 6  | 0x00 `EVT_SetPrimaryCoords` | carries person+0x12a/+0x12c; skips the value store |
| 7  | 0x12 `EVT_MedevacDelivered` | |
| 8  | 0x13 `EVT_VictimPickedUp` | |
| 9  | 0x25 `EVT_CriminalCaught` | |
| 10 | 0x17 `EVT_PersonDied` | |
| 11 | 0x1f `EVT_PassengerLost` | |

Outcome **6** is the answer to "the tag should follow the criminal": every loop of every criminal
program re-posts the criminal's own tile as its mission record's primary coordinates -
`1171 rec[7]`, `1172 rec[6]`, `1173 rec[18]`, `1174 rec[3]`, `1175 rec[0]`, `1078 rec[5]`,
`1302 rec[6]`. It is the same trick `FUN_004b4660` uses to walk a train rescue's marker down the
line, except the criminal does it to itself from data.

## 3. The object-class probe (opcode 15)

`FUN_004cac70`, args `(class, range, rangeScope, localSlot)`. It selects the nearest matching
object into the walker's "current object" slot (walker+0x04), writes the Chebyshev tile distance
into `local[args[3]]` (2000 when nothing matched), and returns true only when that distance is
inside `range` - the object is committed to the slot only on that same branch (0x004cb0e5).

The class is a jump table at **0x004cb130**, 17 entries:

| class | source | meaning |
|-------|--------|---------|
| 0  | `FUN_004a88e0(person+0x10a)` | my mission record's coordinates |
| 1  | walker+0x04 | whatever is already selected |
| 2  | `DAT_005040d0+0xa4` | the player's helicopter |
| 3  | `DAT_005040d0+0xbc` | second player-global object (not identified) |
| 4  | `person+0x1a4` | whatever last interacted with me |
| 5  | `FUN_004ca350(loop=-2, state=6)` | a medevac victim |
| 6  | `FUN_004ca350(loop=0, state=-2)` | **an uncaught criminal or rioter** |
| 7  | - | falls through, no object |
| 8  | `FUN_004ca350(loop=1, state=-2)` | **a police officer** (states 7 and 8) |
| 9  | `DAT_00506444+0x1b4` | the fixed player person |
| 10 | `FUN_0049b060(0, tile)` | nearest fire truck |
| 11 | `FUN_0049b060(1, tile)` | nearest police car |
| 12 | `FUN_0049b060(2, tile)` | nearest ambulance |
| 13 | `FUN_0049b060(3, tile)` | nearest service-3 vehicle |
| 14 | `FUN_004ca350(loop=-2, state=0)` | an ambient civilian (the mugger's victim) |
| 15 | inline scan | tile-grid scan at 0x004caee5 (not identified) |
| 16 | `DAT_005040d0+0xc0` | third player-global object (not identified) |

`FUN_004ca350(this, loopFilter, stateFilter, requireVisible, outDist)` is the person search:
skips itself, requires person+0x142 (alive), matches person+0x14a against `loopFilter` and
person+0x148 against `stateFilter` (-2 = any), requires person+0x152 when `requireVisible`, and
**when `loopFilter == 0` also requires person+0x16e == 0**. Nearest by Manhattan distance over the
16.16 world position.

person+0x16e is attribute 23. `1060 Rx: criminal-caught` sets it to 1 (`rec[0]`) and the criminal
roots clear it (`1300 rec[7]`, `1301 rec[0]`, `1302 rec[5]`). Loop flag 0 is exactly states
3/10/11/12/13 (`FUN_004c7090`), i.e. the riot and the four criminal programs. So "class 6" reads
precisely as *a criminal who has not been arrested yet*.

## 4. The arrest

- `1150 copf - chase criminal`: `[0]` probe class 6 within 5 tiles -> `[1]` face it (op 18) ->
  `[2]` Run-10 -> `[3]` local0 := 2 -> `[4]` walk to it (op 38, 2 tries) -> `[5]` probe class 6
  within 2 -> `[6]` **op 39 with argument 1060** -> the criminal is arrested.
- `1053 cop - get out and chase` does the same at `rec[17]`.
- `FUN_004cc560` (opcode 39) resolves the selected object to a person and pushes the BHAV named by
  `args[0]` onto *that person's* walk stack, popping a frame first if it is nearly full - the same
  mechanism `FUN_004c1050` uses for tool reactions.
- `1060 Rx: criminal-caught`: sets attribute 23, binds `Whoa`, **posts outcome 9 =
  `EVT_CriminalCaught`**, plays sound 25, idles 80 ticks, then walks to the nearest cop car
  (class 11) and deactivates.
- `1175 crim - try to mug` uses the same opcode to kill its victim: `rec[4]` op 39 with 903
  (`Rxn: Die`).

`FUN_004ca940` (opcode 38, reached through `FUN_004cc540`): decrements `local[args[0]]`, faces the
target's octant (`facing = bearing - 2 & 7`, same as opcode 18 / `FUN_004cb270`), takes one move
step at person+0x164, and reports success once the move core returns 10 with the target within 5
units vertically. Opcode 12 is the same handler with the extra "attach to the target" branch.

## 5. The helicopter can catch one on its own

`1173 crim - run from copter` has a second `EVT_CriminalCaught` at `rec[19]`, reached only through
`rec[10]`, which is `FUN_004caaf0` case 1: `|helicopter altitude - ground height| <= 4` original
units, i.e. the player hovering just off the deck. Chain:
`[2]` heli within 3 tiles -> `[4]` rand(10) > 4 -> `[6]` Random Turn -> `[5]` -> `[10]` heli low?
-> `[11]` -> `[12]` idle -> `[19]` **caught** -> sound 12 -> despawn. So setting down on top of a
criminal arrests them without any police at all, roughly half the time they roll for it.

## 6. Port status

Ported in this pass: opcodes 13, 15 (classes 2/5/6/8/14), 18, 38, 39.

Not ported, and why:
- classes 10-13 (dispatch vehicles) - the remake's emergency vehicles are not actors the behaviour
  world can hand back, so `1172 crim - run from cop car` and `1152 copf - return to cop car` take
  their false edges.
- classes 3, 9, 15, 16 - the globals behind them are not identified, so `1151 copf - follow heli`
  does nothing.
- class 4 - the remake keeps no handle to the object that caused the last interaction.
- `FUN_004caaf0` case 1 (the hover-to-catch of section 5) - opcode 14 is still unported, so the
  helicopter-only arrest does not fire.
