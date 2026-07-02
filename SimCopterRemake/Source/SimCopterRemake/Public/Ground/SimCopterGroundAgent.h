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

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	ESimCopterGroundAgentKind GetAgentKind() const { return AgentKind; }

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	bool IsUsingOriginalMesh() const { return bUsingOriginalMesh; }

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

	// Behavior VM ticks per second (the original ran behavior once per sim tick).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Behavior", meta = (ClampMin = "1.0"))
	float BehaviorTickRate = 10.0f;

	// Initial person state (0 = ambient pedestrian; see FPeopleBehaviorModel state table).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Behavior", meta = (ClampMin = "0", ClampMax = "20"))
	int32 InitialPersonState = 0;

	// Initial behavior class at original person+0x146. Ambient city spawning chooses this from
	// DAT_0058ec00/FUN_004c2450 while state remains 0; it also selects the figure (FUN_004c71c0).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Behavior", meta = (ClampMin = "0", ClampMax = "21"))
	int32 InitialBehaviorClass = 0;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement")
	bool bSnapToGround = true;

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

	bool RebuildFigureClip(const FString& Mnemonic);
	void UpdateFigureAnimation(float DeltaSeconds, float SpeedAlpha);

	// Original behavior-VM state (pedestrians only).
	TSharedPtr<FPeopleBehaviorModel> BehaviorModel;
	FSimCopterPersonContext BehaviorContext;
	float BehaviorTickAccumulator = 0.0f;
	bool bBehaviorActive = false;
	bool bBehaviorWantsMove = false;
	TSet<int32> ReportedUnknownOpcodes;

	void StartOriginalBehavior();
	void UpdateOriginalBehavior(float DeltaSeconds);

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
	void UpdateGroundSnap();
	void UpdateJankyAnimation(float DeltaSeconds);
	void ShowOriginalMesh(bool bUseOriginalMesh);
	void ConfigureVehicleHeadlights(const FBox& VehicleLocalBounds);
	void DisableVehicleHeadlights();
	bool TraceGround(FVector& OutGroundLocation) const;
	FString ResolveOriginalGameRoot() const;
};
