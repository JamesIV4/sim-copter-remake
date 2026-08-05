---
name: simcopter-dlss-resolution-scale
description: The deprecated DLSS SetDLSSMode() call was stomping r.ScreenPercentage (fixed), and a manual Resolution Scale while DLSS is on crashes NGX_D3D12_EVALUATE_DLSS_EXT (fixed by greying the row + the new Anti-Aliasing Method dropdown). 2026-08-05.
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

Added in the same pass: an **Anti-Aliasing Method** dropdown (`ESimCopterAntiAliasingMethod`,
mirroring engine `EAntiAliasingMethod` value-for-value so `r.AntiAliasingMethod` needs no
remapping) — None / FXAA / TAA / TSR / SMAA. MSAA is deliberately left out: the project runs
`r.ForwardShading=False`, and the engine silently forces MSAA back to None outside forward
shading, so it would be exactly the kind of dead control this page's own class comment says to
avoid. This row is greyed out under the same `IsDlssEnabled()` condition and for the same reason —
DLSS hooks the TAA/TSR upsample pass itself via `r.TemporalAA.Upscaler` (set inside `EnableDLSS`),
so `ApplyGraphics` leaves `r.AntiAliasingMethod` alone entirely while `bDlssEnabled` is true rather
than writing a method DLSS did not ask for.
