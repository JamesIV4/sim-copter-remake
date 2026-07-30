// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/SimCopterControllerInput.h"

namespace SimCopterControllerInput
{
FFlightRouting ResolveFlightRouting(
	const float LeftStickX,
	const float LeftStickY,
	const float RightStickY,
	const bool bCameraAdjustHeld,
	const bool bRightShoulderHeld,
	const float RightTriggerValue)
{
	FFlightRouting Routing;
	const float ClampedLeftX = FMath::Clamp(LeftStickX, -1.0f, 1.0f);
	const float ClampedLeftY = FMath::Clamp(LeftStickY, -1.0f, 1.0f);
	Routing.PitchAxisPercent = FMath::RoundToInt(-ClampedLeftY * 100.0f);

	if (bCameraAdjustHeld)
	{
		// The controller had no binding for the original's dedicated Q/E slide controls. R3 is
		// already the camera-adjust modifier and leaves the left stick free, so its X axis is the
		// least surprising place to retain that flight capability.
		Routing.SlideAxisPercent = FMath::RoundToInt(ClampedLeftX * 100.0f);
	}
	else
	{
		Routing.TurnAxisPercent = FMath::RoundToInt(ClampedLeftX * 100.0f);
	}

	const bool bRightTriggerHeld =
		FMath::Clamp(RightTriggerValue, 0.0f, 1.0f) > TriggerPressedThreshold;
	const int32 VerticalCommand =
		(bRightShoulderHeld ? 1 : 0) - (bRightTriggerHeld ? 1 : 0);

	if (bCameraAdjustHeld)
	{
		Routing.CameraVerticalCommand = VerticalCommand;
		Routing.CameraZoomCommand = FMath::Clamp(RightStickY, -1.0f, 1.0f);
	}
	else
	{
		Routing.CollectiveCommand = VerticalCommand;
	}

	return Routing;
}

int32 ResolveRadialIndex(
	const FVector2D& Stick,
	const int32 SlotCount,
	const int32 CurrentIndex,
	const float DeadZone)
{
	if (SlotCount <= 0)
	{
		return INDEX_NONE;
	}

	const int32 SafeCurrent = FMath::Clamp(CurrentIndex, 0, SlotCount - 1);
	if (Stick.SizeSquared() < FMath::Square(FMath::Max(0.0f, DeadZone)))
	{
		return SafeCurrent;
	}

	// atan2(X,Y) puts zero at twelve o'clock. Positive angles travel clockwise in Slate/gamepad
	// screen space: right is one quarter-turn after up.
	const float Angle = FMath::Atan2(Stick.X, Stick.Y);
	const float SlotFloat = Angle * static_cast<float>(SlotCount) / (2.0f * UE_PI);
	const int32 Slot = FMath::RoundToInt(SlotFloat);
	return (Slot % SlotCount + SlotCount) % SlotCount;
}
}
