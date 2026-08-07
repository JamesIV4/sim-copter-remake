// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Flight/SimCopterFlightModel.h"
#include "Flight/SimCopterHelicopterRegistry.h"
#include "Game/SimCopterCheckup.h"
#include "Flight/SimCopterSpotlight.h"
#include "Flight/SimCopterWinch.h"
#include "GameFramework/Pawn.h"
#include "UObject/NoExportTypes.h"
#include "SimCopterHelicopterPawn.generated.h"

// Staged model data built by PrepareHelicopterModel and consumed by CommitHelicopterModel.
// Defined in Private/Flight/SimCopterPreparedHelicopterModel.h so the public header does
// not pull in the mesh builder.
struct FSimCopterPreparedHelicopterModel;

class UCameraComponent;
class UCapsuleComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class USceneComponent;
class USplineMeshComponent;
class USpotLightComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class UTexture2D;
class UWidgetComponent;
class USimCopterFlashingLightsComponent;
class USimCopterParticleFXComponent;
class USimCopterTearGasPoolComponent;
class USimCopterApachePoolComponent;
class ASimCity2000CityActor;
class ASimCopterMissionSystemActor;
class ASimCopterOnFootPawn;
class APlayerController;
class SHorizontalBox;
class SProgressBar;
class STextBlock;
class SWidget;
class SSimCopterControllerOverlay;
class SSimCopterToolFlaps;
class FReply;
struct FSlateBrush;

UENUM(BlueprintType)
enum class ESimCopterCameraMode : uint8
{
	Chase,
	Orbit,
	Rescue,
	// First person from the pilot's seat: no boom, and a crosshair for aiming the tools.
	Cockpit
};

SIMCOPTERREMAKE_API bool CameraModeShowsCrosshair(ESimCopterCameraMode Mode, bool bIsApache = false);

enum class ESimCopterControllerMode : uint8
{
	None,
	DispatchWheel,
	ToolWheel,
	PassengerSelect,
	PassengerConfirm
};

// Persistent developer adjustment layered over one of the three normal camera views.
// Translation is in helicopter-local centimetres; rotation is in relative degrees.
struct SIMCOPTERREMAKE_API FSimCopterCameraViewDebugOffset
{
	FVector TranslationCm = FVector::ZeroVector;
	FRotator RotationDeg = FRotator::ZeroRotator;
	// 0 disables framing compensation; 1 keeps the helicopter at approximately the same
	// vertical screen position through zoom changes and right-drag camera movement.
	float ZoomVerticalFramingStrength = 1.0f;
	// Zero uses the authored per-view default; a positive value is a persisted developer
	// override for the far end of the zoom range.
	float MaxZoomDistanceCm = 0.0f;
};

UENUM(BlueprintType)
enum class ESimCopterMissionPassengerKind : uint8
{
	Transport,
	Medevac,
	Rescue
};

/**
 * One record of the original's seat manifest, the 16-entry array at DAT_005040d0+0x1d4+0x1c that
 * FUN_0048bff0 fills, FUN_0048c120 clears and FUN_00453f70 draws. Its five ints are:
 *
 *   +0x00  the passenger's head image (copied from person+0x18e when they board)
 *   +0x04  which of people1.bmp's three rows their portrait uses - opcode 54 writes it
 *   +0x08  flags; FUN_004c6250 always passes 0x200 and nothing reads it back
 *   +0x0c  the person id, which is how FUN_0048c0c0 finds a record again
 *   +0x10  the display slot, recomputed by both writers so the seats stay packed
 *
 * The remake keeps the person as an actor handle instead of an id, and lets Unreal's array index
 * be the display slot; the two fields that decide what is drawn are carried verbatim.
 */
USTRUCT(BlueprintType)
struct SIMCOPTERREMAKE_API FSimCopterMissionPassengerSlot
{
	GENERATED_BODY()

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Missions")
	int32 EventId = INDEX_NONE;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Missions")
	ESimCopterMissionPassengerKind Kind = ESimCopterMissionPassengerKind::Transport;

	/** Record +0x00: person+0x18e at the moment they boarded. Portrait column is this plus one. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Missions")
	int32 HeadImageIndex = 0;

	/** Record +0x04. FUN_004c6250 seats everyone at 1; BHAV 264 moves it between 0, 1 and 2. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Missions")
	int32 PortraitState = 1;

	/** Record +0x0c: who is in this seat. Null for a seat filled without a real person actor. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Missions")
	TWeakObjectPtr<class ASimCopterGroundAgent> Person;
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
	// Every possession path lands here, not just EnterHelicopter: the initial spawn, a console
	// command, and climbing back in after a job on foot.
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Flight")
	bool LoadTuningFromOriginalGameRoot();

	// Loads the original SimCopter fuselage + rotor meshes for HelicopterTypeName from the
	// GEO packs and binds them to the procedural mesh components. Returns false (and keeps
	// the placeholder geometry visible) if the original assets are unavailable.
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "SimCopter|Model")
	bool LoadHelicopterMeshFromOriginalGameRoot();

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Flight")
	void ResetAircraft();

	// SCHOOK: HelicopterPlaceOnPad 0x00484790
	// Park the helicopter on an airport helipad, the way city entry (FUN_0047a240) and the
	// crash respawn (FUN_0048a8b0) both do: the aircraft is moved to the pad's own position
	// with its attitude zeroed, not flown there. PadSurfaceWorldLocation is the top of the pad;
	// the aircraft settles the same 1.2 units above it that a landing leaves it at.
	void PlaceOnHelipad(const FVector& PadSurfaceWorldLocation, float YawDegrees);

	// Where PlaceOnHelipad would put the actor's origin for that pad - so a pawn standing next
	// to the aircraft can be put on the same ground.
	float GetHelipadRestingOriginOffsetCm() const;

	// SCHOOK: HelicopterCrashRespawn 0x0048a8b0
	// Put a wrecked aircraft back on a free airport pad. False when the city has no airport or
	// every pad is blocked, in which case the caller leaves it where it fell.
	bool ReturnToAirportAfterCrash();

	// How close another aircraft has to be to a pad for it to count as parked on it. Pads are one
	// tile across, so anything inside half a tile is on it.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Flight", meta = (ClampMin = "0.0"))
	float CrashRespawnPadClearanceCm = 200.0f;

	// How long the death spiral may run without reaching the ground before the aircraft is
	// recovered anyway. Nothing in the original can reach that state; see UpdateStuckFallWatchdog.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Flight", meta = (ClampMin = "1.0"))
	float StuckFallRecoverySeconds = 15.0f;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Flight")
	float GetFuelFraction() const;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Flight")
	float GetDamageFraction() const;

	// Applies the durable aircraft portion of a save after the game mode has parked the freshly
	// spawned aircraft. Position, velocity, passengers, rope and active missions intentionally
	// restart at the airport; this restores the career-owned machine and its service condition.
	void RestoreSavedCareerState(
		int32 TypeIndex,
		int32 CareerEquipmentMask,
		int32 CareerTearGasRounds,
		float FuelFraction,
		float DamageFraction,
		int32 SelectedToolIndex);

	// Exact live-aircraft half of the original BOMB payload: transform, fixed-point flight
	// integrator, camera, winch/bucket, tool state and the seat manifest. The normal career
	// restore selects/loads the airframe first; this resumes the in-world state on that model.
	bool CaptureRuntimeSaveState(TArray<uint8>& OutData);
	bool RestoreRuntimeSaveState(const TArray<uint8>& Data);
	void RelinkSavedMissionPassenger(class ASimCopterGroundAgent* Person, FName SavedActorName = NAME_None);

	// The two readings the instrument panel takes straight off the flight model, both in the
	// original's own world units (64 per city tile, node +0x1c "Y" for altitude and [0x37] for
	// speed). The KNOTS face is graduated in these, not in physical knots: the fastest airframe
	// reaches roughly the 250 at the top left of the dial.
	float GetAltimeterUnits() const;
	float GetAirspeedDialKnots() const;

	// The rendered fuselage's own box, in ModelPivot's frame (so it banks with the airframe and
	// carries the offset that puts the skids at the bottom of the collision capsule). This is the
	// visible aircraft, rotors excluded - what "next to the helicopter" has to mean for anyone
	// walking up to it. False before any body geometry has been built.
	bool TryGetAirframeLocalBoundsCm(FBox& OutLocalBoundsCm) const;

	// Gap in centimetres from a world point to that box; zero when the point is inside it.
	// bHorizontalOnly measures across the deck only, which is what a walker standing on the same
	// surface wants - it must not lose contact because the aircraft's box is taller than they are.
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Interaction")
	float GetDistanceToAirframeCm(const FVector& WorldLocation, bool bHorizontalOnly = false) const;

	// The gap arithmetic on its own, so the rule can be tested without a world: BodyFrame is
	// ModelPivot's world transform, LocalBoundsCm the fuselage box expressed in that frame.
	static float ComputeAirframeGapCm(
		const FBox& LocalBoundsCm,
		const FTransform& BodyFrame,
		const FVector& WorldLocation,
		bool bHorizontalOnly);

	// Is a body at WorldLocation touching the airframe, within ToleranceCm of its skin? Measured
	// against the mesh, never against a radius about the actor origin: the collision capsule is a
	// 95 cm sphere (InitCapsuleSize clamps its half height up to its radius) sized for the flight
	// impact sweep, and it bears no relation to how wide the fuselage you can see actually is.
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Interaction")
	bool CanBeEnteredBy(const FVector& WorldLocation, float ToleranceCm) const;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Interaction")
	void EnterHelicopter(APlayerController* PlayerController, bool bBlendView = true);

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Interaction")
	bool CanExitHelicopter() const;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Interaction")
	void ExitHelicopter();

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Flight")
	bool IsEngineRunning() const { return bEngineRunning; }

	/** May somebody step OUT of the cabin here? See PassengerAlightClearanceCm. */
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Missions")
	bool CanTransferMissionPassengers() const;

	/** May somebody climb IN here? Looser than the alight, exactly as the original's is. */
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Missions")
	bool CanBoardMissionPassengers() const;

	/**
	 * How long the aircraft has been continuously inside the alight clearance, in seconds.
	 *
	 * The mission-side release paths hold off until this reaches PassengerAlightSettleSeconds, which
	 * is BHAV 292's own polling period. Without it the mission tick beat the shipped program to the
	 * cabin door and the fare was out before the pilot had finished landing.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Missions")
	float GetSecondsWithinAlightClearance() const { return SecondsWithinAlightClearance; }

	/** BHAV 292's polling period; see PassengerAlightSettleSeconds. */
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Missions")
	float GetPassengerAlightSettleSeconds() const { return PassengerAlightSettleSeconds; }

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

	// SCHOOK: SeatManifestAdd 0x0048bff0 / SeatManifestRemove 0x0048c120. Person is the record's
	// +0x0c: pass it and the seat carries that passenger's head and can be found again by opcode
	// 54; leave it null for a seat the mission layer books without a person actor behind it.
	int32 AddMissionPassengersForMission(
		int32 Count,
		int32 EventId,
		ESimCopterMissionPassengerKind Kind,
		ASimCopterGroundAgent* Person = nullptr);
	int32 RemoveMissionPassengersForMission(
		int32 Count,
		int32 EventId,
		ESimCopterMissionPassengerKind Kind,
		const ASimCopterGroundAgent* Person = nullptr);
	int32 GetMissionPassengerCount(int32 EventId, ESimCopterMissionPassengerKind Kind) const;

	// SCHOOK: SeatManifestSetFace 0x0048c0e0, reached from people opcode 54 (FUN_004ccb40). Writes
	// record +0x04 for the seat holding Person and marks the seat window dirty (FUN_0048bf40).
	// False when that person holds no seat, which is the original's iVar1 == -1 arm.
	bool SetMissionPassengerPortraitState(const ASimCopterGroundAgent* Person, int32 PortraitState);

	// Who is aboard, in seat order. The seat window draws one portrait per entry.
	const TArray<FSimCopterMissionPassengerSlot>& GetMissionPassengerSlots() const { return MissionPassengerSlots; }
	bool DropPassengerAtSlot(int32 SlotIndex);

	/**
	 * Where somebody who steps out of the cabin lands: beside the aircraft, on the surface the
	 * aircraft is standing on. **Feet level** - lift it by the person's own capsule half height to
	 * place their actor.
	 *
	 * Z comes from the root sphere's bottom, which `ApplyFlightModelToActor` pins to the flight
	 * model's `Altitude`; the actor origin is 190 cm above that and is not where anybody stands.
	 */
	FVector GetPassengerDropWorldLocation(int32 SlotIndex = INDEX_NONE) const;

	// Read-only controller presentation state consumed by the radial/passenger Slate layer.
	ESimCopterControllerMode GetControllerMode() const { return ControllerMode; }
	int32 GetControllerRadialIndex() const { return ControllerRadialIndex; }
	const TArray<ESimCopterHelicopterTool>& GetControllerToolWheelTools() const { return ControllerToolWheelTools; }
	int32 GetControllerPassengerSlot() const { return ControllerPassengerSlot; }
	int32 GetControllerPassengerConfirmChoice() const { return ControllerPassengerConfirmChoice; }
	bool IsPassengerSlotControllerSelected(int32 SlotIndex) const;

	const FVector& GetVelocityCmPerSec() const { return VelocityCmPerSec; }
	float GetMaxForwardSpeedCmPerSec() const { return MaxForwardSpeedCmPerSec; }
	int32 GetBucketWaterPounds() const { return BucketWaterPounds; }
	int32 GetMaxLoadPounds() const { return HelicopterTuning.MaxLoadPounds; }

	// --- Canonical model registry (Phase 1) ---

	// Runtime type index into SimCopterHelicopterRegistry (the executable's heli[0]).
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Model")
	int32 GetHelicopterTypeIndex() const { return ActiveHelicopterTypeIndex; }

	const FSimCopterHelicopterDefinition* GetHelicopterDefinition() const;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Model")
	bool IsApacheHelicopter() const;

	// Transactional live model switch (prepare -> validate -> commit). Returns false and
	// leaves the current helicopter completely untouched when validation fails; the reason
	// is available from GetLastModelSwitchStatus().
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Model")
	bool SwitchHelicopterModel(int32 TypeIndex);

	// Wraps through the nine registry entries.
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Model")
	bool CycleHelicopterModel(int32 Delta);

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Model")
	FString GetLastModelSwitchStatus() const { return LastModelSwitchStatus; }

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Model")
	bool IsUsingOriginalMesh() const { return bUsingOriginalMesh; }

	// --- Equipment and tool selection (Phase 2) ---

	const FSimCopterEquipmentState& GetEquipmentState() const { return EquipmentState; }

	// SCHOOK: ShopSetEquipmentOwned 0x0042d840
	// The hangar shop writing the career record (career + 0x48), which is what the debug grant
	// deliberately does not do. Buying the tear gas launcher also fills career + 0x54 with the
	// ten rounds FUN_0042d840 writes; selling it empties them.
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Tools")
	void SetCareerEquipmentOwned(ESimCopterHelicopterTool Tool, bool bOwned);

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Tools")
	ESimCopterHelicopterTool GetSelectedTool() const { return SelectedTool; }

	// Remembers the request even when the tool is unavailable; the active tool used by input
	// falls back to the first available normal tool (GetActiveTool).
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Tools")
	void SetSelectedTool(ESimCopterHelicopterTool Tool);

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Tools")
	void CycleSelectedTool(int32 Delta);

	// The tool primary input actually drives right now. Equals GetSelectedTool() unless the
	// selection is unavailable on this model.
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Tools")
	ESimCopterHelicopterTool GetActiveTool() const;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Tools")
	ESimCopterToolAvailability GetToolAvailability(ESimCopterHelicopterTool Tool) const;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Tools")
	bool IsToolAvailable(ESimCopterHelicopterTool Tool) const;

	// True when the tool exists in the selector for the active model (Apache tools are
	// listed only on the Apache).
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Tools")
	bool IsToolSelectable(ESimCopterHelicopterTool Tool) const;

	// Session-only grant/revoke. Never writes the career record.
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Debug")
	void SetDebugToolGrant(ESimCopterHelicopterTool Tool, bool bGranted);

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Debug")
	void DebugRefillTearGas();

	// --- Debug appearance knobs (SSimCopterHelicopterDebugPanel) ---

	// Metallic on the shared vehicle material: the fuselage, the cars and the ambient
	// planes/trains/boats all move together, and the city's buildings deliberately do not.
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Debug")
	float GetVehicleMetallic() const;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Debug")
	void SetVehicleMetallic(float Metallic);

	// One multiplier over both this airframe's blink markers and the city's building beacons.
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Debug")
	float GetFlashingLightIntensityScale() const;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Debug")
	void SetFlashingLightIntensityScale(float Scale);

	// Live five-frame water texture cadence used by both terrain water and page-20 mesh pools.
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Debug")
	float GetWaterTextureFramesPerSecond() const;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Debug")
	void SetWaterTextureFramesPerSecond(float FramesPerSecond);

	// --- Check-up service menu (FUN_00443c20; offer test FUN_00444750) ---

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Checkup")
	bool IsCheckupMenuOpen() const;

	// Raises the panel whatever the offer test says - the console command and the OK/Cancel path
	// both go through here. Returns false when there is no viewport to put it in.
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Checkup")
	bool OpenCheckupMenu();

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Checkup")
	void CloseCheckupMenu();

	// Console: raise the Check-up panel on demand, so it can be seen without flying to the airport.
	UFUNCTION(Exec)
	void SimCheckup();

	// Everything FSimCopterCheckup needs to price this aircraft where it is standing.
	FSimCopterCheckupState BuildCheckupState() const;
	bool IsStandingOnAirport() const;

	// FUN_004385c0: charge the funds and apply the three purchases, in the original's order.
	void ApplyCheckupOrder(const FSimCopterCheckupOrder& Order);

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Tools")
	ESimCopterMegaphoneMessage GetSelectedMegaphoneMessage() const { return SelectedMegaphoneMessage; }

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Tools")
	void CycleMegaphoneMessage(int32 Delta);

	// The megaphone flap picks a message off a menu rather than stepping through them - "Click
	// on the button to open a menu of message types, and click on the type you want" (help
	// 34ref.htm).
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Tools")
	void SetSelectedMegaphoneMessage(ESimCopterMegaphoneMessage Message);

	// FUN_0044ac80's F6-F10 commands choose a message and invoke it synchronously. The cockpit
	// popup uses this instead of a pressed-input latch because its click supplies both edges in
	// one Slate callback.
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Tools")
	bool SendMegaphoneMessage(ESimCopterMegaphoneMessage Message);

	// The two water controls, each level triggered for as long as the button is held.
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Tools")
	void StartBucketDump();

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Tools")
	void StopBucketDump();

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Tools")
	void StartWaterCannon();

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Tools")
	void StopWaterCannon();

	// SCHOOK: HelicopterToolInput 0x00485f50 (actions 0x0b..0x0f)
	// A flap rocker held down. The winch pays out or takes in one node per frame for as long as
	// the button is held - the same level-triggered path the raise/lower keys use - rather than
	// running to the limit the way ToggleRope's one-shot command does. Direction is +1 to raise,
	// -1 to lower, 0 to release. The attachment is named, because the flap belongs to that tool.
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Tools")
	void SetWinchHeldInput(bool bHarness, int32 Direction);

	// The single primary-use entry point shared by left click and the debug panel's USE
	// button. Held tools (bucket/cannon/machine gun) latch; pressed tools (megaphone/gas/
	// missile/harness) act once on Start.
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Tools")
	void StartPrimaryToolUse();

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Tools")
	void StopPrimaryToolUse();

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Tools")
	bool IsPrimaryToolUseHeld() const { return bPrimaryToolUseHeld; }

	// One-line reason string for the debug panel / HUD ("CAREER", "DEBUG GRANT", ...).
	FString DescribeToolAvailability(ESimCopterHelicopterTool Tool) const;

	// Last refusal produced by the primary-use path (missing equipment, out of water, ...).
	FString GetLastToolStatus() const { return LastToolStatus; }

	// --- Tear gas ---

	// Hands one interaction to every ground agent standing on Event.TargetTile. The gas cloud
	// scans a single tile rather than FUN_0048ae70's spiral, so it needs this rather than
	// BroadcastInteraction. Returns how many reacted.
	int32 DeliverInteractionToTile(const struct FSimCopterInteractionEvent& Event);

	// The ten-slot canister pool, so the debug panel and tests can read it.
	USimCopterTearGasPoolComponent* GetTearGasPool() const { return TearGasPool; }

	// The Apache's missile/tracer pools, so the debug panel can read them.
	USimCopterApachePoolComponent* GetApachePool() const { return ApachePool; }

	// --- Spotlight target service (Phase 3) ---

	// The shared semantic target the megaphone (and later dispatch) aim at. Updated every
	// frame regardless of whether the Unreal light is drawn.
	const FSimCopterToolTarget& GetSpotlightTarget() const { return SpotlightTarget; }

	// Aim deltas in the original's 16.16 tenth-degree units, clamped to +/-500.0.
	void AddSpotlightAim(int32 PitchDelta1616, int32 YawDelta1616);

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Spotlight")
	void ResetSpotlightAim();

	// Freezes the target for diagnosis. Debug only; it never redefines the normal target,
	// it just stops the cached one from being overwritten.
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Spotlight")
	void SetSpotlightTargetFrozen(bool bFrozen) { bSpotlightTargetFrozen = bFrozen; }

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Spotlight")
	bool IsSpotlightTargetFrozen() const { return bSpotlightTargetFrozen; }

	// Rope/winch state shared by the bucket and (Phase 4) the harness.
	bool IsRopeDeployed() const { return bRopeDeployed; }
	int32 GetRopeFirstActiveNode() const { return RopeFirstActiveNode; }

	// World position of the last rope node - the bucket or the harness, whichever is on the rope.
	// False while the rope is stowed. Rescue pickups reach for this rather than for the airframe,
	// which is what makes the harness worth deploying.
	bool TryGetRopeEndWorldLocation(FVector& OutWorldLocation) const;

	// Which object the rope end currently renders (heli[0x32] vs heli[0x33]).
	bool IsHarnessRopeEndSelected() const { return bHarnessRopeEndSelected; }
	bool HasHarnessRider() const { return bHarnessRiderAttached; }

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Debug")
	void ToggleRopeFromDebugPanel();

	// --- Emergency dispatch (F2-F5) ---
	//
	// Ported from FUN_0048a580 / FUN_004be910, decoded in
	// Docs/scratchpad/ghidra/emergency_dispatch_decode_20260725.md. Every dispatch aims at
	// the spotlight's ground tile, not the helicopter's, and Shift releases instead of
	// dispatching. The service enum lives in Ground/SimCopterDispatch.h.

	// Runs one dispatch (or release, when Shift is held) for a service. Public so the debug
	// panel can drive the same path the keys use.
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Dispatch")
	void RequestDispatch(int32 ServiceIndex, bool bChaseSpotlight, bool bClearInstead);

	// Dedicated dispatch-panel/controller action. Unlike Shift+F, this does not need a spotlight
	// target and immediately removes every active response and chase vehicle.
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Dispatch")
	void ClearAllDispatchVehicles();

	// Last dispatch outcome as a HUD/debug line ("Fire Truck dispatched", "no unit
	// available", ...). Mirrors which of the four voice clips the original would play.
	FString GetLastDispatchStatus() const { return LastDispatchStatus; }

	// Debug panel selection: which service the DISPATCH/CLEAR buttons act on.
	int32 GetSelectedDispatchService() const { return SelectedDispatchService; }
	void CycleSelectedDispatchService(int32 Delta);

	// Status line for the currently selected service, sourced from the traffic system.
	FString GetSelectedDispatchServiceStatus() const;

	// Debug panel: place one mission of TypeMask into the running session through the original
	// placer (FUN_004a92f0 -> FUN_004a7a10). Nothing here bypasses the placement rules, so a
	// type whose tile test finds nothing nearby reports a failure rather than forcing a spawn.
	// Returns the event id, or INDEX_NONE.
	int32 DebugStartMission(int32 TypeMask);
	FString GetLastDebugMissionStatus() const { return LastDebugMissionStatus; }

	// The decompiled flight simulation state (read-only; for HUD and tests).
	const FSimCopterFlightModel& GetFlightModel() const { return FlightModel; }

	// The original's easy handling model (FSimCopterFlightModel::bEasyFlightModel). The
	// executable bound it to the interior camera views; here it is an explicit option so
	// either model can be flown from any view. Persisted like the camera view offsets.
	bool IsEasyFlightModelEnabled() const { return FlightModel.bEasyFlightModel; }
	void SetEasyFlightModelEnabled(bool bEnabled);

	// The frame rate the original's per-frame rules are assumed to have been written
	// for, in fps. The executable names 20 (and only there), but it had no fixed
	// timestep, so this is a feel knob as much as a fidelity one - hence live tuning.
	// Split three ways: the shake and the airspeed chase each pull the shared reference
	// in the opposite direction from the other, so each owns its own. Turbulence is the
	// shake; sim reference is the collective's neutral decay, the fire burn and the
	// attitude window; speed chase is how quickly the helicopter gets moving.
	float GetTurbulenceReferenceFps() const;
	void SetTurbulenceReferenceFps(float Fps);
	float GetFlightReferenceFps() const;
	void SetFlightReferenceFps(float Fps);
	float GetSpeedChaseReferenceFps() const;
	void SetSpeedChaseReferenceFps(float Fps);
	// Presentation only: how much faster than the original's strobe the blades draw.
	float GetRotorVisualMultiplier() const;
	void SetRotorVisualMultiplier(float Multiplier);

	// Persistent offsets edited by the developer panel. Each normal camera view has its own
	// values; setters update the live camera and flush that view to GameUserSettings.ini.
	ESimCopterCameraMode GetCameraMode() const { return CameraMode; }
	static bool ShouldUseRopeAutoZoom(
		ESimCopterCameraMode Mode,
		float PlayerZoomAlpha,
		int32 FirstActiveRopeNode);
	FSimCopterCameraViewDebugOffset GetCameraViewDebugOffset(ESimCopterCameraMode Mode) const;
	void SetCameraViewDebugTranslation(ESimCopterCameraMode Mode, const FVector& TranslationCm);
	void SetCameraViewDebugRotation(ESimCopterCameraMode Mode, const FRotator& RotationDeg);
	void SetCameraViewZoomVerticalFramingStrength(ESimCopterCameraMode Mode, float Strength);
	float GetCameraViewMinZoomDistanceCm(ESimCopterCameraMode Mode) const;
	float GetCameraViewMaxZoomDistanceCm(ESimCopterCameraMode Mode) const;
	void SetCameraViewMaxZoomDistanceCm(ESimCopterCameraMode Mode, float DistanceCm);
	void ResetCameraViewDebugOffset(ESimCopterCameraMode Mode);

	float GetCockpitAttitudeFollowStrength() const { return CockpitAttitudeFollowStrength; }
	void SetCockpitAttitudeFollowStrength(float Strength);
	float GetCockpitAttitudeLerpSpeed() const { return CockpitAttitudeLerpSpeed; }
	void SetCockpitAttitudeLerpSpeed(float Speed);
	FVector GetCockpitCannonViewModelOffsetCm() const { return CockpitCannonViewModelOffsetCm; }
	void SetCockpitCannonViewModelOffsetCm(const FVector& OffsetCm);

	// Ground-lift framing, live-tunable because how centred a parked helicopter looks is a
	// judgement call no amount of arithmetic settles. See ResolveCameraGroundLift.
	float GetCameraGroundLiftHeightCm() const { return CameraGroundLiftHeightCm; }
	void SetCameraGroundLiftHeightCm(float ValueCm);
	float GetCameraGroundLiftProbeRangeCm() const { return CameraGroundLiftProbeRangeCm; }
	void SetCameraGroundLiftProbeRangeCm(float ValueCm);
	float GetCameraGroundLiftFullDistanceCm() const { return CameraGroundLiftFullDistanceCm; }
	void SetCameraGroundLiftFullDistanceCm(float ValueCm);

	// Live readout for the debug panel: what the lift is actually doing right now. Worth having
	// because "the lift does nothing" and "the probe never finds ground" look identical on screen
	// and want completely different fixes.
	float GetCurrentCameraGroundLiftCm() const { return CurrentCameraGroundLiftCm; }
	// Camera clearance above whatever the probe last hit, or a negative value when it hit nothing.
	float GetLastCameraGroundProbeDistanceCm() const
	{
		return bLastCameraGroundProbeHit ? LastCameraGroundProbeDistanceCm : -1.0f;
	}

	float GetRotorDiscOpacity() const { return RotorDiscOpacity; }
	void SetRotorDiscOpacity(float Opacity);
	FLinearColor GetRotorDiscColor() const { return RotorDiscColor; }
	void SetRotorDiscColor(const FLinearColor& Color);

	// The mission-marker layer lives below the cockpit in the viewport stack, so it cannot infer
	// which parts of a full-screen overlay actually paint UI. Supply the exact live panel widgets.
	void AppendMissionMarkerAvoidanceWidgets(TArray<TSharedPtr<SWidget>>& OutWidgets) const;

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

	// Original SimCopter CANNON object (GEO id 0x16e), shown while the water cannon is fitted.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UProceduralMeshComponent> HeliCannonMeshComponent;

	// BRACKET (heli[0x31]): the rescue harness's triangular mount on the right flank, which is
	// the side a winched Sim is brought aboard.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UProceduralMeshComponent> HeliBracketMeshComponent;

	// The same geometry again as a cockpit view model. Its position is camera-parented while
	// its absolute rotation follows the stabilized aircraft frame without camera look pitch.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UProceduralMeshComponent> CockpitCannonMeshComponent;

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

	// Original SimCopter HARNESS object (GEO id 0x16d). FUN_00483c20 caches both rope-end
	// objects and FUN_00487bb0 swaps which one the rope end renders.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UProceduralMeshComponent> OriginalHarnessMeshComponent;

	// Renders the original water effects: bucket water drips + douse steam (from the bucket) and
	// the rotor-wash "wind kickback" spray/dust under the helicopter.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<USimCopterParticleFXComponent> WaterFXComponent;

	// DAT_005d4bd0: the ten tear gas canisters in flight and the clouds they leave behind. Slots
	// are world-space, so a canister keeps flying and gassing after the helicopter has left.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<USimCopterTearGasPoolComponent> TearGasPool;

	// DAT_005d4900 (ten missiles) and DAT_005d4f30 (seventy tracers), the Apache's armament.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<USimCopterApachePoolComponent> ApachePool;

	// The fuselage's own face-type-25 blink markers (FUN_00496c00). Every flyable airframe carries
	// four: one white, two red and one green. Rides the body mesh, whose local frame they share.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<USimCopterFlashingLightsComponent> FlashingLightsComponent;

	// Follows the rendered fuselage roof, including the visual pitch/roll applied at ModelPivot.
	// Camera translations move the spring-arm endpoint and never move this collision-path origin.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<USceneComponent> CameraAnchor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UCameraComponent> CameraComponent;

	// Screen-space presentation at a world-space aim point: the component projects its location
	// through the active camera, but Slate keeps the mark pixel-sized and above scene depth.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UWidgetComponent> CrosshairComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<USpotLightComponent> SearchLightComponent;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Tuning")
	FDirectoryPath OriginalGameRoot;

	// Seed value only: resolved through SimCopterHelicopterRegistry into
	// ActiveHelicopterTypeIndex on BeginPlay, after which the index is authoritative.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Tuning")
	FString HelicopterTypeName = TEXT("Schweizer 300");

	// Shows the top-left developer panel and bottom-left tool readout. Ctrl+Alt+D toggles both.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Debug")
	bool bShowHelicopterDebugPanel = false;

	// Draws the cockpit's control flaps for the tools aboard (SimCopterFlapLayout).
	UPROPERTY(EditAnywhere, Category = "SimCopter|UI")
	bool bShowToolFlaps = true;

	// Both aiming views position the fixed-size mark six metres from the helicopter midpoint
	// along the view's gameplay axis.
	UPROPERTY(EditAnywhere, Category = "SimCopter|UI", meta = (ClampMin = "1.0"))
	float CrosshairWorldOffsetCm = 600.0f;

	// The cockpit mark sits below the stabilized forward axis so it remains useful with the
	// cannon framed in the lower part of the view.
	UPROPERTY(EditAnywhere, Category = "SimCopter|UI", meta = (ClampMin = "0.0"))
	float CockpitCrosshairDownOffsetCm = 200.0f;

	// Original page pixels to screen pixels. A flap is 138x58 in the original's 640x480, which is
	// far too small on a modern display, so the art is up-filtered. Lettering is not scaled with
	// it; SSimCopterToolFlaps lays text out in screen pixels.
	UPROPERTY(EditAnywhere, Category = "SimCopter|UI", meta = (ClampMin = "0.5", ClampMax = "6.0"))
	float ToolFlapScale = 2.0f;

	// USimCopterSettings::OnHudScaleChanged, so the Settings screen's HUD Scale row rebuilds the
	// cockpit while the player is looking at it.
	FDelegateHandle HudScaleHandle;

	// Career equipment the session starts with. Until the career/shop layer is ported this
	// seeds FSimCopterEquipmentState::CareerEquipmentMask from both original new-game paths.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Equipment", meta = (Bitmask))
	int32 StartingCareerEquipmentMask = SimCopterHelicopterRegistry::StartingCareerEquipmentBits;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Equipment", meta = (ClampMin = "0", ClampMax = "10"))
	int32 StartingTearGasRounds = 0;

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

	// Fallback door offset, used only while no fuselage has been built to measure (a headless test,
	// or the frame before the GEO packs load). ExitHelicopter otherwise derives the spot from the
	// airframe's own box, so the pilot lands against the machine rather than out on the apron.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Interaction")
	FVector ExitOffset = FVector(0.0f, 110.0f, 0.0f);

	// How far outboard of the fuselage skin the pilot is set down. Enough to keep a 30 cm-radius
	// body out of the model, and no more - stepping out should read as leaving the aircraft, not
	// as being teleported beside it.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Interaction", meta = (ClampMin = "0.0"))
	float ExitClearanceCm = 40.0f;

	// How high the aircraft may sit above the ground and still let somebody step OUT of the cabin.
	//
	// FUN_004c9bc0, the test behind opcodes 17 and 21, ends on
	// `(person.Y - FUN_004c82c0(person.pos)) >> 16 < 6` - six original units, 37.5 cm. For a rider
	// that is the aircraft's own height, because FUN_004c6450 copies the carrier's position onto the
	// person every tick. GroundClearanceCm is exactly the same quantity (ApplyFlightModelToActor puts
	// the sphere bottom at the flight model's Altitude), so the shipped number transfers directly and
	// needs no contact tolerance added to it: FUN_00487160 parks at TerrainHeight + 0x13333, so a
	// landed helicopter is already sitting 1.2 units up and this leaves 4.8 units of hover over it.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "0.0"))
	float PassengerAlightClearanceCm = 37.5f;

	// And how high it may sit and still let somebody climb IN. Deliberately the looser of the two,
	// because the original's is: opcode 12's FUN_004ca940 accepts the board while
	// `(objectY - personY) & 0xffff0000 < 0x50000` - five units between the doorsill and a walker who
	// is themselves standing 3 units (0x30000, FUN_004cb190) above the ground, so eight units of
	// aircraft-above-ground, 50 cm. You may drop to a low hover and have a fare climb aboard; you may
	// not have them step out onto the roof from the same height.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "0.0"))
	float PassengerBoardClearanceCm = 50.0f;

	// How long the aircraft has to stay inside PassengerAlightClearanceCm before the MISSION layer
	// will let anybody out. BHAV 292 'Transport wait to get off' loops `local0 := 10` / `op0 wait
	// local0--` and then BHAV 264, whose own 'idle a bit' is another three ticks, before each probe -
	// so the shipped program tests this about every thirteenth tick, 0.87 s at the VM's 15 Hz, and
	// nobody has ever hopped out the instant the skids came into range. The behaviour VM keeps that
	// cadence for free; the mission-side release, which runs every mission tick, does not, and that
	// is what emptied the cabin while the pilot was still on the way down.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "0.0"))
	float PassengerAlightSettleSeconds = 13.0f / 15.0f;

	// How far above the aircraft's own deck the passenger drop probe starts. Small on purpose: it is
	// only there so the trace begins clear of the surface the skids are on, and every centimetre of
	// it is a centimetre of roof the probe could find instead of the ground.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "0.0"))
	float PassengerDropProbeLiftCm = 40.0f;

	// And how far below it the probe looks, for the case where somebody steps out over the lip of a
	// pad or a roof onto the street.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "0.0"))
	float PassengerDropProbeDepthCm = 1800.0f;

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
	// Actor names serialized beside the pointer-free seat records. Mission people are recreated
	// after the aircraft and call RelinkSavedMissionPassenger to fill the weak handles back in.
	TArray<FName> PendingSavedPassengerActorNames;


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

	// Opacity of the spinning-rotor blur disc, written into M_SimCopterRotorDisc's DiscOpacity
	// parameter. Adjustable live from the helicopter debug panel and persisted like the camera
	// view offsets. Matches the material's authored default.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Model", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RotorDiscOpacity = 0.1f;

	// Colour of the blur disc, written into the same material's DiscColor parameter. Alpha is
	// unused - RotorDiscOpacity drives transparency.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Model")
	FLinearColor RotorDiscColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);

	UPROPERTY(EditAnywhere, Category = "SimCopter|Search Light")
	bool bSearchLightStartsEnabled = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Search Light", meta = (ClampMin = "0.0"))
	float SearchLightIntensity = 650000.0f;

	// How much of the scene's exposure is divided back out of the beam. 1 - the default - makes the
	// searchlight hold the same presence on screen whatever the sun is doing; 0 is the raw physical
	// light, which at 650,000 unitless is ~1,000 candelas, a hand torch against the day sequence's
	// 120,000 lux sun. That is why the beam vanished when the celestial vault went in: the sun went
	// up by ~30,000x (the old day/night actor ran it at 4 lux) and auto exposure went with it.
	// Everything unlit in the remake is compensated the same way - see CreateSimCopterMaterials.py's
	// add_exposure_independent_emissive and USimCopterFlashingLightsComponent.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Search Light", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SearchLightExposureCompensation = 1.0f;

	// How much of the beam Lumen bounces, and it needs its own number because of the line above.
	//
	// `IndirectLightingIntensity` is exactly the knob: LumenSceneDirectLighting.cpp gathers a light
	// into the Lumen scene only when `Proxy->GetIndirectLightingScale() > 0`, and then multiplies
	// that light's colour by the same value - so it is both the gate and the scale, and raising it
	// costs nothing beyond what Lumen already computes for the light.
	//
	// It has to be well above 1 here because SearchLightExposureCompensation is a DIRECT-lighting
	// trick: it divides the exposure back out on the renderer side, where Lumen does not see it. The
	// beam's true radiometric output is ~1,000 candelas, so what Lumen was being handed to bounce
	// was a hand torch - which is why the searchlight lit the ground and nothing around it.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Search Light", meta = (ClampMin = "0.0"))
	float SearchLightIndirectLightingIntensity = 24.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Search Light", meta = (ClampMin = "100.0"))
	float SearchLightRangeCm = 5200.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Search Light", meta = (ClampMin = "100.0"))
	float SearchLightBeamLengthCm = 4700.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Search Light", meta = (ClampMin = "20.0"))
	float SearchLightBeamWidthCm = 2300.0f;

	// How far ahead of the resolved water-cannon muzzle (barrel tip, or nose muzzle as a
	// fallback - see ApplyPreparedModelMeshes) the searchlight is seated. Keeps the beam
	// starting a touch in front of the nozzle on every helicopter type instead of a single
	// offset tuned for one mesh.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Search Light", meta = (ClampMin = "0.0"))
	float SearchLightForwardOfCannonOffsetCm = 20.0f;

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

	// Zoom fraction a fresh session starts on, and the reference every view's framing translation
	// is scaled against so the helicopter holds its screen position through zoom. The two must be
	// the same number or the default view is already scaled off its authored framing.
	static constexpr float CameraDefaultZoomAlpha = 0.05f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "100.0"))
	float ChaseCameraMinDistance = 720.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "100.0"))
	float ChaseCameraMaxDistance = 2200.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera")
	float ChaseCameraBasePitch = -14.0f;

	// At full forward speed, pitch this many degrees toward the horizon for better visibility.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.0"))
	float ChaseCameraForwardPitchLiftDeg = 4.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "100.0"))
	float OrbitCameraMaxDistance = 2200.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "100.0"))
	float RescueCameraMaxDistance = 2400.0f;

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

	// Cockpit view only. How much of the airframe's pitch/roll the eye adopts: 1 rides the
	// model rigidly, lower keeps the horizon steadier than the aircraft actually is. Together
	// with the lerp speed below this is the whole of the "artificial stabilization" - it is a
	// camera filter, and the flight model, ModelPivot and every tool's aiming frame go on
	// using the true attitude, so handling and aim never change with it.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CockpitAttitudeFollowStrength = 0.75f;

	// How quickly the stabilized attitude catches up with the airframe. Lower is smoother and
	// lags further behind.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.1"))
	float CockpitAttitudeLerpSpeed = 5.0f;

	// Ceiling on the tilt the view will take, so an extreme attitude cannot roll the horizon
	// past something readable.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float CockpitAttitudeMaxDeg = 35.0f;

	// Where the cockpit cannon view model sits in camera space: X forward, Y right, Z up, in
	// centimetres from the eye. Adjustable live from the debug panel's CANNON VM row.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera")
	FVector CockpitCannonViewModelOffsetCm = FVector(26.0f, 15.0f, -34.0f);

	// Extends only the cockpit copy's rear-most vertex ring so looking down cannot expose the
	// original CANNON mesh's abruptly clipped back.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.0"))
	float CockpitCannonRearExtensionCm = 100.0f;

	// Middle-drag pan rate, in centimetres of camera movement per unit of mouse travel. Mouse
	// axes already arrive as per-frame deltas, so this is not scaled by delta time.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.0"))
	float CameraPanCmPerMouseUnit = 22.0f;

	// How far the pan may push the camera off the view's authored framing, at reference zoom.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.0"))
	float CameraPanMaxOffsetCm = 900.0f;

	// R3 + right-stick Y reaches either end of the current view's zoom range in roughly this
	// many seconds at full deflection.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Controller", meta = (ClampMin = "0.05"))
	float ControllerCameraZoomAlphaPerSecond = 0.65f;

	// View 1 only: at the player's closest zoom, a fully paid-out bucket/harness would fall below
	// the frame. This is layered on top of (never written into) CameraZoomAlpha so pulling in even
	// one node returns to exactly the zoom the player chose.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FullyLoweredRopeZoomOutAlpha = 0.10f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.1"))
	float RopeAutoZoomLerpSpeed = 3.0f;

	// R3 + RB/RT moves the helicopter up/down in the frame through the same per-view offset as
	// middle-mouse drag.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Controller", meta = (ClampMin = "1.0"))
	float ControllerCameraPanCmPerSecond = 520.0f;

	// Boarding and exiting transfer control immediately, but blend between the two pawn cameras.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.0"))
	float CameraPossessionBlendSeconds = 0.85f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.1"))
	float CameraViewPositionLerpSpeed = 4.5f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.1"))
	float CameraViewRotationLerpSpeed = 5.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.0"))
	float CameraGroundClearanceCm = 24.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "1.0"))
	float CameraGroundProbeUpCm = 2000.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "1.0"))
	float CameraGroundProbeDownCm = 6000.0f;

	// The avoidance search uses this much extra radius beyond the actual camera probe. That
	// starts the angle correction before the camera would touch terrain or a building. Keep the
	// margin restrained: a large padded sphere makes the view react to nearby walls that the
	// camera's real collision shape would comfortably miss.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.0"))
	float CameraObstructionPaddingCm = 40.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.0"))
	float CameraMinObstructedDistanceCm = 0.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.0", ClampMax = "85.0"))
	float CameraAvoidanceMaxAngleDeg = 50.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "1.0", ClampMax = "15.0"))
	float CameraAvoidanceSearchStepDeg = 5.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.1"))
	float CameraAvoidanceLerpSpeed = 2.25f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.1"))
	float CameraAvoidanceReturnLerpSpeed = 1.5f;

	// How far the boom pivot rises once the camera is right down on a surface. Raising the pivot
	// while the aim direction holds walks the helicopter DOWN the screen, which is the whole
	// point: parked, the aircraft should sit around the middle of the frame instead of up near
	// the top where the authored chase framing puts it in the air.
	//
	// Promoted from the live debug-panel tuning on 2026-07-30.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.1"))
	float CameraGroundLiftHeightCm = 155.0f;

	// Where the lift starts easing in, measured from the camera down to whatever is under it.
	// It begins during the approach and reaches full strength at the threshold below.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "1.0"))
	float CameraGroundLiftProbeRangeCm = 430.0f;

	// ...and where it reaches full lift. This is the piece the old ramp lacked: it scaled to
	// maximum only at zero clearance, which the camera never reaches, so a landed helicopter got
	// a fraction of the intended lift and barely moved.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.0"))
	float CameraGroundLiftFullDistanceCm = 40.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.1"))
	float CameraGroundLiftLerpSpeed = 7.5f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.1"))
	float CameraObstructionReleaseLerpSpeed = 2.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "0.1"))
	float CameraObstructionPullInLerpSpeed = 4.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	bool bIsLanded = false;

	// Reset the moment the aircraft rises back out of PassengerAlightClearanceCm, so a bounce off
	// the pad restarts the wait rather than banking credit toward it.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	float SecondsWithinAlightClearance = 0.0f;

	// The original runs FUN_00444750's service-threshold test while landed. The remake opens on
	// every airport landing instead so the feature is easy to discover and behaves predictably.
	// Clear this to keep it manual (SimCheckup).
	UPROPERTY(EditAnywhere, Category = "SimCopter|Checkup")
	bool bAutoOpenCheckupOnLanding = true;

	// False at startup and while merely entering a parked helicopter. The first controlled
	// airborne frame arms it; a successful airport-landing open consumes it.
	bool bCheckupAutoOpenArmed = false;

	// One open per touchdown: without this the panel would reopen the instant it is cancelled,
	// because the aircraft is still landed at the airport.
	bool bCheckupOpenedThisLanding = false;

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
	bool bUsingOriginalHarnessMesh = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	bool bUsingOriginalCannonMesh = false;
	FVector CannonBarrelTipLocalCm = FVector::ZeroVector;
	bool bHasCannonBarrelTip = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	bool bUsingOriginalBracketMesh = false;
	// The bracket's outboard tip, in its own local frame: where the winch cable leaves.
	FVector BracketRopeAnchorLocalCm = FVector::ZeroVector;
	bool bHasBracketRopeAnchor = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	FString LastModelLoadError;

	// Runtime type index; the registry entry is the source of truth for names, object ids,
	// seats, the tail-rotor mount, and the Apache armament flag.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	int32 ActiveHelicopterTypeIndex = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	FString LastModelSwitchStatus;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	FSimCopterEquipmentState EquipmentState;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ModelVertexColorMaterial;

	// Translucent grey material for the spinning rotor disc (Maxis face type 11).
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> RotorDiscMaterial;

	// Instance of the above, shared by both rotors, so DiscOpacity can be driven at runtime.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RotorDiscMaterialInstance;

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
	float CameraZoomAlpha = CameraDefaultZoomAlpha;
	float RopeAutoZoomAlpha = 0.0f;
	float CurrentCameraGroundLiftCm = 0.0f;
	// Written by the const ResolveCameraGroundLift purely so the debug panel can show what the
	// probe saw; nothing reads them back into the camera calculation.
	mutable float LastCameraGroundProbeDistanceCm = 0.0f;
	mutable bool bLastCameraGroundProbeHit = false;
	float CurrentCameraPullInAlpha = 1.0f;
	FRotator CurrentCameraAvoidanceOffsetDeg = FRotator::ZeroRotator;
	// Damped, partial copy of the airframe's visual tilt; shared by the cockpit camera,
	// cockpit crosshair anchor, and camera-carried cannon presentation.
	FRotator CockpitStabilizedAttitudeDeg = FRotator::ZeroRotator;
	bool bCockpitStabilizedAttitudeInitialized = false;
	float SmoothedCameraArmLengthCm = 0.0f;
	FVector SmoothedCameraTranslationWorld = FVector::ZeroVector;
	FRotator SmoothedCameraViewWorldRotation = FRotator::ZeroRotator;
	bool bCameraViewSmoothingInitialized = false;
	static constexpr int32 CameraModeCount = 4;
	TStaticArray<FSimCopterCameraViewDebugOffset, CameraModeCount> CameraViewDebugOffsets;

	// Middle-drag pan: how far the camera has been pushed along its own up axis, per view.
	// Held for the session only -- unlike the debug offsets above this is never written to
	// config, so it is back to zero next launch. Stored at the reference zoom distance and
	// scaled by the zoom ratio on use, which keeps the framing constant as the player zooms.
	TStaticArray<float, CameraModeCount> CameraViewPanOffsetsCm{InPlace, 0.0f};

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

	// Raw controller state has its own axes so the left stick can take FUN_00485f50's analog
	// joystick path while keyboard WASD retains the original ramping-key behavior.
	float ControllerLeftXInput = 0.0f;
	float ControllerLeftYInput = 0.0f;
	float ControllerRightXInput = 0.0f;
	float ControllerRightYInput = 0.0f;
	float ControllerRightTriggerInput = 0.0f;
	bool bControllerCameraAdjustHeld = false;
	bool bControllerRightShoulderHeld = false;
	bool bControllerDPadUpHeld = false;
	bool bControllerDPadDownHeld = false;
	bool bControllerDPadLeftHeld = false;
	bool bControllerDPadRightHeld = false;
	bool bControllerEngineStartHeld = false;
	bool bControllerEngineShutdownHeld = false;

	ESimCopterControllerMode ControllerMode = ESimCopterControllerMode::None;
	int32 ControllerRadialIndex = 0;
	TArray<ESimCopterHelicopterTool> ControllerToolWheelTools;
	ESimCopterHelicopterTool ControllerToolWheelOriginal = ESimCopterHelicopterTool::WaterBucket;
	int32 ControllerPassengerSlot = INDEX_NONE;
	// 0 Drop, 1 Cancel.
	int32 ControllerPassengerConfirmChoice = 0;
	int32 ControllerAppliedWinchDirection = 0;
	bool bControllerAppliedWinchHarness = false;

	// Common primary-use latch (FUN_00485f50 actions 2 / 0x0d / 0x10 are all level
	// triggered; the edge-triggered tools consume bPrimaryToolUsePressed once).
	bool bPrimaryToolUseHeld = false;
	bool bPrimaryToolUsePressed = false;

	ESimCopterHelicopterTool SelectedTool = ESimCopterHelicopterTool::WaterBucket;
	ESimCopterMegaphoneMessage SelectedMegaphoneMessage = ESimCopterMegaphoneMessage::ReportTraffic;
	TArray<TArray<FString>> MegaphoneVoiceFilesByMessage;
	TArray<int32> MegaphoneVoiceNextIndices;
	bool bMegaphoneVoicesLoaded = false;
	FString LastToolStatus;

	// FUN_0048e0b0's shared missile/tear-gas cooldown DAT_00504570 (1.0 s).
	float ToolCooldownSeconds = 0.0f;

	// Rope-end object selection, mirroring the original's heli[0x32] (BUCKET) /
	// heli[0x33] (HARNESS) swap in FUN_00487bb0. The rider flag is driven by the behaviour
	// VM attachment work; until that lands it stays false and only guards model switching.
	bool bHarnessRopeEndSelected = false;
	bool bHarnessRiderAttached = false;

	// heli[0x6f]..heli[0x72]. RopeFirstActiveNode/bRopeDeployed above are derived from this
	// each frame so the existing rope simulation and visuals keep working unchanged.
	SimCopterWinch::FWinchState WinchState;

	// One-shot winch command issued by ToggleRope / the debug panel; held until the winch
	// reaches its limit.
	int32 PendingWinchCommand = 0;

	// A cockpit flap's rocker held down: +1 raise, -1 lower, 0 nothing. Unlike RopeAdjustInput
	// it names its attachment instead of following the active tool selection.
	int32 WinchHeldDirection = 0;
	bool bWinchHeldHarness = false;

	// Spotlight aim accumulators DAT_0050408c (pitch) / DAT_00504090 (yaw), 16.16
	// tenth-degrees, clamped to +/-500.0. The rest pose adds the fixed -36 degree base tilt.
	int32 SpotlightAimPitch1616 = 0;
	int32 SpotlightAimYaw1616 = 0;
	float SpotlightAimPitchInput = 0.0f;
	float SpotlightAimYawInput = 0.0f;
	float ControllerSpotlightAimPitchInput = 0.0f;
	float ControllerSpotlightAimYawInput = 0.0f;

	// DAT_00504430: the smoothed march distance the band selection reads.
	int32 SpotlightDistance1616 = 0;
	FSimCopterToolTarget SpotlightTarget;
	bool bSpotlightTargetFrozen = false;

	// Emergency dispatch: the last outcome message and the debug panel's service selection.
	FString LastDispatchStatus;
	int32 SelectedDispatchService = 0;

	// Debug panel: what the last hand-placed mission did.
	FString LastDebugMissionStatus;

	// Cached world actors used by the water trajectories and terrain queries.
	TWeakObjectPtr<ASimCopterMissionSystemActor> CachedMissionSystem;
	mutable TWeakObjectPtr<ASimCity2000CityActor> CachedCityActor;
	mutable TWeakObjectPtr<class ASimCopterTrafficSystemActor> CachedTrafficSystem;

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

	// Middle-drag vertical pan. Kept separate from the look drag above because it neither
	// rotates the view nor recenters on release.
	int32 CameraPanButtonCount = 0;
	bool bCameraPanDragActive = false;

	FHitResult LastGroundHit;
	FHitResult LastForwardProbeHit;

	// Mesh sections holding the face-type-11 rotor blur discs; shown only when
	// the rotor is at lift speed (original: RPM >= 300 toggles those faces).
	int32 MainRotorDiscSectionIndex = INDEX_NONE;
	int32 TailRotorDiscSectionIndex = INDEX_NONE;
	TSharedPtr<SWidget> WaterControlsWidget;
	TSharedPtr<SWidget> WaterControlsPanel;
	TSharedPtr<SProgressBar> WaterCapacityBar;
	TSharedPtr<STextBlock> WaterCapacityText;
	TSharedPtr<STextBlock> WaterControlsText;
	TSharedPtr<SWidget> HelicopterDebugPanelWidget;
	TSharedPtr<SWidget> HelicopterDebugPanel;
	TSharedPtr<SWidget> ToolFlapsWidget;
	TSharedPtr<SSimCopterToolFlaps> ToolFlapsPanel;
	// Fixed-pixel aiming reticle hosted by CrosshairComponent at the mode-specific world point.
	TSharedPtr<SWidget> CrosshairWidget;
	TSharedPtr<SWidget> DashboardWidget;
	TSharedPtr<class SSimCopterDashboard> DashboardPanel;

	TSharedPtr<SWidget> MapWidget;
	TSharedPtr<class SSimCopterMapPanel> MapPanel;
	// The Check-up panel, up only while the player is being served.
	TSharedPtr<SWidget> CheckupWidget;
	TSharedPtr<SWidget> ControllerOverlayWidget;
	TSharedPtr<SSimCopterControllerOverlay> ControllerOverlayPanel;

	// Loads the cockpit flap bitmaps out of the original's BMP folder.
	UPROPERTY(Transient)
	TObjectPtr<class USimCopterHangarArt> FlapArt;

	void MovePitch(float Value);
	void MoveRoll(float Value);
	void MoveYaw(float Value);
	void MoveCollective(float Value);
	void LookYaw(float Value);
	void LookPitch(float Value);
	void MouseLookYaw(float Value);
	void MouseLookPitch(float Value);
	void ControllerLeftX(float Value);
	void ControllerLeftY(float Value);
	void ControllerRightX(float Value);
	void ControllerRightY(float Value);
	void ControllerRightTrigger(float Value);
	void StartCameraDrag();
	void StopCameraDrag();
	void StartCameraPanDrag();
	void StopCameraPanDrag();
	void ZoomCamera(float Value);
	void AdjustRope(float Value);
	void ToggleRope();
	void StartEngineHold();
	void StopEngineHold();
	void StartEngineShutdownHold();
	void StopEngineShutdownHold();
	void Interact();
	void ToggleGamePause();

	// Controller context actions. LB/LT own the right stick while their wheel is open; R3 owns
	// right-stick Y and RB/RT; passenger mode owns X/A/B and D-pad left/right.
	void ControllerDispatchWheelPressed();
	void ControllerDispatchWheelReleased();
	void ControllerToolWheelPressed();
	void ControllerToolWheelReleased();
	void ControllerCameraAdjustPressed();
	void ControllerCameraAdjustReleased();
	void ControllerRightShoulderPressed();
	void ControllerRightShoulderReleased();
	void ControllerPrimaryPressed();
	void ControllerPrimaryReleased();
	void ControllerPassengerPressed();
	void ControllerCancelPressed();
	void ControllerEnterExitPressed();
	void ControllerBackPressed();
	void ControllerSearchLightPressed();
	void ControllerDPadUpPressed();
	void ControllerDPadUpReleased();
	void ControllerDPadDownPressed();
	void ControllerDPadDownReleased();
	void ControllerDPadLeftPressed();
	void ControllerDPadLeftReleased();
	void ControllerDPadRightPressed();
	void ControllerDPadRightReleased();
	void UpdateControllerInput(float DeltaSeconds);
	void UpdateControllerRadialSelection();
	void UpdateControllerToolManipulation();
	void RebuildControllerToolWheel();
	void CloseControllerMode();
	void NormalizeControllerPassengerSelection();
	void StepControllerPassengerSelection(int32 Delta);
	void ConfirmControllerPassengerAction();

	// Key handlers for the four dispatch commands (original command ids 0x16..0x19). Each
	// checks the Shift modifier itself, exactly as FUN_0048a580 tests DAT_0051a078.
	void DispatchFireTruckKey();
	void DispatchAmbulanceKey();
	void DispatchPoliceKey();
	void DispatchPoliceChaseKey();
	bool IsDispatchClearModifierHeld() const;
	void CycleCameraMode();
	void ToggleSearchLight();

	// Debug console commands (routed through the player pawn) to exercise the fire/water work.
	// Radio: no argument cycles to the next station, "off"/"on" switches the set, a call sign
	// or a dial index tunes directly. The dash tuner does the same thing by click.
	UFUNCTION(Exec)
	void SimRadio(const FString& Command);

	UFUNCTION(Exec)
	void SimForceFire();
	UFUNCTION(Exec)
	void SimForceCarFire();
	// Force one mission of the given SimCopterMissions::EType mask into the running session.
	UFUNCTION(Exec)
	void SimStartMission(int32 TypeMask);
	// Log what the plane/boat/train pools are doing.
	UFUNCTION(Exec)
	void SimDumpAmbientVehicles();
	// Park above the nearest tile with this XBLD id (0xd1 hospital, 0xd2 police station), so a
	// roof mechanic can be checked without flying the city looking for one.
	UFUNCTION(Exec)
	void SimGotoBuilding(int32 XbldId);

	// Tool/model debug console commands. These duplicate the debug panel so the transaction
	// can also be exercised headlessly (automation, -game smoke tests).
	UFUNCTION(Exec)
	void SimSwitchHeli(int32 TypeIndex);
	UFUNCTION(Exec)
	void SimCycleHeli(int32 Delta);
	UFUNCTION(Exec)
	void SimSelectTool(int32 ToolIndex);
	UFUNCTION(Exec)
	void SimGrantTool(int32 ToolIndex, int32 bGranted);
	UFUNCTION(Exec)
	void SimDumpHeliState();

	// Emergency dispatch console commands, so F2-F5 can also be exercised headlessly.
	// Service: 0 fire truck, 1 police, 2 ambulance (SimCopterDispatch::EService order).
	UFUNCTION(Exec)
	void SimDispatch(int32 Service);
	UFUNCTION(Exec)
	void SimDispatchChase(int32 Service);
	UFUNCTION(Exec)
	void SimDispatchClear(int32 Service);
	UFUNCTION(Exec)
	void SimDumpDispatchState();
	// Dispatch to an explicit tile instead of the spotlight's, so the drive/arrive/act
	// sequence can be exercised from the ground without flying the beam onto a road.
	UFUNCTION(Exec)
	void SimDispatchTile(int32 Service, int32 TileX, int32 TileY);

	void UpdateEngineState(float DeltaSeconds);

public:
	/**
	 * The engine start/shutdown arbitration, pulled out so it can be tested without a world.
	 * Returns which hold timer, if either, is allowed to advance this frame. Conflicting input
	 * resolves to neither: with both live the two timers take turns and the engine oscillates,
	 * which reads in game as a collective that does nothing and a rotor that will not spool.
	 */
	enum class EEngineHoldAction : uint8 { None, Start, Shutdown };
	static EEngineHoldAction ResolveEngineHoldAction(bool bStartInput, bool bShutdownInput);

private:
	void SimulateFlightStep(float DeltaSeconds);
	void UpdateGroundProbe();
	void UpdateForwardProbe();
	// Ends a death spiral that never reaches the ground. Remake-only; see the definition.
	void UpdateStuckFallWatchdog(float DeltaSeconds);
	float StuckFallSeconds = 0.0f;
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
	// The Apache's gun is held, not pressed: one tracer per frame while the button is down.
	void EmitApacheMachineGunFrame();
	// Rotor-wash "wind kickback" (port of FUN_004881b0): when the helicopter is low over a
	// surface and above the minimum altitude, scatter effect cards under the rotor - spray over
	// water, dust over land. Also emits the bucket douse steam accumulator.
	void UpdateRotorWash(float DeltaSeconds);

	// --- audio (Docs/memory/simcopter-sound.md) ---
	//
	// The engine loop, the spool-up/down pair and the listener, ported from the two functions
	// that own them in the original:
	//   FUN_00488fd0  the rotor loop: nearest-helicopter pick, the rpm -> pitch/volume law, and
	//                 the every-seventh-frame cadence its DAT_00504064 counter imposes.
	//   FUN_00487160  CHOPSTAR / CHOPSTOP / SOFTBMP2 around the Parked state and touchdown.
	void UpdateHelicopterAudio(float DeltaSeconds);

	// One-shots from this step's FSimCopterFlightEvents: EXPLODE, DOUSE, BLDEXPL, SOFTBMP2,
	// MOTOROLD, FIREDMG and GASOUT (FUN_00484d20 / FUN_00489800 / FUN_00489ac0).
	void PlayFlightEventAudio(const FSimCopterFlightEvents& Events);

	// Null unless this pawn is the locally controlled one - `heli[8] & 1` in the original,
	// which is what gates every 2D helicopter sound there.
	class USimCopterAudioSubsystem* GetHelicopterAudio() const;

	// Time since the rotor sound last updated. FUN_00488fd0 runs on every seventh call of the
	// 0.05 s frame, so the port accumulates 0.35 s rather than counting frames.
	float RotorAudioAccumulator = 0.0f;

	// Which engine loop WAV is currently in slot 0, so the per-model SetFile only runs on a
	// change (the original re-issues it on every start, which is equally cheap for it).
	FString ActiveEngineLoopSound;

	// Latches for the level-triggered transitions the original reads off state instead.
	bool bAudioWasFuelStarved = false;

	// The collective this step ran with. FUN_00487160 keys the spool-up/down sounds on
	// heli[3], the collective command, not on the rotor speed it produces.
	int32 LastClimbCommand = 0;

	// Clears every "what is held right now" input cache - axes and action bools alike. All of it
	// goes stale the moment the pawn is unpossessed, and a stuck engine-shutdown bool is what made
	// takeoffs after a job on foot take forever. Called on both edges of a possession change.
	void ResetTransientInputState();
	// Hands keyboard focus back to the game viewport. Under FInputModeGameAndUI a focused Slate
	// widget swallows keys before the axis bindings run.
	void RestoreGameViewportFocus();
	// Releases everything UPlayerInput still believes is held, so a key whose release went missing
	// during a possession change cannot keep reporting itself as down.
	static void FlushStuckKeys(AController* ForController);

	// FSimCopterFlightEnvironment::FireHeightDelta from the last step, so the damage handler
	// can tell FUN_00489800's fire damage from ordinary collision damage.
	int32 LastFlightEnvironmentFireDelta = 0;

	// Where the swept collider actually touched, so the impact burst lands on the contact point
	// rather than at the middle of the airframe. Raised inside ApplyFlightModelToActor and
	// consumed by the effect pass later in the same frame.
	FVector LastImpactWorldLocation = FVector::ZeroVector;
	bool bHasPendingImpactEffect = false;

	ASimCopterMissionSystemActor* ResolveMissionSystem();
	ASimCity2000CityActor* ResolveCityActor() const;
	class ASimCopterTrafficSystemActor* ResolveTrafficSystemActor() const;

	// heli[0x59]'s surface query: the first blocking hit that is not a pawn. People are not
	// landing surfaces - see the comment on the definition.
	bool TraceFlightSurface(
		const FVector& Start,
		const FVector& End,
		const FCollisionQueryParams& QueryParams,
		FHitResult& OutHit) const;

	// Remake airport-landing policy plus the once-per-touchdown latch.
	void UpdateCheckupOffer();
	void UpdateVisuals(float DeltaSeconds);
	void AdvanceCockpitStabilizedAttitude(float DeltaSeconds);
	void UpdateCamera(float DeltaSeconds);
	void UpdateCockpitCannonViewModel(const FRotator& MountWorldRotation);
	void UpdateCameraAnchorFromVisibleBody();
	void LoadCameraViewDebugOffsets();
	void SaveCameraViewDebugOffset(ESimCopterCameraMode Mode) const;
	void LoadCockpitStabilization();
	void SaveCockpitStabilization() const;
	UMaterialInstanceDynamic* GetOrCreateRotorDiscMaterialInstance();
	void ApplyRotorDiscAppearance();
	void LoadRotorDiscAppearance();
	void SaveRotorDiscAppearance() const;
	void LoadCameraGroundLift();
	void SaveCameraGroundLift() const;
	void LoadEasyFlightModel();
	void SaveEasyFlightModel() const;
	void LoadFlightRateTuning();
	void SaveFlightRateTuning() const;
	void UpdateSearchLightEffect();

	// Port of FUN_00489250: aim -> ray march -> smoothing -> band -> tile -> light node.
	void UpdateSpotlightTarget(float DeltaSeconds);
	void AimSpotlightPitch(float Value);
	void AimSpotlightYaw(float Value);
	void AimSpotlightPitchUp() { AimSpotlightPitch(1.0f); }
	void AimSpotlightPitchDown() { AimSpotlightPitch(-1.0f); }
	void StopAimSpotlightPitch() { AimSpotlightPitch(0.0f); }
	void AimSpotlightYawLeft() { AimSpotlightYaw(-1.0f); }
	void AimSpotlightYawRight() { AimSpotlightYaw(1.0f); }
	void StopAimSpotlightYaw() { AimSpotlightYaw(0.0f); }
	// Direction of the aim vector in world space, built the way FUN_00489730 does.
	FVector GetSpotlightAimDirection() const;
	float ResolveCameraGroundLift(
		const FVector& BoomOrigin,
		float ArmLength,
		const FRotator& WorldRotation) const;
	float ResolveCameraPullInAlpha(
		const FVector& PathStart,
		const FVector& DesiredCameraLocation) const;
	FRotator FindCameraAvoidanceOffset(
		const FVector& BoomOrigin,
		float ArmLength,
		const FRotator& DesiredWorldRotation) const;
	bool IsCameraPathClear(
		const FVector& BoomOrigin,
		float ArmLength,
		const FRotator& WorldRotation,
		float ExtraPaddingCm) const;
	bool ProbeBucketWater(const FVector& BucketWorldLocation);
	FString ResolveOriginalGameRoot() const;
	void ApplyDerivedTuning();
	void SyncPassengerFlightModelCount();
	// The Settings screen's HUD Scale row multiplies ToolFlapScale, and the overlays size
	// themselves at construction, so a change means tearing them down and building them again.
	void RebuildCockpitOverlays();

	// ToolFlapScale times the stored HUD scale. Every cockpit overlay is built with this rather
	// than with ToolFlapScale directly.
	float GetCockpitScale() const;

	void EnsureDashboardWidget();
	void RemoveDashboardWidget();
	void RefreshDashboardSeats();
	void EnsureMapWidget();
	void RemoveMapWidget();
	// The original's map zoom commands 0x1b and 0x1c, which input.cfg binds to '=' and '-'.
	void MapZoomIn();
	void MapZoomOut();
	void EnsureWaterControlsWidget();
	void RemoveWaterControlsWidget();
	void RefreshWaterControlsWidget();
	void EnsureToolFlapsWidget();
	void RemoveToolFlapsWidget();
	void EnsureCrosshairWidget();
	void RemoveCrosshairWidget();
	void EnsureControllerOverlayWidget();
	void RemoveControllerOverlayWidget();
	void RefreshControllerOverlayRadials();
	void UpdateCrosshairVisibility();
	void UpdateCrosshairWorldLocation();
	void EnsureHelicopterDebugPanel();
	void RemoveHelicopterDebugPanel();
	// Ctrl+Alt+D. Keeps both developer overlays hidden across a re-possession.
	void ToggleHelicopterDebugPanel();
	FReply HandlePassengerSlotClicked(int32 SlotIndex);
	FVector GetPassengerAirDropWorldLocation(int32 SlotIndex) const;

	void ShowOriginalMesh(bool bUseOriginalMesh);

	// --- Transactional model switching (plan section 7) ---

	// Builds every asset the target model needs without touching a single live component.
	// Records problems in OutPrepared.Errors instead of failing hard so Validate can report
	// all of them at once.
	void PrepareHelicopterModel(int32 TypeIndex, FSimCopterPreparedHelicopterModel& OutPrepared) const;

	// Safety gates: missing assets, too few seats for the onboard passengers, an occupied
	// harness. Returns false with a reason and leaves the live helicopter alone.
	bool ValidateHelicopterModel(const FSimCopterPreparedHelicopterModel& Prepared, FString& OutReason) const;

	// Applies the staged data as one operation, preserving kinematics, engine/rotor state,
	// camera, tool selection, debug grants, and mission association. Fuel and hit points
	// carry over as fractions of the old maxima; water load is clamped to the new maximum.
	void CommitHelicopterModel(FSimCopterPreparedHelicopterModel& Prepared);

	// Mesh/hardpoint half of the commit. Only called once the staged data has validated, so
	// it can replace live procedural sections without risking a half-swapped helicopter.
	void ApplyPreparedModelMeshes(const FSimCopterPreparedHelicopterModel& Prepared);

	// Reads heli.twk into the supplied tuning structs without mutating the pawn.
	bool ReadTuningForSection(
		const FString& SectionName,
		FSimCopterHelicopterTypeTuning& OutHelicopterTuning,
		FSimCopterLandingTuning& OutLandingTuning,
		FSimCopterRopeTuning& OutRopeTuning,
		FSimCopterDamageTuning& OutDamageTuning,
		FString& OutError) const;

	// --- Common tool dispatch (plan section 5.2) ---

	// Runs once per flight substep: consumes the pressed edge, ticks the cooldown, and
	// applies the held tools.
	void UpdateToolDispatch(float DeltaSeconds);
	bool TryBeginToolUse(ESimCopterHelicopterTool Tool);
	void BroadcastMegaphoneMessage();
	void PlayMegaphoneVoice(int32 MessageIndex);

	// Runs the original square-spiral tile scan around Event.TargetTile and routes every
	// eligible object through the shared interaction dispatch. Returns how many reacted.
	int32 BroadcastInteraction(const struct FSimCopterInteractionEvent& Event, int32 Rings);

	// The tile walk both of the above share: hand the event to every agent standing on one of
	// these tiles. FUN_0049a4f0's class routing lives here.
	int32 DeliverInteractionToTiles(
		const struct FSimCopterInteractionEvent& Event,
		const TSet<FIntPoint>& Tiles);

	// FUN_00484d20's heli[0x57] == 3 arm: build the muzzle point/direction/speed and hand them
	// to the pool. False when the pool is full, which refuses the shot before a round is spent.
	bool LaunchTearGasCanister();

	// Shared launch point for the forward-firing tools. True when it came off the cannon barrel
	// or the fuselage nose, false when it fell back to the original's pivot-relative point.
	bool ResolveToolMuzzle(FVector& OutWorld, FVector& OutDirection) const;

	// The fuselage nose in ModelPivot's frame, taken off the built body mesh.
	FVector NoseMuzzleLocalCm = FVector::ZeroVector;
	bool bHasNoseMuzzle = false;

	void RecomputeActiveToolFallback();
	int32 GetModelCapabilityMask() const;
};
