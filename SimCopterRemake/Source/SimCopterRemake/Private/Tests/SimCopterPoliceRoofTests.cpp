#if WITH_DEV_AUTOMATION_TESTS
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Formats/SimCity2000Reader.h"
#include "Formats/SimCopterPeopleReader.h"
#include "Ground/SimCopterGroundAgent.h"
#include "Ground/SimCopterTrafficSystemActor.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterPoliceRoofBoardingTest,
	"SimCopter.Dispatch.PoliceRoofBoarding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterPoliceRoofBoardingTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FPeopleBehaviorModel> Model = MakeShared<FPeopleBehaviorModel>();
	FString Error;
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame")));
	if (!TestTrue(TEXT("Load original police behavior"), FSimCopterPeopleReader::LoadFromFile(
		FSimCopterPeopleReader::ResolvePeoplePath(Root), *Model, Error))) return false;
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	ASimCopterTrafficSystemActor* Traffic = World->SpawnActor<ASimCopterTrafficSystemActor>();
	Traffic->PeopleTileClasses.Init(7, FSimCity2000City::TileCount);
	Traffic->ActiveTileSize = 400.0f;
	ASimCopterGroundAgent* Officer = World->SpawnActor<ASimCopterGroundAgent>();
	Officer->SetOwner(Traffic);
	Officer->BehaviorModel = Model;
	Officer->bBehaviorActive = true;
	Officer->BehaviorContext.ResetToState(7);
	Officer->SetHospitalRoofPost(FVector(0, 0, 1000), 300);
	ASimCopterHelicopterPawn* Heli = World->SpawnActor<ASimCopterHelicopterPawn>();
	Heli->SetActorLocation(FVector(0, 0, 1000 + Heli->FindComponentByClass<UCapsuleComponent>()->GetScaledCapsuleHalfHeight()));
	Officer->SetActorLocation(FVector(0, 0, 1000 + Officer->GetCapsuleHalfHeightCm()));
	ISimCopterBehaviorWorld& Actions = *Officer;
	FSimCopterPersonContext& Context = Officer->BehaviorContext;
	Heli->GroundClearanceCm = 7.5f;
	TestTrue(TEXT("Landed on station roof passes cop height probe"), Actions.EvaluateProximityTest(Context, 1));
	Heli->GroundClearanceCm = 31.25f;
	TestFalse(TEXT("Five original units is too high"), Actions.EvaluateProximityTest(Context, 1));
	Heli->GroundClearanceCm = 31.0f;
	TestTrue(TEXT("Original integer shift accepts below five units"), Actions.EvaluateProximityTest(Context, 1));
	Heli->GroundClearanceCm = 7.5f;
	Context.SelectedObject = Heli;
	Context.SelectedLocation = Heli->GetActorLocation();
	Context.bHasSelection = true;
	Context.Stack.Reset();
	Context.Stack.Add({1051, 5, {}}); // landed probe -> free seat -> walk-and-board
	for (int32 Tick = 0; Tick < 10 && Officer->GetBehaviorCarrier() != Heli; ++Tick)
		FSimCopterBehaviorVM::Tick(Context, *Model, Actions);
	TestTrue(TEXT("Shipped station program boards officer"), Officer->GetBehaviorCarrier() == Heli);
	TestEqual(TEXT("Officer claims a real passenger slot"), Heli->GetMissionPassengerSlots().Num(), 1);
	TestFalse(TEXT("Boarding releases roof confinement"), Officer->bHasHospitalRoofPost);
	Heli->GroundClearanceCm = 200;
	TestFalse(TEXT("Officer cannot alight in flight"), Actions.TryAlightHere());
	Heli->GroundClearanceCm = 7.5f;
	TestTrue(TEXT("Officer can deploy after landing"), Actions.TryAlightHere());
	TestEqual(TEXT("Deployment releases passenger seat"), Heli->GetMissionPassengerSlots().Num(), 0);
	// BHAV 1053's arrest uses the same reaction as the dispatched street police.
	ASimCopterGroundAgent* Criminal = World->SpawnActor<ASimCopterGroundAgent>();
	Criminal->BehaviorModel = Model;
	Criminal->bBehaviorActive = true;
	Criminal->BehaviorContext.ResetToState(10);
	Criminal->SetOwner(Traffic);
	Criminal->SetActorLocation(Officer->GetActorLocation() + FVector(100, 0, 0));
	Criminal->BehaviorContext.Attributes[EBhavAttr::Visible] = 1;
	Criminal->BehaviorContext.Attributes[EBhavAttr::LoopFlag] = 0;
	Traffic->PedestrianAgents.Add(Criminal);
	// Reproduce the observed stall at the actual wave-loop branch, with the aircraft
	// still landed. The shipped false edge must select a criminal without any takeoff.
	Context.SelectedObject = Heli;
	Context.SelectedLocation = Heli->GetActorLocation();
	Context.bHasSelection = true;
	Context.Stack.Reset();
	Context.Stack.Add({1053, 9, {}});
	TestFalse(TEXT("Deployed cop does not wait for takeoff after waving"), Actions.EvaluateProximityTest(Context, 1));
	FSimCopterBehaviorVM::Tick(Context, *Model, Actions);
	TestTrue(TEXT("Pursuit program selects nearby criminal while helicopter remains landed"), Context.SelectedObject.Get() == Criminal);
	TestEqual(TEXT("Search does not require helicopter movement"), Heli->GetLandingSurfaceClearanceCm(), 7.5f);
	for (const FIntPoint Site : {FIntPoint(1051, 5), FIntPoint(1052, 3), FIntPoint(1054, 6), FIntPoint(1053, 18)})
	{
		Context.Stack.Reset();
		Context.Stack.Add({Site.X, Site.Y, {}});
		TestTrue(TEXT("Other police landing probes keep their real height result"), Actions.EvaluateProximityTest(Context, 1));
	}
	Context.SelectedObject = Criminal;
	TestTrue(TEXT("Officer can issue original criminal-caught reaction"), Actions.PushReactionOnSelectedObject(Context, 1060));
	TestEqual(TEXT("Criminal receives arrest program"), Criminal->GetLastReactionProgramId(), 1060);
	Context.SelectedObject = Heli;
	Context.bHasSelection = true;
	TestTrue(TEXT("Officer can return to the cabin after duty"), Actions.BoardSelection(Context));
	World->DestroyWorld(false);
	return true;
}
#endif
