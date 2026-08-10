---
name: simcopter-dlss-resolution-scale
description: Four linked Graphics-page bugs, 2026-08-05 - DLSS SetDLSSMode() stomping r.ScreenPercentage, a manual Resolution Scale crashing NGX_D3D12_EVALUATE_DLSS_EXT, GameInstanceSubsystem::Initialize() wiping DLSS/FrameGen/Reflex because it runs before PostEngineInit, and Overall Quality reading back as "Low" instead of "Custom".
metadata:
  type: project
---

# DLSS's deprecated `SetDLSSMode()` was fighting the Resolution Scale row

*Found and fixed 2026-08-05, alongside [[simcopter-settings-menu]] and [[simcopter-low-power-mode]].*

## The bug

`USimCopterSettings::ApplyGraphics()` (`Private/Game/SimCopterSettings.cpp`) called
`UDLSSLibrary::SetDLSSMode(WorldContextObject, ToDlssPluginMode(DlssQuality))` whenever Super
Resolution was enabled. That overload is **explicitly deprecated** in the DLSS plugin
(`DisplayName = "Set DLSS Mode", DeprecatedFunction, DeprecationMessage = "Use 'Enable DLSS-SR'
instead"`) and, as an undocumented side effect, does this on every call:

```cpp
EConsoleVariableFlags Priority = static_cast<EConsoleVariableFlags>(CVarScreenPercentage->GetFlags() & ECVF_SetByMask);
CVarScreenPercentage->Set(OptimalScreenPercentage, Priority);
```

It force-writes `r.ScreenPercentage` **at whatever priority the CVar currently holds** — which is
`ECVF_SetByScalability`, the exact priority `UGameUserSettings::SetResolutionScaleValueEx` /
`ApplyNonResolutionSettings` write at. `ApplyGraphics` runs from nearly every row's `SetIndex`
handler on the Graphics page (Low Power, DLSS enable/quality, Frame Gen mode/multiple, Reflex,
Lumen, Volumetric Fog) and from clicking OK itself — so with Super Resolution on, touching *any*
row silently snapped Resolution Scale back to that DLSS quality mode's own optimal percentage,
discarding whatever the player had just set on the Resolution Scale row.

**Why it looked like Resolution Scale "only worked with Low Power Graphics on":** Low Power's own
resolution write (`ApplyLowPowerScalability()`) runs *after* the DLSS block in the same function,
so its 75% always won on the way in. Outside Low Power, nothing ran afterwards to put the row's
value back, so the DLSS stomp was always the last word — and it also explains a captured
`LowPowerRestoreResolutionScale` of `75.2` rather than a clean value: the mode was toggled on
while DLSS had already pulled the scale down.

## The fix

Replaced the deprecated call with what `EnableDLSS`'s own doc recommends: *"To select a DLSS-SR
quality mode, set an appropriate upscale screen percentage with r.ScreenPercentage. Use
GetDlssModeInformation to find optimal screen percentage."* `ApplyGraphics` now calls
`UDLSSLibrary::GetDLSSModeInformation(...)` for the optimal percentage and pushes it through the
**same** `UGameUserSettings::SetResolutionScaleValueEx` + `ApplyNonResolutionSettings()` call the
Resolution Scale row and Low Power Graphics already use — one owner of `r.ScreenPercentage`
instead of two paths racing at the same CVar priority. `FrameGenMode`/`ReflexMode`'s plugin
setters (`UStreamlineLibraryDLSSG::SetDLSSGMode`, `UStreamlineLibraryReflex::SetReflexMode`) were
checked too and do **not** touch `r.ScreenPercentage` — this was a DLSS-only bug.

## Persistence was never actually broken

The user's other complaint — Super Resolution / Frame Gen / Reflex "not saving" — turned out to be
this same bug wearing a different hat: with the resolution getting stomped on every interaction,
it read as "settings don't stick." The actual `SaveConfig`/`LoadConfig` round trip for every
`UPROPERTY(Config)` on `USimCopterSettings` (DLSS enable/quality, Frame Gen mode/multiple, Reflex,
Lumen, Volumetric Fog, Low Power) is covered end-to-end by `SimCopter.Settings.GraphicsPersistence`
now, against a scratch ini (`Automation/SimCopterGraphicsSettingsPersistenceTest.ini`) rather than
the developer's real `GameUserSettings.ini` — it passed on the first correct write.

## Trap for next time

**A UE plugin's `Set(Value, CVar->GetFlags() & ECVF_SetByMask)` pattern preserves priority, not
ownership.** Two independent settings paths writing the same CVar at the same `ECVF_SetBy*` level
will fight, and whichever ran most recently wins with no error, no log, nothing — the classic
symptom is "this row works sometimes, depending on what else I touched first." Grep a plugin's
source for the CVar name before assuming a Settings-page row and a plugin toggle can coexist
independently.

## Follow-up (same day): the fix above still let you crash the renderer

Routing DLSS's percentage through `SetResolutionScaleValueEx` stopped the *silent* clobbering, but
the Resolution Scale row was still enabled while Super Resolution was on, and a manual scale that
disagrees with DLSS's own optimal ratio for the current quality mode is fatal, not just wrong:

```
Assertion failed: (((ResultEvaluate) & 0xFFF00000) != NVSDK_NGX_Result_Fail)
NGX_D3D12_EVALUATE_DLSS_EXT failed! (NVSDK_NGX_Result_FAIL_InvalidParameter),
SrcRect=[0x0->853x480], DestRect=[0x0->2560x1440], ScaleX=0.333203, ScaleY=0.333333, ...
```

Note ScaleX != ScaleY — a manually-set Resolution Scale percentage against a non-square-friendly
destination resolution produces a non-uniform source rect, and DLSS's evaluate call hard-crashes
on it rather than rejecting it gracefully. **DLSS owns the scale once it is on; the UI has to say
so, not just avoid fighting it.** Fixed by disabling (greying) the Resolution Scale row whenever
`Settings->IsDlssEnabled()`, both in `BuildSliderRow`'s `IsEnabled` and with an explanatory
`BuildNote` under it.

## The one that actually broke persistence: subsystem init runs before `PostEngineInit`

*Third round on the same complaint — "Super Resolution / Frame Generation / Reflex still don't
survive a restart" — and the first two answers were wrong. It was never the save path.*

`USimCopterSettings::Initialize()` used to "sanitize" stored values against hardware:

```cpp
if (bDlssEnabled && !IsDlssAvailable()) { bDlssEnabled = false; }
if (FrameGenMode != Off && !IsFrameGenAvailable()) { FrameGenMode = Off; }
if (ReflexMode != Off && !IsReflexModeAvailable(ReflexMode)) { ReflexMode = ...; }
```

**`USimCopterSettings` is a `UGameInstanceSubsystem`, and those initialize too early to ask.**
`UGameEngine::Init()` builds the game instance — and so every game-instance subsystem — *before*
`FEngineLoop::Init()` broadcasts `OnPostEngineInit`, which is when the NVIDIA libraries resolve
support. The project's own log says it outright:

```
1893  LogSimCopterAudio:  [Audio] Sound root: ...        <- our subsystems, in order
1894  LogSimCopterRadio:  [Radio] 5 stations ...
1896  LogDLSSBlueprint: Error: IsDLSSSupported should not be called before PostEngineInit
...
2005  LogDLSS: NVIDIA NGX DLSS supported DLSS-SR=1 DLSS-RR=1   <- 249 ms later, same GPU
```

So every launch loaded the player's settings and immediately threw the three plugin-backed ones
away; the next OK wrote the wiped values back to the ini. `LumenMode`, `HudScale`,
`TimeOfDayMode` and the new `AntiAliasingMethod` all persisted fine throughout — **the settings
that failed were exactly the ones with a plugin-availability guard**, which is the tell.

This is NOT an editor artifact — it hits packaged builds identically, and `-game` most of all.

Fixed by deleting the sanitization outright rather than deferring it. Availability is already
checked everywhere it matters: `SSimCopterGraphicsSettings` only *builds* the DLSS, frame
generation and Reflex rows when the feature is offered, and `ApplyGraphics` guards every plugin
call with the same query at a point where the answer is true. An unsupported stored value is
therefore inert rather than a lie, and it now survives a trip to another machine and back. The
Hardware-Lumen fallback stays, because `IsRayTracingEnabled()` is an engine/RHI query resolved
during PreInit and cannot produce a false negative.

**General trap: a `UGameInstanceSubsystem::Initialize()` may not ask a plugin whether a feature
is supported.** Anything gated on `OnPostEngineInit` — every NVIDIA library here — answers "no"
and logs an error nobody reads. Never let an early answer *mutate* stored state; resolve
capability at the point of use.

## And a fourth: Overall Quality read back as "Low" whenever anything touched the resolution scale

`UGameUserSettings::GetOverallScalabilityLevel()` returns **-1** (Custom) far more readily than it
looks, because `FQualityLevels::GetSingleQualityLevel()` requires all eleven groups to agree *and*:

```cpp
if (GetRenderScaleLevelFromQualityLevel(Target) == ResolutionQuality)
```

Any resolution scale that is not the preset's own value for that level makes it Custom — so Low
Power Graphics' 75%, the Resolution Scale row, and DLSS's quality-mode percentage all trigger it.
The row did `FMath::Clamp(GetOverallScalabilityLevel(), 0, 4)`, turning that -1 into **0 = "Low"**,
so opening the page announced "Low" over a perfectly good Epic mix. The row's own comment had
promised "a sixth label the others do not" for years, but `GetCount` returned 5 and the value was
clamped, so Custom was never wired up. Now six entries, -1 maps to index 5, and selecting Custom
applies nothing (it describes the ten rows below rather than commanding them).
`SimCopter.Settings.OverallQualityLabels` locks the label contract.

Worth noting honestly: routing DLSS's percentage through `SetResolutionScaleValueEx` (the fix at
the top of this note) writes `ScalabilityQuality.ResolutionQuality`, where the old deprecated call
wrote the `r.ScreenPercentage` CVar directly — so that fix is what made this fourth bug *visible*
with DLSS on. It was already reachable via Low Power Graphics and the Resolution Scale row.

Added in the same pass: an **Anti-Aliasing Method** dropdown (`ESimCopterAntiAliasingMethod`,
mirroring engine `EAntiAliasingMethod` value-for-value so `r.AntiAliasingMethod` needs no
remapping) — None / FXAA / TAA / TSR / SMAA. MSAA is deliberately left out: the project runs
`r.ForwardShading=False`, and the engine silently forces MSAA back to None outside forward
shading, so it would be exactly the kind of dead control this page's own class comment says to
avoid. This row is greyed out under the same `IsDlssEnabled()` condition and for the same reason —
DLSS hooks the TAA/TSR upsample pass itself via `r.TemporalAA.Upscaler` (set inside `EnableDLSS`),
so `ApplyGraphics` leaves `r.AntiAliasingMethod` alone entirely while `bDlssEnabled` is true rather
than writing a method DLSS did not ask for.
