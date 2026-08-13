// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimCopterReplayFreeCamera.generated.h"

class UCameraComponent;

/**
 * The replay mode's detached camera.
 *
 * NOT a port - the original has four fixed views bolted to the airframe and nothing like this.
 *
 * It is an ordinary `AActor` with a camera on it rather than a `APawn`, and it is never possessed:
 * possession would swap the player out of the helicopter, and a clip can be reviewed with the
 * aircraft mid-air and still flying. `ASimCopterPlayerController` forwards the already-bound flight
 * axes (`SimCopterPitch` W/S, `SimCopterRoll` A/D, `SimCopterCollective` Space/LeftCtrl) into
 * `AddMoveInput` instead, and disables the pawn's own input while this is live so a key cannot both
 * fly the helicopter and move the camera.
 *
 * Everything here ticks while the game is paused - reviewing a clip pauses the sim, and a camera
 * you cannot move during a pause is not a camera.
 */
UCLASS()
class SIMCOPTERREMAKE_API ASimCopterReplayFreeCamera : public AActor
{
	GENERATED_BODY()

public:
	ASimCopterReplayFreeCamera();

	virtual void Tick(float DeltaSeconds) override;

	UCameraComponent* GetCameraComponent() const { return CameraComponent; }

	/** Drops the camera at a pose, clearing any velocity and smoothing carried from before. */
	void SnapTo(const FVector& WorldLocation, const FRotator& WorldRotation);

	// --- input, pushed in by the player controller each frame ---

	/** Camera-local: X forward, Y right, Z world-up. Components are -1..1. */
	void AddMoveInput(const FVector& LocalAxes);
	/** Degrees of raw mouse delta this frame. Only called while the look button is held. */
	void AddLookInput(float YawDelta, float PitchDelta);
	/** One mouse-wheel notch. Positive narrows the field of view (zooms in). */
	void AddFovInput(float WheelDelta);
	/** Shift/boost: multiplies the movement rate while held. */
	void SetBoostActive(bool bActive) { bBoostActive = bActive; }

	// --- the panel's controls ---

	/**
	 * The "gel" toggle. On, movement and look are run through a heavy critically-damped filter so
	 * a handheld mouse produces a dolly move instead of a twitch; off, the camera answers input
	 * immediately.
	 */
	void SetSmoothingEnabled(bool bEnabled);
	bool IsSmoothingEnabled() const { return bSmoothingEnabled; }

	/** Field of view in degrees. Clamped to [MinFovDegrees, MaxFovDegrees]. */
	void SetFieldOfView(float Degrees);
	float GetFieldOfView() const { return TargetFovDegrees; }

	/** Movement rate at 1x, in cm/s. Persisted by the panel's speed control. */
	void SetMoveSpeedCmPerSec(float Speed) { MoveSpeedCmPerSec = FMath::Max(1.0f, Speed); }
	float GetMoveSpeedCmPerSec() const { return MoveSpeedCmPerSec; }

	/** The range the mouse wheel sweeps, as specified: a fisheye at one end and a long lens at the other. */
	static constexpr float MinFovDegrees = 10.0f;
	static constexpr float MaxFovDegrees = 180.0f;

private:
	UPROPERTY(VisibleAnywhere, Category = "SimCopter|Replay")
	TObjectPtr<UCameraComponent> CameraComponent;

	/** Base rate, before the boost multiplier. A city tile is 400 cm, so this crosses one a second. */
	float MoveSpeedCmPerSec = 1400.0f;
	float BoostMultiplier = 4.0f;
	/** Degrees of rotation per unit of mouse delta. */
	float LookSensitivity = 2.2f;
	/** Degrees of field of view per wheel notch, applied multiplicatively so the sweep feels even. */
	float FovStepFraction = 0.12f;

	// Smoothing rates, in "reach 1-1/e of the remaining distance per second". Rough is what the
	// camera does with smoothing off (still filtered a little, so a 240 Hz frame is not a jolt);
	// Smooth is the molasses.
	float RoughInterpSpeed = 22.0f;
	float SmoothMoveInterpSpeed = 2.2f;
	float SmoothLookInterpSpeed = 3.0f;
	/** The FOV lerp is deliberately the same either way: the wheel should always feel geared. */
	float FovInterpSpeed = 9.0f;

	bool bSmoothingEnabled = false;
	bool bBoostActive = false;

	/**
	 * Real (undilated) clock. A review freezes the world with time dilation rather than a pause, so
	 * this actor's own tick delta is ~0 and cannot drive the camera.
	 */
	double LastRealTimeSeconds = 0.0;

	// Accumulated this frame by the controller, consumed and cleared by Tick.
	FVector PendingMoveAxes = FVector::ZeroVector;
	FVector2D PendingLookDelta = FVector2D::ZeroVector;

	// Filter state. The camera chases these rather than being written directly, which is what the
	// smooth toggle is changing the rate of.
	FVector SmoothedVelocityCmPerSec = FVector::ZeroVector;
	FRotator TargetRotation = FRotator::ZeroRotator;
	FRotator SmoothedRotation = FRotator::ZeroRotator;
	float TargetFovDegrees = 90.0f;
	float CurrentFovDegrees = 90.0f;
};
