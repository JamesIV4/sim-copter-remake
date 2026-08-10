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

int32 RandomOffsetInExtent(int32 Extent, uint16& PeopleRandomState)
{
	if (Extent <= 0)
	{
		return 0;
	}

	const uint16 Bound = uint16(Extent * 2 - 2);
	return Extent - int32(FSimCopterPeopleCityRules::NextPeopleRandomBounded(PeopleRandomState, Bound)) - 1;
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

int32 FSimCopterPeopleCityRules::GetFootprintSizeForBuildingId(uint8 BuildingId)
{
	if ((BuildingId >= 0x49 && BuildingId <= 0x50) || (BuildingId >= 0x61 && BuildingId <= 0x6B))
	{
		return 2;
	}
	if (BuildingId < 0x70)
	{
		return 1;
	}

	// DAT_004fad30[buildingId], read as signed 16-bit by FUN_004e4f80.
	static constexpr int16 BuildingFootprintSizeById70[] = {
		// 0x70..0x7F
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		// 0x80..0x8F
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2,
		// 0x90..0x9F
		2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
		// 0xA0..0xAF
		2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3,
		// 0xB0..0xBF
		3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
		// 0xC0..0xCF
		3, 3, 3, 3, 3, 3, 1, 1, 1, 4, 4, 4, 4, 4, 4, 4,
		// 0xD0..0xDF
		3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 1, 1, 1, 1, 1,
		// 0xE0..0xEF
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2,
		// 0xF0..0xFF
		2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 4, 4,
	};
	static_assert(UE_ARRAY_COUNT(BuildingFootprintSizeById70) == 0x90);
	return BuildingFootprintSizeById70[BuildingId - 0x70];
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
	static const TArray<int32> Classes = {12, 13, 11, 10, 5, 4, 3};
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

uint16 FSimCopterPeopleCityRules::NextPeopleRandomRaw(uint16& RandomState)
{
	const uint16 Step = (RandomState & 0x8000) != 0
		? uint16((RandomState << 1) ^ 0x1bf5)
		: uint16(RandomState << 1);
	RandomState = uint16(RandomState ^ Step);
	return Step;
}

uint16 FSimCopterPeopleCityRules::NextPeopleRandomBounded(uint16& RandomState, uint16 Bound)
{
	return Bound != 0 ? uint16(NextPeopleRandomRaw(RandomState) % Bound) : 0;
}

int32 FSimCopterPeopleCityRules::ChooseUnspecifiedBehaviorClass(uint16& PeopleRandomState)
{
	// SCHOOK: PersonChooseRandomClass 0x004c7190. DAT_0058dc3a starts at 65000; this routine
	// quarters it, then ordinarily chooses one of the ten human appearance classes. Its very rare
	// special arm is FUN_004c7170: 60% Elvis (20), 40% class 11.
	if (NextPeopleRandomBounded(PeopleRandomState, uint16(65000 >> 2)) == 0)
	{
		return NextPeopleRandomBounded(PeopleRandomState, 0x14) > 0x0B ? 0x0B : 0x14;
	}
	return NextPeopleRandomBounded(PeopleRandomState, 10);
}

int32 FSimCopterPeopleCityRules::ChooseAmbientBehaviorClassForTileClass(int32 TileClass, uint16& PeopleRandomState)
{
	for (int32 Attempt = 0; Attempt < 5; ++Attempt)
	{
		int32 CandidateClass = 0;
		const int32 SpecialRoll = NextPeopleRandomBounded(PeopleRandomState, 0x14);
		if (SpecialRoll == 0)
		{
			CandidateClass = 10;
		}
		else if (SpecialRoll == 1)
		{
			CandidateClass = 17;
		}
		else
		{
			CandidateClass = ChooseUnspecifiedBehaviorClass(PeopleRandomState);
		}

		if (GetAmbientStateTileClasses(CandidateClass).Contains(TileClass))
		{
			return CandidateClass;
		}
	}

	return INDEX_NONE;
}

FString FSimCopterPeopleCityRules::GetFigureNameForBehaviorClass(int32 BehaviorClass)
{
	// FUN_004c71c0's switch binds the figure whose 4-char key matches; full privanim.df names
	// here. Class 6's original key is "BLUE", which matches no privanim figure (the original
	// leaves the body unbound); the remake substitutes SUIT so the person stays visible.
	static const TCHAR* FigureByClass[] = {
		TEXT("Blonde"),     // 0
		TEXT("Woman"),      // 1
		TEXT("2woman"),     // 2
		TEXT("Child"),      // 3
		TEXT("5man"),       // 4
		TEXT("fatman"),     // 5
		TEXT("SUIT"),       // 6 ("BLUE" in the original)
		TEXT("SUIT"),       // 7
		TEXT("5.5man"),     // 8
		TEXT("SHADES"),     // 9
		TEXT("2DOGG"),      // 10 dog
		TEXT("2blonde"),    // 11
		TEXT("Medik"),      // 12
		TEXT("Fireman"),    // 13
		TEXT("Kopp"),       // 14
		TEXT("Badguy"),     // 15
		TEXT("Nessie"),     // 16
		TEXT("Coww"),       // 17 cow
		TEXT("TubaExpert"), // 18
		TEXT("pilot"),      // 19
		TEXT("Elvis"),      // 20
		TEXT("swimmer"),    // 21
	};
	if (BehaviorClass >= 0 && BehaviorClass < int32(UE_ARRAY_COUNT(FigureByClass)))
	{
		return FigureByClass[BehaviorClass];
	}
	return TEXT("5man");
}

namespace
{
	// FUN_004c71c0's per-class appearance block, read straight off its switch: the head index it
	// stores at person+0x18e and the voice pitch offset it stores at person+0x178. Eleven heads
	// exist (0..10) and no class claims 10 - that one belongs to state 6, the medevac victim.
	struct FPeopleAppearanceRow
	{
		int32 HeadImageIndex;
		int32 VoicePitchDeltaHz;
		int32 VoiceSetEvent;
	};

	// Voice pitch is a signed 16-bit offset in the executable; 0xfd44 reads -700 and so on.
	constexpr FPeopleAppearanceRow GPeopleAppearance[] = {
		{ 4,  500, 0x28 }, //  0 Blonde
		{ 8,  400, 0x28 }, //  1 Woman
		{ 6,  400, 0x28 }, //  2 2woman
		{ 7,  700, 0x0e }, //  3 Child
		{ 5,    0, 0x0e }, //  4 5man
		{ 7, -700, 0x0e }, //  5 fatman
		{ 7, -200, 0x29 }, //  6 BLUE
		{ 7, -300, 0x0e }, //  7 SUIT
		{ 5, -100, 0x0e }, //  8 5.5man
		{ 5,  300, 0x0e }, //  9 SHADES
		{ 6,  900, 0x0e }, // 10 2DOGG - 1 in 200 takes an Elvis voice instead
		{ 9, -500, 0x28 }, // 11 2blonde
		{ 3, -300, 0x0e }, // 12 Medik
		{ 2, -300, 0x29 }, // 13 Fireman
		{ 1, -300, 0x29 }, // 14 Kopp
		{ 5, -900, 0x0e }, // 15 Badguy
		{ 4,-1000, 0x0e }, // 16 Nessie - always an Elvis voice
		{ 4,-1000, 0x0e }, // 17 Coww   - 1 in 200 takes an Elvis voice instead
		{ 7,-1000, 0x29 }, // 18 TubaExpert
		{ 0,-1000, 0x29 }, // 19 pilot
		{ 7,-8000, 0x0e }, // 20 Elvis  - always an Elvis voice
		{ 5, 1000, 0x0e }, // 21 swimmer
	};

	// FUN_004c5210's cases 0x2f..0x36: atomchg, ftrchkbr, hum, decphum, popon, trnsfrm, spacidle,
	// pasfstB, passlwB - the eight "Elvis" noises FUN_004c71c0 rolls between.
	constexpr int32 GElvisVoiceFirst = 0x2f;
	constexpr int32 GElvisVoiceCount = 8;

	// FUN_004c3010 stores 65000 here after figure.twk's "Consider this large" bind, which is the
	// value in force during play - the same threshold the celebrity class re-roll uses.
	constexpr uint16 GConsiderThisLarge = 65000;

	const FPeopleAppearanceRow& AppearanceRow(int32 BehaviorClass)
	{
		return GPeopleAppearance[
			(BehaviorClass >= 0 && BehaviorClass < int32(UE_ARRAY_COUNT(GPeopleAppearance)))
				? BehaviorClass
				: 4]; // FUN_004c71c0's default arm leaves the class defaults; 5man is the plain one
	}
}

int32 FSimCopterPeopleCityRules::GetHeadImageIndexForBehaviorClass(int32 BehaviorClass)
{
	return AppearanceRow(BehaviorClass).HeadImageIndex;
}

int32 FSimCopterPeopleCityRules::GetVoicePitchDeltaForBehaviorClass(int32 BehaviorClass)
{
	return AppearanceRow(BehaviorClass).VoicePitchDeltaHz;
}

int32 FSimCopterPeopleCityRules::ChooseVoiceSetForBehaviorClass(int32 BehaviorClass, uint16& PeopleRandomState)
{
	auto RollElvisVoice = [&PeopleRandomState]()
	{
		return GElvisVoiceFirst + int32(NextPeopleRandomBounded(PeopleRandomState, GElvisVoiceCount));
	};

	int32 VoiceSet = AppearanceRow(BehaviorClass).VoiceSetEvent;
	switch (BehaviorClass)
	{
	case 10: // the dog and the cow ask for one Elvis noise in two hundred
	case 17:
		if (NextPeopleRandomBounded(PeopleRandomState, 200) == 0)
		{
			VoiceSet = RollElvisVoice();
		}
		break;
	case 16: // Nessie and Elvis always have one
	case 20:
		VoiceSet = RollElvisVoice();
		break;
	default:
		break;
	}

	// The tail of FUN_004c71c0 runs for every class and can overwrite whatever the switch chose:
	// one person in DAT_0058dc3a speaks in Elvis noises regardless of what they are.
	if (NextPeopleRandomBounded(PeopleRandomState, GConsiderThisLarge) == 0)
	{
		VoiceSet = RollElvisVoice();
	}
	return VoiceSet;
}

FSimCopterPeopleLocalOffset FSimCopterPeopleCityRules::ChooseSpawnLocalOffset(
	int32 FootprintSize,
	int32 PlacementMode,
	uint16& PeopleRandomState)
{
	FSimCopterPeopleLocalOffset Offset;
	const int32 Extent = FMath::Max(0, FootprintSize) << 5;
	if (Extent == 0)
	{
		return Offset;
	}

	switch (PlacementMode)
	{
	case 0:
	{
		const int32 Variable = RandomOffsetInExtent(Extent, PeopleRandomState);
		const int32 Edge = NextPeopleRandomBounded(PeopleRandomState, 2) == 0 ? 1 - Extent : Extent - 1;
		if (NextPeopleRandomBounded(PeopleRandomState, 2) == 0)
		{
			Offset.OriginalX = Edge;
			Offset.OriginalY = Variable;
		}
		else
		{
			Offset.OriginalX = Variable;
			Offset.OriginalY = Edge;
		}
		break;
	}
	case 1:
		Offset.OriginalX = RandomOffsetInExtent(Extent, PeopleRandomState);
		Offset.OriginalY = RandomOffsetInExtent(Extent, PeopleRandomState);
		break;
	case 2:
	{
		const int32 HalfExtent = Extent >> 1;
		Offset.OriginalX = RandomOffsetInExtent(HalfExtent, PeopleRandomState);
		Offset.OriginalY = RandomOffsetInExtent(HalfExtent, PeopleRandomState);
		break;
	}
	case 3:
		Offset.OriginalX = RandomOffsetInExtent(Extent, PeopleRandomState);
		Offset.OriginalY = RandomOffsetInExtent(Extent, PeopleRandomState);
		break;
	default:
		break;
	}

	return Offset;
}
