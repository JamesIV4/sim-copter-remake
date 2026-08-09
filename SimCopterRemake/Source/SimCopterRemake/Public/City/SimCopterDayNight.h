// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "City/SimCopterDayNightFog.h"
#include "Subsystems/WorldSubsystem.h"
#include "SimCopterDayNight.generated.h"

class ADaySequenceActor;
class UMaterialParameterCollection;

/**
 * The one place anything in the remake asks "is it night?".
 *
 * The original answered that with a single global, `DAT_004f9720`, set from the city's `career.twk`
 * Day/Night column and toggled by the 0x37 debug key in `FUN_004796c0`. **One is night** - proved
 * three ways: `FUN_0049a8b0` hands the renderer a dimmer ambient/diffuse for it (0x1999/0x3333/
 * 0x1999/0x4ccc against the day's 0x1999/0x6666/0x4ccc/0xcccc, 16.16), `FUN_0047a240` calls
 * `FUN_004a03a0(1)` + `FUN_004834f0` to SHOW the face-type-11 light cards (car headlights and the
 * LAMP35..38 street-lamp glows) for it, and `FUN_004606d0` loads `skydark.bmp` instead of `sky.bmp`.
 * That last one also closes the "which value is night" follow-up in Docs/MissionsAndTweakSystem.md.
 *
 * The remake has a continuously moving sun instead of a boolean, so the same answer comes out as an
 * alpha across the sunset and sunrise fades; callers that genuinely need the boolean take
 * `IsNight()`.
 */
namespace SimCopterDayNight
{
// Where CreateSimCopterMaterials.py writes the collection the city atlas material samples. Driving
// one collection scalar means nothing has to touch ~40 MI_CityPage_* instances per frame.
SIMCOPTERREMAKE_API extern const TCHAR* const ParameterCollectionPath;

// The scalars the city atlas material reads out of that collection.
SIMCOPTERREMAKE_API extern const TCHAR* const NightBlendParameterName;
SIMCOPTERREMAKE_API extern const TCHAR* const WindowSeedParameterName;
SIMCOPTERREMAKE_API extern const TCHAR* const WindowLitFractionParameterName;
SIMCOPTERREMAKE_API extern const TCHAR* const WindowRowLitFractionParameterName;
SIMCOPTERREMAKE_API extern const TCHAR* const WindowGlowNitsParameterName;

/**
 * 1 while Low Power Graphics is on, and the one number the city's materials read to take their
 * cheap paths - the terrain drops four octaves of value-noise normals, the water stops displacing
 * and shades flat, and the night window mask skips its two per-pixel hashes.
 *
 * It rides in this collection rather than being pushed at the materials because that is the same
 * problem NightBlend already solved: MI_CityPage_* are MaterialInstanceConstants and cannot be
 * animated, so a collection scalar is the only way to move ~40 of them at once.
 */
SIMCOPTERREMAKE_API extern const TCHAR* const LowPowerParameterName;

// Fraction of windows lit at night, and the chance a whole floor comes on at once. A city where
// every window is lit reads as a render, not a city; these are what make it look occupied.
constexpr float DefaultWindowLitFraction = 0.30f;
constexpr float DefaultWindowRowLitFraction = 0.05f;

// How bright a lit window burns, in nits. Absolute rather than derived from the sun, because a
// window has a bulb behind it - but small, because it is being metered against a night exposure:
// the first pass at 2500 was four orders of magnitude over the moonlit ground and bloomed the
// skyline into a wall of halos. Tunable live, see SimCopter.NightWindows.Nits.
constexpr float DefaultWindowGlowNits = 25.0f;

// --- surface shading ceilings -----------------------------------------------------------------
//
// Three material families that the day sequence's 120,000-lux sun overdrives, each with its own
// albedo ceiling, roughness and specular. All nine are published live from the SimCopter.Shading.*
// console variables, so they can be dialled in while looking at the city instead of by editing a
// material and re-baking.
//
// **The ceiling is a hue-preserving ALBEDO clamp**, not a clamp on the lit result - a material
// cannot see its own lighting, so albedo is the only lever that bounds how bright a diffuse
// surface can get. The texel is scaled down until its brightest channel reaches the ceiling, which
// stops a bright palette entry clipping to white without walking it towards grey the way a
// per-channel min() would.

// Foliage. Real leaves sit near 0.15-0.25 albedo and are almost non-specular. The sprite cards make
// it worse than an ordinary surface: their normal is biased to world up (CardNormalUpBias), so at
// noon EVERY card faces the sun at once and one broad specular lobe washes the whole canopy white.
constexpr float DefaultTreeMaxBrightness = 0.42f;
constexpr float DefaultTreeRoughness = 0.92f;
constexpr float DefaultTreeSpecular = 0.02f;

// Ground. Earth and grass are matte; the shared 0.3 specular and 0.65 roughness the city materials
// were built with read as damp stone across a whole map of it.
constexpr float DefaultTerrainMaxBrightness = 0.55f;
constexpr float DefaultTerrainRoughness = 0.88f;
constexpr float DefaultTerrainSpecular = 0.04f;

// Buildings and roads: the atlas pages, the direct-image faces and every flat palette-coloured
// face. Started at the terrain's exact numbers because the ground was tuned first and looked
// right, and the city standing out beside it was the whole complaint - but it is its own family so
// painted concrete and asphalt can be pulled away from dirt later without moving the ground.
//
// The vertex-colour material is shared with the VEHICLES
// (Docs/memory/simcopter-vehicle-material.md), so cars ride this ceiling too. Roughness and
// specular were already shared; only the ceiling is new to them.
constexpr float DefaultCityMaxBrightness = 0.55f;
constexpr float DefaultCityRoughness = 0.88f;
constexpr float DefaultCitySpecular = 0.04f;

// Glass, where the hand-painted window mask says there is a pane
// (Content/NightWindows/windows_page_<page>.png - pages 2, 39 and 40, the three wall pages).
//
// Those masks were painted to decide which texels LIGHT UP at night, but which texels are windows
// is a fact about the building and not about the hour, so the same data makes them reflective at
// noon. Glass was the one surface in the city with no way to tell itself apart from the masonry it
// is set into.
//
// Note this is a DIFFERENT question from the glow mask's: that one asks "is this window lit right
// now" and so early-outs in daylight and applies the ~30% occupancy roll. Every window is glass
// whether or not anyone is home, so the reflection mask has neither gate. Only the atlas material
// carries the mask, so only it uses these - the rest of the city stays on the matte City pair.
constexpr float DefaultCityWindowRoughness = 0.10f;
constexpr float DefaultCityWindowSpecular = 0.85f;

// Water is the one surface here that SHOULD be glossy, and it was sharing the same matte numbers as
// the dirt. Low roughness and a near-dielectric-maximum specular give it back its sun glint and its
// sky reflection.
constexpr float DefaultWaterMaxBrightness = 0.75f;
constexpr float DefaultWaterRoughness = 0.12f;
constexpr float DefaultWaterSpecular = 0.9f;

// --- the shoreline, and the grid it used to show ------------------------------------------------
//
// Making water glossy exposed how it ENDS. A near-mirror met the land on a tile-quantised, stair-
// stepped coastline, so the reflection stopped dead on a hard line and every 400 cm quad seam near
// it read as part of a grid.
//
// The mask for fixing that already existed: vertex-colour R on the water mesh is the wave weight,
// 0 at the welded shoreline and 1 offshore. Roughness and specular now ease from a matte shoreline
// pair out to the open-water pair across WaterShoreFadeWidth of that ramp, which turns the hard
// edge into a beach without any new vertex data or a re-bake.
constexpr float DefaultWaterShoreRoughness = 0.72f;
constexpr float DefaultWaterShoreSpecular = 0.12f;
// Fraction of the weight ramp the fade spans. Smootherstepped, so it has no visible start or end of
// its own - a linear ramp would just move the hard edge inland.
constexpr float DefaultWaterShoreFadeWidth = 0.4f;

// ...and the fade's SHAPE, which softness alone cannot fix. The weight is interpolated bilinearly
// across 400 cm quads, so any contour thresholded out of it traces the grid: straight along tile
// edges, 45 degrees across quad diagonals, kinked where quads meet. Displacing the weight in world
// space before the ramp displaces that contour laterally - by roughly strength / |grad(weight)| -
// which turns the stair-step into a line that wanders over the tile boundaries instead of along
// them.
//
// Strength is in weight units, so how far the contour actually moves depends on how fast the weight
// ramps on a given coast; a shallow shore wanders more than a steep one. The scale is a wavelength
// in centimetres.
//
// THESE ARE TUNED ON SCREEN AND THEY ARE NOT WHAT THE THEORY PREDICTED. The reasoning above says
// the wavelength has to exceed the 400 cm tile so the contour WANDERS further than the stair-step
// it is hiding, and the first defaults (900 / 300 cm) were picked that way. What actually looks
// right is 25 cm and 4 cm at roughly ten times the strength - far below a tile, which dissolves the
// boundary into a stochastic band instead of moving it. Both hide the grid; the fine one hides it
// better, because a wandering line is still a LINE and the eye finds it. Do not "correct" these
// back up to the tile scale on the strength of the argument - it was tested and lost.
constexpr float DefaultWaterShoreEdgeNoiseStrength = 0.25f;
constexpr float DefaultWaterShoreEdgeNoiseScale = 25.0f;

// A second, INDEPENDENT layer - its own amplitude and its own wavelength, not an octave chained off
// the first, so the two frequencies can be dialled against each other. They are sampled at
// different world offsets so they cannot line up and reinforce into one wave.
//
// Also tuned on screen, and also very fine: 4 cm at nearly layer 1's strength. Between them the two
// read as a dissolve rather than as a shape, which is the look that won.
constexpr float DefaultWaterShoreEdgeNoiseStrength2 = 0.2f;
constexpr float DefaultWaterShoreEdgeNoiseScale2 = 4.0f;

// How fast each layer's field changes, in NOISE FRAMES PER SECOND. The animation is a third axis on
// the hash, not a scroll: the field changes where it is instead of travelling across the water,
// because sliding the sample position reads as a conveyor belt at these wavelengths. 0 freezes a
// layer.
//
// **Interpolated with a Catmull-Rom through four frames, and NOT with a smoothstep between two.**
// The smoothstep version read as visibly framey, and the reason is what it is for: ease-in-ease-out
// gives the field zero velocity at every keyframe, so it arrives, holds, and rushes to the next -
// still, fast, still, fast, once per frame. Going higher-order (smootherstep) flattens the ends
// further and makes the hold longer, so the obvious next step makes it worse. A cubic through four
// control points takes its slope at each frame from that frame's neighbours and therefore passes
// through without stopping, which is the ordinary keyframe-interpolation answer to the ordinary
// keyframe-interpolation problem. It costs four spatial taps per layer instead of two.
//
// Layer 2 is the fine one (4 cm), so its rate is where shimmer would come from if it is pushed:
// a pattern that fine is already near sub-pixel at altitude, and animating sub-pixel detail
// sparkles. If the water fizzes when seen from high up, this is the number to bring down.
constexpr float DefaultWaterShoreEdgeNoiseSpeed = 0.8f;
constexpr float DefaultWaterShoreEdgeNoiseSpeed2 = 3.0f;

// A fine per-pixel ripple on the NORMAL only - it never touches the wave WPO, so the geometry and
// the welded shoreline are untouched. Its job is to stop open water being one flat mirror: at
// roughness 0.12 any large flat span reflects the sky as a single sheet, and every seam in the
// coarse per-vertex data then reads as a hard edge in that sheet. Strength 0 turns it off.
constexpr float DefaultWaterDetailNormalStrength = 0.35f;
// Wavelength in centimetres.
constexpr float DefaultWaterDetailNormalScale = 140.0f;

// Default fade anchors, matching USimCopterDayNightFogComponent's so the fog, the window lights and
// the pacing all turn over on the same hours.
constexpr float DefaultSunriseHour = 6.0f;
constexpr float DefaultSunsetHour = 18.0f;
constexpr float DefaultFadeDurationHours = 1.0f;

// Past this the game calls it night, so the hangar and anything else swapping art rather than
// blending it agree on when to swap.
constexpr float NightThreshold = 0.5f;
}

/**
 * Resolves the level's day sequence, publishes the night blend the city materials read, and applies
 * the player's Dynamic/Static time-of-day choice.
 *
 * A world subsystem rather than another component on the day sequence actor, because the per-actor
 * components (`USimCopterDayNightFogComponent`, `...MoonDiscComponent`, `...StarsComponent`,
 * `...DayNightLengthComponent`) are level-authoring knobs, whereas this is what the *game* asks. The
 * hangar shell and the Settings screen have no business reaching into a level actor to find out what
 * time it is.
 */
UCLASS()
class SIMCOPTERREMAKE_API USimCopterDayNightSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static USimCopterDayNightSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Time of day in hours, 0..DayLength. Zero when the level has no day sequence. */
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Day/Night")
	float GetTimeOfDayHours() const { return TimeOfDayHours; }

	/** Length of the day cycle in hours; 24 unless the level's day sequence says otherwise. */
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Day/Night")
	float GetDayLengthHours() const { return DayLengthHours; }

	/** 0 in full daylight, 1 in full night, easing across the sunset and sunrise fades. */
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Day/Night")
	float GetNightAlpha() const { return NightAlpha; }

	/** The original's boolean, for callers that swap art rather than blend it. */
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Day/Night")
	bool IsNight() const { return NightAlpha >= SimCopterDayNight::NightThreshold; }

	/** Null-safe convenience: false when there is no world, no subsystem or no day sequence. */
	static bool IsNightForWorld(const UObject* WorldContextObject);

	/** Re-reads USimCopterSettings and pushes Dynamic/Static at the day sequence. */
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Day/Night")
	void ApplyTimeOfDaySettings();

	/** Reads the sequence actor directly, so a save never captures a one-frame-old cached clock. */
	bool TryGetLiveTimeOfDayHours(float& OutHours);

	/**
	 * Restores a saved clock after the saved Dynamic/Static settings have been installed.
	 * If the level actor is not ready yet, Tick keeps the request pending until it appears.
	 */
	void RestoreSavedTimeOfDay(float Hours);

	/** The level's day sequence actor, or null. */
	ADaySequenceActor* GetDaySequenceActor() const;

	/**
	 * Picks a new set of lit windows.
	 *
	 * Called automatically when the sun goes down and when the Settings screen forces night, which is
	 * the whole point - the city should not come back with the same windows lit every single night.
	 * It is one number: the material hashes it per window, so re-rolling the whole skyline costs a
	 * single collection write and no geometry work at all.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Day/Night")
	void RerollNightWindows();

	/** Hour the sunset fade starts at; the night blend reaches 1 one fade later. */
	float SunsetHour = SimCopterDayNight::DefaultSunsetHour;

	/** Hour the sunrise fade starts at; the night blend reaches 0 one fade later. */
	float SunriseHour = SimCopterDayNight::DefaultSunriseHour;

	/** How long each fade takes, in time-of-day hours. */
	float FadeDurationHours = SimCopterDayNight::DefaultFadeDurationHours;

	// --- FTickableGameObject ---
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	/** So scrubbing Time Of Day Preview in the editor moves the window lights with the sun. */
	virtual bool IsTickableInEditor() const override { return true; }

private:
	/** Recomputes the time of day and night alpha, and republishes NightBlend if it moved. */
	void Refresh();

	/** Finds the day sequence actor if the cached one has gone away. */
	ADaySequenceActor* ResolveDaySequenceActor();

	/** Loads the parameter collection once; null when the materials have not been rebuilt. */
	UMaterialParameterCollection* ResolveParameterCollection();

	/** Writes one scalar into the collection, if it loaded. */
	void PublishScalar(const TCHAR* ParameterName, float Value);

	/** Pushes the lit fraction, row chance and glow brightness; cheap, only on change. */
	void PublishWindowTuning();

	/**
	 * Pushes the nine SimCopter.Shading.* knobs at the tree, terrain and water materials.
	 *
	 * Same change-gated shape as the window tuning, and for the same reason: a collection write
	 * dirties every material instance sampling it, so an unchanged frame must cost nothing.
	 */
	void PublishSurfaceShading();

	/**
	 * Pushes the low power flag at the city materials.
	 *
	 * Called from Tick rather than from Refresh, because Refresh gives up when the level has no day
	 * sequence and the flag still has to reach the materials there - the main menu and the editor
	 * both count.
	 */
	void PublishLowPower();

	/** Applies PendingSavedTimeOfDayHours once the level's day sequence actor is available. */
	void ApplyPendingSavedTimeOfDay();

	TWeakObjectPtr<ADaySequenceActor> CachedDaySequenceActor;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> CachedParameterCollection;

	float TimeOfDayHours = 0.0f;
	float DayLengthHours = SimCopterDayNightFog::DefaultDayLengthHours;
	float NightAlpha = 0.0f;

	/** Last values written to the collection, so an unchanged frame costs nothing. */
	float PublishedNightBlend = -1.0f;
	float PublishedLitFraction = -1.0f;
	float PublishedRowLitFraction = -1.0f;
	float PublishedGlowNits = -1.0f;
	float PublishedLowPower = -1.0f;

	/** Last value written for each SimCopter.Shading.* knob, in ESurfaceShadingSlot order. */
	static constexpr int32 SurfaceShadingSlotCount = 25;
	float PublishedSurfaceShading[SurfaceShadingSlotCount] = {
		-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };

	/**
	 * Whether the last evaluated frame counted as night, so the day->night EDGE can be detected.
	 * Re-rolling on "is night" rather than on the edge would reshuffle the windows every frame.
	 */
	bool bWasNight = false;

	/** Set once the collection has been looked for and not found, to stop the log repeating. */
	bool bWarnedMissingCollection = false;

	/** Time of the last actor scan, so a level with no day sequence does not scan every frame. */
	double LastActorScanSeconds = -1.0;

	/** The settings only need pushing when they move, not every tick. */
	uint8 AppliedTimeOfDayMode = 0xff;
	float AppliedStaticTimeOfDayHours = -1.0f;
	float AppliedDayRealMinutes = -1.0f;
	float AppliedNightRealMinutes = -1.0f;

	/** Negative means no saved clock is waiting to be restored. */
	float PendingSavedTimeOfDayHours = -1.0f;
};
