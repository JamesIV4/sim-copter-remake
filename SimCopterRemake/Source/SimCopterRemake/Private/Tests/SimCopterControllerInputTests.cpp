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
		/*bRightShoulderHeld=*/true,
		/*RightTriggerValue=*/0.0f);
	TestEqual(TEXT("left Y reaches original analog pitch percentage"), Flight.PitchAxisPercent, -80);
	TestEqual(TEXT("left X is coordinated turn without R3"), Flight.TurnAxisPercent, 65);
	TestEqual(TEXT("slide is idle without R3"), Flight.SlideAxisPercent, 0);
	TestEqual(TEXT("RB raises collective"), Flight.CollectiveCommand, 1);
	TestEqual(TEXT("camera vertical is isolated from flight"), Flight.CameraVerticalCommand, 0);

	const FFlightRouting Camera = ResolveFlightRouting(
		/*LeftStickX=*/-0.55f,
		/*LeftStickY=*/0.25f,
		/*RightStickY=*/0.7f,
		/*bCameraAdjustHeld=*/true,
		/*bRightShoulderHeld=*/false,
		/*RightTriggerValue=*/1.0f);
	TestEqual(TEXT("R3 keeps analog pitch available"), Camera.PitchAxisPercent, -25);
	TestEqual(TEXT("R3 suppresses coordinated turn"), Camera.TurnAxisPercent, 0);
	TestEqual(TEXT("R3 left X preserves dedicated slide"), Camera.SlideAxisPercent, -55);
	TestEqual(TEXT("R3 suppresses collective"), Camera.CollectiveCommand, 0);
	TestEqual(TEXT("R3+RT moves helicopter down in frame"), Camera.CameraVerticalCommand, -1);
	TestEqual(TEXT("R3+RS Y supplies zoom"), Camera.CameraZoomCommand, 0.7f);

	const FFlightRouting Opposed = ResolveFlightRouting(
		0.0f, 0.0f, 0.0f, false, true, 1.0f);
	TestEqual(TEXT("RB and RT together cancel vertical flight"), Opposed.CollectiveCommand, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterControllerRadialSelectionTest,
	"SimCopter.Controller.RadialSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterControllerRadialSelectionTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterControllerInput;

	TestEqual(TEXT("dead zone preserves current slot"), ResolveRadialIndex(FVector2D(0.1f, 0.1f), 4, 2), 2);
	TestEqual(TEXT("up selects slot zero"), ResolveRadialIndex(FVector2D(0.0f, 1.0f), 4, 2), 0);
	TestEqual(TEXT("right proceeds clockwise"), ResolveRadialIndex(FVector2D(1.0f, 0.0f), 4, 0), 1);
	TestEqual(TEXT("down selects opposite slot"), ResolveRadialIndex(FVector2D(0.0f, -1.0f), 4, 0), 2);
	TestEqual(TEXT("left wraps to final slot"), ResolveRadialIndex(FVector2D(-1.0f, 0.0f), 4, 0), 3);
	TestEqual(TEXT("empty radial has no selection"), ResolveRadialIndex(FVector2D(0.0f, 1.0f), 0, 0), INDEX_NONE);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
