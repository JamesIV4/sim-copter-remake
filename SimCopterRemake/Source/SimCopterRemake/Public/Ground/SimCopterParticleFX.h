// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "SimCopterParticleFX.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

// Reproduces the original SimCopter effect particles (bucket water drips, douse splash rings and
// the rotor-wash "wind kickback" over water). In the exe these are flat palette-coloured,
// camera-facing cards (Maxis face type 0x17) built procedurally by FUN_0046edb0 and moved by a
// velocity + rise rate over a short lifetime (creator FUN_0048e0b0 / FUN_004af220, updater
// FUN_0048ed00 / FUN_004af3b0). Here each card is a CPU-billboarded, vertex-coloured quad; the
// component advances and rebuilds them every tick.
//
// Units are Unreal centimetres. Callers convert the original 16.16 world units (1/64 tile) with
// the city's units-per-original-unit (tile/64 = 6.25 cm at a 400 cm tile).
UCLASS(ClassGroup = (SimCopter), meta = (BlueprintSpawnableComponent))
class SIMCOPTERREMAKE_API USimCopterParticleFXComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	USimCopterParticleFXComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Emit one card. World = spawn point; VelocityCmPerSec = lateral drift; RiseCmPerSec = extra
	// +Z speed; SizeCm = card half-extent; Color includes the starting alpha (faded out over life).
	void SpawnCard(const FVector& World, const FVector& VelocityCmPerSec, float RiseCmPerSec,
		float SizeCm, const FLinearColor& Color, float LifeSeconds);

	// Emit a radial ring of cards (the original splash breaks a water column into a ring of
	// smaller type-9 particles). Count cards spread over the horizontal plane with SpeedCmPerSec.
	void SpawnRing(const FVector& World, int32 Count, float SpeedCmPerSec, float RiseCmPerSec,
		float SizeCm, const FLinearColor& Color, float LifeSeconds);

	bool HasActiveParticles() const { return Particles.Num() > 0; }

protected:
	virtual void OnRegister() override;

private:
	struct FCard
	{
		FVector Position = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		float Rise = 0.0f;
		float Size = 0.0f;
		float Age = 0.0f;
		float Life = 1.0f;
		FLinearColor Color = FLinearColor::White;
	};

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> MeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> CardMaterial;

	// Optional soft-edged material override (M_SimCopterParticleFXSoft) for a non-dithered look.
	// Left unset the default is the authentic ordered-dither M_SimCopterParticleFX.
	UPROPERTY(EditAnywhere, Category = "SimCopter|FX")
	TObjectPtr<UMaterialInterface> CardMaterialOverride;

	TArray<FCard> Particles;

	// Cap so a stuck emitter can't grow without bound. The original wash/spray are dense clouds of
	// many small dithered specks, so this is generous.
	static constexpr int32 MaxParticles = 4000;

	FVector GetCameraLocation() const;
	void RebuildMesh(const FVector& CameraLocation);
};
