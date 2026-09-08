#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/World.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "GameFramework/InputSettings.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterControllerContextsTest, "SimCopter.Controller.Contexts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterControllerContextsTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	ASimCopterHelicopterPawn* Pawn = World->SpawnActor<ASimCopterHelicopterPawn>();
	Pawn->ControllerPrimaryPressed();
	TestTrue(TEXT("A holds climb in flight"), Pawn->bControllerClimbHeld);
	Pawn->ControllerToolWheelPressed();
	TestFalse(TEXT("Opening a wheel clears A flight hold"), Pawn->bControllerClimbHeld);
	Pawn->ControllerPassengerPressed();
	TestTrue(TEXT("LB+X opens passengers"), Pawn->ControllerMode == ESimCopterControllerMode::PassengerSelect);
	Pawn->ControllerToolWheelReleased();
	TestTrue(TEXT("Releasing LB leaves passengers open"), Pawn->ControllerMode == ESimCopterControllerMode::PassengerSelect);
	Pawn->MissionPassengerSlots.AddDefaulted();
	Pawn->ControllerPassengerSlot = 0;
	Pawn->ControllerPrimaryPressed();
	TestTrue(TEXT("A selects the passenger action"), Pawn->ControllerMode == ESimCopterControllerMode::PassengerConfirm);
	TestFalse(TEXT("Passenger A never climbs"), Pawn->bControllerClimbHeld);
	Pawn->ControllerPassengerConfirmChoice = 1;
	Pawn->ControllerPrimaryPressed();
	TestTrue(TEXT("A confirms the chosen Cancel action"), Pawn->ControllerMode == ESimCopterControllerMode::PassengerSelect);
	Pawn->ControllerCancelPressed();
	TestTrue(TEXT("B exits passengers"), Pawn->ControllerMode == ESimCopterControllerMode::None);
	TestFalse(TEXT("B used to close a menu never descends"), Pawn->bControllerDescendHeld);
	Pawn->ControllerCancelReleased();
	Pawn->ControllerCancelPressed();
	TestTrue(TEXT("Fresh B holds descent in flight"), Pawn->bControllerDescendHeld);
	Pawn->ControllerCancelReleased();
	Pawn->ControllerDispatchWheelPressed();
	Pawn->ControllerCancelPressed();
	Pawn->ControllerDispatchWheelReleased();
	TestTrue(TEXT("Cancel plus release leaves dispatch closed"), Pawn->ControllerMode == ESimCopterControllerMode::None);
	Pawn->ControllerDispatchWheelPressed();
	TestEqual(TEXT("Wheel opens with no implicit dispatch"), Pawn->ControllerRadialIndex, INDEX_NONE);
	Pawn->ControllerRightY(-1.0f);
	Pawn->UpdateControllerRadialSelection();
	TestEqual(TEXT("Pointing up highlights Fire Truck"), Pawn->ControllerRadialIndex, 0);
	Pawn->ControllerRightY(0.0f);
	Pawn->UpdateControllerRadialSelection();
	TestEqual(TEXT("Centring removes the old dispatch highlight"), Pawn->ControllerRadialIndex, INDEX_NONE);
	Pawn->ControllerDispatchWheelReleased();
	TestTrue(TEXT("Unhighlighted release closes without selecting a command"),
		Pawn->ControllerMode == ESimCopterControllerMode::None);
	Pawn->ControllerDispatchWheelPressed();
	Pawn->ControllerRightY(-1.0f);
	Pawn->ControllerPrimaryPressed();
	TestTrue(TEXT("A leaves dispatch open for shoulder-release selection"),
		Pawn->ControllerMode == ESimCopterControllerMode::DispatchWheel);
	TestFalse(TEXT("Held dispatch A does not become climb"), Pawn->bControllerClimbHeld);
	Pawn->ControllerDispatchWheelReleased();
	TestTrue(TEXT("Later RB release remains outside dispatch context"),
		Pawn->ControllerMode == ESimCopterControllerMode::None);
	Pawn->ControllerPrimaryReleased();
	Pawn->GroundClearanceCm = 0;
	Pawn->bIsLanded = false;
	TestFalse(TEXT("Exit hint eligibility requires landed, even at zero clearance"), Pawn->CanExitHelicopter());
	Pawn->bIsLanded = true;
	TestTrue(TEXT("Landed helicopter can offer exit"), Pawn->CanExitHelicopter());
	Pawn->ControllerMode = ESimCopterControllerMode::None;
	Pawn->bSpotlightTargetFrozen = true; // Test aim accumulation without a city trace.
	Pawn->SpotlightAimPitchInput = Pawn->SpotlightAimYawInput = 0;
	Pawn->SpotlightAimPitch1616 = Pawn->SpotlightAimYaw1616 = 0;
	for (bool bCamera : {false, true})
	{
		Pawn->bControllerCameraAdjustHeld = bCamera;
		Pawn->ControllerDPadUpPressed();
		Pawn->ControllerDPadRightPressed();
		Pawn->UpdateControllerToolManipulation();
		Pawn->UpdateSpotlightTarget(0.05f);
		TestEqual(TEXT("D-pad never changes spotlight pitch"), Pawn->SpotlightAimPitch1616, 0);
		TestEqual(TEXT("D-pad never changes spotlight yaw"), Pawn->SpotlightAimYaw1616, 0);
		Pawn->ControllerDPadUpReleased();
		Pawn->ControllerDPadRightReleased();
	}
	Pawn->SpotlightAimPitchInput = 1;
	Pawn->UpdateSpotlightTarget(0.05f);
	TestTrue(TEXT("Keyboard spotlight input still adjusts aim"), Pawn->SpotlightAimPitch1616 != 0);
	World->DestroyWorld(false);

	// The hint must retain every keyboard alternative as well as the controller shortcut.
	UInputSettings* Settings = UInputSettings::GetInputSettings();
	const FInputAxisKeyMapping Extra(TEXT("SimCopterCollective"), EKeys::F10, 1.0f);
	Settings->AddAxisMapping(Extra, false);
	const FString Hint = ASimCopterHelicopterPawn::GetCollectiveUpKeyDisplayName().ToString();
	TestTrue(TEXT("Hint includes a second live climb binding"), Hint.Contains(EKeys::F10.GetDisplayName().ToString()));
	TestFalse(TEXT("Keyboard hint excludes controller A shortcut"), Hint.Contains(TEXT(" / A")));
	const FString PadHint = ASimCopterHelicopterPawn::GetCollectiveUpKeyDisplayName(true).ToString();
	TestTrue(TEXT("Controller hint includes A"), PadHint.Contains(TEXT("A")));
	TestFalse(TEXT("Controller hint excludes keyboard alternatives"), PadHint.Contains(TEXT("F10")));
	TestEqual(TEXT("Controller exit hint names Y"), ASimCopterHelicopterPawn::GetExitHelicopterKeyDisplayName(true).ToString(), FString(TEXT("Y")));
	Settings->RemoveAxisMapping(Extra, false);
	return true;
}
#endif
