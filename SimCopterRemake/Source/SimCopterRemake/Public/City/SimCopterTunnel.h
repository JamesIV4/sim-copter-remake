#pragma once

#include "CoreMinimal.h"

namespace SimCopterTunnel
{
	// FUN_0047c0c0 selects TL63..TL66 (objects 0x178..0x17b). These are hollow
	// tunnel portals, not ramps. Their open ends follow the same axes as the road markings.
	inline bool IsPortal(uint8 Id) { return Id >= 0x3f && Id <= 0x42; }
	inline float TravelSeconds(int32 TileDistance) { return FMath::Max(0, TileDistance) / 3.0f; }
	inline bool IsNorthSouth(uint8 Id) { return Id == 0x3f || Id == 0x41; }
	inline bool IsRoad(uint8 Id)
	{
		return (Id >= 0x1d && Id <= 0x2b) || (Id >= 0x3f && Id <= 0x46) ||
			(Id >= 0x49 && Id <= 0x59) || (Id >= 0x5d && Id <= 0x6b);
	}
	inline FIntPoint EntranceOffset(uint8 Id, uint8 NegativeNeighbor, uint8 PositiveNeighbor,
		float NegativeHeight, float PositiveHeight)
	{
		const bool NegativeRoad = IsRoad(NegativeNeighbor) && !IsPortal(NegativeNeighbor);
		const bool PositiveRoad = IsRoad(PositiveNeighbor) && !IsPortal(PositiveNeighbor);
		const int32 Sign = NegativeRoad != PositiveRoad ? (NegativeRoad ? -1 : 1)
			: (NegativeHeight <= PositiveHeight ? -1 : 1);
		return IsNorthSouth(Id) ? FIntPoint(0, Sign) : FIntPoint(Sign, 0);
	}
	inline FVector InwardDirection(const FIntPoint& Entrance)
	{
		return FVector(-Entrance.X, Entrance.Y, 0.0f); // file Y is negated in city space
	}
	inline void ApplyTerrainConstraints(TArray<int16>& TerrainCorners, TArray<float>& RoofCorners,
		int32 GridSize, const FIntPoint& Portal, const FIntPoint& MouthOffset,
		float RoofZ, bool bFlattenEntrance, int16 EntranceHeight)
	{
		for (int32 DY = 0; DY <= 1; ++DY)
		for (int32 DX = 0; DX <= 1; ++DX)
		{
			const bool bRear = MouthOffset.X != 0 ? DX == (MouthOffset.X > 0 ? 0 : 1)
				: DY == (MouthOffset.Y > 0 ? 0 : 1);
			const int32 Index = (Portal.Y + DY) * GridSize + Portal.X + DX;
			if (bRear && RoofCorners.IsValidIndex(Index)) RoofCorners[Index] = RoofZ;
		}
		const FIntPoint Entrance = Portal + MouthOffset;
		if (!bFlattenEntrance || Entrance.X < 0 || Entrance.Y < 0 ||
			Entrance.X >= GridSize - 1 || Entrance.Y >= GridSize - 1) return;
		// Flatten the whole approach tile, including shared corners on its outer sides.
		// The front portal corners receive no roof-height clamp.
		for (int32 DY = 0; DY <= 1; ++DY)
		for (int32 DX = 0; DX <= 1; ++DX)
		{
			const int32 Index = (Entrance.Y + DY) * GridSize + Entrance.X + DX;
			if (TerrainCorners.IsValidIndex(Index)) TerrainCorners[Index] = EntranceHeight;
		}
	}
	inline bool IsBehindCap(const FVector& FromCenter, const FVector& Inward, float TileSize, float BodyRadius)
	{
		return FVector::DotProduct(FromCenter, Inward) >= TileSize * 0.5f + BodyRadius;
	}
}
