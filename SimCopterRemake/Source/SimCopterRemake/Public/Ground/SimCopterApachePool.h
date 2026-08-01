// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Formats/MaxisProceduralMeshBuilder.h"
#include "SimCopterApachePool.generated.h"

class ASimCity2000CityActor;
class ASimCopterHelicopterPawn;
class UMaterialInterface;
class UProceduralMeshComponent;
class USimCopterParticleFXComponent;

// The Apache's two weapon pools, ported from the same functions as the tear gas launcher:
//
//   FUN_0048db20  builds them: DAT_005d4900 = 10 missiles (GEO 0x0ae, collision radius 6.0),
//                 DAT_005d4f30 = 70 machine-gun tracers (a 3-point 0x17 trajectory card,
//                 palette cycling 16..31, "radius" 0x140000)
//   FUN_0048e0b0  type 1 arms the shared 1 s cooldown and plays MISSILE; type 2 has no cooldown
//                 at all and starts MACHGUN1 as a loop
//   FUN_0048ed00  moves them: unlike the tear gas and debris pools, BOTH fly at a constant speed
//                 with no drag and no gravity
//   FUN_00490690  on impact: mode 3 for the missile (flags 0x802), mode 7 for a bullet (0x4004),
//                 and both are in the 0x4006 despawn set - they break rather than bounce
//
// The missile is the only projectile in the game that starts fires: a terrain hit on a burnable
// tile opens a fire mission and throws a debris burst.
UCLASS(ClassGroup = (SimCopter), meta = (BlueprintSpawnableComponent))
class SIMCOPTERREMAKE_API USimCopterApachePoolComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	USimCopterApachePoolComponent();

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	void SetEffectComponent(USimCopterParticleFXComponent* InEffects);
	void SetOriginalGameRoot(const FString& InRoot) { OriginalGameRoot = InRoot; }

	// SCHOOK: ApacheEmitterSpawn 0x0048e0b0
	// ForwardSpeed1616 is heli[0x4e]; the per-weapon bonus is added here. False when the pool is
	// full, which is the original's refusal.
	bool LaunchMissile(const FVector& World, const FVector& Direction, int32 ForwardSpeed1616);
	bool LaunchBullet(const FVector& World, const FVector& Direction, int32 ForwardSpeed1616);

	int32 GetActiveMissileCount() const;
	int32 GetActiveBulletCount() const;
	void ClearAll();

protected:
	virtual void OnRegister() override;

private:
	enum class EKind : uint8
	{
		Missile,
		Bullet,
	};

	struct FSlot
	{
		bool bActive = false;
		EKind Kind = EKind::Missile;
		FVector Position = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
		float SpeedCmPerSec = 0.0f;
		int32 Life1616 = 0;
		int32 MeshIndex = INDEX_NONE;
	};

	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> MissileMeshes;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> VertexColorMaterial;

	UPROPERTY(Transient)
	TObjectPtr<USimCopterParticleFXComponent> Effects;

	TWeakObjectPtr<ASimCity2000CityActor> CachedCityActor;

	TArray<FSlot> Slots;
	FString OriginalGameRoot;
	FString LastMeshError;
	FMaxisMeshSection MissileSection;
	bool bMeshBuildAttempted = false;
	int32 StepAccumulator1616 = 0;
	// DAT_00504558, the tracer's cycling palette index (16..31).
	uint8 TracerPaletteCursor = 0x10;

	ASimCity2000CityActor* ResolveCityActor();
	ASimCopterHelicopterPawn* GetHelicopter() const;
	UProceduralMeshComponent* EnsureMissileMesh(int32 SlotIndex);

	bool Launch(EKind Kind, const FVector& World, const FVector& Direction, int32 ForwardSpeed1616);
	void AdvanceSlot(int32 SlotIndex, int32 Delta1616);
	// True when the projectile is spent and the slot should be released.
	bool ResolveImpact(FSlot& Slot, const FVector& Start, const FVector& End);
	void Detonate(const FSlot& Slot, const FVector& World, const FVector& Normal, bool bHitTerrain);
	void ReleaseSlot(int32 SlotIndex);
};
