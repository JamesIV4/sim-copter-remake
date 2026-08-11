// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Ground/SimCopterBehaviorVM.h"
#include "Ground/SimCopterPopulationFigure.h"
#include "UObject/NoExportTypes.h"
#include "SimCopterGroundAgent.generated.h"

class ASimCopterHelicopterPawn;
class UAudioComponent;
class UCapsuleComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
enum class ESimCopterMissionPassengerKind : uint8;
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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
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

	// Shared vertical gate for every behavior-VM pedestrian step. Rendered buildings are not the
	// original cell objects, so any BHAV 308 escape that permits a rise must permit the inverse
	// descent too or a walker can enter a roof surface that it can never leave.
	static bool IsPedestrianHeightTransitionAllowed(float RiseCm, float MaxStepCm, bool bMoveThroughWalls);

	// How high a downward walk-surface probe may start: the walker's own feet plus everything they
	// could step onto, and not one centimetre more.
	//
	// FUN_004c82c0 answers with the tallest object ON THE CELL, which in a 2.5D city is always
	// something the walker is standing under or on. The remake asks rendered geometry the same
	// question, and there the two differ: a bridge arch, a power span, an elevated rail deck or a
	// first-floor overhang passes OVER cells whose ground is the street. A probe that starts above
	// the world and takes its first hit returns that span, so every mesh behaves as a solid box
	// extruded from its highest point down to the terrain - which is exactly the invisible wall
	// pedestrians (police included) were stopping at. Starting the probe at this ceiling makes the
	// first hit the highest surface they could actually step onto, and leaves overhead geometry
	// where it belongs: overhead. Lateral obstruction is a separate question, answered against the
	// real mesh by IsPedestrianStepBlockedByGeometry.
	static float GetPedestrianWalkProbeCeilingZ(float FeetZ, float MaxStepClimbCm, float MarginCm);

	// The swept body IsPedestrianStepBlockedByGeometry pushes from the current position to the step
	// target. Its bottom sits one climb allowance above the higher of the two surfaces so kerbs,
	// road lips and the step itself pass underneath it, and it stops one climb allowance short of
	// full body height so it never scrapes a ceiling the walker fits under.
	// Which of the three things ContainOutsideBuildingGeometry does with a frame's horizontal
	// displacement: too small to be worth a scene query, too large to be a walk (a teleport -
	// boarding, alighting, mission placement, a restored save - where the anchor is meaningless and
	// must be dropped), or a real step to sweep.
	enum class EWallContainmentStep : uint8
	{
		Ignore,
		Rebase,
		Sweep,
	};
	static EWallContainmentStep ClassifyWallContainmentStep(
		float MovedDistanceCm,
		float MinDistanceCm,
		float MaxDistanceCm);

	static void ComputePedestrianStepSweepShape(
		float SourceFeetZ,
		float TargetSurfaceZ,
		float MaxStepClimbCm,
		float CapsuleRadiusCm,
		float CapsuleHalfHeightCm,
		float RadiusScale,
		float& OutRadiusCm,
		float& OutHalfHeightCm,
		float& OutCenterZ);

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
	// SpeedCmPerSec <= 0 keeps the agent's own MovementSpeedCmPerSec. Pass the shipped program's
	// walk speed instead, or a guided passenger sprints to the aircraft at the generic pedestrian
	// speed while their BHAV thinks it is walking at `movespeed`.
	void SetGuidanceMoveTarget(
		const FVector& NewTargetLocation,
		float DurationSeconds,
		float SpeedCmPerSec = 0.0f);

	// BHAV 291 rec[0] / BHAV 305 rec[5] both set `movespeed := 16`, and one behaviour tick
	// displaces movespeed/12 original units. At 15 Hz and 6.25 cm per unit that is 125 cm/s - the
	// speed a passenger walks to the helicopter at in the shipped game.
	static constexpr float ShippedPassengerWalkSpeedCmPerSec = 16.0f / 12.0f * 6.25f * 15.0f;
	bool IsAvoidanceMoveActive() const { return AvoidanceMoveTimeRemainingSeconds > 0.0f; }
	bool IsAvoidancePathOffsetActive() const { return AvoidancePathOffsetTimeRemainingSeconds > 0.0f; }
	bool IsGuidanceMoveTargetActive() const { return GuidanceMoveTargetTimeRemainingSeconds > 0.0f; }
	bool HasMissionPickupCreditAwarded() const { return bMissionPickupCreditAwarded; }
	void SetMissionPickupCreditAwarded(bool bAwarded) { bMissionPickupCreditAwarded = bAwarded; }
	bool IsMissionPickupCounted() const { return bMissionPickupCounted; }
	void SetMissionPickupCounted(bool bCounted) { bMissionPickupCounted = bCounted; }
	bool HasMissionResolutionReported() const { return bMissionResolutionReported; }
	void SetMissionResolutionReported(bool bReported) { bMissionResolutionReported = bReported; }
	bool IsMissionPatientDead() const { return bMissionPatientDead; }
	void ResetMissionActionTracking()
	{
		bMissionPickupCreditAwarded = false;
		bMissionPickupCounted = false;
		bMissionResolutionReported = false;
		bMissionPatientDead = false;
		bAmbulanceHandoffPending = false;
	}

	bool SetForcedPedestrianFigureClip(const FString& Mnemonic);
	void ClearForcedPedestrianFigureClip();
	void SetMissionInjuredPose();
	// A dead medevac patient remains the same physical person. When they die in the cabin this
	// pose stops their VM without relinquishing their seat; the state-5 medic may still remove
	// that same body through BHAV 263's ordinary carrier interactions.
	void SetMissionDeadPose();

	// Mission-required roof staff are outside the disposable ambient-population budget. A medic
	// may be far across the city while the player collects a patient and still has to be present
	// when the helicopter reaches the hospital.
	bool IsPersistentHospitalRoofCrew() const { return bPersistentHospitalRoofCrew; }
	void SetPersistentHospitalRoofCrew(bool bPersistent);

	// Pins that worker to the roof it was posted on: the square its building covers, and the roof
	// surface height it was placed at. The original's roof medic retires within seconds of being
	// spawned, so it never gets the chance to wander; this one lives for as long as the medevac
	// does, and a persistent walker will eventually reach an edge. Several movers (crowd
	// separation, traffic impulses) also displace agents without consulting the walked surface at
	// all, so the containment is applied to the transform rather than to any one of them.
	void SetHospitalRoofPost(const FVector& RoofCenterWorldLocation, float HalfExtentCm);
	// Rooftop-rescue survivors need the same physical containment, but they are mission people,
	// not permanent hospital staff. Boarding clears the post and their ordinary rescue lifecycle
	// remains in charge of despawn/save ownership.
	void SetMissionRoofPost(const FVector& RoofCenterWorldLocation, float HalfExtentCm);

	enum class ERoofPostContainment : uint8
	{
		AtPost,    // already over its own roof; nothing to do
		Contained, // was over the edge or below the roof, and has been put back
		Abandoned  // too far away to be "off the edge"; the post no longer applies
	};

	// Pure form of that containment, so the geometry is testable without a world. The inset keeps
	// the body over the roof rather than half off it, and a position more than FallToleranceCm
	// below the posted surface is a fall to be undone rather than variation in the surface.
	static ERoofPostContainment ClampToHospitalRoofPost(
		const FVector& WorldLocation,
		const FVector& PostCenterWorldLocation,
		float PostHalfExtentCm,
		float BodyRadiusCm,
		float CapsuleHalfHeightCm,
		float FallToleranceCm,
		FVector& OutContainedLocation);

	/**
	 * Pure form of the step-target half of that containment: may a posted worker standing at
	 * `CurrentLocation` step to `TargetLocation`?
	 *
	 * `ExtentFraction` picks how much of the post the walk is entitled to (see
	 * HospitalRoofPostIdleWanderFraction), and a step that shortens the distance to the post centre
	 * is always allowed so a worker already outside the limit can walk back in rather than refusing
	 * every direction. A zero half extent means "unposted", which constrains nobody.
	 */
	static bool IsWithinRoofPostSquare(
		const FVector& TargetLocation,
		const FVector& CurrentLocation,
		const FVector& PostCenterWorldLocation,
		float PostHalfExtentCm,
		float BodyRadiusCm,
		float ExtentFraction);

	// Marks an uninjured victim who still needs picking up. They keep whatever program or carrier
	// they are on, but any moment it leaves them standing still they wave for the helicopter
	// instead of idling.
	//
	// The clip is "WvNo". The old comment here claimed op 22 binds "Wave" when a person notices
	// the player; op 22 binds nothing (it reads the player's speed and facing into two locals),
	// and the program that really does wave at the player - BHAV 291 rec[4] - binds "WvNo".
	// "Wave" is the panic gesture the rioter and Rxn: Ouch programs use.
	void SetMissionAwaitingRescue(bool bAwaiting) { bMissionWavesWhenIdle = bAwaiting; }
	void ClearMissionPose();
	void ResumeNormalPedestrianBehavior();
	// Continue the exact VM stack that was paused by SetMissionScriptedMover.
	void ResumeSuspendedPedestrianBehavior();
	void SetCarriedBy(USceneComponent* CarryParentComponent, const FVector& RelativeLocation, const FRotator& RelativeRotation);
	bool IsMissionCarried() const { return bMissionCarried; }

	// person+0x170, written by FUN_004c4e10 when an emergency vehicle deploys this person.
	// Opcode 62 selects this exact starting object before it considers the player helicopter.
	void SetBehaviorStartingVehicle(AActor* Vehicle)
	{
		BehaviorStartingVehicle = Vehicle;
		bBehaviorStartingVehicleMessaged = false;
	}
	AActor* GetBehaviorStartingVehicle() const { return BehaviorStartingVehicle.Get(); }

	// BHAV 275 has just used opcode 51 to set this patient down at the ambulance selected by
	// BHAV 272. BHAV 285's following outcome 0/1 pair may therefore use the mission service even
	// though no helicopter seat is involved.
	bool IsAmbulanceHandoffPending() const { return bAmbulanceHandoffPending; }
	void SetAmbulanceHandoffPending(bool bPending) { bAmbulanceHandoffPending = bPending; }

	// Detach from a carrier and set the agent back down as an injured pickup at the given world
	// location (used when the player presses drop, or a carrier releases them on the ground).
	void SetDroppedInjuredOnGround(const FVector& WorldLocation);

	// Starts a visible passenger fall from the current airborne position. When the landing impact
	// is too large, the agent becomes an injured medevac victim owned by a new no-reward mission.
	void BeginPassengerFall(int32 SourceEventId, float InjuryDistanceCm);

	// Turns the agent into a script-driven mover: no behavior VM, no ground snapping (its owner
	// keeps it on a chosen plane), driven purely by SetMoveTarget.
	void SetMissionScriptedMover();

	// Whether the agent settles onto the terrain each tick. Off for people whose owner places
	// them somewhere there is no ground - swimmers beside a capsized boat, riders on a train
	// roof - while they still run their behaviour program.
	void SetBehaviorGroundSnap(bool bEnabled) { bSnapToGround = bEnabled; }

	// Choose the privanim figure this agent renders (e.g. "Medik"). Only takes effect before the
	// figure is built (i.e. before ConfigureAgent).
	void SetPedestrianFigureName(const FString& NewFigureName) { PedestrianFigureName = NewFigureName; }

	void ConfigureMarchingBandUniform(int32 BandIndex);

	// DIVERGENCE, deliberate: exempt this walker from FUN_004c9470's tile-class rule. The airport
	// is tile class 1 (both stamped ids fall through GetTileClassForBuildingId to its catch-all)
	// and class 1 is in no behaviour row, so anybody standing on the apron refuses every direction
	// and spins. The level-complete band belongs there, so it is let through.
	//
	// This does NOT let them into buildings: the climb gate is what stops a walker at a wall - the
	// walked surface inside one is the roof, far above the 5-unit allowance - and it still applies,
	// as does the walk-surface probe. Only "may a person of this class stand on this kind of tile"
	// is waived.
	void SetIgnoresTileClassRules(bool bIgnore) { bIgnoresTileClassRules = bIgnore; }
	bool IgnoresTileClassRules() const { return bIgnoresTileClassRules; }

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

	// --- Burglar / criminal car (FUN_004b8470's class, message id 0x11e) ----------------------
	// Turns this vehicle into the mission's CARROBBR getaway car. The traffic system drives the rest; see
	// SimCopterCriminalCar.h for the decode.
	void MakeCriminalCar(int32 InEventId, int32 CruiseDelay1616);
	bool IsCriminalCar() const { return bCriminalCar; }
	int32 GetCriminalEventId() const { return CriminalEventId; }
	// FUN_0049af00/FUN_0049af70 set veh[4] & 0x800 on an ordinary traffic car. This is the
	// ambient Speeder encounter, not the Burglar mission's CARROBBR object.
	void MakeSpeeder();
	void ClearSpeeder();
	bool IsSpeeder() const { return bSpeeder; }

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

	// FUN_004b8b60's hold while the burglar is outside the car.
	float GetBurglarOutsideSeconds() const { return BurglarOutsideSeconds; }
	void SetBurglarOutsideSeconds(float NewSeconds) { BurglarOutsideSeconds = NewSeconds; }

	// How much of its road speed a pulling-over car still has. The traffic pass resets every
	// vehicle's speed scale to 1 each frame, so a stop has to be re-asserted from this rather
	// than written once - which is what let an "arrested" car drive away.
	float GetCriminalStopScale() const { return CriminalStopScale; }
	void SetCriminalStopScale(float NewScale) { CriminalStopScale = NewScale; }
	float GetSpeederRewardCooldownSeconds() const { return SpeederRewardCooldownSeconds; }
	void SetSpeederRewardCooldownSeconds(float NewSeconds) { SpeederRewardCooldownSeconds = NewSeconds; }
	void MarkStopped() { bStopped = true; }
	void ResumeCriminalCarDriving()
	{
		bStopOrdered = false;
		bStopped = false;
		CriminalStopScale = 1.0f;
	}
	float GetCriminalCruiseSeconds() const { return CriminalCruiseSeconds; }
	void SetCriminalCruiseSeconds(float NewSeconds) { CriminalCruiseSeconds = NewSeconds; }
	float GetCriminalSpotlightLostSeconds() const { return CriminalSpotlightLostSeconds; }
	void SetCriminalSpotlightLostSeconds(float NewSeconds) { CriminalSpotlightLostSeconds = NewSeconds; }
	uint8 GetBurglarDoorPhase() const { return BurglarDoorPhase; }
	void SetBurglarDoorPhase(uint8 NewPhase) { BurglarDoorPhase = NewPhase; }
	bool DidCriminalDriverReturn() const { return bCriminalDriverReturned; }
	int32 GetCriminalDriverMessage() const { return CriminalDriverMessage; }
	void SignalCriminalDriverReturned(int32 MessageId)
	{
		bCriminalDriverReturned = true;
		CriminalDriverMessage = MessageId;
	}
	void ClearCriminalDriverSignal()
	{
		bCriminalDriverReturned = false;
		CriminalDriverMessage = 0;
	}

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

	// The airframe ran this person over: interaction mode 12 (BHAV 912 -> 903 "Rxn: Die") plus, for
	// the criminal this is restricted to, outcome 9 so the mission closes. Latched, so one aircraft
	// kills them once. Called by ASimCopterTrafficSystemActor::RunOverCriminalsUnderHelicopter.
	bool ApplyHelicopterRunOver(class ASimCopterHelicopterPawn& Helicopter);

	// The reaction currently running on this agent (INDEX_NONE when none).
	int32 GetActiveReactionProgramId() const { return BehaviorContext.ActiveReactionProgramId; }

	// Megaphone message the agent last received (person+0x15a).
	int32 GetLastMegaphoneMessage() const { return BehaviorContext.MegaphoneMessageIndex; }

	// person+0x1a4: the object that last interacted with this person. Behaviour opcodes 32/33 turn
	// away from or toward it, opcode 80 gabs at it, and opcode 15 class 4 selects it.
	AActor* GetBehaviorInteractionSource() const { return BehaviorInteractionSource.Get(); }

	// person+0x1a8 plus the state change in FUN_004c0f40: start the abduction. The person switches to
	// state 16 (BHAV 666 "Porkchop"), waves, and then flies up to Target at movespeed with no regard
	// for terrain (opcode 78). Returns false when FUN_004c0f80's eligibility test rejects them - not
	// on screen, too far from the camera, riding the player, or a person the mission layer owns.
	//
	// The target is a component rather than an actor because the original's person+0x1a8 is a scene
	// object, and the only thing that ever fills it - the UFO - is one mesh among the plane pool on
	// the ambient-vehicle actor rather than an actor of its own.
	bool BeginBeamAbduction(USceneComponent* Target);
	bool IsBeingBeamedUp() const { return bBeamAbductionActive; }

	// Behaviour-VM state other agents' opcodes have to read. FUN_004ca350 filters candidates on
	// the loop flag (+0x14a), the state (+0x148), visibility (+0x152) and, for criminals, the
	// "already caught" attribute (+0x16e).
	bool IsBehaviorActive() const { return bBehaviorActive; }
	uint16 GetBehaviorAttribute(int32 Index) const
	{
		return Index >= 0 && Index < EBhavAttr::Count ? BehaviorContext.Attributes[Index] : 0;
	}

	// FUN_004cc560: another person's op 39 pushing a reaction BHAV onto this one. This is the
	// arrest: a cop pushes BHAV 1060 "Rx: criminal-caught".
	bool PushBehaviorReaction(int32 ProgramId);

	// --- person+0x1a0, the carrier ------------------------------------------------------------
	// What this person is riding. The shipped programs drive every pickup and drop-off through
	// this: a rescue victim boards the harness (BHAV 305), a transport passenger boards the
	// helicopter (BHAV 291), a paramedic totes a victim (BHAV 262). Boarding a helicopter cabin
	// also claims one of its passenger seats, so the seat window and the mission counters agree
	// with what the VM did.
	AActor* GetBehaviorCarrier() const { return BehaviorCarrier.Get(); }
	bool IsRidingHarness() const { return BehaviorCarrier.IsValid() && bRidingHarness; }
	bool HasClaimedPassengerSeat() const { return bClaimedPassengerSeat; }
	bool IsAtBehaviorHomeTile() const { return IsOnHomeTile(); }
	// bAsCarriedBody separates the two things riding something means. Opcode 44 totes a body: it is
	// slung across the carrier and visibly carried. Opcodes 12/48 are "get in", which is what a
	// crew member does to its own vehicle - the original's op 40 then simply stops that person
	// existing, so it never had to draw them. Passing false is what stops a paramedic being laid
	// across the bonnet of its ambulance in the corpse pose.
	bool BoardCarrier(
		AActor* NewCarrier,
		bool bAsHarnessRider,
		bool bAllowAirborneCabinTransfer = false,
		bool bAsCarriedBody = false);
	bool AlightFromCarrier(bool bPlayDoorSound = true);
	// SCHOOK: HelicopterWriteOffPassengers 0x004c0ba0. A helicopter entering its destroyed state
	// writes off every occupied seat, including medevac patients that ordinary health death keeps
	// aboard for hospital delivery.
	void WriteOffInDestroyedHelicopter();
	// Runtime-observed cabin response to a damaging helicopter impact. Healthy passengers flash
	// the frightened portrait; a medevac patient loses one normal deterioration quantum and its
	// already-playing EKG is re-tuned immediately.
	void ReactToCabinImpact();
	static int32 ComputeMedevacHealthAfterCabinImpact(int32 Health, int32 DifficultyTier);
	// BHAV 264's non-casualty branch. Exposed for the portrait regression test and used when the
	// impact flinch expires, so the VM and the remake-only immediate reaction converge on one face.
	static int32 ComputePassengerPortraitStateFromDamageScaledSpeed(int32 DamageScaledSpeed);

	// Person states 5/7/8/0xe: an emergency worker the dispatcher put on the ground, not somebody
	// the player is being scored on. They carry the record they were sent to only so their own
	// program can post against it.
	bool IsEmergencyCrewMember() const;
	static bool IsEmergencyCrewPersonState(int32 PersonState);
	// Op 58's transfer: a victim on the raised harness climbs into the cabin.
	bool TransferFromHarnessToCabin();
	// The mission passenger kind this person counts as, from their spawn state (person+0x148).
	ESimCopterMissionPassengerKind GetMissionPassengerKind() const;
	bool IsMedevacVictim() const;
	// A direct weapon conversion bypasses BHAV 915 -> 906 -> opcode 35. Preserve opcode 35's
	// old-record casualty notification before the mission actor assigns the new medevac record.
	bool PrepareForPlayerCausedMedevac();

	// --- person+0x18e, the head ----------------------------------------------------------------
	// FUN_004c71c0 gives every behavior class a fixed head, and FUN_004c7090 overwrites it with 10
	// - the bandaged one - for state 6, the medevac victim. It indexes DAT_0058f0e0 for the SIM3D
	// panorama on the 3D figure and people1.bmp's columns for the seat-window portrait, so both
	// have to come from here or a passenger's face stops matching their body.
	int32 GetHeadImageIndex() const { return FigureHeadIndex; }
	// Re-read attribute 39 and swap the figure's head texture when it has moved. Becoming a
	// medevac victim - a collapse (opcode 35), or SetMissionInjuredPose - is what moves it.
	void RefreshHeadImageIndex();

	// Opcode 54's stored face: which people1.bmp row this person's seat portrait uses.
	int32 GetSeatPortraitMood() const { return SeatPortraitMood; }

	// Pointer-free mission-person/vehicle snapshot used by the save subsystem's BOMB payload.
	// ConfigureAgent is replayed on load, then the exact movement and BHAV stack are restored.
	bool CaptureRuntimeSaveState(TArray<uint8>& OutData);
	bool RestoreRuntimeSaveState(const TArray<uint8>& Data);
	void SetRuntimeSaveIdentityName(FName Name) { RuntimeSaveIdentityName = Name; }
	FName GetRuntimeSaveIdentityName() const
	{
		return RuntimeSaveIdentityName.IsNone() ? GetFName() : RuntimeSaveIdentityName;
	}
	void ResolveRuntimeSaveReferences(const TMap<FName, AActor*>& SavedActors, ASimCopterHelicopterPawn* Helicopter);

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

	// Behavior VM ticks per second.
	//
	// MEASURED 2026-08-11, and it is not "once per game frame" as this comment used to say:
	// FUN_004c5fb0 accumulates the frame delta and runs the per-person pass only when it passes
	// DAT_00506450, reloaded to 0x147a = 0.08 s in 16.16. The accumulator is reset to zero rather
	// than decremented, so retail's real cadence is frame-rate quantised - 12 Hz at 60 fps, 10 Hz
	// at 20 - and 15 here is already faster than the original, not slower. Movement distance,
	// walk-clip frame rate and idle durations all scale with this, so converting any shipped
	// program's tick counts to seconds must use ~12 Hz, not this number and not 20.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Behavior", meta = (ClampMin = "1.0"))
	float BehaviorTickRate = 15.0f;

	// Per-step vertical gate adapted from FUN_004c9470: one ordinary behavior step may rise or
	// descend at most MaxStepClimb units (person+0x144 = 5). BHAV 308's repeated-failure escape
	// bypasses the same gate in both directions. 1 unit = tile/64 (~6.25cm at a 400cm tile).
	// This is what normally stops people at building walls - tile classes alone allow them.
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

	// Agitation (person + 0x150) to start with. Spawn mode 3 sets the original's literal 7; see
	// SimCopterMissions::RioterSpawnAgitation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Behavior")
	int32 InitialBehaviorAgitation = 0;

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

	// How much of each beam Lumen bounces. LumenSceneDirectLighting.cpp gathers a light into the
	// Lumen scene only when `Proxy->GetIndirectLightingScale() > 0` and then multiplies its colour
	// by that value, so this is both the gate and the scale - and it is free, being applied to
	// lighting Lumen already computes for the light.
	//
	// Above 1 for the same reason the searchlight's is: SetInverseExposureBlend(1) is a DIRECT
	// lighting trick that Lumen never sees, so the 9,000 unitless (~14 candela) beams were handing
	// Lumen almost no energy to bounce. Lower than the searchlight's, because there are dozens of
	// cars and their bounce should read as a glow on the road rather than as headlights of their own.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Headlights", meta = (ClampMin = "0.0"))
	float HeadlightIndirectLightingIntensity = 8.0f;

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

	// --- Arsonist fire cadence (DELIBERATE DIVERGENCE) ---
	//
	// Retail leaves it to BHAV 1078's `rand(1000) < 6` once per walk cycle, then a 60 s firebomb
	// burn, then `1 in (8 - tier)` on the burnout. Measured against the shipped behaviour tick that
	// is a fire every half hour or so, which is not what an Arsonist mission is for: the player is
	// meant to be racing something.
	//
	// So a state-11 arsonist works to a clock. Every time this window elapses he plays the throw
	// clip and a nearby building catches - no projectile, no burn, no roll. The window is aimed at
	// the Robber's recurring "Burglary Committed!" (the 75 s nag interval) so the two crimes apply
	// comparable pressure, and it is redrawn each time so no two runs line up.
	//
	// This is the cadence of the FIRE itself, so these two numbers are the only tuning knob. The
	// firebomb chain still exists and is still faithful - see
	// ASimCopterMissionSystemActor::ThrowArsonistFirebomb and SpawnCrashBurningDebris - it is just
	// no longer what an arsonist mission depends on. BHAV 1078's own roll can still throw one.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Behavior", meta = (ClampMin = "1.0"))
	float ArsonThrowIntervalMinSeconds = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Behavior", meta = (ClampMin = "1.0"))
	float ArsonThrowIntervalMaxSeconds = 100.0f;

	// Slack above the step-climb allowance where the walk-surface probe starts, so a surface
	// exactly at the limit is still found. See GetPedestrianWalkProbeCeilingZ.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement", meta = (ClampMin = "0.0"))
	float PedestrianWalkProbeCeilingMarginCm = 8.0f;

	// Sweep the real mesh between a walker and their step target instead of inferring obstruction
	// from the height of whatever happens to be overhead. Exposed only so the old column-only
	// behaviour can be reproduced when diagnosing a movement regression.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement")
	bool bUseGeometryStepSweep = true;

	// How much of the collision capsule the wall sweep uses. It is inset only enough to keep a
	// sweep that ends exactly at contact from reading as start-penetrating on the next frame -
	// which would switch containment off for that frame, since penetrating is its escape hatch.
	//
	// It started at 0.8, chosen so a full-width probe would not refuse gaps a person visibly fits
	// through, and that was wrong at this scale. The population is authored at PopulationWorldScale
	// against a shrunk world: the capsule is 8 cm in radius, so a 0.8 inset sweeps 6.4 cm - while
	// the privanim figure is calibrated to a 44 cm body and with its limbs mid-stride is wider than
	// that. The visible person could therefore overlap a wall while the sweep called it clear.
	// Anything below about 0.9 is buying passability with body clipping.
	//
	// Containment uses the same figure, so the two cannot disagree about where a wall is and shuffle
	// a walker back and forth against it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float PedestrianStepSweepRadiusScale = 0.95f;

	// Horizontal displacement in one frame beyond which ContainOutsideBuildingGeometry treats the
	// move as a teleport and re-anchors instead of sweeping. A pedestrian walks a few centimetres a
	// frame; anything approaching a tile is a placement, not a step.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float WallContainmentMaxStepCm = 200.0f;

	// A real ramp/elevated-road surface may differ from the route graph's linear sample. Accept
	// its mesh surface only inside this band; bridges and ordinary road tiles use graph height
	// and phase through arbitrary meshes instead of warping onto their tops.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float VehicleElevatedRoadMeshMaxOffsetCm = 600.0f;

	// Where a person another person is carrying rides, relative to the carrier. X is out in front
	// of the chest; the figure is laid across it by the rotation applied alongside this.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement")
	FVector CarriedPersonRelativeOffsetCm = FVector(14.0f, 0.0f, -6.0f);

	// How far below its posted roof a hospital worker has to be before it counts as having come
	// off the building rather than as ordinary variation in the surface. Roofs are flat by
	// construction and the shortest storey is around 150cm, so anything past this is a fall.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float HospitalRoofPostFallToleranceCm = 100.0f;

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

	/**
	 * `M_SimCopterLitSpriteTexture` - the privanim head is an ordinary LIT surface.
	 *
	 * It used to share the unlit `M_SimCopterSpriteTexture` with the effect cards, which meant its
	 * brightness had to be computed from the key light every frame and written as EmissiveNits. A
	 * head is not a light source and never was: the original had no lighting model at all, so a head
	 * is simply painted geometry, and the only reason it needed a number here was that the material
	 * under it could not be lit. Sharing the city's lit card material instead makes it shade off the
	 * sun exactly like the figure's vertex-coloured body (same SelfIllum/Roughness/Specular), and the
	 * whole "what brightness should a head be after dark" question stops existing.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> FigureHeadMaterial;

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
	// 0 = use MovementSpeedCmPerSec. Not serialised: guidance is re-issued every mission tick, so
	// a restored agent picks its speed back up on the next one.
	float GuidanceMoveSpeedCmPerSec = 0.0f;
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
	bool bSpeeder = false;        // ordinary vehicle flag veh[4] & 0x800
	bool bFleeing = false;        // obj[5] & 8
	bool bStopOrdered = false;    // veh[4] & 0x10
	bool bStopped = false;        // veh[4] & 0x20
	int32 CriminalEventId = INDEX_NONE; // +0x113
	int32 SpotlightMark = 0;      // +0x11b
	uint8 CriminalState = 0;      // +0x12b
	float BurglarOutsideSeconds = 0.0f; // +0x10, armed to 0x780000 by FUN_004b8b60
	float CriminalStopScale = 1.0f; // stands in for the stop distance at +0xd3
	float SpeederRewardCooldownSeconds = 0.0f; // veh +0xd7, event 0x21 repeat timer
	float CriminalCruiseSeconds = 0.0f; // +0x137, DAT_00506360 + rand()%DAT_00506364
	float CriminalSpotlightLostSeconds = 0.0f; // +0x13b, DAT_00506368 = 20.0s
	uint8 BurglarDoorPhase = 0; // +0x133..+0x136 flags in FUN_004b8b60/FUN_004b8c90
	bool bCriminalDriverReturned = false; // veh[8], set by people opcode 61
	int32 CriminalDriverMessage = 0; // veh[0xc], BHAV 1303 returns 1
	bool bUsingPedestrianSprite = false;
	bool bUsingPedestrianBody = false;

	// --- standing in water ---
	// Someone on a water tile is IN the sea, not on it: the boat-rescue survivors treading water and
	// anyone who walks off a quay. The figure drops until the surface cuts it at the waist and then
	// rides the swell M_SimCopterWater draws, because the capsule sits on the water's rest plane and
	// the waves live entirely in the vertex shader.
	//
	// Deliberately VISUAL only - it moves VisualRoot, never the capsule. Every height gate the sim
	// measures against a person is a small number with a decoded origin (the 37.5 cm alight
	// clearance, the 50 cm boarding band, the medic's contact box), and sinking the collision body
	// half a body-length would quietly break all of them.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Population", meta = (ClampMin = "0.0"))
	float WaterSubmergeLerpSeconds = 0.25f;

	float WaterSubmergeAlpha = 0.0f;
	float WaterVisualOffsetCm = 0.0f;
	// The offset the animation/pose code last asked for. The water offset is added on top of it, so
	// both writers can act without either having to know about the other.
	FVector VisualRootBaseRelativeLocation = FVector::ZeroVector;

	void UpdateWaterSubmersion(float DeltaSeconds);
	void SetVisualRootRelativeLocation(const FVector& Local);
	bool IsStandingInWater() const;

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
	// person+0x1a0 and whether it is the rope end rather than the cabin (op 86 distinguishes them).
	TWeakObjectPtr<AActor> BehaviorCarrier;
	// person+0x170: the emergency vehicle that deployed this crew member.
	TWeakObjectPtr<AActor> BehaviorStartingVehicle;
	bool bBehaviorStartingVehicleMessaged = false;
	// person+0x1a4, written by FUN_004c1050 when an interaction is delivered, and by the move core
	// when this person walks into somebody.
	TWeakObjectPtr<AActor> BehaviorInteractionSource;
	// person+0x1a8, the thing opcode 78 flies to - only ever the UFO in the shipped data. The flag is
	// separate so a saucer that despawns mid-flight still finishes the abduction.
	TWeakObjectPtr<USceneComponent> BehaviorBeamTarget;
	FName PendingSavedCarrierName;
	FName PendingSavedStartingVehicleName;
	FName PendingSavedInteractionSourceName;
	FName PendingSavedSelectionName;
	FName RuntimeSaveIdentityName;
	bool bBeamAbductionActive = false;
	// person+0x1c4, this person's own radius in original units (opcode 27 halves it for a rioter).
	float BehaviorBodyRadiusUnits = 3.0f;
	// DAT_00506448. The original's is global, but every shipped use reads it twice inside one
	// person's program and subtracts, so a per-person tick count is indistinguishable.
	int32 BehaviorTickCounter = 0;
	bool bRidingHarness = false;
	bool bClaimedPassengerSeat = false;
	// True while a carrier owns this person's transform, so UpdateMovement leaves them alone.
	bool bBehaviorMoveSuspended = false;
	void AlightAttachmentOnly();
	// DAT_005040d0+0xa4: the player's helicopter, in it or not.
	ASimCopterHelicopterPawn* ResolvePlayerHelicopter() const;
	// Keeps a harness rider on the rope end, which is a point on the helicopter rather than a
	// component this actor can be parented to.
	void UpdateCarriedTransform();
	// person+0x188/+0x18a: the tile the person was placed on, which op 87 compares against.
	FIntPoint BehaviorHomeTile = FIntPoint(INDEX_NONE, INDEX_NONE);
	// Opcode 54's face index: which people1.bmp row this person's seat portrait is drawn from.
	// FUN_004c6250 seats every passenger at 1 and BHAV 264 moves it between 0, 1 and 2 from there.
	int32 SeatPortraitMood = 1;
	// The immediate collision face is not an original persistent attribute. BHAV 292 revisits
	// BHAV 264 about every 13 VM ticks, so this deadline guarantees the direct flinch yields back
	// to the same speed-derived face even if a passenger's behavior stack is temporarily stalled.
	float CabinImpactPortraitSecondsRemaining = 0.0f;

	// person+0x172 / person+0x174: the voice-bank slot this person has borrowed and the event
	// currently loaded into it. FUN_004c5210 hands out one of the fourteen and releases it again.
	int32 VoiceSlotId = INDEX_NONE;
	int32 VoiceCurrentEvent = INDEX_NONE;
	// person+0x198: 1 when the current voice was started 2D, which is how a looping EKG stays
	// audible from the cockpit while a 3D moan follows the person around.
	bool bVoiceIsNonPositional = false;
	// Polyphonic walking loop, deliberately separate from the dialogue/reaction voice-bank slot.
	TWeakObjectPtr<UAudioComponent> WalkingSoundComponent;
	bool bMissionWavesWhenIdle = false;
	bool bMissionStationary = false;
	bool bMissionCarried = false;
	bool bMissionPickupCreditAwarded = false;
	bool bMissionPickupCounted = false;
	bool bMissionResolutionReported = false;
	bool bMissionPatientDead = false;
	bool bAmbulanceHandoffPending = false;
	bool bPersistentHospitalRoofCrew = false;
	// The roof post from SetHospitalRoofPost: centre and surface Z of the building this worker
	// belongs to, and half the width of its footprint.
	bool bHasHospitalRoofPost = false;
	FVector HospitalRoofPostWorldLocation = FVector::ZeroVector;
	float HospitalRoofPostHalfExtentCm = 0.0f;
	// Keeps the posted worker over its own roof. Returns true when it had to intervene.
	bool ContainToHospitalRoofPost();
	/**
	 * Whether a step target is still inside the posted roof; unposted people are unconstrained.
	 *
	 * `ExtentFraction` scales the square: 1.0 is the whole building, which is what a walk that is
	 * seeking something (the aircraft, a casualty) gets, and HospitalRoofPostIdleWanderFraction is
	 * what an aimless one gets. A target that is outside the limit but closer to the post centre
	 * than the walker currently is passes anyway, so a crew member who ends up out at the parapet -
	 * after a handoff, a traffic shove, or a reload - can always walk back in.
	 */
	bool IsWithinHospitalRoofPost(const FVector& WorldLocation, float ExtentFraction = 1.0f) const;
	bool bPassengerFallActive = false;
	bool bPassengerFallStarted = false;
	float PassengerFallStartZ = 0.0f;
	float PassengerFallInjuryDistanceCm = 900.0f;
	int32 PassengerFallSourceEventId = INDEX_NONE;

	bool RebuildFigureClip(const FString& Mnemonic);
	// FUN_004c71c0's `local_4` plus FUN_004c7090's state-6 override, clamped to the head table.
	int32 ResolveHeadImageIndex() const;
	// Keeps a playing 3D voice on this person and hands a finished slot back to the bank.
	void UpdatePersonVoice();
	// FUN_004c6970's normal-move audio half: own voice on while moving, off at speed zero.
	void UpdateWalkingVoice(int32 MoveSpeed);
	void StopWalkingVoice();
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
	// FUN_004c9470's move result 10: the step just taken would have put this body inside the
	// object it was walking toward, so it stopped against it instead. MoveStep raises it,
	// StepTowardSelectedObject reads it as "arrived".
	bool bBehaviorStepTouchedSelection = false;
	// Set only around StepTowardSelectedObject's own move, so a plain op-4 walk is never stopped
	// by a selection that happens to still be sitting in this person's one selection slot.
	bool bBehaviorStepSeekingSelection = false;

	// The gap between this walker's body and its current selection, across the deck; <= 0 is
	// contact (FUN_004c8f70's box overlap, with the selection's own extent). The player's
	// helicopter is measured against its airframe mesh, not its flight-sweep capsule.
	float GetSelectionContactGapCm(const FSimCopterPersonContext& Context, const FVector& FromWorldLocation) const;
	bool IsTouchingSelection(const FSimCopterPersonContext& Context, const FVector& FromWorldLocation) const;

	// Is this worker close enough to hand a casualty through the aircraft's door? Skin contact plus
	// a reach horizontally, a window vertically so a low hover still works. Riding it counts.
	bool IsAtHelicopterForHandoff(const class ASimCopterHelicopterPawn& Helicopter) const;

	// Whether a posted hospital worker should notice the aircraft at all yet: it has to be over
	// their own building, not merely somewhere in the neighbourhood.
	bool IsHelicopterWithinRoofPostAggro(const class ASimCopterHelicopterPawn& Helicopter) const;

	// How far past the fuselage skin a worker may stand and still reach into the cabin.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Behavior", meta = (ClampMin = "0.0"))
	float HelicopterHandoffReachCm = 25.0f;

	// The vertical window for that reach, measured feet-to-doorsill. 150 cm is 24 original units:
	// several times the 5-unit arrival gate, so a helicopter hovering just off the pad can still be
	// unloaded, while one at any real altitude cannot.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Behavior", meta = (ClampMin = "0.0"))
	float HelicopterHandoffMaxVerticalCm = 150.0f;

	// How far outside its own footprint a posted roof crew will accept the aircraft.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Behavior", meta = (ClampMin = "0.0"))
	float HospitalRoofPostAggroMarginCm = 120.0f;

	/**
	 * How much of its roof a posted worker will wander over when it has nowhere to be.
	 *
	 * BHAV 801 walks exactly once, on the way in: rec[4] clears autoturn and rec[6] calls 'Walk-10',
	 * ten ticks at movespeed 10 - about half a metre - and the steady-state loop after it is
	 * Idle-10 / 'idle a bit' / probe, with no walk in it at all. In the original that is the whole
	 * story, because the probe reaches 272 -> 265 'Medevac disappear' within seconds and the worker
	 * simply stops existing. The remake has to keep the post staffed, so it refuses that despawn and
	 * restarts the state program instead - which re-runs the entry, so the once-only half metre
	 * became half a metre every few seconds, always along the same facing, and the medic marched in
	 * a straight line to the parapet and stood there. That is the reported "they go to the edge
	 * every single time".
	 *
	 * Two things hold it in: the restart re-rolls the facing (see UpdateOriginalBehavior), so the
	 * repeats no longer compound into a march, and an aimless walk is held to this fraction of the
	 * post so the drift stays around the middle. A walk that is actually going somewhere - BHAV 263
	 * heading for the aircraft - gets the whole roof.
	 */
	UPROPERTY(EditAnywhere, Category = "SimCopter|Behavior", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float HospitalRoofPostIdleWanderFraction = 0.45f;

	// Latch for ApplyHelicopterRunOver: BHAV 903 takes several ticks to finish dying and the
	// overlap stays true throughout.
	bool bRunOverByHelicopter = false;

	// See SetIgnoresTileClassRules. Not serialised: it is set at spawn by whoever placed this
	// agent, and the only user is the level-complete band, which is re-spawned rather than saved.
	bool bIgnoresTileClassRules = false;

public:
	// One semi-implicit gravity step used by airborne pedestrians. Kept pure so a passenger drop
	// cannot regress back to depending on a successful ground trace before it starts falling.
	static float IntegratePedestrianGravityStep(
		float CurrentZ,
		float DeltaSeconds,
		float GravityCmPerSec2,
		float& InOutVerticalVelocityCmPerSec);

	// FUN_004c8f70's box overlap reduced to the two extents the remake keeps for a pair of bodies.
	// <= 0 is contact. Pure, so the rule can be tested without a city.
	static float ComputeContactGapCm(
		const FVector& FromWorldLocation,
		const FVector& TargetWorldLocation,
		float MyRadiusCm,
		float TargetRadiusCm);

	// The same overlap against a body that is a BOX rather than a circle, which is what a car is.
	// Horizontal only; the caller owns the vertical gate. Pure, so it can be tested without a city.
	static float ComputeBodyGapCm(
		const FBox& LocalBoundsCm,
		const FTransform& BodyFrame,
		const FVector& WorldLocation);

	// This agent's rendered body box, as a gap from WorldLocation across the deck. Used for
	// vehicles, whose collision capsule is sized for traffic separation and is narrower than the
	// car it stands for.
	float GetDistanceToBodyCm(const FVector& WorldLocation) const;

	// The two remake-only gates on the medevac handoff, as pure geometry.
	//
	// Reach: skin contact plus ReachCm across the deck, and a vertical WINDOW feet-to-doorsill so a
	// low hover can still be unloaded while an aircraft at altitude cannot.
	static bool IsWithinHandoffReach(
		float AirframeGapCm,
		float MyRadiusCm,
		float ReachCm,
		float DoorsillWorldZ,
		float FeetWorldZ,
		float MaxVerticalCm);

	// Aggro: is the aircraft over the posted worker's own building (its square plus a margin)?
	static bool IsWithinRoofPostAggro(
		const FVector& HelicopterWorldLocation,
		const FVector& PostCenterWorldLocation,
		float PostHalfExtentCm,
		float MarginCm);

protected:

	void StartOriginalBehavior();
	void ResetBehaviorProgramOverride();
	void UpdateOriginalBehavior(float DeltaSeconds);
	// Runs the arsonist's fire clock. See ArsonThrowIntervalMinSeconds. Called from the TOP of
	// UpdateOriginalBehavior, above the behaviour-tick gate, because it schedules in wall-clock
	// seconds and the gate skips most frames.
	void UpdateArsonistThrowSchedule(float DeltaSeconds);
	// Seconds until this arsonist's next fire; negative until the first one is armed.
	float ArsonThrowCountdownSeconds = -1.0f;

	// The last place this walker's body was known to be clear of city geometry, and the point
	// ContainOutsideBuildingGeometry sweeps forward from.
	FVector WallContainmentAnchor = FVector::ZeroVector;
	bool bHasWallContainmentAnchor = false;
	void UpdateCabinImpactPortrait(float DeltaSeconds);
	void ApplyBehaviorFacingRotation();
	void AdvanceBehaviorFigureFrames(int32 TickCount);
	// The walked surface at a step target: the highest blocking geometry the walker could step
	// onto from where they stand, falling back to the tile's terrain altitude (port of
	// FUN_004c82c0 = max of object tops/terrain). See GetPedestrianWalkProbeCeilingZ for why the
	// probe must not start above the walker.
	bool TryGetWalkSurfaceZAt(const FVector& WorldLocation, float& OutSurfaceZ) const;

	// Is there real city geometry between here and the step target? A swept capsule against the
	// actual mesh, on the Camera channel that agent and player capsules ignore, so this answers
	// for walls, piers, poles and parapets and never for other people.
	bool IsPedestrianStepBlockedByGeometry(
		const FVector& FromWorldLocation,
		const FVector& ToWorldLocation,
		float TargetSurfaceZ) const;

	// Keep the body out of the walls, whatever moved it.
	//
	// MoveStep is only one of the things that displaces a person: crowd separation, traffic
	// impulses, avoidance offsets, mission guidance and the alighting placer all write the
	// transform directly, and none of them consult geometry. So a walker standing against a
	// building gets nudged and settles half inside it, and nothing ever pushes them back out -
	// which is exactly what "they phase partway into buildings" is.
	//
	// This sweeps the same body IsPedestrianStepBlockedByGeometry uses along the NET horizontal
	// displacement since the last frame it was clear, so it catches every mover at once instead of
	// each of them having to remember. Runs after all of them and before the ground snap, beside
	// ContainToHospitalRoofPost.
	void ContainOutsideBuildingGeometry();

	// ISimCopterBehaviorWorld
	virtual int32 GetCurrentTileClass() const override;
	virtual bool TryGetCurrentTileCoordinate(int32& OutFileX, int32& OutFileY) const override;
	virtual bool IsTileClassAllowedForState(int32 StateIndex, int32 TileClass) const override;
	virtual bool MoveStep(FSimCopterPersonContext& Context) override;
	virtual bool IsThreatNearby(const FSimCopterPersonContext& Context) const override;
	virtual bool TryGetPlayerTileProbe(
		const FSimCopterPersonContext& Context,
		FSimCopterBehaviorPlayerTileProbe& OutProbe) const override;
	virtual bool SelectObjectOfClass(FSimCopterPersonContext& Context, int32 ObjectClass, int32& OutTileDistance) override;
	virtual bool EvaluateProximityTest(const FSimCopterPersonContext& Context, int32 TestIndex) const override;
	virtual int32 GetCurrentTileBuildingId() const override;
	virtual bool IsCurrentTileServiceable() const override;
	virtual bool IsRidingCarrier(const FSimCopterPersonContext& Context) const override;
	virtual bool SelectOwningVehicle(FSimCopterPersonContext& Context) override;
	virtual bool IsSelectionPlayerHelicopter(const FSimCopterPersonContext& Context) const override;
	virtual bool IsSelectionWithinUnits(const FSimCopterPersonContext& Context, int32 Units) const override;
	virtual int32 GetDifficultyTier() const override;
	virtual bool CanAlightHere() const override;
	virtual bool TryAlightHere() override;
	virtual bool BoardSelection(FSimCopterPersonContext& Context) override;
	virtual bool PutSelectedPersonOnMe(FSimCopterPersonContext& Context) override;
	virtual bool DropSelectedPerson(FSimCopterPersonContext& Context) override;
	virtual bool SelectCarriedPerson(FSimCopterPersonContext& Context, bool bAlsoDropThem) override;
	virtual bool IsCarryingPerson() const override;
	virtual bool GetOnHelicopterIfHarnessRaised(FSimCopterPersonContext& Context) override;
	virtual bool IsCarrierPlayerHelicopter() const override;
	virtual bool IsCarrierHarness() const override;
	virtual bool IsOnHomeTile() const override;
	virtual bool SelectMedevacVictimAboardPlayer(FSimCopterPersonContext& Context) override;
	virtual void MessageOwningVehicle(int32 MessageId) override;
	virtual void SetSeatPortraitMood(int32 Mood) override;
	virtual void PlayPersonVoiceEvent(int32 VoiceEvent, bool bAllocateSlot, bool bNonPositional, bool bForce) override;
	virtual void StopPersonVoice() override;
	virtual int32 GetPlayerHelicopterSpeed() const override;
	virtual bool HasHiddenPersonInState(int32 State) const override;
	virtual void ThrowProjectileAtSelection(FSimCopterPersonContext& Context, bool bAtSelection, bool bIncendiary) override;
	virtual bool BeginFallAndDie(FSimCopterPersonContext& Context) override;
	virtual bool FaceSelectedObject(FSimCopterPersonContext& Context) override;
	virtual bool FaceAwayFromSelectedObject(FSimCopterPersonContext& Context) override;
	virtual bool FaceInteractionSource(FSimCopterPersonContext& Context, bool bFaceToward) override;
	virtual void ReactToInteractionSource(FSimCopterPersonContext& Context) override;
	virtual bool MeasureRiotCrowd(
		const FSimCopterPersonContext& Context,
		int32 RadiusTiles,
		int32& OutFacingOctant,
		int32& OutAverageAgitation,
		int32& OutCount) const override;
	virtual void SetBodyRadiusOriginalUnits(float RadiusUnits) override { BehaviorBodyRadiusUnits = RadiusUnits; }
	virtual bool JoinLiveRiot(FSimCopterPersonContext& Context) override;
	virtual bool FaceNearestFireWithin(FSimCopterPersonContext& Context, int32 RadiusTiles, int32& OutTileDistance) override;
	virtual int32 GetActiveMedevacMissionCount() const override;
	virtual bool CollapseIntoMedevacVictim(FSimCopterPersonContext& Context) override;
	virtual int32 GetSelectionRoomForBoarding(const FSimCopterPersonContext& Context) const override;
	virtual bool AdvanceBeamAbduction(FSimCopterPersonContext& Context) override;
	virtual int32 GetBehaviorTickCounter() const override { return BehaviorTickCounter; }
	virtual ESimCopterBehaviorStepResult StepTowardSelectedObject(FSimCopterPersonContext& Context) override;
	virtual bool PushReactionOnSelectedObject(FSimCopterPersonContext& Context, int32 ProgramId) override;
	virtual void PostMissionOutcome(FSimCopterPersonContext& Context, int32 OutcomeCode) override;
	virtual void OnUnknownOpcode(int32 Opcode) override;

	// FUN_004c8430: the stored facing octant from this person toward a world point, and whether
	// there is a bearing at all (the original answers -1 for a zero delta, which ops 31/32/33 take
	// their failure edge from).
	bool TryGetBehaviorFacingOctantToward(const FVector& TargetWorldLocation, int32& OutOctant) const;
	// FUN_004c6970's result-5 arm: face the person we just walked into, bind "2Gab" or "HipH" 50/50,
	// and let them know it happened (interaction mode 13, which pushes BHAV 914 on them).
	void RunBumpedPersonSelector(ASimCopterGroundAgent& Other);
	// The half of that which opcode 80 also runs, against an explicitly supplied object.
	void RunMeetSelector(FSimCopterPersonContext& Context, AActor* Source);
	// FUN_004c9000 over the step target: the nearest *other* visible pedestrian whose body radius
	// overlaps ours there. Move result 5 - it blocks the step and turns the walker one octant on.
	ASimCopterGroundAgent* FindBumpedPedestrian(const FVector& StepTargetWorldLocation) const;
	// The tail of opcode 78's flight: the person has reached the UFO and vanishes.
	void FinishBeamAbduction();

	void ApplyAgentShape();
	void UpdateMovement(float DeltaSeconds);
	void UpdateGroundSnap(float DeltaSeconds);
	void UpdateJankyAnimation(float DeltaSeconds);
	void ShowOriginalMesh(bool bUseOriginalMesh);
	void ConfigureVehicleHeadlights(const FBox& VehicleLocalBounds);
	void DisableVehicleHeadlights();

	/**
	 * Shows or hides this car's two headlights from `bEnableVehicleHeadlights` and the low power
	 * setting, which is the only thing that moves after the lights have been placed.
	 *
	 * Low Power drops MegaLights, and a busy street is dozens of cars carrying two spotlights each -
	 * exactly the case the standard deferred light loop is worst at. The beams are the one part of
	 * the mode's local-light cut that a player is likely to notice, and it is still the right trade:
	 * the cars keep their own emissive art either way.
	 */
	void RefreshHeadlightVisibility();

	/** True between ConfigureVehicleHeadlights and DisableVehicleHeadlights - i.e. "this is a car". */
	bool bVehicleHeadlightsConfigured = false;

	/** Only cars subscribe, and only once they have headlights, so the list stays short. */
	FDelegateHandle LowPowerChangedHandle;

	/**
	 * Re-derives EmissiveNits on the legacy PEOPLE1 pedestrian billboard from the world's key light.
	 *
	 * It rides the shared UNLIT `M_SimCopterSpriteTexture`, so what looks like ordinary shading has
	 * to be computed instead - and as a SURFACE, not a light source, so it has no minimum and goes
	 * dark with the sun. Without it the card sat on the material's baked daylight default and glowed
	 * all night. Cheap enough to run per tick: the subsystem caches its scan for the whole frame.
	 *
	 * The figure head is deliberately NOT here any more; see `FigureHeadMaterial`.
	 */
	void RefreshSpriteExposure();
	bool TraceGround(FVector& OutGroundLocation) const;
	void FinishPassengerFall(float FallDistanceCm);
	FString ResolveOriginalGameRoot() const;
};
