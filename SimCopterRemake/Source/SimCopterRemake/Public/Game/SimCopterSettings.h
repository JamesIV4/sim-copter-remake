// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SimCopterSettings.generated.h"

class USimCopterAudioSubsystem;
class USimCopterRadioSubsystem;

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
	bool bDlssEnabled = false;

	UPROPERTY(Config)
	ESimCopterDlssQuality DlssQuality = ESimCopterDlssQuality::Auto;

	UPROPERTY(Config)
	ESimCopterFrameGenMode FrameGenMode = ESimCopterFrameGenMode::Off;

	UPROPERTY(Config)
	int32 FrameGenMultiple = FrameGenMultipleMin;

	UPROPERTY(Config)
	float HudScale = 1.0f;

	void ApplySound(const UObject* WorldContextObject);
	void ApplyGraphics(const UObject* WorldContextObject);
};
