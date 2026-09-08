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
| `L3` | Show / hide contextual button help |
| `R3` + left-stick X | Dedicated lateral slide, equivalent to `Q` / `E` |

### Camera adjustment

Hold `R3` to enter camera-adjust mode:

| Control while `R3` is held | Action |
| --- | --- |
| Right-stick Y | Zoom in/out |
| `RT` / `LT` (or `A` / `B`) | Move the helicopter up/down in the framing of exterior camera views |
| `X` (Square on PlayStation) | Toggle the spotlight |

Flight collective is suppressed while `R3` is held so camera framing cannot accidentally change
altitude. Left-stick forward/back remains analog flight input; left/right becomes the lateral
slide described above.

## Dispatch radial

Hold `RB` to open the dispatch radial and select Fire Truck, Police, Ambulance, or Police (Chase) with the right
stick.

| Control while `RB` is held | Action |
| --- | --- |
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

## Menus

The front end, city and save pickers, pause/settings pages, confirmation dialogs,
hangar pages and airport check-up use spatial controller navigation:

- Left stick or D-pad moves the gold outline toward the control in that direction,
  using its on-screen position. Disabled controls are skipped; focus stops at the edge.
- `A` activates the outlined button, hotspot or checkbox.
- On a slider, list, dropdown or text field, `A` enters the control. The outline turns
  green while adjusting. Use the stick or D-pad to adjust sliders or select list entries,
  then `A` to confirm. Text entry itself uses the platform's text input.
- `B` first leaves an active control, then cancels or goes back one page. On the root
  main menu it stays on the menu; choose Quit and press `A` to exit.
- Hangar catalog tabs and equipment hotspots are included. Settings scroll to reveal
  the selected control. The original main-menu lamps and city preview selection follow
  controller navigation.

The controller hint appears with the selection outline. Keyboard and mouse retain
their existing controls. The check-up panel owns UI input while open and returns it
to flight when closed.

## Wheel layout and input contexts

The wheels use a compact segmented ring with a clear selected rim and a central title.
They scale down to fit small viewports. Slot zero is above the hub and slots proceed
clockwise. Painting and selection share `GetRadialSlotDirection`; Unreal's viewport converts physical right-stick Y
to downward Y before the pawn receives it, so selection uses that value directly. The former constraint-canvas labels
were offset twice because slots default to centre alignment, so the visible labels
were displaced from their selection directions.

Police (Chase) is a separate fourth segment, using the police service in spotlight-chase mode.
Both wheels commit on shoulder release only with a highlighted segment. The wheel fades out over
0.25 seconds; the activated segment turns gold and fades over 0.75 seconds. Actions take effect
immediately, and the fading wheel never captures input. Centring the stick
clears the highlight, so release from centre cancels. Y recalls all and closes the wheel so release cannot send a second
request. A is ignored while either wheel is open. B always cancels a wheel. A and B retain select/confirm and back/cancel in
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

## Tool cards and contextual button help

The entire card containing the active tool has a broad, soft gold halo. The shared
water card glows for either the bucket or cannon, and the Apache card glows for either
weapon. Individual buttons do not receive selection borders.

Controller help sits at the bottom left immediately above the map, with 16 HUD-scaled
pixels of padding below it. It starts collapsed. Clicking the left stick toggles the
contextual rows; the stick-click icon and Show help / Hide help row always remain.
The setting lasts across helicopter and on-foot possession for the current session.

Expanded help uses imported Kenney Input Prompts icons, and only includes actions for
the current tool, wheel, passenger selection, camera adjustment or on-foot context.
Exit helicopter appears only while landed and safe to exit.
The megaphone's broadcast row includes the current message; the D-pad icon explains
how to change it without broadcasting. X / Square broadcasts. No top-of-screen tool
help panel is shown. Wheel and passenger control hints live in this list too.

L3 is no longer spotlight toggle. Hold R3 and press X / Square to toggle the spotlight.
D-pad spotlight aiming has been removed in both flight and camera-adjust mode.
Keyboard spotlight aiming remains unchanged.

The active input event's Unreal device descriptor selects Xbox or PlayStation glyphs,
including DualShock, DualSense and Sony identifiers. Switching controllers switches
icons. Unknown and XInput descriptors use Xbox glyphs; a remapping driver that exposes
a PlayStation pad only as a virtual Xbox pad hides the physical brand from Unreal.

Icons: [Kenney Input Prompts](https://kenney.nl/assets/input-prompts), CC0. The selected
PNG files and license are in Content/Slate/ControllerIcons and staged loose for Slate
in packaged builds. No engine plugin or external runtime dependency was added.
