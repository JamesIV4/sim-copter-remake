// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/SimCopterCareerProgression.h"

namespace SimCopterCareerProgression
{
namespace
{
// FUN_00408370, record +0x00 and +0x28..+0x30. The stores are emitted in whatever order the
// compiler chose, so this was recovered by address rather than read off the listing top to bottom.
constexpr FEntry Entries[CityCount] = {
	/*  0 */ {  0, {  3,  4,  1 } },
	/*  1 */ {  0, {  3,  4,  5 } },
	/*  2 */ {  0, {  1,  4,  5 } },
	/*  3 */ {  1, {  4,  6,  7 } },
	/*  4 */ {  1, {  6,  7,  8 } },
	/*  5 */ {  1, {  4,  7,  8 } },
	/*  6 */ {  2, {  7,  9, 10 } },
	/*  7 */ {  2, {  9, 10, 11 } },
	/*  8 */ {  2, {  7, 10, 11 } },
	/*  9 */ {  3, { 10, 12, 13 } },
	/* 10 */ {  3, { 12, 13, 14 } },
	/* 11 */ {  3, { 10, 13, 14 } },
	/* 12 */ {  4, { 13, 15, 16 } },
	/* 13 */ {  4, { 15, 16, 17 } },
	/* 14 */ {  4, { 13, 16, 17 } },
	/* 15 */ {  5, { 16, 18, 19 } },
	/* 16 */ {  5, { 18, 19, 20 } },
	/* 17 */ {  5, { 16, 19, 20 } },
	// From level 6 the ladder narrows: two successors, then one, then none.
	/* 18 */ {  6, { 19, 21, INDEX_NONE } },
	/* 19 */ {  6, { 21, 22, INDEX_NONE } },
	/* 20 */ {  6, { 19, 22, INDEX_NONE } },
	/* 21 */ {  7, { 23, 24, INDEX_NONE } },
	/* 22 */ {  7, { 23, 24, INDEX_NONE } },
	/* 23 */ {  8, { 25, 26, INDEX_NONE } },
	/* 24 */ {  8, { 25, 26, INDEX_NONE } },
	/* 25 */ {  9, { 27, 28, INDEX_NONE } },
	/* 26 */ {  9, { 27, 28, INDEX_NONE } },
	/* 27 */ { 10, { 29, INDEX_NONE, INDEX_NONE } },
	/* 28 */ { 10, { 29, INDEX_NONE, INDEX_NONE } },
	/* 29 */ { 11, { INDEX_NONE, INDEX_NONE, INDEX_NONE } },
};

// STRINGTABLE 240..269, English (language 1033). Resources, not .rdata - the same place the
// hangar shell and the Check-up dialog take their text from - and the remake has no resource
// reader, so the English block is inlined here with the id that owns it.
const TCHAR* const CityNames[CityCount] = {
	TEXT("Sea Cliff"),     TEXT("Islandtown"),  TEXT("Diabloville"),   TEXT("CatNip Cove"),
	TEXT("Cypress"),       TEXT("Berkeley"),    TEXT("Treeton"),       TEXT("Keithly"),
	TEXT("Circlopolis"),   TEXT("River Rail"),  TEXT("Cumberland"),    TEXT("Scotville"),
	TEXT("Kentown"),       TEXT("Tigger Town"), TEXT("Terraton"),      TEXT("Happyland"),
	TEXT("Roseland"),      TEXT("Waterton"),    TEXT("Myrtle Dam"),    TEXT("River Valley"),
	TEXT("Canyon"),        TEXT("Hidden Valley"), TEXT("Cancer"),      TEXT("Calebopolis"),
	TEXT("Valley"),        TEXT("Whattheheck"), TEXT("Four Cities"),   TEXT("Toronto"),
	TEXT("Conville"),      TEXT("Metropolis"),
};

// STRINGTABLE 290..301.
const TCHAR* const LevelNames[LevelCount] = {
	TEXT("Level 1"), TEXT("Level 2"),  TEXT("Level 3"),  TEXT("Level 4"),
	TEXT("Level 5"), TEXT("Level 6"),  TEXT("Level 7"),  TEXT("Level 8"),
	TEXT("Level 9"), TEXT("Level 10"), TEXT("Level 11"), TEXT("Final Level"),
};
}

const FEntry* GetEntry(const int32 CityIndex)
{
	return (CityIndex >= 0 && CityIndex < CityCount) ? &Entries[CityIndex] : nullptr;
}

int32 GetLevel(const int32 CityIndex)
{
	const FEntry* Entry = GetEntry(CityIndex);
	return Entry != nullptr ? Entry->Level : 0;
}

void GetSuccessors(const int32 CityIndex, TArray<int32>& OutCities)
{
	OutCities.Reset();

	const FEntry* Entry = GetEntry(CityIndex);
	if (Entry == nullptr)
	{
		return;
	}

	for (const int32 Successor : Entry->Successors)
	{
		if (Successor >= 0 && Successor < CityCount)
		{
			OutCities.Add(Successor);
		}
	}
}

void GetNewCareerChoices(TArray<int32>& OutCities)
{
	OutCities.Reset();
	OutCities.Add(0);
	OutCities.Add(1);
	OutCities.Add(2);
}

const TCHAR* GetCityName(const int32 CityIndex)
{
	return (CityIndex >= 0 && CityIndex < CityCount) ? CityNames[CityIndex] : TEXT("");
}

const TCHAR* GetLevelName(const int32 Level)
{
	return (Level >= 0 && Level < LevelCount) ? LevelNames[Level] : TEXT("");
}

FString GetMapBaseName(const int32 CityIndex)
{
	return FString::Printf(TEXT("city%d"), FMath::Clamp(CityIndex, 0, CityCount - 1));
}
}
