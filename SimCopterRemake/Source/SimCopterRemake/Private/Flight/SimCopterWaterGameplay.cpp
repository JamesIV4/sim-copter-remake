// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/SimCopterWaterGameplay.h"

namespace SimCopterWaterGameplay
{
	int32 FixedMul(const int32 A, const int32 B)
	{
		return static_cast<int32>((static_cast<int64>(A) * static_cast<int64>(B)) >> 16);
	}

	int32 FixedDiv(const int32 Numerator, const int32 Denominator)
	{
		if (Denominator == 0)
		{
			return 0;
		}
		return static_cast<int32>((static_cast<int64>(Numerator) << 16) / Denominator);
	}

	FIntVector Normalize1616(const FIntVector& Value, int32& OutLength1616)
	{
		const double LengthSquared =
			static_cast<double>(Value.X) * static_cast<double>(Value.X) +
			static_cast<double>(Value.Y) * static_cast<double>(Value.Y) +
			static_cast<double>(Value.Z) * static_cast<double>(Value.Z);
		OutLength1616 = FMath::RoundToInt(FMath::Sqrt(LengthSquared));
		if (OutLength1616 <= 0)
		{
			OutLength1616 = 0;
			return FIntVector::ZeroValue;
		}
		return FIntVector(
			FixedDiv(Value.X, OutLength1616),
			FixedDiv(Value.Y, OutLength1616),
			FixedDiv(Value.Z, OutLength1616));
	}

	FIntVector DirectionToFixed(const FVector& Direction)
	{
		const FVector Normalized = Direction.GetSafeNormal();
		return FIntVector(
			FMath::RoundToInt(Normalized.X * static_cast<float>(FixedOne)),
			FMath::RoundToInt(Normalized.Y * static_cast<float>(FixedOne)),
			FMath::RoundToInt(Normalized.Z * static_cast<float>(FixedOne)));
	}

	FVector DirectionToFloat(const FIntVector& Direction1616)
	{
		return FVector(
			static_cast<float>(Direction1616.X) / static_cast<float>(FixedOne),
			static_cast<float>(Direction1616.Y) / static_cast<float>(FixedOne),
			static_cast<float>(Direction1616.Z) / static_cast<float>(FixedOne)).GetSafeNormal();
	}

	FWaterParticleFrame AdvanceParticleFrame(
		FWaterParticleMotion& InOutMotion,
		const EWaterEmitter Emitter,
		const int32 Delta1616)
	{
		FWaterParticleFrame Result;
		InOutMotion.Life1616 -= Delta1616;
		if (InOutMotion.Life1616 <= 0)
		{
			InOutMotion.Life1616 = 0;
			Result.bAlive = false;
			return Result;
		}

		const int32 Drag1616 = Emitter == EWaterEmitter::Cannon ? CannonDrag1616 : BucketDrag1616;
		InOutMotion.Speed1616 = FMath::Max(
			0,
			InOutMotion.Speed1616 - FixedMul(Drag1616, InOutMotion.Speed1616));

		FIntVector Velocity1616(
			FixedMul(InOutMotion.Direction1616.X, InOutMotion.Speed1616),
			FixedMul(InOutMotion.Direction1616.Y, InOutMotion.Speed1616),
			FixedMul(InOutMotion.Direction1616.Z, InOutMotion.Speed1616));
		Velocity1616.Z -= FixedMul(Gravity1616, Delta1616);

		InOutMotion.Direction1616 = Normalize1616(Velocity1616, InOutMotion.Speed1616);
		const int32 Distance1616 = FixedMul(InOutMotion.Speed1616, Delta1616);
		Result.Travel1616 = FIntVector(
			FixedMul(InOutMotion.Direction1616.X, Distance1616),
			FixedMul(InOutMotion.Direction1616.Y, Distance1616),
			FixedMul(InOutMotion.Direction1616.Z, Distance1616));
		return Result;
	}

	FWaterImpact ResolveImpact(
		const EWaterEmitter Emitter,
		const int32 RemainingLife1616,
		const bool bLandedInWater)
	{
		FWaterImpact Result;
		if (bLandedInWater)
		{
			Result.PuffClass = 8;
			Result.SoundId = 0x0f;
			return Result;
		}

		Result.bDouse = true;
		Result.PuffClass = RemainingLife1616 < 0x20000 ? 8 : 9;
		Result.SoundId = 10;
		Result.DouseStrength1616 = Emitter == EWaterEmitter::Cannon
			? FMath::Max(0, RemainingLife1616)
			: FMath::Max(0, RemainingLife1616) >> 2;
		return Result;
	}

	FFireTruckJetLaunch AdvanceFireTruckJet(
		FFireTruckJetSweep& InOutSweep,
		const int32 ElevationMax1616,
		const FIntVector& UnitAim1616,
		const int32 Distance1616,
		const int32 SpeedRoll)
	{
		InOutSweep.Elevation1616 += InOutSweep.Step1616;
		if (InOutSweep.Elevation1616 > ElevationMax1616)
		{
			InOutSweep.Elevation1616 = ElevationMax1616;
			InOutSweep.Step1616 = -FireTruckElevationStep1616;
		}
		else if (InOutSweep.Elevation1616 < 0)
		{
			InOutSweep.Elevation1616 = 0;
			InOutSweep.Step1616 = FireTruckElevationStep1616;
		}

		FFireTruckJetLaunch Result;
		Result.Elevation1616 = InOutSweep.Elevation1616;

		// The original keeps the aim's X and Z (its horizontal pair) and overwrites Y, which is
		// its up axis. Unreal's up axis is Z, so the vertical component swaps places.
		int32 Length1616 = 0;
		Result.Direction1616 = Normalize1616(
			FIntVector(UnitAim1616.X, UnitAim1616.Y, InOutSweep.Elevation1616),
			Length1616);

		// (rand() % 100) * 0x10000 + distance / 2
		Result.Speed1616 = SpeedRoll * FixedOne + Distance1616 / 2;
		return Result;
	}

	bool IsWaterTerrainClass(const uint8 TerrainClass)
	{
		return TerrainClass < 10;
	}

	bool CanFillBucket(
		const bool bAttachmentActive,
		const int32 BucketHeight1616,
		const int32 SurfaceHeight1616,
		const uint8 TerrainClass)
	{
		return bAttachmentActive &&
			BucketHeight1616 <= SurfaceHeight1616 + FillSurfaceTolerance1616 &&
			IsWaterTerrainClass(TerrainClass);
	}

	int32 FillBucketFrame(const int32 WaterPounds, const int32 MaxLoadPounds, const int32 FillRatePounds)
	{
		return FMath::Clamp(WaterPounds + FMath::Max(0, FillRatePounds), 0, FMath::Max(0, MaxLoadPounds));
	}

	int32 DumpBucketFrame(const int32 WaterPounds, const int32 DumpRatePounds)
	{
		return FMath::Max(0, WaterPounds - FMath::Max(0, DumpRatePounds));
	}

	float SampleTerrainTriangleHeight(
		const float LocalX,
		const float LocalY,
		const float Z00,
		const float Z10,
		const float Z11,
		const float Z01)
	{
		const float X = FMath::Clamp(LocalX, 0.0f, 1.0f);
		const float Y = FMath::Clamp(LocalY, 0.0f, 1.0f);
		if (X >= Y)
		{
			return Z00 + X * (Z10 - Z00) + Y * (Z11 - Z10);
		}
		return Z00 + X * (Z11 - Z01) + Y * (Z01 - Z00);
	}
}
