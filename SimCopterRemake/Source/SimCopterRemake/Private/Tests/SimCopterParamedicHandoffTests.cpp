#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Formats/SimCity2000Reader.h"
#include "Ground/SimCopterGroundAgent.h"
#include "Ground/SimCopterTrafficSystemActor.h"
#include "Missions/SimCopterMissionSystemActor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterParamedicCabinHandoffTest,
	"SimCopter.Missions.ParamedicCabinHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterParamedicCabinHandoffTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues InitValues = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
		.CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &InitValues);
	ASimCopterTrafficSystemActor* Traffic = World->SpawnActor<ASimCopterTrafficSystemActor>();
	Traffic->PeopleTileClasses.Init(7, FSimCity2000City::TileCount);
	Traffic->ActiveTileSize = 400.0f;
	World->SpawnActor<ASimCopterMissionSystemActor>();
	ASimCopterGroundAgent* Medic = World->SpawnActor<ASimCopterGroundAgent>();
	ASimCopterGroundAgent* Patient = World->SpawnActor<ASimCopterGroundAgent>();
	ASimCopterHelicopterPawn* Helicopter = World->SpawnActor<ASimCopterHelicopterPawn>();
	Medic->SetOwner(Traffic);
	Patient->SetOwner(Traffic);
	Medic->BehaviorContext.Attributes[EBhavAttr::State] = 5;
	Patient->BehaviorContext.Attributes[EBhavAttr::State] = 6;
	// A hospital medic remains persistent after joining the player's crew, but boarding
	// relinquishes the roof post. Returning crew must not inherit the posted-worker gate.
	Medic->SetPersistentHospitalRoofCrew(true);
	Traffic->PedestrianAgents.Add(Patient);
	TestTrue(TEXT("Patient is carried by the medic"), Patient->BoardCarrier(Medic, false, false, true));
	ISimCopterBehaviorWorld& MedicActions = *Medic;
	FSimCopterPersonContext Context;
	TestFalse(TEXT("Missing destination refuses transfer"), MedicActions.SelectCarriedPerson(Context, false));
	TestTrue(TEXT("Missing destination keeps patient carried"), Patient->GetBehaviorCarrier() == Medic);
	Context.SelectedObject = Helicopter;
	Context.bHasSelection = true;
	const int32 SeatsBefore = Helicopter->GetAvailablePassengerSeats();
	TestTrue(TEXT("Test helicopter has room"), SeatsBefore > 0);
	TestTrue(TEXT("Opcode 46 handoff succeeds"), MedicActions.SelectCarriedPerson(Context, false));
	TestTrue(TEXT("Same patient now rides helicopter"), Patient->GetBehaviorCarrier() == Helicopter);
	TestTrue(TEXT("Patient is hidden inside cabin"), Patient->IsHidden());
	TestEqual(TEXT("Patient consumes one seat"), Helicopter->GetAvailablePassengerSeats(), SeatsBefore - 1);
	TestTrue(TEXT("Handoff selects the transferred patient"), Context.SelectedObject.Get() == Patient);
	TestNull(TEXT("Medic no longer totes patient"), Traffic->FindPersonCarriedBy(*Medic));
	TestTrue(TEXT("Returning medic selects helicopter after patient handoff"), MedicActions.SelectOwningVehicle(Context));
	TestTrue(TEXT("Return destination is helicopter"), Context.SelectedObject.Get() == Helicopter);
	TestTrue(TEXT("Returning medic boards with patient already inside"), MedicActions.BoardSelection(Context));
	TestTrue(TEXT("Medic and patient share the helicopter"), Medic->GetBehaviorCarrier() == Patient->GetBehaviorCarrier());
	TestEqual(TEXT("Medic and patient occupy separate seats"), Helicopter->GetAvailablePassengerSeats(), SeatsBefore - 2);
	Medic->AlightFromCarrier();

	Medic->SetHospitalRoofPost(Medic->GetActorLocation(), 200.0f);
	TestFalse(TEXT("Posted hospital worker cannot select ride instead of unloading"), MedicActions.SelectOwningVehicle(Context));
	TestFalse(TEXT("Posted hospital worker cannot bypass boarding gate"), Medic->BoardCarrier(Helicopter, false));
	Medic->SetPersistentHospitalRoofCrew(false);

	Patient->BoardCarrier(Medic, false, false, true);
	Helicopter->AddMissionPassengersForMission(Helicopter->GetAvailablePassengerSeats(), INDEX_NONE,
		Patient->GetMissionPassengerKind());
	Context.SelectedObject = Helicopter;
	TestFalse(TEXT("Full cabin refuses handoff"), MedicActions.SelectCarriedPerson(Context, false));
	TestTrue(TEXT("Full cabin leaves patient with medic"), Patient->GetBehaviorCarrier() == Medic);
	TestTrue(TEXT("Failed transfer retains vehicle selection"), Context.SelectedObject.Get() == Helicopter);
	World->DestroyWorld(false);
	return true;
}

#endif
