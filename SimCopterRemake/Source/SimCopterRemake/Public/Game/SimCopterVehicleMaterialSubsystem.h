// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SimCopterVehicleMaterialSubsystem.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;

// One shared dynamic instance of `M_SimCopterLitVertexColor` for everything that draws original
// GEO *vehicle* geometry: the helicopter fuselage and its rotors, the ground agents' cars, and the
// ambient planes/trains/boats. They all pointed at the base material asset directly, so there was
// nothing to tune at runtime; routing them through one instance means a single scalar moves the
// whole fleet at once, which is what a debug slider wants.
//
// The city's buildings and terrain deliberately do NOT come through here - they share the same
// base asset, and leaving them on it is what keeps a metallic helicopter from turning the whole
// skyline to chrome.
UCLASS()
class SIMCOPTERREMAKE_API USimCopterVehicleMaterialSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// Scalar parameter on M_SimCopterLitVertexColor driving the material's Metallic input.
	static const TCHAR* GetMetallicParameterName() { return TEXT("Metallic"); }

	static USimCopterVehicleMaterialSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	static const TCHAR* GetConfigSection() { return TEXT("SimCopter.VehicleMaterial"); }
	static const TCHAR* GetMetallicConfigKey() { return TEXT("Metallic"); }

	// The shared instance, created from BaseMaterial the first time it is asked for. Returns null
	// (and the caller keeps its own base material) if BaseMaterial is null, or if a later caller
	// asks with a *different* base than the one the instance was built from - the whole point is
	// that these all share one asset, and silently re-skinning a caller would be worse than
	// leaving it alone.
	UMaterialInstanceDynamic* GetVehicleMaterial(UMaterialInterface* BaseMaterial);

	// 0 = dielectric, 1 = fully metallic. The original had no PBR at all, so anything above 0 is
	// taste; 0.84 is where the fleet was dialled to on 2026-07-30. Persisted to GameUserSettings
	// and reloaded on Initialize.
	float GetMetallic() const { return Metallic; }
	void SetMetallic(float NewMetallic);

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> VehicleMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> VehicleBaseMaterial;

	float Metallic = 0.84f;
};
