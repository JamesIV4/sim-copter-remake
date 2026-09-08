// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/SimCopterControllerInput.h"
#include "Ground/SimCopterDispatch.h"

namespace SimCopterControllerInput
{
FDispatchSelection GetDispatchSelection(const int32 Slot)
{
	using SimCopterDispatch::EService;
	static const FDispatchSelection Entries[DispatchSlotCount] = {
		{static_cast<int32>(EService::FireTruck), false, TEXT("Fire Truck")},
		{static_cast<int32>(EService::Police), false, TEXT("Police")},
		{static_cast<int32>(EService::Ambulance), false, TEXT("Ambulance")},
		{static_cast<int32>(EService::Police), true, TEXT("Police (Chase)")},
	};
	return Slot >= 0 && Slot < DispatchSlotCount ? Entries[Slot] : FDispatchSelection{INDEX_NONE, false, TEXT("")};
}

bool UpdateAnalogActivity(const float Value, float& LastActiveValue)
{
	const bool bActive = FMath::Abs(Value) > 0.25f && FMath::Abs(Value - LastActiveValue) > 0.1f;
	// Accumulate small movements against the last meaningful position, not the previous frame.
	if (bActive || FMath::Abs(Value) < 0.15f) LastActiveValue = Value;
	return bActive;
}

FFlightRouting ResolveFlightRouting(
	const float LeftStickX,
	const float LeftStickY,
	const float RightStickY,
	const bool bCameraAdjustHeld,
	const float LeftTriggerValue,
	const float RightTriggerValue,
	const bool bClimbHeld,
	const bool bDescendHeld)
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
		((bRightTriggerHeld || bClimbHeld) ? 1 : 0) -
		((FMath::Clamp(LeftTriggerValue, 0.0f, 1.0f) > TriggerPressedThreshold || bDescendHeld) ? 1 : 0);

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

FVector2D GetRadialSlotDirection(const int32 Index, const int32 SlotCount)
{
	if (SlotCount <= 0) return FVector2D::ZeroVector;
	const float Angle = 2.0f * UE_PI * static_cast<float>(Index) / SlotCount;
	// Slate Y is down; physical gamepad Y is up. Painting and picking share this layout.
	return FVector2D(FMath::Sin(Angle), -FMath::Cos(Angle));
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
		return INDEX_NONE;
	}

	const FVector2D ScreenDirection(Stick.X, -Stick.Y);
	int32 BestIndex = SafeCurrent;
	double BestDot = -TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < SlotCount; ++Index)
	{
		const double Dot = FVector2D::DotProduct(ScreenDirection, GetRadialSlotDirection(Index, SlotCount));
		if (Dot > BestDot)
		{
			BestDot = Dot;
			BestIndex = Index;
		}
	}
	return BestIndex;
}
}
