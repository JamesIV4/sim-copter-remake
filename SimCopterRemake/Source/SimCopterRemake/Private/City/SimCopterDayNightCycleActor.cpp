// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterDayNightCycleActor.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/PostProcessComponent.h"

ASimCopterDayNightCycleActor::ASimCopterDayNightCycleActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// --- Directional light (the sun / moon) ---
	SunLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("SunLight"));
	SunLight->SetupAttachment(SceneRoot);
	SunLight->SetIntensity(SunIntensityDay);
	SunLight->SetLightColor(SunColorNoon);
	// Enable atmospheric/fog interaction so the sky atmosphere and fog respond to the sun.
	SunLight->bAtmosphereSunLight = true;
	SunLight->AtmosphereSunLightIndex = 0;
	// Soft shadows from the sun.
	SunLight->CastShadows = true;
	SunLight->SetDynamicShadowCascades(3);
	SunLight->DynamicShadowDistanceMovableLight = 25000.0f;
	// Wider source angle for softer sunrise/sunset shadows.
	SunLight->LightSourceAngle = 1.5f;
	SunLight->SetVolumetricScatteringIntensity(1.0f);

	// --- Sky atmosphere (Rayleigh/Mie physical scattering) ---
	SkyAtmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("SkyAtmosphere"));
	SkyAtmosphere->SetupAttachment(SceneRoot);

	// --- Sky light (ambient fill, recaptured periodically) ---
	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(SceneRoot);
	SkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	SkyLight->bRealTimeCapture = true;
	SkyLight->SetIntensity(SkyLightIntensityDay);
	SkyLight->bLowerHemisphereIsBlack = false;

	// --- Height fog (distance haze, warms at sunrise/sunset) ---

	HeightFog =
		CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("HeightFog"));

	HeightFog->SetupAttachment(SceneRoot);
	HeightFog->SetMobility(EComponentMobility::Movable);

	// Match the static fog's transform.
	HeightFog->SetRelativeLocation(FVector(-5600.0f, -50.0f, -6850.0f));

	// Standard exponential height fog.
	HeightFog->SetFogDensity(FogDensityDay); // 0.0436f
	HeightFog->FogHeightFalloff = 0.2f;
	HeightFog->FogMaxOpacity = 1.0f;
	HeightFog->StartDistance = 0.0f;
	HeightFog->FogCutoffDistance = 0.0f;

	// Let Sky Atmosphere supply the primary fog coloration.
	HeightFog->SetFogInscatteringColor(FLinearColor::Black);
	HeightFog->SkyAtmosphereAmbientContributionColorScale =
		FogAtmosphereContributionDay;

	// Directional inscattering.
	HeightFog->DirectionalInscatteringExponent = 4.0f;
	HeightFog->DirectionalInscatteringStartDistance = 10000.0f;
	HeightFog->DirectionalInscatteringLuminance = FLinearColor::Black;

	// Volumetric fog.
	HeightFog->bEnableVolumetricFog = true;
	HeightFog->VolumetricFogScatteringDistribution = -0.505606f;
	HeightFog->VolumetricFogAlbedo = FColor(255, 212, 226);
	HeightFog->VolumetricFogEmissive = FogEmissiveDay;
	HeightFog->VolumetricFogExtinctionScale = 2.42067f;
	HeightFog->VolumetricFogDistance = 15000.0f;
	HeightFog->VolumetricFogStartDistance = 200.0f;
	HeightFog->VolumetricFogNearFadeInDistance = 20000.0f;
	HeightFog->VolumetricFogStaticLightingScatteringIntensity = 1.0f;

	// Ensure the dynamically created component renders in-game.
	HeightFog->SetVisibility(true);
	HeightFog->SetHiddenInGame(false);

	// --- Post process volume (exposure control for day/night) ---
	PostProcessVolume =
		CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcessVolume"));
	PostProcessVolume->SetupAttachment(SceneRoot);
	PostProcessVolume->bUnbound = true;
	// Enable these specific post-process overrides.
	PostProcessVolume->Settings.bOverride_AutoExposureMinBrightness = true;
	PostProcessVolume->Settings.bOverride_AutoExposureMaxBrightness = true;
	PostProcessVolume->Settings.AutoExposureMinBrightness = ExposureDay;
	PostProcessVolume->Settings.AutoExposureMaxBrightness = ExposureDay;
}

void ASimCopterDayNightCycleActor::BeginPlay()
{
	Super::BeginPlay();

	// Apply the initial time of day immediately so the first frame is correctly lit.
	ApplyTimeOfDay();
}

void ASimCopterDayNightCycleActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bCycleEnabled)
	{
		AdvanceClock(DeltaTime);
	}

	ApplyTimeOfDay();
}

// --- Public API ---

bool ASimCopterDayNightCycleActor::IsNightTime() const
{
	// Night = between sunset and sunrise (wrapping through midnight).
	// Add a small overlap (half the golden hour) so headlights come on during late sunset.
	const float NightStart = SunsetTime - GoldenHourHalfWidth * 0.5f;
	const float NightEnd = SunriseTime + GoldenHourHalfWidth * 0.5f;

	if (NightStart < NightEnd)
	{
		// Would only happen with very odd sunrise/sunset settings; treat as always night.
		return true;
	}

	// Normal case: night wraps through midnight.
	return TimeOfDay >= NightStart || TimeOfDay <= NightEnd;
}

float ASimCopterDayNightCycleActor::GetSunPitchDegrees() const
{
	return ComputeSunPitch();
}

void ASimCopterDayNightCycleActor::SetTimeOfDay(float InTimeOfDay)
{
	TimeOfDay = FMath::Fmod(FMath::Max(InTimeOfDay, 0.0f), 1.0f);
	ApplyTimeOfDay();
}

// --- Private ---

void ASimCopterDayNightCycleActor::AdvanceClock(float DeltaTime)
{
	// The cycle runs at different speeds for day vs. night:
	// - Day portion: SunriseTime..SunsetTime  → DayLengthMinutes real minutes
	// - Night portion: SunsetTime..SunriseTime (wrapping) → NightLengthMinutes real minutes

	const float DaySpan = SunsetTime - SunriseTime;           // Fraction of clock that is day
	const float NightSpan = 1.0f - DaySpan;                   // Fraction of clock that is night
	const float DayDurationSeconds = DayLengthMinutes; // * 60.0f;
	const float NightDurationSeconds = NightLengthMinutes; // * 60.0f;

	// Are we currently in the day or night portion?
	const bool bInDay = TimeOfDay >= SunriseTime && TimeOfDay < SunsetTime;

	float Rate;
	if (bInDay && DayDurationSeconds > 0.0f)
	{
		// Clock fraction per real second during daytime
		Rate = DaySpan / DayDurationSeconds;
	}
	else if (NightDurationSeconds > 0.0f)
	{
		Rate = NightSpan / NightDurationSeconds;
	}
	else
	{
		Rate = 0.0f;
	}

	TimeOfDay += Rate * DeltaTime;
	if (TimeOfDay >= 1.0f)
	{
		TimeOfDay -= 1.0f;
	}
}

float ASimCopterDayNightCycleActor::ComputeSunPitch() const
{
	// Map the normalized time to sun elevation:
	// SunriseTime (0.25) → 0° (horizon)
	// Noon (midpoint of day) → +90° (zenith)
	// SunsetTime (0.75) → 0° (horizon)
	// Midnight (midpoint of night, wrapping) → -90° (nadir)

	const float DayMid = (SunriseTime + SunsetTime) * 0.5f;
	const float NightMid = FMath::Fmod(SunsetTime + (1.0f - SunsetTime + SunriseTime) * 0.5f, 1.0f);

	// During daytime: sunrise→noon→sunset maps to 0→+90→0 using a sine curve
	if (TimeOfDay >= SunriseTime && TimeOfDay <= SunsetTime)
	{
		const float DaySpan = SunsetTime - SunriseTime;
		const float DayProgress = (TimeOfDay - SunriseTime) / DaySpan; // 0..1
		return FMath::Sin(DayProgress * PI) * 90.0f;
	}

	// During nighttime: sunset→midnight→sunrise maps to 0→-90→0
	float NightProgress;
	if (TimeOfDay > SunsetTime)
	{
		// Sunset to midnight portion
		const float NightSpan = 1.0f - SunsetTime + SunriseTime;
		NightProgress = (TimeOfDay - SunsetTime) / NightSpan;
	}
	else
	{
		// Midnight to sunrise portion
		const float NightSpan = 1.0f - SunsetTime + SunriseTime;
		NightProgress = (1.0f - SunsetTime + TimeOfDay) / NightSpan;
	}

	return -FMath::Sin(NightProgress * PI) * 90.0f;
}

float ASimCopterDayNightCycleActor::ComputeGoldenHourAlpha(float AtTime) const
{
	// How close AtTime is to sunrise or sunset, normalized to 0..1 within the golden hour band.
	const float DistSunrise = FMath::Abs(AtTime - SunriseTime);
	const float DistSunset = FMath::Abs(AtTime - SunsetTime);
	// Handle wrap-around for distances near midnight
	const float DistSunriseWrap = FMath::Min(DistSunrise, 1.0f - DistSunrise);
	const float DistSunsetWrap = FMath::Min(DistSunset, 1.0f - DistSunset);

	const float MinDist = FMath::Min(DistSunriseWrap, DistSunsetWrap);

	if (MinDist >= GoldenHourHalfWidth)
	{
		return 0.0f;
	}

	return 1.0f - (MinDist / GoldenHourHalfWidth);
}

float ASimCopterDayNightCycleActor::ComputeDaytimeFactor() const
{
	// 1.0 during full day, 0.0 during full night, smooth transition during golden hours.
	const float SunPitch = ComputeSunPitch();

	// Normalize: sun above horizon (pitch > 0) is day, below is night.
	// Use a smooth hermite over the transition band near the horizon.
	const float TransitionBandDegrees = 15.0f;

	if (SunPitch >= TransitionBandDegrees)
	{
		return 1.0f;
	}
	if (SunPitch <= -TransitionBandDegrees)
	{
		return 0.0f;
	}

	// Smooth hermite in [-TransitionBand, +TransitionBand]
	const float T = (SunPitch + TransitionBandDegrees) / (2.0f * TransitionBandDegrees);
	return FMath::SmoothStep(0.0f, 1.0f, T);
}

void ASimCopterDayNightCycleActor::ApplyTimeOfDay()
{
	const float SunPitch = ComputeSunPitch();
	const float DayFactor = ComputeDaytimeFactor();
	const float GoldenAlpha = ComputeGoldenHourAlpha(TimeOfDay);

	// --- Sun rotation ---
	if (SunLight != nullptr)
	{
		// Pitch: negative = looking down (UE convention for directional light pointing at ground).
		// When sun is at +90° (zenith), the light should point straight down = pitch -90.
		// When sun is at 0° (horizon), the light is horizontal = pitch 0.
		// When sun is at -90° (nadir/below), the light points up = pitch +90 (but intensity is ~0).
		const float LightPitch = -SunPitch;
		SunLight->SetWorldRotation(FRotator(LightPitch, SunAzimuthDegrees + 180.0f, 0.0f));

		// --- Sun intensity ---
		// Smoothly interpolate between day and night intensity.
		const float Intensity = FMath::Lerp(SunIntensityNight, SunIntensityDay, DayFactor);
		SunLight->SetIntensity(Intensity);

		// --- Sun color ---
		// Base: lerp between noon color and moon color by day factor.
		FLinearColor BaseColor = FLinearColor::LerpUsingHSV(MoonColor, SunColorNoon, DayFactor);

		// Golden hour override: blend toward sunrise or sunset color.
		if (GoldenAlpha > 0.0f)
		{
			// Determine if we're closer to sunrise or sunset.
			const float DistSunrise = FMath::Min(
				FMath::Abs(TimeOfDay - SunriseTime),
				1.0f - FMath::Abs(TimeOfDay - SunriseTime));
			const float DistSunset = FMath::Min(
				FMath::Abs(TimeOfDay - SunsetTime),
				1.0f - FMath::Abs(TimeOfDay - SunsetTime));

			const FLinearColor& GoldenColor = (DistSunrise < DistSunset) ? SunColorSunrise : SunColorSunset;

			// Smooth golden hour blend with a power curve for more dramatic centre peak.
			const float GoldenStrength = FMath::Pow(GoldenAlpha, 0.7f);
			BaseColor = FLinearColor::LerpUsingHSV(BaseColor, GoldenColor, GoldenStrength);
		}

		SunLight->SetLightColor(BaseColor);

		// Widen the source angle near the horizon for softer, warmer shadows at sunrise/sunset.
		const float HorizonSoftening = (1.0f - FMath::Abs(SunPitch) / 90.0f);
		SunLight->LightSourceAngle = FMath::Lerp(0.7f, 3.5f, HorizonSoftening * HorizonSoftening);
	}

	// --- Sky light ---
	if (SkyLight != nullptr)
	{
		SkyLight->SetIntensity(FMath::Lerp(SkyLightIntensityNight, SkyLightIntensityDay, DayFactor));

		// Recapture periodically. Real-time capture handles this automatically but we can also
		// force a recapture when the sun has moved enough. The component's own bRealTimeCapture
		// does this already, so we just ensure it's enabled.
		SkyLightRecaptureTimer += GetWorld() != nullptr ? GetWorld()->GetDeltaSeconds() : 0.0f;
		if (SkyLightRecaptureTimer >= SkyLightRecaptureIntervalSeconds)
		{
			SkyLightRecaptureTimer = 0.0f;
			SkyLight->RecaptureSky();
		}
	}

	// --- Height fog ---
	if (HeightFog != nullptr)
	{
		// Preserve approximately the same amount of fog throughout the cycle.
		const float CurrentFogDensity =
			FMath::Lerp(FogDensityNight, FogDensityDay, DayFactor);

		HeightFog->SetFogDensity(CurrentFogDensity);

		// Keep the optical characteristics identical to the static fog.
		HeightFog->SetVolumetricFogScatteringDistribution(-0.505606f);
		HeightFog->SetVolumetricFogExtinctionScale(2.42067f);

		// Keep these black so Sky Atmosphere supplies the primary fog color.
		HeightFog->SetFogInscatteringColor(FLinearColor::Black);
		HeightFog->SetDirectionalInscatteringColor(FLinearColor::Black);

		// Reduce atmospheric illumination at night, but do not nearly eliminate it.
		FLinearColor AtmosphereContribution =
			FMath::Lerp(
				FogAtmosphereContributionNight,
				FogAtmosphereContributionDay,
				DayFactor);

		// Add a subtle warm contribution near sunrise and sunset.
		if (GoldenAlpha > 0.0f)
		{
			const FLinearColor GoldenAtmosphere(
				1.0f,
				0.55f,
				0.22f);

			AtmosphereContribution = FMath::Lerp(
				AtmosphereContribution,
				GoldenAtmosphere,
				GoldenAlpha * 0.25f);
		}

		HeightFog->SetSkyAtmosphereAmbientContributionColorScale(
			AtmosphereContribution);

		// Keep some low-level illumination at night so the fog remains visible.
		FLinearColor CurrentEmissive =
			FMath::Lerp(
				FogEmissiveNight,
				FogEmissiveDay,
				DayFactor);

		if (GoldenAlpha > 0.0f)
		{
			const FLinearColor GoldenEmissive(
				0.4f,
				0.12f,
				0.025f);

			CurrentEmissive = FMath::Lerp(
				CurrentEmissive,
				GoldenEmissive,
				GoldenAlpha * 0.2f);
		}

		HeightFog->SetVolumetricFogEmissive(CurrentEmissive);
	}


	// --- Exposure ---
	if (PostProcessVolume != nullptr)
	{
		const float Exposure =
			FMath::Lerp(ExposureNight, ExposureDay, DayFactor);

		PostProcessVolume->Settings.AutoExposureMinBrightness = Exposure;
		PostProcessVolume->Settings.AutoExposureMaxBrightness = Exposure;
	}
}
