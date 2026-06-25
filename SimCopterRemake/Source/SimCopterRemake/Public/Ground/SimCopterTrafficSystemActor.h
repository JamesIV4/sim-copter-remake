// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
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
	void AssignNextTarget(ASimCopterGroundAgent& Agent, const TArray<FSimCopterGroundRouteNode>& Nodes);
	bool TrySpawnAgent(bool bVehicle, const FVector& FocusLocation);
	int32 ChooseNodeNearFocus(const TArray<FSimCopterGroundRouteNode>& Nodes, const FVector& FocusLocation);
	FVector MakeRoutePointLocation(const TArray<FSimCopterGroundRouteNode>& Nodes, int32 PointIndex, int32 PreviousIndex, int32 NextIndex, bool bVehicle) const;
};
