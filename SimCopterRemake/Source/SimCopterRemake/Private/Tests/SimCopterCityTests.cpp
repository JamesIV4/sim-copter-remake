// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "City/SimCopterCityGeometryRules.h"
#include "City/SimCopterRuntimeStaticMesh.h"
#include "City/SimCity2000CityActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Formats/SimCity2000Reader.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"

namespace
{
ASimCity2000CityActor* FindLoadedCityActor()
{
	for (TObjectIterator<ASimCity2000CityActor> It; It; ++It)
	{
		ASimCity2000CityActor* Candidate = *It;
		if (Candidate == nullptr || Candidate->IsTemplate() || !IsValid(Candidate))
		{
			continue;
		}
		if (Candidate->GetWorld() != nullptr)
		{
			return Candidate;
		}
	}
	return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterCityBuildingFootprintClaimTest,
	"SimCopter.City.BuildingFootprintClaim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCityBuildingFootprintClaimTest::RunTest(const FString& Parameters)
{
	// Islandtown exposes the regression clearly: its XZON 0x80 marker is at the far corner of a
	// multi-tile building. Treating that marker as a top-left owner placed a full GEO building
	// half to one-and-a-half tiles too far along each axis, directly over roads. FUN_0047c0c0 claims
	// the first row-major XBLD square and never reads XZON for this decision.
	FSimCity2000City SyntheticCity;
	SyntheticCity.Tiles.SetNumZeroed(FSimCity2000City::TileCount);
	constexpr int32 OriginX = 10;
	constexpr int32 OriginY = 20;
	constexpr uint8 ThreeTileBuildingId = 0xD6;
	for (int32 OffsetY = 0; OffsetY < 3; ++OffsetY)
	{
		for (int32 OffsetX = 0; OffsetX < 3; ++OffsetX)
		{
			SyntheticCity.Tiles[(OriginY + OffsetY) * FSimCity2000City::MapSize + OriginX + OffsetX].Building = ThreeTileBuildingId;
		}
	}
	SyntheticCity.Tiles[(OriginY + 2) * FSimCity2000City::MapSize + OriginX + 2].Zone = 0x80;

	TArray<uint8> SceneCellState;
	const FIntPoint OwnerFootprint = FSimCopterCityGeometryRules::ClaimOriginalBuildingFootprint(
		SyntheticCity,
		OriginX,
		OriginY,
		SceneCellState);
	TestEqual(TEXT("The XBLD table supplies the three-tile width"), OwnerFootprint.X, 3);
	TestEqual(TEXT("The original building footprint is square"), OwnerFootprint.Y, 3);
	TestEqual(
		TEXT("The far-corner XZON marker is covered, not treated as a second owner"),
		FSimCopterCityGeometryRules::ClaimOriginalBuildingFootprint(
			SyntheticCity,
			OriginX + 2,
			OriginY + 2,
			SceneCellState),
		FIntPoint::ZeroValue);

	// An incomplete large-building id is rejected instead of placing its large GEO mesh as a
	// one-tile object. That is the exact failure mode that put buildings over adjacent roads.
	FSimCity2000City IncompleteCity;
	IncompleteCity.Tiles.SetNumZeroed(FSimCity2000City::TileCount);
	IncompleteCity.Tiles[OriginY * FSimCity2000City::MapSize + OriginX].Building = ThreeTileBuildingId;
	TArray<uint8> IncompleteSceneCellState;
	TestEqual(
		TEXT("An incomplete three-tile XBLD square is not rendered"),
		FSimCopterCityGeometryRules::ClaimOriginalBuildingFootprint(
			IncompleteCity,
			OriginX,
			OriginY,
			IncompleteSceneCellState),
		FIntPoint::ZeroValue);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterCityIslandBuildingFootprintsTest,
	"SimCopter.City.IslandBuildingFootprints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCityIslandBuildingFootprintsTest::RunTest(const FString& Parameters)
{
	const FString IslandCityPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("../Reference/SimCopterOriginalGame/cities/career/city1.sc2")));
	if (!FPaths::FileExists(IslandCityPath))
	{
		AddWarning(FString::Printf(TEXT("Skipping optional Islandtown footprint test because '%s' is not present."), *IslandCityPath));
		return true;
	}

	FSimCity2000City City;
	FString Error;
	if (!TestTrue(TEXT("Islandtown city data loads"), FSimCity2000Reader::LoadCityFromFile(IslandCityPath, City, Error)))
	{
		AddError(Error);
		return false;
	}

	TArray<uint8> SceneCellState;
	int32 BuildingCount = 0;
	FIntPoint LandmarkFootprint = FIntPoint::ZeroValue;
	FIntPoint OldXzonOwnerFootprint(-1, -1);
	for (int32 FileY = 0; FileY < FSimCity2000City::MapSize; ++FileY)
	{
		for (int32 FileX = 0; FileX < FSimCity2000City::MapSize; ++FileX)
		{
			const FIntPoint Footprint = FSimCopterCityGeometryRules::ClaimOriginalBuildingFootprint(
				City,
				FileX,
				FileY,
				SceneCellState);
			if (Footprint.X > 0)
			{
				++BuildingCount;
			}
			if (FileX == 79 && FileY == 52)
			{
				LandmarkFootprint = Footprint;
			}
			if (FileX == 81 && FileY == 54)
			{
				OldXzonOwnerFootprint = Footprint;
			}
		}
	}

	TestEqual(TEXT("Islandtown has the original 274 claimed building placements"), BuildingCount, 274);
	TestEqual(TEXT("Islandtown XBLD 0xD6 begins at (79,52) as a 3x3 square"), LandmarkFootprint, FIntPoint(3, 3));
	TestEqual(TEXT("Islandtown's old XZON-derived owner at (81,54) is suppressed"), OldXzonOwnerFootprint, FIntPoint::ZeroValue);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterCityRuntimeMeshDuplicationTest,
	"SimCopter.City.RuntimeMeshDuplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCityRuntimeMeshDuplicationTest::RunTest(const FString& Parameters)
{
	TArray<FSimCopterRuntimeMeshSection> Sections;
	FSimCopterRuntimeMeshSection& Section = Sections.AddDefaulted_GetRef();
	Section.Vertices = {
		FVector(0.0f, 0.0f, 0.0f),
		FVector(100.0f, 0.0f, 0.0f),
		FVector(0.0f, 100.0f, 0.0f)
	};
	Section.Triangles = { 0, 1, 2 };

	UStaticMesh* RuntimeMesh =
		SimCopterRuntimeStaticMesh::Build(GetTransientPackage(), Sections, false);
	if (!TestNotNull(TEXT("A valid runtime triangle builds a static mesh"), RuntimeMesh))
	{
		return false;
	}

	TestTrue(TEXT("Runtime building mesh is transient"), RuntimeMesh->HasAnyFlags(RF_Transient));
	TestTrue(
		TEXT("Runtime building mesh is excluded from PIE duplication"),
		RuntimeMesh->HasAnyFlags(RF_DuplicateTransient));

	UInstancedStaticMeshComponent* SourceComponent =
		NewObject<UInstancedStaticMeshComponent>(GetTransientPackage());
	SourceComponent->SetStaticMesh(RuntimeMesh);
	const FName DuplicateName = MakeUniqueObjectName(
		GetTransientPackage(),
		UInstancedStaticMeshComponent::StaticClass(),
		TEXT("RuntimeMeshDuplicate"));
	UInstancedStaticMeshComponent* DuplicateComponent =
		DuplicateObject<UInstancedStaticMeshComponent>(
			SourceComponent,
			GetTransientPackage(),
			DuplicateName);
	if (!TestNotNull(TEXT("The component itself can be duplicated"), DuplicateComponent))
	{
		return false;
	}

	// This is the state AreBuildingInstancesIntact detects at BeginPlay. Rebuilding from GEO is
	// valid; duplicating the fast-built mesh would instead produce the Min LOD warnings.
	TestNull(
		TEXT("The duplicated component does not retain an invalid runtime mesh"),
		DuplicateComponent->GetStaticMesh());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterCityBuildingDemolitionTest,
	"SimCopter.City.BuildingDemolition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCityBuildingDemolitionTest::RunTest(const FString& Parameters)
{
	// Buildings are placed as instances of a per-model runtime static mesh precisely so one can
	// be removed when it burns down (FUN_004a5fd0). Baked into the shared city mesh there is no
	// per-building identity to remove, so this guards the whole chain: the placement map, the
	// footprint clear, and the instance removal.
	ASimCity2000CityActor* CityActor = FindLoadedCityActor();
	if (CityActor == nullptr)
	{
		// No city in the loaded map - nothing to assert against, and failing here would only
		// report on the test environment rather than on the code.
		AddInfo(TEXT("No loaded SimCity2000 city actor; skipping building demolition checks."));
		return true;
	}

	// A freshly built city must report its instances intact. BeginPlay rebuilds when this is
	// false - the recovery for a duplicated (PIE) world whose runtime meshes lost their render
	// data - so a false negative here would silently rebuild the whole city every time play starts.
	TestTrue(
		TEXT("A freshly built city reports its building instances intact"),
		CityActor->AreBuildingInstancesIntact());

	constexpr int32 MapSize = FSimCity2000City::MapSize;
	int32 FoundX = INDEX_NONE;
	int32 FoundY = INDEX_NONE;
	for (int32 TileY = 0; TileY < MapSize && FoundX == INDEX_NONE; ++TileY)
	{
		for (int32 TileX = 0; TileX < MapSize; ++TileX)
		{
			if (CityActor->HasStandingBuildingAtTile(TileX, TileY))
			{
				FoundX = TileX;
				FoundY = TileY;
				break;
			}
		}
	}

	if (FoundX == INDEX_NONE)
	{
		AddInfo(TEXT("Loaded city has no instanced buildings; skipping building demolition checks."));
		return true;
	}

	FBox BuildingBounds(ForceInit);
	TestTrue(
		TEXT("A standing building reports world bounds for the demolition burst"),
		CityActor->TryGetBuildingBoundsAtTile(FoundX, FoundY, BuildingBounds));
	TestTrue(TEXT("Those bounds enclose real volume"), BuildingBounds.GetVolume() > 0.0);
	TestTrue(
		TEXT("The mission-spawn guard recognizes the rendered building volume"),
		CityActor->IsInsideStandingBuildingBounds(BuildingBounds.GetCenter()));
	const FVector JustOutsideWall(
		BuildingBounds.Max.X + 16.0f,
		BuildingBounds.GetCenter().Y,
		BuildingBounds.GetCenter().Z);
	TestTrue(
		TEXT("The mission-spawn guard reserves room for the pedestrian capsule"),
		CityActor->IsInsideStandingBuildingBounds(JustOutsideWall, 32.0f));

	TArray<FIntPoint> ClearedTiles;
	TestTrue(
		TEXT("Demolishing a standing building succeeds"),
		CityActor->DemolishBuildingAtTile(FoundX, FoundY, ClearedTiles));
	TestTrue(TEXT("Demolition reports the footprint it cleared"), ClearedTiles.Num() > 0);
	TestTrue(
		TEXT("The requested tile is part of the cleared footprint"),
		ClearedTiles.Contains(FIntPoint(FoundX, FoundY)));

	// Every tile of the footprint, not just the one asked about, stops being a building and
	// becomes rubble - FUN_004a5fd0 swaps the structure for the footprint-sized rubble model
	// rather than leaving bare ground.
	for (const FIntPoint& Tile : ClearedTiles)
	{
		TestFalse(
			FString::Printf(TEXT("Tile (%d,%d) no longer holds a standing building"), Tile.X, Tile.Y),
			CityActor->HasStandingBuildingAtTile(Tile.X, Tile.Y));
		TestTrue(
			FString::Printf(TEXT("Tile (%d,%d) is left as rubble"), Tile.X, Tile.Y),
			CityActor->HasRubbleAtTile(Tile.X, Tile.Y));
	}
	TestFalse(
		TEXT("A demolished building no longer blocks mission-person placement"),
		CityActor->IsInsideStandingBuildingBounds(BuildingBounds.GetCenter()));

	// A second demolition of the same ground is a no-op rather than a double removal, which is
	// what keeps a repeated burn-down from tearing an unrelated building's instance out.
	TArray<FIntPoint> SecondPass;
	TestFalse(
		TEXT("Demolishing already-cleared ground reports nothing to remove"),
		CityActor->DemolishBuildingAtTile(FoundX, FoundY, SecondPass));
	TestEqual(TEXT("The no-op demolition clears no tiles"), SecondPass.Num(), 0);

	// Neighbouring buildings must survive: RemoveInstance shifts every later instance down, so a
	// stale index here would silently delete the wrong building.
	int32 SurvivingBuildings = 0;
	for (int32 TileY = 0; TileY < MapSize; ++TileY)
	{
		for (int32 TileX = 0; TileX < MapSize; ++TileX)
		{
			if (CityActor->HasStandingBuildingAtTile(TileX, TileY))
			{
				++SurvivingBuildings;
			}
		}
	}
	TestTrue(TEXT("Demolishing one building leaves the rest of the city standing"), SurvivingBuildings > 0);

	// Put the city back so a later test (or the editor session that ran this) sees it intact.
	CityActor->RebuildCity();
	TestTrue(
		TEXT("Rebuilding the city restores the demolished building"),
		CityActor->HasStandingBuildingAtTile(FoundX, FoundY));

	return true;
}
