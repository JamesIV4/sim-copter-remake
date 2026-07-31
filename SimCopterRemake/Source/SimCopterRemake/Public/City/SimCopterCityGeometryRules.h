// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FSimCity2000City;

// Pure city-assembly rules shared by the renderer and its automation tests.
class SIMCOPTERREMAKE_API FSimCopterCityGeometryRules
{
public:
	// SCHOOK: FUN_0047c0c0 0x0047c0c0
	// Claims the square scene-cell footprint for an XBLD building at this raster position.
	// The original ignores XZON here: FUN_004e4f80 supplies the size, every cell in that
	// square must carry the same XBLD id, and the first row-major claim owns the placement.
	// A zero result means this tile is already covered or does not begin a valid building.
	static FIntPoint ClaimOriginalBuildingFootprint(
		const FSimCity2000City& City,
		int32 FileX,
		int32 FileY,
		TArray<uint8>& SceneCellState);
};
