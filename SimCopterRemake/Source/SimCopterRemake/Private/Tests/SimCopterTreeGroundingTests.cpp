#if WITH_DEV_AUTOMATION_TESTS
#include "City/SimCopterTreeGrounding.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterTreeGroundingTest,
	"SimCopter.City.TreeGrounding", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterTreeGroundingTest::RunTest(const FString& Parameters)
{
	FMaxisTextureImage Image;
	Image.Width = 4;
	Image.Height = 4;
	Image.Pixels.Init(FColor::Transparent, 16);
	// Transparent border, and uneven visible bottom edge: the V=0 end is at the base.
	Image.Pixels[1 * 4 + 1] = FColor::White;
	Image.Pixels[2 * 4 + 2] = FColor::White;
	const TArray<FVector> Samples = SimCopterTreeGrounding::BuildBottomSamples(Image, FVector(0, 0, 20), 8, 20);
	TestEqual(TEXT("Only the lowest occupied row contributes, on both cards"), Samples.Num(), 2);
	TestEqual(TEXT("Transparent bottom margin raises first visible edge"), Samples[0].Z, 10.0);
	TestEqual(TEXT("Both crossed cards use the trunk base"), Samples[1].Z, 10.0);
	auto Terrain = [](const FVector& Point) -> float { return 80 + Point.X * 2 - Point.Y * 3; };
	for (float InitialZ : {200.0f, -200.0f})
	{
		const FVector Origin(0, 0, InitialZ);
		const float Offset = SimCopterTreeGrounding::PlacementOffset(Samples, Origin, Terrain);
		TestTrue(TEXT("Floating tree lowers; buried tree rises"), InitialZ > 0 ? Offset < 0 : Offset > 0);
		float HighestGap = -TNumericLimits<float>::Max();
		for (const FVector& Sample : Samples)
		{
			const FVector World = Origin + Sample + FVector(0, 0, Offset);
			const float Gap = World.Z - Terrain(World);
			TestTrue(TEXT("All bottom pixels on both cards meet or lie below terrain"), Gap <= 0.001f);
			HighestGap = FMath::Max(HighestGap, Gap);
		}
		TestTrue(TEXT("Highest gap closes exactly with one shared offset"), FMath::IsNearlyZero(HighestGap));
	}
	// Exercise the engine's cooked terrain mesh, including transformed city placement.
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	AActor* Owner = World->SpawnActor<AActor>();
	UProceduralMeshComponent* TerrainMesh = NewObject<UProceduralMeshComponent>(Owner);
	Owner->SetRootComponent(TerrainMesh);
	TerrainMesh->RegisterComponent();
	TerrainMesh->SetWorldLocation(FVector(500, -250, 300));
	TerrainMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TerrainMesh->bUseAsyncCooking = false;
	TerrainMesh->CreateMeshSection(0,
		TArray<FVector>{FVector(-100,-100,10), FVector(100,-100,30), FVector(100,100,50), FVector(-100,100,30)},
		TArray<int32>{0,1,2,0,2,3}, TArray<FVector>(), TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), true);
	float TracedOffset = 0;
	TestTrue(TEXT("Engine traces the actual cooked terrain"), SimCopterTreeGrounding::TracePlacementOffset(
		*TerrainMesh, Samples, FVector(0,0,200), TracedOffset));
	TestTrue(TEXT("Trace respects terrain and city transform"), FMath::IsNearlyEqual(TracedOffset, -179.8f, 0.01f));
	TestFalse(TEXT("Missing terrain hit leaves placement alone"), SimCopterTreeGrounding::TracePlacementOffset(
		*TerrainMesh, Samples, FVector(1000,1000,0), TracedOffset));
	TestEqual(TEXT("No hit does not invent a height"), TracedOffset, 0.0f);
	World->DestroyWorld(false);
	Image.Pixels.Init(FColor::Transparent, 16);
	TestTrue(TEXT("Fully transparent image has no ground samples"),
		SimCopterTreeGrounding::BuildBottomSamples(Image, FVector::ZeroVector, 8, 20).IsEmpty());
	return true;
}
#endif
