#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/World.h"
#include "Game/SimCopterGameMode.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Ground/SimCopterOnFootPawn.h"
#include "Camera/CameraComponent.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterStartupCameraTest,
	"SimCopter.Camera.StartupPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterStartupCameraTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	ASimCopterGameMode* Mode = World->SpawnActor<ASimCopterGameMode>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	// This synthetic world does not run actor initialization/BeginPlay.
	World->AddController(Controller);
	ASimCopterOnFootPawn* Pilot = World->SpawnActor<ASimCopterOnFootPawn>();
	Controller->Possess(Pilot);
	TestTrue(TEXT("Startup controller possesses pilot"), Controller->GetPawn() == Pilot);
	USpringArmComponent* Arm = Pilot->FindComponentByClass<USpringArmComponent>();
	UCameraComponent* Camera = Pilot->FindComponentByClass<UCameraComponent>();
	Arm->bDoCollisionTest = false;
	Arm->TickComponent(1.0f, LEVELTICK_All, nullptr);
	const FVector InitialCamera = Camera->GetComponentLocation();
	const FVector AirportOffset(20000, -15000, 800);
	Pilot->SetActorLocation(AirportOffset, false, nullptr, ETeleportType::TeleportPhysics);
	Arm->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("Fixture reproduces camera lag after airport teleport"),
		Camera->GetComponentLocation().Equals(InitialCamera + AirportOffset, 0.01f));
	Mode->FinishStartupCamera();
	TestTrue(TEXT("First revealed camera starts at the final pilot location"),
		Camera->GetComponentLocation().Equals(InitialCamera + AirportOffset, 0.01f));
	const FVector RevealedCamera = Camera->GetComponentLocation();
	Arm->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Next frame stays stationary"), Camera->GetComponentLocation().Equals(RevealedCamera, 0.01f));
	TestTrue(TEXT("Normal walking camera smoothing remains enabled"), Arm->bEnableCameraLag);
	World->DestroyWorld(false);
	return true;
}
#endif
