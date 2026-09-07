#if WITH_DEV_AUTOMATION_TESTS
#include "City/SimCopterTunnel.h"
#include "Engine/World.h"
#include "Ground/SimCopterGroundAgent.h"
#include "Ground/SimCopterTrafficSystemActor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterTunnelTransitTest,
	"SimCopter.Traffic.TunnelTransit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterTunnelTransitTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Three tiles take one second"), SimCopterTunnel::TravelSeconds(3), 1.0f);
	TestEqual(TEXT("Six tiles take two seconds"), SimCopterTunnel::TravelSeconds(6), 2.0f);
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	ASimCopterTrafficSystemActor* Traffic = World->SpawnActor<ASimCopterTrafficSystemActor>();
	Traffic->ActiveTileSize = 400;
	Traffic->RoadNodes.SetNum(4);
	const int32 Xs[] = {49, 50, 53, 54};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		auto& Node = Traffic->RoadNodes[Index];
		Node.FileX = Xs[Index]; Node.FileY = 64;
		Node.Location = FVector((Xs[Index] - 50) * 400, 0, 10);
		Node.LocalLocation = Node.Location;
		Node.BuildingId = Index == 1 ? 0x40 : (Index == 2 ? 0x42 : 0x1d);
		Traffic->RoadNodeIndexByTile.Add(FIntPoint(Xs[Index], 64), Index);
	}
	Traffic->RoadNodes[0].Neighbors = {1}; Traffic->RoadNodes[1].Neighbors = {0};
	Traffic->RoadNodes[2].Neighbors = {3}; Traffic->RoadNodes[3].Neighbors = {2};
	int32 Exit, ExitRoad;
	TestTrue(TEXT("Paired exit found across hill"), Traffic->FindLinkedTunnelExit(1, 0, Exit, ExitRoad));
	TestEqual(TEXT("Correct exit portal"), Exit, 2);
	TestEqual(TEXT("Correct outgoing road"), ExitRoad, 3);
	TestTrue(TEXT("Tunnel links work in reverse"), Traffic->FindLinkedTunnelExit(2, 3, Exit, ExitRoad));
	TestEqual(TEXT("Reverse exit portal"), Exit, 1);
	TArray<int32> Route;
	TestTrue(TEXT("Emergency route can cross disconnected surface networks through tunnel"),
		Traffic->TryPlanRoadRoute(FIntPoint(49, 64), FIntPoint(54, 64), Route));
	TestTrue(TEXT("Route includes entry and exit"), Route == TArray<int32>({0, 1, 2, 3}));
	Traffic->RoadNodes[2].LocalLocation.Z += 200;
	TestFalse(TEXT("Different elevation portal is not linked"), Traffic->FindLinkedTunnelExit(1, 0, Exit, ExitRoad));
	Traffic->RoadNodes[2].LocalLocation.Z -= 200;
	for (SimCopterDispatch::EService Service : {SimCopterDispatch::EService::Police, SimCopterDispatch::EService::Ambulance, SimCopterDispatch::EService::FireTruck})
	{
		ASimCopterGroundAgent* Vehicle = World->SpawnActor<ASimCopterGroundAgent>();
		Vehicle->SetOwner(Traffic);
		Vehicle->SetActorLocation(FVector(321, 0, 0));
		Vehicle->SetRouteState(1, 0, 2);
		Traffic->VehicleAgents.Add(Vehicle);
		auto& Fleet = Traffic->DispatchVehicles[int32(Service)];
		Fleet.SetNum(1);
		Fleet[0].Agent = Vehicle;
		Fleet[0].State = ESimCopterDispatchVehicleState::Responding;
		Fleet[0].DestinationTile = FIntPoint(54, 64);
		Fleet[0].RouteNodes = Route;
		Fleet[0].RouteCursor = 1;
		TestTrue(TEXT("Service vehicle starts tunnel trip"), Traffic->BeginTunnelTransit(*Vehicle, 1, 0));
		Traffic->VehicleAgents.Remove(Vehicle); // UpdateAgentPool removes hidden trips after its iteration
		TestTrue(TEXT("Vehicle hidden underground"), Vehicle->IsHidden());
		TestFalse(TEXT("Underground vehicle cannot drive through the terrain"), Vehicle->IsActorTickEnabled());
		Traffic->UpdateOneDispatchVehicle(Service, 0, 0.25f);
		TestEqual(TEXT("Dispatch route waits during transit"), Fleet[0].RouteCursor, 1);
		Traffic->UpdateTunnelTransits(0.75f);
		TestTrue(TEXT("Vehicle stays hidden until travel time elapsed"), Vehicle->IsHidden());
		if (Service == SimCopterDispatch::EService::Police)
		{
			TArray<uint8> Saved;
			const FName Identity = Vehicle->GetRuntimeSaveIdentityName();
			TestTrue(TEXT("Save during underground trip"), Traffic->CaptureRuntimeSaveState(Saved));
			if (!TestTrue(TEXT("Restore underground trip"), Traffic->RestoreRuntimeSaveState(Saved, nullptr)) ||
				!TestEqual(TEXT("One trip restored"), Traffic->TunnelTransits.Num(), 1))
			{
				World->DestroyWorld(false);
				return false;
			}
			Vehicle = Traffic->TunnelTransits[0].Agent.Get();
			TestEqual(TEXT("Saved vehicle identity survives transit"), Vehicle->GetRuntimeSaveIdentityName(), Identity);
			TestEqual(TEXT("Remaining underground time survives save/load"), Traffic->TunnelTransits[0].RemainingSeconds, 0.25f);
			TestTrue(TEXT("Restored vehicle remains hidden"), Vehicle->IsHidden());
		}
		Traffic->UpdateTunnelTransits(0.25f);
		TestFalse(TEXT("Same vehicle emerges after one second"), Vehicle->IsHidden());
		TestTrue(TEXT("Emerging vehicle resumes ticking"), Vehicle->IsActorTickEnabled());
		TestTrue(TEXT("Dispatch preserves vehicle identity"), Fleet[0].Agent.Get() == Vehicle);
		TestEqual(TEXT("Vehicle heads toward exit road"), Vehicle->GetRouteTargetNode(), 3);
		TestTrue(TEXT("Service replans from exit to original destination"), Fleet[0].RouteNodes == TArray<int32>({2, 3}));
		TestTrue(TEXT("Vehicle rejoins traffic pool"), Traffic->VehicleAgents.Contains(Vehicle));
		Traffic->VehicleAgents.Remove(Vehicle);
		Fleet.Reset();
		Vehicle->Destroy();
	}
	World->DestroyWorld(false);
	return true;
}
#endif
