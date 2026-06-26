// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectKey.h"
#include "UObject/NoExportTypes.h"
#include "SimCopterTrafficSystemActor.generated.h"

class ASimCity2000CityActor;
class ASimCopterGroundAgent;

struct FSimCopterGroundRouteNode
{
	FVector LocalLocation = FVector::ZeroVector;
	FVector Location = FVector::ZeroVector;
	int32 FileX = 0;
	int32 FileY = 0;
	uint8 BuildingId = 0;
	TArray<int32> Neighbors;
};

UENUM(BlueprintType)
enum class ESimCopterTrafficFlowMode : uint8
{
	Normal,
	TrafficJam
};

struct FSimCopterVehicleTrafficState
{
	FVector LastLocation = FVector::ZeroVector;
	float BlockedSeconds = 0.0f;
	float RecentCollisionSeconds = 0.0f;
	float RecoveryCooldownSeconds = 0.0f;
	float TrafficLightLineGraceSeconds = 0.0f;
	float RecoveryBypassSeconds = 0.0f;
	float RecoveryRejoinSeconds = 0.0f;
	float IntersectionCommitSeconds = 0.0f;
	bool bInitialized = false;
	bool bInTrafficLightLine = false;
};

UCLASS()
class SIMCOPTERREMAKE_API ASimCopterTrafficSystemActor : public AActor
{
	GENERATED_BODY()

public:
	ASimCopterTrafficSystemActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "SimCopter|Traffic")
	bool RebuildSpawnData();

protected:
	UPROPERTY(EditInstanceOnly, Category = "SimCopter|City")
	TObjectPtr<ASimCity2000CityActor> SourceCityActor;

	UPROPERTY(EditAnywhere, Category = "SimCopter|City")
	bool bUseActiveCityActor = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|City", meta = (FilePathFilter = "sc2"))
	FFilePath CityFile;

	UPROPERTY(EditAnywhere, Category = "SimCopter|City")
	FDirectoryPath OriginalGameRoot;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population")
	TSubclassOf<ASimCopterGroundAgent> GroundAgentClass;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population")
	bool bSpawnOnBeginPlay = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population", meta = (ClampMin = "0"))
	int32 MaxVehicleAgents = 160;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population", meta = (ClampMin = "0"))
	int32 MaxPedestrianAgents = 280;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population", meta = (ClampMin = "1000.0"))
	float SpawnRadiusCm = 26000.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population", meta = (ClampMin = "1000.0"))
	float DespawnRadiusCm = 34000.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population", meta = (ClampMin = "0.0"))
	float MinSpawnDistanceCm = 1200.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population", meta = (ClampMin = "0.01"))
	float SpawnThinkIntervalSeconds = 0.1f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population", meta = (ClampMin = "1"))
	int32 MaxSpawnAttemptsPerThink = 8;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population")
	int32 RandomSeed = 1996;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population")
	TArray<FString> VehicleMeshNames;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population")
	TArray<FString> PedestrianMeshNames;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Population")
	bool bRequireOriginalPopulationMeshes = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float VehicleSpeedCmPerSec = 720.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float PedestrianSpeedCmPerSec = 230.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "0.0", ClampMax = "0.45"))
	float VehicleLaneOffsetTileFraction = 0.20f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "0.0", ClampMax = "0.35"))
	float VehicleCornerClipTileFraction = 0.12f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VehicleRightTurnEarlyClipTileFraction = 0.12f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VehicleRightTurnCornerClipTileFraction = 0.14f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic")
	ESimCopterTrafficFlowMode TrafficFlowMode = ESimCopterTrafficFlowMode::Normal;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float NormalVehicleFollowLookAheadCm = 560.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float NormalVehicleStopDistanceCm = 82.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float NormalVehicleMinimumFollowDistanceCm = 128.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float NormalVehicleSlowDistanceCm = 305.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float NormalTrafficBrakeRate = 5.5f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.5"))
	float TrafficLightPhaseSeconds = 10.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal")
	bool bStaggerTrafficLightPhases = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float TrafficLightStopDistanceCm = 205.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float TrafficLightSlowDistanceCm = 430.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float TrafficLightLineGraceDurationSeconds = 2.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float TrafficLightQueueStopDistanceCm = 230.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float TrafficLightQueueSlotSpacingCm = 255.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float TrafficLightQueueSlowDistanceCm = 780.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float TrafficLightQueueBrakeRate = 12.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float TrafficLightQueueLaneWidthCm = 520.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float TrafficLightIntersectionCommitDurationSeconds = 4.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float VehicleBlockedSpeedThresholdCmPerSec = 45.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float VehicleBlockedSecondsBeforeRecovery = 2.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float VehicleCollisionMemorySeconds = 5.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float VehicleRecoveryCooldownSeconds = 6.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float VehicleRecoveryReverseImpulseCmPerSec = 220.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float VehicleRecoveryBackUpDistanceCm = 120.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float VehicleRecoveryBypassOffsetCm = 185.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float VehicleRecoveryBypassDurationSeconds = 3.2f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.1"))
	float VehicleRecoveryBypassSpeedMultiplier = 1.1f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float VehicleRecoveryBlockerLookAheadCm = 560.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.0"))
	float VehicleRecoveryRejoinDurationSeconds = 2.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float VehicleRecoveryRejoinStopDistanceCm = 220.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float VehicleRecoveryRejoinSlowDistanceCm = 620.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float VehicleRecoveryRejoinLaneWidthCm = 520.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float VehicleRoadContainmentDistanceCm = 300.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "1.0"))
	float VehicleLaneGuidanceLookAheadCm = 260.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic|Normal", meta = (ClampMin = "0.05"))
	float VehicleLaneGuidanceDurationSeconds = 0.35f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "0.0"))
	float VehicleOverlapPaddingCm = 18.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "0.0"))
	float VehicleBumpImpulseCmPerSec = 180.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "1.0"))
	float VehicleFollowLookAheadCm = 520.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "1.0"))
	float VehicleStopDistanceCm = 145.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "1.0"))
	float VehicleSlowDistanceCm = 430.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "0.0"))
	float PedestrianCarLookAheadCm = 700.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "0.0"))
	float PedestrianRoadEscapeDistanceCm = 115.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "0.0"))
	float PedestrianAvoidanceDurationSeconds = 1.6f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic Avoidance", meta = (ClampMin = "0.1"))
	float PedestrianAvoidanceSpeedMultiplier = 1.25f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render", meta = (ClampMin = "10.0"))
	float TileSize = 400.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render")
	bool bUseOriginalTerrainHeightScale = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Render", meta = (ClampMin = "1.0"))
	float TerrainHeightScale = 200.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	FString LastLoadError;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	FString LastCitySource;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	int32 RoadNodeCount = 0;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	int32 PedestrianNodeCount = 0;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	int32 ActiveVehicleCount = 0;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	int32 ActivePedestrianCount = 0;

private:
	TArray<FSimCopterGroundRouteNode> RoadNodes;
	TArray<FSimCopterGroundRouteNode> PedestrianNodes;
	TMap<FIntPoint, int32> RoadNodeIndexByTile;
	TArray<TWeakObjectPtr<ASimCopterGroundAgent>> VehicleAgents;
	TArray<TWeakObjectPtr<ASimCopterGroundAgent>> PedestrianAgents;
	TMap<TObjectKey<ASimCopterGroundAgent>, FSimCopterVehicleTrafficState> VehicleTrafficStates;
	FRandomStream RandomStream;
	FTransform ActiveCityToWorldTransform = FTransform::Identity;
	FString ActiveOriginalGameRootPath;
	float ActiveTileSize = 400.0f;
	float SpawnThinkAccumulatorSeconds = 0.0f;
	bool bLoggedMissingPedestrianMeshes = false;

	ASimCity2000CityActor* ResolveSourceCityActor() const;
	FString ResolveCityPath() const;
	FString ResolveOriginalGameRoot() const;
	FVector GetPopulationFocusLocation() const;
	void UpdateAgentPool(float DeltaSeconds);
	void PruneAgentArray(TArray<TWeakObjectPtr<ASimCopterGroundAgent>>& Agents, const FVector& FocusLocation);
	void UpdateTrafficInteractions(float DeltaSeconds);
	void SyncVehicleTrafficStates(float DeltaSeconds);
	void ApplyTrafficLights(float DeltaSeconds);
	void ApplyVehicleFollowing(float LookAheadCm, float StopDistanceCm, float SlowDistanceCm, bool bUseNormalBraking, float DeltaSeconds);
	void ResolveVehicleOverlaps();
	void UpdateVehicleBlockageRecovery();
	void ApplyVehicleLaneGuidance(float DeltaSeconds);
	void UpdatePedestrianAvoidance();
	bool IsVehicleSpawnLocationClear(const FVector& SpawnLocation) const;
	bool TryFindPedestrianEscapeTarget(const FVector& PedestrianLocation, const FVector& EscapeDirection, FVector& OutTarget) const;
	bool TryGetPedestrianAwayFromRoadCenterDirection(const ASimCopterGroundAgent& Pedestrian, FVector& OutAwayDirection) const;
	bool IsTrafficLightIntersectionNode(int32 NodeIndex) const;
	bool IsTrafficLightGreenForApproach(int32 IntersectionNodeIndex, int32 PreviousNodeIndex) const;
	void MarkVehicleInTrafficLightLine(ASimCopterGroundAgent& Vehicle);
	void MarkVehicleCommittedToIntersection(ASimCopterGroundAgent& Vehicle);
	void MarkVehicleCollision(ASimCopterGroundAgent& Vehicle);
	bool TryStartVehicleRecovery(ASimCopterGroundAgent& Vehicle, FSimCopterVehicleTrafficState& State);
	ASimCopterGroundAgent* FindClosestBlockingVehicle(const ASimCopterGroundAgent& Vehicle, const FVector& ForwardDirection) const;
	FVector ChooseVehicleBypassDirection(const ASimCopterGroundAgent& Vehicle, const ASimCopterGroundAgent* BlockingVehicle, const FVector& ForwardDirection) const;
	bool TryMakeVehicleLaneGuidanceTarget(const ASimCopterGroundAgent& Vehicle, FVector& OutTarget, float& OutDistanceFromLane, bool& bOutTraversingDiagonalRoad) const;
	bool IsVehicleTraversingDiagonalRoadTile(const ASimCopterGroundAgent& Vehicle) const;
	bool DoesVehicleRouteTouchDiagonalRoadTile(int32 TargetIndex, int32 PreviousIndex, int32 NextIndex) const;
	FVector ClampVehicleLocationToRoadNetwork(const FVector& Location) const;
	FVector MakeVehicleRoadSafePathOffset(const FVector& BaseLocation, const FVector& DesiredOffset) const;
	void AssignNextTarget(ASimCopterGroundAgent& Agent, const TArray<FSimCopterGroundRouteNode>& Nodes);
	bool TrySpawnAgent(bool bVehicle, const FVector& FocusLocation);
	int32 ChooseNodeNearFocus(const TArray<FSimCopterGroundRouteNode>& Nodes, const FVector& FocusLocation);
	FVector MakeVehicleRouteTargetLocation(const TArray<FSimCopterGroundRouteNode>& Nodes, int32 TargetIndex, int32 PreviousIndex, int32 ApproachIndex, int32 LookAheadIndex) const;
	FVector MakeRoutePointLocation(const TArray<FSimCopterGroundRouteNode>& Nodes, int32 PointIndex, int32 PreviousIndex, int32 NextIndex, bool bVehicle) const;
};
