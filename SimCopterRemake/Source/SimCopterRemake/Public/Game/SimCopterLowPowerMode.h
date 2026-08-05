// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Low Power Graphics - one checkbox on the Settings screen that takes the whole renderer down to
 * what an integrated GPU can hold 60 fps on.
 *
 * The remake renders a 1996 game with Unreal 5.8's *defaults*, and the defaults are the expensive
 * part, not the content: the city is a few hundred thousand triangles of flat-shaded 8-bit palette
 * art, but it is drawn through Lumen, virtual shadow maps, MegaLights, a volumetric cloud layer, a
 * volumetric fog volume and TSR. None of that is doing anything a player of SimCopter would miss,
 * and all of it is priced for a discrete GPU.
 *
 * The mode has three parts, and they are deliberately kept apart:
 *
 * 1. **Overall scalability drops to Low** (`UGameUserSettings::SetOverallScalabilityLevel(0)`),
 *    which is what covers the long tail - shadow resolutions, post process quality, texture
 *    streaming, translucency, effects. Doing it through the engine's own group means the Settings
 *    page's Quality rows keep reading back the truth, and the player can still raise one group
 *    afterwards. It costs almost nothing visually here: the atlas pages are 256x256 with no mips,
 *    so `r.Streaming.MipBias` has nothing to bias, and nothing in the project uses Cascade,
 *    Niagara, hair or subsurface, which is most of what the Low profile turns down.
 *
 * 2. **The switches below**, which are the ones scalability does NOT touch - it *tunes* Lumen, the
 *    virtual shadow map and MegaLights rather than switching them off, and it has no opinion at all
 *    about the anti-aliasing method or the cloud layer. This is where the frame actually comes from.
 *
 * 3. **The runtime responders**, which read `IsEnabled()`: the hundreds of point lights the
 *    building beacons throw (`USimCopterFlashingLightsComponent`) and the two spotlights on every
 *    car (`ASimCopterGroundAgent`). Those are content decisions, not renderer settings, so they
 *    cannot be expressed as a CVar.
 *
 * Everything here is restored on the way out: the switches capture whatever they were before the
 * first time the mode was entered, and the scalability level and screen percentage are remembered
 * in `USimCopterSettings`.
 */
namespace SimCopterLowPower
{
/** One renderer switch the mode forces, and why - the "why" is what stops the list rotting. */
struct FRenderSwitch
{
	const TCHAR* Name;

	/**
	 * Set as a string, not an int: the list mixes int, float and bool CVars, and
	 * `IConsoleVariable::Set(const TCHAR*)` is the one overload that is correct for all three.
	 */
	const TCHAR* LowPowerValue;

	/**
	 * True for switches that must not be forced while an external upscaler (DLSS super resolution)
	 * is running - it owns the anti-aliasing method itself, and overriding it stops DLSS engaging
	 * at all. A laptop with an RTX chip is better off keeping DLSS than gaining FXAA.
	 */
	bool bSkipWhileUpscaling = false;

	const TCHAR* Why;
};

/** The table, in the order it is applied. Public so a test can check every name still resolves. */
SIMCOPTERREMAKE_API TArrayView<const FRenderSwitch> GetRenderSwitches();

/**
 * Whether low power graphics is on right now.
 *
 * A free-standing global rather than a walk to `USimCopterSettings`, because the callers are
 * per-spawn and per-frame paths in the ground agents and the light components, and several of them
 * have no world context handy.
 */
SIMCOPTERREMAKE_API bool IsEnabled();

/**
 * Applies or reverts the CVar half of the mode and updates `IsEnabled()`.
 *
 * Idempotent: re-applying while already on re-asserts the switches (something else may have moved
 * one) without re-capturing the values to restore.
 */
SIMCOPTERREMAKE_API void Apply(bool bLowPower, bool bExternalUpscalerActive);

/** Fires when `IsEnabled()` changes, so live actors can drop their lights without a level reload. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnLowPowerModeChanged, bool /*bEnabled*/);
SIMCOPTERREMAKE_API FOnLowPowerModeChanged& OnChanged();

/** The overall scalability level the mode drops to: 0 is Low. */
constexpr int32 ScalabilityLevel = 0;

/**
 * The screen percentage the mode renders at, on `UGameUserSettings`' own 0..100 scale.
 *
 * 75% is 56% of the pixels - the single largest saving available on an integrated GPU - and it goes
 * through the user setting rather than `r.ScreenPercentage` so the Settings page's Resolution Scale
 * row still reads it back and the player can push it up or down from there.
 */
constexpr float ScreenPercentage = 75.0f;
}
