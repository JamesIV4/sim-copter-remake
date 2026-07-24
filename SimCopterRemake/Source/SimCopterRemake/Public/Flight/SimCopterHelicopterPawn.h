// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Flight/SimCopterFlightModel.h"
#include "GameFramework/Pawn.h"
#include "UObject/NoExportTypes.h"
#include "SimCopterHelicopterPawn.generated.h"

class UCameraComponent;
class UCapsuleComponent;
class UMaterialInterface;
class UProceduralMeshComponent;
class USceneComponent;
class USplineMeshComponent;
class USpotLightComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class UTexture2D;
class USimCopterParticleFXComponent;
class ASimCity2000CityActor;
class ASimCopterMissionSystemActor;
class ASimCopterOnFootPawn;
class APlayerController;
class SHorizontalBox;
class SProgressBar;
class STextBlock;
class SWidget;
class FReply;
struct FSlateBrush;

UENUM(BlueprintType)
enum class ESimCopterCameraMode : uint8
{
	Chase,
	Orbit,
	Rescue
};

UENUM(BlueprintType)
enum class ESimCopterMissionPassengerKind : uint8
{
	Transport,
	Medevac,
	Rescue
};

USTRUCT(BlueprintType)
struct SIMCOPTERREMAKE_API FSimCopterMissionPassengerSlot
{
	GENERATED_BODY()

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Missions")
	int32 EventId = INDEX_NONE;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Missions")
	ESimCopterMissionPassengerKind Kind = ESimCopterMissionPassengerKind::Transport;
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
	float BucketFillPoundsPerFrame = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Rope")
	float BucketDumpPoundsPerFrame = 21.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Rope")
	float RopeLoadFactor = 102.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Rope")
	float RopeTension = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Rope")
	float WaterThrow = 49.0f;

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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
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

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Missions")
	bool CanTransferMissionPassengers() const;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Missions")
	int32 GetPassengerSeatCount() const { return FlightModel.Tuning.PassengerSeats; }

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Missions")
	int32 GetPassengerCount() const { return FlightModel.Passengers; }

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Missions")
	int32 GetAvailablePassengerSeats() const;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Missions")
	int32 AddMissionPassengers(int32 Count);

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Missions")
	int32 RemoveMissionPassengers(int32 Count);

	int32 AddMissionPassengersForMission(int32 Count, int32 EventId, ESimCopterMissionPassengerKind Kind);
	int32 RemoveMissionPassengersForMission(int32 Count, int32 EventId, ESimCopterMissionPassengerKind Kind);
	int32 GetMissionPassengerCount(int32 EventId, ESimCopterMissionPassengerKind Kind) const;
	bool DropPassengerAtSlot(int32 SlotIndex);
	FVector GetPassengerDropWorldLocation(int32 SlotIndex = INDEX_NONE) const;

	const FVector& GetVelocityCmPerSec() const { return VelocityCmPerSec; }
	float GetMaxForwardSpeedCmPerSec() const { return MaxForwardSpeedCmPerSec; }
	int32 GetBucketWaterPounds() const { return BucketWaterPounds; }

	// Debug-only equipment selector used until the career capability record is ported.
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Debug")
	void ToggleDebugWaterEquipment();

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Debug")
	bool IsDebugWaterCannonSelected() const { return bDebugWaterCannonSelected; }

	// The decompiled flight simulation state (read-only; for HUD and tests).
	const FSimCopterFlightModel& GetFlightModel() const { return FlightModel; }

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
	TArray<TObjectPtr<USplineMeshComponent>> RopeSegmentComponents;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UStaticMeshComponent> BucketMeshComponent;

	// Original SimCopter BUCKET object (GEO id 0x7b). The static cube above remains only as
	// a fallback when the original GEO packs are unavailable.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UProceduralMeshComponent> OriginalBucketMeshComponent;

	// Renders the original water effects: bucket water drips + douse steam (from the bucket) and
	// the rotor-wash "wind kickback" spray/dust under the helicopter.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<USimCopterParticleFXComponent> WaterFXComponent;

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

	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "0.0"))
	float PassengerDropSideOffsetCm = 175.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "0.0"))
	float PassengerDropForwardOffsetCm = 35.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "0.0"))
	float PassengerDropVerticalOffsetCm = 55.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "1.0"))
	float PassengerFallInjuryDistanceCm = 900.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Missions")
	TArray<FSimCopterMissionPassengerSlot> MissionPassengerSlots;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> PassengerSlotIconTexture;

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

	UPROPERTY(EditAnywhere, Category = "SimCopter|Model", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RotorDiscAlphaScale = 0.5f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Search Light")
	bool bSearchLightStartsEnabled = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Search Light", meta = (ClampMin = "0.0"))
	float SearchLightIntensity = 650000.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Search Light", meta = (ClampMin = "100.0"))
	float SearchLightRangeCm = 5200.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Search Light", meta = (ClampMin = "100.0"))
	float SearchLightBeamLengthCm = 4700.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Search Light", meta = (ClampMin = "20.0"))
	float SearchLightBeamWidthCm = 1550.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Search Light")
	FLinearColor SearchLightBeamColor = FLinearColor(1.0f, 0.94f, 0.58f, 1.0f);

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

	// Derived from the decompiled flight model for HUD/camera scaling: the
	// original's airspeed equals the smoothed pitch angle in tenth-degrees, so
	// top speed = MaxPitch * 0.610 world-units/s.
	UPROPERTY(VisibleAnywhere, Category = "SimCopter|Flight")
	float MaxForwardSpeedCmPerSec = 2850.0f;

	// Centimetres per original world unit. The original city tile is 64 units;
	// the remake renders tiles at 400 cm, giving 6.25 cm per unit. All flight
	// model distances/speeds convert through this scale.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Flight", meta = (ClampMin = "0.01"))
	float OriginalUnitToCm = 6.25f;

	// Terrain steeper than this normal (cosine) is "not flat" for landing;
	// mirrors the original's 9-units-across-a-tile corner test (about 8 deg).
	UPROPERTY(EditAnywhere, Category = "SimCopter|Flight", meta = (ClampMin = "0.5", ClampMax = "1.0"))
	float LandingFlatNormalZ = 0.99f;

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

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Rope")
	float RopeLengthCm = 0.0f;

	// Attachment point under the fuselage, in the banking model-pivot frame.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Rope")
	FVector RopeAnchorOffsetCm = FVector(0.0f, 0.0f, -55.0f);

	// The decoded capability bit is 0x10, but ownership of that bit still belongs to the unported
	// career equipment record. Expose the resolved capability without assigning it to a guessed
	// helicopter type.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Rope")
	bool bWaterCannonInstalled = false;

	// --- Rotor wash "wind kickback" (FUN_004881b0) ---
	// Enable the downwash effect cards under the rotor when flying low.
	UPROPERTY(EditAnywhere, Category = "SimCopter|RotorWash")
	bool bEnableRotorWash = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera")
	ESimCopterCameraMode CameraMode = ESimCopterCameraMode::Chase;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "100.0"))
	float ChaseCameraMinDistance = 720.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "100.0"))
	float ChaseCameraMaxDistance = 1350.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera")
	float ChaseCameraBasePitch = -14.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera")
	float ChaseCameraTargetHeightCm = -130.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera")
	float ChaseCameraSpeedTargetLiftCm = 20.0f;

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

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.0"))
	float CameraGroundClearanceCm = 24.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "1.0"))
	float CameraGroundProbeUpCm = 2000.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "1.0"))
	float CameraGroundProbeDownCm = 6000.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.0"))
	float CameraObstructionPaddingCm = 18.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.0"))
	float CameraMinObstructedArmLengthCm = 0.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.1"))
	float CameraGroundLiftHeightCm = 250.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "1.0"))
	float CameraGroundLiftProbeRangeCm = 260.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.1"))
	float CameraGroundLiftLerpSpeed = 7.5f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.1"))
	float CameraObstructionPullInLerpSpeed = 14.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.1"))
	float CameraObstructionReleaseLerpSpeed = 5.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	bool bIsLanded = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	bool bRopeDeployed = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	float BucketWaterFraction = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	int32 BucketWaterPounds = 0;

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
	bool bUsingOriginalBucketMesh = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	FString LastModelLoadError;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ModelVertexColorMaterial;

	// Translucent grey material for the spinning rotor disc (Maxis face type 11).
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> RotorDiscMaterial;

private:
	// The decompiled original flight simulation; the pawn feeds it inputs and
	// city geometry and mirrors its position/attitude onto the actor.
	FSimCopterFlightModel FlightModel;
	FSimCopterFlightEvents LastFlightEvents;
	bool bFlightModelSeeded = false;

	FVector VelocityCmPerSec = FVector::ZeroVector;
	float CurrentPitchDeg = 0.0f;
	float CurrentRollDeg = 0.0f;
	float CameraYawOffsetDeg = 0.0f;
	float CameraPitchOffsetDeg = 0.0f;
	float CameraZoomAlpha = 0.25f;
	float CurrentCameraArmLengthCm = 900.0f;
	float CurrentCameraGroundLiftCm = 0.0f;

	float PitchInput = 0.0f;
	float RollInput = 0.0f;
	float YawInput = 0.0f;
	float CollectiveInput = 0.0f;
	float CameraYawInput = 0.0f;
	float CameraPitchInput = 0.0f;
	float MouseLookYawInput = 0.0f;
	float MouseLookPitchInput = 0.0f;
	float RopeAdjustInput = 0.0f;
	bool bBucketDumpHeld = false;
	bool bWaterCannonHeld = false;
	bool bPrimaryWaterUseHeld = false;
	bool bDebugWaterCannonSelected = false;

	// Cached world actors used by the water trajectories and terrain queries.
	TWeakObjectPtr<ASimCopterMissionSystemActor> CachedMissionSystem;
	mutable TWeakObjectPtr<ASimCity2000CityActor> CachedCityActor;

	TArray<FVector> RopeNodeWorldPositions;
	int32 RopeFirstActiveNode = 17;
	bool bRopeStateInitialized = false;
	FVector PreviousRopeAnchorWorld = FVector::ZeroVector;
	FVector PreviousBucketWorld = FVector::ZeroVector;
	FVector PreviousRopeEndDirection = -FVector::UpVector;
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

	// Mesh sections holding the face-type-11 rotor blur discs; shown only when
	// the rotor is at lift speed (original: RPM >= 300 toggles those faces).
	int32 MainRotorDiscSectionIndex = INDEX_NONE;
	int32 TailRotorDiscSectionIndex = INDEX_NONE;
	TSharedPtr<SWidget> PassengerSlotsWidget;
	TSharedPtr<SHorizontalBox> PassengerSlotsBox;
	TSharedPtr<FSlateBrush> PassengerSlotIconBrush;
	TSharedPtr<SWidget> WaterControlsWidget;
	TSharedPtr<SProgressBar> WaterCapacityBar;
	TSharedPtr<STextBlock> WaterCapacityText;
	TSharedPtr<STextBlock> WaterControlsText;

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
	void StartPrimaryWaterUse();
	void StopPrimaryWaterUse();
	void StartBucketDump();
	void StopBucketDump();
	void StartWaterCannon();
	void StopWaterCannon();
	void StartEngineHold();
	void StopEngineHold();
	void StartEngineShutdownHold();
	void StopEngineShutdownHold();
	void Interact();
	void UseMegaphone();
	void CycleCameraMode();
	void ToggleSearchLight();

	// Debug console commands (routed through the player pawn) to exercise the fire/water work.
	UFUNCTION(Exec)
	void SimForceFire();
	UFUNCTION(Exec)
	void SimForceCarFire();

	void UpdateEngineState(float DeltaSeconds);
	void SimulateFlightStep(float DeltaSeconds);
	void UpdateGroundProbe();
	void UpdateForwardProbe();
	void SeedFlightModelFromActor();
	void ApplyFlightTuningToModel();
	FSimCopterFlightInputs BuildFlightInputs() const;
	FSimCopterFlightEnvironment BuildFlightEnvironment() const;
	void ApplyFlightModelToActor(float DeltaSeconds);
	void UpdateRopeAndBucket(float DeltaSeconds);
	void InitializeRopeState();
	bool StepRopeState();
	void UpdateRopeVisuals();
	FVector GetRopeAnchorWorldLocation() const;
	void EmitBucketWaterFrame(bool bCollisionSpill);
	void EmitWaterCannonFrame();
	// Rotor-wash "wind kickback" (port of FUN_004881b0): when the helicopter is low over a
	// surface and above the minimum altitude, scatter effect cards under the rotor - spray over
	// water, dust over land. Also emits the bucket douse steam accumulator.
	void UpdateRotorWash(float DeltaSeconds);
	ASimCopterMissionSystemActor* ResolveMissionSystem();
	ASimCity2000CityActor* ResolveCityActor() const;
	void UpdateVisuals(float DeltaSeconds);
	void UpdateCamera(float DeltaSeconds);
	void UpdateSearchLightEffect();
	float ResolveCameraGroundLift(
		const FVector& BoomOrigin,
		float ArmLength,
		const FRotator& WorldRotation,
		float& OutRequiredLiftCm) const;
	float ResolveCameraArmLengthForObstruction(
		const FVector& BoomOrigin,
		float DesiredArmLength,
		const FRotator& WorldRotation) const;
	bool ProbeBucketWater(const FVector& BucketWorldLocation);
	FString ResolveOriginalGameRoot() const;
	void ApplyDerivedTuning();
	void SyncPassengerFlightModelCount();
	void EnsurePassengerSlotsWidget();
	void RemovePassengerSlotsWidget();
	void RefreshPassengerSlotsWidget();
	void EnsureWaterControlsWidget();
	void RemoveWaterControlsWidget();
	void RefreshWaterControlsWidget();
	bool LoadPassengerSlotIconTexture();
	FReply HandlePassengerSlotClicked(int32 SlotIndex);
	FVector GetPassengerAirDropWorldLocation(int32 SlotIndex) const;

	// Resolves the GEO table names (fuselage + main rotor) for HelicopterTypeName.
	// Returns false when the name is not a known flyable helicopter.
	static bool GetHelicopterMeshNames(const FString& TypeName, FString& OutBodyName, FString& OutMainRotorName);
	void ShowOriginalMesh(bool bUseOriginalMesh);
};
