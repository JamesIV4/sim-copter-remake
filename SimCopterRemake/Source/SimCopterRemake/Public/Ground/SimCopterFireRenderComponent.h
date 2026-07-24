// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "SimCopterFireRenderComponent.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;

// One flame the owner wants drawn this frame. Key is a stable per-flame id (the mission
// system's flame slot index, or a burning-car key); World is the base-of-flame world location;
// Scale drives the flame size; FlickerSeed decorrelates each flame's flicker phase.
struct FSimCopterFlameVisual
{
	int32 Key = INDEX_NONE;
	FVector World = FVector::ZeroVector;
	float Scale = 1.0f;
	float FlickerSeed = 0.0f;
	bool bVehicleFire = false;
};

// Renders the original SimCopter building/car fire-point template (FIREPTS, GEO object id 0x120).
// Its face type 26/light type 1 entries are effect/light markers: material indices 1 and 2 are
// semantic smoke/fire classes, not literal palette colors. The original renderer expanded those
// markers with FUN_00496da0's stochastic, palette-indexed screen-pixel kernels.
UCLASS(ClassGroup = (SimCopter), meta = (BlueprintSpawnableComponent))
class SIMCOPTERREMAKE_API USimCopterFireRenderComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	USimCopterFireRenderComponent();

	// Loads the GEO packs from OriginalGameRoot once and extracts the FIREPTS point cloud.
	// InFlameMaterial should be the masked unlit sprite-texture material. Returns false (and
	// sets OutError) if the flame object could not be read.
	bool InitFireAssets(const FString& OriginalGameRoot, UMaterialInterface* InFlameMaterial, FString& OutError);
	bool IsReady() const { return bAssetsReady; }

	// Rebuild the drawn flames. CameraLocation orients the software-renderer cards.
	void SyncFlames(const TArray<FSimCopterFlameVisual>& Visuals, float TimeSeconds, const FVector& CameraLocation);

private:
	struct FFirePoint
	{
		FVector LocalOffset = FVector::ZeroVector; // cm, base of the flame at Z = 0
		uint8 EffectClass = 0;
	};

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> MeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> FlameMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FlameMaterialInstance;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> SelectorAtlas;

	TArray<FFirePoint> FirePoints;
	TArray<FColor> SharedPalette;
	bool bAssetsReady = false;

	// Matches the ground-vehicle mesh conversion so the flame reads at the city's 0.25 scale.
	static constexpr float ModelUnitsPerCentimeter = 2621.44f;
	static constexpr float FlameModelScale = 0.25f;
};
