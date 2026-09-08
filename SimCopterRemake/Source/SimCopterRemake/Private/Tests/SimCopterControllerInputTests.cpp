// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Flight/SimCopterControllerInput.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterControllerFlightRoutingTest,
	"SimCopter.Controller.FlightRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterControllerFlightRoutingTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterControllerInput;

	const FFlightRouting Flight = ResolveFlightRouting(
		/*LeftStickX=*/0.65f,
		/*LeftStickY=*/0.8f,
		/*RightStickY=*/-0.4f,
		/*bCameraAdjustHeld=*/false,
		/*LeftTriggerValue=*/0.0f,
		/*RightTriggerValue=*/1.0f);
	TestEqual(TEXT("left Y reaches original analog pitch percentage"), Flight.PitchAxisPercent, -80);
	TestEqual(TEXT("left X is coordinated turn without R3"), Flight.TurnAxisPercent, 65);
	TestEqual(TEXT("slide is idle without R3"), Flight.SlideAxisPercent, 0);
	TestEqual(TEXT("RT raises collective"), Flight.CollectiveCommand, 1);
	TestEqual(TEXT("camera vertical is isolated from flight"), Flight.CameraVerticalCommand, 0);

	const FFlightRouting Camera = ResolveFlightRouting(
		/*LeftStickX=*/-0.55f,
		/*LeftStickY=*/0.25f,
		/*RightStickY=*/0.7f,
		/*bCameraAdjustHeld=*/true,
		/*LeftTriggerValue=*/1.0f,
		/*RightTriggerValue=*/0.0f);
	TestEqual(TEXT("R3 keeps analog pitch available"), Camera.PitchAxisPercent, -25);
	TestEqual(TEXT("R3 suppresses coordinated turn"), Camera.TurnAxisPercent, 0);
	TestEqual(TEXT("R3 left X preserves dedicated slide"), Camera.SlideAxisPercent, -55);
	TestEqual(TEXT("R3 suppresses collective"), Camera.CollectiveCommand, 0);
	TestEqual(TEXT("R3+LT moves helicopter down in frame"), Camera.CameraVerticalCommand, -1);
	TestEqual(TEXT("R3+RS Y supplies zoom"), Camera.CameraZoomCommand, 0.7f);

	const FFlightRouting Opposed = ResolveFlightRouting(
		0.0f, 0.0f, 0.0f, false, 1.0f, 1.0f);
	TestEqual(TEXT("LT and RT together cancel vertical flight"), Opposed.CollectiveCommand, 0);

	TestEqual(TEXT("A climbs without a trigger"), ResolveFlightRouting(0, 0, 0, false, 0, 0, true, false).CollectiveCommand, 1);
	TestEqual(TEXT("B descends without a trigger"), ResolveFlightRouting(0, 0, 0, false, 0, 0, false, true).CollectiveCommand, -1);
	TestEqual(TEXT("A plus LT cancels"), ResolveFlightRouting(0, 0, 0, false, 1, 0, true, false).CollectiveCommand, 0);
	TestEqual(TEXT("RT plus B cancels"), ResolveFlightRouting(0, 0, 0, false, 0, 1, false, true).CollectiveCommand, 0);
	TestEqual(TEXT("A and RT do not double collective"), ResolveFlightRouting(0, 0, 0, false, 0, 1, true, false).CollectiveCommand, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterControllerRadialSelectionTest,
	"SimCopter.Controller.RadialSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterControllerRadialSelectionTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterControllerInput;

	TestEqual(TEXT("centring the stick clears the highlight"), ResolveRadialIndex(FVector2D(0.1f, 0.1f), 4, 2), INDEX_NONE);
	TestEqual(TEXT("up selects slot zero"), ResolveRadialIndex(FVector2D(0.0f, 1.0f), 4, 2), 0);
	TestEqual(TEXT("right proceeds clockwise"), ResolveRadialIndex(FVector2D(1.0f, 0.0f), 4, 0), 1);
	TestEqual(TEXT("down selects opposite slot"), ResolveRadialIndex(FVector2D(0.0f, -1.0f), 4, 0), 2);
	TestEqual(TEXT("left wraps to final slot"), ResolveRadialIndex(FVector2D(-1.0f, 0.0f), 4, 0), 3);
	TestEqual(TEXT("empty radial has no selection"), ResolveRadialIndex(FVector2D(0.0f, 1.0f), 0, 0), INDEX_NONE);
	const auto Police = GetDispatchSelection(1);
	const auto Chase = GetDispatchSelection(3);
	TestEqual(TEXT("Police chase uses the same police service"), Chase.ServiceIndex, Police.ServiceIndex);
	TestFalse(TEXT("Normal Police does not chase"), Police.bChaseSpotlight);
	TestTrue(TEXT("Police (Chase) is a distinct wheel command"), Chase.bChaseSpotlight);
	TestEqual(TEXT("Invalid dispatch slot cannot dispatch"), GetDispatchSelection(4).ServiceIndex, INDEX_NONE);
	float LastAnalog = 0;
	TestFalse(TEXT("Idle stick noise is not device use"), UpdateAnalogActivity(0.05f, LastAnalog));
	bool bSawSlowMovement = false;
	for (float Value = 0.1f; Value < 0.6f; Value += 0.05f)
		bSawSlowMovement |= UpdateAnalogActivity(Value, LastAnalog);
	TestTrue(TEXT("Slow deliberate stick movement switches device"), bSawSlowMovement);
	LastAnalog = 1;
	TestFalse(TEXT("Held analog reports do not steal the keyboard hint"), UpdateAnalogActivity(1, LastAnalog));
	TestFalse(TEXT("Returning to centre is not new controller use"), UpdateAnalogActivity(0, LastAnalog));
	TestTrue(TEXT("A fresh trigger squeeze switches device"), UpdateAnalogActivity(0.8f, LastAnalog));

	// Point at every painted sector for every tool count and the dispatch wheel.
	for (int32 Count = 1; Count <= 8; ++Count)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector2D Painted = GetRadialSlotDirection(Index, Count);
			TestEqual(TEXT("Physical stick points at the painted label"),
				ResolveRadialIndex(FVector2D(Painted.X, -Painted.Y), Count, 0), Index);
		}
	}
	TestTrue(TEXT("Slot zero is painted above the hub"), GetRadialSlotDirection(0, 4).Equals(FVector2D(0, -1)));
	TestTrue(TEXT("Slot one is painted right of the hub"), GetRadialSlotDirection(1, 4).Equals(FVector2D(1, 0)));


	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
