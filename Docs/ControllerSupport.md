# Controller Support

The game uses Xbox-style button names below. Controller input is context-sensitive so the same
buttons can cover flight, camera adjustment, dispatch, tools, and passengers without removing
the existing keyboard and mouse controls.

## Helicopter

| Control | Action |
| --- | --- |
| Left stick | Analog equivalent of `WASD`: forward/back pitch and coordinated left/right turn |
| Right stick | Pan/orbit the camera like holding the right mouse button |
| `RB` / `RT` | Climb / descend; on the ground these also hold engine start / shutdown |
| `Y` | Exit the helicopter when it is safely landed with the engine stopped |
| View / Back | Cycle camera view |
| Menu / Start | Pause or resume |
| `L3` | Toggle the spotlight |
| `R3` + left-stick X | Dedicated lateral slide, equivalent to `Q` / `E` |

### Camera adjustment

Hold `R3` to enter camera-adjust mode:

| Control while `R3` is held | Action |
| --- | --- |
| Right-stick Y | Zoom in/out |
| `RB` / `RT` | Move the helicopter up/down in the framing of exterior camera views |
| D-pad | Aim the spotlight on both axes |

Flight collective is suppressed while `R3` is held so camera framing cannot accidentally change
altitude. Left-stick forward/back remains analog flight input; left/right becomes the lateral
slide described above.

## Dispatch radial

Hold `LB` to open the dispatch radial and select Fire Truck, Police, or Ambulance with the right
stick.

| Control while `LB` is held | Action |
| --- | --- |
| `A` | Dispatch the selected service to the spotlight target |
| `X` | Dispatch the selected service in spotlight-chase mode |
| `B` | Clear the selected service at the spotlight target |
| Release `LB` | Close the radial |

The spotlight still determines the dispatch tile, matching the existing dispatch implementation.

## Tool radial and use

Hold `LT` to open the tool radial, navigate with the right stick, and release `LT` to equip the
highlighted installed tool. Press `B` before releasing `LT` to cancel without changing tools.

| Control | Action |
| --- | --- |
| `A` | Activate the equipped tool; held tools remain active until `A` is released |
| D-pad up/down with bucket selected | Raise/lower the bucket |
| D-pad up/down with rescue harness selected | Raise/lower the harness |
| D-pad up/down with megaphone selected | Select the previous/next message |
| D-pad left/right | Aim the spotlight left/right |
| D-pad up/down with another tool selected | Aim the spotlight up/down |
| `R3` + D-pad | Aim the spotlight on both axes regardless of the selected tool |

The radial only contains tools installed on and available to the current helicopter.

## Passengers

Press `X` to enter passenger-selection mode. The selected seat is highlighted on the dashboard.

| Control in passenger-selection mode | Action |
| --- | --- |
| D-pad left/right | Select a passenger |
| `A` | Open the Drop / Cancel confirmation |
| D-pad or left/right | Select Drop or Cancel |
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

