// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "SimCopterFireRenderComponent.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

// One flame the owner wants drawn this frame. Key is a stable per-flame id (the mission
// system's flame slot index, or a burning-car key); World is the base-of-flame world location;
// Scale drives the flame size; FlickerSeed decorrelates each flame's flicker phase.
struct FSimCopterFlameVisual
{
	int32 Key = INDEX_NONE;
	FVector World = FVector::ZeroVector;
	float Scale = 1.0f;
	float FlickerSeed = 0.0f;
};

// Renders the original SimCopter building/car flame (FIREPTS, GEO object id 0x120). FIREPTS is
// not a solid mesh: its 22 "faces" are single-vertex points, i.e. a cloud of palette-coloured
// point sprites (hence "fire points"). This component extracts those points + their SIM3D palette
// colours once, then every tick rebuilds one procedural mesh of camera-facing, emissive,
// translucent quads - one per point per active flame - with a per-point flicker. Faithful to the
// original, which draws each fire point as a flat palette-coloured card.
UCLASS(ClassGroup = (SimCopter), meta = (BlueprintSpawnableComponent))
class SIMCOPTERREMAKE_API USimCopterFireRenderComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	USimCopterFireRenderComponent();

	// Loads the GEO packs from OriginalGameRoot once and extracts the FIREPTS point cloud.
	// InFlameMaterial should be the emissive/translucent particle material. Returns false (and
	// sets OutError) if the flame object could not be read.
	bool InitFireAssets(const FString& OriginalGameRoot, UMaterialInterface* InFlameMaterial, FString& OutError);
	bool IsReady() const { return bAssetsReady; }

	// Rebuild the drawn flames for the requested set. CameraLocation orients the billboards.
	void SyncFlames(const TArray<FSimCopterFlameVisual>& Visuals, float TimeSeconds, const FVector& CameraLocation);

private:
	struct FFirePoint
	{
		FVector LocalOffset = FVector::ZeroVector; // cm, base of the flame at Z = 0
		FLinearColor Color = FLinearColor::White;
	};

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> MeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> FlameMaterial;

	TArray<FFirePoint> FirePoints;
	float FireSpriteHalfSizeCm = 35.0f;
	float FireMaxLocalZ = 1.0f; // top of the fire-point cloud (cm), for the height->colour ramp
	bool bAssetsReady = false;

	// Matches the ground-vehicle mesh conversion so the flame reads at the city's 0.25 scale.
	static constexpr float ModelUnitsPerCentimeter = 2621.44f;
	static constexpr float FlameModelScale = 0.25f;
};
