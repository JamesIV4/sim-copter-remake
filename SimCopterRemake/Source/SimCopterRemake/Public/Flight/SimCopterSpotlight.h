// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SimCopterSpotlight.generated.h"

// The original spotlight is gameplay, not illumination: it ray-marches the aim vector,
// smooths the hit distance, picks a range band, and broadcasts a three-ring interaction
// scan around the tile it lands on. The megaphone and (later) emergency dispatch both
// consume that target.
//
// Ported from (Docs/scratchpad/ghidra/heli_tools_models_decode_20260724.md section 4):
//   FUN_00489250  march, smoothing, band selection, node placement, FUN_0048ae70(1, ...)
//   FUN_00489630  per-step object intersection test
//   FUN_00489730  aim accumulation and clamping, direction rebuild
//   FUN_00479060  input actions 0x2e..0x31 -> aim deltas
//
// The "rotor downwash disc" label previously attached to FUN_00489250 was wrong; see the
// correction note in out_effects_DECODED.md.

class AActor;

namespace SimCopterSpotlight
{
// FUN_00489250: 16 steps of 0x200000 (32.0 units), so the beam reaches 512 units.
constexpr int32 MarchStep1616 = 0x200000;
constexpr int32 MaxMarchSteps = 16;

// Accumulated distance is clamped when it exceeds 0x1ffffff.
constexpr int32 MaxDistance1616 = 0x1ff0000;
constexpr int32 DistanceClampThreshold1616 = 0x1ffffff;

// Band thresholds (inclusive upper bounds for bands 0..2).
constexpr int32 Band0Max1616 = 0x800000;  // 128.0
constexpr int32 Band1Max1616 = 0x1000000; // 256.0
constexpr int32 Band2Max1616 = 0x1800000; // 384.0
constexpr int32 BandCount = 4;

// The megaphone only broadcasts while the target is inside band 3 (FUN_0048a800).
constexpr int32 MegaphoneMaxBand = 2;

// FUN_00489730: aim accumulators clamp to +/-0x1f40000 = +/-500.0 tenth-degrees.
constexpr int32 AimClamp1616 = 0x1f40000;

// FUN_00479060: one frame of key input moves the aim 0x280000 = 40.0 tenth-degrees.
constexpr int32 AimStep1616 = 0x280000;

// FUN_00483c20/FUN_00489730 seed the aim matrix with a fixed -0x1680000 X rotation, i.e.
// the rest pose points 36 degrees below the helicopter's forward axis.
constexpr int32 BasePitch1616 = 0x1680000;

// Node scale floor, 0x4ccc = 0.3.
constexpr int32 MinNodeScale1616 = 0x4ccc;

// FUN_00489250: while flying the raw distance is folded in at 1/8 weight.
SIMCOPTERREMAKE_API int32 SmoothDistance(int32 PreviousDistance1616, int32 RawDistance1616, bool bFlying);

// Clamp the accumulated march distance the way the original does before smoothing.
SIMCOPTERREMAKE_API int32 ClampMarchDistance(int32 RawDistance1616);

// 0 = closest, 3 = out of range.
SIMCOPTERREMAKE_API int32 SelectBand(int32 Distance1616);

// Cone scale: max(0x4ccc, (distance / 512.0) * 10).
SIMCOPTERREMAKE_API int32 SelectNodeScale(int32 Distance1616);

SIMCOPTERREMAKE_API int32 ClampAim(int32 Aim1616);
}

// The shared semantic target produced by the spotlight and consumed by the megaphone and
// any later dispatch port (plan section 4.2 "FHelicopterToolTarget").
USTRUCT(BlueprintType)
struct SIMCOPTERREMAKE_API FSimCopterToolTarget
{
	GENERATED_BODY()

	// False when the march found nothing at all (no terrain under the beam).
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Spotlight")
	bool bValid = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Spotlight")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Spotlight")
	FVector WorldNormal = FVector::UpVector;

	// City tile the beam lands on; the interaction scan is centred here, not on the pawn.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Spotlight")
	FIntPoint Tile = FIntPoint(INDEX_NONE, INDEX_NONE);

	// Smoothed hit distance in original world units.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Spotlight")
	float DistanceUnits = 0.0f;

	// heli[0x150]: 0..3.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Spotlight")
	int32 Band = SimCopterSpotlight::BandCount - 1;

	// Rings the mode-1 spotlight scan uses around Tile.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Spotlight")
	int32 InteractionRings = 3;

	// Actor the beam struck, when it was not plain terrain.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Spotlight")
	TWeakObjectPtr<AActor> HitActor;

	bool HasTile() const { return Tile.X >= 0 && Tile.Y >= 0; }
};
