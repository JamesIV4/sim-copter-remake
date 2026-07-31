// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FSimCopterPeopleSpawnPlacement
{
	int32 PlacementMode = 0;
	int32 SurfaceMode = 4;
};

struct FSimCopterPeopleLocalOffset
{
	int32 OriginalX = 0;
	int32 OriginalY = 0;
};

class SIMCOPTERREMAKE_API FSimCopterPeopleCityRules
{
public:
	// FUN_004c9220: XBLD building id -> DAT_0058e800 people tile class.
	static int32 GetTileClassForBuildingId(uint8 BuildingId);

	// FUN_004e4f80: XBLD building id -> square scene-cell footprint size.
	static int32 GetFootprintSizeForBuildingId(uint8 BuildingId);

	// DAT_0058d6d0 rows initialized by FUN_004c3010. The original passes PlacementMode into
	// FUN_004c02a0 when choosing an in-tile spawn point.
	static FSimCopterPeopleSpawnPlacement GetSpawnPlacementForTileClass(int32 TileClass);

	// Tile classes accepted by the original random ambient spawn driver (FUN_004c2450):
	// behavior-class rows in DAT_0058ec00, not the movement allowance table.
	static bool IsAmbientPedestrianTileClass(int32 TileClass);
	static const TArray<int32>& GetAmbientPedestrianTileClasses();

	// DAT_0058ec00: behavior class (+0x146) -> tile classes used by the original random
	// ambient spawner (FUN_004c2450) and VM opcode 20 (FUN_004cb300).
	static const TArray<int32>& GetAmbientStateTileClasses(int32 StateIndex);

	// FUN_004ce9d0/FUN_004cea00: shared people-runtime 16-bit PRNG.
	static uint16 NextPeopleRandomRaw(uint16& RandomState);
	static uint16 NextPeopleRandomBounded(uint16& RandomState, uint16 Bound);

	// FUN_004c7190/FUN_004c7170 candidate class selection, used by FUN_004c2450.
	static int32 ChooseAmbientBehaviorClassForTileClass(int32 TileClass, uint16& PeopleRandomState);

	// FUN_004c71c0: behavior class (person+0x146) -> privanim figure, applied at spawn.
	// Class 10 is the dog (2DOGG), 17 the cow (Coww), 20 Elvis, 16 Nessie.
	static FString GetFigureNameForBehaviorClass(int32 BehaviorClass);

	// FUN_004c71c0's `local_4`, written to person+0x18e whenever that field is still -1: which
	// head this class wears. It indexes both DAT_0058f0e0 (the SIM3D.BMP head panorama the 3D
	// figure gets) and people1.bmp's portrait columns, where column 0 is the empty seat and the
	// portrait for head H is column H + 1 (FUN_00453f70).
	static int32 GetHeadImageIndexForBehaviorClass(int32 BehaviorClass);

	// FUN_004c7090: setting a person to state 6 - the medevac victim - overwrites person+0x18e
	// with 10, which is the bandaged head. No behavior class is assigned it, so this is the only
	// way to get one; picking heads at random is what put bandages on healthy pedestrians.
	static constexpr int32 MedevacVictimHeadImageIndex = 10;

	// FUN_004c71c0's `uVar9`, written to person+0x178: the per-class offset FUN_004c5210 hands to
	// AddFrequency, i.e. how high or low this class speaks.
	static int32 GetVoicePitchDeltaForBehaviorClass(int32 BehaviorClass);

	// FUN_004c71c0's `uVar3`, written to person+0x18c: the person's own looping voice event -
	// 0x0e/0x28/0x29 are the three footstep clips, and 0x2f..0x36 are the Elvis noises the dog
	// (1 in 200), the cow (1 in 200), Nessie, Elvis and - through the tail roll - anyone at all
	// (1 in DAT_0058dc3a = 65000) can end up with instead.
	static int32 ChooseVoiceSetForBehaviorClass(int32 BehaviorClass, uint16& PeopleRandomState);

	// FUN_004c02a0's local offset sampler, before collision/height rejection. The original
	// coordinates are signed 16.16 tile-space units where one SC2 tile is 0x40 units wide.
	static FSimCopterPeopleLocalOffset ChooseSpawnLocalOffset(
		int32 FootprintSize,
		int32 PlacementMode,
		uint16& PeopleRandomState);
};
