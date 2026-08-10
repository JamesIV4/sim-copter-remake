// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "City/SimCopterRoadDecorations.h"
#include "City/SimCopterSmokeStacks.h"
#include "Formats/MaxisMeshLibrary.h"
#include "Formats/MaxisMeshReader.h"
#include "Ground/SimCopterFlashingLights.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

using namespace SimCopterRoadDecorations;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterRoadDecorationDispatchTest,
	"SimCopter.City.RoadDecorationDispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterRoadDecorationDispatchTest::RunTest(const FString& Parameters)
{
	// The rand() & 0xf buckets, in FUN_0047c0c0's own order. The litter bin is the `default:` arm,
	// which is why it is nearly half of them.
	const auto Kind = [](const int32 Roll) { return static_cast<int32>(GetStreetFurnitureForRoll(Roll)); };
	const int32 Hydrant = static_cast<int32>(EStreetFurniture::FireHydrant);
	const int32 Phone = static_cast<int32>(EStreetFurniture::PhoneBooth);
	const int32 Mail = static_cast<int32>(EStreetFurniture::MailBox);
	const int32 Trash = static_cast<int32>(EStreetFurniture::TrashCan);

	TestEqual(TEXT("Roll 0 is a hydrant"), Kind(0), Hydrant);
	TestEqual(TEXT("Roll 2 is still a hydrant"), Kind(2), Hydrant);
	TestEqual(TEXT("Roll 3 is a phone box"), Kind(3), Phone);
	TestEqual(TEXT("Roll 5 is still a phone box"), Kind(5), Phone);
	TestEqual(TEXT("Roll 6 is a post box"), Kind(6), Mail);
	TestEqual(TEXT("Roll 8 is still a post box"), Kind(8), Mail);
	TestEqual(TEXT("Roll 9 falls through to a litter bin"), Kind(9), Trash);
	TestEqual(TEXT("Roll 15 too"), Kind(15), Trash);
	TestEqual(TEXT("The roll is masked, not clamped"), Kind(0x10), Hydrant);

	// 7 of 16 rolls are a litter bin, 3 each for the rest.
	int32 Counts[4] = { 0, 0, 0, 0 };
	for (int32 Roll = 0; Roll < 16; ++Roll)
	{
		++Counts[static_cast<int32>(GetStreetFurnitureForRoll(Roll))];
	}
	TestEqual(TEXT("Three rolls in sixteen give a hydrant"), Counts[0], 3);
	TestEqual(TEXT("Three give a phone box"), Counts[1], 3);
	TestEqual(TEXT("Three give a post box"), Counts[2], 3);
	TestEqual(TEXT("Seven give a litter bin"), Counts[3], 7);

	// The *29 set belongs to XBLD 0x1d and the *30 set to 0x1e.
	TestEqual(TEXT("NS road takes FIREH29"), GetStreetFurnitureObjectId(EStreetFurniture::FireHydrant, false), FireHydrant29ObjectId);
	TestEqual(TEXT("EW road takes FIREH30"), GetStreetFurnitureObjectId(EStreetFurniture::FireHydrant, true), FireHydrant30ObjectId);
	TestEqual(TEXT("NS road takes TRASH29"), GetStreetFurnitureObjectId(EStreetFurniture::TrashCan, false), Trash29ObjectId);
	TestEqual(TEXT("EW road takes TRASH30"), GetStreetFurnitureObjectId(EStreetFurniture::TrashCan, true), Trash30ObjectId);

	// --- the gates ---------------------------------------------------------------------------
	// Every case is inside the flatness test, so a sloped road carries nothing.
	TestEqual(TEXT("A sloped straight road takes no furniture"),
		GetRoadDecorationObjectId(0x1d, 3, 5, /*bTileIsFlat=*/false, 0), INDEX_NONE);
	TestEqual(TEXT("A sloped four-way takes no signal"),
		GetRoadDecorationObjectId(0x2b, 3, 5, /*bTileIsFlat=*/false, 0), INDEX_NONE);

	// Straight roads: odd/odd only.
	TestEqual(TEXT("Odd/odd straight road takes its prop"),
		GetRoadDecorationObjectId(0x1d, 3, 5, true, 0), FireHydrant29ObjectId);
	TestEqual(TEXT("Even X does not"),
		GetRoadDecorationObjectId(0x1d, 4, 5, true, 0), INDEX_NONE);
	TestEqual(TEXT("Even Y does not"),
		GetRoadDecorationObjectId(0x1d, 3, 4, true, 0), INDEX_NONE);
	TestEqual(TEXT("The other orientation takes the *30 prop"),
		GetRoadDecorationObjectId(0x1e, 3, 5, true, 0), FireHydrant30ObjectId);

	// T junctions: one lamp per junction id, and only every fourth tile in both axes.
	TestEqual(TEXT("0x23 at 3,3 takes LAMP35"), GetRoadDecorationObjectId(0x23, 3, 3, true, 0), Lamp35ObjectId);
	TestEqual(TEXT("0x24 takes LAMP36"), GetRoadDecorationObjectId(0x24, 7, 11, true, 0), Lamp36ObjectId);
	TestEqual(TEXT("0x25 takes LAMP37"), GetRoadDecorationObjectId(0x25, 15, 3, true, 0), Lamp37ObjectId);
	TestEqual(TEXT("0x26 takes LAMP38"), GetRoadDecorationObjectId(0x26, 3, 15, true, 0), Lamp38ObjectId);
	TestEqual(TEXT("A T junction on an odd but not-every-fourth tile is bare"),
		GetRoadDecorationObjectId(0x23, 1, 3, true, 0), INDEX_NONE);
	TestEqual(TEXT("...and so is an even one"),
		GetRoadDecorationObjectId(0x23, 4, 4, true, 0), INDEX_NONE);

	// Crossroads: signal on odd/odd, except 0x2b which always takes one.
	for (uint8 BuildingId = 0x27; BuildingId <= 0x2a; ++BuildingId)
	{
		TestEqual(FString::Printf(TEXT("0x%x at odd/odd takes a signal"), BuildingId),
			GetRoadDecorationObjectId(BuildingId, 5, 7, true, 0), Signal1ObjectId);
		TestEqual(FString::Printf(TEXT("0x%x elsewhere does not"), BuildingId),
			GetRoadDecorationObjectId(BuildingId, 5, 8, true, 0), INDEX_NONE);
	}
	TestEqual(TEXT("0x2b always takes a signal"), GetRoadDecorationObjectId(0x2b, 4, 8, true, 0), Signal1ObjectId);
	TestEqual(TEXT("...on odd tiles too"), GetRoadDecorationObjectId(0x2b, 5, 7, true, 0), Signal1ObjectId);

	// Nothing outside the road band picks anything up.
	TestEqual(TEXT("A building tile takes no decoration"), GetRoadDecorationObjectId(0x80, 3, 3, true, 0), INDEX_NONE);
	TestEqual(TEXT("A bridge takes none"), GetRoadDecorationObjectId(0x4a, 3, 3, true, 0), INDEX_NONE);
	TestEqual(TEXT("Rail takes none"), GetRoadDecorationObjectId(0x2d, 3, 3, true, 0), INDEX_NONE);

	TestTrue(TEXT("SIGNAL1 is recognised as the signal"), IsTrafficSignalObjectId(Signal1ObjectId));
	TestFalse(TEXT("A lamp is not"), IsTrafficSignalObjectId(Lamp35ObjectId));
	TestTrue(TEXT("All four lamps are recognised"),
		IsStreetLightObjectId(Lamp35ObjectId) && IsStreetLightObjectId(Lamp36ObjectId) &&
		IsStreetLightObjectId(Lamp37ObjectId) && IsStreetLightObjectId(Lamp38ObjectId));
	TestFalse(TEXT("The signal is not a street light"), IsStreetLightObjectId(Signal1ObjectId));

	// The remake's stand-in for the original's build-time rand(): stable for a tile, and it does
	// actually spread across the buckets rather than picking one prop for the whole city.
	TestEqual(TEXT("The roll is stable for a tile"),
		MakeStreetFurnitureRoll(11, 23, 1996), MakeStreetFurnitureRoll(11, 23, 1996));

	// Two cities must not lay out the same street, but the roll is only four bits wide, so any one
	// tile agrees between two seeds about one time in sixteen. Compare the whole map instead.
	int32 SeedDifferenceCount = 0;
	bool bSeenAllKinds[4] = { false, false, false, false };
	int32 TileCount = 0;
	for (int32 TileX = 1; TileX < 128; TileX += 2)
	{
		for (int32 TileY = 1; TileY < 128; TileY += 2)
		{
			const int32 Roll = MakeStreetFurnitureRoll(TileX, TileY, 1996);
			TestTrue(TEXT("The roll stays inside the nibble"), Roll >= 0 && Roll <= 15);
			bSeenAllKinds[static_cast<int32>(GetStreetFurnitureForRoll(Roll))] = true;
			SeedDifferenceCount += Roll != MakeStreetFurnitureRoll(TileX, TileY, 1997) ? 1 : 0;
			++TileCount;
		}
	}
	TestTrue(TEXT("A city's worth of tiles produces all four props"),
		bSeenAllKinds[0] && bSeenAllKinds[1] && bSeenAllKinds[2] && bSeenAllKinds[3]);
	// A perfect hash would disagree on 15/16 of them; anything over half proves the seed is really
	// mixed in rather than shifted off the end.
	TestTrue(TEXT("A different city seed re-rolls most of the map"),
		SeedDifferenceCount > TileCount / 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterRoadDecorationMeshTest,
	"SimCopter.City.RoadDecorationMeshes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterRoadDecorationMeshTest::RunTest(const FString& Parameters)
{
	const FString OriginalGameRoot = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame")));
	if (!FPaths::DirectoryExists(FPaths::Combine(OriginalGameRoot, TEXT("GEO"))))
	{
		AddWarning(FString::Printf(
			TEXT("Skipping optional road decoration mesh test because '%s' is not present."), *OriginalGameRoot));
		return true;
	}

	FMaxisMeshLibrary MeshLibrary;
	FString Error;
	if (!TestTrue(TEXT("Loads original game mesh library"), MeshLibrary.LoadFromOriginalGameRoot(OriginalGameRoot, Error)))
	{
		AddError(Error);
		return false;
	}

	// Every id FUN_0047c0c0's decoration arm hands to FUN_00470571 has to resolve, or a city
	// silently comes back with bare corners and nothing says why.
	struct FDecoration
	{
		int32 ObjectId;
		const TCHAR* Name;
	};
	const FDecoration Decorations[] = {
		{ Lamp35ObjectId, TEXT("LAMP35") },
		{ Lamp36ObjectId, TEXT("LAMP36") },
		{ Lamp37ObjectId, TEXT("LAMP37") },
		{ Lamp38ObjectId, TEXT("LAMP38") },
		{ Signal1ObjectId, TEXT("SIGNAL1") },
		{ Trash29ObjectId, TEXT("TRASH29") },
		{ Trash30ObjectId, TEXT("TRASH30") },
		{ Phone29ObjectId, TEXT("PHONE29") },
		{ Phone30ObjectId, TEXT("PHONE30") },
		{ FireHydrant29ObjectId, TEXT("FIREH29") },
		{ FireHydrant30ObjectId, TEXT("FIREH30") },
		{ Mail29ObjectId, TEXT("MAIL29") },
		{ Mail30ObjectId, TEXT("MAIL30") },
	};
	for (const FDecoration& Decoration : Decorations)
	{
		const FMaxisMeshObject* Object = MeshLibrary.FindObjectByObjectId(Decoration.ObjectId);
		if (TestNotNull(FString::Printf(TEXT("Decoration 0x%x resolves"), Decoration.ObjectId), Object))
		{
			TestEqual(
				FString::Printf(TEXT("Decoration 0x%x is %s"), Decoration.ObjectId, Decoration.Name),
				Object->Header.TableName,
				FString(Decoration.Name));
		}
	}

	// SIGNAL1 carries two markers each in red, green and yellow - one set per direction. They are
	// what the flashing-light schedule cycles, and they are the set that must NOT throw a light.
	const TArray<FColor>* SignalPalette = nullptr;
	const FMaxisMeshObject* Signal = MeshLibrary.FindObjectByObjectId(Signal1ObjectId, &SignalPalette);
	if (TestNotNull(TEXT("SIGNAL1 resolves"), Signal))
	{
		TArray<FSimCopterFlashingLightPoint> SignalLights;
		FSimCopterFlashingLightSchedule::ExtractLightPoints(
			*Signal, SignalPalette, FMaxisMeshReader::MeshUnitsPerCentimeter, 1.0f, true, SignalLights);
		TestEqual(TEXT("SIGNAL1 has six blink markers"), SignalLights.Num(), 6);

		int32 RedCount = 0, GreenCount = 0, YellowCount = 0;
		for (const FSimCopterFlashingLightPoint& Light : SignalLights)
		{
			RedCount += Light.PaletteIndex == FSimCopterFlashingLightSchedule::RedPaletteIndex ? 1 : 0;
			GreenCount += Light.PaletteIndex == FSimCopterFlashingLightSchedule::GreenPaletteIndex ? 1 : 0;
			YellowCount += Light.PaletteIndex == FSimCopterFlashingLightSchedule::YellowPaletteIndex ? 1 : 0;
			TestTrue(TEXT("Extraction defaults a marker to casting a light"), Light.bCastPointLight);
		}
		TestEqual(TEXT("Two red aspects"), RedCount, 2);
		TestEqual(TEXT("Two green aspects"), GreenCount, 2);
		TestEqual(TEXT("Two yellow aspects"), YellowCount, 2);
	}

	// The lamps' light cone is geometry, and it is what the spot light is measured from.
	for (int32 LampObjectId = Lamp35ObjectId; LampObjectId <= Lamp38ObjectId; ++LampObjectId)
	{
		const FMaxisMeshObject* Lamp = MeshLibrary.FindObjectByObjectId(LampObjectId);
		if (!TestNotNull(FString::Printf(TEXT("Lamp 0x%x resolves"), LampObjectId), Lamp))
		{
			continue;
		}

		FStreetLightEmitter Emitter;
		if (!TestTrue(
			FString::Printf(TEXT("Lamp 0x%x has a measurable emitter"), LampObjectId),
			TryGetStreetLightEmitter(*Lamp, FMaxisMeshReader::MeshUnitsPerCentimeter, 1.0f, true, Emitter)))
		{
			continue;
		}

		// One original unit is a quarter of a metre at MeshUnitsPerCentimeter, so the post's 55.7
		// units are 1393 cm and the topmost light card (52.6 units) is 1316. The apex has to land
		// under the head and well above the pavement - the two ways this goes wrong are picking the
		// ground pool instead of the top band, and picking the object origin.
		const float PostTopCm = 55.7f * 25.0f;
		TestTrue(
			FString::Printf(TEXT("Lamp 0x%x apex is high on the post"), LampObjectId),
			Emitter.LocalOffset.Z > PostTopCm * 0.5f);
		TestTrue(
			FString::Printf(TEXT("Lamp 0x%x apex is under the top of the post"), LampObjectId),
			Emitter.LocalOffset.Z < PostTopCm);
		TestTrue(
			FString::Printf(TEXT("Lamp 0x%x cone reaches most of the way down the post"), LampObjectId),
			Emitter.ConeLengthCm > PostTopCm * 0.5f);
		TestTrue(
			FString::Printf(TEXT("Lamp 0x%x cone spreads but is not a hemisphere"), LampObjectId),
			Emitter.ConeHalfAngleDegrees > 1.0f && Emitter.ConeHalfAngleDegrees < 85.0f);
	}

	// A road slab has no light cards at all, so nothing else on a road tile can be mistaken for a
	// lamp by the same extraction.
	if (const FMaxisMeshObject* Road = MeshLibrary.FindObjectByObjectId(0x3b))
	{
		FStreetLightEmitter Emitter;
		TestFalse(
			TEXT("RD29L has no light cone to measure"),
			TryGetStreetLightEmitter(*Road, FMaxisMeshReader::MeshUnitsPerCentimeter, 1.0f, true, Emitter));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterSmokeStackMarkerTest,
	"SimCopter.City.SmokeStackMarkers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterSmokeStackMarkerTest::RunTest(const FString& Parameters)
{
	const FString OriginalGameRoot = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame")));
	if (!FPaths::DirectoryExists(FPaths::Combine(OriginalGameRoot, TEXT("GEO"))))
	{
		AddWarning(FString::Printf(
			TEXT("Skipping optional smoke stack test because '%s' is not present."), *OriginalGameRoot));
		return true;
	}

	FMaxisMeshLibrary MeshLibrary;
	FString Error;
	if (!TestTrue(TEXT("Loads original game mesh library"), MeshLibrary.LoadFromOriginalGameRoot(OriginalGameRoot, Error)))
	{
		AddError(Error);
		return false;
	}

	// The eight shipped models that smoke, with the marker counts scanned out of the .MAX packs.
	// If a pack ever changes these move, and the plumes are silently wrong - so they are asserted.
	struct FStack
	{
		int32 ObjectId;
		const TCHAR* Name;
		int32 MarkerCount;
	};
	const FStack Stacks[] = {
		{ 0xa1, TEXT("IN160"), 6 },
		{ 0xa3, TEXT("IN162"), 8 },
		{ 0xa4, TEXT("IN163"), 6 },
		{ 0xa5, TEXT("IN164"), 4 },
		{ 0xa6, TEXT("IN165"), 6 },
		{ 0xd3, TEXT("IN192"), 8 },
		{ 0xdd, TEXT("PP202"), 4 },
		{ 0xe2, TEXT("PP207"), 6 },
	};

	for (const FStack& Stack : Stacks)
	{
		const FMaxisMeshObject* Object = MeshLibrary.FindObjectByObjectId(Stack.ObjectId);
		if (!TestNotNull(FString::Printf(TEXT("%s resolves"), Stack.Name), Object))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s object id"), Stack.Name), Object->Header.TableName, FString(Stack.Name));

		TArray<USimCopterSmokeStacksComponent::FSmokeMarker> Markers;
		const int32 Added = USimCopterSmokeStacksComponent::ExtractSmokeMarkers(
			*Object, FMaxisMeshReader::MeshUnitsPerCentimeter, 1.0f, true, Markers);
		TestEqual(FString::Printf(TEXT("%s marker count"), Stack.Name), Added, Stack.MarkerCount);

		int32 AnchorCount = 0;
		float TopZ = -TNumericLimits<float>::Max();
		int32 AnchorIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Markers.Num(); ++Index)
		{
			// Every shipped chimney marker is effect class 1, which the selector table resolves to
			// the "light smoke" greys.
			TestEqual(FString::Printf(TEXT("%s marker is effect class 1"), Stack.Name),
				static_cast<int32>(Markers[Index].EffectClass), 1);
			if (Markers[Index].bPointLightAnchor)
			{
				++AnchorCount;
				AnchorIndex = Index;
			}
			TopZ = FMath::Max(TopZ, Markers[Index].LocalOffset.Z);
		}
		TestEqual(FString::Printf(TEXT("%s gets exactly one point light anchor"), Stack.Name), AnchorCount, 1);
		if (Markers.IsValidIndex(AnchorIndex))
		{
			TestEqual(FString::Printf(TEXT("%s anchors its light at the top of the plume"), Stack.Name),
				static_cast<float>(Markers[AnchorIndex].LocalOffset.Z), static_cast<float>(TopZ));
		}
	}

	// A plain office block has no chimney, so the extraction must not invent one - and it must not
	// pick up the neighbouring face-type-25 blink markers either. HO209 carries eleven of those.
	if (const FMaxisMeshObject* Hospital = MeshLibrary.FindObjectByObjectId(0xd1))
	{
		TArray<USimCopterSmokeStacksComponent::FSmokeMarker> Markers;
		USimCopterSmokeStacksComponent::ExtractSmokeMarkers(
			*Hospital, FMaxisMeshReader::MeshUnitsPerCentimeter, 1.0f, true, Markers);
		TestEqual(TEXT("A building with only blink markers produces no smoke"), Markers.Num(), 0);
	}

	return true;
}
