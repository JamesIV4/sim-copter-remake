// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterTrafficSystemActor.h"

#include "City/SimCity2000CityActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "Formats/SimCopterPeopleCityRules.h"
#include "Formats/SimCity2000Reader.h"
#include "GameFramework/PlayerController.h"
#include "Ground/SimCopterGroundAgent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "UObject/ConstructorHelpers.h"

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

// Drivable tiles for cars: the EXACT road family the original's RoadGraph dump uses
// (FUN_00495700 range checks): surface roads 0x1d-0x2b, sloped roads/power crossings
// 0x3f-0x46, road BRIDGES 0x49-0x59, and onramps/highways 0x5d-0x6b. Bridge decks sit on
// water tiles, so callers must not reject these ids for bWater.
bool IsOriginalTrafficRoadTile(uint8 BuildingId)
{
	return (BuildingId >= 0x1D && BuildingId <= 0x2B) ||
		(BuildingId >= 0x3F && BuildingId <= 0x46) ||
		(BuildingId >= 0x49 && BuildingId <= 0x59) ||
		(BuildingId >= 0x5D && BuildingId <= 0x6B);
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

bool IsAdjacentRoadCornerMask(int32 Mask)
{
	return CountRoadOpenings(Mask) == 2 && !IsOppositeRoadOpeningPair(Mask);
}

bool IsAdjacentRoadCornerTile(uint8 BuildingId)
{
	return IsAdjacentRoadCornerMask(GetRoadOpeningMask(BuildingId));
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
	if (IsAdjacentRoadCornerMask(Mask))
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
	if (IsAdjacentRoadCornerMask(Mask))
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

struct FPeopleSceneFootprint
{
	int32 OriginX = 0;
	int32 OriginY = 0;
	int32 Size = 1;
};

bool TryResolvePeopleSceneFootprint(const FSimCity2000City& City, int32 FileX, int32 FileY, FPeopleSceneFootprint& OutFootprint)
{
	const FSimCity2000Tile& Tile = City.Tiles[FileY * FSimCity2000City::MapSize + FileX];
	const int32 FootprintSize = FSimCopterPeopleCityRules::GetFootprintSizeForBuildingId(Tile.Building);
	if (Tile.Building >= 0x70)
	{
		const uint8 ZoneHigh = Tile.Zone & 0xF0;
		if (ZoneHigh != 0xF0 && (Tile.Zone & 0x80) == 0)
		{
			return false;
		}
	}

	if (FootprintSize <= 0 ||
		FileX + FootprintSize > FSimCity2000City::MapSize ||
		FileY + FootprintSize > FSimCity2000City::MapSize)
	{
		return false;
	}

	for (int32 DeltaY = 0; DeltaY < FootprintSize; ++DeltaY)
	{
		for (int32 DeltaX = 0; DeltaX < FootprintSize; ++DeltaX)
		{
			const FSimCity2000Tile& Candidate = City.Tiles[(FileY + DeltaY) * FSimCity2000City::MapSize + FileX + DeltaX];
			if (Candidate.Building != Tile.Building)
			{
				return false;
			}
		}
	}

	OutFootprint.OriginX = FileX;
	OutFootprint.OriginY = FileY;
	OutFootprint.Size = FootprintSize;
	return true;
}

FVector MakePeopleSpawnOffsetWorld(
	const FTransform& CityToWorldTransform,
	float TileSize,
	int32 FootprintSize,
	int32 PlacementMode,
	uint16& PeopleRandomState)
{
	const FSimCopterPeopleLocalOffset LocalOffset =
		FSimCopterPeopleCityRules::ChooseSpawnLocalOffset(FootprintSize, PlacementMode, PeopleRandomState);
	const float OriginalUnitToWorld = TileSize / 64.0f;
	return CityToWorldTransform.TransformVector(FVector(
		float(LocalOffset.OriginalX) * OriginalUnitToWorld,
		float(LocalOffset.OriginalY) * OriginalUnitToWorld,
		0.0f));
}

FVector2D GetPeopleFacingLocalDirection(int32 Facing)
{
	constexpr float Diagonal = 0.70710678118f;
	static const FVector2D OriginalDirectionByIndex[8] = {
		FVector2D(0.0f, -1.0f),
		FVector2D(Diagonal, -Diagonal),
		FVector2D(1.0f, 0.0f),
		FVector2D(Diagonal, Diagonal),
		FVector2D(0.0f, 1.0f),
		FVector2D(-Diagonal, Diagonal),
		FVector2D(-1.0f, 0.0f),
		FVector2D(-Diagonal, -Diagonal)};

	return OriginalDirectionByIndex[(Facing + 2) & 7];
}

int32 GetOriginalFacingFromLocalDelta(const FVector2D& Delta)
{
	const float AbsX = FMath::Abs(Delta.X);
	const float AbsY = FMath::Abs(Delta.Y);
	if (AbsY < AbsX * 0.5f)
	{
		return Delta.X < 0.0f ? 6 : 2;
	}
	if (AbsX < AbsY * 0.5f)
	{
		return Delta.Y < 0.0f ? 0 : 4;
	}
	if (Delta.X < 0.0f)
	{
		return Delta.Y < 0.0f ? 7 : 5;
	}
	return Delta.Y < 0.0f ? 1 : 3;
}

bool TryGetRoadOpeningLocalDirectionToNode(
	const TArray<FSimCopterGroundRouteNode>& Nodes,
	int32 PointIndex,
	int32 OtherIndex,
	FVector& OutDirection)
{
	if (!Nodes.IsValidIndex(PointIndex) || !Nodes.IsValidIndex(OtherIndex))
	{
		return false;
	}

	const FSimCopterGroundRouteNode& Point = Nodes[PointIndex];
	const FSimCopterGroundRouteNode& Other = Nodes[OtherIndex];
	const int32 DeltaX = Other.FileX - Point.FileX;
	const int32 DeltaY = Other.FileY - Point.FileY;
	if (FMath::Abs(DeltaX) + FMath::Abs(DeltaY) != 1)
	{
		return false;
	}

	const int32 Opening = GetRoadOpeningForOffset(DeltaX, DeltaY);
	if ((GetRoadOpeningMask(Point.BuildingId) & Opening) == 0)
	{
		return false;
	}

	const FVector2D OpeningDirection = GetRoadOpeningLocalDirection(Opening);
	OutDirection = FVector(OpeningDirection.X, OpeningDirection.Y, 0.0f);
	return !OutDirection.IsNearlyZero();
}

bool TryGetCornerRoadTravelDirectionLocal(
	const TArray<FSimCopterGroundRouteNode>& Nodes,
	int32 PointIndex,
	int32 PreviousIndex,
	int32 NextIndex,
	FVector& OutDirection)
{
	if (!Nodes.IsValidIndex(PointIndex) || !IsAdjacentRoadCornerTile(Nodes[PointIndex].BuildingId))
	{
		return false;
	}

	FVector PreviousOpeningDirection = FVector::ZeroVector;
	FVector NextOpeningDirection = FVector::ZeroVector;
	if (!TryGetRoadOpeningLocalDirectionToNode(Nodes, PointIndex, PreviousIndex, PreviousOpeningDirection) ||
		!TryGetRoadOpeningLocalDirectionToNode(Nodes, PointIndex, NextIndex, NextOpeningDirection))
	{
		return false;
	}

	OutDirection = (NextOpeningDirection - PreviousOpeningDirection).GetSafeNormal();
	return !OutDirection.IsNearlyZero();
}

FVector GetRightHandLaneSideLocalDirection(const FVector& TravelDirection)
{
	FVector FlatTravelDirection(TravelDirection.X, TravelDirection.Y, 0.0f);
	FlatTravelDirection = FlatTravelDirection.GetSafeNormal();
	if (FlatTravelDirection.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}

	return FVector::CrossProduct(FVector::UpVector, FlatTravelDirection).GetSafeNormal();
}

bool IsRightHandTurnLocal(const FVector& IncomingDirection, const FVector& OutgoingDirection)
{
	const FVector Incoming = FVector(IncomingDirection.X, IncomingDirection.Y, 0.0f).GetSafeNormal();
	const FVector Outgoing = FVector(OutgoingDirection.X, OutgoingDirection.Y, 0.0f).GetSafeNormal();
	if (Incoming.IsNearlyZero() || Outgoing.IsNearlyZero())
	{
		return false;
	}

	return FVector::CrossProduct(Incoming, Outgoing).Z < -0.05f;
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
	FRandomStream& RandomStream,
	bool bPreferStraight = true)
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

	// Modernized mode prefers continuing straight (~30% turns). The decoded original picks
	// uniformly among the exits (rand % candidates in the per-tile transition choosers).
	if (bPreferStraight && Nodes.IsValidIndex(PrevIndex))
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

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* FarInstanceMesh = CubeMeshFinder.Succeeded() ? CubeMeshFinder.Object : nullptr;
	const auto MakeFarInstances = [this, FarInstanceMesh](const TCHAR* Name) {
		UInstancedStaticMeshComponent* Instances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Instances->SetupAttachment(RootComponent);
		if (FarInstanceMesh != nullptr)
		{
			Instances->SetStaticMesh(FarInstanceMesh);
		}
		Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Instances->SetCastShadow(false);
		Instances->SetMobility(EComponentMobility::Movable);
		return Instances;
	};
	FarPedestrianInstances = MakeFarInstances(TEXT("FarPedestrianInstances"));
	FarVehicleInstances = MakeFarInstances(TEXT("FarVehicleInstances"));

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
	UpdateTrafficInteractions(DeltaSeconds);
	UpdateWholeMapPopulation(DeltaSeconds);
}

bool ASimCopterTrafficSystemActor::TryStartTrafficJam(int32 EventId, int32& OutTileX, int32& OutTileY)
{
	TArray<ASimCopterGroundAgent*> EligibleVehicles;
	for (TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : VehicleAgents)
	{
		if (ASimCopterGroundAgent* Vehicle = AgentPtr.Get())
		{
			if (FSimCopterVehicleTrafficState* State = VehicleTrafficStates.Find(TObjectKey<ASimCopterGroundAgent>(Vehicle)))
			{
				if (!State->bMissionJammed)
				{
					EligibleVehicles.Add(Vehicle);
				}
			}
		}
	}

	if (EligibleVehicles.Num() == 0)
	{
		return false;
	}

	int32 RandomIndex = FMath::RandRange(0, EligibleVehicles.Num() - 1);
	ASimCopterGroundAgent* ChosenVehicle = EligibleVehicles[RandomIndex];
	
	if (FSimCopterVehicleTrafficState* State = VehicleTrafficStates.Find(TObjectKey<ASimCopterGroundAgent>(ChosenVehicle)))
	{
		State->bMissionJammed = true;
		State->MissionEventId = EventId;
		
		FVector Location = ChosenVehicle->GetActorLocation();
		FVector Relative = ActiveCityToWorldTransform.InverseTransformPositionNoScale(Location);
		OutTileX = FMath::Clamp(FMath::FloorToInt(Relative.X / ActiveTileSize), 0, 127);
		OutTileY = FMath::Clamp(FMath::FloorToInt(Relative.Y / ActiveTileSize), 0, 127);
		return true;
	}
	return false;
}

void ASimCopterTrafficSystemActor::EndTrafficJam(int32 EventId)
{
	for (auto& Pair : VehicleTrafficStates)
	{
		if (Pair.Value.bMissionJammed && Pair.Value.MissionEventId == EventId)
		{
			Pair.Value.bMissionJammed = false;
			Pair.Value.MissionEventId = INDEX_NONE;
		}
	}
}

bool ASimCopterTrafficSystemActor::TryStartCarFire(int32 EventId, int32& OutTileX, int32& OutTileY)
{
	TArray<ASimCopterGroundAgent*> EligibleVehicles;
	for (TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : VehicleAgents)
	{
		if (ASimCopterGroundAgent* Vehicle = AgentPtr.Get())
		{
			EligibleVehicles.Add(Vehicle);
		}
	}

	if (EligibleVehicles.Num() == 0) return false;
	
	ASimCopterGroundAgent* ChosenVehicle = EligibleVehicles[FMath::RandRange(0, EligibleVehicles.Num() - 1)];
	
	if (FSimCopterVehicleTrafficState* State = VehicleTrafficStates.Find(TObjectKey<ASimCopterGroundAgent>(ChosenVehicle)))
	{
		State->bMissionJammed = true;
		State->MissionEventId = EventId;
	}
	
	FVector Location = ChosenVehicle->GetActorLocation();
	FVector Relative = ActiveCityToWorldTransform.InverseTransformPositionNoScale(Location);
	OutTileX = FMath::Clamp(FMath::FloorToInt(Relative.X / ActiveTileSize), 0, 127);
	OutTileY = FMath::Clamp(FMath::FloorToInt(Relative.Y / ActiveTileSize), 0, 127);
	
	return true;
}

bool ASimCopterTrafficSystemActor::TrySpawnMissionPerson(int32 PersonState, int32 BehaviorClass, int32 TileX, int32 TileY, int32 EventId)
{
	if (GroundAgentClass == nullptr) return false;
	
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	
	FVector Relative(TileX * ActiveTileSize + (ActiveTileSize * 0.5f), TileY * ActiveTileSize + (ActiveTileSize * 0.5f), 1000.0f);
	FVector WorldLoc = ActiveCityToWorldTransform.TransformPositionNoScale(Relative);
	
	if (ASimCopterGroundAgent* Person = GetWorld()->SpawnActor<ASimCopterGroundAgent>(GroundAgentClass, WorldLoc, FRotator::ZeroRotator, SpawnParams))
	{
		Person->InitialPersonState = PersonState;
		if (BehaviorClass != -1) Person->InitialBehaviorClass = BehaviorClass;
		Person->MissionEventId = EventId;
		PedestrianAgents.Add(Person);
		return true;
	}
	return false;
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
	PeopleTileClasses.Reset();
	VehicleAgents.Reset();
	PedestrianAgents.Reset();
	VehicleTrafficStates.Reset();

	ActiveCityToWorldTransform = FTransform::Identity;
	ActiveOriginalGameRootPath = ResolveOriginalGameRoot();
	ActiveTileSize = TileSize;
	PeopleRandomState = uint16(RandomSeed & 0xffff);
	if (PeopleRandomState == 0)
	{
		PeopleRandomState = 1;
	}

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

	PeopleTileClasses.SetNum(FSimCity2000City::TileCount);
	TileCenterWorldZ.SetNum(FSimCity2000City::TileCount);

	for (int32 FileY = 0; FileY < FSimCity2000City::MapSize; ++FileY)
	{
		for (int32 FileX = 0; FileX < FSimCity2000City::MapSize; ++FileX)
		{
			const FSimCity2000Tile& Tile = City.Tiles[FileY * FSimCity2000City::MapSize + FileX];
			const int32 TileIndex = FileY * FSimCity2000City::MapSize + FileX;
			const int32 PeopleTileClass = FSimCopterPeopleCityRules::GetTileClassForBuildingId(Tile.Building);
			PeopleTileClasses[TileIndex] = uint8(PeopleTileClass);
			const float LocalX = GetWorldTileCenterCoordinate(static_cast<float>(FileX), ActiveTileSize, HalfMapSize);
			const float LocalY = -GetWorldTileCenterCoordinate(static_cast<float>(FileY), ActiveTileSize, HalfMapSize);
			const float LocalZ = GetTerrainTileCenterZ(City, FileX, FileY, EffectiveTerrainHeightScale);
			TileCenterWorldZ[TileIndex] = ActiveCityToWorldTransform.TransformPosition(FVector(LocalX, LocalY, LocalZ)).Z;

			// No bWater rejection here: bridge decks (XBLD 0x49-0x59) legitimately sit on water
			// tiles, and excluding them is exactly why cars could never cross a bridge.
			if (IsOriginalTrafficRoadTile(Tile.Building))
			{
				const FVector2D CenterlineOffset = GetRoadCenterlineLocalOffset(Tile.Building, ActiveTileSize);
				const int32 NodeIndex = RoadNodes.Num();
				FSimCopterGroundRouteNode& Node = RoadNodes.AddDefaulted_GetRef();
				Node.FileX = FileX;
				Node.FileY = FileY;
				Node.BuildingId = Tile.Building;
				Node.PeopleTileClass = PeopleTileClass;
				Node.LocalLocation = FVector(LocalX + CenterlineOffset.X, LocalY + CenterlineOffset.Y, LocalZ + 10.0f);
				Node.Location = ActiveCityToWorldTransform.TransformPosition(Node.LocalLocation);
				RoadNodeIndexByTile.Add(FIntPoint(FileX, FileY), NodeIndex);
			}

			if (!Tile.bWater && FSimCopterPeopleCityRules::IsAmbientPedestrianTileClass(PeopleTileClass))
			{
				FPeopleSceneFootprint SceneFootprint;
				if (!TryResolvePeopleSceneFootprint(City, FileX, FileY, SceneFootprint))
				{
					continue;
				}

				const float SceneCenterX = GetWorldTileCenterCoordinate(
					static_cast<float>(SceneFootprint.OriginX) + (static_cast<float>(SceneFootprint.Size) - 1.0f) * 0.5f,
					ActiveTileSize,
					HalfMapSize);
				const float SceneCenterY = -GetWorldTileCenterCoordinate(
					static_cast<float>(SceneFootprint.OriginY) + (static_cast<float>(SceneFootprint.Size) - 1.0f) * 0.5f,
					ActiveTileSize,
					HalfMapSize);
				const FSimCopterPeopleSpawnPlacement Placement = FSimCopterPeopleCityRules::GetSpawnPlacementForTileClass(PeopleTileClass);
				FSimCopterGroundRouteNode& Node = PedestrianNodes.AddDefaulted_GetRef();
				Node.FileX = SceneFootprint.OriginX;
				Node.FileY = SceneFootprint.OriginY;
				Node.BuildingId = Tile.Building;
				Node.PeopleTileClass = PeopleTileClass;
				Node.PeopleFootprintSize = SceneFootprint.Size;
				Node.PeoplePlacementMode = Placement.PlacementMode;
				Node.LocalLocation = FVector(SceneCenterX, SceneCenterY, LocalZ + 10.0f);
				Node.Location = ActiveCityToWorldTransform.TransformPosition(Node.LocalLocation);
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

	// Pedestrians are moved by the people.df VM using facing + tile-class checks. The nodes above
	// are spawn candidates only; building a road-style graph here recreates the old sidewalk
	// fallback and makes ops 13/14 meaningless.

	RoadNodeCount = RoadNodes.Num();
	PedestrianNodeCount = PedestrianNodes.Num();

	BuildWholeMapPopulation();

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

void ASimCopterTrafficSystemActor::BuildWholeMapPopulation()
{
	WholeMapPedestrianRecords.Reset();
	WholeMapVehicleRecords.Reset();
	WholeMapSimAccumulatorSeconds = 0.0f;
	if (FarPedestrianInstances != nullptr)
	{
		FarPedestrianInstances->ClearInstances();
	}
	if (FarVehicleInstances != nullptr)
	{
		FarVehicleInstances->ClearInstances();
	}
	WholeMapPedestrianRecordCount = 0;
	WholeMapVehicleRecordCount = 0;

	if (!bSimulateWholeMap || FarPedestrianInstances == nullptr || FarVehicleInstances == nullptr)
	{
		return;
	}

	const int32 SectorTiles = FMath::Clamp(WholeMapSectorTiles, 4, FSimCity2000City::MapSize);
	const int32 SectorsPerSide = FMath::DivideAndRoundUp(FSimCity2000City::MapSize, SectorTiles);
	const int32 SectorCount = SectorsPerSide * SectorsPerSide;
	const auto SectorOfTile = [SectorTiles, SectorsPerSide](int32 FileX, int32 FileY) {
		return (FileY / SectorTiles) * SectorsPerSide + (FileX / SectorTiles);
	};

	// Bucket spawn candidates per sector. Pedestrian scene nodes cover FootprintSize^2 tiles;
	// road nodes cover one tile each.
	TArray<TArray<int32>> SectorPedNodes;
	TArray<TArray<int32>> SectorRoadNodes;
	TArray<int32> SectorPedTileWeight;
	SectorPedNodes.SetNum(SectorCount);
	SectorRoadNodes.SetNum(SectorCount);
	SectorPedTileWeight.SetNumZeroed(SectorCount);

	for (int32 Index = 0; Index < PedestrianNodes.Num(); ++Index)
	{
		const FSimCopterGroundRouteNode& Node = PedestrianNodes[Index];
		const int32 Sector = SectorOfTile(Node.FileX, Node.FileY);
		SectorPedNodes[Sector].Add(Index);
		SectorPedTileWeight[Sector] += FMath::Square(FMath::Max(1, Node.PeopleFootprintSize));
	}
	for (int32 Index = 0; Index < RoadNodes.Num(); ++Index)
	{
		const FSimCopterGroundRouteNode& Node = RoadNodes[Index];
		SectorRoadNodes[SectorOfTile(Node.FileX, Node.FileY)].Add(Index);
	}

	// Budgets derive from each sector's actual content: a fully built-up sector gets the
	// original's per-camera-area ambient population (figure.twk Max random ambient = 55),
	// emptier sectors proportionally less. This is the anti-clumping guarantee.
	const float TilesPerSector = float(SectorTiles * SectorTiles);
	for (int32 Sector = 0; Sector < SectorCount; ++Sector)
	{
		if (WholeMapPedestrianRecords.Num() >= WholeMapMaxPedestrians)
		{
			break;
		}
		const TArray<int32>& Candidates = SectorPedNodes[Sector];
		if (Candidates.Num() == 0)
		{
			continue;
		}
		const float Fullness = FMath::Clamp(float(SectorPedTileWeight[Sector]) / TilesPerSector, 0.0f, 1.0f);
		int32 Budget = FMath::RoundToInt(float(WholeMapPedestriansPerFullSector) * Fullness);
		Budget = FMath::Min(Budget, WholeMapMaxPedestrians - WholeMapPedestrianRecords.Num());
		for (int32 Spawn = 0; Spawn < Budget; ++Spawn)
		{
			const FSimCopterGroundRouteNode& Node = PedestrianNodes[Candidates[RandomStream.RandRange(0, Candidates.Num() - 1)]];
			const int32 BehaviorClass =
				FSimCopterPeopleCityRules::ChooseAmbientBehaviorClassForTileClass(Node.PeopleTileClass, PeopleRandomState);
			if (BehaviorClass == INDEX_NONE)
			{
				continue; // original spawn attempt failure (five rejected class rolls)
			}
			FSimCopterWholeMapRecord& Record = WholeMapPedestrianRecords.AddDefaulted_GetRef();
			Record.Location = Node.Location + MakePeopleSpawnOffsetWorld(
				ActiveCityToWorldTransform, ActiveTileSize, Node.PeopleFootprintSize, Node.PeoplePlacementMode, PeopleRandomState);
			Record.Facing = RandomStream.RandRange(0, 7);
			Record.BehaviorClass = BehaviorClass;
		}
	}

	for (int32 Sector = 0; Sector < SectorCount; ++Sector)
	{
		if (WholeMapVehicleRecords.Num() >= WholeMapMaxVehicles)
		{
			break;
		}
		const TArray<int32>& Candidates = SectorRoadNodes[Sector];
		if (Candidates.Num() == 0)
		{
			continue;
		}
		int32 Budget = FMath::RoundToInt(float(Candidates.Num()) * WholeMapVehiclesPerRoadTile);
		Budget = FMath::Min(Budget, WholeMapMaxVehicles - WholeMapVehicleRecords.Num());
		for (int32 Spawn = 0; Spawn < Budget; ++Spawn)
		{
			const int32 NodeIndex = Candidates[RandomStream.RandRange(0, Candidates.Num() - 1)];
			const int32 NextIndex = ChooseNextRouteNode(RoadNodes, NodeIndex, INDEX_NONE, RandomStream, TrafficAiMode == ESimCopterTrafficAiMode::Modernized);
			if (NextIndex == INDEX_NONE)
			{
				continue;
			}
			FSimCopterWholeMapRecord& Record = WholeMapVehicleRecords.AddDefaulted_GetRef();
			Record.Location = RoadNodes[NodeIndex].Location;
			Record.RouteNodeIndex = NodeIndex;
			Record.RouteNextIndex = NextIndex;
		}
	}

	// One instance per record, index-aligned; transforms are batch-updated by the far tick.
	TArray<FTransform> InitialTransforms;
	InitialTransforms.Reserve(WholeMapPedestrianRecords.Num());
	for (const FSimCopterWholeMapRecord& Record : WholeMapPedestrianRecords)
	{
		InitialTransforms.Add(FTransform(FRotator::ZeroRotator, Record.Location + FVector(0, 0, 85.0f), FVector(0.42f, 0.42f, 1.7f)));
	}
	FarPedestrianInstances->AddInstances(InitialTransforms, false, true);

	InitialTransforms.Reset(WholeMapVehicleRecords.Num());
	for (const FSimCopterWholeMapRecord& Record : WholeMapVehicleRecords)
	{
		InitialTransforms.Add(FTransform(FRotator::ZeroRotator, Record.Location + FVector(0, 0, 52.0f), FVector(3.6f, 1.7f, 1.05f)));
	}
	FarVehicleInstances->AddInstances(InitialTransforms, false, true);

	WholeMapPedestrianRecordCount = WholeMapPedestrianRecords.Num();
	WholeMapVehicleRecordCount = WholeMapVehicleRecords.Num();
	UE_LOG(LogSimCopterTrafficSystem, Display,
		TEXT("Whole-map population: %d pedestrians, %d vehicles across %d sectors."),
		WholeMapPedestrianRecordCount, WholeMapVehicleRecordCount, SectorCount);
}

void ASimCopterTrafficSystemActor::UpdateWholeMapPopulation(float DeltaSeconds)
{
	if (!bSimulateWholeMap ||
		(WholeMapPedestrianRecords.Num() == 0 && WholeMapVehicleRecords.Num() == 0) ||
		FarPedestrianInstances == nullptr || FarVehicleInstances == nullptr)
	{
		return;
	}

	WholeMapSimAccumulatorSeconds += DeltaSeconds;
	if (WholeMapSimAccumulatorSeconds < WholeMapSimTickIntervalSeconds)
	{
		return;
	}
	const float StepSeconds = FMath::Min(WholeMapSimAccumulatorSeconds, 1.0f);
	WholeMapSimAccumulatorSeconds = 0.0f;

	const FVector FocusLocation = GetPopulationFocusLocation();
	const float HideRadiusSq = FMath::Square(WholeMapHideRadiusCm);

	// Movement-allowed classes for the ambient state: DAT_0058d750 default row (13,11,10,12,7).
	const auto IsFarWalkable = [](int32 TileClass) {
		return TileClass == 13 || TileClass == 11 || TileClass == 10 || TileClass == 12 || TileClass == 7;
	};

	TArray<FTransform> Transforms;
	Transforms.Reserve(WholeMapPedestrianRecords.Num());
	const float PedStepCm = PedestrianSpeedCmPerSec * 0.8f * StepSeconds;
	for (FSimCopterWholeMapRecord& Record : WholeMapPedestrianRecords)
	{
		// Occasional wander turn, mirroring the shipped 'Random Turn' programs at a distance.
		if (RandomStream.FRand() < 0.25f * StepSeconds)
		{
			Record.Facing = RandomStream.RandRange(0, 7);
		}

		FVector TargetLocation = FVector::ZeroVector;
		int32 TargetTileClass = INDEX_NONE;
		if (TryGetPeopleFacingStepTarget(Record.Location, Record.Facing, PedStepCm, TargetLocation, TargetTileClass) &&
			IsFarWalkable(TargetTileClass))
		{
			int32 FileX = INDEX_NONE;
			int32 FileY = INDEX_NONE;
			if (TryGetPeopleTileCoordinateAtWorldLocation(TargetLocation, FileX, FileY))
			{
				TargetLocation.Z = TileCenterWorldZ[FileY * FSimCity2000City::MapSize + FileX];
			}
			Record.Location = TargetLocation;
		}
		else
		{
			Record.Facing = (Record.Facing + 1) & 7; // blocked: rotate like the original mover
		}

		const bool bNearCamera = FVector::DistSquared2D(Record.Location, FocusLocation) < HideRadiusSq;
		Transforms.Add(FTransform(
			FRotator::ZeroRotator,
			Record.Location + FVector(0, 0, 85.0f),
			bNearCamera ? FVector::ZeroVector : FVector(0.42f, 0.42f, 1.7f)));
	}
	FarPedestrianInstances->BatchUpdateInstancesTransforms(0, Transforms, true, true, true);

	Transforms.Reset(WholeMapVehicleRecords.Num());
	const float CarStepCm = VehicleSpeedCmPerSec * 0.9f * StepSeconds;
	for (FSimCopterWholeMapRecord& Record : WholeMapVehicleRecords)
	{
		FRotator Rotation = FRotator::ZeroRotator;
		if (RoadNodes.IsValidIndex(Record.RouteNextIndex))
		{
			const FVector Target = RoadNodes[Record.RouteNextIndex].Location;
			const FVector ToTarget = Target - Record.Location;
			const float Distance = ToTarget.Size2D();
			if (Distance <= CarStepCm)
			{
				Record.Location = Target;
				const int32 PreviousIndex = Record.RouteNextIndex;
				Record.RouteNextIndex = ChooseNextRouteNode(RoadNodes, Record.RouteNextIndex, Record.RouteNodeIndex, RandomStream, TrafficAiMode == ESimCopterTrafficAiMode::Modernized);
				Record.RouteNodeIndex = PreviousIndex;
			}
			else
			{
				const FVector Direction = ToTarget / FMath::Max(Distance, 1.0f);
				Record.Location += FVector(Direction.X, Direction.Y, 0.0f) * CarStepCm;
				Record.Location.Z = FMath::Lerp(Record.Location.Z, Target.Z, FMath::Clamp(CarStepCm / Distance, 0.0f, 1.0f));
			}
			Rotation.Yaw = FMath::RadiansToDegrees(FMath::Atan2(ToTarget.Y, ToTarget.X));
		}

		const bool bNearCamera = FVector::DistSquared2D(Record.Location, FocusLocation) < HideRadiusSq;
		Transforms.Add(FTransform(
			Rotation,
			Record.Location + FVector(0, 0, 52.0f),
			bNearCamera ? FVector::ZeroVector : FVector(3.6f, 1.7f, 1.05f)));
	}
	FarVehicleInstances->BatchUpdateInstancesTransforms(0, Transforms, true, true, true);
}

bool ASimCopterTrafficSystemActor::TryGetPeopleTileCoordinateAtWorldLocation(
	const FVector& WorldLocation,
	int32& OutFileX,
	int32& OutFileY) const
{
	OutFileX = INDEX_NONE;
	OutFileY = INDEX_NONE;
	if (PeopleTileClasses.Num() != FSimCity2000City::TileCount || ActiveTileSize <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector LocalLocation = ActiveCityToWorldTransform.InverseTransformPosition(WorldLocation);
	const float HalfMapSize = FSimCity2000City::MapSize * ActiveTileSize * 0.5f;
	const int32 FileX = FMath::FloorToInt((LocalLocation.X + HalfMapSize) / ActiveTileSize);
	const int32 FileY = FMath::FloorToInt((HalfMapSize - LocalLocation.Y) / ActiveTileSize);
	if (FileX < 0 || FileX >= FSimCity2000City::MapSize || FileY < 0 || FileY >= FSimCity2000City::MapSize)
	{
		return false;
	}

	OutFileX = FileX;
	OutFileY = FileY;
	return true;
}

int32 ASimCopterTrafficSystemActor::GetPeopleTileClassAtWorldLocation(const FVector& WorldLocation) const
{
	int32 FileX = INDEX_NONE;
	int32 FileY = INDEX_NONE;
	if (!TryGetPeopleTileCoordinateAtWorldLocation(WorldLocation, FileX, FileY))
	{
		return INDEX_NONE;
	}

	return int32(PeopleTileClasses[FileY * FSimCity2000City::MapSize + FileX]);
}

bool ASimCopterTrafficSystemActor::TryGetTerrainWorldZAtWorldLocation(
	const FVector& WorldLocation,
	float& OutTerrainWorldZ) const
{
	OutTerrainWorldZ = 0.0f;
	int32 FileX = INDEX_NONE;
	int32 FileY = INDEX_NONE;
	if (TileCenterWorldZ.Num() != FSimCity2000City::TileCount ||
		!TryGetPeopleTileCoordinateAtWorldLocation(WorldLocation, FileX, FileY))
	{
		return false;
	}

	OutTerrainWorldZ = TileCenterWorldZ[FileY * FSimCity2000City::MapSize + FileX];
	return true;
}

int32 ASimCopterTrafficSystemActor::GetPeopleStoredFacingFromWorldLocations(
	const FVector& FromWorldLocation,
	const FVector& ToWorldLocation) const
{
	const FVector FromLocal = ActiveCityToWorldTransform.InverseTransformPosition(FromWorldLocation);
	const FVector ToLocal = ActiveCityToWorldTransform.InverseTransformPosition(ToWorldLocation);
	const int32 OriginalFacing = GetOriginalFacingFromLocalDelta(FVector2D(ToLocal.X - FromLocal.X, ToLocal.Y - FromLocal.Y));
	return (OriginalFacing - 2) & 7;
}

bool ASimCopterTrafficSystemActor::TryGetPeopleFacingStepTarget(
	const FVector& FromWorldLocation,
	int32 Facing,
	float StepDistanceCm,
	FVector& OutWorldLocation,
	int32& OutTileClass) const
{
	OutWorldLocation = FVector::ZeroVector;
	OutTileClass = INDEX_NONE;
	if (PeopleTileClasses.Num() != FSimCity2000City::TileCount || StepDistanceCm <= 0.0f)
	{
		return false;
	}

	const FVector2D LocalDirection = GetPeopleFacingLocalDirection(Facing);
	if (LocalDirection.IsNearlyZero())
	{
		return false;
	}

	const FVector WorldDelta = ActiveCityToWorldTransform.TransformVector(FVector(
		LocalDirection.X * StepDistanceCm,
		LocalDirection.Y * StepDistanceCm,
		0.0f));
	OutWorldLocation = FromWorldLocation + FVector(WorldDelta.X, WorldDelta.Y, 0.0f);
	OutTileClass = GetPeopleTileClassAtWorldLocation(OutWorldLocation);
	return OutTileClass != INDEX_NONE;
}

float ASimCopterTrafficSystemActor::GetPeopleWorldCmPerOriginalUnit() const
{
	return ActiveTileSize / 64.0f;
}

FVector ASimCopterTrafficSystemActor::GetPeopleFacingWorldDirection(int32 Facing) const
{
	const FVector2D LocalDirection = GetPeopleFacingLocalDirection(Facing);
	const FVector WorldDelta = ActiveCityToWorldTransform.TransformVector(
		FVector(LocalDirection.X, LocalDirection.Y, 0.0f));
	return FVector(WorldDelta.X, WorldDelta.Y, 0.0f).GetSafeNormal();
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

void ASimCopterTrafficSystemActor::UpdateTrafficInteractions(float DeltaSeconds)
{
	SyncVehicleTrafficStates(DeltaSeconds);

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

	if (TrafficAiMode == ESimCopterTrafficAiMode::Original)
	{
		// Decoded original behavior: no traffic lights and no blockage recovery. Cars queue
		// behind whatever blocks the road (including the player), so jams form and clear the
		// way the original's traffic-jam events expect.
		ApplyVehicleFollowing(VehicleFollowLookAheadCm, VehicleStopDistanceCm, VehicleSlowDistanceCm, false, DeltaSeconds);
		ApplyPlayerRoadBlocking();
		ApplyIntersectionApproachSlowdown(DeltaSeconds);
		ResolveVehicleOverlaps();
		ApplyVehicleLaneGuidance(DeltaSeconds);
		UpdatePedestrianAvoidance();
		return;
	}

	if (TrafficFlowMode == ESimCopterTrafficFlowMode::Normal)
	{
		ApplyTrafficLights(DeltaSeconds);
		ApplyVehicleFollowing(NormalVehicleFollowLookAheadCm, NormalVehicleStopDistanceCm, NormalVehicleSlowDistanceCm, true, DeltaSeconds);
	}
	else
	{
		ApplyVehicleFollowing(VehicleFollowLookAheadCm, VehicleStopDistanceCm, VehicleSlowDistanceCm, false, DeltaSeconds);
	}

	ApplyIntersectionApproachSlowdown(DeltaSeconds);
	ResolveVehicleOverlaps();
	if (TrafficFlowMode == ESimCopterTrafficFlowMode::Normal)
	{
		UpdateVehicleBlockageRecovery();
	}
	ApplyVehicleLaneGuidance(DeltaSeconds);
	UpdatePedestrianAvoidance();
}

void ASimCopterTrafficSystemActor::ApplyPlayerRoadBlocking()
{
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (PlayerPawn == nullptr || PlayerRoadBlockLookAheadCm <= 0.0f)
	{
		return;
	}

	// Only a grounded player blocks traffic; a helicopter in flight above the road does not.
	const FVector PlayerLocation = PlayerPawn->GetActorLocation();

	for (TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : VehicleAgents)
	{
		ASimCopterGroundAgent* Agent = AgentPtr.Get();
		if (Agent == nullptr)
		{
			continue;
		}
		const FVector AgentLocation = Agent->GetActorLocation();
		if (FMath::Abs(PlayerLocation.Z - AgentLocation.Z) > 600.0f)
		{
			continue;
		}
		const FVector Forward = Agent->GetActorForwardVector().GetSafeNormal2D();
		const FVector ToPlayer = PlayerLocation - AgentLocation;
		const float Ahead = FVector::DotProduct(FVector(ToPlayer.X, ToPlayer.Y, 0.0f), Forward);
		if (Ahead < 0.0f || Ahead > PlayerRoadBlockLookAheadCm)
		{
			continue;
		}
		const float Lateral = FMath::Abs(FVector::DotProduct(
			FVector(ToPlayer.X, ToPlayer.Y, 0.0f),
			FVector(-Forward.Y, Forward.X, 0.0f)));
		if (Lateral < PlayerRoadBlockLaneWidthCm * 0.5f)
		{
			Agent->SetTrafficSpeedScale(0.0f);
		}
	}
}

void ASimCopterTrafficSystemActor::SyncVehicleTrafficStates(float DeltaSeconds)
{
	TSet<TObjectKey<ASimCopterGroundAgent>> LiveVehicleKeys;
	for (TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : VehicleAgents)
	{
		ASimCopterGroundAgent* Vehicle = AgentPtr.Get();
		if (Vehicle == nullptr)
		{
			continue;
		}

		const TObjectKey<ASimCopterGroundAgent> VehicleKey(Vehicle);
		LiveVehicleKeys.Add(VehicleKey);
		FSimCopterVehicleTrafficState& State = VehicleTrafficStates.FindOrAdd(VehicleKey);
		State.bInTrafficLightLine = false;
		State.RecentCollisionSeconds = FMath::Max(0.0f, State.RecentCollisionSeconds - DeltaSeconds);
		State.RecoveryCooldownSeconds = FMath::Max(0.0f, State.RecoveryCooldownSeconds - DeltaSeconds);
		State.TrafficLightLineGraceSeconds = FMath::Max(0.0f, State.TrafficLightLineGraceSeconds - DeltaSeconds);
		const float PreviousRecoveryBypassSeconds = State.RecoveryBypassSeconds;
		State.RecoveryBypassSeconds = FMath::Max(0.0f, State.RecoveryBypassSeconds - DeltaSeconds);
		State.RecoveryRejoinSeconds = FMath::Max(0.0f, State.RecoveryRejoinSeconds - DeltaSeconds);
		if (PreviousRecoveryBypassSeconds > 0.0f && State.RecoveryBypassSeconds <= 0.0f)
		{
			State.RecoveryRejoinSeconds = FMath::Max(State.RecoveryRejoinSeconds, VehicleRecoveryRejoinDurationSeconds);
		}
		State.IntersectionCommitSeconds = FMath::Max(0.0f, State.IntersectionCommitSeconds - DeltaSeconds);

		const FVector CurrentLocation = Vehicle->GetActorLocation();
		if (!State.bInitialized)
		{
			State.LastLocation = CurrentLocation;
			State.bInitialized = true;
			continue;
		}

		const float ActualSpeedCmPerSec = DeltaSeconds > KINDA_SMALL_NUMBER
			? FVector::Dist2D(CurrentLocation, State.LastLocation) / DeltaSeconds
			: Vehicle->GetCurrentVelocityCmPerSec().Size2D();
		if (Vehicle->HasMoveTarget() && ActualSpeedCmPerSec <= VehicleBlockedSpeedThresholdCmPerSec)
		{
			State.BlockedSeconds += DeltaSeconds;
		}
		else
		{
			State.BlockedSeconds = FMath::Max(0.0f, State.BlockedSeconds - DeltaSeconds * 2.0f);
		}

		State.LastLocation = CurrentLocation;
	}

	for (auto StateIt = VehicleTrafficStates.CreateIterator(); StateIt; ++StateIt)
	{
		if (!LiveVehicleKeys.Contains(StateIt.Key()))
		{
			StateIt.RemoveCurrent();
		}
	}
}

void ASimCopterTrafficSystemActor::ApplyTrafficLights(float DeltaSeconds)
{
	const float StopDistance = FMath::Max(1.0f, TrafficLightStopDistanceCm);
	const float SlowDistance = FMath::Max(TrafficLightSlowDistanceCm, StopDistance + 1.0f);
	const float QueueSlotSpacing = FMath::Max(TrafficLightQueueSlotSpacingCm, NormalVehicleMinimumFollowDistanceCm);
	const float QueueAwarenessDistance = FMath::Max(FMath::Max(SlowDistance, TrafficLightQueueSlowDistanceCm), StopDistance + QueueSlotSpacing * 8.0f);
	const float QueueSlotSlowDistance = FMath::Max(SlowDistance, QueueSlotSpacing * 1.5f);
	const float GuidanceDuration = FMath::Max(VehicleLaneGuidanceDurationSeconds, DeltaSeconds * 2.0f);

	struct FTrafficLightQueueVehicle
	{
		ASimCopterGroundAgent* Vehicle = nullptr;
		FVector StopReferenceLocation = FVector::ZeroVector;
		FVector ApproachDirection = FVector::ZeroVector;
		float DistanceToIntersectionCm = 0.0f;
	};

	TMap<FIntPoint, TArray<FTrafficLightQueueVehicle>> QueuesByApproach;

	for (TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : VehicleAgents)
	{
		ASimCopterGroundAgent* Vehicle = AgentPtr.Get();
		if (Vehicle == nullptr || !Vehicle->HasMoveTarget())
		{
			continue;
		}

		const int32 TargetNodeIndex = Vehicle->GetRouteTargetNode();
		const int32 PreviousNodeIndex = Vehicle->GetRoutePrevNode();
		if (!RoadNodes.IsValidIndex(PreviousNodeIndex))
		{
			continue;
		}

		if (IsTrafficLightIntersectionNode(PreviousNodeIndex))
		{
			MarkVehicleCommittedToIntersection(*Vehicle);
			continue;
		}

		if (!IsTrafficLightIntersectionNode(TargetNodeIndex))
		{
			continue;
		}

		const FVector StopReferenceLocation = MakeRoutePointLocation(RoadNodes, TargetNodeIndex, PreviousNodeIndex, INDEX_NONE, true);
		const FVector ApproachStartLocation = MakeRoutePointLocation(RoadNodes, PreviousNodeIndex, INDEX_NONE, TargetNodeIndex, true);
		FVector ApproachDirection = GetFlatSafeNormal(StopReferenceLocation - ApproachStartLocation);
		if (ApproachDirection.IsNearlyZero())
		{
			ApproachDirection = GetFlatSafeNormal(StopReferenceLocation - Vehicle->GetActorLocation());
		}
		if (ApproachDirection.IsNearlyZero())
		{
			continue;
		}

		const FVector ToIntersection = StopReferenceLocation - Vehicle->GetActorLocation();
		const float DistanceToIntersectionCm = FVector::DotProduct(FVector(ToIntersection.X, ToIntersection.Y, 0.0f), ApproachDirection);
		if (DistanceToIntersectionCm > QueueAwarenessDistance)
		{
			continue;
		}

		const bool bGreenLight = IsTrafficLightGreenForApproach(TargetNodeIndex, PreviousNodeIndex);
		const bool bPastStopLine = DistanceToIntersectionCm <= 90.0f;
		if (bGreenLight || bPastStopLine)
		{
			MarkVehicleCommittedToIntersection(*Vehicle);
			continue;
		}

		FTrafficLightQueueVehicle QueueVehicle;
		QueueVehicle.Vehicle = Vehicle;
		QueueVehicle.StopReferenceLocation = StopReferenceLocation;
		QueueVehicle.ApproachDirection = ApproachDirection;
		QueueVehicle.DistanceToIntersectionCm = DistanceToIntersectionCm;
		QueuesByApproach.FindOrAdd(FIntPoint(TargetNodeIndex, PreviousNodeIndex)).Add(QueueVehicle);
	}

	for (TPair<FIntPoint, TArray<FTrafficLightQueueVehicle>>& QueuePair : QueuesByApproach)
	{
		TArray<FTrafficLightQueueVehicle>& Queue = QueuePair.Value;
		Queue.Sort([](const FTrafficLightQueueVehicle& A, const FTrafficLightQueueVehicle& B)
		{
			return A.DistanceToIntersectionCm < B.DistanceToIntersectionCm;
		});

		for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
		{
			FTrafficLightQueueVehicle& QueueVehicle = Queue[QueueIndex];
			if (QueueVehicle.Vehicle == nullptr)
			{
				continue;
			}

			const float SlotDistanceFromIntersection = StopDistance + QueueSlotSpacing * QueueIndex;
			const FVector QueueSlotLocation = QueueVehicle.StopReferenceLocation - QueueVehicle.ApproachDirection * SlotDistanceFromIntersection;
			MarkVehicleInTrafficLightLine(*QueueVehicle.Vehicle);
			QueueVehicle.Vehicle->SetGuidanceMoveTarget(QueueSlotLocation, GuidanceDuration);

			const FVector ToSlot = QueueSlotLocation - QueueVehicle.Vehicle->GetActorLocation();
			const float DistanceToSlotCm = FVector::DotProduct(FVector(ToSlot.X, ToSlot.Y, 0.0f), QueueVehicle.ApproachDirection);
			float SpeedScale = 1.0f;
			if (DistanceToSlotCm <= 0.0f)
			{
				SpeedScale = 0.0f;
			}
			else if (DistanceToSlotCm < QueueSlotSlowDistance)
			{
				const float Alpha = DistanceToSlotCm / FMath::Max(1.0f, QueueSlotSlowDistance);
				SpeedScale = FMath::Pow(FMath::Clamp(Alpha, 0.0f, 1.0f), 1.45f);
			}

			QueueVehicle.Vehicle->ApplyTrafficBrake(SpeedScale, DeltaSeconds, NormalTrafficBrakeRate);
		}
	}
}

void ASimCopterTrafficSystemActor::ApplyVehicleFollowing(
	float LookAheadCm,
	float StopDistanceCm,
	float SlowDistanceCm,
	bool bUseNormalBraking,
	float DeltaSeconds)
{
	const float StopDistance = bUseNormalBraking
		? FMath::Max(1.0f, StopDistanceCm)
		: StopDistanceCm;
	const float SlowDistance = bUseNormalBraking
		? FMath::Max(SlowDistanceCm, StopDistance + 1.0f)
		: FMath::Max(SlowDistanceCm, StopDistance + 1.0f);
	const float LookAhead = bUseNormalBraking
		? FMath::Max(FMath::Max(FMath::Max(LookAheadCm, SlowDistance), VehicleRecoveryRejoinSlowDistanceCm), TrafficLightQueueSlowDistanceCm)
		: FMath::Max(LookAheadCm, SlowDistance);

	for (TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : VehicleAgents)
	{
		ASimCopterGroundAgent* Vehicle = AgentPtr.Get();
		if (Vehicle == nullptr)
		{
			continue;
		}

		FSimCopterVehicleTrafficState* State = VehicleTrafficStates.Find(TObjectKey<ASimCopterGroundAgent>(Vehicle));
		if (State != nullptr && State->bMissionJammed)
		{
			Vehicle->SetTrafficSpeedScale(0.0f);
			continue;
		}

		if (bUseNormalBraking)
		{
			if (State != nullptr && (State->RecoveryBypassSeconds > 0.0f || State->IntersectionCommitSeconds > 0.0f))
			{
				continue;
			}
		}

		const FVector VehicleLocation = Vehicle->GetActorLocation();
		const FVector Forward = GetAgentTravelDirection(*Vehicle);
		if (Forward.IsNearlyZero())
		{
			continue;
		}

		const FVector Right(-Forward.Y, Forward.X, 0.0f);
		float ClosestForwardDistance = TNumericLimits<float>::Max();
		ASimCopterGroundAgent* ClosestVehicleAhead = nullptr;
		const bool bVehicleRejoining = State != nullptr && State->RecoveryRejoinSeconds > 0.0f;

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

			const FSimCopterVehicleTrafficState* OtherState = bUseNormalBraking
				? VehicleTrafficStates.Find(TObjectKey<ASimCopterGroundAgent>(Other))
				: nullptr;
			const bool bOtherRecovering = OtherState != nullptr &&
				(OtherState->RecoveryBypassSeconds > 0.0f || OtherState->RecoveryRejoinSeconds > 0.0f);
			const bool bOtherTrafficLightQueue = OtherState != nullptr &&
				(OtherState->bInTrafficLightLine || OtherState->TrafficLightLineGraceSeconds > 0.0f) &&
				OtherState->IntersectionCommitSeconds <= 0.0f;
			const float LateralDistance = FMath::Abs(FVector::DotProduct(FVector(ToOther.X, ToOther.Y, 0.0f), Right));
			const float BaseLaneWidth = Vehicle->GetCollisionRadiusCm() + Other->GetCollisionRadiusCm() + ActiveTileSize * 0.16f;
			float SameLaneWidth = BaseLaneWidth;
			if (bVehicleRejoining || bOtherRecovering)
			{
				SameLaneWidth = FMath::Max(SameLaneWidth, VehicleRecoveryRejoinLaneWidthCm);
			}
			if (bOtherTrafficLightQueue)
			{
				SameLaneWidth = FMath::Max(SameLaneWidth, TrafficLightQueueLaneWidthCm);
			}
			if (LateralDistance > SameLaneWidth)
			{
				continue;
			}

			if (ForwardDistance < ClosestForwardDistance)
			{
				ClosestForwardDistance = ForwardDistance;
				ClosestVehicleAhead = Other;
			}
		}

		if (ClosestForwardDistance == TNumericLimits<float>::Max())
		{
			continue;
		}

		const FSimCopterVehicleTrafficState* AheadState = TrafficFlowMode == ESimCopterTrafficFlowMode::Normal && ClosestVehicleAhead != nullptr
			? VehicleTrafficStates.Find(TObjectKey<ASimCopterGroundAgent>(ClosestVehicleAhead))
			: nullptr;
		const bool bFollowingTrafficLightQueue = AheadState != nullptr &&
			(AheadState->bInTrafficLightLine || AheadState->TrafficLightLineGraceSeconds > 0.0f) &&
			AheadState->IntersectionCommitSeconds <= 0.0f;
		const bool bStateRejoining = State != nullptr && State->RecoveryRejoinSeconds > 0.0f;
		const bool bAheadRecovering = AheadState != nullptr &&
			(AheadState->RecoveryBypassSeconds > 0.0f || AheadState->RecoveryRejoinSeconds > 0.0f);
		const bool bFollowingRecoveryRejoin = bUseNormalBraking && (bStateRejoining || bAheadRecovering);
		const float NormalMinimumFollowDistance = bUseNormalBraking
			? FMath::Max(StopDistance, NormalVehicleMinimumFollowDistanceCm)
			: StopDistance;
		float EffectiveStopDistance = bFollowingTrafficLightQueue
			? FMath::Max(FMath::Max(StopDistance, TrafficLightQueueStopDistanceCm), NormalMinimumFollowDistance)
			: NormalMinimumFollowDistance;
		float EffectiveSlowDistance = bFollowingTrafficLightQueue
			? FMath::Max(FMath::Max(SlowDistance, TrafficLightQueueSlowDistanceCm), EffectiveStopDistance + 1.0f)
			: SlowDistance;
		if (bFollowingRecoveryRejoin)
		{
			EffectiveStopDistance = FMath::Max(EffectiveStopDistance, VehicleRecoveryRejoinStopDistanceCm);
			EffectiveSlowDistance = FMath::Max(FMath::Max(EffectiveSlowDistance, VehicleRecoveryRejoinSlowDistanceCm), EffectiveStopDistance + 1.0f);
		}

		float SpeedScale = 1.0f;
		if (ClosestForwardDistance <= EffectiveStopDistance)
		{
			SpeedScale = bFollowingTrafficLightQueue ? 0.0f : 0.18f;
		}
		else if (ClosestForwardDistance < EffectiveSlowDistance)
		{
			const float Alpha = (ClosestForwardDistance - EffectiveStopDistance) / FMath::Max(1.0f, EffectiveSlowDistance - EffectiveStopDistance);
			const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
			SpeedScale = bUseNormalBraking
				? FMath::Max(bFollowingTrafficLightQueue ? 0.0f : 0.22f, FMath::Pow(ClampedAlpha, bFollowingTrafficLightQueue ? 2.0f : 0.72f))
				: FMath::Lerp(0.12f, 1.0f, ClampedAlpha);
		}

		if (bUseNormalBraking)
		{
			const float EffectiveBrakeRate = bFollowingTrafficLightQueue
				? FMath::Max(NormalTrafficBrakeRate, TrafficLightQueueBrakeRate)
				: NormalTrafficBrakeRate;
			Vehicle->ApplyTrafficBrake(SpeedScale, DeltaSeconds, EffectiveBrakeRate);
		}
		else
		{
			Vehicle->LimitTrafficSpeedScale(SpeedScale);
		}
		if (TrafficFlowMode == ESimCopterTrafficFlowMode::Normal && ClosestVehicleAhead != nullptr)
		{
			if (bFollowingTrafficLightQueue)
			{
				MarkVehicleInTrafficLightLine(*Vehicle);
			}
		}
	}
}

void ASimCopterTrafficSystemActor::ApplyIntersectionApproachSlowdown(float DeltaSeconds)
{
	const float SlowDistance = FMath::Max(VehicleIntersectionSlowDistanceCm, ActiveTileSize * 0.75f);
	if (SlowDistance <= 1.0f)
	{
		return;
	}

	for (TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : VehicleAgents)
	{
		ASimCopterGroundAgent* Vehicle = AgentPtr.Get();
		if (Vehicle == nullptr || !Vehicle->HasMoveTarget())
		{
			continue;
		}

		const FSimCopterVehicleTrafficState* State = VehicleTrafficStates.Find(TObjectKey<ASimCopterGroundAgent>(Vehicle));
		if (State != nullptr && State->RecoveryBypassSeconds > 0.0f)
		{
			continue;
		}

		const int32 TargetIndex = Vehicle->GetRouteTargetNode();
		const int32 PreviousIndex = Vehicle->GetRoutePrevNode();
		const int32 NextIndex = Vehicle->GetRoutePlannedNextNode();
		if (!RoadNodes.IsValidIndex(TargetIndex) || !RoadNodes.IsValidIndex(PreviousIndex))
		{
			continue;
		}

		const bool bIntersectionNode = IsTrafficLightIntersectionNode(TargetIndex);
		bool bUpcomingTurn = IsAdjacentRoadCornerTile(RoadNodes[TargetIndex].BuildingId);
		if (RoadNodes.IsValidIndex(NextIndex))
		{
			FVector IncomingDirection = RoadNodes[TargetIndex].LocalLocation - RoadNodes[PreviousIndex].LocalLocation;
			FVector OutgoingDirection = RoadNodes[NextIndex].LocalLocation - RoadNodes[TargetIndex].LocalLocation;
			IncomingDirection.Z = 0.0f;
			OutgoingDirection.Z = 0.0f;
			IncomingDirection = IncomingDirection.GetSafeNormal();
			OutgoingDirection = OutgoingDirection.GetSafeNormal();
			if (!IncomingDirection.IsNearlyZero() && !OutgoingDirection.IsNearlyZero())
			{
				bUpcomingTurn = bUpcomingTurn || FVector::DotProduct(IncomingDirection, OutgoingDirection) < 0.94f;
			}
		}

		if (!bIntersectionNode && !bUpcomingTurn)
		{
			continue;
		}

		const FVector ApproachStartLocation = MakeRoutePointLocation(RoadNodes, PreviousIndex, INDEX_NONE, TargetIndex, true);
		const FVector IntersectionLocation = MakeRoutePointLocation(RoadNodes, TargetIndex, PreviousIndex, NextIndex, true);
		FVector ApproachDirection = GetFlatSafeNormal(IntersectionLocation - ApproachStartLocation);
		if (ApproachDirection.IsNearlyZero())
		{
			ApproachDirection = GetFlatSafeNormal(IntersectionLocation - Vehicle->GetActorLocation());
		}
		if (ApproachDirection.IsNearlyZero())
		{
			continue;
		}

		const FVector ToIntersection = IntersectionLocation - Vehicle->GetActorLocation();
		const float DistanceToIntersectionCm = FVector::DotProduct(FVector(ToIntersection.X, ToIntersection.Y, 0.0f), ApproachDirection);
		if (DistanceToIntersectionCm <= 0.0f || DistanceToIntersectionCm > SlowDistance)
		{
			continue;
		}

		const float MinimumSpeedScale = bUpcomingTurn
			? VehicleIntersectionTurnSpeedScale
			: VehicleIntersectionCruiseSpeedScale;
		const float Alpha = FMath::Clamp(DistanceToIntersectionCm / SlowDistance, 0.0f, 1.0f);
		const float SpeedScale = FMath::Lerp(
			FMath::Clamp(MinimumSpeedScale, 0.0f, 1.0f),
			1.0f,
			FMath::Pow(Alpha, 0.65f));
		Vehicle->ApplyTrafficBrake(SpeedScale, DeltaSeconds, VehicleIntersectionBrakeRate);
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
				MarkVehicleCollision(*A);
				MarkVehicleCollision(*B);

				if (PassIndex == 0)
				{
					const FVector Impulse = SeparationDirection * VehicleBumpImpulseCmPerSec;
					A->AddTrafficVelocityImpulse(-Impulse);
					B->AddTrafficVelocityImpulse(Impulse);
				}
				A->LimitTrafficSpeedScale(0.25f);
				B->LimitTrafficSpeedScale(0.25f);
				bResolvedAnyOverlap = true;
			}
		}

		if (!bResolvedAnyOverlap)
		{
			break;
		}
	}
}

void ASimCopterTrafficSystemActor::UpdateVehicleBlockageRecovery()
{
	for (TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : VehicleAgents)
	{
		ASimCopterGroundAgent* Vehicle = AgentPtr.Get();
		if (Vehicle == nullptr)
		{
			continue;
		}

		FSimCopterVehicleTrafficState* State = VehicleTrafficStates.Find(TObjectKey<ASimCopterGroundAgent>(Vehicle));
		const bool bCommittedToIntersection = State != nullptr && State->IntersectionCommitSeconds > 0.0f;
		if (State == nullptr ||
			State->RecoveryCooldownSeconds > 0.0f ||
			(!bCommittedToIntersection && (State->bInTrafficLightLine || State->TrafficLightLineGraceSeconds > 0.0f)))
		{
			continue;
		}

		const bool bCommittedBlocked = bCommittedToIntersection &&
			State->BlockedSeconds >= FMath::Min(0.45f, VehicleBlockedSecondsBeforeRecovery * 0.5f);
		const bool bBlockedAfterCollision = State->RecentCollisionSeconds > 0.0f &&
			State->BlockedSeconds >= VehicleBlockedSecondsBeforeRecovery;
		const bool bLongBlockedInJam = State->BlockedSeconds >= VehicleBlockedSecondsBeforeRecovery * 2.0f;
		if (!bCommittedBlocked && !bBlockedAfterCollision && !bLongBlockedInJam)
		{
			continue;
		}

		TryStartVehicleRecovery(*Vehicle, *State);
	}
}

void ASimCopterTrafficSystemActor::ApplyVehicleLaneGuidance(float DeltaSeconds)
{
	for (TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : VehicleAgents)
	{
		ASimCopterGroundAgent* Vehicle = AgentPtr.Get();
		if (Vehicle == nullptr || !Vehicle->HasMoveTarget())
		{
			continue;
		}

		const FSimCopterVehicleTrafficState* State = VehicleTrafficStates.Find(TObjectKey<ASimCopterGroundAgent>(Vehicle));
		if (State != nullptr &&
			(State->bInTrafficLightLine || State->TrafficLightLineGraceSeconds > 0.0f) &&
			State->IntersectionCommitSeconds <= 0.0f)
		{
			continue;
		}

		FVector GuidanceTarget = FVector::ZeroVector;
		float DistanceFromLane = 0.0f;
		bool bTraversingDiagonalRoad = false;
		if (!TryMakeVehicleLaneGuidanceTarget(*Vehicle, GuidanceTarget, DistanceFromLane, bTraversingDiagonalRoad))
		{
			continue;
		}

		const float OffLaneDistance = FMath::Max(VehicleRoadContainmentDistanceCm, ActiveTileSize * 0.55f);
		const bool bNeedsImmediateLaneReturn = !bTraversingDiagonalRoad && DistanceFromLane > OffLaneDistance;
		if (bNeedsImmediateLaneReturn)
		{
			Vehicle->SetAvoidancePathOffset(FVector::ZeroVector, 0.0f);
			if (FSimCopterVehicleTrafficState* MutableState = VehicleTrafficStates.Find(TObjectKey<ASimCopterGroundAgent>(Vehicle)))
			{
				MutableState->RecoveryRejoinSeconds = 0.0f;
			}
		}

		const float GuidanceDuration = FMath::Max(VehicleLaneGuidanceDurationSeconds, DeltaSeconds * 2.0f);
		Vehicle->SetGuidanceMoveTarget(GuidanceTarget, GuidanceDuration);
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
		FVector BestCarAvoidanceDirection = FVector::ZeroVector;

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
				BestCarAvoidanceDirection = VehicleRight * SideSign;
			}
		}

		if (!BestCarAvoidanceDirection.IsNearlyZero())
		{
			FVector RouteDirection = GetFlatSafeNormal(Pedestrian->GetMoveTargetLocation() - PedestrianLocation);
			if (RouteDirection.IsNearlyZero())
			{
				RouteDirection = GetAgentTravelDirection(*Pedestrian);
			}
			if (RouteDirection.IsNearlyZero())
			{
				continue;
			}

			const FVector RouteRight(-RouteDirection.Y, RouteDirection.X, 0.0f);
			FVector AwayFromRoadCenter = FVector::ZeroVector;
			const bool bHasRoadCenterDirection = TryGetPedestrianAwayFromRoadCenterDirection(*Pedestrian, AwayFromRoadCenter);
			const FVector CandidateDirections[2] = {RouteRight, -RouteRight};
			FVector BestOffsetDirection = CandidateDirections[0];
			float BestScore = -TNumericLimits<float>::Max();
			for (const FVector& CandidateDirection : CandidateDirections)
			{
				const float CarAvoidanceScore = FVector::DotProduct(CandidateDirection, BestCarAvoidanceDirection.GetSafeNormal());
				const float RoadCenterScore = bHasRoadCenterDirection ? FVector::DotProduct(CandidateDirection, AwayFromRoadCenter) : 0.0f;
				const float Score = CarAvoidanceScore + RoadCenterScore * 1.5f;
				if (Score > BestScore)
				{
					BestScore = Score;
					BestOffsetDirection = CandidateDirection;
				}
			}

			Pedestrian->SetAvoidancePathOffset(
				BestOffsetDirection.GetSafeNormal() * PedestrianRoadEscapeDistanceCm,
				PedestrianAvoidanceDurationSeconds,
				PedestrianAvoidanceSpeedMultiplier);
		}
	}
}

bool ASimCopterTrafficSystemActor::IsPedestrianSpawnLocationOpen(const FVector& SpawnLocation) const
{
	float TerrainZ = 0.0f;
	if (GetWorld() == nullptr || !TryGetTerrainWorldZAtWorldLocation(SpawnLocation, TerrainZ))
	{
		return true;
	}

	// Covered by geometry when the topmost surface at the point sits well above the tile's
	// terrain altitude. Building tiles are flat by construction so the threshold stays near
	// the move core's climb allowance; open tiles can slope up to half a terrain step above
	// the tile-center altitude and have no roofs to reject.
	const int32 TileClass = GetPeopleTileClassAtWorldLocation(SpawnLocation);
	const bool bBuildingTile = TileClass >= 10 && TileClass <= 13;
	const float MaxSurfaceRiseCm = bBuildingTile
		? GetPeopleWorldCmPerOriginalUnit() * 5.0f + 60.0f
		: 160.0f;

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterPedSpawnProbe), false);
	const FVector Start(SpawnLocation.X, SpawnLocation.Y, TerrainZ + 12000.0f);
	const FVector End(SpawnLocation.X, SpawnLocation.Y, TerrainZ - 500.0f);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Camera, QueryParams) && Hit.bBlockingHit)
	{
		return Hit.ImpactPoint.Z <= TerrainZ + MaxSurfaceRiseCm;
	}

	return true;
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

bool ASimCopterTrafficSystemActor::TryGetPedestrianAwayFromRoadCenterDirection(
	const ASimCopterGroundAgent& Pedestrian,
	FVector& OutAwayDirection) const
{
	const FVector PedestrianLocation = Pedestrian.GetActorLocation();
	const int32 RouteNodeCandidates[2] = {Pedestrian.GetRouteTargetNode(), Pedestrian.GetRoutePrevNode()};
	FVector BestRoadCenter = FVector::ZeroVector;
	float BestDistanceSq = TNumericLimits<float>::Max();

	for (const int32 PedestrianNodeIndex : RouteNodeCandidates)
	{
		if (!PedestrianNodes.IsValidIndex(PedestrianNodeIndex))
		{
			continue;
		}

		const FSimCopterGroundRouteNode& PedestrianNode = PedestrianNodes[PedestrianNodeIndex];
		const int32* RoadNodeIndex = RoadNodeIndexByTile.Find(FIntPoint(PedestrianNode.FileX, PedestrianNode.FileY));
		if (RoadNodeIndex == nullptr || !RoadNodes.IsValidIndex(*RoadNodeIndex))
		{
			continue;
		}

		const FVector RoadCenter = RoadNodes[*RoadNodeIndex].Location;
		const float DistanceSq = FVector::DistSquared2D(PedestrianLocation, RoadCenter);
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			BestRoadCenter = RoadCenter;
		}
	}

	if (BestDistanceSq == TNumericLimits<float>::Max())
	{
		const int32 NearestRoadNodeIndex = FindNearestNodeIndex(RoadNodes, PedestrianLocation);
		if (!RoadNodes.IsValidIndex(NearestRoadNodeIndex))
		{
			return false;
		}

		BestRoadCenter = RoadNodes[NearestRoadNodeIndex].Location;
	}

	OutAwayDirection = GetFlatSafeNormal(PedestrianLocation - BestRoadCenter);
	return !OutAwayDirection.IsNearlyZero();
}

bool ASimCopterTrafficSystemActor::IsTrafficLightIntersectionNode(int32 NodeIndex) const
{
	if (!RoadNodes.IsValidIndex(NodeIndex))
	{
		return false;
	}

	const FSimCopterGroundRouteNode& Node = RoadNodes[NodeIndex];
	return Node.Neighbors.Num() >= 3 || CountRoadOpenings(GetRoadOpeningMask(Node.BuildingId)) >= 3;
}

bool ASimCopterTrafficSystemActor::IsTrafficLightGreenForApproach(int32 IntersectionNodeIndex, int32 PreviousNodeIndex) const
{
	if (!RoadNodes.IsValidIndex(IntersectionNodeIndex) || !RoadNodes.IsValidIndex(PreviousNodeIndex))
	{
		return true;
	}

	const FSimCopterGroundRouteNode& IntersectionNode = RoadNodes[IntersectionNodeIndex];
	const FSimCopterGroundRouteNode& PreviousNode = RoadNodes[PreviousNodeIndex];
	const int32 DeltaX = IntersectionNode.FileX - PreviousNode.FileX;
	const int32 DeltaY = IntersectionNode.FileY - PreviousNode.FileY;
	const bool bEastWestApproach = FMath::Abs(DeltaX) >= FMath::Abs(DeltaY);
	const float PhaseSeconds = FMath::Max(0.5f, TrafficLightPhaseSeconds);
	const float WorldTimeSeconds = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0f;
	const int32 StaggeredPhaseOffset = bStaggerTrafficLightPhases
		? (FMath::Abs(IntersectionNode.FileX * 17 + IntersectionNode.FileY * 31) & 1)
		: 0;
	const int32 PhaseIndex = FMath::FloorToInt(WorldTimeSeconds / PhaseSeconds) + StaggeredPhaseOffset;
	const bool bEastWestGreen = (PhaseIndex % 2) == 0;
	return bEastWestGreen == bEastWestApproach;
}

void ASimCopterTrafficSystemActor::MarkVehicleInTrafficLightLine(ASimCopterGroundAgent& Vehicle)
{
	FSimCopterVehicleTrafficState& State = VehicleTrafficStates.FindOrAdd(TObjectKey<ASimCopterGroundAgent>(&Vehicle));
	State.bInTrafficLightLine = true;
	State.TrafficLightLineGraceSeconds = FMath::Max(State.TrafficLightLineGraceSeconds, TrafficLightLineGraceDurationSeconds);
}

void ASimCopterTrafficSystemActor::MarkVehicleCommittedToIntersection(ASimCopterGroundAgent& Vehicle)
{
	FSimCopterVehicleTrafficState& State = VehicleTrafficStates.FindOrAdd(TObjectKey<ASimCopterGroundAgent>(&Vehicle));
	State.IntersectionCommitSeconds = FMath::Max(State.IntersectionCommitSeconds, TrafficLightIntersectionCommitDurationSeconds);
	State.bInTrafficLightLine = false;
	State.TrafficLightLineGraceSeconds = 0.0f;
}

void ASimCopterTrafficSystemActor::MarkVehicleCollision(ASimCopterGroundAgent& Vehicle)
{
	FSimCopterVehicleTrafficState& State = VehicleTrafficStates.FindOrAdd(TObjectKey<ASimCopterGroundAgent>(&Vehicle));
	State.RecentCollisionSeconds = FMath::Max(State.RecentCollisionSeconds, VehicleCollisionMemorySeconds);
	if (State.RecoveryCooldownSeconds > 0.0f)
	{
		State.RecoveryRejoinSeconds = FMath::Max(State.RecoveryRejoinSeconds, VehicleRecoveryRejoinDurationSeconds);
	}
}

bool ASimCopterTrafficSystemActor::TryStartVehicleRecovery(ASimCopterGroundAgent& Vehicle, FSimCopterVehicleTrafficState& State)
{
	FVector ForwardDirection = GetFlatSafeNormal(Vehicle.GetMoveTargetLocation() - Vehicle.GetActorLocation());
	if (ForwardDirection.IsNearlyZero())
	{
		ForwardDirection = GetAgentTravelDirection(Vehicle);
	}
	if (ForwardDirection.IsNearlyZero())
	{
		return false;
	}

	ASimCopterGroundAgent* BlockingVehicle = FindClosestBlockingVehicle(Vehicle, ForwardDirection);
	const FVector BypassDirection = ChooseVehicleBypassDirection(Vehicle, BlockingVehicle, ForwardDirection);
	if (BypassDirection.IsNearlyZero())
	{
		return false;
	}

	const FVector ReverseDirection = -ForwardDirection;
	const float BackUpDistance = FMath::Max(0.0f, VehicleRecoveryBackUpDistanceCm);
	if (BackUpDistance > 0.0f)
	{
		Vehicle.MoveByTrafficSeparation(ReverseDirection * BackUpDistance);
	}

	Vehicle.AddTrafficVelocityImpulse(ReverseDirection * VehicleRecoveryReverseImpulseCmPerSec);
	const FVector DesiredBypassOffset = BypassDirection * VehicleRecoveryBypassOffsetCm;
	const FVector SafeBypassOffset = IsVehicleTraversingDiagonalRoadTile(Vehicle)
		? DesiredBypassOffset
		: MakeVehicleRoadSafePathOffset(Vehicle.GetMoveTargetLocation(), DesiredBypassOffset);
	Vehicle.SetAvoidancePathOffset(
		SafeBypassOffset,
		VehicleRecoveryBypassDurationSeconds,
		VehicleRecoveryBypassSpeedMultiplier);
	State.BlockedSeconds = 0.0f;
	State.RecentCollisionSeconds = 0.0f;
	State.RecoveryCooldownSeconds = VehicleRecoveryCooldownSeconds;
	State.RecoveryBypassSeconds = VehicleRecoveryBypassDurationSeconds;
	State.RecoveryRejoinSeconds = 0.0f;
	return true;
}

ASimCopterGroundAgent* ASimCopterTrafficSystemActor::FindClosestBlockingVehicle(
	const ASimCopterGroundAgent& Vehicle,
	const FVector& ForwardDirection) const
{
	const FVector VehicleLocation = Vehicle.GetActorLocation();
	const FVector Forward = GetFlatSafeNormal(ForwardDirection);
	if (Forward.IsNearlyZero())
	{
		return nullptr;
	}

	const FVector Right(-Forward.Y, Forward.X, 0.0f);
	const float LookAhead = FMath::Max(VehicleRecoveryBlockerLookAheadCm, VehicleRecoveryBypassOffsetCm * 2.0f);
	float BestForwardDistance = TNumericLimits<float>::Max();
	ASimCopterGroundAgent* BestVehicle = nullptr;

	for (const TWeakObjectPtr<ASimCopterGroundAgent>& OtherPtr : VehicleAgents)
	{
		ASimCopterGroundAgent* Other = OtherPtr.Get();
		if (Other == nullptr || Other == &Vehicle)
		{
			continue;
		}

		const FVector ToOther = Other->GetActorLocation() - VehicleLocation;
		const FVector FlatToOther(ToOther.X, ToOther.Y, 0.0f);
		const float ForwardDistance = FVector::DotProduct(FlatToOther, Forward);
		if (ForwardDistance < -Vehicle.GetCollisionRadiusCm() || ForwardDistance > LookAhead)
		{
			continue;
		}

		const float LateralDistance = FMath::Abs(FVector::DotProduct(FlatToOther, Right));
		const float BlockingWidth = Vehicle.GetCollisionRadiusCm() + Other->GetCollisionRadiusCm() + VehicleRecoveryBypassOffsetCm * 0.75f;
		if (LateralDistance > BlockingWidth)
		{
			continue;
		}

		if (ForwardDistance < BestForwardDistance)
		{
			BestForwardDistance = ForwardDistance;
			BestVehicle = Other;
		}
	}

	return BestVehicle;
}

FVector ASimCopterTrafficSystemActor::ChooseVehicleBypassDirection(
	const ASimCopterGroundAgent& Vehicle,
	const ASimCopterGroundAgent* BlockingVehicle,
	const FVector& ForwardDirection) const
{
	const FVector Forward = GetFlatSafeNormal(ForwardDirection);
	if (Forward.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}

	const FVector Right(-Forward.Y, Forward.X, 0.0f);
	const FVector VehicleLocation = Vehicle.GetActorLocation();
	const float BypassOffset = FMath::Max(VehicleRecoveryBypassOffsetCm, ActiveTileSize * 0.28f);
	const float ProbeDistance = FMath::Max(VehicleRecoveryBlockerLookAheadCm, BypassOffset * 2.5f);
	const FVector CandidateDirections[2] = {Right, -Right};
	const bool bTraversingDiagonalRoad = IsVehicleTraversingDiagonalRoadTile(Vehicle);
	float BestScore = -TNumericLimits<float>::Max();
	FVector BestDirection = CandidateDirections[0];

	for (const FVector& CandidateDirection : CandidateDirections)
	{
		float Score = 0.0f;
		const FVector CandidateCenter = VehicleLocation + CandidateDirection * BypassOffset + Forward * (ProbeDistance * 0.45f);
		if (!bTraversingDiagonalRoad)
		{
			const FVector RoadClampedCandidateCenter = ClampVehicleLocationToRoadNetwork(CandidateCenter);
			const float OffRoadDistance = FVector::Dist2D(CandidateCenter, RoadClampedCandidateCenter);
			if (OffRoadDistance > KINDA_SMALL_NUMBER)
			{
				Score -= 500.0f + OffRoadDistance * 2.0f;
			}
		}

		for (const TWeakObjectPtr<ASimCopterGroundAgent>& OtherPtr : VehicleAgents)
		{
			const ASimCopterGroundAgent* Other = OtherPtr.Get();
			if (Other == nullptr || Other == &Vehicle)
			{
				continue;
			}

			const FVector ToOther = Other->GetActorLocation() - VehicleLocation;
			const FVector FlatToOther(ToOther.X, ToOther.Y, 0.0f);
			const float ForwardDistance = FVector::DotProduct(FlatToOther, Forward);
			if (ForwardDistance < -VehicleRecoveryBackUpDistanceCm || ForwardDistance > ProbeDistance)
			{
				continue;
			}

			const float SideDistance = FVector::DotProduct(FlatToOther, CandidateDirection);
			const float DistanceFromBypassLane = FMath::Abs(SideDistance - BypassOffset);
			const float CombinedRadius = Vehicle.GetCollisionRadiusCm() + Other->GetCollisionRadiusCm() + VehicleOverlapPaddingCm;
			if (DistanceFromBypassLane < CombinedRadius + ActiveTileSize * 0.1f)
			{
				const float DistancePenalty = 1.0f - FMath::Clamp(DistanceFromBypassLane / FMath::Max(1.0f, CombinedRadius + ActiveTileSize * 0.1f), 0.0f, 1.0f);
				const float ForwardPenalty = 1.0f - FMath::Clamp(FMath::Abs(ForwardDistance - ProbeDistance * 0.45f) / FMath::Max(1.0f, ProbeDistance), 0.0f, 1.0f);
				Score -= 100.0f * DistancePenalty * FMath::Max(0.25f, ForwardPenalty);
			}
		}

		if (BlockingVehicle != nullptr)
		{
			const FVector ToBlocker = BlockingVehicle->GetActorLocation() - VehicleLocation;
			const FVector FlatToBlocker(ToBlocker.X, ToBlocker.Y, 0.0f);
			const float BlockerSide = FVector::DotProduct(FlatToBlocker, CandidateDirection);
			Score += BlockerSide < 0.0f ? 25.0f : -25.0f;
		}

		Score -= FVector::DistSquared2D(CandidateCenter, VehicleLocation + Forward * (ProbeDistance * 0.45f)) * 0.00005f;
		if (Score > BestScore)
		{
			BestScore = Score;
			BestDirection = CandidateDirection;
		}
	}

	return BestDirection.GetSafeNormal();
}

bool ASimCopterTrafficSystemActor::TryMakeVehicleLaneGuidanceTarget(
	const ASimCopterGroundAgent& Vehicle,
	FVector& OutTarget,
	float& OutDistanceFromLane,
	bool& bOutTraversingDiagonalRoad) const
{
	OutTarget = FVector::ZeroVector;
	OutDistanceFromLane = 0.0f;
	bOutTraversingDiagonalRoad = false;

	const int32 TargetIndex = Vehicle.GetRouteTargetNode();
	const int32 PreviousIndex = Vehicle.GetRoutePrevNode();
	const int32 NextIndex = Vehicle.GetRoutePlannedNextNode();
	if (!RoadNodes.IsValidIndex(TargetIndex) || !RoadNodes.IsValidIndex(PreviousIndex))
	{
		return false;
	}

	bOutTraversingDiagonalRoad = DoesVehicleRouteTouchDiagonalRoadTile(TargetIndex, PreviousIndex, NextIndex);

	const FVector SegmentStart = MakeRoutePointLocation(RoadNodes, PreviousIndex, INDEX_NONE, TargetIndex, true);
	const FVector SegmentEnd = MakeRoutePointLocation(RoadNodes, TargetIndex, PreviousIndex, NextIndex, true);
	FVector Segment = SegmentEnd - SegmentStart;
	Segment.Z = 0.0f;
	const float SegmentLength = Segment.Size();
	if (SegmentLength <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector SegmentDirection = Segment / SegmentLength;
	const FVector VehicleLocation = Vehicle.GetActorLocation();
	FVector ToVehicle = VehicleLocation - SegmentStart;
	ToVehicle.Z = 0.0f;
	const float AlongSegment = FVector::DotProduct(ToVehicle, SegmentDirection);
	const float ClampedAlongSegment = FMath::Clamp(AlongSegment, 0.0f, SegmentLength);
	FVector ClosestLanePoint = SegmentStart + SegmentDirection * ClampedAlongSegment;
	ClosestLanePoint.Z = VehicleLocation.Z;
	OutDistanceFromLane = FVector::Dist2D(VehicleLocation, ClosestLanePoint);

	const float OffLaneDistance = FMath::Max(VehicleRoadContainmentDistanceCm, ActiveTileSize * 0.55f);
	const float BaseLookAhead = FMath::Max(VehicleLaneGuidanceLookAheadCm, ActiveTileSize * 0.25f);
	const bool bNeedsImmediateLaneReturn = !bOutTraversingDiagonalRoad && OutDistanceFromLane > OffLaneDistance;
	const float EffectiveLookAhead = bNeedsImmediateLaneReturn
		? FMath::Clamp(BaseLookAhead * 0.45f, 80.0f, ActiveTileSize * 0.5f)
		: BaseLookAhead;
	const float DesiredAlongSegment = FMath::Clamp(ClampedAlongSegment + EffectiveLookAhead, 0.0f, SegmentLength);
	OutTarget = SegmentStart + SegmentDirection * DesiredAlongSegment;
	OutTarget.Z = SegmentEnd.Z;
	return true;
}

bool ASimCopterTrafficSystemActor::IsVehicleTraversingDiagonalRoadTile(const ASimCopterGroundAgent& Vehicle) const
{
	return DoesVehicleRouteTouchDiagonalRoadTile(
		Vehicle.GetRouteTargetNode(),
		Vehicle.GetRoutePrevNode(),
		Vehicle.GetRoutePlannedNextNode());
}

bool ASimCopterTrafficSystemActor::DoesVehicleRouteTouchDiagonalRoadTile(
	int32 TargetIndex,
	int32 PreviousIndex,
	int32 NextIndex) const
{
	const int32 RouteNodeIndexes[3] = {TargetIndex, PreviousIndex, NextIndex};
	for (const int32 RouteNodeIndex : RouteNodeIndexes)
	{
		if (RoadNodes.IsValidIndex(RouteNodeIndex) && IsAdjacentRoadCornerTile(RoadNodes[RouteNodeIndex].BuildingId))
		{
			return true;
		}
	}

	return false;
}

FVector ASimCopterTrafficSystemActor::ClampVehicleLocationToRoadNetwork(const FVector& Location) const
{
	if (RoadNodes.Num() == 0)
	{
		return Location;
	}

	const int32 NearestRoadNodeIndex = FindNearestNodeIndex(RoadNodes, Location);
	if (!RoadNodes.IsValidIndex(NearestRoadNodeIndex))
	{
		return Location;
	}

	const FVector RoadLocation = RoadNodes[NearestRoadNodeIndex].Location;
	FVector FlatDelta = Location - RoadLocation;
	FlatDelta.Z = 0.0f;
	const float MaxRoadDistance = FMath::Max(VehicleRoadContainmentDistanceCm, ActiveTileSize * 0.55f);
	if (FlatDelta.SizeSquared() <= FMath::Square(MaxRoadDistance))
	{
		return Location;
	}

	FVector ClampedLocation = RoadLocation;
	if (!FlatDelta.IsNearlyZero())
	{
		ClampedLocation += FlatDelta.GetSafeNormal() * MaxRoadDistance;
	}
	ClampedLocation.Z = Location.Z;
	return ClampedLocation;
}

FVector ASimCopterTrafficSystemActor::MakeVehicleRoadSafePathOffset(const FVector& BaseLocation, const FVector& DesiredOffset) const
{
	const FVector DesiredTarget = BaseLocation + FVector(DesiredOffset.X, DesiredOffset.Y, 0.0f);
	const FVector ClampedTarget = ClampVehicleLocationToRoadNetwork(DesiredTarget);
	const FVector SafeOffset = ClampedTarget - BaseLocation;
	return FVector(SafeOffset.X, SafeOffset.Y, 0.0f);
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

	const bool bVehicle = Agent.GetAgentKind() == ESimCopterGroundAgentKind::Vehicle;
	const int32 ApproachIndex = Agent.GetRoutePrevNode();
	int32 NextIndex = Agent.GetRoutePlannedNextNode();
	if (!Nodes.IsValidIndex(NextIndex) || !Nodes[FromIndex].Neighbors.Contains(NextIndex))
	{
		NextIndex = ChooseNextRouteNode(Nodes, FromIndex, ApproachIndex, RandomStream, TrafficAiMode == ESimCopterTrafficAiMode::Modernized);
	}
	if (NextIndex == INDEX_NONE)
	{
		// Isolated node with no neighbours: re-seed from the nearest node so the agent isn't stuck.
		const int32 ReseedIndex = FindNearestNodeIndex(Nodes, Agent.GetActorLocation());
		Agent.SetRouteState(ReseedIndex, INDEX_NONE);
		if (Nodes.IsValidIndex(ReseedIndex))
		{
			Agent.SetMoveTarget(bVehicle
				? Nodes[ReseedIndex].Location
				: Nodes[ReseedIndex].Location);
		}
		else
		{
			Agent.ClearMoveTarget();
		}
		return;
	}

	const int32 PlannedNextIndex = bVehicle ? ChooseNextRouteNode(Nodes, NextIndex, FromIndex, RandomStream, TrafficAiMode == ESimCopterTrafficAiMode::Modernized) : INDEX_NONE;

	// Drive to the chosen adjacent graph node; record where we came from for the next hop.
	Agent.SetRouteState(NextIndex, FromIndex, PlannedNextIndex);
	if (bVehicle)
	{
		Agent.SetMoveTarget(MakeVehicleRouteTargetLocation(Nodes, NextIndex, FromIndex, ApproachIndex, PlannedNextIndex));
	}
	else
	{
		Agent.SetMoveTarget(MakeRoutePointLocation(Nodes, NextIndex, FromIndex, INDEX_NONE, false));
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

	int32 NodeIndex = INDEX_NONE;
	int32 InitialNextIndex = INDEX_NONE;
	int32 InitialBehaviorClass = 0;
	FVector SpawnBaseLocation = FVector::ZeroVector;
	for (int32 Attempt = 0; Attempt < 18; ++Attempt)
	{
		const int32 CandidateNodeIndex = ChooseNodeNearFocus(Nodes, FocusLocation);
		if (CandidateNodeIndex == INDEX_NONE)
		{
			return false;
		}

		const int32 CandidateNextIndex = bVehicle ? ChooseNextRouteNode(Nodes, CandidateNodeIndex, INDEX_NONE, RandomStream, TrafficAiMode == ESimCopterTrafficAiMode::Modernized) : INDEX_NONE;
		const int32 CandidateBehaviorClass = bVehicle
			? 0
			: FSimCopterPeopleCityRules::ChooseAmbientBehaviorClassForTileClass(Nodes[CandidateNodeIndex].PeopleTileClass, PeopleRandomState);
		if (!bVehicle && CandidateBehaviorClass == INDEX_NONE)
		{
			continue;
		}

		FVector CandidateSpawnBaseLocation = bVehicle
			? MakeRoutePointLocation(Nodes, CandidateNodeIndex, INDEX_NONE, CandidateNextIndex, true)
			: Nodes[CandidateNodeIndex].Location + MakePeopleSpawnOffsetWorld(
				ActiveCityToWorldTransform,
				ActiveTileSize,
				Nodes[CandidateNodeIndex].PeopleFootprintSize,
				Nodes[CandidateNodeIndex].PeoplePlacementMode,
				PeopleRandomState);
		// The original spawn placement rejects points covered by object geometry; without this
		// a pedestrian dropped inside a building footprint is trapped by the move core's
		// climb gate (every escape step sees the roof as the walked surface).
		if (!bVehicle && !IsPedestrianSpawnLocationOpen(CandidateSpawnBaseLocation))
		{
			continue;
		}
		if (!bVehicle || IsVehicleSpawnLocationClear(CandidateSpawnBaseLocation))
		{
			NodeIndex = CandidateNodeIndex;
			InitialNextIndex = CandidateNextIndex;
			InitialBehaviorClass = CandidateBehaviorClass;
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
	if (bVehicle && Nodes.IsValidIndex(InitialNextIndex))
	{
		const FVector Target = MakeRoutePointLocation(Nodes, InitialNextIndex, NodeIndex, INDEX_NONE, bVehicle);
		SpawnRotation.Yaw = (Target - SpawnBaseLocation).Rotation().Yaw;
	}
	else if (bVehicle && Node.Neighbors.Num() > 0)
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

	if (!bVehicle)
	{
		Agent->SetInitialBehaviorClass(InitialBehaviorClass);
	}

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

	// Seed the route at the spawn node. Vehicles then pick the first road-graph hop; pedestrians
	// leave movement to the original people VM and use RouteTargetNode only as spawn/debug state.
	if (bVehicle && Nodes.IsValidIndex(InitialNextIndex))
	{
		const int32 InitialPlannedNextIndex = ChooseNextRouteNode(Nodes, InitialNextIndex, NodeIndex, RandomStream, TrafficAiMode == ESimCopterTrafficAiMode::Modernized);
		Agent->SetRouteState(InitialNextIndex, NodeIndex, InitialPlannedNextIndex);
		Agent->SetMoveTarget(MakeVehicleRouteTargetLocation(Nodes, InitialNextIndex, NodeIndex, INDEX_NONE, InitialPlannedNextIndex));
	}
	else if (bVehicle)
	{
		Agent->SetRouteState(NodeIndex, INDEX_NONE);
		AssignNextTarget(*Agent, Nodes);
	}
	else
	{
		Agent->SetRouteState(NodeIndex, INDEX_NONE);
		Agent->ClearMoveTarget();
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

FVector ASimCopterTrafficSystemActor::MakeVehicleRouteTargetLocation(
	const TArray<FSimCopterGroundRouteNode>& Nodes,
	int32 TargetIndex,
	int32 PreviousIndex,
	int32 ApproachIndex,
	int32 LookAheadIndex) const
{
	const int32 BaseLookAheadIndex = Nodes.IsValidIndex(TargetIndex) && IsAdjacentRoadCornerTile(Nodes[TargetIndex].BuildingId)
		? LookAheadIndex
		: INDEX_NONE;
	FVector TargetLocation = MakeRoutePointLocation(Nodes, TargetIndex, PreviousIndex, BaseLookAheadIndex, true);
	if (Nodes.IsValidIndex(TargetIndex) &&
		Nodes.IsValidIndex(PreviousIndex) &&
		Nodes.IsValidIndex(LookAheadIndex))
	{
		FVector TargetIncomingDirection = Nodes[TargetIndex].LocalLocation - Nodes[PreviousIndex].LocalLocation;
		FVector TargetOutgoingDirection = Nodes[LookAheadIndex].LocalLocation - Nodes[TargetIndex].LocalLocation;
		TargetIncomingDirection.Z = 0.0f;
		TargetOutgoingDirection.Z = 0.0f;
		TargetIncomingDirection = TargetIncomingDirection.GetSafeNormal();
		TargetOutgoingDirection = TargetOutgoingDirection.GetSafeNormal();
		if (!TargetIncomingDirection.IsNearlyZero() && !TargetOutgoingDirection.IsNearlyZero())
		{
			const float TargetTurnDot = FVector::DotProduct(TargetIncomingDirection, TargetOutgoingDirection);
			const FVector TargetIncomingSideways = TargetIncomingDirection -
				TargetOutgoingDirection * FVector::DotProduct(TargetIncomingDirection, TargetOutgoingDirection);
			const FVector TargetCounterInertiaDirection = -TargetIncomingSideways.GetSafeNormal();
			const bool bUpcomingTurnNeedsEarlyClip = TargetTurnDot <= 0.92f &&
				TargetTurnDot >= -0.25f &&
				!TargetCounterInertiaDirection.IsNearlyZero() &&
				IsRightHandTurnLocal(TargetIncomingDirection, TargetOutgoingDirection);
			if (bUpcomingTurnNeedsEarlyClip)
			{
				TargetLocation = MakeRoutePointLocation(Nodes, TargetIndex, PreviousIndex, LookAheadIndex, true);
				const FVector EarlyClipLocalOffset = TargetCounterInertiaDirection * (ActiveTileSize * VehicleRightTurnEarlyClipTileFraction);
				TargetLocation += ActiveCityToWorldTransform.TransformVector(EarlyClipLocalOffset);
			}
		}
	}

	if (VehicleCornerClipTileFraction <= 0.0f ||
		!Nodes.IsValidIndex(TargetIndex) ||
		!Nodes.IsValidIndex(PreviousIndex) ||
		!Nodes.IsValidIndex(ApproachIndex))
	{
		return TargetLocation;
	}

	FVector IncomingDirection = Nodes[PreviousIndex].LocalLocation - Nodes[ApproachIndex].LocalLocation;
	FVector OutgoingDirection = Nodes[TargetIndex].LocalLocation - Nodes[PreviousIndex].LocalLocation;
	IncomingDirection.Z = 0.0f;
	OutgoingDirection.Z = 0.0f;
	IncomingDirection = IncomingDirection.GetSafeNormal();
	OutgoingDirection = OutgoingDirection.GetSafeNormal();
	if (IncomingDirection.IsNearlyZero() || OutgoingDirection.IsNearlyZero())
	{
		return TargetLocation;
	}

	const float TurnDot = FVector::DotProduct(IncomingDirection, OutgoingDirection);
	if (TurnDot > 0.92f || TurnDot < -0.25f)
	{
		return TargetLocation;
	}

	const FVector IncomingSideways = IncomingDirection - OutgoingDirection * FVector::DotProduct(IncomingDirection, OutgoingDirection);
	const FVector CounterInertiaDirection = -IncomingSideways.GetSafeNormal();
	if (CounterInertiaDirection.IsNearlyZero())
	{
		return TargetLocation;
	}

	const bool bRightHandTurn = IsRightHandTurnLocal(IncomingDirection, OutgoingDirection);
	const float EffectiveClipFraction = bRightHandTurn
		? VehicleRightTurnCornerClipTileFraction
		: VehicleCornerClipTileFraction;
	const FVector CornerClipLocalOffset = CounterInertiaDirection * (ActiveTileSize * EffectiveClipFraction);
	return TargetLocation + ActiveCityToWorldTransform.TransformVector(CornerClipLocalOffset);
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
	if (TryGetCornerRoadTravelDirectionLocal(Nodes, PointIndex, PreviousIndex, NextIndex, Direction))
	{
		// Direction already follows the tile's visual diagonal/curve from entry opening to exit opening.
	}
	else if (Nodes.IsValidIndex(NextIndex))
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

	const FVector LaneSide = GetRightHandLaneSideLocalDirection(Direction);
	if (LaneSide.IsNearlyZero())
	{
		return Point.Location;
	}

	const FVector LaneLocalLocation = Point.LocalLocation + LaneSide * (ActiveTileSize * VehicleLaneOffsetTileFraction);
	return ActiveCityToWorldTransform.TransformPosition(LaneLocalLocation);
}
