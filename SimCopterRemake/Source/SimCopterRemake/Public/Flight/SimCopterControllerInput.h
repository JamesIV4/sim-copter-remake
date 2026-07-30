// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Pure controller-routing helpers. Keeping the context switch out of the pawn makes the
// modifier behavior independently testable: the same physical controls must never feed flight
// and camera adjustment during the same frame.
namespace SimCopterControllerInput
{
constexpr float RadialDeadZone = 0.45f;
constexpr float TriggerPressedThreshold = 0.25f;

struct SIMCOPTERREMAKE_API FFlightRouting
{
	// Original joystick percentage range. Pitch is inverted here because the original joystick
	// path expects forward stick as a negative axis (FUN_00485f50).
	int32 PitchAxisPercent = 0;
	int32 TurnAxisPercent = 0;
	int32 SlideAxisPercent = 0;

	// +1 is up, -1 is down. The flight model's collective is digital in the original even when
	// the attitude controls come from analog joystick axes.
	int32 CollectiveCommand = 0;

	// Screen-space framing adjustment while R3 is held. +1 means move the helicopter upward in
	// the frame; the pawn moves the camera the opposite way to achieve it.
	int32 CameraVerticalCommand = 0;

	// Raw right-stick Y while R3 is held: positive (stick up) zooms in.
	float CameraZoomCommand = 0.0f;
};

// Left stick is the analog equivalent of WASD. R3 is also the missing lateral-slide modifier:
// while held, left X feeds the original joystick slide axis instead of the coordinated turn
// axis. RB is up and RT is down; with R3 held that same pair adjusts framing instead of lift.
SIMCOPTERREMAKE_API FFlightRouting ResolveFlightRouting(
	float LeftStickX,
	float LeftStickY,
	float RightStickY,
	bool bCameraAdjustHeld,
	bool bRightShoulderHeld,
	float RightTriggerValue);

// Radial slot zero is at twelve o'clock and the remaining slots proceed clockwise. Returning
// CurrentIndex inside the dead zone keeps a wheel from jumping when it first opens.
SIMCOPTERREMAKE_API int32 ResolveRadialIndex(
	const FVector2D& Stick,
	int32 SlotCount,
	int32 CurrentIndex,
	float DeadZone = RadialDeadZone);
}
