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

	// Emit one particle. World = spawn point; VelocityCmPerSec = initial velocity (all axes);
	// SizeCm = sprite half-extent; Color includes the starting alpha (faded out over life);
	// GravityCmPerSec2 pulls the Z velocity down each frame (the original subtracts 0x280000*dt
	// from every particle's Z velocity - see SimCopterEffectFX::GravityCmPerSec2).
	void SpawnParticle(const FVector& World, const FVector& VelocityCmPerSec, float SizeCm,
		const FLinearColor& Color, float LifeSeconds, float GravityCmPerSec2 = 0.0f);

	// Emit a radial ring of particles (the original splash breaks a column into a ring of smaller
	// type-9 sub-particles - "smaller versions of the water effect"). Count particles spread over
	// the horizontal plane with SpeedCmPerSec, given an initial +Z velocity and gravity.
	void SpawnRing(const FVector& World, int32 Count, float SpeedCmPerSec, float InitialRiseCmPerSec,
		float SizeCm, const FLinearColor& Color, float LifeSeconds, float GravityCmPerSec2 = 0.0f);

	bool HasActiveParticles() const { return Particles.Num() > 0; }

protected:
	virtual void OnRegister() override;

private:
	struct FCard
	{
		FVector Position = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		float Gravity = 0.0f; // cm/s^2 applied to Velocity.Z each frame
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
