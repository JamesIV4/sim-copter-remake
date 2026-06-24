// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "UObject/NoExportTypes.h"
#include "SimCopterHelicopterPawn.generated.h"

class UCameraComponent;
class UCapsuleComponent;
class USpotLightComponent;
class USpringArmComponent;
class UStaticMeshComponent;

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

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Flight")
	void ResetAircraft();

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Flight")
	float GetFuelFraction() const;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Flight")
	float GetDamageFraction() const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UCapsuleComponent> CollisionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UStaticMeshComponent> BodyMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UStaticMeshComponent> MainRotorMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UStaticMeshComponent> TailRotorMeshComponent;

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
	FString LastTuningLoadError;

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
	float RopeAdjustInput = 0.0f;
	bool bBucketFillHeld = false;
	bool bBucketDumpHeld = false;

	FHitResult LastGroundHit;
	FHitResult LastForwardProbeHit;

	void MovePitch(float Value);
	void MoveRoll(float Value);
	void MoveYaw(float Value);
	void MoveCollective(float Value);
	void LookYaw(float Value);
	void LookPitch(float Value);
	void ZoomCamera(float Value);
	void AdjustRope(float Value);
	void ToggleRope();
	void StartBucketFill();
	void StopBucketFill();
	void StartBucketDump();
	void StopBucketDump();
	void CycleCameraMode();
	void ToggleSearchLight();

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
};
