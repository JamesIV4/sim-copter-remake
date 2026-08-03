// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimCopterDayNightCycleActor.generated.h"

class UDirectionalLightComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UExponentialHeightFogComponent;
class USceneComponent;
class UPostProcessComponent; 

// Drives the city level's sun, sky, ambient light and fog through a continuous day/night cycle.
//
// The original SimCopter had no continuous cycle: each career city carried a binary flag
// (career.twk Ctrl8, stored as FSimCopterCareerCity::DayOrNight) that set the palette to day
// or night. The remake keeps that flag as the STARTING time when a city is entered and adds a
// smooth cycle on top: the sun rotates, the sky atmosphere produces physical sunrise/sunset
// scattering, and all light intensities interpolate along tunable curves.
//
// ASimCopterGameMode spawns one of these when the city level begins, the same way it spawns the
// traffic, mission and hangar systems. The actor exposes TimeOfDay as a normalized 0..1 clock
// (0.0 = midnight, 0.25 = sunrise, 0.5 = noon, 0.75 = sunset) and a query function IsNightTime()
// that vehicles use to switch their headlights on/off.
UCLASS()
class SIMCOPTERREMAKE_API ASimCopterDayNightCycleActor : public AActor
{
	GENERATED_BODY()

public:
	ASimCopterDayNightCycleActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// --- Public queries ---

	// Normalized time of day: 0.0 = midnight, 0.25 = sunrise, 0.5 = noon, 0.75 = sunset.
	float GetTimeOfDay() const { return TimeOfDay; }

	// True when the scene is dark enough that headlights, streetlights etc. should be on.
	// Returns true between sunset and sunrise (with a small overlap for the twilight transition).
	bool IsNightTime() const;

	// The sun's current pitch angle in degrees: +90 at zenith (noon), 0 at horizon, -90 below.
	float GetSunPitchDegrees() const;

	// --- Setters (game mode, console commands, save loading) ---

	void SetTimeOfDay(float InTimeOfDay);
	void SetCycleEnabled(bool bEnabled) { bCycleEnabled = bEnabled; }
	bool IsCycleEnabled() const { return bCycleEnabled; }

protected:
	// --- Sky components ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|DayNight")
    TObjectPtr<UPostProcessComponent> PostProcessVolume;

	UPROPERTY(VisibleAnywhere, Category = "SimCopter|DayNight")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "SimCopter|DayNight")
	TObjectPtr<UDirectionalLightComponent> SunLight;

	UPROPERTY(VisibleAnywhere, Category = "SimCopter|DayNight")
	TObjectPtr<USkyAtmosphereComponent> SkyAtmosphere;

	UPROPERTY(VisibleAnywhere, Category = "SimCopter|DayNight")
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere, Category = "SimCopter|DayNight")
	TObjectPtr<UExponentialHeightFogComponent> HeightFog;

	// --- Cycle timing ---

	// Real-time minutes for the daytime half of the cycle (sunrise to sunset).
	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Timing", meta = (ClampMin = "0.5", ClampMax = "60.0"))
	float DayLengthMinutes = 6.0f;

	// Real-time minutes for the nighttime half (sunset to sunrise).
	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Timing", meta = (ClampMin = "0.5", ClampMax = "60.0"))
	float NightLengthMinutes = 4.0f;

	// Whether the clock advances each tick.
	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Timing")
	bool bCycleEnabled = true;

	// --- Sun parameters ---

	// Compass direction the sun rises from (0 = North, 90 = East, 180 = South, 270 = West).
	// Default 90 = east, giving an east-to-west arc.
	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Sun", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float SunAzimuthDegrees = 90.0f;

	// Directional light intensity at solar noon (lux).
	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Sun", meta = (ClampMin = "0.1"))
	float SunIntensityDay = 4.0f;

	// Directional light intensity at midnight (moonlight, lux).
	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Sun", meta = (ClampMin = "0.0"))
	float SunIntensityNight = 0.0f;

	// Sun color at noon.
	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Sun")
	FLinearColor SunColorNoon = FLinearColor(1.0f, 0.90f, 0.78f);

	// Sun color during sunrise golden hour.
	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Sun")
	FLinearColor SunColorSunrise = FLinearColor(1.0f, 0.65f, 0.3f);

	// Sun color during sunset golden hour (can differ from sunrise for asymmetry).
	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Sun")
	FLinearColor SunColorSunset = FLinearColor(1.0f, 0.55f, 0.25f);

	// Moon tint at midnight.
	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Sun")
	FLinearColor MoonColor = FLinearColor(0.6f, 0.65f, 0.85f);

	// Hide the directional light once the sun is this far below the horizon.
	// A negative value preserves some directional sunlight during twilight.
	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Sun",
		meta = (ClampMin = "-20.0", ClampMax = "0.0"))
	float SunHideBelowHorizonDegrees = -6.0f;

	// --- Sky light ---

	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Sky", meta = (ClampMin = "0.0"))
	float SkyLightIntensityDay = 3.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Sky", meta = (ClampMin = "0.0"))
	float SkyLightIntensityNight = 0.3f;

	// How often the sky light recaptures the scene (seconds). Lower = smoother ambient changes
	// but higher cost. UE5's real-time sky light capture is fast.
	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Sky", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float SkyLightRecaptureIntervalSeconds = 1.5f;

	// --- Fog ---

	// Keep the actual amount of fog consistent all day and night.
	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Fog",
		meta = (ClampMin = "0.0"))
	float FogDensityDay = 0.0436f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Fog",
		meta = (ClampMin = "0.0"))
	float FogDensityNight = 0.4f;

	// A small baseline inscattering color prevents the fog from disappearing
	// when the sun is overhead or below the horizon.
	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Fog")
	FLinearColor FogInscatteringDay =
		FLinearColor(0.035f, 0.045f, 0.060f);

	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Fog")
	FLinearColor FogInscatteringNight =
		FLinearColor(0.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Fog",
		meta = (ClampMin = "0.0"))
	float FogExtinctionScaleDay = 2.42067f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Fog",
		meta = (ClampMin = "0.0"))
	float FogExtinctionScaleNight = 2.745291f;

	// Allow Sky Atmosphere to color the fog without being its only source
	// of visible fog illumination.
	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Fog")
	FLinearColor FogAtmosphereContributionDay =
		FLinearColor(0.70f, 0.75f, 0.85f);

	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Fog")
	FLinearColor FogAtmosphereContributionNight =
		FLinearColor(0.014f, 0.018f, 0.036f);

	// A restrained emissive floor keeps nighttime fog visible without glowing.
	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Fog")
	FLinearColor FogEmissiveDay =
		FLinearColor(0.382f, 0.594f, 1.0f);

	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Fog")
	FLinearColor FogEmissiveNight =
		FLinearColor(0.159f, 0.213f, 0.422f);

	// --- Exposure ---
	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Exposure")
	float ExposureDay = 1.15f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Exposure")
	float ExposureNight = 2.0f;

	// --- Transition band ---

	// Fraction of the cycle around sunrise/sunset considered the "golden hour" transition.
	// 0.06 = about 36 seconds of a 10-minute cycle.
	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Timing", meta = (ClampMin = "0.01", ClampMax = "0.15"))
	float GoldenHourHalfWidth = 0.06f;

	// Where sunrise/sunset sit on the 0..1 clock.
	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Timing", meta = (ClampMin = "0.1", ClampMax = "0.4"))
	float SunriseTime = 0.25f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|DayNight|Timing", meta = (ClampMin = "0.6", ClampMax = "0.9"))
	float SunsetTime = 0.75f;

private:
	// The normalized clock: 0.0 = midnight, 0.25 = sunrise, 0.5 = noon, 0.75 = sunset.
	float TimeOfDay = 0.35f;

	// Timer for sky light recapture throttling.
	float SkyLightRecaptureTimer = 0.0f;

	// Advance the clock by DeltaTime, accounting for different day/night rates.
	void AdvanceClock(float DeltaTime);

	// Apply the current TimeOfDay to all lighting components.
	void ApplyTimeOfDay();

	// Map TimeOfDay to the sun's pitch angle (-90..+90).
	float ComputeSunPitch() const;

	// How far we are into the golden hour: 0 outside it, 1 at the centre (sunrise/sunset exact).
	float ComputeGoldenHourAlpha(float AtTime) const;

	// 0..1 daytime factor: 1 during full day, 0 during full night, smooth transition.
	float ComputeDaytimeFactor() const;
};
