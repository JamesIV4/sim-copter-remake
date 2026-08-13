# Replay clips, the free camera and the Tab panel

**WHOLE-CLOTH ADDITION, 2026-08-13.** SimCopter has no replay, no clip files, no free camera and no
HUD toggle. Nothing in this feature cites a `FUN_004xxxxx`, and none of it uses the sim's 16.16
fixed point — a clip is presentation data, and quantising it twice only loses precision the camera
can see. Do not go looking for ground truth in the decompile for any of it.

## What it is

Tab raises a bottom-docked panel. REC starts sampling the live world, STOP ends the take and moves
straight into review, and from there the clip can be scrubbed, played at 0.1x–4x, saved under a name
and loaded back. Clips are per city.

**Tab is show/hide for the CONTROLS and never a mode change.** A take keeps recording and a review
keeps playing while the panel is down — the point of getting the panel out of the way is to *watch*:
full screen, HUD off, flying the free camera round the shot. An indicator appears top-right in both
cases (`● REC 0:12.4` in red, `▶ REPLAY 0:03.2 / 0:12.4` in amber, both saying "Tab for controls"),
owned by the player controller and hidden by Hide HUD like everything else.

Because Tab no longer ends a review, the free camera must stay live with the panel down —
`IsFreeCameraActive` asks `IsReplayModeActive()` (panel open **or** reviewing), not `bPanelOpen` —
and the free camera actor is created in `EnterReview` as well as `OpenPanel`.

Three buttons that are easy to confuse, so they are never merged:

| Button | Does | Clip |
| --- | --- | --- |
| **CLOSE** | ends the review, resumes the world, lowers the panel | **kept** |
| **REVIEW** | re-enters a review of the clip in memory | kept |
| **RESET** | discards the in-progress or in-view clip | **destroyed** |

## The one decision that shapes everything: snapshots, not re-simulation

This is a **snapshot recorder**, chosen deliberately over deterministic re-simulation.

Every 0.05 s the recorder asks each `ISimCopterReplayRecordable` in the world where it is and what
pose it holds, and stores a key only when that *changed*. Playback pauses the sim and pushes
interpolated states onto a set of puppets.

The alternative — record RNG seeds and inputs, then re-run the behaviour VM, traffic, missions and
the fire sim — was rejected because the sim is not deterministic today (UE tick order, variable
delta time, traces, physics, float drift all diverge) and because scrubbing *backwards* would mean
re-simulating from t=0 every time the operator dragged left. The consequence worth knowing:

- Scrubbing, reverse and slow motion are free and **cannot desync**.
- A clip is a **recording, not a save**. Playing it does not re-run the simulation, and nothing in
  it can be changed after the fact.

`FReplayClip` carries a version field. A deterministic mode could be added later as a second track
without breaking saved clips.

## Traps

- **`ASimCopterGroundAgent::BeginPlay` starts the behaviour VM, and there is no unwinding it.**
  Playback therefore spawns its stand-ins with `SpawnActorDeferred` and calls `BecomeReplayPuppet()`
  *before* `FinishSpawning`. Spawning normally and switching the program off afterwards leaves a
  running VM frame that walks the puppet off its mark.

- **The recorded crowd and the live crowd both exist during a review.** `SuspendLiveWorld` hides and
  freezes every live recordable and remembers its pose; `RestoreLiveWorld` puts it back *before* the
  pause is released, so the flight model does not resume integrating from wherever playback left the
  airframe. Get that order wrong and leaving a review teleports the helicopter.

- **The helicopter and the on-foot pawn are borrowed, not spawned.** A city has exactly one of each,
  and reusing them avoids a second helicopter with its own audio, rotor wash and collision. They are
  in `Puppets` but never in `SpawnedPuppets`, which is the only list `DestroyPuppets` touches.

- **The sim's own helicopter transform is upright.** Yaw is on the actor; the bank and the nose-down
  are drawn by `ModelPivot` underneath it (`UpdateVisuals`). Recording the actor rotation alone plays
  back a helicopter that flies perfectly level everywhere — hence `VisualRotationDeg`.

- **`UpdateCamera` runs from the pawn's Tick, which the pause stops.** Reviewing drives it by hand
  through `UpdateCameraForReplay`, or the view stays wherever the aircraft was when the clip opened.

- **Angles must interpolate the short way.** 350° → 10° is a 20° turn right, not a 340° spin left,
  and the error is most visible in slow motion — which is the shot a replay tool exists to take.
  `FReplayActorState::Blend` unwinds every angle, including the rotor.

- **A holding key before every change.** Without one, a car parked for ten seconds and then pulling
  away appears to have been creeping the whole time, because the two keys either side of the gap get
  interpolated across it.

- **A REVIEW MUST NOT USE `SetPause`. This was the single worst mistake in the feature and it hid
  itself for four rounds.** A paused world stops **component** ticks, and `USpringArmComponent`
  recomputes its socket in `TickComponent` — so the camera boom stays frozen at whatever transform
  it held when the pause landed. The helicopter *was* being driven along the clip correctly the
  whole time; the view simply never re-read it, which is indistinguishable from "the clip does not
  play". The free camera was dead for exactly the same reason. Pausing also raises the engine's own
  PAUSED overlay in the middle of every shot.

  A review now freezes the world with **near-zero global time dilation**
  (`FreezeWorldForReview`, 0.0001 — not 0, which `MinGlobalTimeDilation` would silently clamp).
  Gameplay is just as stopped, but the world is *running*, so component ticks, the camera manager
  and input all behave normally and no overlay appears. Three things must then run on **real**
  time, because every delta the engine hands out during a review is ~0:
  the playhead, `ASimCopterReplayFreeCamera::Tick`, and the `UpdateCameraForReplay` call
  (its interpolations are `RInterpTo`, which returns its input unchanged at delta zero).
  Boom lag is forced off during a review as well — `USpringArmComponent`'s own lag runs on the
  frozen world delta and would trail the aircraft forever.

  Recording deliberately stays on the world delta, so opening the Settings screen mid-take does not
  record frames of a stopped world.

- **The free camera's mouse pitch has to be negated.** `SimCopterMouseLookPitch` is mapped at scale
  -1 because the helicopter's boom views are a *drag* — pulling the mouse down drags the world down,
  which raises the camera. A free camera is a head, not a drag handle: mouse up must look up.

- **The crosshair is hidden in free camera**, and the pawn cannot work that out for itself — its
  `CameraMode` is still whatever it was when the operator switched away, so `ApplyCameraView` calls
  `RefreshCrosshairVisibility` on every view change.

- **`AActor::bHidden` exists.** A `bHidden` parameter on any actor method shadows it and the build
  fails on C4458. Both HUD setters take `bHide`.

- **THE WORST ONE: a controller-side binding that shares a key with gameplay must set
  `bConsumeInput = false`.** `APlayerController::BuildInputStack` says it outright — *"Controlled
  pawn gets last dibs on the input stack"* — the controller's `InputComponent` is processed **first**,
  and both action and axis bindings consume their keys by default (`PlayerInput.cpp` ~1239). The
  replay bindings deliberately reuse the flight axes, so leaving them consuming ate W/A/S/D, Space,
  Ctrl, the mouse look, the wheel, C and the right button **before the pawn ever saw them**: the
  helicopter stopped answering the controls entirely, with the panel closed and nothing on screen to
  suggest why. Exclusivity is not needed — the free camera blocks the pawn's input outright while it
  is live, and does nothing when it is not. Guarded by `SimCopter.Replay.InputDoesNotConsume`, which
  was verified to fail when the bug is reintroduced.

- **THE OTHER BIG ONE, and it bit twice: a viewport-content widget that accepts keyboard focus stops
  the player flying.** Gameplay axis bindings only fire while the **game viewport** holds keyboard
  focus — the rule `ASimCopterHelicopterPawn::RestoreGameViewportFocus` already exists for on the
  possession path. Slate hands focus to the first widget in the clicked path that accepts it; every
  button in the panel refuses (`IsFocusable(false)`), so the panel itself accepting meant **one
  click on REC killed all gameplay input**. **The panel is now mouse-only and never takes focus at
  all**: `SupportsKeyboardFocus()` returns false unconditionally, there is no `OnKeyDown`, and every
  shortcut it needs (Tab, C, H, Space, arrows, Home, M) is an ordinary controller binding instead.
  `SScrollBox` accepts focus by default too (`SetIsFocusable(false)` on both). The clip name box is
  the single exception — typing requires focus — and it hands the keyboard back on commit; the
  controller's focus enforcement leaves it alone via `IsTypingClipName`.

- **The same widget also swallows the mouse.** Viewport content covers the whole screen even when
  what it draws is a bottom-docked bar, and the tools are on the left mouse button. The panel is
  `SelfHitTestInvisible` (children still take clicks) and the REC indicator is `HitTestInvisible`.

- **Slate claims Tab for focus navigation before the game viewport sees it.** The controller's Tab
  binding stops firing the moment anything in the panel holds keyboard focus — the clip name box,
  most obviously — which makes the panel impossible to close with the key that opened it. The panel
  therefore answers Tab in its own `OnKeyDown` as well, routing out through an `OnRequestClose`
  delegate (the widget cannot lower itself; the controller owns it and the input mode).

- **The view target must move off the free camera BEFORE the actor is destroyed.** Destroying it
  first leaves the camera manager pointing at a destroyed actor and it never re-resolves — the
  symptom is "closing the panel left me stuck on a detached camera".

- **Closing the panel restores the view to the POSSESSED PAWN, not the helicopter.** `ApplyCameraView`
  always targets the aircraft, which is right while the panel is up and wrong on the way out: routed
  through it, closing the panel while on foot left the player watching their parked helicopter.
  `RestoreCameraAfterPanel` is the separate path, and it also puts back the camera mode the panel
  found rather than forcing Chase.

- **`EnableInput` has to go back to the pawn that was blocked**, held as a weak pointer — not a bool
  plus `GetPawn()`. Getting out of the helicopter while the free camera is live otherwise leaves the
  aircraft permanently deaf to input.

## Audio

A review **silences the live game** (`USimCopterAudioSubsystem::SilenceForReplayReview`) and plays
the clip's own recorded sound events instead. It has to: the world is frozen, so nothing will ever
stop the rotor loop, the sirens, the radio or the per-person walking voices — they hang at whatever
they were doing when the clip opened and drone underneath the replay. Slot stops alone are not
enough; the attached voice loops are separate polyphonic components and need stopping by hand.

`Play2D`, `Play3D` and `Stop` record `SoundStart`/`SoundStop` events carrying the slot id, the play
flags and the world position. Recording happens **after** `Play3D`'s audibility reject and its
already-playing early-out, so a clip only carries the plays that actually made a sound. **A zero
position is the marker for a 2D play** — that is what playback reads to choose between `Play2D` and
`Play3D`.

**A loop is started once and then re-aimed every tick** (`FUN_0042a1f0` calls `SetPosition`
unconditionally), so start events alone pin the rotor to wherever the aircraft was when the take
began — which is why the helicopter sounded like it was next to the listener for the whole replay.
Every recorded frame therefore also snapshots each active positional slot as a `SoundMove`. Those
never appear in the panel's event list or on the timeline: they fire every frame and would bury
both.

Playback fires them only on **forward, continuous** motion of the playhead. Scrubbing re-anchors
silently: replaying two seconds of a busy city's effects in one frame is a noise, not a replay.

## Particles: advancing them is only half of it

Two separate things have to be true for a replay to have fire, water, dust and rotor wash in it, and
doing only the first leaves the picture still:

1. **The pools must advance.** They run on `GetPresentationDeltaSeconds` (below).
2. **Something must SPAWN into them.** Particles are spawned by *gameplay* — the rotor wash by the
   helicopter's tick, fire by the mission layer, water by a tool — and a review freezes all of it.
   With nothing spawning, the particles already in flight simply finish and the replay goes still.

So the creator calls are recorded and re-issued: `FReplayEffectSpawn`, one per call, on its own clip
track. **Only the OUTERMOST call is recorded** — `SpawnEffect` makes its own tile puff,
`SpawnHardLanding` makes a puff plus five debris plus a column, and `SpawnRing` makes N particles, so
recording the inner calls too would have playback issue both the outer call and everything it
already produces, doubling every effect. A depth counter in the component enforces that.

`SpawnParticle` gets its own record rather than leaning on the inner `SpawnEffect`, because
everything that makes it what it is — life, size, gravity, colour — is applied to the slot *after*
that call returns. `SpawnHardLanding`'s debris directions are random and are deliberately re-rolled
on playback rather than recorded: five chunks of wreckage scattering differently is not something a
viewer can tell from the take.

A city has several particle components (the helicopter's water/wash, the mission layer's fire and
smoke, the ambient vehicles' debris), so each spawn carries a **channel** — owner class plus
component name, interned per clip — and playback resolves it back to the same component.

## Presentation time: what must keep running while the world is frozen

`SimCopterReplay::GetPresentationDeltaSeconds(WorldDelta)` returns real elapsed time during a
review and the world's own delta otherwise, cached per frame so two pools cannot disagree about how
much time passed. The particle pool, the tear gas and the Apache tracers all advance on it —
they are presentation, they carry the clip's fire, water, dust and rotor wash, and frozen they make
the replay a city where nothing is happening.

The **world systems** go the other way: `SuspendLiveWorld` explicitly disables the tick on the
mission, traffic and ambient-vehicle actors. The dilation already reduces their delta to nothing,
but this makes it certain that no callout, dispatch or scheduler roll lands in the middle of a
review. They carry `bIsWorldSystem` so the restore only re-enables their tick — applying a recorded
transform to a system actor would move the whole system to the origin. The mission markers and
message log are hidden for the length of a review whatever Hide HUD says: the layer is suspended, so
they are a countdown that is not counting and a callout for a job that is not happening.

## The event track — where the "decisions" come from

The clip's second half is a flat event list, and it is fed by **tapping call sites that already
exist**:

- `SIMCOPTER_PEOPLE_TRACE` — every shipped behaviour-VM decision site already calls it. The macro
  now also feeds `SimCopterReplay::RecordEvent`. The two conditions are independent: the log answers
  to `SimCopter.People.Trace` and its state filter, the replay answers only to "is a clip
  recording", because a clip should carry the whole city's decisions rather than the states someone
  happened to be debugging. The line is formatted once and shared.
- `SIMCOPTER_PEOPLE_TRACE_OP` — thousands a second, so it is behind its own switch
  (`SimCopter.Replay.RecordOpcodes`, off).
- `ASimCopterMissionSystemActor::PushMissionLogMessage` — the single funnel for everything the
  mission layer says to the player. Tapped *above* the `bShowMissionMessageLog` gate: that switch is
  about the on-screen log, not about whether the event happened.

Attribution works because `ASimCopterGroundAgent::Tick` opens a `SimCopterReplay::FScopedEventSource`
around its update, so a line printed six frames deep in the VM lands on the right track without any
of the hundreds of call sites being rewritten.

The funnel is a raw function pointer over a global bool (`GRecordingEvents`), not a subsystem call —
it is reached from the VM's innermost loop through a header that must not drag the subsystem in.
With no clip recording it costs a load and a branch.

## Numbers

- **20 Hz** (`SimCopterReplay::FrameIntervalSeconds`, 0.05 s) — the original's own simulation period.
  It has to stay above both the behaviour VM's 12.5 Hz ceiling and the remake's 15 Hz people tick, or
  a decision could fall between two recorded frames. See
  [people logic](simcopter-people-logic-next.md).
- A busy city records at roughly **25 MB a minute** after the sparse-key rule; a take stops itself at
  `SimCopter.Replay.MemoryBudgetMB` (512).
- `SimCopter.Replay.MaxPuppets` (600) caps how many stand-ins a review will spawn. The population
  caps are 160 vehicles + 280 pedestrians, so this is slack, not a limit anyone should hit.

## Files and layout

- `Public/Replay/SimCopterReplayTypes.h` — clip model, the event funnel. Reflection-free, so the
  trace header can include it.
- `Public/Replay/SimCopterReplayRecordable.h` — the `UINTERFACE`. Reflected rather than a plain C++
  interface like `ISimCopterBehaviorWorld` for one reason: the recorder sweeps the world with
  `TActorIterator<AActor>`, the module builds without RTTI, and `Cast<>` is the only way to ask an
  arbitrary actor whether it takes part.
- `Public/Replay/SimCopterReplaySubsystem.h` — recorder, player, clip IO, camera and HUD state.
- `Public/Replay/SimCopterReplayFreeCamera.h` — the detached camera.
- `Private/UI/SSimCopterReplayPanel.*`, `Private/UI/SSimCopterReplayTimeline.*`.
- Clips: `Saved/SimCopterReplays/<sanitised level id>/<name>_<crc>.screplay`. **A folder per level is
  how "you can only load clips from the level you're in" is enforced** — it is a property of where
  the files are, not a filter someone has to remember. `LoadClip` re-checks the id anyway, for a clip
  copied in by hand.

## Input

Bound on the **player controller, not the pawn**, and every binding runs while paused: reviewing
pauses the whole sim, and the panel has to work in the helicopter, on foot, or in neither.

| Key | Does |
| --- | --- |
| Tab | panel up/down — **a running take survives it** (`SimReplay` is the console equivalent) |
| H | hide HUD — **deliberately inert while the panel is down**, so the HUD cannot be hidden with no way back |
| C | cycles Chase → Orbit → Rescue → Cockpit → **Free** → Chase |
| P | play/pause — **not Space**, which is reserved for the free camera's "up" and the collective |
| ← → | one recorded frame |
| M | drop a timeline marker |

The free camera **reuses the flight axes** (`SimCopterPitch` W/S, `SimCopterRoll` A/D,
`SimCopterCollective` Space/LeftCtrl, the mouse-look axes, the wheel) rather than adding a second set
of mappings for the same keys. Two consequences:

- The pawn's input is **blocked** while free cam is live (`UpdateFreeCameraInputSuppression`), or W
  both pitches the helicopter and flies the camera — very obvious while recording, because the take
  then contains the aircraft lurching every time the operator moved the shot.
- `ASimCopterHelicopterPawn::CycleCameraMode` **early-returns while the panel is open**, so one press
  of C does not move two view lists.

Mouse look is gated on the right mouse button, matching the game's existing camera drag: the panel
needs a visible cursor to be clickable, and a cursor that also steers the camera makes it unusable.

FOV is **multiplicative per wheel notch**, not linear — over a 10°–180° sweep a linear step feels
stuck at the wide end and skippy at the narrow one.

## What is not covered

Particles, fire, smoke and the water/tear-gas pools are not recorded. The sim is paused during a
review so they freeze where they stand rather than replaying. Ambient planes, boats and trains live
as structs inside `ASimCopterAmbientVehiclesActor` rather than as actors, so they are not recordable
either; adding them means implementing the interface on a per-vehicle proxy.

## Getting logs out of it

Every transition is logged to `LogSimCopterReplay` at **`Log`, on by default** — panel open/close,
recording start/stop with the frame and event counts, review entered/left, pause pushed/popped with
the world's actual paused flag, and camera view changes. It is deliberately not behind a CVar:
these are a handful of lines a session, and "the panel did something odd" is exactly the report that
arrives without anyone having switched a trace on first.

    SimReplayStatus            one line: state, panel, pause, world paused, TRACK and EVENT counts,
                               camera view, free-camera liveness, puppet counts, pawn, view target
    SimReplayDump              per track: keys, frame span, whether a stand-in is bound, and the
                               clip's position at the playhead NEXT TO the actor's actual position
    log LogSimCopterReplay Verbose      adds view-target and input-suppression detail

**"The timeline runs but nothing moves" can only be one of four things**, and `SimReplayDump`
separates them in one command: no tracks (`tracks=0` with `clipFrames>0` — the frame counter ran
while the recorder captured nobody), tracks with no keys, tracks with `<NONE BOUND>` for a puppet,
or clip position and actual position disagreeing (the apply is not landing).

`USimCopterReplaySubsystem::DescribeState` is the single formatter both use, so a log line and a
status command can never disagree.

## Coverage

`SimCopter.Replay.*` — sampling and interpolation, blend rules, the sparse-key threshold, the
mnemonic table, the clip file round trip (including refusing a future version and a non-clip file),
the timebase, and the clip-name/file-name rules. Recording and playback themselves need a live world
with a population in it and are **not verified on screen** — AGENTS.md §7.
