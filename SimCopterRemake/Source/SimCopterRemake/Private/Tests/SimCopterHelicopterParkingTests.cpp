#if WITH_DEV_AUTOMATION_TESTS
#include "City/SimCopterAirport.h"
#include "City/SimCopterHangar.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Flight/SimCopterHelicopterParking.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Formats/SimCopterOriginalGamePaths.h"
#include "Game/SimCopterCareerSubsystem.h"
#include "Ground/SimCopterTrafficSystemActor.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Missions/SimCopterMissionSystemActor.h"
#include "UI/SimCopterHangarShop.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterParkingRulesTest, "SimCopter.UI.HangarParkingRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterParkingRulesTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterHelicopterParking;
	TArray<FPad> Pads;
	for (int32 Index = 0; Index < SimCopterAirport::PadCount; ++Index)
	{
		if (!IsHangarPad(Index)) continue;
		const FIntPoint Tile = SimCopterAirport::GetPadTile(FIntPoint::ZeroValue, Index);
		Pads.Add({Index, FVector(Tile.X * 400.0, Tile.Y * 400.0, 0)});
	}
	TestEqual(TEXT("Only the eight edge pads are purchase candidates"), Pads.Num(), 8);
	TestFalse(TEXT("Invalid pad rejected"), IsHangarPad(INDEX_NONE));
	for (const FVector Door : {FVector(200, 600, 0), FVector(1000, 600, 0), FVector(600, 200, 0), FVector(600, 1000, 0)})
	{
		SortByDoorDistance(Pads, Door);
		for (int32 Index = 1; Index < Pads.Num(); ++Index)
		{
			TestTrue(TEXT("Pads ordered nearest door first for every facing"),
				FVector::DistSquared2D(Pads[Index-1].Surface, Door) <= FVector::DistSquared2D(Pads[Index].Surface, Door));
		}
	}
	const FBox Aircraft(FVector(-250, -250, 0), FVector(250, 250, 200));
	TestFalse(TEXT("Empty apron is clear"), OverlapsParkedAircraft(Aircraft, {}));
	TestTrue(TEXT("Same pad is occupied"), OverlapsParkedAircraft(Aircraft, {Aircraft}));
	TestTrue(TEXT("Rotor overhang from adjacent tile blocks placement"),
		OverlapsParkedAircraft(Aircraft, {Aircraft.ShiftBy(FVector(400, 0, 0))}));
	TestTrue(TEXT("Rotor height difference does not allow footprint overlap"),
		OverlapsParkedAircraft(Aircraft, {Aircraft.ShiftBy(FVector(400, 0, 300))}));
	TestFalse(TEXT("Separated aircraft fit"), OverlapsParkedAircraft(Aircraft, {Aircraft.ShiftBy(FVector(600, 0, 0))}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterPurchaseParkingTest, "SimCopter.UI.HangarPurchaseParking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterPurchaseParkingTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UGameInstance* Instance = NewObject<UGameInstance>();
	USimCopterCareerSubsystem* Career = NewObject<USimCopterCareerSubsystem>(Instance);
	Career->EnsurePricesLoaded(SimCopterOriginalGame::ResolveRoot());
	Career->BeginCareer();
	ASimCopterMissionSystemActor* Missions = World->SpawnActor<ASimCopterMissionSystemActor>();
	Missions->AddSessionCash(100000 - Missions->GetSessionCash());
	ASimCopterHelicopterPawn* Current = World->SpawnActor<ASimCopterHelicopterPawn>();
	if (!TestTrue(TEXT("Starting model loads"), Current->SwitchHelicopterModel(4))) return false;
	ASimCopterTrafficSystemActor* Traffic = World->SpawnActor<ASimCopterTrafficSystemActor>();
	if (!TestTrue(TEXT("Airport pad data loads"), Traffic->RebuildSpawnData())) return false;
	FVector FirstPad;
	if (!TestTrue(TEXT("Starting pad exists"), Traffic->TryGetAirportPadWorldLocation(0, FirstPad))) return false;
	Current->PlaceOnHelipad(FirstPad, 0);
	ASimCopterHangar* Hangar = World->SpawnActor<ASimCopterHangar>();
	if (!TestTrue(TEXT("Hangar placed"), Hangar->PlaceAtAirport(Traffic, FirstPad))) return false;
	const FTransform Before = Current->GetActorTransform();
	const float FuelBefore = Current->GetFuelFraction();
	SimCopterHangarShop::FContext Shop;
	Shop.Career = Career; Shop.Missions = Missions; Shop.Helicopter = Current; Shop.Hangar = Hangar;
	FString Message;
	if (!TestTrue(TEXT("Buy Jet Ranger"), SimCopterHangarShop::BuyHelicopter(Shop, 1, Message)))
	{
		AddError(Message);
		return false;
	}
	TestEqual(TEXT("Current model preserved"), Current->GetHelicopterTypeIndex(), 4);
	TestTrue(TEXT("Current transform preserved"), Current->GetActorTransform().Equals(Before));
	TestEqual(TEXT("Current fuel preserved"), Current->GetFuelFraction(), FuelBefore);
	TestEqual(TEXT("Exact purchase price charged"), Missions->GetSessionCash(), 100000 - Career->GetHelicopterPrice(0));
	TArray<AActor*> Aircraft;
	UGameplayStatics::GetAllActorsOfClass(World, ASimCopterHelicopterPawn::StaticClass(), Aircraft);
	TestEqual(TEXT("Both aircraft exist"), Aircraft.Num(), 2);
	for (AActor* Actor : Aircraft)
	{
		if (Actor == Current) continue;
		ASimCopterHelicopterPawn* Bought = CastChecked<ASimCopterHelicopterPawn>(Actor);
		TestEqual(TEXT("New model matches catalog"), Bought->GetHelicopterTypeIndex(), 0);
		TestNull(TEXT("Purchase does not possess the new aircraft"), Bought->GetController());
		TestFalse(TEXT("New aircraft does not overlap the original"),
			SimCopterHelicopterParking::OverlapsParkedAircraft(Bought->GetParkingWorldBounds(), {Current->GetParkingWorldBounds()}));
		bool bOnPurchasePad = false;
		const double ChosenDistance = FVector::DistSquared2D(Bought->GetActorLocation(), Hangar->GetDoorWorldLocation());
		for (int32 PadIndex = 0; PadIndex < SimCopterAirport::PadCount; ++PadIndex)
		{
			if (!SimCopterHelicopterParking::IsHangarPad(PadIndex)) continue;
			FVector Surface;
			Traffic->TryGetAirportPadWorldLocation(PadIndex, Surface);
			bOnPurchasePad |= FVector::DistSquared2D(Bought->GetActorLocation(), Surface) < 1.0;
			if (PadIndex == 0) continue; // The original occupies this tile.
			const FBox Candidate = Bought->GetParkingWorldBounds().ShiftBy(
				Surface + FVector(0, 0, Bought->GetHelipadRestingOriginOffsetCm()) - Bought->GetActorLocation());
			if (!SimCopterHelicopterParking::OverlapsParkedAircraft(Candidate, {Current->GetParkingWorldBounds()}))
			{
				TestTrue(TEXT("No closer clear pad was skipped"),
					ChosenDistance <= FVector::DistSquared2D(Surface, Hangar->GetDoorWorldLocation()) + 1.0);
			}
		}
		TestTrue(TEXT("Delivery is on one of the eight hangar pads"), bOnPurchasePad);
	}
	TestTrue(TEXT("Selling the new airframe succeeds"), SimCopterHangarShop::SellHelicopter(Shop, 1, Message));
	Aircraft.Reset();
	UGameplayStatics::GetAllActorsOfClass(World, ASimCopterHelicopterPawn::StaticClass(), Aircraft);
	TestEqual(TEXT("Sold aircraft is removed"), Aircraft.Num(), 1);
	TestTrue(TEXT("Selling another aircraft preserves the original"), Current->GetActorTransform().Equals(Before));
	// Fill each of the eight candidate tiles. Failure must leave cash, ownership and actor count alone.
	for (int32 Index = 0; Index < SimCopterAirport::PadCount; ++Index)
	{
		if (!SimCopterHelicopterParking::IsHangarPad(Index)) continue;
		FVector Surface;
		Traffic->TryGetAirportPadWorldLocation(Index, Surface);
		World->SpawnActor<ASimCopterHelicopterPawn>(Surface, FRotator::ZeroRotator);
	}
	const int32 CashBeforeFailure = Missions->GetSessionCash();
	Aircraft.Reset();
	UGameplayStatics::GetAllActorsOfClass(World, ASimCopterHelicopterPawn::StaticClass(), Aircraft);
	const int32 CountBeforeFailure = Aircraft.Num();
	TestFalse(TEXT("Full pads reject a purchase"), SimCopterHangarShop::BuyHelicopter(Shop, 2, Message));
	TestEqual(TEXT("Failed purchase does not charge"), Missions->GetSessionCash(), CashBeforeFailure);
	TestFalse(TEXT("Failed purchase does not grant ownership"), Career->OwnsHelicopter(1));
	Aircraft.Reset();
	UGameplayStatics::GetAllActorsOfClass(World, ASimCopterHelicopterPawn::StaticClass(), Aircraft);
	TestEqual(TEXT("Failed purchase leaves no phantom aircraft"), Aircraft.Num(), CountBeforeFailure);
	return true;
}
#endif
