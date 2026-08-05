// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SimCopterSettings.generated.h"

class USimCopterAudioSubsystem;
class USimCopterRadioSubsystem;

/**
 * Picking the resolution the game should open at, kept free of the engine's display plumbing so it
 * can be tested without a monitor.
 *
 * `UGameUserSettings` seeds a brand new ini from `[/Script/Engine.GameUserSettings]` defaults, which
 * is 1280x720 - not the display's own size. On a first run that opens a small window on a 4K panel
 * and looks broken, so the first launch takes the native resolution of whichever monitor the game
 * actually came up on.
 */
namespace SimCopterDisplay
{
/** One monitor, in virtual-desktop coordinates. */
struct FMonitor
{
	/** The panel's real pixel size, which is NOT DisplayRect when Windows is scaling it. */
	FIntPoint NativeResolution = FIntPoint::ZeroValue;

	/** Where the monitor sits on the virtual desktop. */
	FIntRect DisplayRect = FIntRect(0, 0, 0, 0);

	bool bIsPrimary = false;
};

/**
 * Native resolution of the monitor the window is on: the one it overlaps most, falling back to the
 * primary and then to the first listed. Zero when there are no monitors or none report a size.
 *
 * Overlap area rather than window centre, because a window straddling two monitors renders on the
 * one showing most of it, and a zero-size window (which is what an unshown window reports) has no
 * centre worth trusting.
 */
SIMCOPTERREMAKE_API FIntPoint FindNativeResolutionForWindow(
	const TArray<FMonitor>& Monitors,
	const FIntRect& WindowRect);
}

/**
 * DLSS super resolution quality, mirroring NVIDIA's UDLSSMode so the settings store does not have
 * to include the plugin's header (and so the value that lands in the ini stays stable if the
 * plugin ever renumbers). Whether DLSS runs at all is a separate flag, because that is the shape
 * of the plugin's own API - EnableDLSS(bool) and SetDLSSMode(quality) - and the Settings page
 * gives each its own dropdown.
 */
UENUM()
enum class ESimCopterDlssQuality : uint8
{
	Auto = 0,
	Dlaa,
	UltraQuality,
	Quality,
	Balanced,
	Performance,
	UltraPerformance,
};

/** Whether DLSS Frame Generation runs. Auto lets the driver drop it when it would cost more than it gains. */
UENUM()
enum class ESimCopterFrameGenMode : uint8
{
	Off = 0,
	On,
	Auto,
};

/**
 * NVIDIA Reflex low latency, mirroring `EStreamlineReflexMode` so the ini value stays stable if the
 * plugin ever renumbers (its own enum is deliberately sparse - Boost is 3, not 2).
 */
UENUM()
enum class ESimCopterReflexMode : uint8
{
	Off = 0,
	On,
	OnBoost,
};

/**
 * Lumen, as the three choices a player actually cares about.
 *
 * Hardware and Software are the same GI method - `r.DynamicGlobalIlluminationMethod 1` - differing
 * only in `r.Lumen.HardwareRayTracing`, so the dropdown collapses them into one row. Off drops GI to
 * None and leaves reflections on screen-space, which is the nearest thing to "no Lumen" that still
 * shows a reflection at all.
 */
UENUM()
enum class ESimCopterLumenMode : uint8
{
	HardwareRayTracing = 0,
	Software,
	Off,
};

/**
 * Anti-aliasing method, mirroring Unreal's own `EAntiAliasingMethod` value for value so writing it
 * to `r.AntiAliasingMethod` needs no remapping. MSAA is left out on purpose: the project runs
 * deferred shading (`r.ForwardShading=False` in DefaultEngine.ini) and the engine silently forces
 * MSAA back to None outside forward shading, so listing it would be exactly the kind of dead
 * control this page otherwise avoids (see the class comment).
 */
UENUM()
enum class ESimCopterAntiAliasingMethod : uint8
{
	None = 0,
	Fxaa = 1,
	TemporalAA = 2,
	Tsr = 4,
	Smaa = 5,
};

/** How the Settings screen's Time of Day row drives the level's day sequence. */
UENUM()
enum class ESimCopterTimeOfDayMode : uint8
{
	/** The day sequence runs, so the sun, the fog and the window lights all move. */
	Dynamic = 0,
	/** The clock is pinned to Static Time Of Day Hours and the day cycle is paused. */
	Static,
};

/**
 * Everything the Settings screen owns, persisted to the user's GameUserSettings ini.
 *
 * The sound half is the original's, value for value: `FUN_0043f7c0` builds both volume sliders
 * over 320..10000 and the tuner over 0..2, and `FUN_00440130` reads them back. The graphics half
 * is deliberately NOT the original's - `render.bmp`'s options (building textures, ground textures,
 * sky, fog closeness, the three resolution modes) are all about making 1996 hardware keep up with
 * assets this project renders without noticing, so the page carries Unreal's settings instead.
 * See Docs/scratchpad/settings-DECODED.md.
 *
 * City Settings is not here: it edits the live city record through the mission system
 * (FSimCopterCareerCity), which is session state rather than a user preference, exactly as
 * `FUN_00440ec0` writes straight back into the career/user block.
 */
UCLASS(Config = GameUserSettings)
class SIMCOPTERREMAKE_API USimCopterSettings : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static USimCopterSettings* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Writes the stored values through to the audio, radio, HUD and renderer. */
	void ApplyAll(const UObject* WorldContextObject);

	/** SaveConfig plus UGameUserSettings::ApplySettings for the engine-owned half. */
	void Save();

	// --- sound (the original's, control 0x7d6) ---

	/**
	 * FUN_0043f7c0's slider range for both volumes. The original stores a DirectSound attenuation
	 * and maps it onto the slider logarithmically (FUN_00440020 / FUN_00440130); the remake's
	 * mixer already indexes volume linearly in [0, 10000], so the slider is linear over the
	 * original's own end points and no log curve is needed.
	 */
	static constexpr int32 VolumeMin = 320;
	static constexpr int32 VolumeMax = 10000;

	int32 GetGameVolume() const { return GameVolume; }
	void SetGameVolume(int32 Value);

	int32 GetRadioVolume() const { return RadioVolume; }
	void SetRadioVolume(int32 Value);

	/** The tuner slider, 0..2 in the original. Clamped to the stations actually discovered. */
	int32 GetRadioStation() const { return RadioStation; }
	void SetRadioStation(int32 Index);

	bool IsDjEnabled() const { return bDjEnabled; }
	void SetDjEnabled(bool bEnabled);

	bool AreCommercialsEnabled() const { return bCommercialsEnabled; }
	void SetCommercialsEnabled(bool bEnabled);

	/**
	 * The original's third toggle. It ducks the radio while a dispatch call is speaking; the
	 * remake applies it as a radio volume scale for the same reason.
	 */
	bool IsAutoQuietEnabled() const { return bAutoQuiet; }
	void SetAutoQuietEnabled(bool bEnabled);

	// --- graphics (the remake's, control 0x7d5) ---

	/**
	 * Low Power Graphics: drops overall scalability to Low, renders at 75% screen percentage, and
	 * switches off everything in `SimCopterLowPowerMode.h`'s table - Lumen, the virtual shadow map,
	 * MegaLights, the cloud layer, TSR. Live actors drop their local lights with it.
	 *
	 * The visual compromises are real and confined to this mode; see the header for what each one
	 * buys. Everything is restored on the way out, including the scalability level and screen
	 * percentage that were in force before it was switched on.
	 */
	bool IsLowPowerMode() const { return bLowPowerMode; }
	void SetLowPowerMode(bool bEnabled) { bLowPowerMode = bEnabled; }

	bool IsDlssEnabled() const { return bDlssEnabled; }
	void SetDlssEnabled(bool bEnabled) { bDlssEnabled = bEnabled; }

	ESimCopterDlssQuality GetDlssQuality() const { return DlssQuality; }
	void SetDlssQuality(ESimCopterDlssQuality Quality) { DlssQuality = Quality; }

	ESimCopterFrameGenMode GetFrameGenMode() const { return FrameGenMode; }
	void SetFrameGenMode(ESimCopterFrameGenMode Mode) { FrameGenMode = Mode; }

	/**
	 * How many presented frames per rendered frame, 2..6 - i.e. multi-frame generation's
	 * "multiples". Streamline folds this into its mode enum (On2X..On6X); the Settings page keeps
	 * it as its own dropdown, so the two are recombined on the way out.
	 */
	static constexpr int32 FrameGenMultipleMin = 2;
	static constexpr int32 FrameGenMultipleMax = 6;

	int32 GetFrameGenMultiple() const { return FrameGenMultiple; }
	void SetFrameGenMultiple(int32 Multiple);

	ESimCopterReflexMode GetReflexMode() const { return ReflexMode; }
	void SetReflexMode(ESimCopterReflexMode Mode) { ReflexMode = Mode; }

	ESimCopterLumenMode GetLumenMode() const { return LumenMode; }
	void SetLumenMode(ESimCopterLumenMode Mode) { LumenMode = Mode; }

	/**
	 * Greyed out on the page while Super Resolution is on: DLSS hooks the TAA/TSR upsample pass
	 * itself (`r.TemporalAA.Upscaler`, set by `EnableDLSS`) and ApplyGraphics leaves
	 * `r.AntiAliasingMethod` alone whenever `bDlssEnabled` is true, the same way it defers to DLSS
	 * for Resolution Scale - picking a different method out from under it is what produced the
	 * `NGX_D3D12_EVALUATE_DLSS_EXT` crash the Resolution Scale row could already trigger.
	 */
	ESimCopterAntiAliasingMethod GetAntiAliasingMethod() const { return AntiAliasingMethod; }
	void SetAntiAliasingMethod(ESimCopterAntiAliasingMethod Method) { AntiAliasingMethod = Method; }

	bool IsVolumetricFogEnabled() const { return bVolumetricFog; }
	void SetVolumetricFogEnabled(bool bEnabled) { bVolumetricFog = bEnabled; }

	/**
	 * One knob over everything the remake draws as emissive: the fire and effect cards, the people
	 * sprites, and the night window lights.
	 *
	 * All of those are unlit cards or emissive terms whose brightness is derived from the sun rather
	 * than authored (see `USimCopterEffectExposureSubsystem`), so one multiplier moves them together
	 * and keeps their relationship to each other. 1.0 is the derived value; drop it if the bloom
	 * halos are too strong on your display.
	 */
	float GetEmissiveBrightness() const { return EmissiveBrightness; }
	void SetEmissiveBrightness(float Scale);

	static constexpr float EmissiveBrightnessMin = 0.05f;
	static constexpr float EmissiveBrightnessMax = 3.0f;

	// --- time of day (the remake's; the original's equivalent is career.twk's Day/Night column) ---

	ESimCopterTimeOfDayMode GetTimeOfDayMode() const { return TimeOfDayMode; }
	void SetTimeOfDayMode(ESimCopterTimeOfDayMode Mode) { TimeOfDayMode = Mode; }

	/** The hour Static mode pins the clock to, 0..24. Ignored in Dynamic. */
	float GetStaticTimeOfDayHours() const { return StaticTimeOfDayHours; }
	void SetStaticTimeOfDayHours(float Hours);

	static constexpr float StaticTimeOfDayMinHours = 0.0f;
	static constexpr float StaticTimeOfDayMaxHours = 24.0f;

	/**
	 * How long daylight and night each last in real minutes, in Dynamic mode.
	 *
	 * These are the player-facing face of `USimCopterDayNightLengthComponent`'s `DayRealMinutes` /
	 * `NightRealMinutes`, which the day/night subsystem pushes at the level's component. They set the
	 * two PLATEAU speeds; the transition ramps between them cost a little extra on top, so the
	 * component's Effective Day/Night Minutes readouts are what a band actually takes.
	 */
	float GetDayRealMinutes() const { return DayRealMinutes; }
	void SetDayRealMinutes(float Minutes);

	float GetNightRealMinutes() const { return NightRealMinutes; }
	void SetNightRealMinutes(float Minutes);

	/** Half a minute is already a comically fast sunrise; 30 is far longer than a session needs. */
	static constexpr float CycleLengthMinMinutes = 0.5f;
	static constexpr float CycleLengthMaxMinutes = 30.0f;

	/** "13:30" for 13.5, for the Settings row's readout. */
	static FText FormatTimeOfDay(float Hours);

	/** "7.0 min", for the two cycle length rows. */
	static FText FormatMinutes(float Minutes);

	/** Multiplies the cockpit/dashboard/map overlays. 0.5 .. 2.0. */
	float GetHudScale() const { return HudScale; }
	void SetHudScale(float Scale);

	static constexpr float HudScaleMin = 0.5f;
	static constexpr float HudScaleMax = 2.0f;

	/** Broadcast when HudScale changes so live overlays can re-scale without a level reload. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnHudScaleChanged, float);
	FOnHudScaleChanged OnHudScaleChanged;

	// --- availability, so the page can grey rows out instead of lying ---

	/** True when an RTX GPU and a current driver actually offer DLSS super resolution. */
	static bool IsDlssAvailable();

	/** True when DLSS Frame Generation is offered. */
	static bool IsFrameGenAvailable();

	/** True when this GPU and driver offer NVIDIA Reflex. */
	static bool IsReflexAvailable();

	/** True when a Reflex mode is offered - Boost is not on every card that has Reflex at all. */
	static bool IsReflexModeAvailable(ESimCopterReflexMode Mode);

	/**
	 * True when hardware ray tracing is actually available, so the Lumen row can fall back to
	 * Software instead of offering a mode that silently degrades to it.
	 */
	static bool IsHardwareRayTracingAvailable();

	/** The DLSS quality modes this GPU offers; empty when DLSS is unavailable. */
	static void GetAvailableDlssQualities(TArray<ESimCopterDlssQuality>& OutQualities);

	/**
	 * The frame multiples this GPU offers, ascending. A card with frame generation but not the
	 * multi-frame variant reports just {2}; an empty result means no generation at all.
	 */
	static void GetAvailableFrameGenMultiples(TArray<int32>& OutMultiples);

	static FText GetDlssQualityLabel(ESimCopterDlssQuality Quality);
	static FText GetFrameGenModeLabel(ESimCopterFrameGenMode Mode);
	static FText GetFrameGenMultipleLabel(int32 Multiple);
	static FText GetReflexModeLabel(ESimCopterReflexMode Mode);
	static FText GetLumenModeLabel(ESimCopterLumenMode Mode);
	static FText GetAntiAliasingMethodLabel(ESimCopterAntiAliasingMethod Method);
	static FText GetTimeOfDayModeLabel(ESimCopterTimeOfDayMode Mode);

private:
	UPROPERTY(Config)
	int32 GameVolume = VolumeMax;

	UPROPERTY(Config)
	int32 RadioVolume = VolumeMax;

	UPROPERTY(Config)
	int32 RadioStation = 0;

	UPROPERTY(Config)
	bool bDjEnabled = true;

	UPROPERTY(Config)
	bool bCommercialsEnabled = true;

	UPROPERTY(Config)
	bool bAutoQuiet = true;

	UPROPERTY(Config)
	bool bLowPowerMode = false;

	/**
	 * The overall scalability level and screen percentage in force when Low Power was switched on,
	 * so switching it off puts them back.
	 *
	 * They are also the latch that says the mode's one-shot half has been applied: INDEX_NONE and a
	 * negative scale mean "not currently applied", which is what stops a second ApplyGraphics from
	 * capturing Low as the value to restore. Persisted, because the mode itself is - a session that
	 * starts with it already on must still know what to go back to.
	 */
	UPROPERTY(Config)
	int32 LowPowerRestoreScalabilityLevel = INDEX_NONE;

	UPROPERTY(Config)
	float LowPowerRestoreResolutionScale = -1.0f;

	UPROPERTY(Config)
	bool bDlssEnabled = false;

	UPROPERTY(Config)
	ESimCopterDlssQuality DlssQuality = ESimCopterDlssQuality::Auto;

	UPROPERTY(Config)
	ESimCopterFrameGenMode FrameGenMode = ESimCopterFrameGenMode::Off;

	UPROPERTY(Config)
	int32 FrameGenMultiple = FrameGenMultipleMin;

	/** On by default: Reflex costs nothing when the frame is GPU bound and helps when it is not. */
	UPROPERTY(Config)
	ESimCopterReflexMode ReflexMode = ESimCopterReflexMode::On;

	/**
	 * Hardware by default, because the project ships with `r.Lumen.HardwareRayTracing=True` and
	 * `r.RayTracing=True` in DefaultEngine.ini. Initialize() drops it to Software on a machine whose
	 * RHI cannot do it, so the ini travelling to another PC does not leave the row lying.
	 */
	UPROPERTY(Config)
	ESimCopterLumenMode LumenMode = ESimCopterLumenMode::HardwareRayTracing;

	/** TSR: the project never overrides DefaultFeatureAntiAliasing, so this is what r.AntiAliasingMethod is already running at before the row is ever touched. */
	UPROPERTY(Config)
	ESimCopterAntiAliasingMethod AntiAliasingMethod = ESimCopterAntiAliasingMethod::Tsr;

	UPROPERTY(Config)
	bool bVolumetricFog = true;

	UPROPERTY(Config)
	float EmissiveBrightness = 1.0f;

	UPROPERTY(Config)
	ESimCopterTimeOfDayMode TimeOfDayMode = ESimCopterTimeOfDayMode::Dynamic;

	/** Noon, so switching to Static without touching the slider gives full daylight. */
	UPROPERTY(Config)
	float StaticTimeOfDayHours = 12.0f;

	/** 7 and 3, matching USimCopterDayNightLengthComponent's own defaults. */
	UPROPERTY(Config)
	float DayRealMinutes = 7.0f;

	UPROPERTY(Config)
	float NightRealMinutes = 3.0f;

	UPROPERTY(Config)
	float HudScale = 1.0f;

	/**
	 * False until the resolution has been seeded from the display once. Without it the seeding could
	 * not tell "never configured" from "the player deliberately chose the same size as the desktop",
	 * and would re-seed over their choice on every launch.
	 */
	UPROPERTY(Config)
	bool bResolutionSeededFromDisplay = false;

	void ApplySound(const UObject* WorldContextObject);
	void ApplyGraphics(const UObject* WorldContextObject);

	/**
	 * The half of Low Power Graphics that is a one-shot on the transition rather than a value to
	 * re-assert: the overall scalability level and the screen percentage, both of which the player
	 * can still move afterwards from the Quality and Resolution Scale rows.
	 */
	void ApplyLowPowerScalability();

	/** First run only: puts the window on the native resolution of the display it opened on. */
	void SeedResolutionFromDisplay();
};
