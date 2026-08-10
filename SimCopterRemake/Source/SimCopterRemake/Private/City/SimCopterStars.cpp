// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterStars.h"

#include "CelestialVaultDaySequenceActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterStars, Log, All);

namespace
{
// The scalar on the stars material that shifts apparent magnitude before the luminance conversion.
// Confirmed against the material actually bound in the level, not the class default - the two are
// different assets here, and SetScalarParameterValue on a missing name is silently ignored.
const FName StarsMagnitudeOffsetParameterName(TEXT("MagnitudeOffset"));
}

USimCopterStarsComponent::USimCopterStarsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// InitStarsComponent only clears and refills instances, so it does not drop the material - but
	// reconstructing the actor re-runs the constructor's SetMaterial and would. Re-asserting every
	// tick is cheap and covers both. It also makes the value live while dragging it in the panel,
	// which matters because the editor throttles ticks hard when the window is not focused.
	bTickInEditor = true;
}

void USimCopterStarsComponent::BeginPlay()
{
	Super::BeginPlay();
	ApplyMagnitudeOffset();
}

void USimCopterStarsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ApplyMagnitudeOffset();
}

void USimCopterStarsComponent::ApplyMagnitudeOffset()
{
	if (!bOverrideMagnitudeOffset)
	{
		return;
	}

	UMaterialInstanceDynamic* StarsMaterial = ResolveStarsMaterial();
	if (StarsMaterial == nullptr)
	{
		return;
	}

	float ExistingOffset = 0.0f;
	if (!StarsMaterial->GetScalarParameterValue(FMaterialParameterInfo(StarsMagnitudeOffsetParameterName), ExistingOffset))
	{
		if (!bWarnedMissingParameter)
		{
			bWarnedMissingParameter = true;
			UE_LOG(LogSimCopterStars, Warning,
				TEXT("The stars material has no '%s' scalar parameter, so star brightness cannot be overridden. Has the bound material been swapped for one that is not energy conservative?"),
				*StarsMagnitudeOffsetParameterName.ToString());
		}
		return;
	}

	if (!FMath::IsNearlyEqual(ExistingOffset, MagnitudeOffset))
	{
		StarsMaterial->SetScalarParameterValue(StarsMagnitudeOffsetParameterName, MagnitudeOffset);
	}

	AppliedMagnitudeOffset = MagnitudeOffset;
}

UMaterialInstanceDynamic* USimCopterStarsComponent::ResolveStarsMaterial()
{
	ACelestialVaultDaySequenceActor* CelestialActor = Cast<ACelestialVaultDaySequenceActor>(GetOwner());
	if (CelestialActor == nullptr || CelestialActor->StarsComponent == nullptr)
	{
		if (!bWarnedMissingActor)
		{
			bWarnedMissingActor = true;
			UE_LOG(LogSimCopterStars, Warning,
				TEXT("%s is not on a CelestialVaultDaySequenceActor with a stars component, so there is nothing to adjust."),
				*GetPathName());
		}
		return nullptr;
	}

	UInstancedStaticMeshComponent* Stars = CelestialActor->StarsComponent;
	UMaterialInstanceDynamic* StarsMaterial = Cast<UMaterialInstanceDynamic>(Stars->GetMaterial(0));
	if (StarsMaterial == nullptr)
	{
		// Built from whatever is on slot 0, so the level's own material override stays the parent
		// and only the one parameter is added on top of it. Once created, GetMaterial returns this
		// instance, so the branch does not run again and no MID is leaked per tick.
		StarsMaterial = Stars->CreateAndSetMaterialInstanceDynamic(0);
	}

	return StarsMaterial;
}

#if WITH_EDITOR
void USimCopterStarsComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ApplyMagnitudeOffset();
}
#endif
