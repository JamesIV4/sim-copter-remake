// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "UObject/NoExportTypes.h"
#include "SimCopterHelicopterPawn.generated.h"

class UCameraComponent;
class UCapsuleComponent;
class UMaterialInterface;
class UProceduralMeshComponent;
class USceneComponent;
class USpotLightComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class ASimCopterOnFootPawn;
class APlayerController;

UENUM(BlueprintType)
enum class ESimCopterCameraMode : uint8
{
	Chase,
	Orbit,
	Rescue
};

USTRUCT(BlueprintType)
struct SIMCOPTERREMAKE_API FSimCopterHelicopterTypeTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Flight")
	float MaxBankDeg = 42.67f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Flight")
	float MaxSlideDeg = 14.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Flight")
	float MaxPitchDeg = 19.23f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Flight")
	float PitchRateDegPerSec = 45.27f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Flight")
	float YawAccelDegPerSec = 105.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Flight")
	float RollRateDegPerSec = 20.97f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Flight")
	float SlideResponse = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Flight")
	float ClimbRateCmPerSec = 710.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Flight")
	int32 MaxLoadPounds = 1548;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Flight")
	float MaxYawRateDegPerSec = 58.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Flight")
	float FuelRateGallonsPerHour = 230.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Flight")
	int32 NewCostDollars = 7800;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Flight")
	int32 MaxDamage = 604;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Flight")
	float FuelGallons = 91.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Flight")
	float RepairRatePerDamage = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Flight")
	float FuelCostPerGallon = 3.0f;
};

USTRUCT(BlueprintType)
struct SIMCOPTERREMAKE_API FSimCopterLandingTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Landing")
	float MaxPitchDeg = 5.16f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Landing")
	float MaxRollDeg = 4.33f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Landing")
	float MaxHorizontalSpeedCmPerSec = 1052.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Landing")
	float MaxVerticalSpeedCmPerSec = 502.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Landing")
	float MaxDescentRateCmPerSec = 485.0f;
};

USTRUCT(BlueprintType)
struct SIMCOPTERREMAKE_API FSimCopterRopeTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Rope")
	float BucketFillPerSec = 0.30f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Rope")
	float BucketDumpPerSec = 0.21f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Rope")
	float RopeLoadFactor = 102.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Rope")
	float RopeTension = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Rope")
	float WaterThrowCmPerSec = 4900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Rope")
	float CannonForce = 128.6f;
};

USTRUCT(BlueprintType)
struct SIMCOPTERREMAKE_API FSimCopterDamageTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Damage")
	float MinFireAltitudeCm = -4800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Damage")
	float MaxFireAltitudeCm = 6110.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Damage")
	float DepreciateDollarsPerSec = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Damage")
	float CollisionDamageScale = 27.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Damage")
	float RepairDistanceValue = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Damage")
	float FuelDistanceValue = 25.0f;
};

UCLASS()
class SIMCOPTERREMAKE_API ASimCopterHelicopterPawn : public APawn
{
	GENERATED_BODY()

public:
	ASimCopterHelicopterPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Flight")
	bool LoadTuningFromOriginalGameRoot();

	// Loads the original SimCopter fuselage + rotor meshes for HelicopterTypeName from the
	// GEO packs and binds them to the procedural mesh components. Returns false (and keeps
	// the placeholder geometry visible) if the original assets are unavailable.
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "SimCopter|Model")
	bool LoadHelicopterMeshFromOriginalGameRoot();

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Flight")
	void ResetAircraft();

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Flight")
	float GetFuelFraction() const;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Flight")
	float GetDamageFraction() const;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Interaction")
	bool CanBeEnteredBy(const FVector& WorldLocation, float RadiusCm) const;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Interaction")
	void EnterHelicopter(APlayerController* PlayerController);

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Interaction")
	bool CanExitHelicopter() const;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Interaction")
	void ExitHelicopter();

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Flight")
	bool IsEngineRunning() const { return bEngineRunning; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UCapsuleComponent> CollisionComponent;

	// Tilts with the helicopter's pitch/roll. Parents both the placeholder geometry and
	// the original-mesh geometry so banking is shared by whichever is visible.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<USceneComponent> ModelPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UStaticMeshComponent> BodyMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UStaticMeshComponent> MainRotorMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UStaticMeshComponent> TailRotorMeshComponent;

	// Original SimCopter fuselage mesh (replaces the placeholder body when loaded).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UProceduralMeshComponent> HeliBodyMeshComponent;

	// Original SimCopter main rotor mesh; spun about the mast each frame.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UProceduralMeshComponent> HeliMainRotorMeshComponent;

	// Original SimCopter tail rotor mesh (shared ROTORTL object); spun about its hub.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UProceduralMeshComponent> HeliTailRotorMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UStaticMeshComponent> RopeMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UStaticMeshComponent> BucketMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UCameraComponent> CameraComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<USpotLightComponent> SearchLightComponent;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Tuning")
	FDirectoryPath OriginalGameRoot;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Tuning")
	FString HelicopterTypeName = TEXT("Jet Ranger");

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Tuning")
	bool bLoadTuningOnBeginPlay = true;

	// Loads the original fuselage/rotor meshes from the GEO packs on BeginPlay.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Model")
	bool bLoadHelicopterMeshOnBeginPlay = true;

	// Renders the shared ROTORTL tail rotor at the tail and spins it.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Model")
	bool bShowSeparateTailRotor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Engine")
	bool bEngineRunning = false;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Engine", meta = (ClampMin = "0.0"))
	float EngineStartHoldSeconds = 0.85f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Engine", meta = (ClampMin = "0.0"))
	float EngineShutdownHoldSeconds = 0.8f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Interaction")
	TSubclassOf<ASimCopterOnFootPawn> ExitPawnClass;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Interaction")
	FVector ExitOffset = FVector(180.0f, 175.0f, 0.0f);

	// Mesh units per centimetre for the GEO packs (matches the city renderer's value).
	UPROPERTY(EditAnywhere, Category = "SimCopter|Model", meta = (ClampMin = "1.0"))
	float ModelUnitsPerCentimeter = 2621.44f;

	// Display scale applied to the loaded mesh. Defaults to 0.25 so the helicopter matches
	// the city's original-mesh scale (TileSize 400 / source tile 1600).
	UPROPERTY(EditAnywhere, Category = "SimCopter|Model", meta = (ClampMin = "0.001"))
	float ModelScale = 0.25f;

	// Adds reversed triangles so faces are visible from both sides (matches the city default).
	UPROPERTY(EditAnywhere, Category = "SimCopter|Model")
	bool bRenderModelBackfaces = true;

	// Main rotor revolutions per second while the engine is running.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Model", meta = (ClampMin = "0.0"))
	float MainRotorRevsPerSec = 4.5f;

	// Tail rotor spin speed relative to the main rotor.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Model", meta = (ClampMin = "0.0"))
	float TailRotorSpeedMultiplier = 3.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Flight")
	FSimCopterHelicopterTypeTuning HelicopterTuning;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Flight")
	FSimCopterLandingTuning LandingTuning;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Flight")
	FSimCopterRopeTuning RopeTuning;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Flight")
	FSimCopterDamageTuning DamageTuning;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Flight", meta = (ClampMin = "1.0"))
	float TweakAngleScale = 0.1f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Flight", meta = (ClampMin = "1.0"))
	float TweakSpeedToCmPerSec = 25.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Flight", meta = (ClampMin = "1.0"))
	float TweakClimbToCmPerSec = 100.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Flight", meta = (ClampMin = "1.0"))
	float TweakAltitudeToCm = 100.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Flight", meta = (ClampMin = "1.0"))
	float MaxForwardSpeedCmPerSec = 2850.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Flight", meta = (ClampMin = "1.0"))
	float MaxReverseSpeedFraction = 0.45f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Flight", meta = (ClampMin = "1.0"))
	float MaxSlideSpeedCmPerSec = 1350.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Flight", meta = (ClampMin = "0.0"))
	float HoverDamping = 2.75f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Collision", meta = (ClampMin = "1.0"))
	float LandingProbeDistance = 320.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Collision", meta = (ClampMin = "0.0"))
	float GroundContactTolerance = 28.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Collision", meta = (ClampMin = "1.0"))
	float CollisionProbeDistance = 320.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Collision", meta = (ClampMin = "1.0"))
	float CollisionProbeRadius = 75.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Collision")
	bool bDrawDebugProbes = false;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Rope", meta = (ClampMin = "100.0"))
	float RopeLengthCm = 650.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Rope", meta = (ClampMin = "100.0"))
	float MinRopeLengthCm = 220.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Rope", meta = (ClampMin = "100.0"))
	float MaxRopeLengthCm = 1100.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Rope", meta = (ClampMin = "1.0"))
	float RopeAdjustCmPerSec = 320.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Rope")
	float WaterFillWorldZ = 0.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera")
	ESimCopterCameraMode CameraMode = ESimCopterCameraMode::Chase;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "100.0"))
	float ChaseCameraMinDistance = 720.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "100.0"))
	float ChaseCameraMaxDistance = 1350.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera")
	float ChaseCameraBasePitch = -14.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera")
	float RescueCameraPitch = -62.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "1.0"))
	float CameraYawSpeedDegPerSec = 135.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "1.0"))
	float CameraPitchSpeedDegPerSec = 90.0f;

	// How far (cm) the chase camera eases back at full forward speed. Kept small so forward
	// flight only nudges the camera back a little instead of pulling way out.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.0"))
	float ChaseSpeedPullbackCm = 120.0f;

	// Seconds the mouse-drag camera offset is held after the button is released before it
	// eases back to center.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.0"))
	float CameraRecenterDelaySeconds = 1.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	bool bIsLanded = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	bool bRopeDeployed = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	float BucketWaterFraction = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	float CurrentFuelGallons = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	float CurrentDamage = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	float GroundClearanceCm = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	float ForwardObstacleDistanceCm = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	float EngineStartHoldAlpha = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	float EngineShutdownHoldAlpha = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	FString LastTuningLoadError;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	bool bUsingOriginalMesh = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	FString LastModelLoadError;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ModelVertexColorMaterial;

	// Translucent grey material for the spinning rotor disc (Maxis face type 11).
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> RotorDiscMaterial;

private:
	FVector VelocityCmPerSec = FVector::ZeroVector;
	float CurrentPitchDeg = 0.0f;
	float CurrentRollDeg = 0.0f;
	float CurrentYawRateDegPerSec = 0.0f;
	float RotorSpinDeg = 0.0f;
	float CameraYawOffsetDeg = 0.0f;
	float CameraPitchOffsetDeg = 0.0f;
	float CameraZoomAlpha = 0.25f;

	float PitchInput = 0.0f;
	float RollInput = 0.0f;
	float YawInput = 0.0f;
	float CollectiveInput = 0.0f;
	float CameraYawInput = 0.0f;
	float CameraPitchInput = 0.0f;
	float MouseLookYawInput = 0.0f;
	float MouseLookPitchInput = 0.0f;
	float RopeAdjustInput = 0.0f;
	bool bBucketFillHeld = false;
	bool bBucketDumpHeld = false;
	bool bEngineStartHeld = false;
	bool bEngineShutdownHeld = false;
	float EngineStartHoldElapsed = 0.0f;
	float EngineShutdownHoldElapsed = 0.0f;

	// Mouse-drag camera control: the camera only follows the mouse while a mouse button is
	// held, and recenters CameraRecenterDelaySeconds after release.
	int32 CameraDragButtonCount = 0;
	bool bCameraDragActive = false;
	float CameraRecenterDelayRemaining = 0.0f;

	FHitResult LastGroundHit;
	FHitResult LastForwardProbeHit;

	void MovePitch(float Value);
	void MoveRoll(float Value);
	void MoveYaw(float Value);
	void MoveCollective(float Value);
	void LookYaw(float Value);
	void LookPitch(float Value);
	void MouseLookYaw(float Value);
	void MouseLookPitch(float Value);
	void StartCameraDrag();
	void StopCameraDrag();
	void ZoomCamera(float Value);
	void AdjustRope(float Value);
	void ToggleRope();
	void StartBucketFill();
	void StopBucketFill();
	void StartBucketDump();
	void StopBucketDump();
	void StartEngineHold();
	void StopEngineHold();
	void StartEngineShutdownHold();
	void StopEngineShutdownHold();
	void Interact();
	void CycleCameraMode();
	void ToggleSearchLight();

	void UpdateEngineState(float DeltaSeconds);
	void SimulateFlightStep(float DeltaSeconds);
	void UpdateGroundProbe();
	void UpdateForwardProbe();
	void UpdateLandingState(float DeltaSeconds);
	void MoveWithCollision(const FVector& DeltaLocation, const FRotator& DesiredRotation, float DeltaSeconds);
	void HandleBlockingHit(const FHitResult& Hit, float DeltaSeconds);
	void UpdateFuel(float DeltaSeconds);
	void UpdateRopeAndBucket(float DeltaSeconds);
	void UpdateVisuals(float DeltaSeconds);
	void UpdateCamera(float DeltaSeconds);
	bool ProbeBucketWater(const FVector& BucketWorldLocation) const;
	FString ResolveOriginalGameRoot() const;
	void ApplyDerivedTuning();

	// Resolves the GEO table names (fuselage + main rotor) for HelicopterTypeName.
	// Returns false when the name is not a known flyable helicopter.
	static bool GetHelicopterMeshNames(const FString& TypeName, FString& OutBodyName, FString& OutMainRotorName);
	void ShowOriginalMesh(bool bUseOriginalMesh);
};
