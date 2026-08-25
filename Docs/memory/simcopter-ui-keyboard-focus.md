# The cockpit UI may not take the keyboard

*Fixed 2026-08-25. Remake-only — the original is a single-window Win32 app with one input queue and
no widget focus to lose. The report was "many buttons and dropdowns stop a held key from being
registered as held (usually W to fly forward)", and it was two engine behaviours stacked, both of
which are now closed.*

## The chain, in the order it bites

1. **Slate picks a new focus target on mouse-down.** `FSlateApplication::RoutePointerDownEvent` walks
   the widget path under the cursor **leaf to root** and focuses the first widget whose
   `SupportsKeyboardFocus()` says yes. If none does, focus is not touched at all.
2. **Cockpit widgets are descendants of `SViewport`.** `UGameEngine::CreateGameViewportWidget` puts
   the `SGameLayerManager` — whose content is the overlay `AddViewportWidgetContent` fills — *inside*
   the viewport widget. So when nothing in the HUD accepts focus, that walk reaches `SViewport`,
   which already has it, and `SetUserFocus` returns early. **Nothing moves and the flight keys carry
   on.** One focusable widget in that path is all it takes to break it.
3. **Gameplay bindings only fire while the game viewport holds focus.** Key events bubble along the
   *focus path*; a focused HUD widget's path does not contain `SViewport`, so `UPlayerInput` never
   sees the key. Every axis and action goes dead at once, silently.
4. **And the keys being held are released on the way out.**
   `UGameViewportClient::LostFocus` calls `FlushPressedKeys()` on every local controller whenever the
   viewport loses focus — which is why the symptom is specifically "the key I was HOLDING stopped",
   not "the game ignored my next press".

`APlayerController` already knows steps 3-4 are wrong for in-game UI: `SetInputMode(GameAndUI)`
clears `bShouldFlushInputWhenViewportFocusChanges` for exactly this reason. But
`UInputSettings::bShouldFlushPressedKeysOnViewportFocusLost` is an **OR** over that decision, and it
shipped `True`, so the cockpit could not opt out.

## What is in place now

**Prevention — no widget over the running city accepts keyboard focus.**
`SButton` needs an explicit `.IsFocusable(false)`; `SCompoundWidget`/`SLeafWidget` already refuse.
Two offenders were left: `SSimCopterToolFlaps` answered `SupportsKeyboardFocus() == true` for the
whole flap column (its `Ctrl+Alt+M` handlers were the reason, and they were already dead code — the
chord is handled application-wide by `FSimCopterFlapCalibrationInputPreProcessor`, which needs no
focus), and `SSimCopterCheckupMenu`'s buttons were plain focusable `SButton`s over a running city.
The replay panel and the helicopter debug panel already carried the rule and say why in comments.

**Config — a focus change no longer drops held keys.** `DefaultInput.ini` now has
`bShouldFlushPressedKeysOnViewportFocusLost=False`, restoring the engine's own GameAndUI intent.
The half worth keeping is put back explicitly:
`ASimCopterPlayerController::HandleApplicationActivationChanged` flushes when the **application**
deactivates, which is the case that genuinely needs it — a background window delivers no key-up, so
a key held across an Alt-Tab would otherwise stay down forever. UIOnly screens (Settings, the
hangar) still flush, because that mode leaves the per-controller flag set.
`SimCopter.Input.HeldKeysSurviveUiFocus` asserts the resolved value, not the file — a saved
`Input.ini` in the user directory can override `DefaultInput.ini`.

**Enforcement — a Slate input pre-processor puts the keyboard back.**
`ASimCopterPlayerController` registers one for its whole life; `RestoreGameViewportKeyboardFocusIfStolen`
is the repair and `SimCopterKeyboardFocus::ShouldRestoreGameViewportFocus` is the rule, pure and
covered by `SimCopter.Input.KeyboardFocusGuard`. **A pre-processor is the only place that sees input
before focus decides where it goes** — `ProcessKeyDownEvent`/`ProcessKeyUpEvent` run the
pre-processors and only then read the focus path — so a repair there means the key *being pressed at
that moment* still reaches the pawn, not merely the next one. It consumes nothing, ever.

## The four things the guard must not do

- **It must not fight the editor.** Clicking a Details panel during PIE is how a developer takes the
  keyboard off a running game. So focus is only reclaimed when it is inside the game viewport's own
  subtree, when it is on the game's own top-level window, or (outside the editor only) when nobody
  holds it.
- **A dropdown does not leave focus in the viewport.** Slate hosts an in-window menu in the
  **window's** popup layer, not the viewport's (`FMenuStack::SetHostPath` takes the first ancestor
  answering `OnVisualizePopup`, and `SViewport` does not; the game viewport asks for
  `EPopupMethod::UseCurrentWindow`). So dismissing the megaphone menu drops focus on the *window*,
  past the viewport entirely — which is why "is it a descendant of the viewport" alone was not
  enough, and why the megaphone dropdown was one of the reported offenders.
- **An open menu keeps the keyboard while it is up** (`AnyMenusVisible`), or pulling focus out from
  under it dismisses it. The remake's own menus are pushed with `bFocusMenu=false` and never take it,
  so keys keep working with the megaphone list open.
- **Text entry keeps it too.** The replay panel's clip-name box is the only widget in a running city
  that legitimately types, and it gives the keyboard back itself ([[simcopter-replay-clips]]).
  `IsTextEntryActive` is the exception.

A UIOnly screen is not an exception at all — it makes the viewport client ignore input, and
`!GEngine->GameViewport->IgnoreInput()` is the single flag that covers Settings, the hangar shell and
the front end without the controller knowing about any of them.

## When somebody reports "the controls died"

`SimInputFocus` prints the whole decision: which widget holds the keyboard, whether the viewport
should own it, whether it is reclaimable, and what the rule would do. The guard also logs the
offending widget's type once per theft under `LogSimCopterInputFocus`, naming the fix
(`IsFocusable(false)`). **Ask that first** — it is the answer most of the time, which is also why
`SimReplayStatus` prints the focused widget before anything else.

Related: [[simcopter-replay-clips]] (which found the same trap twice from the other end),
[[simcopter-collective-is-the-engine]], [[simcopter-settings-menu]], [[simcopter-checkup-menu]].
