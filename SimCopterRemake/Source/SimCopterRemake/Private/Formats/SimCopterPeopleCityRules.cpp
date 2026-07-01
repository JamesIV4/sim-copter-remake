// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/SimCopterPeopleCityRules.h"

#include <initializer_list>

namespace
{
bool InRange(uint8 Value, uint8 MinValue, uint8 MaxValue)
{
	return Value >= MinValue && Value <= MaxValue;
}

bool IsOneOf(uint8 Value, std::initializer_list<uint8> Values)
{
	for (const uint8 Candidate : Values)
	{
		if (Value == Candidate)
		{
			return true;
		}
	}
	return false;
}
}

int32 FSimCopterPeopleCityRules::GetTileClassForBuildingId(uint8 BuildingId)
{
	if (BuildingId == 0x00)
	{
		return 2;
	}
	if (InRange(BuildingId, 0x01, 0x04))
	{
		return 4;
	}
	if (InRange(BuildingId, 0x06, 0x0C))
	{
		return 3;
	}
	if (IsOneOf(BuildingId, {0x0D, 0xD5, 0xDA}))
	{
		return 5;
	}
	if (InRange(BuildingId, 0x0E, 0x1C))
	{
		return 6;
	}
	if (InRange(BuildingId, 0x1D, 0x2B) ||
		InRange(BuildingId, 0x43, 0x44) ||
		InRange(BuildingId, 0x49, 0x50) ||
		InRange(BuildingId, 0x61, 0x69))
	{
		return 7;
	}
	if (InRange(BuildingId, 0x51, 0x5A) ||
		InRange(BuildingId, 0x6A, 0x6B))
	{
		return 8;
	}
	if (InRange(BuildingId, 0x2C, 0x3E) ||
		InRange(BuildingId, 0x3F, 0x50) ||
		InRange(BuildingId, 0x45, 0x49) ||
		InRange(BuildingId, 0x5B, 0x60))
	{
		return 9;
	}
	if (InRange(BuildingId, 0x70, 0x7B) ||
		InRange(BuildingId, 0x8C, 0x93) ||
		InRange(BuildingId, 0xAA, 0xB1) ||
		InRange(BuildingId, 0xFB, 0xFF))
	{
		return 10;
	}
	if (InRange(BuildingId, 0xB2, 0xBB) ||
		InRange(BuildingId, 0xD0, 0xD1) ||
		IsOneOf(BuildingId, {0xD9, 0xE1, 0xF1, 0xF3, 0xF7}))
	{
		return 12;
	}
	if (BuildingId == 0x82 ||
		InRange(BuildingId, 0x84, 0x8B) ||
		InRange(BuildingId, 0x9E, 0xA9) ||
		InRange(BuildingId, 0xBC, 0xC5) ||
		InRange(BuildingId, 0xC8, 0xCF) ||
		InRange(BuildingId, 0xE2, 0xEF) ||
		BuildingId == 0xF2 ||
		BuildingId == 0xF4 ||
		InRange(BuildingId, 0xF9, 0xFA))
	{
		return 13;
	}
	if (!(InRange(BuildingId, 0x7C, 0x83) ||
		InRange(BuildingId, 0x94, 0x9D) ||
		InRange(BuildingId, 0xD2, 0xDC) ||
		InRange(BuildingId, 0xE1, 0xE5) ||
		InRange(BuildingId, 0xE8, 0xF5) ||
		BuildingId == 0xF7 ||
		InRange(BuildingId, 0xF9, 0xFA)))
	{
		return 1;
	}
	return 11;
}

FSimCopterPeopleSpawnPlacement FSimCopterPeopleCityRules::GetSpawnPlacementForTileClass(int32 TileClass)
{
	FSimCopterPeopleSpawnPlacement Placement;
	if (TileClass == 2 || TileClass == 3 || TileClass == 4 || TileClass == 5 || TileClass == 7)
	{
		Placement.PlacementMode = 1;
		Placement.SurfaceMode = TileClass == 4 ? 2 : 4;
	}
	return Placement;
}

bool FSimCopterPeopleCityRules::IsAmbientPedestrianTileClass(int32 TileClass)
{
	return GetAmbientPedestrianTileClasses().Contains(TileClass);
}

const TArray<int32>& FSimCopterPeopleCityRules::GetAmbientPedestrianTileClasses()
{
	static const TArray<int32> Classes = {13, 11, 10, 12, 7};
	return Classes;
}

const TArray<int32>& FSimCopterPeopleCityRules::GetAmbientStateTileClasses(int32 StateIndex)
{
	static const TArray<int32> DefaultRow = {12, 13, 11, 10};
	static const TArray<int32> Class13Only = {13};
	static const TArray<int32> ServiceRows = {5, 4, 3};
	static const TArray<int32> Class4Only = {4};

	switch (StateIndex)
	{
	case 6:
		return Class13Only;
	case 10:
	case 17:
		return ServiceRows;
	case 16:
		return Class4Only;
	default:
		return DefaultRow;
	}
}
