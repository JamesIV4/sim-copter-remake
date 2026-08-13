// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterEffectExposure.h"

#include "Components/DirectionalLightComponent.h"
#include "Engine/World.h"
#include "Game/SimCopterSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "UObject/UObjectIterator.h"

/**
 * Records the renderer's own exposure metering for the world it belongs to.
 *
 * `FSceneView::GetLastAverageSceneLuminance()` is the GPU readback of what the auto exposure
 * metered - the measurement the effect emissive floor rides on - and a view extension is the only
 * public way a game module can reach it: `ULocalPlayer` keeps its `FSceneViewStateReference`s
 * private, and the accessor lives on the view.
 *
 * `SetupView` is a game-thread callback, and the subsystem reads this on the game thread too, so
 * the value needs no synchronisation.
 */
class FSimCopterExposureViewExtension : public FSceneViewExtensionBase
{
public:
	FSimCopterExposureViewExtension(const FAutoRegister& AutoRegister, const UWorld* InWorld)
		: FSceneViewExtensionBase(AutoRegister)
		, World(InWorld)
	{
	}

	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override
	{
		// The player's view and nothing else. A scene capture, a reflection capture or a thumbnail
		// meters its own framing, and letting one of those set the city's effect brightness would
		// make the fire flicker with whatever else happens to be rendering.
		if (InView.bIsSceneCapture || InView.bIsReflectionCapture || InView.bIsPlanarReflection)
		{
			return;
		}
		if (InViewFamily.Scene == nullptr || InViewFamily.Scene->GetWorld() != World.Get())
		{
			return;
		}

		const float Luminance = InView.GetLastAverageSceneLuminance();
		if (Luminance > 0.0f)
		{
			AverageSceneLuminanceNits = Luminance;
		}
	}

	float GetAverageSceneLuminanceNits() const { return AverageSceneLuminanceNits; }

private:
	TWeakObjectPtr<const UWorld> World;
	float AverageSceneLuminanceNits = 0.0f;
};

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
	TEXT("1 is the original's balance; raise it to make effects pop. Multiplies the Settings screen's ")
	TEXT("Emissive Brightness rather than replacing it, so this stays a live tuning knob."),
	ECVF_Default);

// The measured half, on a switch of its own so the sun-only behaviour can be compared against it
// on screen without a rebuild - and so a bad exposure readback is one console command away from
// being ruled out.
static int32 GSimCopterEffectMeasuredFloor = 1;
static FAutoConsoleVariableRef CVarSimCopterEffectMeasuredFloor(
	TEXT("SimCopter.Effects.MeasuredFloor"),
	GSimCopterEffectMeasuredFloor,
	TEXT("1 (default): an unlit effect card is never dimmer than the average scene luminance the ")
	TEXT("auto exposure metered last frame, which is what keeps fire, spray and smoke off black ")
	TEXT("through the dawn/dusk window - the sun model alone reports near-darkness there because ")
	TEXT("its cosine is against flat ground. 0: sun model only, the pre-2026-08-12 behaviour."),
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

float SimCopterEffectExposure::ComputeMeasuredFloorNits(
	const float AverageSceneLuminanceNits,
	const float EffectBrightness)
{
	const float SafeLuminance = FMath::Max(AverageSceneLuminanceNits, 0.0f);
	const float SafeBrightness = FMath::Max(EffectBrightness, 0.0f);
	if (SafeLuminance <= 0.0f)
	{
		return 0.0f;
	}

	// Already a luminance, so it needs no E/PI conversion - it IS the "as bright as what is around
	// it" number the sun model computes the long way round. The cap is the noon value: the effects
	// are part of the frame the exposure meters, so an uncapped floor is a positive feedback loop.
	const float NoonNits = ComputeEffectEmissiveNits(NoonIlluminanceLux, SafeBrightness, 0.0f);
	return FMath::Min(SafeLuminance * SafeBrightness, NoonNits);
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

float USimCopterEffectExposureSubsystem::GetMeasuredSceneLuminanceNits()
{
	GetEffectEmissiveNits();
	return CachedSceneLuminanceNits;
}

void USimCopterEffectExposureSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Only a real game/PIE world renders anything to meter. Editor preview and inactive worlds keep
	// the sun model, which is what they had before.
	const UWorld* World = GetWorld();
	if (World != nullptr && (World->IsGameWorld() || World->WorldType == EWorldType::Editor))
	{
		ExposureViewExtension =
			FSceneViewExtensions::NewExtension<FSimCopterExposureViewExtension>(World);
	}
}

void USimCopterEffectExposureSubsystem::Deinitialize()
{
	// The registry holds a weak reference, so dropping ours is the unregistration.
	ExposureViewExtension.Reset();
	Super::Deinitialize();
}

float USimCopterEffectExposureSubsystem::ReadAverageSceneLuminanceNits() const
{
	if (!GSimCopterEffectMeasuredFloor || !ExposureViewExtension.IsValid())
	{
		return 0.0f;
	}
	return ExposureViewExtension->GetAverageSceneLuminanceNits();
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

	const float BrightnessScale = GetBrightnessScale(GetWorld());
	CachedSceneLuminanceNits = ReadAverageSceneLuminanceNits();

	// The sun model owns the day, where it is the authored look and comfortably the larger of the
	// two. The measured floor owns the dawn/dusk window, where the sun's cosine against flat ground
	// has collapsed but the scene has not. Taking the larger makes the handover continuous - at the
	// crossover they are equal - and can only ever raise a card, never dim one.
	CachedEmissiveNits = FMath::Max(
		SimCopterEffectExposure::ComputeEffectEmissiveNits(CachedIlluminanceLux, BrightnessScale),
		SimCopterEffectExposure::ComputeMeasuredFloorNits(CachedSceneLuminanceNits, BrightnessScale));
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
	return SimCopterEffectExposure::ComputeEffectEmissiveNits(
		SimCopterEffectExposure::NoonIlluminanceLux,
		GetBrightnessScale(World));
}

float USimCopterEffectExposureSubsystem::GetSurfaceEmissiveNitsForWorld(const UWorld* World)
{
	// Deliberately the sun model ONLY, with no measured floor: these are pedestrian sprites and
	// privanim heads, ordinary surfaces that happen to be drawn unlit, and they are supposed to go
	// dark when the sun does. The floor exists for light sources.
	float IlluminanceLux = SimCopterEffectExposure::NoonIlluminanceLux;
	if (World != nullptr)
	{
		if (USimCopterEffectExposureSubsystem* Subsystem =
			World->GetSubsystem<USimCopterEffectExposureSubsystem>())
		{
			IlluminanceLux = Subsystem->GetKeyIlluminanceLux();
		}
	}

	return SimCopterEffectExposure::ComputeEffectEmissiveNits(
		IlluminanceLux,
		GetBrightnessScale(World),
		SimCopterEffectExposure::SurfaceMinimumEmissiveNits);
}

void USimCopterEffectExposureSubsystem::ApplyEmissiveNits(
	UMaterialInstanceDynamic* MaterialInstance,
	const UWorld* World,
	const bool bIsLightSource)
{
	if (MaterialInstance == nullptr)
	{
		return;
	}

	MaterialInstance->SetScalarParameterValue(
		GetEmissiveNitsParameterName(),
		bIsLightSource ? GetEffectEmissiveNitsForWorld(World) : GetSurfaceEmissiveNitsForWorld(World));
}

float USimCopterEffectExposureSubsystem::GetBrightnessScale(const UWorld* World)
{
	float Scale = GSimCopterEffectBrightness;
	if (const USimCopterSettings* Settings = USimCopterSettings::Get(World))
	{
		Scale *= Settings->GetEmissiveBrightness();
	}
	return FMath::Max(Scale, 0.0f);
}
