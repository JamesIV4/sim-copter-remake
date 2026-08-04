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

// The scalar the city atlas material blends its day and night pages with.
SIMCOPTERREMAKE_API extern const TCHAR* const NightBlendParameterName;

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

	/** The level's day sequence actor, or null. */
	ADaySequenceActor* GetDaySequenceActor() const;

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

	TWeakObjectPtr<ADaySequenceActor> CachedDaySequenceActor;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> CachedParameterCollection;

	float TimeOfDayHours = 0.0f;
	float DayLengthHours = SimCopterDayNightFog::DefaultDayLengthHours;
	float NightAlpha = 0.0f;

	/** Last value written to the collection, so an unchanged blend costs nothing. */
	float PublishedNightBlend = -1.0f;

	/** Set once the collection has been looked for and not found, to stop the log repeating. */
	bool bWarnedMissingCollection = false;

	/** Time of the last actor scan, so a level with no day sequence does not scan every frame. */
	double LastActorScanSeconds = -1.0;

	/** The settings only need pushing when they move, not every tick. */
	uint8 AppliedTimeOfDayMode = 0xff;
	float AppliedStaticTimeOfDayHours = -1.0f;
	float AppliedDayRealMinutes = -1.0f;
	float AppliedNightRealMinutes = -1.0f;
};
