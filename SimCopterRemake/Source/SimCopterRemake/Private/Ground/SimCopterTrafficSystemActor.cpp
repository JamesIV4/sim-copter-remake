// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterTrafficSystemActor.h"

#include "City/SimCity2000CityActor.h"
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

// Surface roads only: XBLD ids 0x1D..0x2B are the RD29..RD43 road meshes. (Power lines are
// 0x0E..0x1C / WR14..WR28, and 0x2C..0x3A are RL44..RL58 RAILS - not drivable. The previous
// implementation matched the 0x2C..0x3E rail range, which put every car on the train tracks.)
bool IsSurfaceRoadTile(uint8 BuildingId)
{
	return BuildingId >= 0x1D && BuildingId <= 0x2B;
}

// The shipped cities use 0x43/0x44 as road-continuity crossing/bridge pieces: 0x43 connects E/W,
// 0x44 connects N/S. They are selected by the city actor's bridge dispatch and appear exactly where
// road neighbours would otherwise be split by overlay/crossing infrastructure.
bool IsRoadCrossingTile(uint8 BuildingId)
{
	return BuildingId == 0x43 || BuildingId == 0x44;
}

// Drivable tiles for cars: surface roads, crossing pieces, plus the elevated/bridge road ids
// confirmed as roads by the bridge-mesh dispatch (0x45/0x46 road-on-bridge, 0x47/0x48,
// 0x4d/0x4e, 0x5a/0x5b).
bool IsOriginalTrafficRoadTile(uint8 BuildingId)
{
	return IsSurfaceRoadTile(BuildingId) ||
		IsRoadCrossingTile(BuildingId) ||
		(BuildingId >= 0x45 && BuildingId <= 0x48) ||
		(BuildingId == 0x4D || BuildingId == 0x4E) ||
		(BuildingId == 0x5A || BuildingId == 0x5B);
}

bool IsPedestrianRoadTile(uint8 BuildingId)
{
	return IsSurfaceRoadTile(BuildingId) || IsRoadCrossingTile(BuildingId);
}

enum ERoadOpeningMask : int32
{
	RoadOpenNorth = 1 << 0,
	RoadOpenEast = 1 << 1,
	RoadOpenSouth = 1 << 2,
	RoadOpenWest = 1 << 3,
	RoadOpenAll = RoadOpenNorth | RoadOpenEast | RoadOpenSouth | RoadOpenWest
};

int32 GetRoadOpeningMask(uint8 BuildingId)
{
	switch (BuildingId)
	{
	case 0x1D:
	case 0x20:
	case 0x22:
	case 0x43:
	case 0x45:
		return RoadOpenEast | RoadOpenWest;

	case 0x1E:
	case 0x1F:
	case 0x21:
	case 0x44:
	case 0x46:
		return RoadOpenNorth | RoadOpenSouth;

	case 0x23:
		return RoadOpenSouth | RoadOpenWest;
	case 0x24:
		return RoadOpenEast | RoadOpenSouth;
	case 0x25:
		return RoadOpenNorth | RoadOpenEast;
	case 0x26:
		return RoadOpenNorth | RoadOpenWest;

	case 0x27:
		return RoadOpenNorth | RoadOpenSouth | RoadOpenWest;
	case 0x28:
		return RoadOpenEast | RoadOpenSouth | RoadOpenWest;
	case 0x29:
		return RoadOpenNorth | RoadOpenEast | RoadOpenSouth;
	case 0x2A:
		return RoadOpenNorth | RoadOpenEast | RoadOpenWest;
	case 0x2B:
		return RoadOpenAll;

	default:
		return RoadOpenAll;
	}
}

int32 GetRoadOpeningForOffset(int32 DeltaX, int32 DeltaY)
{
	if (DeltaY < 0)
	{
		return RoadOpenNorth;
	}
	if (DeltaX > 0)
	{
		return RoadOpenEast;
	}
	if (DeltaY > 0)
	{
		return RoadOpenSouth;
	}
	if (DeltaX < 0)
	{
		return RoadOpenWest;
	}
	return 0;
}

int32 GetOppositeRoadOpening(int32 Opening)
{
	switch (Opening)
	{
	case RoadOpenNorth:
		return RoadOpenSouth;
	case RoadOpenEast:
		return RoadOpenWest;
	case RoadOpenSouth:
		return RoadOpenNorth;
	case RoadOpenWest:
		return RoadOpenEast;
	default:
		return 0;
	}
}

bool CanRoadTilesConnect(uint8 FromBuildingId, uint8 ToBuildingId, int32 DeltaX, int32 DeltaY)
{
	const int32 FromOpening = GetRoadOpeningForOffset(DeltaX, DeltaY);
	if (FromOpening == 0)
	{
		return false;
	}

	const int32 ToOpening = GetOppositeRoadOpening(FromOpening);
	return (GetRoadOpeningMask(FromBuildingId) & FromOpening) != 0 &&
		(GetRoadOpeningMask(ToBuildingId) & ToOpening) != 0;
}

FVector2D GetRoadOpeningLocalDirection(int32 Opening)
{
	switch (Opening)
	{
	case RoadOpenNorth:
		return FVector2D(0.0f, 1.0f);
	case RoadOpenEast:
		return FVector2D(1.0f, 0.0f);
	case RoadOpenSouth:
		return FVector2D(0.0f, -1.0f);
	case RoadOpenWest:
		return FVector2D(-1.0f, 0.0f);
	default:
		return FVector2D::ZeroVector;
	}
}

bool HasRoadOpening(int32 Mask, int32 Opening)
{
	return (Mask & Opening) != 0;
}

int32 CountRoadOpenings(int32 Mask)
{
	int32 Count = 0;
	Count += HasRoadOpening(Mask, RoadOpenNorth) ? 1 : 0;
	Count += HasRoadOpening(Mask, RoadOpenEast) ? 1 : 0;
	Count += HasRoadOpening(Mask, RoadOpenSouth) ? 1 : 0;
	Count += HasRoadOpening(Mask, RoadOpenWest) ? 1 : 0;
	return Count;
}

bool IsOppositeRoadOpeningPair(int32 Mask)
{
	return Mask == (RoadOpenNorth | RoadOpenSouth) || Mask == (RoadOpenEast | RoadOpenWest);
}

FVector2D GetAdjacentOpeningBisector(int32 Mask)
{
	FVector2D Sum = FVector2D::ZeroVector;
	if (HasRoadOpening(Mask, RoadOpenNorth))
	{
		Sum += GetRoadOpeningLocalDirection(RoadOpenNorth);
	}
	if (HasRoadOpening(Mask, RoadOpenEast))
	{
		Sum += GetRoadOpeningLocalDirection(RoadOpenEast);
	}
	if (HasRoadOpening(Mask, RoadOpenSouth))
	{
		Sum += GetRoadOpeningLocalDirection(RoadOpenSouth);
	}
	if (HasRoadOpening(Mask, RoadOpenWest))
	{
		Sum += GetRoadOpeningLocalDirection(RoadOpenWest);
	}
	return Sum;
}

FVector2D GetRoadCenterlineLocalOffset(uint8 BuildingId, float TileSize)
{
	const int32 Mask = GetRoadOpeningMask(BuildingId);
	if (CountRoadOpenings(Mask) == 2 && !IsOppositeRoadOpeningPair(Mask))
	{
		// RD35..RD38 are visually diagonal/curved corner road pieces. A tile-centre route makes
		// agents stair-step from one side of the road to the other; bias the waypoint toward the
		// corner shared by the two openings so the hop follows the road's diagonal through the tile.
		return GetAdjacentOpeningBisector(Mask) * (TileSize * 0.25f);
	}

	return FVector2D::ZeroVector;
}

// SimCopter pedestrians walk the sidewalks that are part of the road tiles, not the empty land
// beside them. Pull a road tile's pedestrian node toward one edge (parallel to the road) so a
// straight road grows a continuous sidewalk while cars keep the centre line.
FVector2D GetRoadSidewalkLocalOffset(const FSimCity2000City& City, int32 FileX, int32 FileY, float TileSize)
{
	const FSimCity2000Tile& Tile = City.Tiles[FileY * FSimCity2000City::MapSize + FileX];
	const int32 Mask = GetRoadOpeningMask(Tile.Building);
	if (CountRoadOpenings(Mask) == 2 && !IsOppositeRoadOpeningPair(Mask))
	{
		// Keep pedestrians on the same diagonal/corner side as the visible road piece. The old
		// straight-road test alternated between X and Y offsets on these tiles, which made crowds
		// weave back and forth across angled roads.
		return GetAdjacentOpeningBisector(Mask) * (TileSize * 0.34f);
	}

	auto IsRoadAt = [&City](int32 X, int32 Y) -> bool
	{
		if (X < 0 || X >= FSimCity2000City::MapSize || Y < 0 || Y >= FSimCity2000City::MapSize)
		{
			return false;
		}
		return IsPedestrianRoadTile(City.Tiles[Y * FSimCity2000City::MapSize + X].Building);
	};

	const bool bNorthSouth = IsRoadAt(FileX, FileY - 1) || IsRoadAt(FileX, FileY + 1);
	const bool bEastWest = IsRoadAt(FileX - 1, FileY) || IsRoadAt(FileX + 1, FileY);
	const float SidewalkInset = TileSize * 0.34f;

	if (bNorthSouth && !bEastWest)
	{
		return FVector2D(-SidewalkInset, 0.0f); // road runs N/S -> sidewalk along local X
	}
	if (bEastWest && !bNorthSouth)
	{
		return FVector2D(0.0f, SidewalkInset); // road runs E/W -> sidewalk along local Y
	}
	return FVector2D::ZeroVector; // intersection / isolated tile: keep pedestrians centred
}

int32 FindNodeByTile(const TMap<FIntPoint, int32>& NodeIndexByTile, int32 FileX, int32 FileY)
{
	if (const int32* NodeIndex = NodeIndexByTile.Find(FIntPoint(FileX, FileY)))
	{
		return *NodeIndex;
	}
	return INDEX_NONE;
}

int32 FindNearestNodeIndex(const TArray<FSimCopterGroundRouteNode>& Nodes, const FVector& Location)
{
	int32 BestIndex = INDEX_NONE;
	float BestDistanceSq = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		const float DistanceSq = FVector::DistSquared2D(Nodes[Index].Location, Location);
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			BestIndex = Index;
		}
	}
	return BestIndex;
}

// Walk the road/sidewalk graph: from FromIndex pick a connected neighbour to drive to next.
// Avoids immediate U-turns (excludes PrevIndex) unless at a dead-end, and prefers continuing
// roughly straight so traffic flows down a road and only occasionally turns at intersections.
// Because it only ever returns an adjacent graph node, agents can never leave the road network.
int32 ChooseNextRouteNode(
	const TArray<FSimCopterGroundRouteNode>& Nodes,
	int32 FromIndex,
	int32 PrevIndex,
	FRandomStream& RandomStream)
{
	if (!Nodes.IsValidIndex(FromIndex))
	{
		return INDEX_NONE;
	}

	const FSimCopterGroundRouteNode& From = Nodes[FromIndex];

	TArray<int32, TInlineAllocator<4>> Candidates;
	for (const int32 NeighborIndex : From.Neighbors)
	{
		if (Nodes.IsValidIndex(NeighborIndex) && NeighborIndex != PrevIndex)
		{
			Candidates.Add(NeighborIndex);
		}
	}

	// Dead-end: the only neighbour is where we came from, so allow the U-turn.
	if (Candidates.Num() == 0)
	{
		for (const int32 NeighborIndex : From.Neighbors)
		{
			if (Nodes.IsValidIndex(NeighborIndex))
			{
				Candidates.Add(NeighborIndex);
			}
		}
	}

	if (Candidates.Num() == 0)
	{
		return INDEX_NONE;
	}
	if (Candidates.Num() == 1)
	{
		return Candidates[0];
	}

	// Prefer the neighbour that best continues the current heading; turn at intersections ~30%.
	if (Nodes.IsValidIndex(PrevIndex))
	{
		const FVector InDirection = (From.Location - Nodes[PrevIndex].Location).GetSafeNormal2D();
		if (!InDirection.IsNearlyZero() && RandomStream.FRand() < 0.7f)
		{
			int32 StraightestIndex = Candidates[0];
			float BestDot = -2.0f;
			for (const int32 NeighborIndex : Candidates)
			{
				const FVector OutDirection = (Nodes[NeighborIndex].Location - From.Location).GetSafeNormal2D();
				const float Dot = FVector::DotProduct(InDirection, OutDirection);
				if (Dot > BestDot)
				{
					BestDot = Dot;
					StraightestIndex = NeighborIndex;
				}
			}
			return StraightestIndex;
		}
	}

	return Candidates[RandomStream.RandRange(0, Candidates.Num() - 1)];
}

FVector GetFlatSafeNormal(const FVector& Vector)
{
	const FVector Flat(Vector.X, Vector.Y, 0.0f);
	return Flat.GetSafeNormal();
}

FVector GetAgentTravelDirection(const ASimCopterGroundAgent& Agent)
{
	const FVector VelocityDirection = GetFlatSafeNormal(Agent.GetCurrentVelocityCmPerSec());
	if (!VelocityDirection.IsNearlyZero())
	{
		return VelocityDirection;
	}
	return GetFlatSafeNormal(Agent.GetActorForwardVector());
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
	UpdateTrafficInteractions();
}

ASimCity2000CityActor* ASimCopterTrafficSystemActor::ResolveSourceCityActor() const
{
	if (!bUseActiveCityActor)
	{
		return nullptr;
	}

	if (SourceCityActor != nullptr && IsValid(SourceCityActor))
	{
		return SourceCityActor;
	}

	if (UWorld* World = GetWorld())
	{
		return Cast<ASimCity2000CityActor>(UGameplayStatics::GetActorOfClass(World, ASimCity2000CityActor::StaticClass()));
	}

	return nullptr;
}

bool ASimCopterTrafficSystemActor::RebuildSpawnData()
{
	LastLoadError.Reset();
	LastCitySource.Reset();
	RoadNodes.Reset();
	PedestrianNodes.Reset();
	RoadNodeIndexByTile.Reset();
	VehicleAgents.Reset();
	PedestrianAgents.Reset();

	ActiveCityToWorldTransform = FTransform::Identity;
	ActiveOriginalGameRootPath = ResolveOriginalGameRoot();
	ActiveTileSize = TileSize;

	float EffectiveTerrainHeightScale = bUseOriginalTerrainHeightScale ? TileSize * 0.5f : TerrainHeightScale;
	FString CityPath = ResolveCityPath();

	if (const ASimCity2000CityActor* CityActor = ResolveSourceCityActor())
	{
		CityPath = CityActor->GetResolvedCityPath();
		ActiveTileSize = CityActor->GetTileSize();
		EffectiveTerrainHeightScale = CityActor->GetEffectiveTerrainHeightScale();
		ActiveCityToWorldTransform = CityActor->GetActorTransform();

		const FString CityOriginalRoot = CityActor->GetResolvedOriginalGameRoot();
		if (!CityOriginalRoot.IsEmpty())
		{
			ActiveOriginalGameRootPath = CityOriginalRoot;
		}

		LastCitySource = FString::Printf(TEXT("City actor '%s'"), *CityActor->GetName());
	}
	else
	{
		LastCitySource = TEXT("Traffic actor fallback settings");
	}

	FSimCity2000City City;
	FString Error;
	if (CityPath.IsEmpty() || !FSimCity2000Reader::LoadCityFromFile(CityPath, City, Error))
	{
		LastLoadError = CityPath.IsEmpty() ? TEXT("Traffic system city path is empty.") : Error;
		UE_LOG(LogSimCopterTrafficSystem, Warning, TEXT("%s"), *LastLoadError);
		return false;
	}

	const float HalfMapSize = FSimCity2000City::MapSize * ActiveTileSize * 0.5f;

	TMap<FIntPoint, int32> PedestrianIndexByTile;

	for (int32 FileY = 0; FileY < FSimCity2000City::MapSize; ++FileY)
	{
		for (int32 FileX = 0; FileX < FSimCity2000City::MapSize; ++FileX)
		{
			const FSimCity2000Tile& Tile = City.Tiles[FileY * FSimCity2000City::MapSize + FileX];
			const float LocalX = GetWorldTileCenterCoordinate(static_cast<float>(FileX), ActiveTileSize, HalfMapSize);
			const float LocalY = -GetWorldTileCenterCoordinate(static_cast<float>(FileY), ActiveTileSize, HalfMapSize);
			const float LocalZ = GetTerrainTileCenterZ(City, FileX, FileY, EffectiveTerrainHeightScale);

			if (!Tile.bWater && IsOriginalTrafficRoadTile(Tile.Building))
			{
				const FVector2D CenterlineOffset = GetRoadCenterlineLocalOffset(Tile.Building, ActiveTileSize);
				const int32 NodeIndex = RoadNodes.Num();
				FSimCopterGroundRouteNode& Node = RoadNodes.AddDefaulted_GetRef();
				Node.FileX = FileX;
				Node.FileY = FileY;
				Node.BuildingId = Tile.Building;
				Node.LocalLocation = FVector(LocalX + CenterlineOffset.X, LocalY + CenterlineOffset.Y, LocalZ + 10.0f);
				Node.Location = ActiveCityToWorldTransform.TransformPosition(Node.LocalLocation);
				RoadNodeIndexByTile.Add(FIntPoint(FileX, FileY), NodeIndex);
			}

			if (!Tile.bWater && IsPedestrianRoadTile(Tile.Building))
			{
				const FVector2D Sidewalk = GetRoadSidewalkLocalOffset(City, FileX, FileY, ActiveTileSize);
				const int32 NodeIndex = PedestrianNodes.Num();
				FSimCopterGroundRouteNode& Node = PedestrianNodes.AddDefaulted_GetRef();
				Node.FileX = FileX;
				Node.FileY = FileY;
				Node.BuildingId = Tile.Building;
				Node.LocalLocation = FVector(LocalX + Sidewalk.X, LocalY + Sidewalk.Y, LocalZ + 10.0f);
				Node.Location = ActiveCityToWorldTransform.TransformPosition(Node.LocalLocation);
				PedestrianIndexByTile.Add(FIntPoint(FileX, FileY), NodeIndex);
			}
		}
	}

	const int32 NeighborOffsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
	for (FSimCopterGroundRouteNode& Node : RoadNodes)
	{
		for (const int32* Offset : NeighborOffsets)
		{
			const int32 DeltaX = Offset[0];
			const int32 DeltaY = Offset[1];
			const int32 NeighborIndex = FindNodeByTile(RoadNodeIndexByTile, Node.FileX + DeltaX, Node.FileY + DeltaY);
			if (NeighborIndex != INDEX_NONE && CanRoadTilesConnect(Node.BuildingId, RoadNodes[NeighborIndex].BuildingId, DeltaX, DeltaY))
			{
				Node.Neighbors.Add(NeighborIndex);
			}
		}
	}

	for (FSimCopterGroundRouteNode& Node : PedestrianNodes)
	{
		for (const int32* Offset : NeighborOffsets)
		{
			const int32 DeltaX = Offset[0];
			const int32 DeltaY = Offset[1];
			const int32 NeighborIndex = FindNodeByTile(PedestrianIndexByTile, Node.FileX + DeltaX, Node.FileY + DeltaY);
			if (NeighborIndex != INDEX_NONE && CanRoadTilesConnect(Node.BuildingId, PedestrianNodes[NeighborIndex].BuildingId, DeltaX, DeltaY))
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
		TEXT("Built SimCopter traffic spawn data from '%s' via %s: roadNodes=%d pedestrianNodes=%d maxVehicles=%d maxPedestrians=%d spawnRadius=%.0f tileSize=%.1f."),
		*CityPath,
		*LastCitySource,
		RoadNodeCount,
		PedestrianNodeCount,
		MaxVehicleAgents,
		MaxPedestrianAgents,
		SpawnRadiusCm,
		ActiveTileSize);

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

void ASimCopterTrafficSystemActor::UpdateTrafficInteractions()
{
	for (TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : VehicleAgents)
	{
		if (ASimCopterGroundAgent* Agent = AgentPtr.Get())
		{
			Agent->SetTrafficSpeedScale(1.0f);
		}
	}

	for (TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : PedestrianAgents)
	{
		if (ASimCopterGroundAgent* Agent = AgentPtr.Get())
		{
			Agent->SetTrafficSpeedScale(1.0f);
		}
	}

	ApplyVehicleFollowing();
	ResolveVehicleOverlaps();
	UpdatePedestrianAvoidance();
}

void ASimCopterTrafficSystemActor::ApplyVehicleFollowing()
{
	const float LookAhead = FMath::Max(VehicleFollowLookAheadCm, VehicleStopDistanceCm + 1.0f);
	const float SlowDistance = FMath::Max(VehicleSlowDistanceCm, VehicleStopDistanceCm + 1.0f);

	for (TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : VehicleAgents)
	{
		ASimCopterGroundAgent* Vehicle = AgentPtr.Get();
		if (Vehicle == nullptr)
		{
			continue;
		}

		const FVector VehicleLocation = Vehicle->GetActorLocation();
		const FVector Forward = GetAgentTravelDirection(*Vehicle);
		if (Forward.IsNearlyZero())
		{
			continue;
		}

		const FVector Right(-Forward.Y, Forward.X, 0.0f);
		float ClosestForwardDistance = TNumericLimits<float>::Max();

		for (TWeakObjectPtr<ASimCopterGroundAgent>& OtherPtr : VehicleAgents)
		{
			ASimCopterGroundAgent* Other = OtherPtr.Get();
			if (Other == nullptr || Other == Vehicle)
			{
				continue;
			}

			const FVector ToOther = Other->GetActorLocation() - VehicleLocation;
			const float ForwardDistance = FVector::DotProduct(FVector(ToOther.X, ToOther.Y, 0.0f), Forward);
			if (ForwardDistance <= 0.0f || ForwardDistance > LookAhead)
			{
				continue;
			}

			const float LateralDistance = FMath::Abs(FVector::DotProduct(FVector(ToOther.X, ToOther.Y, 0.0f), Right));
			const float SameLaneWidth = Vehicle->GetCollisionRadiusCm() + Other->GetCollisionRadiusCm() + ActiveTileSize * 0.16f;
			if (LateralDistance > SameLaneWidth)
			{
				continue;
			}

			ClosestForwardDistance = FMath::Min(ClosestForwardDistance, ForwardDistance);
		}

		if (ClosestForwardDistance == TNumericLimits<float>::Max())
		{
			continue;
		}

		float SpeedScale = 1.0f;
		if (ClosestForwardDistance <= VehicleStopDistanceCm)
		{
			SpeedScale = 0.0f;
		}
		else if (ClosestForwardDistance < SlowDistance)
		{
			const float Alpha = (ClosestForwardDistance - VehicleStopDistanceCm) / FMath::Max(1.0f, SlowDistance - VehicleStopDistanceCm);
			SpeedScale = FMath::Lerp(0.12f, 1.0f, FMath::Clamp(Alpha, 0.0f, 1.0f));
		}

		Vehicle->SetTrafficSpeedScale(SpeedScale);
	}
}

void ASimCopterTrafficSystemActor::ResolveVehicleOverlaps()
{
	constexpr int32 MaxSeparationPasses = 2;
	for (int32 PassIndex = 0; PassIndex < MaxSeparationPasses; ++PassIndex)
	{
		bool bResolvedAnyOverlap = false;
		for (int32 Index = 0; Index < VehicleAgents.Num(); ++Index)
		{
			ASimCopterGroundAgent* A = VehicleAgents[Index].Get();
			if (A == nullptr)
			{
				continue;
			}

			for (int32 OtherIndex = Index + 1; OtherIndex < VehicleAgents.Num(); ++OtherIndex)
			{
				ASimCopterGroundAgent* B = VehicleAgents[OtherIndex].Get();
				if (B == nullptr)
				{
					continue;
				}

				const FVector Delta = B->GetActorLocation() - A->GetActorLocation();
				const FVector FlatDelta(Delta.X, Delta.Y, 0.0f);
				const float DistanceSq = FlatDelta.SizeSquared();
				const float MinimumDistance = A->GetCollisionRadiusCm() + B->GetCollisionRadiusCm() + VehicleOverlapPaddingCm;
				if (DistanceSq >= FMath::Square(MinimumDistance))
				{
					continue;
				}

				FVector SeparationDirection = DistanceSq > KINDA_SMALL_NUMBER
					? FlatDelta / FMath::Sqrt(DistanceSq)
					: GetAgentTravelDirection(*A).GetSafeNormal();
				if (SeparationDirection.IsNearlyZero())
				{
					SeparationDirection = FVector::RightVector;
				}

				const float Distance = DistanceSq > KINDA_SMALL_NUMBER ? FMath::Sqrt(DistanceSq) : 0.0f;
				const float PushDistance = FMath::Max(0.0f, MinimumDistance - Distance);
				const FVector Push = SeparationDirection * (PushDistance * 0.5f + 0.5f);
				A->MoveByTrafficSeparation(-Push);
				B->MoveByTrafficSeparation(Push);

				if (PassIndex == 0)
				{
					const FVector Impulse = SeparationDirection * VehicleBumpImpulseCmPerSec;
					A->AddTrafficVelocityImpulse(-Impulse);
					B->AddTrafficVelocityImpulse(Impulse);
				}
				A->SetTrafficSpeedScale(0.25f);
				B->SetTrafficSpeedScale(0.25f);
				bResolvedAnyOverlap = true;
			}
		}

		if (!bResolvedAnyOverlap)
		{
			break;
		}
	}
}

void ASimCopterTrafficSystemActor::UpdatePedestrianAvoidance()
{
	const float LookAhead = FMath::Max(PedestrianCarLookAheadCm, 1.0f);

	for (TWeakObjectPtr<ASimCopterGroundAgent>& PedestrianPtr : PedestrianAgents)
	{
		ASimCopterGroundAgent* Pedestrian = PedestrianPtr.Get();
		if (Pedestrian == nullptr)
		{
			continue;
		}

		const FVector PedestrianLocation = Pedestrian->GetActorLocation();
		float BestTimeToImpact = TNumericLimits<float>::Max();
		FVector BestEscapeDirection = FVector::ZeroVector;

		for (TWeakObjectPtr<ASimCopterGroundAgent>& VehiclePtr : VehicleAgents)
		{
			ASimCopterGroundAgent* Vehicle = VehiclePtr.Get();
			if (Vehicle == nullptr)
			{
				continue;
			}

			const FVector VehicleLocation = Vehicle->GetActorLocation();
			const FVector VehicleDirection = GetAgentTravelDirection(*Vehicle);
			if (VehicleDirection.IsNearlyZero())
			{
				continue;
			}

			const FVector ToPedestrian = PedestrianLocation - VehicleLocation;
			const FVector FlatToPedestrian(ToPedestrian.X, ToPedestrian.Y, 0.0f);
			const float ForwardDistance = FVector::DotProduct(FlatToPedestrian, VehicleDirection);
			if (ForwardDistance <= 0.0f || ForwardDistance > LookAhead)
			{
				continue;
			}

			const FVector VehicleRight(-VehicleDirection.Y, VehicleDirection.X, 0.0f);
			const float SignedLateralDistance = FVector::DotProduct(FlatToPedestrian, VehicleRight);
			const float DangerRadius = Vehicle->GetCollisionRadiusCm() + Pedestrian->GetCollisionRadiusCm() + ActiveTileSize * 0.14f;
			if (FMath::Abs(SignedLateralDistance) > DangerRadius)
			{
				continue;
			}

			const float VehicleSpeed = FMath::Max(Vehicle->GetCurrentVelocityCmPerSec().Size2D(), VehicleSpeedCmPerSec * 0.35f);
			const float TimeToImpact = ForwardDistance / VehicleSpeed;
			if (TimeToImpact < BestTimeToImpact)
			{
				BestTimeToImpact = TimeToImpact;
				const float SideSign = SignedLateralDistance >= 0.0f ? 1.0f : -1.0f;
				BestEscapeDirection = VehicleRight * SideSign;
			}
		}

		if (!BestEscapeDirection.IsNearlyZero())
		{
			FVector EscapeTarget = PedestrianLocation + BestEscapeDirection.GetSafeNormal() * PedestrianRoadEscapeDistanceCm;
			TryFindPedestrianEscapeTarget(PedestrianLocation, BestEscapeDirection, EscapeTarget);
			Pedestrian->SetAvoidanceMoveTarget(EscapeTarget, PedestrianAvoidanceDurationSeconds, PedestrianAvoidanceSpeedMultiplier);
		}
	}
}

bool ASimCopterTrafficSystemActor::IsVehicleSpawnLocationClear(const FVector& SpawnLocation) const
{
	const float ClearanceCm = FMath::Max(VehicleStopDistanceCm, ActiveTileSize * 0.35f);
	const float ClearanceSq = FMath::Square(ClearanceCm);
	for (const TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : VehicleAgents)
	{
		const ASimCopterGroundAgent* Vehicle = AgentPtr.Get();
		if (Vehicle != nullptr && FVector::DistSquared2D(Vehicle->GetActorLocation(), SpawnLocation) < ClearanceSq)
		{
			return false;
		}
	}

	return true;
}

bool ASimCopterTrafficSystemActor::TryFindPedestrianEscapeTarget(
	const FVector& PedestrianLocation,
	const FVector& EscapeDirection,
	FVector& OutTarget) const
{
	const FVector FlatEscapeDirection = GetFlatSafeNormal(EscapeDirection);
	if (FlatEscapeDirection.IsNearlyZero() || PedestrianNodes.Num() == 0)
	{
		return false;
	}

	const FVector DesiredEscapeTarget = PedestrianLocation + FlatEscapeDirection * PedestrianRoadEscapeDistanceCm;
	const float MaxSearchDistanceSq = FMath::Square(FMath::Max(ActiveTileSize * 1.5f, PedestrianRoadEscapeDistanceCm * 2.5f));
	const float MinSideProgressCm = FMath::Max(24.0f, PedestrianRoadEscapeDistanceCm * 0.25f);
	int32 BestIndex = INDEX_NONE;
	float BestScore = TNumericLimits<float>::Max();

	for (int32 Index = 0; Index < PedestrianNodes.Num(); ++Index)
	{
		const FVector ToNode = PedestrianNodes[Index].Location - PedestrianLocation;
		const FVector FlatToNode(ToNode.X, ToNode.Y, 0.0f);
		const float DistanceSq = FlatToNode.SizeSquared();
		if (DistanceSq > MaxSearchDistanceSq)
		{
			continue;
		}

		const float SideProgress = FVector::DotProduct(FlatToNode, FlatEscapeDirection);
		if (SideProgress < MinSideProgressCm)
		{
			continue;
		}

		const float Score = FVector::DistSquared2D(PedestrianNodes[Index].Location, DesiredEscapeTarget);
		if (Score < BestScore)
		{
			BestScore = Score;
			BestIndex = Index;
		}
	}

	if (!PedestrianNodes.IsValidIndex(BestIndex))
	{
		return false;
	}

	OutTarget = PedestrianNodes[BestIndex].Location;
	return true;
}

void ASimCopterTrafficSystemActor::AssignNextTarget(ASimCopterGroundAgent& Agent, const TArray<FSimCopterGroundRouteNode>& Nodes)
{
	if (Nodes.Num() == 0)
	{
		Agent.ClearMoveTarget();
		return;
	}

	// The agent has reached (or lost) its target node; that node is the start of the next hop.
	int32 FromIndex = Agent.GetRouteTargetNode();
	if (!Nodes.IsValidIndex(FromIndex))
	{
		FromIndex = FindNearestNodeIndex(Nodes, Agent.GetActorLocation());
		if (FromIndex == INDEX_NONE)
		{
			Agent.ClearMoveTarget();
			return;
		}
		Agent.SetRouteState(FromIndex, INDEX_NONE);
	}

	const int32 NextIndex = ChooseNextRouteNode(Nodes, FromIndex, Agent.GetRoutePrevNode(), RandomStream);
	if (NextIndex == INDEX_NONE)
	{
		// Isolated node with no neighbours: re-seed from the nearest node so the agent isn't stuck.
		const int32 ReseedIndex = FindNearestNodeIndex(Nodes, Agent.GetActorLocation());
		Agent.SetRouteState(ReseedIndex, INDEX_NONE);
		if (Nodes.IsValidIndex(ReseedIndex))
		{
			Agent.SetMoveTarget(Nodes[ReseedIndex].Location);
		}
		else
		{
			Agent.ClearMoveTarget();
		}
		return;
	}

	// Drive to the chosen adjacent graph node; record where we came from for the next hop.
	Agent.SetRouteState(NextIndex, FromIndex);
	Agent.SetMoveTarget(MakeRoutePointLocation(
		Nodes,
		NextIndex,
		FromIndex,
		INDEX_NONE,
		Agent.GetAgentKind() == ESimCopterGroundAgentKind::Vehicle));
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

	int32 NodeIndex = INDEX_NONE;
	int32 InitialNextIndex = INDEX_NONE;
	FVector SpawnBaseLocation = FVector::ZeroVector;
	for (int32 Attempt = 0; Attempt < 18; ++Attempt)
	{
		const int32 CandidateNodeIndex = ChooseNodeNearFocus(Nodes, FocusLocation);
		if (CandidateNodeIndex == INDEX_NONE)
		{
			return false;
		}

		const int32 CandidateNextIndex = ChooseNextRouteNode(Nodes, CandidateNodeIndex, INDEX_NONE, RandomStream);
		const FVector CandidateSpawnBaseLocation = MakeRoutePointLocation(Nodes, CandidateNodeIndex, INDEX_NONE, CandidateNextIndex, bVehicle);
		if (!bVehicle || IsVehicleSpawnLocationClear(CandidateSpawnBaseLocation))
		{
			NodeIndex = CandidateNodeIndex;
			InitialNextIndex = CandidateNextIndex;
			SpawnBaseLocation = CandidateSpawnBaseLocation;
			break;
		}
	}

	if (NodeIndex == INDEX_NONE)
	{
		return false;
	}

	const FSimCopterGroundRouteNode& Node = Nodes[NodeIndex];
	FRotator SpawnRotation = FRotator::ZeroRotator;
	if (Nodes.IsValidIndex(InitialNextIndex))
	{
		const FVector Target = MakeRoutePointLocation(Nodes, InitialNextIndex, NodeIndex, INDEX_NONE, bVehicle);
		SpawnRotation.Yaw = (Target - SpawnBaseLocation).Rotation().Yaw;
	}
	else if (Node.Neighbors.Num() > 0)
	{
		const FVector Target = Nodes[Node.Neighbors[RandomStream.RandRange(0, Node.Neighbors.Num() - 1)]].Location;
		SpawnRotation.Yaw = (Target - SpawnBaseLocation).Rotation().Yaw;
	}
	else
	{
		SpawnRotation.Yaw = RandomStream.FRandRange(0.0f, 360.0f);
	}

	const FVector SpawnLocation = SpawnBaseLocation + FVector::UpVector * (bVehicle ? 100.0f : 92.0f);
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
		ActiveOriginalGameRootPath.IsEmpty() ? ResolveOriginalGameRoot() : ActiveOriginalGameRootPath,
		bVehicle ? VehicleSpeedCmPerSec : PedestrianSpeedCmPerSec);

	if (bRequireOriginalPopulationMeshes && !Agent->IsUsingOriginalMesh())
	{
		UE_LOG(LogSimCopterTrafficSystem, Warning, TEXT("Discarding %s population agent because original mesh '%s' could not be loaded."),
			bVehicle ? TEXT("vehicle") : TEXT("pedestrian"),
			*MeshName);
		Agent->Destroy();
		return false;
	}

	// Drop the agent onto the road/ground immediately so it is grounded on its very first frame
	// instead of briefly hovering at the spawner's estimated terrain height.
	Agent->SnapToGroundImmediate();

	// Seed the route at the spawn node, then pick the first hop along the graph.
	if (Nodes.IsValidIndex(InitialNextIndex))
	{
		Agent->SetRouteState(InitialNextIndex, NodeIndex);
		Agent->SetMoveTarget(MakeRoutePointLocation(Nodes, InitialNextIndex, NodeIndex, INDEX_NONE, bVehicle));
	}
	else
	{
		Agent->SetRouteState(NodeIndex, INDEX_NONE);
		AssignNextTarget(*Agent, Nodes);
	}

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

FVector ASimCopterTrafficSystemActor::MakeRoutePointLocation(
	const TArray<FSimCopterGroundRouteNode>& Nodes,
	int32 PointIndex,
	int32 PreviousIndex,
	int32 NextIndex,
	bool bVehicle) const
{
	if (!Nodes.IsValidIndex(PointIndex))
	{
		return GetActorLocation();
	}

	const FSimCopterGroundRouteNode& Point = Nodes[PointIndex];
	if (!bVehicle || VehicleLaneOffsetTileFraction <= 0.0f)
	{
		return Point.Location;
	}

	FVector Direction = FVector::ZeroVector;
	if (Nodes.IsValidIndex(NextIndex))
	{
		Direction = Nodes[NextIndex].LocalLocation - Point.LocalLocation;
	}
	else if (Nodes.IsValidIndex(PreviousIndex))
	{
		Direction = Point.LocalLocation - Nodes[PreviousIndex].LocalLocation;
	}

	Direction.Z = 0.0f;
	Direction = Direction.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return Point.Location;
	}

	const FVector Right = FVector::CrossProduct(FVector::UpVector, Direction).GetSafeNormal();
	const FVector LaneLocalLocation = Point.LocalLocation + Right * (ActiveTileSize * VehicleLaneOffsetTileFraction);
	return ActiveCityToWorldTransform.TransformPosition(LaneLocalLocation);
}
