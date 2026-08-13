// Copyright Epic Games, Inc. All Rights Reserved.

#include "Replay/SimCopterReplayFreeCamera.h"

#include "Camera/CameraComponent.h"

ASimCopterReplayFreeCamera::ASimCopterReplayFreeCamera()
{
	PrimaryActorTick.bCanEverTick = true;
	// Reviewing a clip pauses the sim (the mission clock, the behaviour VM and the traffic all have
	// to stop or playback would be fighting them for the same actors), so every part of the replay
	// camera has to run through a pause or the operator cannot frame a shot.
	PrimaryActorTick.bTickEvenWhenPaused = true;
	// Ahead of the ground agents, so a camera bolted to nothing still updates before the frame is
	// composed. It has no dependencies of its own.
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	SetCanBeDamaged(false);

	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FreeCamera"));
	SetRootComponent(CameraComponent);
	CameraComponent->bConstrainAspectRatio = false;
	CameraComponent->SetFieldOfView(TargetFovDegrees);
	// The free camera is not attached to the aircraft, so there is nothing for the engine's
	// "use pawn control rotation" path to read - this actor owns its own orientation outright.
	CameraComponent->bUsePawnControlRotation = false;
}

void ASimCopterReplayFreeCamera::SnapTo(const FVector& WorldLocation, const FRotator& WorldRotation)
{
	SetActorLocation(WorldLocation);

	// Free-cam roll is always zero: a rolled camera reads as a mistake in every shot that is not
	// deliberately Dutch-angled, and nothing here offers a roll control.
	const FRotator Levelled(WorldRotation.Pitch, WorldRotation.Yaw, 0.0f);
	SetActorRotation(Levelled);
	TargetRotation = Levelled;
	SmoothedRotation = Levelled;

	// Snapping is a cut, not a move: any velocity still in the filter would drift the camera away
	// from the pose the caller just asked for.
	SmoothedVelocityCmPerSec = FVector::ZeroVector;
	PendingMoveAxes = FVector::ZeroVector;
	PendingLookDelta = FVector2D::ZeroVector;
}

void ASimCopterReplayFreeCamera::AddMoveInput(const FVector& LocalAxes)
{
	PendingMoveAxes += LocalAxes;
}

void ASimCopterReplayFreeCamera::AddLookInput(const float YawDelta, const float PitchDelta)
{
	PendingLookDelta.X += YawDelta;
	PendingLookDelta.Y += PitchDelta;
}

void ASimCopterReplayFreeCamera::AddFovInput(const float WheelDelta)
{
	if (FMath::IsNearlyZero(WheelDelta))
	{
		return;
	}

	// Multiplicative, so one notch covers the same *proportion* of the range at 15 degrees as at
	// 150. Stepping the angle linearly makes the wide end feel stuck and the narrow end feel like
	// it is skipping, which is very obvious over a 10-180 sweep.
	const float Scale = FMath::Pow(1.0f - FovStepFraction, WheelDelta);
	SetFieldOfView(TargetFovDegrees * Scale);
}

void ASimCopterReplayFreeCamera::SetSmoothingEnabled(const bool bEnabled)
{
	if (bSmoothingEnabled == bEnabled)
	{
		return;
	}
	bSmoothingEnabled = bEnabled;

	// Turning smoothing ON from a camera that is already moving would otherwise start the heavy
	// filter with a spike of velocity in it and glide for a second after the key is released.
	if (bSmoothingEnabled)
	{
		SmoothedVelocityCmPerSec = FVector::ZeroVector;
		TargetRotation = GetActorRotation();
		SmoothedRotation = TargetRotation;
	}
}

void ASimCopterReplayFreeCamera::SetFieldOfView(const float Degrees)
{
	TargetFovDegrees = FMath::Clamp(Degrees, MinFovDegrees, MaxFovDegrees);
}

void ASimCopterReplayFreeCamera::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// REAL time, not the tick's delta. Reviewing a clip freezes the world with near-zero global
	// time dilation (see USimCopterReplaySubsystem::FreezeWorldForReview), which reaches this
	// actor's tick too - driving the camera from DeltaSeconds would leave it immovable during the
	// one mode it exists for. Clamped, so a hitch (a clip loading, a level streaming in) cannot
	// throw the camera across the city.
	const double NowRealSeconds = FPlatformTime::Seconds();
	const float Delta = LastRealTimeSeconds > 0.0
		? static_cast<float>(FMath::Clamp(NowRealSeconds - LastRealTimeSeconds, 0.0, 0.1))
		: 0.0f;
	LastRealTimeSeconds = NowRealSeconds;
	if (Delta <= 0.0f)
	{
		return;
	}

	// --- look ---

	const float LookInterp = bSmoothingEnabled ? SmoothLookInterpSpeed : RoughInterpSpeed;
	TargetRotation.Yaw += PendingLookDelta.X * LookSensitivity;
	TargetRotation.Pitch = FMath::Clamp(
		TargetRotation.Pitch + PendingLookDelta.Y * LookSensitivity,
		// Short of straight up and straight down: at the poles yaw and roll become the same axis
		// and the camera gimbal-flips mid-shot.
		-89.0f,
		89.0f);
	TargetRotation.Roll = 0.0f;
	PendingLookDelta = FVector2D::ZeroVector;

	SmoothedRotation = FMath::RInterpTo(SmoothedRotation, TargetRotation, Delta, LookInterp);
	SetActorRotation(SmoothedRotation);

	// --- movement ---

	// Forward and right come from where the camera is looking, so pushing W flies into frame.
	// Up is world up rather than camera up, which is what makes Ctrl/Space read as "down" and "up"
	// rather than "toward the floor of the shot" while the camera is pitched.
	FVector Axes = PendingMoveAxes;
	PendingMoveAxes = FVector::ZeroVector;
	Axes.X = FMath::Clamp(Axes.X, -1.0f, 1.0f);
	Axes.Y = FMath::Clamp(Axes.Y, -1.0f, 1.0f);
	Axes.Z = FMath::Clamp(Axes.Z, -1.0f, 1.0f);

	const FRotator YawOnly(0.0f, SmoothedRotation.Yaw, 0.0f);
	const FVector Forward = SmoothedRotation.Vector();
	const FVector Right = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::Y);

	FVector DesiredDirection = Forward * Axes.X + Right * Axes.Y + FVector::UpVector * Axes.Z;
	// Normalising rather than summing keeps a diagonal the same speed as a straight line; a
	// diagonal that is 41% faster is visible in a slow dolly.
	if (!DesiredDirection.IsNearlyZero())
	{
		DesiredDirection.Normalize();
	}

	const float Speed = MoveSpeedCmPerSec * (bBoostActive ? BoostMultiplier : 1.0f);
	const FVector DesiredVelocity = DesiredDirection * Speed;
	const float MoveInterp = bSmoothingEnabled ? SmoothMoveInterpSpeed : RoughInterpSpeed;
	SmoothedVelocityCmPerSec = FMath::VInterpTo(SmoothedVelocityCmPerSec, DesiredVelocity, Delta, MoveInterp);

	// Below a millimetre a second the filter is just holding a rounding error; snapping it to zero
	// is what makes the camera actually stop instead of creeping for the rest of the shot.
	if (SmoothedVelocityCmPerSec.SizeSquared() < 0.01f)
	{
		SmoothedVelocityCmPerSec = FVector::ZeroVector;
	}
	else
	{
		// No sweep and no collision: a camera that stops at a wall cannot get the shot from inside
		// the building, and the free camera is a tool, not a pawn.
		AddActorWorldOffset(SmoothedVelocityCmPerSec * Delta, /*bSweep=*/false);
	}

	// --- field of view ---

	CurrentFovDegrees = FMath::FInterpTo(CurrentFovDegrees, TargetFovDegrees, Delta, FovInterpSpeed);
	if (CameraComponent != nullptr)
	{
		CameraComponent->SetFieldOfView(CurrentFovDegrees);
	}
}
