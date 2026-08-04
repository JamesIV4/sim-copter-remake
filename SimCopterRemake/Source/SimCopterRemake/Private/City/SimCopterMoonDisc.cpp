// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterMoonDisc.h"

#include "CelestialVaultDaySequenceActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterMoonDisc, Log, All);

namespace
{
// The scalar on M_Moon / MI_Moon that scales the lit side's emissive. Confirmed against the asset
// rather than assumed: SetScalarParameterValue on a name a material does not have is silently
// ignored, so a typo here would look exactly like the feature not working.
const FName MoonBrightnessParameterName(TEXT("Brightness"));
}

USimCopterMoonDiscComponent::USimCopterMoonDiscComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// The override has to be re-asserted, not set once: the celestial actor calls
	// CreateAndSetMaterialInstanceDynamic on the disc whenever it reconstructs, which throws away
	// every parameter it does not itself re-apply (it only re-applies MoonAge). Ticking in the
	// editor as well as at runtime is also what makes the value live while tuning it in the panel.
	bTickInEditor = true;
}

void USimCopterMoonDiscComponent::BeginPlay()
{
	Super::BeginPlay();
	ApplyBrightness();
}

void USimCopterMoonDiscComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ApplyBrightness();
}

void USimCopterMoonDiscComponent::ApplyBrightness()
{
	if (!bOverrideBrightness)
	{
		return;
	}

	UMaterialInstanceDynamic* MoonDiscMaterial = ResolveMoonDiscMaterial();
	if (MoonDiscMaterial == nullptr)
	{
		return;
	}

	// GetScalarParameterValue answers false for a parameter the material chain does not have, which
	// is the only way to catch a renamed parameter - the setter below would just do nothing.
	float ExistingBrightness = 0.0f;
	if (!MoonDiscMaterial->GetScalarParameterValue(FMaterialParameterInfo(MoonBrightnessParameterName), ExistingBrightness))
	{
		if (!bWarnedMissingParameter)
		{
			bWarnedMissingParameter = true;
			UE_LOG(LogSimCopterMoonDisc, Warning,
				TEXT("The moon disc's material has no '%s' scalar parameter, so its brightness cannot be overridden. Has the CelestialVault plugin's M_Moon changed?"),
				*MoonBrightnessParameterName.ToString());
		}
		return;
	}

	if (!FMath::IsNearlyEqual(ExistingBrightness, Brightness))
	{
		MoonDiscMaterial->SetScalarParameterValue(MoonBrightnessParameterName, Brightness);
	}

	AppliedBrightness = Brightness;
}

UMaterialInstanceDynamic* USimCopterMoonDiscComponent::ResolveMoonDiscMaterial()
{
	// Resolved through the typed actor rather than FindComponentByClass: the celestial actor carries
	// several UStaticMeshComponents (the compass, the deep sky dome, the velocity proxy), so a
	// class search would pick an arbitrary one. Going through the property means a plugin rename
	// breaks the build instead of silently brightening the wrong mesh.
	ACelestialVaultDaySequenceActor* CelestialActor = Cast<ACelestialVaultDaySequenceActor>(GetOwner());
	if (CelestialActor == nullptr || CelestialActor->MoonDiscComponent == nullptr)
	{
		if (!bWarnedMissingActor)
		{
			bWarnedMissingActor = true;
			UE_LOG(LogSimCopterMoonDisc, Warning,
				TEXT("%s is not on a CelestialVaultDaySequenceActor with a moon disc, so there is nothing to brighten."),
				*GetPathName());
		}
		return nullptr;
	}

	UStaticMeshComponent* MoonDisc = CelestialActor->MoonDiscComponent;
	UMaterialInstanceDynamic* MoonDiscMaterial = Cast<UMaterialInstanceDynamic>(MoonDisc->GetMaterial(0));
	if (MoonDiscMaterial == nullptr)
	{
		// Same call the actor makes for MoonAge. Reached when this component ticks before the actor
		// has made its own MID; sharing one instance keeps both overrides on the same material.
		MoonDiscMaterial = MoonDisc->CreateAndSetMaterialInstanceDynamic(0);
	}

	return MoonDiscMaterial;
}

#if WITH_EDITOR
void USimCopterMoonDiscComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Show the new brightness in the viewport on the spot instead of waiting for the next tick,
	// which the editor throttles hard when the window is not focused.
	ApplyBrightness();
}
#endif
