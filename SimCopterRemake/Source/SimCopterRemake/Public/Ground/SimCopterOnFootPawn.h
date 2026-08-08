// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Ground/SimCopterPopulationFigure.h"
#include "SimCopterOnFootPawn.generated.h"

class ASimCity2000CityActor;
class ASimCopterHelicopterPawn;
class ASimCopterGroundAgent;
class UAudioComponent;
class UCameraComponent;
class UCapsuleComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class UTexture2D;
class SWidget;

UCLASS()
class SIMCOPTERREMAKE_API ASimCopterOnFootPawn : public ACharacter
{
	GENERATED_BODY()

public:
	ASimCopterOnFootPawn();

	virtual void BeginPlay() override;
	// Keyboard focus has to come back to the viewport on every possession, or the pawn's axis
	// bindings never see the keys - see the helicopter's RestoreGameViewportFocus.
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// Backed by the character movement velocity (used by nearby NPCs to gauge the player's speed).
	FVector GetCurrentVelocityCmPerSec() const { return GetVelocity(); }
	float GetWalkSpeedCmPerSec() const { return WalkSpeedCmPerSec; }
	bool IsCarryingMissionPerson() const { return CarriedMissionPerson.IsValid(); }
	int32 GetCarriedMissionEventId() const { return CarriedMissionEventId; }
	bool PickUpMissionPerson(ASimCopterGroundAgent* MissionPerson);
	ASimCopterGroundAgent* ConsumeCarriedMissionPerson();
	// Player-side live snapshot used when the save was made outside the helicopter: movement,
	// view/animation state and the exact mission person currently being carried.
	bool CaptureRuntimeSaveState(TArray<uint8>& OutData) const;
	bool RestoreRuntimeSaveState(const TArray<uint8>& Data);

	// False for a short window after the player manually drops a person, so the auto-pickup logic
	// does not instantly re-grab someone they just set down on the ground.
	bool CanPickUpMissionPersonNow() const { return MissionPickupCooldownSeconds <= 0.0f; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UStaticMeshComponent> BodyProxyComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UProceduralMeshComponent> OriginalBodySpriteComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UCameraComponent> CameraComponent;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Ground Start")
	TSubclassOf<ASimCopterHelicopterPawn> HelicopterClass;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Ground Start")
	bool bFindOrSpawnParkedHelicopterOnBeginPlay = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Ground Start", meta = (ClampMin = "100.0"))
	float ParkedHelicopterSearchRadiusCm = 7000.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Ground Start")
	FVector ParkedHelicopterOffset = FVector(160.0f, 60.0f, 0.0f);

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Assets")
	FDirectoryPath OriginalGameRoot;

	// Both reaches are gaps between the avatar's own body and the *airframe mesh*
	// (ASimCopterHelicopterPawn::GetDistanceToAirframeCm), not radii about the helicopter's
	// origin. The old radii were 620 cm and 145 cm measured from that origin: in a world where a
	// city tile is 400 cm and the avatar is 46 cm tall, the interaction reach covered a tile and a
	// half in every direction, and the auto-enter bubble swallowed the whole fuselage plus about a
	// metre of clear air - so the player was boarding from beside the aircraft, never at it.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Interaction", meta = (ClampMin = "0.0"))
	float HelicopterInteractionReachCm = 60.0f;

	// Walking into the aircraft boards it. This is skin contact plus a hair of slack for the
	// discrete movement step, not a proximity bubble.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Interaction", meta = (ClampMin = "0.0"))
	float HelicopterAutoEnterReachCm = 4.0f;

	// Forward walk speed. The avatar is only ~46cm tall in this 0.25x-scale world, so this is
	// already several body heights a second; the original's pedestrian shuffle was far slower
	// still, but that is deliberately not matched.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float WalkSpeedCmPerSec = 270.0f;

	// Side-step speed as a fraction of WalkSpeedCmPerSec. Strafing at full walk speed reads as a
	// skate; halving it keeps sideways movement usable for lining up a pickup without it
	// outrunning the walk cycle.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float StrafeSpeedScale = 0.5f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float MaxAccelerationCmPerSec2 = 3000.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float GroundProbeDistanceCm = 25000.0f;

	/** How far above the desired spot the ground probe starts. Just enough to clear the surface we
	    are meant to land on - never enough to catch the roof of an adjacent building. */
	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "0.0"))
	float GroundProbeLiftCm = 40.0f;

	// Tallest vertical lip the avatar walks up automatically (curbs, road/sidewalk edges). Handled
	// by the character movement component's step-up; taller obstacles still block.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "0.0"))
	float MaxStepHeightCm = 34.0f;

	// Jump launch speed (spacebar). Tuned for the 0.25x-scale world so it clears curbs and low
	// obstacles without launching the avatar over buildings.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "0.0"))
	float JumpZVelocityCmPerSec = 320.0f;

	// Horizontal control authority while airborne (0 = none, 1 = full ground control).
	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AirControl = 0.65f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "0.0"))
	float GravityScale = 1.0f;

	// Seconds after a manual drop during which the dropped person is not auto-picked back up.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Interaction", meta = (ClampMin = "0.0"))
	float MissionDropRepickupCooldownSeconds = 1.5f;

	// Where a casualty the player is carrying rides, relative to the avatar's capsule. X is out in
	// front of the chest: enough not to intersect the body, not so much that they float along ahead
	// of it. The figure is laid across the carrier by the rotation applied alongside this.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Interaction")
	FVector CarriedMissionPersonOffsetCm = FVector(16.0f, 0.0f, -7.0f);

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "1.0"))
	float LookYawSpeedDegPerSec = 155.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "1.0"))
	float LookPitchSpeedDegPerSec = 95.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	TObjectPtr<ASimCopterHelicopterPawn> ParkedHelicopter;

	/**
	 * `M_SimCopterLitSpriteTexture` - the pilot's head is an ordinary LIT surface, not a card that
	 * emits. See `ASimCopterGroundAgent::FigureHeadMaterial`; this is the same head, same reason.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> FigureHeadMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SpriteMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BodySpriteTexture;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BodyVertexColorMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FigureHeadTexture;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FigureHeadMaterialInstance;

	// The player's original figure ("pilot" - you are the SimCopter pilot). Empty disables.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Assets")
	FString PlayerFigureName = TEXT("pilot");

	// Playback rate of the privanim clips, in frames per second. The walk clip is 8 frames for a
	// full two-step cycle, so at the avatar's scale a slow rate reads as gliding: the stride has
	// to keep up with the ground covered.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Assets", meta = (ClampMin = "1.0"))
	float FigureWalkFrameRate = 16.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Assets", meta = (ClampMin = "1.0"))
	float FigureIdleFrameRate = 8.0f;

private:
	float LookYawInput = 0.0f;
	float LookPitchInput = 0.0f;
	float MouseLookYawInput = 0.0f;
	float MouseLookPitchInput = 0.0f;
	float CameraPitchDeg = -12.0f;
	int32 BodySpriteRow = INDEX_NONE;
	float BodySpriteTimeSeconds = 0.0f;
	bool bUsingOriginalBodySprite = false;
	TWeakObjectPtr<ASimCopterGroundAgent> CarriedMissionPerson;
	int32 CarriedMissionEventId = INDEX_NONE;
	float MissionPickupCooldownSeconds = 0.0f;
	TSharedPtr<SWidget> ControllerOverlayWidget;

	// Original pilot-figure state (mirrors the ground agent's figure path).
	TSharedPtr<FSimCopterPrivAnimShared> FigureShared;
	struct FSimCopterFigureAnimState
	{
		FString Mnemonic;
		int32 FigureIndex = INDEX_NONE;
		int32 FrameCount = 0;
		int32 CurrentFrame = 0;
		float FrameTime = 0.0f;
		bool bHasHeadSection = false;
	} FigureAnim;
	bool bUsingOriginalFigure = false;
	// The original player-person is class 19 (pilot), so its own looping walking voice is boots.
	TWeakObjectPtr<UAudioComponent> WalkingSoundComponent;
	void UpdateWalkingSound();
	void StopWalkingSound();

	bool LoadOriginalBodyFigure();
	bool RebuildPlayerFigureClip(const FString& Mnemonic);

	// --- standing in water ---
	// The player gets exactly what the pedestrians get (ASimCopterGroundAgent::UpdateWaterSubmersion):
	// step onto a water tile and the body sinks to the waist over WaterSubmergeLerpSeconds, then rides
	// M_SimCopterWater's swell. Visual only - the character capsule keeps standing on the sea's rest
	// plane, so movement, boarding reach and the ground snap are untouched.
	UPROPERTY(EditAnywhere, Category = "SimCopter|OnFoot", meta = (ClampMin = "0.0"))
	float WaterSubmergeLerpSeconds = 0.25f;

	float WaterSubmergeAlpha = 0.0f;
	float WaterVisualOffsetCm = 0.0f;
	TWeakObjectPtr<ASimCity2000CityActor> CachedCityActor;

	void UpdateWaterSubmersion(float DeltaSeconds);
	bool IsStandingInWater(const ASimCity2000CityActor& City) const;
	ASimCity2000CityActor* ResolveCityActor();

	void MoveForward(float Value);
	void MoveRight(float Value);
	void LookYaw(float Value);
	void LookPitch(float Value);
	void MouseLookYaw(float Value);
	void MouseLookPitch(float Value);
	void ControllerLookPitch(float Value);
	void Interact();
	void DropCarriedMissionPerson();
	void ToggleGamePause();
	bool TryBoardCarriedMissionPerson(ASimCopterHelicopterPawn* Helicopter);
	void TryAutoEnterHelicopter();
	void TryEnterHelicopter(float ReachCm);
	void EnsureControllerOverlayWidget();
	void RemoveControllerOverlayWidget();

	// Board the nearest helicopter regardless of walking distance. Test scaffolding: a
	// -game smoke test drives the shell through Slate, which never possesses the
	// helicopter, so its keys and Exec commands stay unreachable without this.
	UFUNCTION(Exec)
	void SimBoardHelicopter();

	// Force one mission of the given type mask (SimCopterMissions::EType) into the running
	// session, for verifying a type without waiting for the scheduler to roll it.
	UFUNCTION(Exec)
	void SimStartMission(int32 TypeMask);

	// Log what the plane/boat/train pools are doing right now.
	UFUNCTION(Exec)
	void SimDumpAmbientVehicles();

	// Jump beside one of them: 0 train, 1 capsized boat, 2 plane, 3 nearest wreck. Reading a
	// position out of the log and teleporting to it is always a second or two stale, which is
	// enough for a running train to have left.
	UFUNCTION(Exec)
	void SimGotoAmbient(int32 Which);

	void UpdateLookYaw(float DeltaSeconds);
	void UpdateCamera(float DeltaSeconds);
	void UpdateBodySprite(float DeltaSeconds);
	void LoadOriginalBodySprite();
	void SnapToGround();
	void FindOrSpawnParkedHelicopter();
	ASimCopterHelicopterPawn* FindNearestHelicopter(float SearchRadiusCm) const;
	// The nearest helicopter whose *airframe* is within ReachCm of the avatar's body, ranked by
	// that gap rather than by distance to its origin - a long fuselage two metres away can
	// otherwise be "nearer" than the one you are standing against.
	ASimCopterHelicopterPawn* FindHelicopterWithinReach(float ReachCm) const;
	bool ResolveGroundedLocation(const FVector& DesiredLocation, float ActorHalfHeight, FVector& OutLocation) const;
};
