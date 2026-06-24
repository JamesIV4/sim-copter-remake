// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterTrafficSystemActor.h"

#include "Engine/World.h"
#include "Formats/SimCity2000Reader.h"
#include "GameFramework/PlayerController.h"
#include "Ground/SimCopterGroundAgent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterTrafficSystem, Log, All);

namespace
{
float GetWorldTileCenterCoordinate(float FileCoordinate, float TileSize, float HalfMapSize)
{
	return (FileCoordinate + 0.5f) * TileSize - HalfMapSize;
}

int32 GetOriginalTerrainHeightStep(const FSimCity2000Tile& Tile)
{
	const int32 BaseAltitude = static_cast<int32>(Tile.Altitude);
	const int32 SecondaryAltitude = static_cast<int32>(Tile.SecondaryAltitude);
	return (SecondaryAltitude > BaseAltitude && Tile.Terrain > 0x0F) ? SecondaryAltitude : BaseAltitude;
}

float GetTerrainSurfaceZ(const FSimCity2000Tile& Tile, float TerrainHeightScale)
{
	const int32 TunnelHeightOffset = (Tile.Terrain == 0x0D || Tile.Terrain == 0x0E) ? 1 : 0;
	return static_cast<float>(GetOriginalTerrainHeightStep(Tile) + TunnelHeightOffset + 1) * TerrainHeightScale;
}

float GetTerrainTileCenterZ(const FSimCity2000City& City, int32 FileX, int32 FileY, float TerrainHeightScale)
{
	if (FileX < 0 || FileX >= FSimCity2000City::MapSize || FileY < 0 || FileY >= FSimCity2000City::MapSize)
	{
		return 0.0f;
	}

	return GetTerrainSurfaceZ(City.Tiles[FileY * FSimCity2000City::MapSize + FileX], TerrainHeightScale);
}

bool IsRoadLikeTile(uint8 BuildingId)
{
	return BuildingId >= 0x0E && BuildingId <= 0x6F;
}

bool IsOriginalTrafficRoadTile(uint8 BuildingId)
{
	return (BuildingId > 0x2B && BuildingId < 0x3F) ||
		(BuildingId > 0x44 && BuildingId < 0x49) ||
		(BuildingId > 0x4C && BuildingId < 0x4F) ||
		(BuildingId > 0x59 && BuildingId < 0x5C);
}

bool IsBuildingLikeTile(uint8 BuildingId)
{
	return BuildingId >= 0x70;
}

bool IsPedestrianCandidateTile(const FSimCity2000City& City, int32 FileX, int32 FileY)
{
	if (FileX < 0 || FileX >= FSimCity2000City::MapSize || FileY < 0 || FileY >= FSimCity2000City::MapSize)
	{
		return false;
	}

	const FSimCity2000Tile& Tile = City.Tiles[FileY * FSimCity2000City::MapSize + FileX];
	if (Tile.bWater || IsRoadLikeTile(Tile.Building) || IsBuildingLikeTile(Tile.Building))
	{
		return false;
	}

	const int32 Offsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
	for (const int32* Offset : Offsets)
	{
		const int32 NeighborX = FileX + Offset[0];
		const int32 NeighborY = FileY + Offset[1];
		if (NeighborX < 0 || NeighborX >= FSimCity2000City::MapSize || NeighborY < 0 || NeighborY >= FSimCity2000City::MapSize)
		{
			continue;
		}

		const FSimCity2000Tile& Neighbor = City.Tiles[NeighborY * FSimCity2000City::MapSize + NeighborX];
		if (IsRoadLikeTile(Neighbor.Building))
		{
			return true;
		}
	}

	return false;
}

int32 FindNodeByTile(const TMap<FIntPoint, int32>& NodeIndexByTile, int32 FileX, int32 FileY)
{
	if (const int32* NodeIndex = NodeIndexByTile.Find(FIntPoint(FileX, FileY)))
	{
		return *NodeIndex;
	}
	return INDEX_NONE;
}

struct FOriginalTrafficStep
{
	FIntPoint TargetTile = FIntPoint::ZeroValue;
	int32 DirectionBits = 0;
	bool bHasStep = false;
};

FOriginalTrafficStep MakeOriginalTrafficStep(const FSimCopterGroundRouteNode& Node, int32 DeltaX, int32 DeltaY, int32 DirectionBits)
{
	FOriginalTrafficStep Step;
	Step.TargetTile = FIntPoint(Node.FileX + DeltaX, Node.FileY + DeltaY);
	Step.DirectionBits = DirectionBits;
	Step.bHasStep = DirectionBits != 0;
	return Step;
}

FOriginalTrafficStep OriginalTrafficNorth(const FSimCopterGroundRouteNode& Node, int32 DirectionBits = 1)
{
	return MakeOriginalTrafficStep(Node, 0, -1, DirectionBits);
}

FOriginalTrafficStep OriginalTrafficEast(const FSimCopterGroundRouteNode& Node, int32 DirectionBits = 2)
{
	return MakeOriginalTrafficStep(Node, 1, 0, DirectionBits);
}

FOriginalTrafficStep OriginalTrafficSouth(const FSimCopterGroundRouteNode& Node, int32 DirectionBits = 4)
{
	return MakeOriginalTrafficStep(Node, 0, 1, DirectionBits);
}

FOriginalTrafficStep OriginalTrafficWest(const FSimCopterGroundRouteNode& Node, int32 DirectionBits = 8)
{
	return MakeOriginalTrafficStep(Node, -1, 0, DirectionBits);
}

bool IsOriginalTrafficStepValid(
	const FOriginalTrafficStep& Step,
	const TMap<FIntPoint, int32>& RoadNodeIndexByTile,
	const TArray<FSimCopterGroundRouteNode>& RoadNodes)
{
	if (!Step.bHasStep)
	{
		return false;
	}

	if (const int32* NodeIndex = RoadNodeIndexByTile.Find(Step.TargetTile))
	{
		return RoadNodes.IsValidIndex(*NodeIndex) && IsOriginalTrafficRoadTile(RoadNodes[*NodeIndex].BuildingId);
	}

	return false;
}

FOriginalTrafficStep ChooseInitialOriginalTrafficStep(
	const FSimCopterGroundRouteNode& Node,
	FRandomStream& RandomStream)
{
	switch (Node.BuildingId)
	{
	case 0x2C:
		return RandomStream.RandRange(0, 1) == 0 ? OriginalTrafficSouth(Node) : OriginalTrafficNorth(Node);
	case 0x2D:
		return RandomStream.RandRange(0, 1) == 0 ? OriginalTrafficEast(Node) : OriginalTrafficWest(Node);
	case 0x2E:
		return RandomStream.RandRange(0, 1) == 0 ? OriginalTrafficEast(Node) : OriginalTrafficWest(Node, 0x18);
	case 0x2F:
		return RandomStream.RandRange(0, 1) == 0 ? OriginalTrafficSouth(Node) : OriginalTrafficNorth(Node, 0x11);
	case 0x30:
		return RandomStream.RandRange(0, 1) != 0 ? OriginalTrafficWest(Node) : OriginalTrafficEast(Node, 0x12);
	case 0x31:
		return RandomStream.RandRange(0, 1) == 0 ? OriginalTrafficSouth(Node, 0x14) : OriginalTrafficNorth(Node);
	case 0x32:
		return RandomStream.RandRange(0, 1) != 0 ? OriginalTrafficNorth(Node, 9) : OriginalTrafficEast(Node, 6);
	case 0x33:
		return RandomStream.RandRange(0, 1) == 0 ? OriginalTrafficSouth(Node, 0x0C) : OriginalTrafficEast(Node, 3);
	case 0x34:
		return RandomStream.RandRange(0, 1) == 0 ? OriginalTrafficSouth(Node, 6) : OriginalTrafficWest(Node, 9);
	case 0x35:
		return RandomStream.RandRange(0, 1) == 0 ? OriginalTrafficWest(Node, 0x0C) : OriginalTrafficNorth(Node, 3);
	case 0x36:
		switch (RandomStream.RandRange(0, 2))
		{
		case 0: return OriginalTrafficNorth(Node);
		case 1: return OriginalTrafficEast(Node);
		default: return OriginalTrafficWest(Node);
		}
	case 0x37:
		switch (RandomStream.RandRange(0, 2))
		{
		case 0: return OriginalTrafficNorth(Node);
		case 1: return OriginalTrafficSouth(Node);
		default: return OriginalTrafficEast(Node);
		}
	case 0x38:
		switch (RandomStream.RandRange(0, 2))
		{
		case 0: return OriginalTrafficSouth(Node);
		case 1: return OriginalTrafficEast(Node);
		default: return OriginalTrafficWest(Node);
		}
	case 0x39:
		switch (RandomStream.RandRange(0, 2))
		{
		case 0: return OriginalTrafficNorth(Node);
		case 1: return OriginalTrafficSouth(Node);
		default: return OriginalTrafficWest(Node);
		}
	case 0x3A:
		switch (RandomStream.RandRange(0, 3))
		{
		case 0: return OriginalTrafficNorth(Node);
		case 1: return OriginalTrafficSouth(Node);
		case 2: return OriginalTrafficWest(Node);
		default: return OriginalTrafficEast(Node);
		}
	case 0x3B:
		return RandomStream.RandRange(0, 1) != 0 ? OriginalTrafficWest(Node, 0x18) : OriginalTrafficEast(Node);
	case 0x3C:
		return RandomStream.RandRange(0, 1) == 0 ? OriginalTrafficSouth(Node) : OriginalTrafficNorth(Node, 0x11);
	case 0x3D:
		return RandomStream.RandRange(0, 1) != 0 ? OriginalTrafficWest(Node) : OriginalTrafficEast(Node, 0x12);
	case 0x3E:
		return RandomStream.RandRange(0, 1) == 0 ? OriginalTrafficSouth(Node, 0x14) : OriginalTrafficNorth(Node);
	case 0x45:
	case 0x48:
	case 0x4D:
		return (Node.FileY & 1) == 0 ? OriginalTrafficWest(Node) : OriginalTrafficEast(Node);
	case 0x46:
	case 0x47:
	case 0x4E:
	case 0x5A:
	case 0x5B:
		return (Node.FileX & 1) == 0 ? OriginalTrafficSouth(Node) : OriginalTrafficNorth(Node);
	default:
		return FOriginalTrafficStep();
	}
}

FOriginalTrafficStep ChooseFallbackOriginalTrafficStep(
	const FSimCopterGroundRouteNode& Node,
	int32 PreviousDirectionBits,
	FRandomStream& RandomStream)
{
	switch (Node.BuildingId)
	{
	case 0x2C:
		return PreviousDirectionBits == 1 ? OriginalTrafficNorth(Node) : OriginalTrafficSouth(Node);
	case 0x2D:
		return PreviousDirectionBits == 2 ? OriginalTrafficWest(Node) : OriginalTrafficEast(Node);
	case 0x2E:
		return PreviousDirectionBits == 2 ? OriginalTrafficWest(Node, 0x18) : OriginalTrafficEast(Node);
	case 0x2F:
		return PreviousDirectionBits == 4 ? OriginalTrafficNorth(Node, 0x11) : OriginalTrafficSouth(Node);
	case 0x30:
		return PreviousDirectionBits == 0x18 ? OriginalTrafficWest(Node) : OriginalTrafficEast(Node, 0x12);
	case 0x31:
		return PreviousDirectionBits == 0x14 ? OriginalTrafficNorth(Node) : OriginalTrafficSouth(Node, 0x14);
	case 0x32:
		return PreviousDirectionBits == 6 ? OriginalTrafficNorth(Node, 9) : OriginalTrafficEast(Node, 6);
	case 0x33:
		return PreviousDirectionBits == 0x0C ? OriginalTrafficEast(Node, 3) : OriginalTrafficSouth(Node, 0x0C);
	case 0x34:
		return PreviousDirectionBits == 6 ? OriginalTrafficWest(Node, 9) : OriginalTrafficSouth(Node, 6);
	case 0x35:
		return PreviousDirectionBits == 0x0C ? OriginalTrafficNorth(Node, 3) : OriginalTrafficWest(Node, 0x0C);
	case 0x36:
	case 0x37:
	case 0x38:
	case 0x39:
	case 0x3A:
		return ChooseInitialOriginalTrafficStep(Node, RandomStream);
	case 0x3B:
		return PreviousDirectionBits == 2 ? OriginalTrafficWest(Node, 0x18) : OriginalTrafficEast(Node);
	case 0x3C:
		return PreviousDirectionBits == 4 ? OriginalTrafficNorth(Node, 0x11) : OriginalTrafficSouth(Node);
	case 0x3D:
		return PreviousDirectionBits == 0x18 ? OriginalTrafficWest(Node) : OriginalTrafficEast(Node, 0x12);
	case 0x3E:
		return PreviousDirectionBits == 0x14 ? OriginalTrafficNorth(Node) : OriginalTrafficSouth(Node, 0x14);
	case 0x45:
	case 0x48:
	case 0x4D:
		return (Node.FileY & 1) == 0 ? OriginalTrafficWest(Node) : OriginalTrafficEast(Node);
	case 0x46:
	case 0x47:
	case 0x4E:
	case 0x5A:
	case 0x5B:
		return (Node.FileX & 1) == 0 ? OriginalTrafficSouth(Node) : OriginalTrafficNorth(Node);
	default:
		return FOriginalTrafficStep();
	}
}

FOriginalTrafficStep ChooseOriginalTrafficStep(
	const FSimCopterGroundRouteNode& Node,
	int32 PreviousDirectionBits,
	FRandomStream& RandomStream,
	const TMap<FIntPoint, int32>& RoadNodeIndexByTile,
	const TArray<FSimCopterGroundRouteNode>& RoadNodes)
{
	const FOriginalTrafficStep InitialStep = ChooseInitialOriginalTrafficStep(Node, RandomStream);
	if (IsOriginalTrafficStepValid(InitialStep, RoadNodeIndexByTile, RoadNodes))
	{
		return InitialStep;
	}

	const int32 FallbackDirectionBits = InitialStep.DirectionBits != 0 ? InitialStep.DirectionBits : PreviousDirectionBits;
	const FOriginalTrafficStep FallbackStep = ChooseFallbackOriginalTrafficStep(Node, FallbackDirectionBits, RandomStream);
	return IsOriginalTrafficStepValid(FallbackStep, RoadNodeIndexByTile, RoadNodes) ? FallbackStep : FOriginalTrafficStep();
}
}

ASimCopterTrafficSystemActor::ASimCopterTrafficSystemActor()
{
	PrimaryActorTick.bCanEverTick = true;
	GroundAgentClass = ASimCopterGroundAgent::StaticClass();
	CityFile.FilePath = TEXT("../Reference/SimCopterOriginalGame/cities/cape wells.sc2");
	OriginalGameRoot.Path = TEXT("../Reference/SimCopterOriginalGame");

	VehicleMeshNames = {
		TEXT("AUTO"),
		TEXT("AUTO2"),
		TEXT("AUTO3"),
		TEXT("AUTO4"),
		TEXT("AUTO5"),
		TEXT("AUTO6"),
		TEXT("CARPOLIC"),
		TEXT("CARAMBUL")
	};

	PedestrianMeshNames = {
		TEXT("PEOPLE1")
	};

	RandomStream.Initialize(RandomSeed);
}

void ASimCopterTrafficSystemActor::BeginPlay()
{
	Super::BeginPlay();

	RandomStream.Initialize(RandomSeed);
	if (bSpawnOnBeginPlay)
	{
		RebuildSpawnData();
	}
}

void ASimCopterTrafficSystemActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateAgentPool(DeltaSeconds);
}

bool ASimCopterTrafficSystemActor::RebuildSpawnData()
{
	LastLoadError.Reset();
	RoadNodes.Reset();
	PedestrianNodes.Reset();
	RoadNodeIndexByTile.Reset();
	VehicleAgents.Reset();
	PedestrianAgents.Reset();

	const FString CityPath = ResolveCityPath();
	FSimCity2000City City;
	FString Error;
	if (CityPath.IsEmpty() || !FSimCity2000Reader::LoadCityFromFile(CityPath, City, Error))
	{
		LastLoadError = CityPath.IsEmpty() ? TEXT("Traffic system city path is empty.") : Error;
		UE_LOG(LogSimCopterTrafficSystem, Warning, TEXT("%s"), *LastLoadError);
		return false;
	}

	const float EffectiveTerrainHeightScale = bUseOriginalTerrainHeightScale ? TileSize * 0.5f : TerrainHeightScale;
	const float HalfMapSize = FSimCity2000City::MapSize * TileSize * 0.5f;

	TMap<FIntPoint, int32> PedestrianIndexByTile;

	for (int32 FileY = 0; FileY < FSimCity2000City::MapSize; ++FileY)
	{
		for (int32 FileX = 0; FileX < FSimCity2000City::MapSize; ++FileX)
		{
			const FSimCity2000Tile& Tile = City.Tiles[FileY * FSimCity2000City::MapSize + FileX];
			const float WorldX = GetWorldTileCenterCoordinate(static_cast<float>(FileX), TileSize, HalfMapSize);
			const float WorldY = -GetWorldTileCenterCoordinate(static_cast<float>(FileY), TileSize, HalfMapSize);
			const float WorldZ = GetTerrainTileCenterZ(City, FileX, FileY, EffectiveTerrainHeightScale);

			if (!Tile.bWater && IsOriginalTrafficRoadTile(Tile.Building))
			{
				const int32 NodeIndex = RoadNodes.Num();
				FSimCopterGroundRouteNode& Node = RoadNodes.AddDefaulted_GetRef();
				Node.FileX = FileX;
				Node.FileY = FileY;
				Node.BuildingId = Tile.Building;
				Node.Location = FVector(WorldX, WorldY, WorldZ + 10.0f);
				RoadNodeIndexByTile.Add(FIntPoint(FileX, FileY), NodeIndex);
			}

			if (IsPedestrianCandidateTile(City, FileX, FileY))
			{
				const int32 NodeIndex = PedestrianNodes.Num();
				FSimCopterGroundRouteNode& Node = PedestrianNodes.AddDefaulted_GetRef();
				Node.FileX = FileX;
				Node.FileY = FileY;
				Node.BuildingId = Tile.Building;
				Node.Location = FVector(WorldX, WorldY, WorldZ + 10.0f);
				PedestrianIndexByTile.Add(FIntPoint(FileX, FileY), NodeIndex);
			}
		}
	}

	const int32 NeighborOffsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
	for (FSimCopterGroundRouteNode& Node : RoadNodes)
	{
		for (const int32* Offset : NeighborOffsets)
		{
			const int32 NeighborIndex = FindNodeByTile(RoadNodeIndexByTile, Node.FileX + Offset[0], Node.FileY + Offset[1]);
			if (NeighborIndex != INDEX_NONE)
			{
				Node.Neighbors.Add(NeighborIndex);
			}
		}
	}

	for (FSimCopterGroundRouteNode& Node : PedestrianNodes)
	{
		for (const int32* Offset : NeighborOffsets)
		{
			const int32 NeighborIndex = FindNodeByTile(PedestrianIndexByTile, Node.FileX + Offset[0], Node.FileY + Offset[1]);
			if (NeighborIndex != INDEX_NONE)
			{
				Node.Neighbors.Add(NeighborIndex);
			}
		}
	}

	RoadNodeCount = RoadNodes.Num();
	PedestrianNodeCount = PedestrianNodes.Num();

	UE_LOG(
		LogSimCopterTrafficSystem,
		Display,
		TEXT("Built SimCopter traffic spawn data from '%s': roadNodes=%d pedestrianNodes=%d maxVehicles=%d maxPedestrians=%d spawnRadius=%.0f."),
		*CityPath,
		RoadNodeCount,
		PedestrianNodeCount,
		MaxVehicleAgents,
		MaxPedestrianAgents,
		SpawnRadiusCm);

	return true;
}

FString ASimCopterTrafficSystemActor::ResolveCityPath() const
{
	const FString ConfiguredPath = CityFile.FilePath.TrimStartAndEnd();
	if (ConfiguredPath.IsEmpty())
	{
		return FString();
	}

	if (FPaths::IsRelative(ConfiguredPath))
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), ConfiguredPath));
	}

	return FPaths::ConvertRelativePathToFull(ConfiguredPath);
}

FString ASimCopterTrafficSystemActor::ResolveOriginalGameRoot() const
{
	const FString ConfiguredPath = OriginalGameRoot.Path.TrimStartAndEnd();
	if (ConfiguredPath.IsEmpty())
	{
		return FString();
	}

	if (FPaths::IsRelative(ConfiguredPath))
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), ConfiguredPath));
	}

	return FPaths::ConvertRelativePathToFull(ConfiguredPath);
}

FVector ASimCopterTrafficSystemActor::GetPopulationFocusLocation() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0))
		{
			if (const APawn* Pawn = PlayerController->GetPawn())
			{
				return Pawn->GetActorLocation();
			}
		}
	}

	return GetActorLocation();
}

void ASimCopterTrafficSystemActor::UpdateAgentPool(float DeltaSeconds)
{
	const FVector FocusLocation = GetPopulationFocusLocation();
	PruneAgentArray(VehicleAgents, FocusLocation);
	PruneAgentArray(PedestrianAgents, FocusLocation);

	for (TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : VehicleAgents)
	{
		if (ASimCopterGroundAgent* Agent = AgentPtr.Get())
		{
			if (!Agent->HasMoveTarget() || Agent->IsNearMoveTarget())
			{
				AssignNextTarget(*Agent, RoadNodes);
			}
		}
	}

	for (TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : PedestrianAgents)
	{
		if (ASimCopterGroundAgent* Agent = AgentPtr.Get())
		{
			if (!Agent->HasMoveTarget() || Agent->IsNearMoveTarget())
			{
				AssignNextTarget(*Agent, PedestrianNodes);
			}
		}
	}

	SpawnThinkAccumulatorSeconds += DeltaSeconds;
	if (SpawnThinkAccumulatorSeconds < SpawnThinkIntervalSeconds)
	{
		ActiveVehicleCount = VehicleAgents.Num();
		ActivePedestrianCount = PedestrianAgents.Num();
		return;
	}
	SpawnThinkAccumulatorSeconds = 0.0f;

	int32 SpawnAttemptsRemaining = MaxSpawnAttemptsPerThink;
	while (VehicleAgents.Num() < MaxVehicleAgents && SpawnAttemptsRemaining-- > 0)
	{
		if (!TrySpawnAgent(true, FocusLocation))
		{
			break;
		}
	}

	SpawnAttemptsRemaining = MaxSpawnAttemptsPerThink;
	while (PedestrianAgents.Num() < MaxPedestrianAgents && SpawnAttemptsRemaining-- > 0)
	{
		if (!TrySpawnAgent(false, FocusLocation))
		{
			break;
		}
	}

	ActiveVehicleCount = VehicleAgents.Num();
	ActivePedestrianCount = PedestrianAgents.Num();
}

void ASimCopterTrafficSystemActor::PruneAgentArray(TArray<TWeakObjectPtr<ASimCopterGroundAgent>>& Agents, const FVector& FocusLocation)
{
	for (int32 Index = Agents.Num() - 1; Index >= 0; --Index)
	{
		ASimCopterGroundAgent* Agent = Agents[Index].Get();
		if (Agent == nullptr)
		{
			Agents.RemoveAtSwap(Index);
			continue;
		}

		const float DistanceSq = FVector::DistSquared2D(Agent->GetActorLocation(), FocusLocation);
		if (DistanceSq > FMath::Square(DespawnRadiusCm))
		{
			Agent->Destroy();
			Agents.RemoveAtSwap(Index);
		}
	}
}

void ASimCopterTrafficSystemActor::AssignNextTarget(ASimCopterGroundAgent& Agent, const TArray<FSimCopterGroundRouteNode>& Nodes)
{
	if (Nodes.Num() == 0)
	{
		Agent.ClearMoveTarget();
		return;
	}

	int32 BestNodeIndex = INDEX_NONE;
	float BestDistanceSq = TNumericLimits<float>::Max();
	const FVector AgentLocation = Agent.GetActorLocation();
	for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
	{
		const float DistanceSq = FVector::DistSquared2D(Nodes[NodeIndex].Location, AgentLocation);
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			BestNodeIndex = NodeIndex;
		}
	}

	if (BestNodeIndex == INDEX_NONE)
	{
		Agent.ClearMoveTarget();
		return;
	}

	const FSimCopterGroundRouteNode& Node = Nodes[BestNodeIndex];
	if (Agent.GetAgentKind() == ESimCopterGroundAgentKind::Vehicle && &Nodes == &RoadNodes)
	{
		const FOriginalTrafficStep OriginalStep = ChooseOriginalTrafficStep(
			Node,
			Agent.GetOriginalTrafficDirectionBits(),
			RandomStream,
			RoadNodeIndexByTile,
			RoadNodes);
		if (OriginalStep.bHasStep)
		{
			if (const int32* TargetNodeIndex = RoadNodeIndexByTile.Find(OriginalStep.TargetTile))
			{
				Agent.SetOriginalTrafficDirectionBits(OriginalStep.DirectionBits);
				Agent.SetMoveTarget(RoadNodes[*TargetNodeIndex].Location);
				return;
			}
		}
	}

	if (Node.Neighbors.Num() > 0 && RandomStream.FRand() < 0.84f)
	{
		const int32 NeighborIndex = Node.Neighbors[RandomStream.RandRange(0, Node.Neighbors.Num() - 1)];
		const FSimCopterGroundRouteNode& Neighbor = Nodes[NeighborIndex];
		if (Agent.GetAgentKind() == ESimCopterGroundAgentKind::Vehicle)
		{
			const int32 DeltaX = FMath::Clamp(Neighbor.FileX - Node.FileX, -1, 1);
			const int32 DeltaY = FMath::Clamp(Neighbor.FileY - Node.FileY, -1, 1);
			const int32 DirectionBits = DeltaY < 0 ? 1 : (DeltaX > 0 ? 2 : (DeltaY > 0 ? 4 : (DeltaX < 0 ? 8 : 0)));
			Agent.SetOriginalTrafficDirectionBits(DirectionBits);
		}
		Agent.SetMoveTarget(Nodes[NeighborIndex].Location);
	}
	else
	{
		const int32 RandomNodeIndex = RandomStream.RandRange(0, Nodes.Num() - 1);
		if (Agent.GetAgentKind() == ESimCopterGroundAgentKind::Vehicle)
		{
			Agent.SetOriginalTrafficDirectionBits(0);
		}
		Agent.SetMoveTarget(Nodes[RandomNodeIndex].Location);
	}
}

bool ASimCopterTrafficSystemActor::TrySpawnAgent(bool bVehicle, const FVector& FocusLocation)
{
	const TArray<FSimCopterGroundRouteNode>& Nodes = bVehicle ? RoadNodes : PedestrianNodes;
	if (GetWorld() == nullptr || GroundAgentClass == nullptr || Nodes.Num() == 0)
	{
		return false;
	}

	if (!bVehicle && PedestrianMeshNames.Num() == 0)
	{
		if (!bLoggedMissingPedestrianMeshes)
		{
			bLoggedMissingPedestrianMeshes = true;
			UE_LOG(LogSimCopterTrafficSystem, Log, TEXT("Pedestrian spawning is paused until original pedestrian model names/frames are decoded from X/privanim.df."));
		}
		return false;
	}

	const int32 NodeIndex = ChooseNodeNearFocus(Nodes, FocusLocation);
	if (NodeIndex == INDEX_NONE)
	{
		return false;
	}

	const FSimCopterGroundRouteNode& Node = Nodes[NodeIndex];
	FRotator SpawnRotation = FRotator::ZeroRotator;
	if (Node.Neighbors.Num() > 0)
	{
		const FVector Target = Nodes[Node.Neighbors[RandomStream.RandRange(0, Node.Neighbors.Num() - 1)]].Location;
		SpawnRotation.Yaw = (Target - Node.Location).Rotation().Yaw;
	}
	else
	{
		SpawnRotation.Yaw = RandomStream.FRandRange(0.0f, 360.0f);
	}

	const FVector SpawnLocation = Node.Location + FVector::UpVector * (bVehicle ? 100.0f : 92.0f);
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ASimCopterGroundAgent* Agent = GetWorld()->SpawnActor<ASimCopterGroundAgent>(GroundAgentClass, SpawnLocation, SpawnRotation, SpawnParams);
	if (Agent == nullptr)
	{
		return false;
	}

	const FString MeshName = bVehicle && VehicleMeshNames.Num() > 0
		? VehicleMeshNames[RandomStream.RandRange(0, VehicleMeshNames.Num() - 1)]
		: (!bVehicle && PedestrianMeshNames.Num() > 0 ? PedestrianMeshNames[RandomStream.RandRange(0, PedestrianMeshNames.Num() - 1)] : FString());

	Agent->ConfigureAgent(
		bVehicle ? ESimCopterGroundAgentKind::Vehicle : ESimCopterGroundAgentKind::Pedestrian,
		MeshName,
		ResolveOriginalGameRoot(),
		bVehicle ? VehicleSpeedCmPerSec : PedestrianSpeedCmPerSec);

	if (bRequireOriginalPopulationMeshes && !Agent->IsUsingOriginalMesh())
	{
		UE_LOG(LogSimCopterTrafficSystem, Warning, TEXT("Discarding %s population agent because original mesh '%s' could not be loaded."),
			bVehicle ? TEXT("vehicle") : TEXT("pedestrian"),
			*MeshName);
		Agent->Destroy();
		return false;
	}

	AssignNextTarget(*Agent, Nodes);

	if (bVehicle)
	{
		VehicleAgents.Add(Agent);
	}
	else
	{
		PedestrianAgents.Add(Agent);
	}

	return true;
}

int32 ASimCopterTrafficSystemActor::ChooseNodeNearFocus(const TArray<FSimCopterGroundRouteNode>& Nodes, const FVector& FocusLocation)
{
	if (Nodes.Num() == 0)
	{
		return INDEX_NONE;
	}

	const float MinDistanceSq = FMath::Square(MinSpawnDistanceCm);
	const float MaxDistanceSq = FMath::Square(SpawnRadiusCm);
	int32 BestFallbackIndex = INDEX_NONE;
	float BestFallbackDistanceSq = TNumericLimits<float>::Max();

	for (int32 Attempt = 0; Attempt < 80; ++Attempt)
	{
		const int32 NodeIndex = RandomStream.RandRange(0, Nodes.Num() - 1);
		const float DistanceSq = FVector::DistSquared2D(Nodes[NodeIndex].Location, FocusLocation);
		if (DistanceSq >= MinDistanceSq && DistanceSq <= MaxDistanceSq)
		{
			return NodeIndex;
		}

		if (DistanceSq < BestFallbackDistanceSq)
		{
			BestFallbackDistanceSq = DistanceSq;
			BestFallbackIndex = NodeIndex;
		}
	}

	return BestFallbackDistanceSq <= MaxDistanceSq ? BestFallbackIndex : INDEX_NONE;
}
