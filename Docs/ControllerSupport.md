# Controller Support

The game uses Xbox-style button names below. Controller input is context-sensitive so the same
buttons can cover flight, camera adjustment, dispatch, tools, and passengers without removing
the existing keyboard and mouse controls.

## Helicopter

| Control | Action |
| --- | --- |
| Left stick | Analog equivalent of `WASD`: forward/back pitch and coordinated left/right turn |
| Right stick | Pan/orbit the camera like holding the right mouse button |
| `RT` or `A` / `LT` or `B` | Climb / descend; on the ground these also hold engine start / shutdown |
| `Y` | Exit the helicopter when it is safely landed |
| View / Back | Cycle camera view |
| Menu / Start | Pause or resume |
| `L3` | Toggle the spotlight |
| `R3` + left-stick X | Dedicated lateral slide, equivalent to `Q` / `E` |

### Camera adjustment

Hold `R3` to enter camera-adjust mode:

| Control while `R3` is held | Action |
| --- | --- |
| Right-stick Y | Zoom in/out |
| `RT` / `LT` (or `A` / `B`) | Move the helicopter up/down in the framing of exterior camera views |
| D-pad | Aim the spotlight on both axes |

Flight collective is suppressed while `R3` is held so camera framing cannot accidentally change
altitude. Left-stick forward/back remains analog flight input; left/right becomes the lateral
slide described above.

## Dispatch radial

Hold `RB` to open the dispatch radial and select Fire Truck, Police, Ambulance, or Police (Chase) with the right
stick.

| Control while `RB` is held | Action |
| --- | --- |
| `A` | Dispatch the selected service to the spotlight target |
| `B` | Cancel and close without dispatching |
| `Y` | Recall every active dispatch and chase vehicle, then close |
| Release `RB` | Dispatch only if a segment is highlighted; otherwise just close |

The spotlight determines the dispatch and chase tile. Recall All is immediate and does not require
a valid spotlight target; the original Shift+F2-F5 bindings retain their selected-service,
spotlight-local release behavior.

## Tool radial and use

Hold `LB` to open the tool radial, navigate with the right stick, and release `LB` to equip the
highlighted installed tool. Press `B` before releasing `LB` to cancel without changing tools.

| Control | Action |
| --- | --- |
| `X` | Activate the equipped tool; held tools remain active until `X` is released |
| `LB` + `X` | Open passenger selection |
| D-pad up/down with bucket selected | Raise/lower the bucket |
| D-pad up/down with rescue harness selected | Raise/lower the harness |
| D-pad up/down with megaphone selected | Select the previous/next message |
| D-pad left/right | Aim the spotlight left/right |
| D-pad up/down with another tool selected | Aim the spotlight up/down |
| `R3` + D-pad | Aim the spotlight on both axes regardless of the selected tool |

The radial only contains tools installed on and available to the current helicopter.

## Passengers

Hold `LB` and press `X` to enter passenger-selection mode; you can then release `LB`. The selected seat is highlighted on the dashboard.

| Control in passenger-selection mode | Action |
| --- | --- |
| D-pad left/right | Select a passenger |
| `A` | Open the Drop / Cancel confirmation |
| D-pad left/up or right/down | Select Drop or Cancel |
| `A` | Confirm the highlighted choice |
| `B` | Back out one level; press again to leave passenger mode |
| `X` | Leave passenger mode immediately |

## On foot

| Control | Action |
| --- | --- |
| Left stick | Move |
| Right stick | Look |
| `A` | Jump |
| `X` | Put down a carried mission person |
| `Y` | Enter a nearby helicopter |
| Menu / Start | Pause or resume |

Mission-person pickup remains proximity-driven, as in the existing on-foot mission flow.

## Front end and hangar

The main menu and every hangar page acquire controller focus when opened or rebuilt:

- D-pad or left stick navigates focus.
- `A` activates the focused button or hotspot.
- `B` returns from a city-selection or hangar subpage; on the main hangar page it closes the
  hangar.

## Wheel layout and input contexts

The wheels use a compact segmented ring with a clear selected rim and a central title.
They scale down to fit small viewports. Slot zero is above the hub and slots proceed
clockwise. Painting and selection share `GetRadialSlotDirection`; physical stick Y
is converted to Slate's downward Y exactly once. The former constraint-canvas labels
were offset twice because slots default to centre alignment, so the visible labels
were displaced from their selection directions.

Police (Chase) is a separate fourth segment, using the police service in spotlight-chase mode.
Both wheels commit on shoulder release only with a highlighted segment. Centring the stick
clears the highlight, so release from centre cancels. Dispatch A sends immediately and Y recalls all; each closes the wheel so release cannot send a second
request. B always cancels a wheel. A and B retain select/confirm and back/cancel in
the passenger menu and never become altitude commands when closing it. A/B flight
holds are cleared when entering a wheel and require a fresh press after leaving it.
Triggers remain available for altitude while using wheels. On foot, A remains jump.

Controller support is a remake-specific scheme; these changes route through the
existing flight, tool and dispatch implementations without changing retail simulation
rules. The original digital collective still uses a trigger threshold of 0.25;
opposed climb/descent commands cancel, and duplicate climb inputs do not add up.

The takeoff hint follows the last-used device. Keyboard/mouse activity shows every
live keyboard binding for climb and exit; controller activity shows the controller
alternatives (RT / A for takeoff, Y for exit by default). It updates while visible,
filters out the other device, and is hidden while a wheel or passenger menu is open.
Neutral stick noise, repeated held-axis reports and releases do not change device.
