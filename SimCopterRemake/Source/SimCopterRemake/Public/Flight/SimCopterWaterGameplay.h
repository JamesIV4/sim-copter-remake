// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Fixed-point water-gameplay rules shared by the helicopter, particle updater, and automation
// tests. These values stay in the original executable's 16.16 units until the Unreal integration
// boundary so particle paths and impact strengths can be checked directly against the decompile.
namespace SimCopterWaterGameplay
{
	static constexpr int32 FixedOne = 0x10000;
	static constexpr int32 InitialParticleLife1616 = 0x50000;
	static constexpr int32 Gravity1616 = 0x280000;
	static constexpr int32 CannonDrag1616 = 0x51e;
	static constexpr int32 BucketDrag1616 = 0x28f;
	static constexpr int32 FillPoundsPerFrame = 30;
	static constexpr int32 DumpPoundsPerFrame = 21;
	static constexpr int32 CannonPoundsPerFrame = DumpPoundsPerFrame >> 1;
	static constexpr int32 FillSurfaceTolerance1616 = 0x20000;
	static constexpr int32 BucketEmissionSpeed1616 = 0xf0000;
	static constexpr int32 BucketEmissionOffset1616 = 0x80000;
	static constexpr int32 RopeNodeCount = 20;
	static constexpr int32 RopeMinimumFirstActiveNode = 3;
	static constexpr int32 RopeStowedFirstActiveNode = 17;
	static constexpr int32 RopeSegmentLength1616 = 0x40000;

	enum class EWaterEmitter : uint8
	{
		Cannon,
		Bucket,
	};

	struct SIMCOPTERREMAKE_API FWaterParticleMotion
	{
		FIntVector Direction1616 = FIntVector(0, 0, -FixedOne);
		int32 Speed1616 = 0;
		int32 Life1616 = InitialParticleLife1616;
	};

	struct SIMCOPTERREMAKE_API FWaterParticleFrame
	{
		FIntVector Travel1616 = FIntVector::ZeroValue;
		bool bAlive = true;
	};

	struct SIMCOPTERREMAKE_API FWaterImpact
	{
		bool bDouse = false;
		int32 DouseStrength1616 = 0;
		uint8 PuffClass = 8;
		uint8 SoundId = 0;
	};

	SIMCOPTERREMAKE_API int32 FixedMul(int32 A, int32 B);
	SIMCOPTERREMAKE_API int32 FixedDiv(int32 Numerator, int32 Denominator);
	SIMCOPTERREMAKE_API FIntVector Normalize1616(const FIntVector& Value, int32& OutLength1616);
	SIMCOPTERREMAKE_API FIntVector DirectionToFixed(const FVector& Direction);
	SIMCOPTERREMAKE_API FVector DirectionToFloat(const FIntVector& Direction1616);

	// SCHOOK: WaterParticleUpdate 0x0048ed00
	SIMCOPTERREMAKE_API FWaterParticleFrame AdvanceParticleFrame(
		FWaterParticleMotion& InOutMotion,
		EWaterEmitter Emitter,
		int32 Delta1616);

	// SCHOOK: WaterParticleImpact 0x00490690
	SIMCOPTERREMAKE_API FWaterImpact ResolveImpact(
		EWaterEmitter Emitter,
		int32 RemainingLife1616,
		bool bLandedInWater);

	SIMCOPTERREMAKE_API bool IsWaterTerrainClass(uint8 TerrainClass);

	// SCHOOK: BucketFill 0x00487bb0
	SIMCOPTERREMAKE_API bool CanFillBucket(
		bool bAttachmentActive,
		int32 BucketHeight1616,
		int32 SurfaceHeight1616,
		uint8 TerrainClass);
	SIMCOPTERREMAKE_API int32 FillBucketFrame(int32 WaterPounds, int32 MaxLoadPounds, int32 FillRatePounds);

	// SCHOOK: BucketDump 0x00488060
	SIMCOPTERREMAKE_API int32 DumpBucketFrame(int32 WaterPounds, int32 DumpRatePounds);

	// Matches the V0/V1/V2 and V0/V2/V3 diagonal used by the rendered terrain quad and
	// FUN_004ae7a0, rather than smoothing across that diagonal with a bilinear approximation.
	SIMCOPTERREMAKE_API float SampleTerrainTriangleHeight(
		float LocalX,
		float LocalY,
		float Z00,
		float Z10,
		float Z11,
		float Z01);
}
