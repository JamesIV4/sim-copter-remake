// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "City/SimCopterRuntimeStaticMesh.h"
#include "City/SimCity2000CityActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Formats/SimCity2000Reader.h"
#include "Misc/AutomationTest.h"
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
