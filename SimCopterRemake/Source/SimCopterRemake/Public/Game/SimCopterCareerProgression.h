// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// SCHOOK: CareerTableInit 0x00408370
//
// The half of the career city table that is *not* in career.twk.
//
// FUN_00408370 fills 30 records of 0x50 bytes at 0x518dc8 at startup. career.twk owns the tuning
// half - difficulty, the seven mission weights, day/night, points needed, money earned, all of
// which FSimCopterCareerCity already carries - and the executable owns everything below: the
// career ladder's level number and the branching successor graph the city-select screen offers.
//
//   +0x00        career level 0..11        -> STRINGTABLE 290..301, "Level 1".."Final Level"
//   +0x28..+0x30 successor trio, -1 = fewer
//   +0x34..+0x3c a second trio, all -1 in the shipped table, so it is not modelled here
//   +0x40        the city's own index
//   +0x44        map base name, "city0".."city29"
//
// The career is therefore not a walk from 0 to 29: it is a graph, three cities wide at first and
// narrowing to one at the end. Rebuilt by Docs/scratchpad/parse_career_table.py; the full decode
// is in Docs/scratchpad/mainmenu-DECODED.md.
namespace SimCopterCareerProgression
{
constexpr int32 CityCount = 30;

// How many cities a single record can offer as successors, and how many panels the city-select
// screen therefore has (FUN_00457c90 lays out exactly three).
constexpr int32 MaxSuccessors = 3;

// The deepest level in the ladder; 11 is "Final Level".
constexpr int32 LevelCount = 12;

struct FEntry
{
	int32 Level = 0;
	int32 Successors[MaxSuccessors] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
};

// City 0..29, or null when the index is out of range.
SIMCOPTERREMAKE_API const FEntry* GetEntry(int32 CityIndex);

// Career level of a city, or 0 when the index is out of range.
SIMCOPTERREMAKE_API int32 GetLevel(int32 CityIndex);

// The successors of CityIndex with the -1 slots dropped, so the caller gets 0..3 real cities.
// This is what FUN_00411ca0 hands the city-select screen when a career is already running.
SIMCOPTERREMAKE_API void GetSuccessors(int32 CityIndex, TArray<int32>& OutCities);

// FUN_00457c90's null-trio branch: a brand new career always offers cities 0, 1 and 2.
SIMCOPTERREMAKE_API void GetNewCareerChoices(TArray<int32>& OutCities);

// STRINGTABLE 240 + index, English. "" when the index is out of range.
SIMCOPTERREMAKE_API const TCHAR* GetCityName(int32 CityIndex);

// STRINGTABLE 290 + level, English: "Level 1".."Level 11", then "Final Level".
SIMCOPTERREMAKE_API const TCHAR* GetLevelName(int32 Level);

// FUN_00407f30 copies record +0x44 and appends ".sc2"; the file lives under cities\career\.
SIMCOPTERREMAKE_API FString GetMapBaseName(int32 CityIndex);
}
