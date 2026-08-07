// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "SimCopterSmokeStacks.generated.h"

struct FMaxisMeshObject;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UProceduralMeshComponent;
class UTexture2D;

// Chimney smoke, and it is not a particle system in the original either.
//
// Eight of the shipped city models carry Maxis face-type-26 markers - the EFFECT-marker type, whose
// material byte is an effect class rather than the palette index type 25 carries:
//
//   IN160 (0xa1) x6   IN162 (0xa3) x8   IN163 (0xa4) x6   IN164 (0xa5) x4
//   IN165 (0xa6) x6   IN192 (0xd3) x8   PP202 (0xdd) x4   PP207 (0xe2) x6
//
// All of them are effect class 1, and on every one of those models the markers run in a vertical
// trail from roughly three quarters of the building's height to its very top - they ARE the plume,
// authored as static positions above the stack. The original draws each one with FUN_00496da0, the
// same stochastic screen-pixel kernel that draws FIREPTS's twenty-two fire points, and class 1
// resolves through the selector table at 0x00504830 to the "light smoke" family: greys #959595,
// #A5A5A5, #B5B5B5, #C0C0C0.
//
// So this is the same renderer USimCopterFireRenderComponent already runs, pointed at a different
// marker set: one entry per authored marker, fixed in the world, instead of a template cloned into
// every burning tile. Both go through FSimCopterEffectRasterizer, which is where the decoded
// kernel/stencil/selector rules live.
//
// AppendMaxisMeshObject drops these markers on the floor (a single vertex is neither a polygon nor
// one of its two-point lines), which is why the stacks were bare.
UCLASS(ClassGroup = (SimCopter), meta = (BlueprintSpawnableComponent))
class SIMCOPTERREMAKE_API USimCopterSmokeStacksComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	USimCopterSmokeStacksComponent();

	// One authored face-type-26 marker, in this component's space.
	struct FSmokeMarker
	{
		FVector LocalOffset = FVector::ZeroVector;
		// The face's material byte. Every shipped chimney marker is 1; kept per marker because
		// FSimCopterEffectRasterizer keys its whole kernel off it and FIREPTS proves a model can
		// mix classes.
		uint8 EffectClass = 1;
		// Set on the topmost marker of each placed building, so a chimney gets one point light
		// rather than one per puff.
		bool bPointLightAnchor = false;
	};

	// Palette must be the shared SIM3D colour map the city built its meshes with - the selector
	// atlas is indices into it. False (with OutError set) if the atlas could not be built.
	bool InitSmokeAssets(const TArray<FColor>& Palette, UMaterialInterface* InCardMaterial, FString& OutError);
	bool IsReady() const { return bAssetsReady; }

	void SetSmokeMarkers(TArray<FSmokeMarker> InMarkers);
	void ClearSmokeMarkers();
	int32 GetSmokeMarkerCount() const { return Markers.Num(); }
	int32 GetSmokeStackLightCount() const { return PointLights.Num(); }

	// Rebuilds the drawn kernels against the live camera. Cheap to call every frame.
	void SyncSmoke(float TimeSeconds, const FVector& CameraLocation);
	void SyncSmokeFromPlayerCamera(float TimeSeconds);

	// Pulls every face-type-26 marker out of a GEO object, with the caller's units/scale and the
	// city builder's 180-degree yaw, exactly as FSimCopterFlashingLightSchedule::ExtractLightPoints
	// does for type 25. Returns how many were added.
	static int32 ExtractSmokeMarkers(
		const FMaxisMeshObject& Object,
		float ModelUnitsPerCentimeter,
		float ModelScale,
		bool bApplyCityMeshOrientation,
		TArray<FSmokeMarker>& OutMarkers);

	// The Maxis face type these markers use. Type 25 is the neighbouring BLINK type - do not mix
	// them up; see Docs/memory/simcopter-flashing-lights.md.
	static constexpr uint8 EffectMarkerFaceType = 26;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Smoke Stacks")
	bool bEnabled = true;

	// --- the point light half -------------------------------------------------------------------
	// A remake addition, like the beacons' point lights: the original had no dynamic lighting, so a
	// plume at night was a grey smudge over an unlit roof. One light per stack, seated at the
	// topmost marker.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Smoke Stacks")
	bool bCastPointLights = true;

	// Defaults to the effect class's own selector palette - the "light smoke" greys above - rather
	// than an invented colour. Override it for a warmer furnace glow.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Smoke Stacks")
	bool bUsePaletteLightColor = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Smoke Stacks")
	FColor PointLightColor = FColor(255, 196, 140);

	UPROPERTY(EditAnywhere, Category = "SimCopter|Smoke Stacks", meta = (ClampMin = "0.0"))
	float PointLightIntensity = 4000.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Smoke Stacks", meta = (ClampMin = "100.0"))
	float PointLightAttenuationRadiusCm = 2400.0f;

	// Off by default for the same reason the street lights' are: hundreds of them, and MegaLights
	// pays per shadowed light.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Smoke Stacks")
	bool bPointLightsCastShadows = false;

	// The plume is lit by daylight and only wants its own light once the sun is off it.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Smoke Stacks", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DaytimeIntensityScale = 0.15f;

private:
	void RebuildPointLights();
	void RefreshPointLightIntensity();

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> MeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> CardMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CardMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> SelectorAtlas;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> PointLights;

	TArray<FSmokeMarker> Markers;
	TArray<FColor> SharedPalette;
	bool bAssetsReady = false;
	float AppliedLightScale = -1.0f;
};
