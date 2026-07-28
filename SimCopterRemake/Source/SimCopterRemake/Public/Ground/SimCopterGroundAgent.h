// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Ground/SimCopterBehaviorVM.h"
#include "Ground/SimCopterPopulationFigure.h"
#include "UObject/NoExportTypes.h"
#include "SimCopterGroundAgent.generated.h"

class UCapsuleComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class USceneComponent;
class USpotLightComponent;
class UStaticMeshComponent;
class UTexture2D;

UENUM(BlueprintType)
enum class ESimCopterGroundAgentKind : uint8
{
	Pedestrian,
	Vehicle
};

UCLASS()
class SIMCOPTERREMAKE_API ASimCopterGroundAgent : public AActor, public ISimCopterBehaviorWorld
{
	GENERATED_BODY()

public:
	ASimCopterGroundAgent();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	void ConfigureAgent(
		ESimCopterGroundAgentKind NewAgentKind,
		const FString& NewMeshTableName,
		const FString& NewOriginalGameRoot,
		float NewMovementSpeedCmPerSec);

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	void SetInitialBehaviorClass(int32 NewInitialBehaviorClass);

	void SetInitialBehaviorProgramId(int32 NewInitialBehaviorProgramId);
	void SetPedestrianFigureClothesOffset(int32 NewClothesOffset);

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	bool LoadOriginalMeshFromOriginalGameRoot();

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	bool LoadOriginalPedestrianSpriteFromOriginalGameRoot();

	// Builds the procedural low-poly 3D pedestrian body (replaces the old flat sprite).
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	bool BuildPedestrianBody();

	// Builds the pedestrian as an original privanim.df figure with its real animation clips
	// (falls back to the procedural box body when the original data is unavailable).
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	bool BuildPedestrianFigure();

	// Immediately drops the agent onto the ground beneath it (used right after spawn so the
	// very first frame is grounded instead of floating).
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	void SnapToGroundImmediate();

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	void SetMoveTarget(const FVector& NewTargetLocation);

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	void ClearMoveTarget();

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	bool HasMoveTarget() const { return bHasMoveTarget; }

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	bool IsNearMoveTarget(float DistanceCm = 90.0f) const;

	FVector GetMoveTargetLocation() const { return MoveTargetLocation; }
	FVector GetCurrentVelocityCmPerSec() const { return CurrentVelocityCmPerSec; }
	float GetCollisionRadiusCm() const;

	void SetTrafficSpeedScale(float NewSpeedScale);
	void LimitTrafficSpeedScale(float MaxSpeedScale);
	void ApplyTrafficBrake(float MaxSpeedScale, float DeltaSeconds, float BrakeRate);
	void AddTrafficVelocityImpulse(const FVector& ImpulseCmPerSec);
	void MoveByTrafficSeparation(const FVector& WorldDelta);
	void SetAvoidanceMoveTarget(const FVector& NewTargetLocation, float DurationSeconds, float SpeedMultiplier = 1.0f);
	void SetAvoidancePathOffset(const FVector& NewWorldOffset, float DurationSeconds, float SpeedMultiplier = 1.0f);
	void SetGuidanceMoveTarget(const FVector& NewTargetLocation, float DurationSeconds);
	bool IsAvoidanceMoveActive() const { return AvoidanceMoveTimeRemainingSeconds > 0.0f; }
	bool IsAvoidancePathOffsetActive() const { return AvoidancePathOffsetTimeRemainingSeconds > 0.0f; }
	bool IsGuidanceMoveTargetActive() const { return GuidanceMoveTargetTimeRemainingSeconds > 0.0f; }
	bool HasMissionPickupCreditAwarded() const { return bMissionPickupCreditAwarded; }
	void SetMissionPickupCreditAwarded(bool bAwarded) { bMissionPickupCreditAwarded = bAwarded; }

	bool SetForcedPedestrianFigureClip(const FString& Mnemonic);
	void ClearForcedPedestrianFigureClip();
	void SetMissionInjuredPose();

	// Marks an uninjured victim who still needs picking up. They keep whatever program or carrier
	// they are on, but any moment it leaves them standing still they wave for the helicopter
	// instead of idling. The original binds the same "Wave" clip when a person notices the player
	// (behavior op 22: face the player, bind "Wave", wait 15).
	void SetMissionAwaitingRescue(bool bAwaiting) { bMissionWavesWhenIdle = bAwaiting; }
	void ClearMissionPose();
	void ResumeNormalPedestrianBehavior();
	void SetCarriedBy(USceneComponent* CarryParentComponent, const FVector& RelativeLocation, const FRotator& RelativeRotation);
	bool IsMissionCarried() const { return bMissionCarried; }

	// Detach from a carrier and set the agent back down as an injured pickup at the given world
	// location (used when the player presses drop, or a carrier releases them on the ground).
	void SetDroppedInjuredOnGround(const FVector& WorldLocation);

	// Starts a visible passenger fall from the current airborne position. When the landing impact
	// is too large, the agent becomes an injured medevac victim owned by a new no-reward mission.
	void BeginPassengerFall(int32 SourceEventId, float InjuryDistanceCm);

	// Turns the agent into a script-driven mover: no behavior VM, no ground snapping (its owner
	// keeps it on a chosen plane), driven purely by SetMoveTarget. Used for the hospital EMT.
	void SetMissionScriptedMover();

	// Choose the privanim figure this agent renders (e.g. "Medik"). Only takes effect before the
	// figure is built (i.e. before ConfigureAgent).
	void SetPedestrianFigureName(const FString& NewFigureName) { PedestrianFigureName = NewFigureName; }

	float GetCapsuleHalfHeightCm() const;

	// Road/sidewalk graph route state, driven by ASimCopterTrafficSystemActor. TargetNode is the
	// graph node the agent is currently driving toward; PrevNode is where it came from (used to
	// avoid immediate U-turns). INDEX_NONE means "unset / re-acquire nearest node".
	void SetRouteState(int32 TargetNode, int32 PrevNode, int32 PlannedNextNode = INDEX_NONE)
	{
		RouteTargetNodeIndex = TargetNode;
		RoutePrevNodeIndex = PrevNode;
		RoutePlannedNextNodeIndex = PlannedNextNode;
	}
	int32 GetRouteTargetNode() const { return RouteTargetNodeIndex; }
	int32 GetRoutePrevNode() const { return RoutePrevNodeIndex; }
	int32 GetRoutePlannedNextNode() const { return RoutePlannedNextNodeIndex; }

	// --- Speeder / criminal car (FUN_004b8470's class, message id 0x11e) ---------------------
	// Turns this vehicle into the mission's speeder. The traffic system drives the rest; see
	// SimCopterCriminalCar.h for the decode.
	void MakeCriminalCar(int32 InEventId);
	bool IsCriminalCar() const { return bCriminalCar; }
	int32 GetCriminalEventId() const { return CriminalEventId; }

	// obj[5] & 8 - the fleeing flag the police target filter tests. A criminal car always flies
	// it; the same flag is what would make a speeder *person* a valid target.
	bool IsFleeing() const { return bFleeing; }
	void SetFleeing(bool bInFleeing) { bFleeing = bInFleeing; }

	// obj[0x11b] - how long the player's searchlight has been on it, 0..10 (FUN_004a01f0).
	int32 GetSpotlightMark() const { return SpotlightMark; }
	void SetSpotlightMark(int32 NewMark) { SpotlightMark = NewMark; }

	// obj[0x12b] - the state FUN_004b8630 switches on, and FUN_004b89a0 gates the stop order on.
	uint8 GetCriminalState() const { return CriminalState; }
	void SetCriminalState(uint8 NewState) { CriminalState = NewState; }

	// veh[4] & 0x10 / & 0x20: the stop has been ordered, and the car has actually come to rest.
	bool IsStopOrdered() const { return bStopOrdered; }
	bool IsStopped() const { return bStopped; }

	// FUN_004b89a0 -> FUN_0049e0c0. Returns true when the order was accepted, which is only when
	// the car has been marked by the spotlight first.
	bool TryOrderStop(int32 CallerMessageId);

	// FUN_004b8b60's hold before the car is removed.
	float GetArrestHoldSeconds() const { return ArrestHoldSeconds; }
	void SetArrestHoldSeconds(float NewSeconds) { ArrestHoldSeconds = NewSeconds; }

	// How much of its road speed a pulling-over car still has. The traffic pass resets every
	// vehicle's speed scale to 1 each frame, so a stop has to be re-asserted from this rather
	// than written once - which is what let an "arrested" car drive away.
	float GetCriminalStopScale() const { return CriminalStopScale; }
	void SetCriminalStopScale(float NewScale) { CriminalStopScale = NewScale; }
	void MarkStopped() { bStopped = true; }

	// The map tile this agent is standing on. Same answer as the behaviour-world override below,
	// which is private because it is part of that interface rather than this actor's API.
	bool TryGetTileCoordinate(int32& OutFileX, int32& OutFileY) const
	{
		return TryGetCurrentTileCoordinate(OutFileX, OutFileY);
	}

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	ESimCopterGroundAgentKind GetAgentKind() const { return AgentKind; }

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	bool IsUsingOriginalMesh() const { return bUsingOriginalMesh; }

	// Port of the person branch of FUN_0049a4f0 -> FUN_004c1050: look the interaction mode up
	// in the reaction table and push that BHAV onto the behaviour stack, honouring the
	// original's acceptance tests and interrupt priority. Returns true when the person
	// actually reacted.
	bool ApplyInteraction(const struct FSimCopterInteractionEvent& Event);

	// The reaction currently running on this agent (INDEX_NONE when none).
	int32 GetActiveReactionProgramId() const { return BehaviorContext.ActiveReactionProgramId; }

	// Megaphone message the agent last received (person+0x15a).
	int32 GetLastMegaphoneMessage() const { return BehaviorContext.MegaphoneMessageIndex; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UCapsuleComponent> CollisionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<USceneComponent> VisualRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UProceduralMeshComponent> OriginalMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UStaticMeshComponent> ProxyMeshComponent;

	// Vehicle headlights. The original cars carry translucent "headlight beam" cards (Maxis
	// face type 11) in front of the body; the remake strips those and drives real spotlights
	// instead, so the beams light the road at night rather than rendering as opaque blocks.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<USpotLightComponent> HeadlightLeft;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<USpotLightComponent> HeadlightRight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Ground Agent")
	ESimCopterGroundAgentKind AgentKind = ESimCopterGroundAgentKind::Pedestrian;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Ground Agent")
	FString MeshTableName;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Assets")
	FDirectoryPath OriginalGameRoot;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Assets", meta = (ClampMin = "1.0"))
	float ModelUnitsPerCentimeter = 2621.44f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Assets", meta = (ClampMin = "0.001"))
	float VehicleModelScale = 0.25f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Assets", meta = (ClampMin = "0.001"))
	float PedestrianModelScale = 0.25f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Assets")
	bool bRenderModelBackfaces = true;

	// Prefer the decoded privanim.df figures over the procedural box people.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Original Assets")
	bool bUseOriginalFigures = true;

	// Explicit figure to use ("pilot", "Kopp", "Elvis", ...); empty picks a stable random
	// entry from PedestrianFigurePool.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Original Assets")
	FString PedestrianFigureName;

	// Everyday street mix; service figures (Kopp/Medik/Fireman) and easter eggs (Elvis,
	// Nessie, Coww) can be requested explicitly by missions/spawners.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Original Assets")
	TArray<FString> PedestrianFigurePool;

	// Original-figure animation playback rate in frames per second (walk clips are 8 frames).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Animation", meta = (ClampMin = "0.1"))
	float FigureFrameRate = 8.0f;

	// Run pedestrians on the original people.df behavior programs (the shipped BHAV bytecode,
	// interpreted by FSimCopterBehaviorVM). Falls back to the waypoint wander when the data or
	// the figure model is unavailable.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Behavior")
	bool bUseOriginalBehaviors = true;

	// Behavior VM ticks per second. The original ran behavior once per game frame (~15fps era
	// pacing); movement distance, walk-clip frame rate and idle durations all scale with this.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Behavior", meta = (ClampMin = "1.0"))
	float BehaviorTickRate = 15.0f;

	// Original per-step vertical gates (FUN_004c9470): one behavior step may rise at most
	// MaxStepClimb units (person+0x144 = 5) and drop at most MaxStepClimb + 0.5 units;
	// 1 unit = tile/64 (~6.25cm at a 400cm tile). This is what stops people at building
	// walls in the original - tile classes alone allow building tiles.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Behavior", meta = (ClampMin = "0.0"))
	float MaxStepClimbOriginalUnits = 5.0f;

public:
	// Initial person state (0 = ambient pedestrian; see FPeopleBehaviorModel state table).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Behavior", meta = (ClampMin = "0", ClampMax = "20"))
	int32 InitialPersonState = 0;

	// Initial behavior class at original person+0x146. Ambient city spawning chooses this from
	// DAT_0058ec00/FUN_004c2450 while state remains 0; it also selects the figure (FUN_004c71c0).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Behavior", meta = (ClampMin = "0", ClampMax = "21"))
	int32 InitialBehaviorClass = 0;

	// Optional direct BHAV entry id for scripted building spawns (FUN_004c20b0 callers such as
	// baseball batter/fielders and park ambient people).
	int32 InitialBehaviorProgramId = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Behavior")
	int32 MissionEventId = INDEX_NONE;

	// Vehicle headlight spotlights (replace the removed translucent beam cards).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Headlights")
	bool bEnableVehicleHeadlights = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Headlights", meta = (ClampMin = "0.0"))
	float HeadlightIntensity = 9000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Headlights", meta = (ClampMin = "100.0"))
	float HeadlightAttenuationRadiusCm = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Headlights")
	FColor HeadlightColor = FColor(255, 244, 214);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float MovementSpeedCmPerSec = 420.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float TurnRateDegPerSec = 420.0f;

	// Downward reach of the ground probe, below the capsule bottom.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float GroundProbeDistanceCm = 4000.0f;

	// How far above the agent the ground probe starts. Kept large so placement survives a
	// mismatch between the spawner's terrain estimate and the city's actual rendered surface
	// (the reason agents used to hover in the air).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float GroundProbeUpCm = 2500.0f;

	// Pedestrians only: the ground probe starts this far above the tile's terrain altitude
	// instead of high above the agent. The original's max-climb gate (person+0x144 = 5 units of
	// a 64-unit tile) scales to ~31cm at a 400cm tile, and one-story roofs sit around ~150cm at
	// this scale, so the probe must start well below that or it lands people on small buildings.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float PedestrianGroundProbeStartAboveTerrainCm = 40.0f;

	// Extra probe headroom on non-building tiles only. Buildings stand on flat terrain, but
	// parks/trees/roads can slope, where the real surface rises up to half a terrain step above
	// the tile-center altitude (and there is no roof to catch the probe).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement", meta = (ClampMin = "0.0"))
	float PedestrianGroundProbeSlopeHeadroomCm = 110.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement")
	bool bSnapToGround = true;

	// Pedestrians are affected by gravity: instead of teleporting to the surface each tick they
	// fall onto it (when spawned in the air or when they walk off a ledge). This is the downward
	// acceleration used for that fall (vehicles keep instant placement).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement", meta = (ClampMin = "0.0"))
	float GravityCmPerSec2 = 980.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Animation")
	bool bEnableJankyAnimation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Animation", meta = (ClampMin = "0.0"))
	float JankyAnimationRate = 7.5f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	bool bUsingOriginalMesh = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	FString LastMeshLoadError;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> VertexColorMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SpriteMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SpriteMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> PedestrianSpriteTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FigureHeadTexture;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FigureHeadMaterialInstance;

private:
	FVector MoveTargetLocation = FVector::ZeroVector;
	FVector CurrentVelocityCmPerSec = FVector::ZeroVector;
	FVector ExternalVelocityCmPerSec = FVector::ZeroVector;
	float VerticalVelocityCmPerSec = 0.0f;
	FVector AvoidanceMoveTargetLocation = FVector::ZeroVector;
	FVector AvoidancePathOffset = FVector::ZeroVector;
	FVector GuidanceMoveTargetLocation = FVector::ZeroVector;
	bool bHasMoveTarget = false;
	float TrafficSpeedScale = 1.0f;
	float AvoidanceMoveTimeRemainingSeconds = 0.0f;
	float AvoidancePathOffsetTimeRemainingSeconds = 0.0f;
	float GuidanceMoveTargetTimeRemainingSeconds = 0.0f;
	float AvoidanceSpeedMultiplier = 1.0f;
	float AvoidancePathOffsetSpeedMultiplier = 1.0f;
	float AnimationTimeSeconds = 0.0f;
	float AnimationPhase = 0.0f;
	int32 PedestrianSpriteColumn = 0;
	int32 PedestrianSpriteRow = INDEX_NONE;
	int32 PedestrianOutfitIndex = 0;
	int32 RouteTargetNodeIndex = INDEX_NONE;
	int32 RoutePrevNodeIndex = INDEX_NONE;
	int32 RoutePlannedNextNodeIndex = INDEX_NONE;

	// Speeder / criminal car state. Offsets in the comments are the original's, on the class
	// FUN_004b8470 builds.
	bool bCriminalCar = false;    // message id 0x11e
	bool bFleeing = false;        // obj[5] & 8
	bool bStopOrdered = false;    // veh[4] & 0x10
	bool bStopped = false;        // veh[4] & 0x20
	int32 CriminalEventId = INDEX_NONE; // +0x113
	int32 SpotlightMark = 0;      // +0x11b
	uint8 CriminalState = 0;      // +0x12b
	float ArrestHoldSeconds = 0.0f; // +0x10, armed to 0x780000 by FUN_004b8b60
	float CriminalStopScale = 1.0f; // stands in for the stop distance at +0xd3
	bool bUsingPedestrianSprite = false;
	bool bUsingPedestrianBody = false;

	// Original privanim figure state.
	TSharedPtr<FSimCopterPrivAnimShared> FigureShared;
	FSimCopterPopulationFigure::FCalibration FigureCalibration;
	FString FigureMnemonic;
	int32 FigureIndex = INDEX_NONE;
	int32 FigureFrameCount = 0;
	int32 FigureCurrentFrame = 0;
	int32 FigureClothesOffset = 0;
	int32 FigureHeadIndex = 0;
	float FigureFrameTime = 0.0f;
	bool bFigureHasHeadSection = false;
	bool bUsingPedestrianFigure = false;
	FString ForcedFigureMnemonic;
	int32 ForcedFigureClothesOffset = INDEX_NONE;
	bool bMissionWavesWhenIdle = false;
	bool bMissionStationary = false;
	bool bMissionCarried = false;
	bool bMissionPickupCreditAwarded = false;
	bool bPassengerFallActive = false;
	bool bPassengerFallStarted = false;
	float PassengerFallStartZ = 0.0f;
	float PassengerFallInjuryDistanceCm = 900.0f;
	int32 PassengerFallSourceEventId = INDEX_NONE;

	bool RebuildFigureClip(const FString& Mnemonic);
	void UpdateFigureAnimation(float DeltaSeconds, float SpeedAlpha);

	// Original behavior-VM state (pedestrians only).
	TSharedPtr<FPeopleBehaviorModel> BehaviorModel;
	FSimCopterPersonContext BehaviorContext;
	float BehaviorTickAccumulator = 0.0f;
	bool bBehaviorActive = false;
	TSet<int32> ReportedUnknownOpcodes;

	// Per-tick move command (original FUN_004c9300 semantics): each successful MoveStep renews
	// a constant velocity that UpdateMovement integrates until the next behavior tick. There is
	// no target seeking or deceleration - the original displaces the person every tick.
	FVector BehaviorStepVelocityCmPerSec = FVector::ZeroVector;
	float BehaviorStepTimeRemainingSeconds = 0.0f;
	int32 LastAppliedBehaviorFacing = INDEX_NONE;

	void StartOriginalBehavior();
	void ResetBehaviorProgramOverride();
	void UpdateOriginalBehavior(float DeltaSeconds);
	void ApplyBehaviorFacingRotation();
	void AdvanceBehaviorFigureFrames(int32 TickCount);
	// The walked surface at a step target: highest blocking geometry at that column, falling
	// back to the tile's terrain altitude (port of FUN_004c82c0 = max of object tops/terrain).
	bool TryGetWalkSurfaceZAt(const FVector& WorldLocation, float& OutSurfaceZ) const;

	// ISimCopterBehaviorWorld
	virtual int32 GetCurrentTileClass() const override;
	virtual bool TryGetCurrentTileCoordinate(int32& OutFileX, int32& OutFileY) const override;
	virtual bool IsTileClassAllowedForState(int32 StateIndex, int32 TileClass) const override;
	virtual bool MoveStep(FSimCopterPersonContext& Context) override;
	virtual bool IsThreatNearby(const FSimCopterPersonContext& Context) const override;
	virtual bool TryGetPlayerTileProbe(
		const FSimCopterPersonContext& Context,
		FSimCopterBehaviorPlayerTileProbe& OutProbe) const override;
	virtual void OnUnknownOpcode(int32 Opcode) override;

	void ApplyAgentShape();
	void UpdateMovement(float DeltaSeconds);
	void UpdateGroundSnap(float DeltaSeconds);
	void UpdateJankyAnimation(float DeltaSeconds);
	void ShowOriginalMesh(bool bUseOriginalMesh);
	void ConfigureVehicleHeadlights(const FBox& VehicleLocalBounds);
	void DisableVehicleHeadlights();
	bool TraceGround(FVector& OutGroundLocation) const;
	void FinishPassengerFall(float FallDistanceCm);
	FString ResolveOriginalGameRoot() const;
};
