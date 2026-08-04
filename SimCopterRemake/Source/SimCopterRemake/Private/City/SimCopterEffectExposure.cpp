// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterEffectExposure.h"

#include "Components/DirectionalLightComponent.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"

namespace
{
// Live knob for tuning the effects on screen without a rebuild - the whole point of deriving the
// value instead of baking it is that one number moves all of them together.
static float GSimCopterEffectBrightness = SimCopterEffectExposure::DefaultEffectBrightness;
static FAutoConsoleVariableRef CVarSimCopterEffectBrightness(
	TEXT("SimCopter.Effects.Brightness"),
	GSimCopterEffectBrightness,
	TEXT("How bright an unlit effect card (fire, spray, dust, blinking markers) is relative to white ")
	TEXT("ground under the same light. The materials apply their own authored 1.4 on top of this, so ")
	TEXT("1 is the original's balance; raise it to make effects pop."),
	ECVF_Default);

// A directional light can move (the sun does, constantly) but the SET of them almost never changes,
// so the scan runs at most this often. Everything in between rides the cached component pointer,
// whose intensity and direction are read fresh every frame.
constexpr double KeyLightRescanIntervalSeconds = 2.0;
}

float SimCopterEffectExposure::ComputeGroundIlluminanceLux(
	const float IntensityLux,
	const FVector& LightDirection)
{
	if (IntensityLux <= 0.0f)
	{
		return 0.0f;
	}

	const FVector Direction = LightDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return 0.0f;
	}

	// A light component's forward vector points the way the light travels, so a sun overhead points
	// DOWN and -Z is the cosine against an up-facing surface. Below the horizon it is negative and
	// clamps to nothing, which is exactly what should happen after sunset.
	const float CosIncidence = FMath::Max(static_cast<float>(-Direction.Z), 0.0f);
	return IntensityLux * CosIncidence;
}

float SimCopterEffectExposure::ComputeEffectEmissiveNits(
	const float KeyIlluminanceLux,
	const float EffectBrightness,
	const float MinimumNits)
{
	const float SafeIlluminance = FMath::Max(KeyIlluminanceLux, 0.0f);
	const float SafeBrightness = FMath::Max(EffectBrightness, 0.0f);
	const float SafeMinimum = FMath::Max(MinimumNits, 0.0f);

	// L = E * rho / PI for a Lambertian surface; rho is 1 because the card is being placed against
	// the brightest thing the ground could be, not against a specific material.
	const float GroundLuminanceNits = SafeIlluminance / UE_PI;
	return FMath::Max(GroundLuminanceNits * SafeBrightness, SafeMinimum);
}

UDirectionalLightComponent* USimCopterEffectExposureSubsystem::ResolveKeyLight()
{
	if (UDirectionalLightComponent* Cached = KeyLight.Get())
	{
		return Cached;
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	// Rate limited: with no directional light at all (the main menu, a test map) the cache stays
	// empty and this would otherwise walk every component in the world every frame.
	const double Now = FPlatformTime::Seconds();
	if (LastScanTimeSeconds >= 0.0 && (Now - LastScanTimeSeconds) < KeyLightRescanIntervalSeconds)
	{
		return nullptr;
	}
	LastScanTimeSeconds = Now;

	// By component, not by actor class: the celestial vault's sun is a UDirectionalLightComponent on
	// the day sequence actor, so TActorIterator<ADirectionalLight> finds nothing in this level.
	UDirectionalLightComponent* Brightest = nullptr;
	float BrightestIntensity = 0.0f;
	for (TObjectIterator<UDirectionalLightComponent> It; It; ++It)
	{
		UDirectionalLightComponent* Light = *It;
		if (Light == nullptr || Light->GetWorld() != World || !Light->IsRegistered() || !Light->IsVisible())
		{
			continue;
		}

		if (Light->Intensity > BrightestIntensity)
		{
			BrightestIntensity = Light->Intensity;
			Brightest = Light;
		}
	}

	KeyLight = Brightest;
	return Brightest;
}

float USimCopterEffectExposureSubsystem::GetKeyIlluminanceLux()
{
	GetEffectEmissiveNits();
	return CachedIlluminanceLux;
}

float USimCopterEffectExposureSubsystem::GetEffectEmissiveNits()
{
	// Every effect component in the world asks for this while rebuilding, and the answer cannot
	// change inside a frame, so the scan and the maths run once.
	const uint64 Frame = GFrameCounter;
	if (bHasCachedValue && Frame == CachedFrame)
	{
		return CachedEmissiveNits;
	}

	CachedFrame = Frame;
	bHasCachedValue = true;
	CachedIlluminanceLux = 0.0f;

	if (const UDirectionalLightComponent* Light = ResolveKeyLight())
	{
		// The moon is a directional light too and is by far the dimmer of the two, so the brightest
		// one is the sun by day and the moon by night without needing to know which is which.
		CachedIlluminanceLux = SimCopterEffectExposure::ComputeGroundIlluminanceLux(
			Light->Intensity,
			Light->GetComponentTransform().GetUnitAxis(EAxis::X));
	}

	CachedEmissiveNits = SimCopterEffectExposure::ComputeEffectEmissiveNits(
		CachedIlluminanceLux,
		GSimCopterEffectBrightness);
	return CachedEmissiveNits;
}

float USimCopterEffectExposureSubsystem::GetEffectEmissiveNitsForWorld(const UWorld* World)
{
	if (World != nullptr)
	{
		if (USimCopterEffectExposureSubsystem* Subsystem =
			World->GetSubsystem<USimCopterEffectExposureSubsystem>())
		{
			return Subsystem->GetEffectEmissiveNits();
		}
	}

	// No world, no subsystem: fall back to a noon-ish value rather than to zero, because zero is
	// the black card this whole file exists to prevent.
	return SimCopterEffectExposure::ComputeEffectEmissiveNits(120000.0f);
}
