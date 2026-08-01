// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Flight/SimCopterTearGas.h"
#include "Formats/MaxisProceduralMeshBuilder.h"
#include "SimCopterTearGasPool.generated.h"

class ASimCity2000CityActor;
class ASimCopterHelicopterPawn;
class UMaterialInterface;
class UProceduralMeshComponent;
class USimCopterParticleFXComponent;

// The ten-slot tear gas emitter pool (DAT_005d4bd0), ported as one component on the helicopter.
//
// The original keeps the canisters in a global array that the master effect tick walks, so they
// carry on flying, bouncing and gassing after the helicopter has moved on. Keeping them here
// rather than as separate actors preserves that: the slots are world-space and only the pool's
// bookkeeping belongs to the pawn.
//
//   FUN_0048db20  builds the pool: ten slots, TEARGAS (GEO 0x147), collision radius 3.0
//   FUN_0048e0b0  type 3 allocates a free slot, arms the fuse and plays TGSHWH
//   FUN_0048ed00  moves them, bursts them and runs the 30 s cloud
//   FUN_00490690  bounces them off whatever they hit and splashes them on water
UCLASS(ClassGroup = (SimCopter), meta = (BlueprintSpawnableComponent))
class SIMCOPTERREMAKE_API USimCopterTearGasPoolComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	USimCopterTearGasPoolComponent();

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	// Where the cards come from. The pool has no renderer of its own: trail smoke and gas puffs
	// are FUN_004af220 tile puffs, the same ones the bucket and the rotor wash use.
	void SetEffectComponent(USimCopterParticleFXComponent* InEffects);

	// Where the GEO packs live, for the canister mesh.
	void SetOriginalGameRoot(const FString& InRoot) { OriginalGameRoot = InRoot; }

	// SCHOOK: TearGasEmitterSpawn 0x0048e0b0
	// Returns false when every slot is busy, which is the original's "pool full" refusal - the
	// caller must not charge a round for a shot that never left the tube.
	bool Launch(
		const FVector& LaunchWorldLocation,
		const FVector& Direction,
		int32 ForwardSpeed1616,
		int32 MissionEventId);

	// Slots still in the air (fuse burning) and slots that have burst into gas.
	int32 GetActiveCanisterCount() const;
	int32 GetActiveCloudCount() const;

	// Drops everything, for a session boundary the canisters must not cross.
	void ClearAll();

protected:
	virtual void OnRegister() override;

private:
	struct FSlot
	{
		bool bActive = false;
		SimCopterTearGas::FCanisterState State;
		FVector Position = FVector::ZeroVector;
		int32 MissionEventId = INDEX_NONE;
		// Spin is presentation only: the original leaves the node's matrix at identity for the
		// whole flight (FUN_0048e0b0 calls the identity builder for type 3, not the orienting one).
		float SpinDegrees = 0.0f;
	};

	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> CanisterMeshes;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> VertexColorMaterial;

	UPROPERTY(Transient)
	TObjectPtr<USimCopterParticleFXComponent> Effects;

	TWeakObjectPtr<ASimCity2000CityActor> CachedCityActor;

	TArray<FSlot> Slots;
	FString OriginalGameRoot;
	FString LastMeshError;

	// Built once from GEO 0x147 and shared by all ten slots: loading the GEO packs walks every
	// .MAX file, and the first shot is not the moment to do that ten times.
	FMaxisMeshSection CanisterSection;
	bool bMeshBuildAttempted = false;

	// Unspent time carried into the next fixed step. The drag term is per frame, not per second,
	// so the pool has to run on the original's 0.05 s clock however fast the game is rendering.
	int32 StepAccumulator1616 = 0;

	ASimCity2000CityActor* ResolveCityActor();
	ASimCopterHelicopterPawn* GetHelicopter() const;
	UProceduralMeshComponent* EnsureCanisterMesh(int32 SlotIndex);

	void AdvanceSlot(int32 SlotIndex, int32 Delta1616);
	void EmitCloudPuff(FSlot& Slot);
	// The original tests the swept step against the tile's object meshes and then the terrain;
	// the remake sweeps the real collision instead and keeps the same responses: water swallows
	// the canister, anything else turns it around.
	bool ResolveImpact(FSlot& Slot, const FVector& Start, const FVector& End, FVector& OutPosition);
	void ReleaseSlot(int32 SlotIndex);
};
