// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FSimCopterPeopleSpawnPlacement
{
	int32 PlacementMode = 0;
	int32 SurfaceMode = 4;
};

class SIMCOPTERREMAKE_API FSimCopterPeopleCityRules
{
public:
	// FUN_004c9220: XBLD building id -> DAT_0058e800 people tile class.
	static int32 GetTileClassForBuildingId(uint8 BuildingId);

	// DAT_0058d6d0 rows initialized by FUN_004c3010. The original passes PlacementMode into
	// FUN_004c02a0 when choosing an in-tile spawn point.
	static FSimCopterPeopleSpawnPlacement GetSpawnPlacementForTileClass(int32 TileClass);

	// Initial ambient pedestrian state 0 uses DAT_0058d750's default row:
	// {13, 11, 10, 12, 7}.
	static bool IsAmbientPedestrianTileClass(int32 TileClass);
	static const TArray<int32>& GetAmbientPedestrianTileClasses();

	// DAT_0058ec00: candidate state -> tile classes used by the original random ambient
	// spawner (FUN_004c2450). Kept here so the state-selection port can reuse the decoded table.
	static const TArray<int32>& GetAmbientStateTileClasses(int32 StateIndex);
};
