// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/SimCopterVehicleMaterialSubsystem.h"

#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ConfigCacheIni.h"

void USimCopterVehicleMaterialSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (GConfig != nullptr && !GGameUserSettingsIni.IsEmpty())
	{
		double Value = 0.0;
		if (GConfig->GetDouble(GetConfigSection(), GetMetallicConfigKey(), Value, GGameUserSettingsIni))
		{
			Metallic = FMath::Clamp(static_cast<float>(Value), 0.0f, 1.0f);
		}
	}
}

USimCopterVehicleMaterialSubsystem* USimCopterVehicleMaterialSubsystem::Get(const UObject* WorldContext)
{
	if (WorldContext == nullptr)
	{
		return nullptr;
	}

	const UWorld* World = WorldContext->GetWorld();
	return World != nullptr ? World->GetSubsystem<USimCopterVehicleMaterialSubsystem>() : nullptr;
}

UMaterialInstanceDynamic* USimCopterVehicleMaterialSubsystem::GetVehicleMaterial(UMaterialInterface* BaseMaterial)
{
	if (BaseMaterial == nullptr)
	{
		return nullptr;
	}

	if (VehicleMaterial == nullptr)
	{
		VehicleMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		VehicleBaseMaterial = BaseMaterial;
		if (VehicleMaterial != nullptr)
		{
			// Whatever the slider was left on, so a vehicle spawned mid-session matches the fleet.
			VehicleMaterial->SetScalarParameterValue(GetMetallicParameterName(), Metallic);
		}
	}

	return VehicleBaseMaterial == BaseMaterial ? VehicleMaterial.Get() : nullptr;
}

void USimCopterVehicleMaterialSubsystem::SetMetallic(float NewMetallic)
{
	Metallic = FMath::Clamp(NewMetallic, 0.0f, 1.0f);
	if (GConfig != nullptr && !GGameUserSettingsIni.IsEmpty())
	{
		GConfig->SetDouble(GetConfigSection(), GetMetallicConfigKey(), Metallic, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	if (VehicleMaterial != nullptr)
	{
		// A no-op until M_SimCopterLitVertexColor actually exposes a "Metallic" scalar wired to
		// its Metallic input - SetScalarParameterValue on a name the material does not have is
		// silently ignored rather than an error.
		VehicleMaterial->SetScalarParameterValue(GetMetallicParameterName(), Metallic);
	}
}
