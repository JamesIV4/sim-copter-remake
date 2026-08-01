// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "SimCopterParticleFX.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;
class ASimCity2000CityActor;
class ASimCopterMissionSystemActor;

// Original effect types used by FUN_0048e0b0.  Keep these values in the port: callers and the
// updater use the type, rather than selecting a generic visual approximation.
enum class ESimCopterEffectType : uint8
{
	Smoke = 1,
	FireTrajectory = 2,
	SmallSmoke = 3,
	Debris = 4,
	Spray = 5,
	BucketDrip = 6,
	SmallSpray = 7,
	RotorWash = 8,
	SplashSubParticle = 9,
	HeavyDebris = 10,
	GeoSmoke = 11,
	BuildingFireSmoke = 12,
	BuildingFireEmber = 13,
	FireTrajectoryAlt = 14,
};

enum class ESimCopterEffectPool : uint8
{
	Smoke10,
	Geo10,
	Geo2,
	Debris30,
	Wash20,
	Trajectory70,
	Fire25,
	SplashColumns20,
	TilePuffs100,
};

// A fixed-pool entry corresponding to the decoded 0x12-dword entry.  Unreal uses normal float
// vectors, but retains the original type, pool, frame, cell, life and primitive topology.
struct FSimCopterEffectSlot
{
	bool bActive = false;
	ESimCopterEffectType Type = ESimCopterEffectType::Smoke;
	ESimCopterEffectPool Pool = ESimCopterEffectPool::Smoke10;
	FVector Position = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	FIntVector Direction1616 = FIntVector::ZeroValue;
	FIntPoint Cell = FIntPoint::ZeroValue;
	int32 Age1616 = 0;
	int32 Life1616 = 0;
	int32 SpawnTimer1616 = 0;
	int32 MotionScale1616 = 0;
	// Unspent time carried into the next fixed water step (see SimulationStep1616).
	int32 StepCarry1616 = 0;
	float SizeCm = 0.0f;
	uint8 PaletteIndex = 0;
	uint8 PointPaletteIndices[4] = { 0, 0, 0, 0 };
	uint8 PointCount = 1;
	uint8 FaceType = 0;
	uint8 EffectClass = 0;
	int8 FrameCursor = 0;
	int32 GeoObjectId = INDEX_NONE;
	bool bTrajectory = false;
	bool bApplyGravity = false;
	bool bBurstEmitted = false;
};

// Typed recreation of the SimCopter effects pools. Runtime rendering retains the source split:
// face 0x17 fixed 2x2 screen points, face 0x1a palette-selector kernels, and the actual small
// SMOKE/DEBRIS GEO objects loaded from the original MAX archives.
UCLASS(ClassGroup = (SimCopter), meta = (BlueprintSpawnableComponent))
class SIMCOPTERREMAKE_API USimCopterParticleFXComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	USimCopterParticleFXComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	bool InitEffectAssets(const FString& OriginalGameRoot, FString& OutError);

	// Typed creator (FUN_0048e0b0).  Size is the original point size converted to cm.
	bool SpawnEffect(ESimCopterEffectType Type, const FVector& World, const FVector& VelocityCmPerSec,
		float SizeCm = 0.0f, int32 CellX = INDEX_NONE, int32 CellY = INDEX_NONE);
	// Tile puffs (FUN_004af220) and splash columns (FUN_004af100/FUN_004af3b0).
	bool SpawnTilePuff(const FVector& World, uint8 EffectClass, int32 CellX = INDEX_NONE, int32 CellY = INDEX_NONE);
	// FUN_004af100 seats the column 32 units below the point it is given, so a water splash rises
	// up through the surface it came out of. An impact against a wall or a slope wants the burst
	// where the contact was, so it passes bSubmergeOrigin = false.
	bool SpawnSplashColumn(const FVector& World, int32 ScaleExponent = 4, uint8 PaletteIndex = 0xFF,
		int32 CellX = INDEX_NONE, int32 CellY = INDEX_NONE, bool bSubmergeOrigin = true);
	// Crash landing phase one: one class-1 puff, five debris items and a scale-4 splash column.
	void SpawnHardLanding(const FVector& World, bool bWaterSurface, int32 CellX = INDEX_NONE, int32 CellY = INDEX_NONE);

	// Compatibility entry points for unrelated legacy callers.  New gameplay paths should call the
	// typed API above.  This selects a 1-point trajectory with the supplied palette colour.
	void SpawnParticle(const FVector& World, const FVector& VelocityCmPerSec, float SizeCm,
		const FLinearColor& Color, float LifeSeconds, float GravityCmPerSec2 = 0.0f);
	void SpawnRing(const FVector& World, int32 Count, float SpeedCmPerSec, float InitialRiseCmPerSec,
		float SizeCm, const FLinearColor& Color, float LifeSeconds, float GravityCmPerSec2 = 0.0f);

	bool HasActiveParticles() const;
	int32 GetActiveCount(ESimCopterEffectPool Pool) const;
	static int32 GetPoolCapacity(ESimCopterEffectPool Pool);
	static int32 GetPointCountForType(ESimCopterEffectType Type);
	static float GetLifetimeForType(ESimCopterEffectType Type);
	static int32 GetLifetime1616ForType(ESimCopterEffectType Type);
	static int32 GetFaceTypeForType(ESimCopterEffectType Type);
	static float GetDefaultSizeCmForType(ESimCopterEffectType Type);
	static int32 GetTilePuffLife1616();
	static float GetTilePuffRiseSpeedCmPerSec(uint8 EffectClass);
	static int32 GetSplashRingParticleCount() { return 14; }
	static bool IsRotorWashEligible(float HeightCm, int32 RotorSpeed1616);

protected:
	virtual void OnRegister() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> MeshComponent;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> CardMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> KernelMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> KernelMaterialInstance;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> SelectorAtlas;
	UPROPERTY(EditAnywhere, Category = "SimCopter|FX")
	TObjectPtr<UMaterialInterface> CardMaterialOverride;

	struct FGeoEffectPoint
	{
		FVector LocalOffset = FVector::ZeroVector;
		uint8 FaceType = 0;
		uint8 MaterialIndex = 0;
	};

	struct FGeoEffectFace
	{
		TArray<FVector> Vertices;
		FLinearColor Color = FLinearColor::White;
	};

	struct FGeoEffectTemplate
	{
		TArray<FGeoEffectPoint> Points;
		TArray<FGeoEffectFace> Faces;
		float SourceSpanCm = 1.0f;
	};

	TArray<FSimCopterEffectSlot> Slots;
	TArray<FColor> SharedPalette;
	TMap<int32, FGeoEffectTemplate> GeoTemplates;
	uint8 FireRampCursor = 0x10;
	uint8 FireTipCursor = 0;
	int32 DebrisObjectCursor = 0;
	TWeakObjectPtr<ASimCity2000CityActor> CachedCityActor;
	TWeakObjectPtr<ASimCopterMissionSystemActor> CachedMissionActor;

	FVector GetCameraLocation() const;
	FIntPoint GetCellForWorld(const FVector& World) const;
	bool Allocate(ESimCopterEffectPool Pool, FSimCopterEffectSlot*& OutSlot);
	void ConfigureEffect(FSimCopterEffectSlot& Slot, ESimCopterEffectType Type, const FVector& World,
		const FVector& VelocityCmPerSec, float SizeCm, const FIntPoint& Cell);
	void UpdateSlots(float DeltaTime);
	void AdvanceWaterTrajectory(FSimCopterEffectSlot& Slot, int32 Delta1616);
	void AdvanceWaterTrajectoryStep(FSimCopterEffectSlot& Slot, int32 Step1616);
	bool FindWaterTrajectoryImpact(
		const FVector& Start,
		const FVector& End,
		FVector& OutImpact,
		bool& bOutWaterSurface,
		FIntPoint& OutCell);
	ASimCity2000CityActor* ResolveCityActor();
	ASimCopterMissionSystemActor* ResolveMissionActor();
	void UpdateSplashColumns();
	void RebuildMesh(const FVector& CameraLocation);
	FLinearColor PaletteColor(uint8 PaletteIndex) const;
	void EmitFireBurst(const FSimCopterEffectSlot& Source);
};
