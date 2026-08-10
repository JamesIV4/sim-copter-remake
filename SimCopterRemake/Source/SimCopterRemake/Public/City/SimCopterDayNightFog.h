// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SimCopterDayNightFog.generated.h"

class UExponentialHeightFogComponent;

// Time-of-day curve for the height fog, kept free of the component so it can be tested without a
// world or a day sequence.
//
// The city moved to a `CelestialVaultDaySequenceActor` for its day/night cycle. That actor owns the
// `UExponentialHeightFogComponent` the level's fog comes from, but its Fog Density is a single
// authored constant - the sky, the sun and the moon all move while the fog stays put. This drives
// that one property from the sequence's time of day.
namespace SimCopterDayNightFog
{
// Hours are the day sequence's own units: `ADaySequenceActor::GetTimeOfDay()` returns 0..DayLength,
// and the shipped level runs a 24 hour day. Everything here wraps on DayLengthHours rather than
// assuming 24, so a shortened day cycle still lands the fades on the configured hours.
constexpr float DefaultDayLengthHours = 24.0f;

// Hours elapsed since `EventHour`, wrapped into [0, DayLengthHours). A negative or out-of-range
// input is folded back in first, so an unclamped time of day cannot produce a negative age.
float HoursSince(float CurrentHour, float EventHour, float DayLengthHours);

// 0 = full day density, 1 = full night density.
//
// **Whichever of sunset and sunrise happened most recently decides the direction of the fade.**
// That single rule is what makes the wrap over midnight fall out for free - at 03:00 sunset is 9
// hours ago and sunrise is 21 hours ago, so the night fade owns the frame - and it degrades sanely
// when the fade is longer than the night it sits in: the two fades cannot fight, the newer one just
// takes over from wherever the other one had reached.
//
// The fade STARTS at the configured hour and completes FadeDurationHours later, so a sunset of
// 18:00 with a 1 hour fade is full day density at 18:00 and full night density at 19:00. A fade
// duration of zero is a hard switch on the hour.
float ComputeNightAlpha(
	float CurrentHour,
	float SunriseHour,
	float SunsetHour,
	float FadeDurationHours,
	float DayLengthHours,
	bool bSmoothFade);

// The density the fog should be showing at this time of day.
float ComputeFogDensity(
	float CurrentHour,
	float SunriseHour,
	float SunsetHour,
	float FadeDurationHours,
	float DayFogDensity,
	float NightFogDensity,
	float DayLengthHours,
	bool bSmoothFade);

// The inscattering luminance the fog should be showing at this time of day, on the SAME curve as
// the density above - so the fog cannot end up night-blue while it is still daytime-thin.
//
// These are LINEAR colours, which is what `FogInscatteringLuminance` stores and what the colour
// picker's R/G/B fields show; its Hex field is sRGB, so the two read differently for the same
// colour (linear 0.01 is sRGB 0x1A).
FLinearColor ComputeFogInscatteringLuminance(
	float CurrentHour,
	float SunriseHour,
	float SunsetHour,
	float FadeDurationHours,
	const FLinearColor& DayLuminance,
	const FLinearColor& NightLuminance,
	float DayLengthHours,
	bool bSmoothFade);

// Defaults for the two ends, as measured in the editor's colour picker.
//   day   linear (0.01, 0.01, 0.01)   = sRGB #1A1A1A, a neutral haze
//   night linear (0.008, 0.008, 0.01) = sRGB #15151A, dimmer and pushed to blue
SIMCOPTERREMAKE_API extern const FLinearColor DefaultDayInscatteringLuminance;
SIMCOPTERREMAKE_API extern const FLinearColor DefaultNightInscatteringLuminance;
}

/**
 * Drives Fog Density on the owning actor's Exponential Height Fog from the day sequence's time of
 * day, fading between a day value and a night value around configurable sunrise and sunset hours.
 *
 * Add it to the `CelestialVaultDaySequenceActor` in the level: it finds that actor's own
 * `ExponentialHeightFogComponent` and reads the time of day off the same actor, so there is nothing
 * to wire up. `ADaySequenceActor::GetTimeOfDay()` already falls back to the editor's Time Of Day
 * Preview outside a game world, so scrubbing that slider in the details panel moves the fog in the
 * viewport exactly as it will in game.
 */
UCLASS(ClassGroup = (SimCopter), meta = (BlueprintSpawnableComponent, DisplayName = "SimCopter Day/Night Fog"))
class SIMCOPTERREMAKE_API USimCopterDayNightFogComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimCopterDayNightFogComponent();

	/** Master switch. Off leaves everything on the fog component exactly as authored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day/Night Fog")
	bool bEnabled = true;

	/**
	 * Whether Fog Density is driven. Separate from the colour below because they are genuinely
	 * separate decisions: a density that already reads well at every hour is worth leaving alone
	 * while still letting the fog turn blue after dark.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day/Night Fog", meta = (DisplayName = "Drive Fog Density"))
	bool bDriveFogDensity = true;

	/** Whether Fog Inscattering Colour is driven, on the same curve as the density. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day/Night Fog", meta = (DisplayName = "Drive Inscattering Colour"))
	bool bDriveInscatteringLuminance = true;

	/** Fog Density held through the day, between the end of the sunrise fade and the start of sunset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day/Night Fog", meta = (EditCondition = "bDriveFogDensity", ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	float DayFogDensity = 0.1f;

	/** Fog Density held through the night, between the end of the sunset fade and the start of sunrise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day/Night Fog", meta = (EditCondition = "bDriveFogDensity", ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	float NightFogDensity = 0.25f;

	/**
	 * Fog Inscattering Colour held through the day. Linear, and HDR-enabled in the picker because
	 * these values sit far below 1 - the fog's colour is a luminance, not a swatch.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day/Night Fog", meta = (DisplayName = "Day Inscattering Colour", EditCondition = "bDriveInscatteringLuminance", HideAlphaChannel))
	FLinearColor DayInscatteringLuminance = FLinearColor(0.01f, 0.01f, 0.01f, 1.0f);

	/** Fog Inscattering Colour held through the night. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day/Night Fog", meta = (DisplayName = "Night Inscattering Colour", EditCondition = "bDriveInscatteringLuminance", HideAlphaChannel))
	FLinearColor NightInscatteringLuminance = FLinearColor(0.008f, 0.008f, 0.01f, 1.0f);

	/** Hour the sunrise fade STARTS at; the fog reaches Day Fog Density this many hours later. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day/Night Fog", meta = (DisplayName = "Sunup Hour", ForceUnits = "Hours", ClampMin = "0.0", ClampMax = "24.0"))
	float SunriseHour = 6.0f;

	/** Hour the sunset fade STARTS at; the fog reaches Night Fog Density this many hours later. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day/Night Fog", meta = (ForceUnits = "Hours", ClampMin = "0.0", ClampMax = "24.0"))
	float SunsetHour = 18.0f;

	/** How long each fade takes, in time-of-day hours. Zero switches on the hour with no fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day/Night Fog", meta = (ForceUnits = "Hours", ClampMin = "0.0", ClampMax = "12.0"))
	float FadeDurationHours = 1.0f;

	/** Eases the fade in and out instead of running it linearly, so neither end shows a kink. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Day/Night Fog")
	bool bSmoothFade = true;

	/** The time of day the fog was last evaluated at, for reading off the details panel. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Day/Night Fog", meta = (ForceUnits = "Hours"))
	float CurrentTimeOfDay = 0.0f;

	/** The Fog Density last written to the fog component. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Day/Night Fog")
	float CurrentFogDensity = 0.0f;

	/** The Fog Inscattering Colour last written to the fog component. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Day/Night Fog", meta = (DisplayName = "Current Inscattering Colour"))
	FLinearColor CurrentInscatteringLuminance = FLinearColor::Black;

	/** 0 in full daylight, 1 in full night; what both the density and the colour are lerped by. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Day/Night Fog")
	float CurrentNightAlpha = 0.0f;

	/** Evaluates the curve for a time of day and pushes density and colour at the fog component. */
	UFUNCTION(BlueprintCallable, Category = "Day/Night Fog")
	void ApplyFogForTimeOfDay(float TimeOfDayHours);

	/** Re-reads the time of day from the owning day sequence actor and applies it. */
	UFUNCTION(BlueprintCallable, Category = "Day/Night Fog")
	void RefreshFromDaySequence();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/** The owner's height fog, resolved on demand and cached. */
	UExponentialHeightFogComponent* ResolveFogComponent();

	/** Day length in hours from the owning day sequence actor, or the 24 hour default. */
	float ResolveDayLengthHours() const;

	UPROPERTY(Transient)
	TObjectPtr<UExponentialHeightFogComponent> CachedFogComponent;

	/** Set once the owner has been checked and found not to be a day sequence actor. */
	bool bWarnedMissingDaySequence = false;

	/** Set once the owner has been checked and found to have no height fog. */
	bool bWarnedMissingFog = false;
};
