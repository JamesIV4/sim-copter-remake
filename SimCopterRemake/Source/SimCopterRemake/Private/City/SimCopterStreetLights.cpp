// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterStreetLights.h"

#include "City/SimCopterDayNight.h"
#include "Components/SpotLightComponent.h"
#include "Game/SimCopterLowPowerMode.h"

USimCopterStreetLightsComponent::USimCopterStreetLightsComponent()
{
	// Only to follow the night blend, which moves slowly and is compared before anything is written.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void USimCopterStreetLightsComponent::BeginPlay()
{
	Super::BeginPlay();

	// Only matters when bDisableInLowPower is set - see the note on it for why street lights survive
	// the mode by default where the headlights and beacons do not.
	if (!LowPowerChangedHandle.IsValid())
	{
		LowPowerChangedHandle = SimCopterLowPower::OnChanged().AddWeakLambda(
			this, [this](bool) { AppliedIntensityScale = -1.0f; RefreshStreetLights(); });
	}
	RefreshStreetLights();
}

void USimCopterStreetLightsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (LowPowerChangedHandle.IsValid())
	{
		SimCopterLowPower::OnChanged().Remove(LowPowerChangedHandle);
		LowPowerChangedHandle.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void USimCopterStreetLightsComponent::SetStreetLights(TArray<FPlacement> InPlacements)
{
	ClearStreetLights();
	Placements = MoveTemp(InPlacements);

	SpotLights.Reserve(Placements.Num());
	for (int32 Index = 0; Index < Placements.Num(); ++Index)
	{
		const FPlacement& Placement = Placements[Index];
		USpotLightComponent* Light = NewObject<USpotLightComponent>(this);
		if (Light == nullptr)
		{
			continue;
		}
		Light->SetupAttachment(this);
		Light->SetRelativeLocation(Placement.Location);
		// Straight down. The lamp head's own arm is already baked into the apex the emitter
		// measured, so the light does not need to be aimed anywhere else.
		Light->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));

		const float HalfAngle = FMath::Clamp(Placement.ConeHalfAngleDegrees, 5.0f, 80.0f);
		Light->SetOuterConeAngle(HalfAngle);
		Light->SetInnerConeAngle(HalfAngle * FMath::Clamp(InnerConeFraction, 0.05f, 1.0f));
		Light->SetAttenuationRadius(
			FMath::Max(100.0f, Placement.ConeLengthCm * FMath::Max(1.0f, AttenuationRadiusScale)));
		Light->SetLightColor(FLinearColor(Color));
		Light->SetIntensity(0.0f);
		// Exposure independent, like every other gameplay light in this project: the level's day
		// sequence runs the sun at 120,000 lux, and a raw beam metered against that contributes
		// nothing the tonemapper can show. See Docs/memory/simcopter-exposure-scale.md.
		Light->SetInverseExposureBlend(1.0f);
		Light->SetCastShadows(bCastShadows);
		Light->bAffectsWorld = true;
		Light->SetVisibility(false);
		Light->RegisterComponent();
		SpotLights.Add(Light);
	}

	AppliedIntensityScale = -1.0f;
	RefreshStreetLights();
}

void USimCopterStreetLightsComponent::ClearStreetLights()
{
	for (USpotLightComponent* Light : SpotLights)
	{
		if (Light != nullptr)
		{
			Light->DestroyComponent();
		}
	}
	SpotLights.Reset();
	Placements.Reset();
	AppliedIntensityScale = -1.0f;
}

void USimCopterStreetLightsComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RefreshStreetLights();
}

void USimCopterStreetLightsComponent::RefreshStreetLights()
{
	if (SpotLights.IsEmpty())
	{
		return;
	}

	float Scale = 0.0f;
	if (bEnabled && !(bDisableInLowPower && SimCopterLowPower::IsEnabled()))
	{
		// The lamps ride the same night blend the lit windows and the fog do, so the whole city
		// comes on together across the sunset fade instead of snapping at a threshold.
		float NightAlpha = 1.0f;
		if (const USimCopterDayNightSubsystem* DayNight = USimCopterDayNightSubsystem::Get(this))
		{
			NightAlpha = DayNight->GetNightAlpha();
		}
		Scale = FMath::Lerp(FMath::Clamp(DaytimeIntensityScale, 0.0f, 1.0f), 1.0f, FMath::Clamp(NightAlpha, 0.0f, 1.0f));
	}

	if (Scale < MinimumVisibleScale)
	{
		Scale = 0.0f;
	}
	if (FMath::IsNearlyEqual(Scale, AppliedIntensityScale, 1.0f / 512.0f))
	{
		return;
	}
	AppliedIntensityScale = Scale;

	const bool bLightsVisible = Scale > 0.0f;
	const float LightIntensity = FMath::Max(0.0f, Intensity) * Scale;
	for (USpotLightComponent* Light : SpotLights)
	{
		if (Light == nullptr)
		{
			continue;
		}
		Light->SetVisibility(bLightsVisible);
		if (bLightsVisible)
		{
			Light->SetIntensity(LightIntensity);
		}
	}
}
