// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterTrafficSystemActor.h"

#include "Algo/Reverse.h"
#include "City/SimCity2000CityActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Formats/SimCopterPeopleCityRules.h"
#include "Formats/SimCity2000Reader.h"
#include "GameFramework/PlayerController.h"
#include "Ground/SimCopterCriminalCar.h"
#include "Ground/SimCopterDispatchMarker.h"
#include "Ground/SimCopterEffectFX.h"
#include "Ground/SimCopterGroundAgent.h"
#include "Kismet/GameplayStatics.h"
#include "Missions/SimCopterMissionSystemActor.h"
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

FVector MakePeopleSpawnOffsetWorldFromOriginalUnits(
	const FTransform& CityToWorldTransform,
	float TileSize,
	const FVector2D& OriginalOffset)
{
	const float OriginalUnitToWorld = TileSize / 64.0f;
	return CityToWorldTransform.TransformVector(FVector(
		OriginalOffset.X * OriginalUnitToWorld,
		OriginalOffset.Y * OriginalUnitToWorld,
		0.0f));
}

uint8 GetPeopleTerrainTypeForAmbientGate(const FSimCity2000Tile& Tile)
{
	if (Tile.bWater)
	{
		return 5;
	}
	if (Tile.Building >= 1 && Tile.Building <= 4)
	{
		return 10;
	}
	if (Tile.Building != 0)
	{
		return 0x10;
	}
	return Tile.Terrain == 0x0D ? 0x20 : 0x30;
}

bool IsOriginalAmbientTerrainType(uint8 TerrainType)
{
	// FUN_004c92a0 returns 0 for type > 9; the ambient null-person gate in FUN_004c9cc0
	// accepts that branch after the scene-cell flag check.
	return TerrainType > 9;
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
	// Speeders take their spotlight mark before the police read it, so a car lit this frame can
	// be pulled over this frame.
	UpdateCriminalCars(DeltaSeconds);
	UpdateDispatchVehicles(DeltaSeconds);
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
		
		if (!TryGetPeopleTileCoordinateAtWorldLocation(ChosenVehicle->GetActorLocation(), OutTileX, OutTileY))
		{
			OutTileX = 64;
			OutTileY = 64;
		}
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
		// A burning car stops (jams) and shows flames until doused.
		State->bMissionJammed = true;
		State->bMissionOnFire = true;
		State->MissionEventId = EventId;
	}

	if (!TryGetPeopleTileCoordinateAtWorldLocation(ChosenVehicle->GetActorLocation(), OutTileX, OutTileY))
	{
		OutTileX = 64;
		OutTileY = 64;
	}

	return true;
}

void ASimCopterTrafficSystemActor::GetBurningVehicles(TArray<FSimCopterBurningVehicle>& Out) const
{
	for (const TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : VehicleAgents)
	{
		const ASimCopterGroundAgent* Vehicle = AgentPtr.Get();
		if (Vehicle == nullptr)
		{
			continue;
		}
		const FSimCopterVehicleTrafficState* State = VehicleTrafficStates.Find(TObjectKey<ASimCopterGroundAgent>(Vehicle));
		if (State == nullptr || !State->bMissionOnFire)
		{
			continue;
		}
		FSimCopterBurningVehicle Burning;
		// Stable, collision-free key distinct from the mission-system flame slot indices.
		Burning.Key = 0x40000000 | static_cast<int32>(Vehicle->GetUniqueID() & 0x3FFFFFFF);
		Burning.EventId = State->MissionEventId;
		Burning.World = Vehicle->GetActorLocation();
		Out.Add(Burning);
	}
}

void ASimCopterTrafficSystemActor::DouseBurningVehiclesNear(const FVector& WorldLocation, float RadiusCm, TArray<int32>& OutExtinguishedEventIds)
{
	const float RadiusSq = RadiusCm * RadiusCm;
	for (TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : VehicleAgents)
	{
		ASimCopterGroundAgent* Vehicle = AgentPtr.Get();
		if (Vehicle == nullptr)
		{
			continue;
		}
		FSimCopterVehicleTrafficState* State = VehicleTrafficStates.Find(TObjectKey<ASimCopterGroundAgent>(Vehicle));
		if (State == nullptr || !State->bMissionOnFire)
		{
			continue;
		}
		if (FVector::DistSquared(Vehicle->GetActorLocation(), WorldLocation) > RadiusSq)
		{
			continue;
		}

		// Put the car out: it stops burning and resumes normal traffic.
		State->bMissionOnFire = false;
		State->bMissionJammed = false;
		OutExtinguishedEventIds.Add(State->MissionEventId);
		State->MissionEventId = INDEX_NONE;
	}
}

bool ASimCopterTrafficSystemActor::TrySpawnMissionPerson(
	int32 SpawnMode,
	int32 PersonState,
	int32 TileX,
	int32 TileY,
	int32 EventId,
	const FString& FigureName,
	ASimCopterGroundAgent** OutSpawned)
{
	if (OutSpawned != nullptr)
	{
		*OutSpawned = nullptr;
	}
	if (GroundAgentClass == nullptr) return false;
	const bool bTransportPassenger = SpawnMode == 4;
	if (bTransportPassenger && IsWaterTile(TileX, TileY))
	{
		int32 LandX = TileX;
		int32 LandY = TileY;
		if (!TryFindNearestTransportLandTile(TileX, TileY, LandX, LandY))
		{
			return false;
		}
		TileX = LandX;
		TileY = LandY;
	}

	int32 ExistingMissionPeople = 0;
	for (const TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : PedestrianAgents)
	{
		if (const ASimCopterGroundAgent* Agent = AgentPtr.Get())
		{
			if (Agent->MissionEventId == EventId)
			{
				ExistingMissionPeople++;
			}
		}
	}

	const float GoldenAngleRadians = 2.39996323f;
	FVector SpawnLocation = FVector::ZeroVector;
	bool bFoundSpawnLocation = false;

	// The mission tile is usually the building itself (an injured person at an apartment/office), so
	// searching only that tile buries victims inside the mesh. Search outward from the tile - across
	// the building's whole footprint and a margin - and accept the first point that is not inside a
	// building (or that is on a road). This lands victims on the building edge, the adjacent
	// sidewalk or the street, never inside a wall.
	const int32 FootprintSize = FMath::Clamp(GetBuildingFootprintSize(TileX, TileY), 1, 4);
	const int32 MaxRing = FootprintSize + 2;
	for (int32 Ring = 0; Ring <= MaxRing && !bFoundSpawnLocation; ++Ring)
	{
		for (int32 OffsetY = -Ring; OffsetY <= Ring && !bFoundSpawnLocation; ++OffsetY)
		{
			for (int32 OffsetX = -Ring; OffsetX <= Ring && !bFoundSpawnLocation; ++OffsetX)
			{
				// Only the new outer shell of each ring.
				if (Ring > 0 && FMath::Abs(OffsetX) != Ring && FMath::Abs(OffsetY) != Ring)
				{
					continue;
				}

				FVector TileCenter = FVector::ZeroVector;
				if (!TryGetTileCenterWorldLocation(TileX + OffsetX, TileY + OffsetY, TileCenter))
				{
					continue;
				}
				if (SpawnMode == 4 && IsWaterTile(TileX + OffsetX, TileY + OffsetY))
				{
					continue;
				}
				if (bTransportPassenger && IsPedestrianRoadTile(uint8(GetXbldTileId(TileX + OffsetX, TileY + OffsetY))))
				{
					continue;
				}

				const int32 SampleCount = (Ring == 0) ? 6 : 3;
				for (int32 Sample = 0; Sample < SampleCount; ++Sample)
				{
					const int32 CandidateIndex = ExistingMissionPeople + Ring * 4 + Sample;
					const float Angle = bTransportPassenger ? RandomStream.FRandRange(0.0f, 2.0f * PI) : float(CandidateIndex) * GoldenAngleRadians;
					const float Radius = bTransportPassenger
						? ActiveTileSize * RandomStream.FRandRange(0.08f, 0.34f)
						: ActiveTileSize * (Ring == 0 ? (0.12f + 0.05f * float(Sample)) : 0.18f);
					FVector CandidateLocation = TileCenter + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Radius;
					CandidateLocation.Z += 92.0f;
					if (IsMissionGroundSpawnValid(CandidateLocation))
					{
						SpawnLocation = CandidateLocation;
						bFoundSpawnLocation = true;
						break;
					}
				}
			}
		}
	}

	if (!bFoundSpawnLocation)
	{
		// Nowhere open near the mission tile: skip rather than spawn a victim inside a building.
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	const float SpawnYaw = RandomStream.FRandRange(0.0f, 360.0f);
	ASimCopterGroundAgent* Person = GetWorld()->SpawnActor<ASimCopterGroundAgent>(GroundAgentClass, SpawnLocation, FRotator(0.0f, SpawnYaw, 0.0f), SpawnParams);
	if (Person == nullptr)
	{
		return false;
	}

	Person->InitialPersonState = bTransportPassenger ? 0 : SpawnMode;
	if (PersonState != -1)
	{
		Person->SetInitialBehaviorClass(PersonState);
	}
	else if (bTransportPassenger)
	{
		const int32 TileClass = GetPeopleTileClassAtWorldLocation(SpawnLocation);
		Person->SetInitialBehaviorClass(FSimCopterPeopleCityRules::ChooseAmbientBehaviorClassForTileClass(TileClass, PeopleRandomState));
	}

	const FString MeshName = PedestrianMeshNames.Num() > 0 ? PedestrianMeshNames[RandomStream.RandRange(0, PedestrianMeshNames.Num() - 1)] : FString();
	Person->MissionEventId = EventId;
	if (!FigureName.IsEmpty())
	{
		// Has to happen before ConfigureAgent - that is where the figure is built.
		Person->SetPedestrianFigureName(FigureName);
	}
	Person->ConfigureAgent(
		ESimCopterGroundAgentKind::Pedestrian,
		MeshName,
		ActiveOriginalGameRootPath.IsEmpty() ? ResolveOriginalGameRoot() : ActiveOriginalGameRootPath,
		PedestrianSpeedCmPerSec);

	if (bRequireOriginalPopulationMeshes && !Person->IsUsingOriginalMesh())
	{
		UE_LOG(LogSimCopterTrafficSystem, Warning, TEXT("Discarding mission pedestrian because original mesh '%s' could not be loaded."), *MeshName);
		Person->Destroy();
		return false;
	}

	Person->SnapToGroundImmediate();
	if (SpawnMode == 6)
	{
		Person->SetMissionInjuredPose();
	}
	else if (SpawnMode == 2)
	{
		// Fire-rescue victims are uninjured, so they keep milling about on their program - but
		// every time it leaves them standing they wave for the helicopter.
		Person->SetMissionAwaitingRescue(true);
	}
	PedestrianAgents.Add(Person);
	if (OutSpawned != nullptr)
	{
		*OutSpawned = Person;
	}
	return true;
}

bool ASimCopterTrafficSystemActor::HasArrestedCriminalNear(const FVector& WorldLocation, const float RadiusCm) const
{
	const float RadiusSq = FMath::Square(RadiusCm);
	for (const TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : PedestrianAgents)
	{
		const ASimCopterGroundAgent* Agent = AgentPtr.Get();
		if (Agent == nullptr || Agent->IsActorBeingDestroyed() || Agent->IsHidden())
		{
			// Hidden means opcode 40 has already run on them: they are in the car.
			continue;
		}
		if (Agent->GetBehaviorAttribute(EBhavAttr::CriminalCaught) == 0)
		{
			continue;
		}
		if (FVector::DistSquared(WorldLocation, Agent->GetActorLocation()) <= RadiusSq)
		{
			return true;
		}
	}
	return false;
}

int32 ASimCopterTrafficSystemActor::PickUpMissionPeopleNear(
	int32 EventId,
	const FVector& WorldLocation,
	int32 MaxCount,
	float RadiusCm,
	float MaxVerticalDeltaCm,
	int32* OutNewPickupCreditCount)
{
	if (OutNewPickupCreditCount != nullptr)
	{
		*OutNewPickupCreditCount = 0;
	}
	if (MaxCount <= 0)
	{
		return 0;
	}

	struct FMissionPickupCandidate
	{
		TWeakObjectPtr<ASimCopterGroundAgent> Agent;
		float DistanceSq = 0.0f;
	};

	TArray<FMissionPickupCandidate, TInlineAllocator<16>> Candidates;
	const float RadiusSq = FMath::Square(RadiusCm);
	for (const TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : PedestrianAgents)
	{
		ASimCopterGroundAgent* Agent = AgentPtr.Get();
		if (Agent == nullptr || Agent->MissionEventId != EventId || Agent->IsActorBeingDestroyed())
		{
			continue;
		}

		const FVector AgentLocation = Agent->GetActorLocation();
		if (FMath::Abs(AgentLocation.Z - WorldLocation.Z) > MaxVerticalDeltaCm)
		{
			continue;
		}

		const float DistanceSq = FVector::DistSquared2D(AgentLocation, WorldLocation);
		if (DistanceSq > RadiusSq)
		{
			continue;
		}

		FMissionPickupCandidate Candidate;
		Candidate.Agent = Agent;
		Candidate.DistanceSq = DistanceSq;
		Candidates.Add(Candidate);
	}

	Candidates.Sort([](const FMissionPickupCandidate& Left, const FMissionPickupCandidate& Right)
	{
		return Left.DistanceSq < Right.DistanceSq;
	});

	int32 PickedUp = 0;
	int32 NewPickupCreditCount = 0;
	for (const FMissionPickupCandidate& Candidate : Candidates)
	{
		if (PickedUp >= MaxCount)
		{
			break;
		}

		if (ASimCopterGroundAgent* Agent = Candidate.Agent.Get())
		{
			if (!Agent->HasMissionPickupCreditAwarded())
			{
				Agent->SetMissionPickupCreditAwarded(true);
				NewPickupCreditCount++;
			}
			Agent->MissionEventId = INDEX_NONE;
			Agent->Destroy();
			PickedUp++;
		}
	}

	if (PickedUp > 0)
	{
		PedestrianAgents.RemoveAll([](const TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr)
		{
			const ASimCopterGroundAgent* Agent = AgentPtr.Get();
			return Agent == nullptr || Agent->IsActorBeingDestroyed();
		});
	}

	if (OutNewPickupCreditCount != nullptr)
	{
		*OutNewPickupCreditCount = NewPickupCreditCount;
	}
	return PickedUp;
}

int32 ASimCopterTrafficSystemActor::GuideMissionPeopleToLocation(
	int32 EventId,
	const FVector& SearchLocation,
	const FVector& TargetLocation,
	int32 MaxCount,
	float SearchRadiusCm,
	float MaxVerticalDeltaCm,
	float GuidanceSeconds)
{
	if (MaxCount <= 0)
	{
		return 0;
	}

	struct FMissionGuidanceCandidate
	{
		TWeakObjectPtr<ASimCopterGroundAgent> Agent;
		float DistanceSq = 0.0f;
	};

	TArray<FMissionGuidanceCandidate, TInlineAllocator<16>> Candidates;
	const float RadiusSq = FMath::Square(SearchRadiusCm);
	for (const TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : PedestrianAgents)
	{
		ASimCopterGroundAgent* Agent = AgentPtr.Get();
		if (Agent == nullptr || Agent->MissionEventId != EventId || Agent->IsActorBeingDestroyed() || Agent->IsMissionCarried())
		{
			continue;
		}

		const FVector AgentLocation = Agent->GetActorLocation();
		if (FMath::Abs(AgentLocation.Z - SearchLocation.Z) > MaxVerticalDeltaCm)
		{
			continue;
		}

		const float DistanceSq = FVector::DistSquared2D(AgentLocation, SearchLocation);
		if (DistanceSq > RadiusSq)
		{
			continue;
		}

		FMissionGuidanceCandidate Candidate;
		Candidate.Agent = Agent;
		Candidate.DistanceSq = DistanceSq;
		Candidates.Add(Candidate);
	}

	Candidates.Sort([](const FMissionGuidanceCandidate& Left, const FMissionGuidanceCandidate& Right)
	{
		return Left.DistanceSq < Right.DistanceSq;
	});

	int32 Guided = 0;
	for (const FMissionGuidanceCandidate& Candidate : Candidates)
	{
		if (Guided >= MaxCount)
		{
			break;
		}

		if (ASimCopterGroundAgent* Agent = Candidate.Agent.Get())
		{
			Agent->SetGuidanceMoveTarget(TargetLocation, GuidanceSeconds);
			Guided++;
		}
	}

	return Guided;
}

int32 ASimCopterTrafficSystemActor::BoardMissionPeopleTouching(
	int32 EventId,
	const FVector& WorldLocation,
	int32 MaxCount,
	float TouchRadiusCm,
	float MaxVerticalDeltaCm,
	int32* OutNewPickupCreditCount)
{
	if (MaxCount <= 0)
	{
		if (OutNewPickupCreditCount != nullptr)
		{
			*OutNewPickupCreditCount = 0;
		}
		return 0;
	}

	return PickUpMissionPeopleNear(EventId, WorldLocation, MaxCount, TouchRadiusCm, MaxVerticalDeltaCm, OutNewPickupCreditCount);
}

ASimCopterGroundAgent* ASimCopterTrafficSystemActor::FindMissionPersonNear(int32 EventId, const FVector& WorldLocation, float RadiusCm, float MaxVerticalDeltaCm)
{
	ASimCopterGroundAgent* BestAgent = nullptr;
	float BestDistanceSq = FMath::Square(RadiusCm);
	for (const TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : PedestrianAgents)
	{
		ASimCopterGroundAgent* Agent = AgentPtr.Get();
		if (Agent == nullptr || Agent->MissionEventId != EventId || Agent->IsActorBeingDestroyed() || Agent->IsMissionCarried())
		{
			continue;
		}

		const FVector AgentLocation = Agent->GetActorLocation();
		if (FMath::Abs(AgentLocation.Z - WorldLocation.Z) > MaxVerticalDeltaCm)
		{
			continue;
		}

		const float DistanceSq = FVector::DistSquared2D(AgentLocation, WorldLocation);
		if (DistanceSq <= BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			BestAgent = Agent;
		}
	}

	return BestAgent;
}

int32 ASimCopterTrafficSystemActor::SpawnMissionPeopleAtWorldLocation(
	int32 Count,
	const FVector& WorldLocation,
	int32 EventId,
	int32 SpawnMode,
	int32 PersonState,
	float SpreadRadiusCm)
{
	if (GroundAgentClass == nullptr || Count <= 0 || GetWorld() == nullptr)
	{
		return 0;
	}

	int32 Spawned = 0;
	const float ClampedSpread = FMath::Max(20.0f, SpreadRadiusCm);
	const float GoldenAngleRadians = 2.39996323f;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		// Deliberate drop point (beside a landed helicopter / at a hospital); fall back to it if no
		// scattered candidate validates, but prefer a candidate that isn't buried in a mesh.
		FVector SpawnLocation = WorldLocation + FVector(0.0f, 0.0f, 92.0f);
		for (int32 Attempt = 0; Attempt < 16; ++Attempt)
		{
			const int32 CandidateIndex = Spawned + Attempt;
			const float Angle = float(CandidateIndex) * GoldenAngleRadians;
			const float Radius = FMath::Min(ClampedSpread, 35.0f + 28.0f * float(CandidateIndex));
			FVector CandidateLocation = WorldLocation + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Radius;
			CandidateLocation.Z += 92.0f;
			if (IsMissionGroundSpawnValid(CandidateLocation))
			{
				SpawnLocation = CandidateLocation;
				break;
			}
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		const float SpawnYaw = RandomStream.FRandRange(0.0f, 360.0f);
		ASimCopterGroundAgent* Person = GetWorld()->SpawnActor<ASimCopterGroundAgent>(GroundAgentClass, SpawnLocation, FRotator(0.0f, SpawnYaw, 0.0f), SpawnParams);
		if (Person == nullptr)
		{
			continue;
		}

		Person->InitialPersonState = SpawnMode;
		if (PersonState != -1)
		{
			Person->SetInitialBehaviorClass(PersonState);
		}

		const FString MeshName = PedestrianMeshNames.Num() > 0 ? PedestrianMeshNames[RandomStream.RandRange(0, PedestrianMeshNames.Num() - 1)] : FString();
		Person->MissionEventId = EventId;
		Person->ConfigureAgent(
			ESimCopterGroundAgentKind::Pedestrian,
			MeshName,
			ActiveOriginalGameRootPath.IsEmpty() ? ResolveOriginalGameRoot() : ActiveOriginalGameRootPath,
			PedestrianSpeedCmPerSec);

		if (bRequireOriginalPopulationMeshes && !Person->IsUsingOriginalMesh())
		{
			UE_LOG(LogSimCopterTrafficSystem, Warning, TEXT("Discarding dropped pedestrian because original mesh '%s' could not be loaded."), *MeshName);
			Person->Destroy();
			continue;
		}

		Person->SnapToGroundImmediate();
		if (SpawnMode == 6)
		{
			Person->SetMissionInjuredPose();
		}
		PedestrianAgents.Add(Person);
		Spawned++;
	}

	return Spawned;
}

int32 ASimCopterTrafficSystemActor::SpawnMissionSwimmersAtWorldLocation(
	int32 Count,
	const FVector& WorldLocation,
	int32 EventId,
	int32 SpawnMode,
	float SpreadRadiusCm,
	bool bFloatOnWaterSurface,
	TArray<ASimCopterGroundAgent*>* OutSpawned)
{
	if (GroundAgentClass == nullptr || Count <= 0 || GetWorld() == nullptr)
	{
		return 0;
	}

	// The surface the survivors bob on. ASimCity2000CityActor keeps the same conditioned water
	// height the bucket samples, so they float at the level the boat sits at.
	float SurfaceZ = WorldLocation.Z;
	if (bFloatOnWaterSurface)
	{
		if (const ASimCity2000CityActor* City = ResolveSourceCityActor())
		{
			uint8 TerrainClass = 0;
			float SampledZ = 0.0f;
			if (City->TryGetWaterGameplaySurface(WorldLocation, SampledZ, TerrainClass))
			{
				SurfaceZ = SampledZ;
			}
		}
	}

	const float ClampedSpread = FMath::Max(40.0f, SpreadRadiusCm);
	const float GoldenAngleRadians = 2.39996323f;
	int32 Spawned = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Angle = float(Index) * GoldenAngleRadians;
		const float Radius = ClampedSpread * FMath::Sqrt((float(Index) + 0.5f) / float(Count));
		FVector SpawnLocation = WorldLocation + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Radius;
		if (bFloatOnWaterSurface)
		{
			// Shoulders at the waterline: the capsule sits mostly under the surface.
			SpawnLocation.Z = SurfaceZ + 20.0f;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ASimCopterGroundAgent* Person = GetWorld()->SpawnActor<ASimCopterGroundAgent>(
			GroundAgentClass,
			SpawnLocation,
			FRotator(0.0f, RandomStream.FRandRange(0.0f, 360.0f), 0.0f),
			SpawnParams);
		if (Person == nullptr)
		{
			continue;
		}

		Person->InitialPersonState = SpawnMode;
		const FString MeshName = PedestrianMeshNames.Num() > 0
			? PedestrianMeshNames[RandomStream.RandRange(0, PedestrianMeshNames.Num() - 1)]
			: FString();
		Person->MissionEventId = EventId;
		Person->ConfigureAgent(
			ESimCopterGroundAgentKind::Pedestrian,
			MeshName,
			ActiveOriginalGameRootPath.IsEmpty() ? ResolveOriginalGameRoot() : ActiveOriginalGameRootPath,
			PedestrianSpeedCmPerSec);

		if (bRequireOriginalPopulationMeshes && !Person->IsUsingOriginalMesh())
		{
			UE_LOG(LogSimCopterTrafficSystem, Warning,
				TEXT("Discarding water-rescue survivor because original mesh '%s' could not be loaded."), *MeshName);
			Person->Destroy();
			continue;
		}

		// They tread water where they are: no ground snap (there is no walkable ground under
		// them) and no behaviour program, which is what the original's spawn-mode-1 people do
		// until the harness reaches them.
		Person->SetMissionScriptedMover();
		// Both callers are rescue victims with nowhere to go - treading water beside the capsized
		// boat, or stranded on the runaway train's roof - so they wave for the helicopter.
		Person->SetMissionAwaitingRescue(true);
		Person->SetActorLocation(SpawnLocation, false);
		PedestrianAgents.Add(Person);
		if (OutSpawned != nullptr)
		{
			OutSpawned->Add(Person);
		}
		Spawned++;
	}

	return Spawned;
}

ASimCopterGroundAgent* ASimCopterTrafficSystemActor::FindNearestBehaviorPerson(
	const ASimCopterGroundAgent& From,
	const int32 LoopFlagFilter,
	const int32 StateFilter) const
{
	// FUN_004ca350 walks the whole person array and keeps the closest match by Manhattan
	// distance in world units; the remake walks the pedestrian pool instead but keeps the same
	// filters and the same metric.
	const FVector FromLocation = From.GetActorLocation();
	ASimCopterGroundAgent* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();

	for (const TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : PedestrianAgents)
	{
		ASimCopterGroundAgent* Agent = AgentPtr.Get();
		if (Agent == nullptr || Agent == &From || Agent->IsActorBeingDestroyed() || !Agent->IsBehaviorActive())
		{
			continue;
		}
		// person+0x152: invisible people (riding a carrier) are never candidates.
		if (Agent->GetBehaviorAttribute(EBhavAttr::Visible) == 0)
		{
			continue;
		}
		if (LoopFlagFilter != -2 && int16(Agent->GetBehaviorAttribute(EBhavAttr::LoopFlag)) != int16(LoopFlagFilter))
		{
			continue;
		}
		if (StateFilter != -2 && int16(Agent->GetBehaviorAttribute(EBhavAttr::State)) != int16(StateFilter))
		{
			continue;
		}
		if (LoopFlagFilter == 0 && Agent->GetBehaviorAttribute(EBhavAttr::CriminalCaught) != 0)
		{
			continue;
		}

		const FVector Delta = FromLocation - Agent->GetActorLocation();
		const float Distance = FMath::Abs(Delta.X) + FMath::Abs(Delta.Y) + FMath::Abs(Delta.Z);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Agent;
		}
	}

	return Best;
}

ASimCopterGroundAgent* ASimCopterTrafficSystemActor::FindNearestServiceVehicleAgent(
	const FVector& FromWorldLocation,
	const int32 Service) const
{
	ASimCopterGroundAgent* Best = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();

	auto Consider = [&Best, &BestDistanceSq, &FromWorldLocation](ASimCopterGroundAgent* Candidate)
	{
		if (Candidate == nullptr || Candidate->IsActorBeingDestroyed())
		{
			return;
		}
		const float DistanceSq = FVector::DistSquared(FromWorldLocation, Candidate->GetActorLocation());
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			Best = Candidate;
		}
	};

	// The cop programs' service 3 is the speeder pool, not a fourth emergency service.
	if (Service == 3)
	{
		for (const TWeakObjectPtr<ASimCopterGroundAgent>& CarPtr : CriminalCars)
		{
			Consider(CarPtr.Get());
		}
		return Best;
	}

	if (Service < 0 || Service >= static_cast<int32>(SimCopterDispatch::EService::Count))
	{
		return nullptr;
	}
	for (const FSimCopterDispatchVehicle& Vehicle : DispatchVehicles[Service])
	{
		if (Vehicle.State == ESimCopterDispatchVehicleState::Empty)
		{
			continue;
		}
		Consider(Vehicle.Agent.Get());
	}
	return Best;
}

int32 ASimCopterTrafficSystemActor::RemoveMissionPeople(int32 EventId)
{
	if (EventId == INDEX_NONE)
	{
		return 0;
	}

	int32 Removed = 0;
	for (int32 Index = PedestrianAgents.Num() - 1; Index >= 0; --Index)
	{
		ASimCopterGroundAgent* Agent = PedestrianAgents[Index].Get();
		if (Agent == nullptr)
		{
			PedestrianAgents.RemoveAtSwap(Index);
			continue;
		}
		if (Agent->MissionEventId != EventId || Agent->IsMissionCarried())
		{
			continue;
		}
		Agent->Destroy();
		PedestrianAgents.RemoveAtSwap(Index);
		Removed++;
	}
	return Removed;
}

ASimCopterGroundAgent* ASimCopterTrafficSystemActor::SpawnFallingMissionPassengerAtWorldLocation(
	const FVector& WorldLocation,
	int32 EventId,
	int32 SpawnMode,
	int32 PersonState,
	float FallInjuryDistanceCm)
{
	if (GroundAgentClass == nullptr || GetWorld() == nullptr)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	const float SpawnYaw = RandomStream.FRandRange(0.0f, 360.0f);
	ASimCopterGroundAgent* Person = GetWorld()->SpawnActor<ASimCopterGroundAgent>(
		GroundAgentClass,
		WorldLocation,
		FRotator(0.0f, SpawnYaw, 0.0f),
		SpawnParams);
	if (Person == nullptr)
	{
		return nullptr;
	}

	Person->InitialPersonState = SpawnMode;
	if (PersonState != -1)
	{
		Person->SetInitialBehaviorClass(PersonState);
	}

	const FString MeshName = PedestrianMeshNames.Num() > 0 ? PedestrianMeshNames[RandomStream.RandRange(0, PedestrianMeshNames.Num() - 1)] : FString();
	Person->MissionEventId = EventId;
	Person->ConfigureAgent(
		ESimCopterGroundAgentKind::Pedestrian,
		MeshName,
		ActiveOriginalGameRootPath.IsEmpty() ? ResolveOriginalGameRoot() : ActiveOriginalGameRootPath,
		PedestrianSpeedCmPerSec);

	if (bRequireOriginalPopulationMeshes && !Person->IsUsingOriginalMesh())
	{
		UE_LOG(LogSimCopterTrafficSystem, Warning, TEXT("Discarding falling passenger because original mesh '%s' could not be loaded."), *MeshName);
		Person->Destroy();
		return nullptr;
	}

	if (SpawnMode == 6)
	{
		Person->SetMissionInjuredPose();
	}
	Person->BeginPassengerFall(EventId, FallInjuryDistanceCm);
	PedestrianAgents.Add(Person);
	return Person;
}

ASimCopterGroundAgent* ASimCopterTrafficSystemActor::SpawnScriptedMissionAgent(
	const FVector& FeetWorldLocation,
	int32 EventId,
	const FString& FigureName,
	bool bInjuredPose,
	float MovementSpeedScale)
{
	if (GroundAgentClass == nullptr || GetWorld() == nullptr)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ASimCopterGroundAgent* Person = GetWorld()->SpawnActor<ASimCopterGroundAgent>(
		GroundAgentClass, FeetWorldLocation, FRotator::ZeroRotator, SpawnParams);
	if (Person == nullptr)
	{
		return nullptr;
	}

	Person->MissionEventId = EventId;
	Person->InitialPersonState = bInjuredPose ? 6 : 0;
	if (!FigureName.IsEmpty())
	{
		Person->SetPedestrianFigureName(FigureName);
	}

	const FString MeshName = PedestrianMeshNames.Num() > 0 ? PedestrianMeshNames[RandomStream.RandRange(0, PedestrianMeshNames.Num() - 1)] : FString();
	Person->ConfigureAgent(
		ESimCopterGroundAgentKind::Pedestrian,
		MeshName,
		ActiveOriginalGameRootPath.IsEmpty() ? ResolveOriginalGameRoot() : ActiveOriginalGameRootPath,
		PedestrianSpeedCmPerSec * FMath::Max(0.1f, MovementSpeedScale));

	if (bRequireOriginalPopulationMeshes && !Person->IsUsingOriginalMesh())
	{
		Person->Destroy();
		return nullptr;
	}

	Person->SetMissionScriptedMover();
	// Stand the feet on the requested plane (scripted movers don't ground-snap).
	Person->SetActorLocation(FeetWorldLocation + FVector(0.0f, 0.0f, Person->GetCapsuleHalfHeightCm()), false);
	if (bInjuredPose)
	{
		Person->SetMissionInjuredPose();
	}
	return Person;
}

int32 ASimCopterTrafficSystemActor::ReleaseMissionPeopleNear(int32 EventId, const FVector& WorldLocation, int32 MaxCount, float RadiusCm, float MaxVerticalDeltaCm)
{
	if (MaxCount <= 0)
	{
		return 0;
	}

	struct FMissionReleaseCandidate
	{
		TWeakObjectPtr<ASimCopterGroundAgent> Agent;
		float DistanceSq = 0.0f;
	};

	TArray<FMissionReleaseCandidate, TInlineAllocator<16>> Candidates;
	const float RadiusSq = FMath::Square(RadiusCm);
	for (const TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : PedestrianAgents)
	{
		ASimCopterGroundAgent* Agent = AgentPtr.Get();
		if (Agent == nullptr || Agent->MissionEventId != EventId || Agent->IsActorBeingDestroyed() || Agent->IsMissionCarried())
		{
			continue;
		}

		const FVector AgentLocation = Agent->GetActorLocation();
		if (FMath::Abs(AgentLocation.Z - WorldLocation.Z) > MaxVerticalDeltaCm)
		{
			continue;
		}

		const float DistanceSq = FVector::DistSquared2D(AgentLocation, WorldLocation);
		if (DistanceSq > RadiusSq)
		{
			continue;
		}

		FMissionReleaseCandidate Candidate;
		Candidate.Agent = Agent;
		Candidate.DistanceSq = DistanceSq;
		Candidates.Add(Candidate);
	}

	Candidates.Sort([](const FMissionReleaseCandidate& Left, const FMissionReleaseCandidate& Right)
	{
		return Left.DistanceSq < Right.DistanceSq;
	});

	int32 Released = 0;
	for (const FMissionReleaseCandidate& Candidate : Candidates)
	{
		if (Released >= MaxCount)
		{
			break;
		}

		if (ASimCopterGroundAgent* Agent = Candidate.Agent.Get())
		{
			Agent->MissionEventId = INDEX_NONE;
			Agent->InitialPersonState = 0;
			Agent->SetInitialBehaviorClass(0);
			Agent->ClearMissionPose();
			Released++;
		}
	}

	return Released;
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
	PedestrianNodeIndexByTile.Reset();
	XbldTileIds.Reset();
	PeopleTileClasses.Reset();
	PeopleTerrainTypes.Reset();
	WaterTileFlags.Reset();
	VehicleAgents.Reset();
	PedestrianAgents.Reset();
	VehicleTrafficStates.Reset();
	LastAmbientScanTileX = INDEX_NONE;
	LastAmbientScanTileY = INDEX_NONE;

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

	// FUN_0047c0c0/FUN_004829f0 build the airport into the city before any cell is made, so the
	// grid this actor caches has to see the stamped block - twelve bare pads and a terminal -
	// rather than the SimCity 2000 airport the file was saved with. No corner grid here: this
	// actor takes its heights from ALTM, which FUN_004829f0 never touches.
	AirportOriginTile = SimCopterAirport::BuildAirportIntoCity(City, nullptr);

	const float HalfMapSize = FSimCity2000City::MapSize * ActiveTileSize * 0.5f;

	XbldTileIds.SetNum(FSimCity2000City::TileCount);
	ZoneTileIds.SetNum(FSimCity2000City::TileCount);
	PeopleTileClasses.SetNum(FSimCity2000City::TileCount);
	PeopleTerrainTypes.SetNum(FSimCity2000City::TileCount);
	WaterTileFlags.SetNum(FSimCity2000City::TileCount);
	TileCenterWorldZ.SetNum(FSimCity2000City::TileCount);

	for (int32 FileY = 0; FileY < FSimCity2000City::MapSize; ++FileY)
	{
		for (int32 FileX = 0; FileX < FSimCity2000City::MapSize; ++FileX)
		{
			const FSimCity2000Tile& Tile = City.Tiles[FileY * FSimCity2000City::MapSize + FileX];
			const int32 TileIndex = FileY * FSimCity2000City::MapSize + FileX;
			const int32 PeopleTileClass = FSimCopterPeopleCityRules::GetTileClassForBuildingId(Tile.Building);
			XbldTileIds[TileIndex] = Tile.Building;
			ZoneTileIds[TileIndex] = Tile.Zone;
			PeopleTileClasses[TileIndex] = uint8(PeopleTileClass);
			PeopleTerrainTypes[TileIndex] = GetPeopleTerrainTypeForAmbientGate(Tile);
			WaterTileFlags[TileIndex] = Tile.bWater ? 1 : 0;
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

				const int32 NodeIndex = PedestrianNodes.Num() - 1;
				for (int32 DeltaY = 0; DeltaY < SceneFootprint.Size; ++DeltaY)
				{
					for (int32 DeltaX = 0; DeltaX < SceneFootprint.Size; ++DeltaX)
					{
						PedestrianNodeIndexByTile.Add(FIntPoint(SceneFootprint.OriginX + DeltaX, SceneFootprint.OriginY + DeltaY), NodeIndex);
					}
				}
			}
		}
	}

	// AirportOriginTile was resolved above, before the grid was cached, because the stamp has to
	// land on the city data every one of these arrays is filled from.
	{
		if (SimCopterAirport::IsFallbackAirportOrigin(AirportOriginTile))
		{
			UE_LOG(
				LogSimCopterTrafficSystem,
				Display,
				TEXT("SimCopter airport: this city has no airport zone; the original builds one just past the map corner at (%d, %d)."),
				AirportOriginTile.X,
				AirportOriginTile.Y);
		}
		else
		{
			UE_LOG(
				LogSimCopterTrafficSystem,
				Display,
				TEXT("SimCopter airport: 4x4 block at (%d, %d); pad 0 at (%d, %d)."),
				AirportOriginTile.X,
				AirportOriginTile.Y,
				SimCopterAirport::GetPadTile(AirportOriginTile, 0).X,
				SimCopterAirport::GetPadTile(AirportOriginTile, 0).Y);
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

	RebuildDispatchStations();
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

// ---------------------------------------------------------------------------
// Emergency dispatch (F2-F5). Decoded in
// Docs/scratchpad/ghidra/emergency_dispatch_decode_20260725.md; the pure
// selection logic lives in Ground/SimCopterDispatch.h.
//
// Dispatched vehicles are deliberately kept out of VehicleAgents: the ambient
// pool prunes by distance from the camera and re-rolls random turns, both of
// which would break a unit that is meant to persist and drive a fixed route.
// The original ran them through the same mover but with their own state
// machines on top, which is what UpdateOneDispatchVehicle reproduces.
// ---------------------------------------------------------------------------

namespace
{
// The service each pool index belongs to, in EService order.
constexpr int32 DispatchServiceCount = static_cast<int32>(SimCopterDispatch::EService::Count);

const TCHAR* GetDispatchServiceName(SimCopterDispatch::EService Service)
{
	switch (Service)
	{
	case SimCopterDispatch::EService::FireTruck: return TEXT("Fire Truck");
	case SimCopterDispatch::EService::Police: return TEXT("Police");
	case SimCopterDispatch::EService::Ambulance: return TEXT("Ambulance");
	default: return TEXT("?");
	}
}

const TCHAR* GetDispatchStateName(ESimCopterDispatchVehicleState State)
{
	switch (State)
	{
	case ESimCopterDispatchVehicleState::Responding: return TEXT("responding");
	case ESimCopterDispatchVehicleState::Chasing: return TEXT("chasing");
	case ESimCopterDispatchVehicleState::OnScene: return TEXT("on scene");
	case ESimCopterDispatchVehicleState::Returning: return TEXT("returning");
	case ESimCopterDispatchVehicleState::Idle: return TEXT("idle");
	default: return TEXT("empty");
	}
}

// The body mesh IS the service's message id: FUN_0049dbb0 builds the render node from
// FUN_00470571(veh[0x14]), and veh[0x14] is 0x11c/0x11d/0x11f. Resolved against the
// shipped GEO tables those ids are CARFIRET "firetruk", CARPOLIC "popo" and CARAMBUL
// "amblance" (0x11e, CARROBBR "badguy", is the criminal car FUN_0049dab0 hunts).
// The 0x121/0x122/0x123 objects the constructors also load are AICON/PICON/FICON - the
// dispatch pylon icons on a second, initially hidden node at veh+0x13b, not the bodies.
const TCHAR* GetDispatchMeshName(SimCopterDispatch::EService Service)
{
	switch (Service)
	{
	case SimCopterDispatch::EService::FireTruck: return TEXT("CARFIRET");
	case SimCopterDispatch::EService::Police: return TEXT("CARPOLIC");
	case SimCopterDispatch::EService::Ambulance: return TEXT("CARAMBUL");
	default: return TEXT("");
	}
}
}

void ASimCopterTrafficSystemActor::RebuildDispatchStations()
{
	auto GetTileId = [this](int32 X, int32 Y) { return GetXbldTileId(X, Y); };

	for (int32 ServiceIndex = 0; ServiceIndex < DispatchServiceCount; ++ServiceIndex)
	{
		const SimCopterDispatch::EService Service = static_cast<SimCopterDispatch::EService>(ServiceIndex);
		SimCopterDispatch::ScanStations(GetTileId, Service, DispatchStations[ServiceIndex]);

		// A rebuild replaces the road graph the pool's routes point into, so any unit that
		// is still out has to go with it rather than be orphaned in the world.
		for (FSimCopterDispatchVehicle& Vehicle : DispatchVehicles[ServiceIndex])
		{
			if (ASimCopterGroundAgent* Agent = Vehicle.Agent.Get())
			{
				Agent->Destroy();
			}
		}

		// FUN_004bcc80 allocates the pool up front and leaves every slot empty.
		DispatchVehicles[ServiceIndex].Reset();
		DispatchVehicles[ServiceIndex].SetNum(SimCopterDispatch::VehiclesPerService);

		UE_LOG(
			LogSimCopterTrafficSystem,
			Display,
			TEXT("Dispatch: %s stations=%d (XBLD id 0x%02x)."),
			GetDispatchServiceName(Service),
			DispatchStations[ServiceIndex].Num(),
			SimCopterDispatch::GetServiceStationXbldId(Service));
	}
}

bool ASimCopterTrafficSystemActor::TryPlanRoadRoute(
	const FIntPoint& FromTile,
	const FIntPoint& ToTile,
	TArray<int32>& OutNodes) const
{
	OutNodes.Reset();
	if (RoadNodes.Num() == 0)
	{
		return false;
	}

	const int32* StartPtr = RoadNodeIndexByTile.Find(FromTile);
	const int32* GoalPtr = RoadNodeIndexByTile.Find(ToTile);
	if (StartPtr == nullptr || GoalPtr == nullptr)
	{
		return false;
	}

	const int32 Start = *StartPtr;
	const int32 Goal = *GoalPtr;
	if (Start == Goal)
	{
		OutNodes.Add(Goal);
		return true;
	}

	// Breadth-first over the road-tile graph. The original searched its coarser
	// intersection graph with Dijkstra (FUN_004bef30) and every edge there is a whole
	// road segment; over per-tile nodes with unit edges BFS gives the same shortest path.
	TArray<int32> Parent;
	Parent.Init(INDEX_NONE, RoadNodes.Num());
	TBitArray<> Visited(false, RoadNodes.Num());

	TArray<int32> Queue;
	Queue.Reserve(RoadNodes.Num());
	Queue.Add(Start);
	Visited[Start] = true;

	int32 Head = 0;
	bool bFound = false;
	while (Head < Queue.Num())
	{
		const int32 Current = Queue[Head++];
		if (Current == Goal)
		{
			bFound = true;
			break;
		}
		for (const int32 Neighbor : RoadNodes[Current].Neighbors)
		{
			if (!RoadNodes.IsValidIndex(Neighbor) || Visited[Neighbor])
			{
				continue;
			}
			Visited[Neighbor] = true;
			Parent[Neighbor] = Current;
			Queue.Add(Neighbor);
		}
	}

	if (!bFound)
	{
		return false;
	}

	for (int32 Node = Goal; Node != INDEX_NONE; Node = Parent[Node])
	{
		OutNodes.Add(Node);
		if (Node == Start)
		{
			break;
		}
	}
	Algo::Reverse(OutNodes);
	return OutNodes.Num() > 0;
}

namespace
{
// Adapter that hands SimCopterDispatch::Dispatch the two world queries it needs.
class FTrafficDispatchWorld final : public SimCopterDispatch::ISimCopterDispatchWorld
{
public:
	explicit FTrafficDispatchWorld(const ASimCopterTrafficSystemActor& InActor, TFunctionRef<bool(const FIntPoint&, const FIntPoint&)> InRoute)
		: Actor(InActor)
		, Route(InRoute)
	{
	}

	virtual int32 GetXbldTileId(int32 TileX, int32 TileY) const override
	{
		return Actor.GetXbldTileId(TileX, TileY);
	}

	virtual bool CanRouteBetween(const FIntPoint& FromRoadTile, const FIntPoint& ToRoadTile) const override
	{
		return Route(FromRoadTile, ToRoadTile);
	}

private:
	const ASimCopterTrafficSystemActor& Actor;
	TFunctionRef<bool(const FIntPoint&, const FIntPoint&)> Route;
};
}

bool ASimCopterTrafficSystemActor::TryGetDispatchVehicleTile(const FSimCopterDispatchVehicle& Vehicle, FIntPoint& OutTile) const
{
	const ASimCopterGroundAgent* Agent = Vehicle.Agent.Get();
	if (Agent == nullptr)
	{
		return false;
	}
	return TryGetPeopleTileCoordinateAtWorldLocation(Agent->GetActorLocation(), OutTile.X, OutTile.Y);
}

SimCopterDispatch::EDispatchResult ASimCopterTrafficSystemActor::RequestEmergencyDispatch(
	SimCopterDispatch::EService Service,
	const FIntPoint& TargetTile,
	bool bChaseSpotlight)
{
	const int32 ServiceIndex = static_cast<int32>(Service);
	if (ServiceIndex < 0 || ServiceIndex >= DispatchServiceCount)
	{
		return SimCopterDispatch::EDispatchResult::InvalidTarget;
	}

	TArray<SimCopterDispatch::FStation>& Stations = DispatchStations[ServiceIndex];
	TArray<FSimCopterDispatchVehicle>& Vehicles = DispatchVehicles[ServiceIndex];

	// Project the pool into the view FUN_004bc250 works from.
	TArray<SimCopterDispatch::FVehicleSlotView> Slots;
	Slots.SetNum(Vehicles.Num());
	for (int32 Index = 0; Index < Vehicles.Num(); ++Index)
	{
		const FSimCopterDispatchVehicle& Vehicle = Vehicles[Index];
		SimCopterDispatch::FVehicleSlotView& Slot = Slots[Index];
		Slot.bSpawned = Vehicle.State != ESimCopterDispatchVehicleState::Empty && Vehicle.Agent.IsValid();
		Slot.bIdle = Slot.bSpawned && Vehicle.State == ESimCopterDispatchVehicleState::Idle;
		if (Slot.bSpawned)
		{
			TryGetDispatchVehicleTile(Vehicle, Slot.Tile);
		}
	}

	auto RouteQuery = [this](const FIntPoint& From, const FIntPoint& To)
	{
		TArray<int32> Unused;
		return TryPlanRoadRoute(From, To, Unused);
	};
	const FTrafficDispatchWorld DispatchWorld(*this, RouteQuery);

	const SimCopterDispatch::FDispatchOutcome Outcome =
		SimCopterDispatch::Dispatch(DispatchWorld, Stations, Slots, TargetTile);

	if (Outcome.Result != SimCopterDispatch::EDispatchResult::Dispatched)
	{
		if (Outcome.Result == SimCopterDispatch::EDispatchResult::CannotReach)
		{
			UE_LOG(
				LogSimCopterTrafficSystem,
				Verbose,
				TEXT("Dispatch: %s cannot reach (%d,%d) - %s."),
				GetDispatchServiceName(Service),
				TargetTile.X,
				TargetTile.Y,
				Outcome.bNoRoadNearTarget
					? TEXT("no road within 4 tiles of the target")
					: TEXT("no unit could route to the snapped road tile"));
		}
		return Outcome.Result;
	}

	FSimCopterDispatchVehicle& Vehicle = Vehicles[Outcome.SlotIndex];

	if (!Outcome.bRedirectedExistingVehicle)
	{
		const SimCopterDispatch::FStation& Station = Stations[Outcome.StationIndex];
		ASimCopterGroundAgent* Agent = SpawnDispatchVehicleAgent(Service, Station.RoadTile);
		if (Agent == nullptr)
		{
			// The station's slot was already claimed by SimCopterDispatch::Dispatch; give it
			// back so a later request can use it (FUN_004bc660's role on failure).
			Stations[Outcome.StationIndex].Outstanding = FMath::Max(0, Stations[Outcome.StationIndex].Outstanding - 1);
			return SimCopterDispatch::EDispatchResult::NoUnitAvailable;
		}

		Vehicle = FSimCopterDispatchVehicle();
		Vehicle.Agent = Agent;
		Vehicle.HomeTile = Station.RoadTile;
		Vehicle.StationIndex = Outcome.StationIndex;
	}

	Vehicle.State = bChaseSpotlight
		? ESimCopterDispatchVehicleState::Chasing
		: ESimCopterDispatchVehicleState::Responding;
	Vehicle.StayTimerSeconds = 0.0f;
	Vehicle.ActionTimerSeconds = 0.0f;
	Vehicle.bActedAtScene = false;
	Vehicle.TargetEventId = INDEX_NONE;

	if (!TryRetargetDispatchVehicle(Vehicle, Outcome.DestinationTile))
	{
		// The candidate routed a moment ago, so this only trips when the vehicle is not on
		// a graph node yet; leave it responding and let the per-frame retarget pick it up.
		Vehicle.DestinationTile = Outcome.DestinationTile;
	}

	UE_LOG(
		LogSimCopterTrafficSystem,
		Verbose,
		TEXT("Dispatch: %s %s to (%d,%d)%s."),
		GetDispatchServiceName(Service),
		Outcome.bRedirectedExistingVehicle ? TEXT("redirected") : TEXT("launched"),
		Outcome.DestinationTile.X,
		Outcome.DestinationTile.Y,
		bChaseSpotlight ? TEXT(" [chase]") : TEXT(""));

	return SimCopterDispatch::EDispatchResult::Dispatched;
}

bool ASimCopterTrafficSystemActor::ClearEmergencyDispatch(SimCopterDispatch::EService Service, const FIntPoint& SpotlightTile)
{
	const int32 ServiceIndex = static_cast<int32>(Service);
	if (ServiceIndex < 0 || ServiceIndex >= DispatchServiceCount || !SimCopterDispatch::IsTileInBounds(SpotlightTile))
	{
		return false;
	}

	// FUN_0049b3f0 walks a radius-2 spiral and inspects the first vehicle it meets on each
	// tile. A vehicle of the wrong kind aborts the whole scan rather than being skipped, so
	// a fire truck parked between the spotlight and a police car blocks the release - that
	// is original behaviour, not an oversight. The original also aborted on an ambient car
	// (every car carries the same tile-object flag); the remake only considers dispatched
	// units, which makes the release slightly more forgiving than the original.
	auto TestTile = [this, ServiceIndex](const FIntPoint& Tile, bool& bOutAbort) -> FSimCopterDispatchVehicle*
	{
		bOutAbort = false;
		for (int32 OtherService = 0; OtherService < DispatchServiceCount; ++OtherService)
		{
			for (FSimCopterDispatchVehicle& Vehicle : DispatchVehicles[OtherService])
			{
				if (Vehicle.State == ESimCopterDispatchVehicleState::Empty || !Vehicle.Agent.IsValid())
				{
					continue;
				}
				FIntPoint VehicleTile;
				if (!TryGetDispatchVehicleTile(Vehicle, VehicleTile) || VehicleTile != Tile)
				{
					continue;
				}
				if (OtherService != ServiceIndex)
				{
					bOutAbort = true;
					return nullptr;
				}
				return &Vehicle;
			}
		}
		return nullptr;
	};

	FIntPoint Tile = SpotlightTile;
	bool bAbort = false;
	if (FSimCopterDispatchVehicle* Found = TestTile(Tile, bAbort))
	{
		RecallDispatchVehicle(*Found);
		return true;
	}
	if (bAbort)
	{
		return false;
	}

	SimCopterDispatch::FSpiralWalker Walker(SimCopterDispatch::ClearDispatchRadius);
	while (Walker.Step(Tile))
	{
		if (FSimCopterDispatchVehicle* Found = TestTile(Tile, bAbort))
		{
			RecallDispatchVehicle(*Found);
			return true;
		}
		if (bAbort)
		{
			return false;
		}
	}
	return false;
}

int32 ASimCopterTrafficSystemActor::GetDispatchStationCount(SimCopterDispatch::EService Service) const
{
	const int32 ServiceIndex = static_cast<int32>(Service);
	if (ServiceIndex < 0 || ServiceIndex >= DispatchServiceCount)
	{
		return 0;
	}
	return DispatchStations[ServiceIndex].Num();
}

int32 ASimCopterTrafficSystemActor::GetActiveDispatchCount(SimCopterDispatch::EService Service) const
{
	const int32 ServiceIndex = static_cast<int32>(Service);
	if (ServiceIndex < 0 || ServiceIndex >= DispatchServiceCount)
	{
		return 0;
	}

	int32 Count = 0;
	for (const FSimCopterDispatchVehicle& Vehicle : DispatchVehicles[ServiceIndex])
	{
		if (Vehicle.State != ESimCopterDispatchVehicleState::Empty && Vehicle.Agent.IsValid())
		{
			++Count;
		}
	}
	return Count;
}

FString ASimCopterTrafficSystemActor::GetDispatchStatusLine(SimCopterDispatch::EService Service) const
{
	const int32 ServiceIndex = static_cast<int32>(Service);
	if (ServiceIndex < 0 || ServiceIndex >= DispatchServiceCount)
	{
		return FString();
	}

	const int32 StationCount = DispatchStations[ServiceIndex].Num();
	if (StationCount == 0)
	{
		return FString::Printf(TEXT("no %s stations in this city"), GetDispatchServiceName(Service));
	}

	TArray<FString> StationParts;
	for (const SimCopterDispatch::FStation& Station : DispatchStations[ServiceIndex])
	{
		StationParts.Add(FString::Printf(
			TEXT("(%d,%d)road(%d,%d)out%d"),
			Station.Tile.X,
			Station.Tile.Y,
			Station.RoadTile.X,
			Station.RoadTile.Y,
			Station.Outstanding));
	}

	TArray<FString> Parts;
	for (const FSimCopterDispatchVehicle& Vehicle : DispatchVehicles[ServiceIndex])
	{
		if (Vehicle.State == ESimCopterDispatchVehicleState::Empty || !Vehicle.Agent.IsValid())
		{
			continue;
		}
		Parts.Add(FString::Printf(
			TEXT("%s->(%d,%d)"),
			GetDispatchStateName(Vehicle.State),
			Vehicle.DestinationTile.X,
			Vehicle.DestinationTile.Y));
	}

	if (Parts.Num() == 0)
	{
		return FString::Printf(TEXT("%d stations %s, no units out"), StationCount, *FString::Join(StationParts, TEXT(" ")));
	}
	return FString::Printf(
		TEXT("%d stations %s  |  %s"),
		StationCount,
		*FString::Join(StationParts, TEXT(" ")),
		*FString::Join(Parts, TEXT("  ")));
}

ASimCopterGroundAgent* ASimCopterTrafficSystemActor::SpawnDispatchVehicleAgent(
	SimCopterDispatch::EService Service,
	const FIntPoint& RoadTile)
{
	if (GetWorld() == nullptr || GroundAgentClass == nullptr)
	{
		return nullptr;
	}

	FVector SpawnBase = FVector::ZeroVector;
	if (!TryGetTileCenterWorldLocation(RoadTile.X, RoadTile.Y, SpawnBase))
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ASimCopterGroundAgent* Agent = GetWorld()->SpawnActor<ASimCopterGroundAgent>(
		GroundAgentClass,
		SpawnBase + FVector::UpVector * 100.0f,
		FRotator::ZeroRotator,
		SpawnParams);
	if (Agent == nullptr)
	{
		return nullptr;
	}

	// FUN_0049dbb0 is the shared placement for every vehicle class, so an emergency unit draws
	// its road speed from the same range an ambient car does. That is deliberate: a police car
	// cannot out-run a speeder at 1.75x, which is why the player has to slow one with the
	// searchlight before the chase can end.
	const float ServiceSpeedCmPerSec = DrawVehicleSpeedCmPerSec();
	FString MeshName = GetDispatchMeshName(Service);
	Agent->ConfigureAgent(
		ESimCopterGroundAgentKind::Vehicle,
		MeshName,
		ActiveOriginalGameRootPath.IsEmpty() ? ResolveOriginalGameRoot() : ActiveOriginalGameRootPath,
		ServiceSpeedCmPerSec);

	if (!Agent->IsUsingOriginalMesh() && VehicleMeshNames.Num() > 0)
	{
		// The named service body is missing from this GEO dump; fall back to an ambient car
		// so the dispatch is still visible and drivable.
		MeshName = VehicleMeshNames[RandomStream.RandRange(0, VehicleMeshNames.Num() - 1)];
		Agent->ConfigureAgent(
			ESimCopterGroundAgentKind::Vehicle,
			MeshName,
			ActiveOriginalGameRootPath.IsEmpty() ? ResolveOriginalGameRoot() : ActiveOriginalGameRootPath,
			ServiceSpeedCmPerSec);
	}

	Agent->SnapToGroundImmediate();

	const int32* NodeIndex = RoadNodeIndexByTile.Find(RoadTile);
	Agent->SetRouteState(NodeIndex != nullptr ? *NodeIndex : INDEX_NONE, INDEX_NONE);
	Agent->ClearMoveTarget();
	return Agent;
}

bool ASimCopterTrafficSystemActor::TryRetargetDispatchVehicle(FSimCopterDispatchVehicle& Vehicle, const FIntPoint& DestinationTile)
{
	FIntPoint FromTile;
	if (!TryGetDispatchVehicleTile(Vehicle, FromTile))
	{
		return false;
	}

	// The vehicle may sit on a non-road tile after a bump; snap it back onto the network
	// the same way the dispatcher snaps the requested target.
	auto GetTileId = [this](int32 X, int32 Y) { return GetXbldTileId(X, Y); };
	FIntPoint FromRoadTile = FromTile;
	if (!SimCopterDispatch::IsRoadTileId(GetXbldTileId(FromTile.X, FromTile.Y)))
	{
		if (!SimCopterDispatch::TryFindNearestRoadTile(GetTileId, FromTile, SimCopterDispatch::TargetRoadSnapRadius, FromRoadTile))
		{
			return false;
		}
	}

	TArray<int32> Route;
	if (!TryPlanRoadRoute(FromRoadTile, DestinationTile, Route))
	{
		return false;
	}

	Vehicle.DestinationTile = DestinationTile;
	Vehicle.RouteNodes = MoveTemp(Route);
	Vehicle.RouteCursor = 0;
	AdvanceDispatchRoute(Vehicle);
	return true;
}

void ASimCopterTrafficSystemActor::AdvanceDispatchRoute(FSimCopterDispatchVehicle& Vehicle)
{
	ASimCopterGroundAgent* Agent = Vehicle.Agent.Get();
	if (Agent == nullptr)
	{
		return;
	}

	while (Vehicle.RouteNodes.IsValidIndex(Vehicle.RouteCursor))
	{
		const int32 NodeIndex = Vehicle.RouteNodes[Vehicle.RouteCursor];
		if (!RoadNodes.IsValidIndex(NodeIndex))
		{
			++Vehicle.RouteCursor;
			continue;
		}

		const int32 PrevIndex = Vehicle.RouteCursor > 0 ? Vehicle.RouteNodes[Vehicle.RouteCursor - 1] : INDEX_NONE;
		const int32 NextIndex = Vehicle.RouteNodes.IsValidIndex(Vehicle.RouteCursor + 1)
			? Vehicle.RouteNodes[Vehicle.RouteCursor + 1]
			: INDEX_NONE;

		Agent->SetRouteState(NodeIndex, PrevIndex, NextIndex);
		Agent->SetMoveTarget(MakeVehicleRouteTargetLocation(RoadNodes, NodeIndex, PrevIndex, INDEX_NONE, NextIndex));
		return;
	}

	Agent->ClearMoveTarget();
}

bool ASimCopterTrafficSystemActor::HasDispatchVehicleArrived(const FSimCopterDispatchVehicle& Vehicle) const
{
	FIntPoint Tile;
	if (!TryGetDispatchVehicleTile(Vehicle, Tile))
	{
		return false;
	}
	// The original compares whole tile coordinates (veh[0x12d] == veh.tile), so the vehicle
	// counts as arrived as soon as it is standing on the destination square.
	return Tile == Vehicle.DestinationTile;
}

void ASimCopterTrafficSystemActor::RecallDispatchVehicle(FSimCopterDispatchVehicle& Vehicle)
{
	if (Vehicle.State == ESimCopterDispatchVehicleState::Empty || Vehicle.State == ESimCopterDispatchVehicleState::Idle)
	{
		// FUN_004bdc70 returns 0 for state 2 and leaves the vehicle alone.
		return;
	}

	Vehicle.State = ESimCopterDispatchVehicleState::Returning;
	Vehicle.StayTimerSeconds = 0.0f;
	Vehicle.ActionTimerSeconds = 0.0f;
	Vehicle.TargetEventId = INDEX_NONE;
	TryRetargetDispatchVehicle(Vehicle, Vehicle.HomeTile);
}

void ASimCopterTrafficSystemActor::ReleaseDispatchVehicle(SimCopterDispatch::EService Service, int32 SlotIndex)
{
	const int32 ServiceIndex = static_cast<int32>(Service);
	if (ServiceIndex < 0 || ServiceIndex >= DispatchServiceCount || !DispatchVehicles[ServiceIndex].IsValidIndex(SlotIndex))
	{
		return;
	}

	FSimCopterDispatchVehicle& Vehicle = DispatchVehicles[ServiceIndex][SlotIndex];

	// FUN_004bc660: give the station its slot back.
	if (DispatchStations[ServiceIndex].IsValidIndex(Vehicle.StationIndex))
	{
		SimCopterDispatch::FStation& Station = DispatchStations[ServiceIndex][Vehicle.StationIndex];
		Station.Outstanding = FMath::Max(0, Station.Outstanding - 1);
	}

	if (ASimCopterGroundAgent* Agent = Vehicle.Agent.Get())
	{
		Agent->Destroy();
	}

	// FUN_004bd5f0's teardown drops the marker with the vehicle.
	if (USimCopterDispatchMarkerComponent* Marker = Vehicle.Marker.Get())
	{
		Marker->DestroyComponent();
	}

	Vehicle = FSimCopterDispatchVehicle();
}

void ASimCopterTrafficSystemActor::UpdateDispatchMarker(
	const SimCopterDispatch::EService Service,
	FSimCopterDispatchVehicle& Vehicle)
{
	// FUN_004b9c00 / FUN_004babe0 re-link the marker on exactly `2 < state < 5`: it belongs to a
	// unit that is still on its way, and goes as soon as it arrives, parks or is recalled.
	const bool bWantMarker =
		(Vehicle.State == ESimCopterDispatchVehicleState::Responding ||
		 Vehicle.State == ESimCopterDispatchVehicleState::Chasing) &&
		SimCopterDispatch::IsTileInBounds(Vehicle.DestinationTile);

	if (!bWantMarker)
	{
		// FUN_004be820: unlink, keep the node.
		if (USimCopterDispatchMarkerComponent* Marker = Vehicle.Marker.Get())
		{
			Marker->Hide();
		}
		Vehicle.MarkerTile = FIntPoint(INDEX_NONE, INDEX_NONE);
		return;
	}

	USimCopterDispatchMarkerComponent* Marker = Vehicle.Marker.Get();
	if (Marker == nullptr)
	{
		Marker = NewObject<USimCopterDispatchMarkerComponent>(this);
		if (Marker == nullptr)
		{
			return;
		}
		Marker->SetOriginalGameRoot(ResolveOriginalGameRoot());
		Marker->SetupAttachment(GetRootComponent());
		Marker->RegisterComponent();
		Vehicle.Marker = Marker;
	}

	// FUN_004b9e40 re-reads the destination tile's cell every tick, so a chase unit's marker
	// tracks the spotlight without anything else having to notice the destination moved.
	if (Vehicle.MarkerTile != Vehicle.DestinationTile)
	{
		Vehicle.MarkerTile = Vehicle.DestinationTile;
	}

	FVector TileWorld = FVector::ZeroVector;
	if (!TryGetTileCenterWorldLocation(Vehicle.DestinationTile.X, Vehicle.DestinationTile.Y, TileWorld))
	{
		Marker->Hide();
		return;
	}

	// The original's +0xa0000 on the cell altitude: ten original units off the ground.
	TileWorld.Z += USimCopterDispatchMarkerComponent::MarkerHeightOriginalUnits * GetPeopleWorldCmPerOriginalUnit();

	if (!Marker->ShowAt(Service, TileWorld))
	{
		if (!Marker->GetLastLoadError().IsEmpty() && !bLoggedDispatchMarkerError)
		{
			bLoggedDispatchMarkerError = true;
			UE_LOG(LogSimCopterTrafficSystem, Warning,
				TEXT("Dispatch marker unavailable: %s"), *Marker->GetLastLoadError());
		}
		return;
	}

	// FUN_004be750, once per tick while the marker is up.
	Marker->StepSpin();
}

void ASimCopterTrafficSystemActor::SetSpotlightMarkSource(
	const FVector& GroundWorldLocation,
	const int32 Band,
	const bool bActive)
{
	SpotlightMarkWorldLocation = GroundWorldLocation;
	SpotlightMarkBand = Band;
	bSpotlightMarkActive = bActive;
}

bool ASimCopterTrafficSystemActor::TryActivateSpeederCar(
	const int32 EventId,
	const int32 TileX,
	const int32 TileY)
{
	if (GetWorld() == nullptr || GroundAgentClass == nullptr || RoadNodes.Num() == 0)
	{
		return false;
	}

	// FUN_004b8540 walks the pool for a slot whose flags & 2 is clear and gives up when there is
	// none. The pool is five deep, so five is a hard ceiling on live speeders.
	CriminalCars.RemoveAll([](const TWeakObjectPtr<ASimCopterGroundAgent>& Car) { return !Car.IsValid(); });
	if (CriminalCars.Num() >= SimCopterCriminalCar::PoolCapacity)
	{
		return false;
	}

	// FUN_0049cf10: a radius-5 spiral from the requested tile for a road tile it may sit on.
	auto GetTileId = [this](int32 X, int32 Y) { return GetXbldTileId(X, Y); };
	FIntPoint RoadTile = FIntPoint(INDEX_NONE, INDEX_NONE);
	if (!SimCopterDispatch::TryFindNearestRoadTile(GetTileId, FIntPoint(TileX, TileY), 5, RoadTile))
	{
		return false;
	}

	const int32 NodeIndex = FindNodeByTile(RoadNodeIndexByTile, RoadTile.X, RoadTile.Y);
	if (!RoadNodes.IsValidIndex(NodeIndex))
	{
		return false;
	}

	const int32 NextIndex = ChooseNextRouteNode(
		RoadNodes, NodeIndex, INDEX_NONE, RandomStream, TrafficAiMode == ESimCopterTrafficAiMode::Modernized);
	const FVector SpawnBase = MakeRoutePointLocation(RoadNodes, NodeIndex, INDEX_NONE, NextIndex, true);

	FRotator SpawnRotation = FRotator::ZeroRotator;
	if (RoadNodes.IsValidIndex(NextIndex))
	{
		const FVector Target = MakeRoutePointLocation(RoadNodes, NextIndex, NodeIndex, INDEX_NONE, true);
		SpawnRotation.Yaw = (Target - SpawnBase).Rotation().Yaw;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ASimCopterGroundAgent* Car = GetWorld()->SpawnActor<ASimCopterGroundAgent>(
		GroundAgentClass, SpawnBase + FVector::UpVector * 100.0f, SpawnRotation, SpawnParams);
	if (Car == nullptr)
	{
		return false;
	}

	// GEO object 0x11e is CARROBBR, table name "badguy" - the body the original loads for this
	// class, and the id FUN_0049dab0 tests for.
	// The speeder's base speed is drawn the same way every other car's is; the 1.75x it runs at
	// comes from the fleeing multiplier on top, not from a different base.
	Car->ConfigureAgent(
		ESimCopterGroundAgentKind::Vehicle,
		TEXT("CARROBBR"),
		ActiveOriginalGameRootPath.IsEmpty() ? ResolveOriginalGameRoot() : ActiveOriginalGameRootPath,
		DrawVehicleSpeedCmPerSec());
	Car->SnapToGroundImmediate();
	Car->MakeCriminalCar(EventId);

	if (RoadNodes.IsValidIndex(NextIndex))
	{
		const int32 PlannedNext = ChooseNextRouteNode(
			RoadNodes, NextIndex, NodeIndex, RandomStream, TrafficAiMode == ESimCopterTrafficAiMode::Modernized);
		Car->SetRouteState(NextIndex, NodeIndex, PlannedNext);
	}
	else
	{
		Car->SetRouteState(NodeIndex, INDEX_NONE);
	}

	// FUN_0049d980: a speeder runs at 1.75x until the searchlight finds it.
	Car->SetTrafficSpeedScale(SimCopterCriminalCar::GetFleeingSpeedMultiplier(true, 0, INDEX_NONE));

	CriminalCars.Add(Car);
	VehicleAgents.Add(Car);

	UE_LOG(LogSimCopterTrafficSystem, Display,
		TEXT("Speeder car for event %d placed on road tile (%d,%d); %d of %d in play."),
		EventId, RoadTile.X, RoadTile.Y, CriminalCars.Num(), SimCopterCriminalCar::PoolCapacity);
	return true;
}

// How fast a pulled-over car sheds its speed, in speed-scale per second. The original expresses
// this as a stop *distance* at veh[0xd3] that the mover consumes; a fixed decay reads the same
// on screen and does not need the mover's internals.
static constexpr float SpeederStopBrakeRate = 1.5f;

float ASimCopterTrafficSystemActor::DrawVehicleSpeedCmPerSec()
{
	// FUN_0049dbb0's two draws span 36..47 units/s together. The property carries the mean so it
	// stays tunable in the editor; the draw supplies the original's roughly +/-13% spread, which
	// is what stops a street of cars moving as one block.
	constexpr int32 MinUnits = SimCopterCriminalCar::RoadSpeedMinUnitsPerSecond;
	constexpr int32 MaxUnits = SimCopterCriminalCar::RoadSpeedMaxUnitsPerSecond;
	constexpr float MeanUnits = (MinUnits + MaxUnits) * 0.5f;
	const float Units = static_cast<float>(RandomStream.RandRange(MinUnits, MaxUnits));
	return VehicleSpeedCmPerSec * (Units / MeanUnits);
}

bool ASimCopterTrafficSystemActor::TryGetSpeederCarState(
	const int32 EventId,
	FVector& OutWorldLocation,
	int32& OutSpotlightMark,
	bool& OutStopped) const
{
	for (const TWeakObjectPtr<ASimCopterGroundAgent>& CarPtr : CriminalCars)
	{
		const ASimCopterGroundAgent* Car = CarPtr.Get();
		if (Car == nullptr || Car->GetCriminalEventId() != EventId)
		{
			continue;
		}
		OutWorldLocation = Car->GetActorLocation();
		OutSpotlightMark = Car->GetSpotlightMark();
		OutStopped = Car->GetCriminalState() ==
			static_cast<uint8>(SimCopterCriminalCar::EState::Arrested);
		return true;
	}
	return false;
}

void ASimCopterTrafficSystemActor::GetRecentlyStoppedSpeederLocations(TArray<FVector>& OutWorldLocations) const
{
	OutWorldLocations.Reset();
	for (const TWeakObjectPtr<ASimCopterGroundAgent>& CarPtr : CriminalCars)
	{
		const ASimCopterGroundAgent* Car = CarPtr.Get();
		if (Car == nullptr ||
			Car->GetCriminalState() != static_cast<uint8>(SimCopterCriminalCar::EState::Arrested))
		{
			continue;
		}
		// The hold counts down from ArrestHoldSeconds, so what is left tells us how long ago the
		// car stopped without needing a second timer.
		const float SinceStopped =
			SimCopterCriminalCar::ArrestHoldSeconds - Car->GetArrestHoldSeconds();
		if (SinceStopped <= SimCopterCriminalCar::ArrestTagLingerSeconds)
		{
			OutWorldLocations.Add(Car->GetActorLocation());
		}
	}
}

bool ASimCopterTrafficSystemActor::CanVehicleStopOnTile(const FIntPoint& Tile) const
{
	// FUN_0049df60's first test: never pull over on an intersection, because the car would block
	// the junction.
	if (SimCopterDispatch::IsIntersectionTileId(GetXbldTileId(Tile.X, Tile.Y)))
	{
		return false;
	}

	// ...and its last: no other stopped emergency vehicle already holds the tile.
	for (int32 ServiceIndex = 0; ServiceIndex < DispatchServiceCount; ++ServiceIndex)
	{
		for (const FSimCopterDispatchVehicle& Vehicle : DispatchVehicles[ServiceIndex])
		{
			if (Vehicle.State != ESimCopterDispatchVehicleState::OnScene)
			{
				continue;
			}
			const ASimCopterGroundAgent* Agent = Vehicle.Agent.Get();
			int32 AgentX = 0;
			int32 AgentY = 0;
			if (Agent != nullptr && Agent->TryGetTileCoordinate(AgentX, AgentY) &&
				FIntPoint(AgentX, AgentY) == Tile)
			{
				return false;
			}
		}
	}
	return true;
}

ASimCopterGroundAgent* ASimCopterTrafficSystemActor::FindPursuitTarget(const FIntPoint& FromTile) const
{
	// FUN_004b9e40 case 0: FUN_004beda0(3) rings, first object passing FUN_0049dab0 wins, and the
	// hit is only kept when FUN_0049b000 puts it inside three steps. Scanning the live speeder
	// list and applying the same step test gives the same answer without a per-tile object map.
	ASimCopterGroundAgent* Best = nullptr;
	int32 BestSteps = SimCopterCriminalCar::PursuitMaxTileSteps;
	for (const TWeakObjectPtr<ASimCopterGroundAgent>& CarPtr : CriminalCars)
	{
		ASimCopterGroundAgent* Car = CarPtr.Get();
		if (Car == nullptr)
		{
			continue;
		}
		if (!SimCopterCriminalCar::IsPursuitTarget(
				Car->IsCriminalCar() ? SimCopterCriminalCar::CriminalCarMessageId : 0,
				Car->IsFleeing()))
		{
			continue;
		}
		int32 CarX = 0;
		int32 CarY = 0;
		if (!Car->TryGetTileCoordinate(CarX, CarY))
		{
			continue;
		}
		const int32 Steps = SimCopterCriminalCar::GetTileStepDistance(FromTile, FIntPoint(CarX, CarY));
		if (Steps < BestSteps)
		{
			BestSteps = Steps;
			Best = Car;
		}
	}
	return Best;
}

void ASimCopterTrafficSystemActor::RunCriminalCarArrest(ASimCopterGroundAgent& Car, const float DeltaSeconds)
{
	// FUN_004b8630 case 3 counts veh[0x10] down and then hands over to FUN_004b8c90. Its other
	// exit, veh[8] != 0, cannot fire for a speeder - see SimCopterCriminalCar.h - so the timer is
	// the whole story. The mission was already paid out when the car stopped, so this only
	// clears the wreck and frees its pool slot.
	const float Remaining = Car.GetArrestHoldSeconds() - DeltaSeconds;
	Car.SetArrestHoldSeconds(Remaining);
	if (Remaining > 0.0f)
	{
		return;
	}

	CriminalCars.Remove(&Car);
	Car.Destroy();
}

void ASimCopterTrafficSystemActor::UpdateCriminalCars(const float DeltaSeconds)
{
	if (CriminalCars.Num() == 0)
	{
		return;
	}

	const float CmPerUnit = GetPeopleWorldCmPerOriginalUnit();
	const float MarkRadiusCm =
		SimCopterCriminalCar::GetSpotlightMarkRadiusOriginalUnits(SpotlightMarkBand) * CmPerUnit;

	for (int32 Index = CriminalCars.Num() - 1; Index >= 0; --Index)
	{
		ASimCopterGroundAgent* Car = CriminalCars[Index].Get();
		if (Car == nullptr)
		{
			CriminalCars.RemoveAt(Index);
			continue;
		}

		const SimCopterCriminalCar::EState State =
			static_cast<SimCopterCriminalCar::EState>(Car->GetCriminalState());
		if (State == SimCopterCriminalCar::EState::Arrested ||
			State == SimCopterCriminalCar::EState::Leaving)
		{
			// UpdateTrafficInteractions resets every vehicle to full speed each frame, so a
			// stopped car has to be pinned here every frame or it simply drives off again.
			Car->SetTrafficSpeedScale(0.0f);
			RunCriminalCarArrest(*Car, DeltaSeconds);
			continue;
		}

		// FUN_004a01f0. The distance is horizontal only - the beam's ground point against the
		// car's position, with height ignored.
		bool bLit = false;
		if (bSpotlightMarkActive && MarkRadiusCm > 0.0f)
		{
			const FVector CarLocation = Car->GetActorLocation();
			const FVector2D Flat(
				CarLocation.X - SpotlightMarkWorldLocation.X,
				CarLocation.Y - SpotlightMarkWorldLocation.Y);
			bLit = Flat.SizeSquared() <= MarkRadiusCm * MarkRadiusCm;
		}
		Car->SetSpotlightMark(SimCopterCriminalCar::AccumulateSpotlightMark(
			Car->GetSpotlightMark(), bLit, bSpotlightMarkActive));

		if (Car->IsStopOrdered())
		{
			// FUN_0049e0c0 sets a stop distance rather than halting outright, so the car coasts
			// down instead of dropping dead. The scale is carried on the agent because the
			// traffic pass overwrites TrafficSpeedScale every frame.
			const float Coast = FMath::Max(0.0f, Car->GetCriminalStopScale() - DeltaSeconds * SpeederStopBrakeRate);
			Car->SetCriminalStopScale(Coast);
			Car->SetTrafficSpeedScale(Coast);
			if (Coast > 0.0f)
			{
				continue;
			}

			// At rest: FUN_004b8b60's sequence.
			Car->MarkStopped();
			Car->SetFleeing(false);

			int32 CarX = 0;
			int32 CarY = 0;
			Car->TryGetTileCoordinate(CarX, CarY);

			// FUN_0049bd00(0xf, 0xd) puts the driver on the ground. Its return value is what
			// decides the whole outcome: succeed and the arrest runs to a proper completion,
			// fail and the record is retired silently with no payout.
			const bool bPersonPlaced = TrySpawnMissionPerson(
				SimCopterCriminalCar::ArrestPersonSpawnMode,
				SimCopterCriminalCar::ArrestPersonState,
				CarX,
				CarY,
				Car->GetCriminalEventId());

			if (bPersonPlaced)
			{
				Car->SetCriminalState(static_cast<uint8>(SimCopterCriminalCar::EState::Arrested));
				Car->SetArrestHoldSeconds(SimCopterCriminalCar::ArrestHoldSeconds);

				// DIVERGENCE: the original posts EVT_CriminalCaught from FUN_004b8c90, at the end
				// of the 120 s hold. Paying out here instead tells the player the job is done the
				// moment the car stops, rather than leaving them waiting two minutes with no
				// signal. The car still lingers for the hold, but nothing is owed on it.
				if (ASimCopterMissionSystemActor* Missions = ResolveMissionSystem())
				{
					Missions->ReportSpeederCarCaught(Car->GetCriminalEventId());
				}
				UE_LOG(LogSimCopterTrafficSystem, Display,
					TEXT("Speeder car for event %d pulled over at (%d,%d); mission paid out, car leaves in %.0fs."),
					Car->GetCriminalEventId(), CarX, CarY, SimCopterCriminalCar::ArrestHoldSeconds);
			}
			else
			{
				// FUN_004b8b60's `if (FUN_0049bd00(...) == 0)` branch: state 4, record retired.
				Car->SetCriminalState(static_cast<uint8>(SimCopterCriminalCar::EState::Leaving));
				Car->SetArrestHoldSeconds(0.0f);
				if (ASimCopterMissionSystemActor* Missions = ResolveMissionSystem())
				{
					Missions->ReportSpeederCarUnresolved(Car->GetCriminalEventId());
				}
				UE_LOG(LogSimCopterTrafficSystem, Warning,
					TEXT("Speeder car for event %d stopped at (%d,%d) but nobody could be put on the ground; retiring it."),
					Car->GetCriminalEventId(), CarX, CarY);
			}
			continue;
		}

		// FUN_0049d980: the mark decides how fast it can run.
		Car->SetTrafficSpeedScale(SimCopterCriminalCar::GetFleeingSpeedMultiplier(
			Car->IsFleeing(), Car->GetSpotlightMark(), SpotlightMarkBand));
	}
}

ASimCopterMissionSystemActor* ASimCopterTrafficSystemActor::ResolveMissionSystem() const
{
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ASimCopterMissionSystemActor> It(World); It; ++It)
		{
			return *It;
		}
	}
	return nullptr;
}

bool ASimCopterTrafficSystemActor::RunDispatchOnSceneAction(SimCopterDispatch::EService Service, FSimCopterDispatchVehicle& Vehicle)
{
	FIntPoint Tile;
	if (!TryGetDispatchVehicleTile(Vehicle, Tile))
	{
		return false;
	}

	ASimCopterMissionSystemActor* Missions = ResolveMissionSystem();

	switch (Service)
	{
	case SimCopterDispatch::EService::FireTruck:
	{
		// FUN_004b9890 scans five rings for something alight and FUN_004b9b10 stores the aim;
		// FUN_004b9790 then sprays emitter type 6 at it every frame. The truck itself never
		// extinguishes anything - each droplet douses where it lands, through the same impact
		// path (FUN_004a50c0) the helicopter's water uses. So "acted" here means "has a
		// target", not "put a fire out".
		if (Missions == nullptr)
		{
			return false;
		}

		ASimCopterMissionSystemActor::FServiceFireTarget Target;
		if (!Missions->TryAcquireServiceFireTarget(Tile, SimCopterDispatch::FireTargetScanRadius, Target))
		{
			Vehicle.bHasJetTarget = false;
			Vehicle.TargetTile = FIntPoint(INDEX_NONE, INDEX_NONE);
			UE_LOG(
				LogSimCopterTrafficSystem,
				Verbose,
				TEXT("Dispatch: fire truck at (%d,%d) found nothing alight within %d tiles."),
				Tile.X,
				Tile.Y,
				SimCopterDispatch::FireTargetScanRadius);
			return false;
		}

		Vehicle.bHasJetTarget = true;
		Vehicle.TargetWorld = Target.World;
		Vehicle.TargetTile = Target.Tile;
		Vehicle.TargetEventId = Target.EventId;
		UE_LOG(
			LogSimCopterTrafficSystem,
			Verbose,
			TEXT("Dispatch: fire truck at (%d,%d) is hosing the fire at (%d,%d)."),
			Tile.X,
			Tile.Y,
			Target.Tile.X,
			Target.Tile.Y);
		return true;
	}

	case SimCopterDispatch::EService::Police:
	{
		bool bActed = false;

		// FUN_004b9e40 case 0: sweep three rings around the car itself for a speeder, and order
		// the nearest one to pull over. The order only sticks against a car the player has held
		// the searchlight on - an unmarked speeder drives straight past.
		ASimCopterGroundAgent* Target = FindPursuitTarget(Tile);
		bool bTargetFleeing = false;
		if (Target != nullptr)
		{
			bTargetFleeing = Target->IsFleeing();
			int32 TargetX = 0;
			int32 TargetY = 0;
			Target->TryGetTileCoordinate(TargetX, TargetY);
			// FUN_0049df60 is asked of the *target* - "may that car stop where it is" - not of
			// the police car.
			if (CanVehicleStopOnTile(FIntPoint(TargetX, TargetY)) &&
				Target->TryOrderStop(SimCopterCriminalCar::PoliceCarMessageId))
			{
				Vehicle.TargetEventId = Target->GetCriminalEventId();
				Vehicle.TargetTile = FIntPoint(TargetX, TargetY);
				bActed = true;
				UE_LOG(LogSimCopterTrafficSystem, Verbose,
					TEXT("Dispatch: police at (%d,%d) ordered the speeder at (%d,%d) to pull over."),
					Tile.X, Tile.Y, TargetX, TargetY);
			}
		}

		// The jam clearance the help documents for a police dispatch ("You should also dispatch
		// police to help clear the traffic jam", 20ref.htm).
		if (Missions != nullptr)
		{
			FIntPoint JamTile;
			int32 EventId = INDEX_NONE;
			if (Missions->TryFindNearestJamTile(Tile, SimCopterDispatch::OnSceneScanRadius, JamTile, EventId))
			{
				Vehicle.TargetEventId = EventId;
				bActed |= Missions->ClearTrafficJamEvent(EventId);
			}
		}

		// FUN_0049bd00(0xe, personState): put an officer on the ground beside the car. The state
		// is 0xe when the car it just stopped was fleeing, 8 otherwise - and those are person
		// states, not spawn modes: 8 is BHAV 1401 "Cop foot", which runs 1150 "copf - chase
		// criminal", and 0xe is BHAV 1402 "Cop speeder", which walks up to the car it stopped.
		// The officer wears the original's "Kopp" figure so the player can tell them from the
		// civilians standing around.
		if (!Vehicle.bActedAtScene)
		{
			const int32 OfficerState = SimCopterCriminalCar::GetOfficerPersonState(Target != nullptr, bTargetFleeing);
			ASimCopterGroundAgent* Officer = nullptr;
			const bool bDeployed = TrySpawnMissionPerson(
				OfficerState,
				-1,
				Tile.X,
				Tile.Y,
				Vehicle.TargetEventId,
				SimCopterCriminalCar::OfficerFigureName,
				&Officer);
			if (bDeployed)
			{
				Vehicle.DeployedOfficer = Officer;
				Vehicle.bOfficerDeployed = true;
			}
			bActed |= bDeployed;
		}
		return bActed;
	}

	case SimCopterDispatch::EService::Ambulance:
	{
		// FUN_0049bd00(0xf, 0xd): deploy a paramedic. The original ambulance has no other
		// on-scene call - the crew's own behaviour program does the rest.
		if (Vehicle.bActedAtScene)
		{
			return false;
		}
		if (Missions != nullptr)
		{
			FIntPoint MedicalTile;
			int32 EventId = INDEX_NONE;
			if (Missions->TryFindNearestMedicalTile(Tile, SimCopterDispatch::OnSceneScanRadius, MedicalTile, EventId))
			{
				Vehicle.TargetEventId = EventId;
			}
		}
		return TrySpawnMissionPerson(0xf, 0xd, Tile.X, Tile.Y, Vehicle.TargetEventId);
	}

	default:
		return false;
	}
}

void ASimCopterTrafficSystemActor::UpdateOneDispatchVehicle(SimCopterDispatch::EService Service, int32 SlotIndex, float DeltaSeconds)
{
	const int32 ServiceIndex = static_cast<int32>(Service);
	FSimCopterDispatchVehicle& Vehicle = DispatchVehicles[ServiceIndex][SlotIndex];

	if (Vehicle.State == ESimCopterDispatchVehicleState::Empty)
	{
		return;
	}
	if (!Vehicle.Agent.IsValid())
	{
		// The agent was destroyed under us; free the station slot so the service does not
		// leak capacity.
		ReleaseDispatchVehicle(Service, SlotIndex);
		return;
	}

	// The original updates the marker inside this same tick, off the state it is about to act
	// on, so it appears the frame the unit is dispatched and clears the frame it arrives.
	UpdateDispatchMarker(Service, Vehicle);

	switch (Vehicle.State)
	{
	case ESimCopterDispatchVehicleState::Chasing:
	case ESimCopterDispatchVehicleState::Responding:
	{
		if (Vehicle.State == ESimCopterDispatchVehicleState::Chasing)
		{
			// FUN_004b9e40 case 2 sweeps three rings around the *spotlight* tile first: a
			// speeder found there is driven to directly, and only when there is none does the
			// unit fall back to the radius-4 road snap below.
			bool bChasingTarget = false;
			if (SimCopterDispatch::IsTileInBounds(SpotlightChaseTile))
			{
				if (ASimCopterGroundAgent* Target = FindPursuitTarget(SpotlightChaseTile))
				{
					int32 TargetX = 0;
					int32 TargetY = 0;
					if (Target->TryGetTileCoordinate(TargetX, TargetY))
					{
						const FIntPoint TargetTile(TargetX, TargetY);
						if (TargetTile != Vehicle.DestinationTile)
						{
							bChasingTarget = TryRetargetDispatchVehicle(Vehicle, TargetTile);
						}
						else
						{
							bChasingTarget = true;
						}

						// The chase issues the same stop order an arrived unit does, so a
						// marked speeder can be taken while the police car is still rolling.
						if (bChasingTarget &&
							SimCopterCriminalCar::GetTileStepDistance(Vehicle.DestinationTile, TargetTile)
								< SimCopterCriminalCar::PursuitMaxTileSteps &&
							CanVehicleStopOnTile(TargetTile))
						{
							Target->TryOrderStop(SimCopterCriminalCar::PoliceCarMessageId);
						}
					}
				}
			}

			// FUN_004b9e40 case 2's no-target path: re-read the spotlight tile every frame and
			// re-snap it to a road tile through the same radius-4 spiral the dispatcher uses.
			if (!bChasingTarget && SimCopterDispatch::IsTileInBounds(SpotlightChaseTile))
			{
				auto GetTileId = [this](int32 X, int32 Y) { return GetXbldTileId(X, Y); };
				FIntPoint ChaseRoadTile;
				if (SimCopterDispatch::TryFindNearestRoadTile(GetTileId, SpotlightChaseTile, SimCopterDispatch::TargetRoadSnapRadius, ChaseRoadTile)
					&& ChaseRoadTile != Vehicle.DestinationTile)
				{
					TryRetargetDispatchVehicle(Vehicle, ChaseRoadTile);
				}
			}
		}

		if (HasDispatchVehicleArrived(Vehicle))
		{
			if (Vehicle.State == ESimCopterDispatchVehicleState::Chasing)
			{
				// A chase unit parks on the spotlight tile but stays in chase mode so it
				// keeps following when the beam moves again.
				if (ASimCopterGroundAgent* Agent = Vehicle.Agent.Get())
				{
					Agent->ClearMoveTarget();
				}
				break;
			}
			Vehicle.State = ESimCopterDispatchVehicleState::OnScene;
			Vehicle.StayTimerSeconds = SimCopterDispatch::OnSceneStaySeconds;
			Vehicle.ActionTimerSeconds = 0.0f;
			if (ASimCopterGroundAgent* Agent = Vehicle.Agent.Get())
			{
				Agent->ClearMoveTarget();
			}
			break;
		}

		ASimCopterGroundAgent* Agent = Vehicle.Agent.Get();
		if (Agent != nullptr && !Agent->HasMoveTarget())
		{
			// Route exhausted without arriving (the destination sits mid-tile): re-plan.
			if (!TryRetargetDispatchVehicle(Vehicle, Vehicle.DestinationTile))
			{
				RecallDispatchVehicle(Vehicle);
			}
		}
		else if (Agent != nullptr && Agent->IsNearMoveTarget())
		{
			++Vehicle.RouteCursor;
			AdvanceDispatchRoute(Vehicle);
		}
		break;
	}

	case ESimCopterDispatchVehicleState::OnScene:
	{
		// FUN_004bd980 arms +0x2ad for the stay and retries the service call on the
		// +0x2a5 gap until the stay runs out.
		Vehicle.StayTimerSeconds -= DeltaSeconds;
		Vehicle.ActionTimerSeconds -= DeltaSeconds;

		// FUN_004b9790 sprays every frame it holds a target and only re-runs the five-ring
		// search when FUN_004a5ca0 says so - which it does on a 1-in-8 roll, or at once when
		// the flame it was aimed at has gone out. Nothing is extinguished here: the droplets
		// do that where they land.
		Vehicle.JetTimerSeconds -= DeltaSeconds;
		if (Service == SimCopterDispatch::EService::FireTruck
			&& Vehicle.bHasJetTarget
			&& Vehicle.JetTimerSeconds <= 0.0f
			&& Vehicle.Agent.IsValid())
		{
			// One droplet per game frame. Emitting per rendered frame instead would swamp the
			// 70-slot trajectory pool the player's bucket and cannon share.
			Vehicle.JetTimerSeconds = SimCopterDispatch::JetShotIntervalSeconds;

			if (ASimCopterMissionSystemActor* JetMissions = ResolveMissionSystem())
			{
				JetMissions->SpawnServiceWaterJet(Vehicle.Agent->GetActorLocation(), Vehicle.TargetWorld);
			}
			if (FMath::Rand() % 8 == 0)
			{
				// Re-acquire on the next update, so the truck follows a fire as it spreads
				// and moves on once the one it was hosing is out.
				Vehicle.ActionTimerSeconds = 0.0f;
			}
		}
		if (Vehicle.ActionTimerSeconds <= 0.0f)
		{
			const bool bActed = RunDispatchOnSceneAction(Service, Vehicle);
			if (bActed)
			{
				Vehicle.bActedAtScene = true;
				// FUN_004b9e40 holds for 0x780000 after a successful action.
				Vehicle.ActionTimerSeconds = (Service == SimCopterDispatch::EService::FireTruck)
					? SimCopterDispatch::JetRetargetSeconds
					: SimCopterDispatch::OnSceneHoldSeconds;
			}
			else
			{
				// A fire truck re-scans continuously in the original: FUN_004b9790 runs
				// FUN_004b9890's five-ring search every frame, and only the give-up timer
				// (+0x2ad) is long. Keeping the short cadence here is what lets a parked
				// truck pick up the next fire in range - one that spread, or a second
				// building that was already burning - instead of idling for half a minute.
				// Police and ambulances deploy their crew once, so they keep the long gap.
				Vehicle.ActionTimerSeconds = (Service == SimCopterDispatch::EService::FireTruck)
					? SimCopterDispatch::JetRetargetSeconds
					: SimCopterDispatch::OnSceneRetrySeconds;
			}
		}

		// Everyone aboard: leave. BHAV 1150/1152 (the officer) and BHAV 1060 (the criminal they
		// arrested) both end by walking to object class 11 - this car - and running opcode 40,
		// which hides them. That is the "gets in" the original never had to model, because its
		// people simply stopped existing. Waiting out the rest of the 180 s stay after that just
		// leaves an empty car parked at the scene.
		if (Vehicle.bOfficerDeployed)
		{
			const ASimCopterGroundAgent* Officer = Vehicle.DeployedOfficer.Get();
			const bool bOfficerAboard = Officer == nullptr || Officer->IsActorBeingDestroyed() || Officer->IsHidden();
			const ASimCopterGroundAgent* CarAgent = Vehicle.Agent.Get();
			// The same ten tiles both programs use for their class-11 probe: anyone still walking
			// in is still a passenger this car is waiting on.
			const float BoardingRadiusCm = GetPeopleWorldCmPerOriginalUnit() * 64.0f * 10.0f;
			if (bOfficerAboard &&
				(CarAgent == nullptr || !HasArrestedCriminalNear(CarAgent->GetActorLocation(), BoardingRadiusCm)))
			{
				Vehicle.bOfficerDeployed = false;
				Vehicle.DeployedOfficer.Reset();
				RecallDispatchVehicle(Vehicle);
				break;
			}
		}

		if (Vehicle.StayTimerSeconds <= 0.0f)
		{
			RecallDispatchVehicle(Vehicle);
		}
		break;
	}

	case ESimCopterDispatchVehicleState::Returning:
	{
		FIntPoint Tile;
		if (TryGetDispatchVehicleTile(Vehicle, Tile) && Tile == Vehicle.HomeTile)
		{
			ReleaseDispatchVehicle(Service, SlotIndex);
			return;
		}

		ASimCopterGroundAgent* Agent = Vehicle.Agent.Get();
		if (Agent != nullptr && !Agent->HasMoveTarget())
		{
			if (!TryRetargetDispatchVehicle(Vehicle, Vehicle.HomeTile))
			{
				// Cannot get home (the road was demolished): despawn rather than idle
				// forever holding the station's slot.
				ReleaseDispatchVehicle(Service, SlotIndex);
				return;
			}
		}
		else if (Agent != nullptr && Agent->IsNearMoveTarget())
		{
			++Vehicle.RouteCursor;
			AdvanceDispatchRoute(Vehicle);
		}
		break;
	}

	default:
		break;
	}
}

void ASimCopterTrafficSystemActor::UpdateDispatchVehicles(float DeltaSeconds)
{
	for (int32 ServiceIndex = 0; ServiceIndex < DispatchServiceCount; ++ServiceIndex)
	{
		const SimCopterDispatch::EService Service = static_cast<SimCopterDispatch::EService>(ServiceIndex);
		for (int32 SlotIndex = 0; SlotIndex < DispatchVehicles[ServiceIndex].Num(); ++SlotIndex)
		{
			UpdateOneDispatchVehicle(Service, SlotIndex, DeltaSeconds);
		}
	}
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

bool ASimCopterTrafficSystemActor::IsWaterTile(int32 FileX, int32 FileY) const
{
	if (WaterTileFlags.Num() != FSimCity2000City::TileCount ||
		FileX < 0 || FileX >= FSimCity2000City::MapSize ||
		FileY < 0 || FileY >= FSimCity2000City::MapSize)
	{
		return false;
	}

	return WaterTileFlags[FileY * FSimCity2000City::MapSize + FileX] != 0;
}

bool ASimCopterTrafficSystemActor::TryFindNearestTransportLandTile(
	int32 OriginX,
	int32 OriginY,
	int32& OutX,
	int32& OutY)
{
	auto IsUsableTransportLandTile = [this](int32 FileX, int32 FileY, bool bRequireEmptyTile) -> bool
	{
		if (FileX < 0 || FileX >= FSimCity2000City::MapSize ||
			FileY < 0 || FileY >= FSimCity2000City::MapSize ||
			IsWaterTile(FileX, FileY))
		{
			return false;
		}

		const uint8 BuildingId = uint8(GetXbldTileId(FileX, FileY));
		if (IsPedestrianRoadTile(BuildingId))
		{
			return false;
		}
		return !bRequireEmptyTile || BuildingId == 0;
	};

	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const bool bRequireEmptyTile = Pass == 0;
		for (int32 Ring = 0; Ring < FSimCity2000City::MapSize; ++Ring)
		{
			TArray<FIntPoint, TInlineAllocator<16>> Candidates;
			for (int32 OffsetY = -Ring; OffsetY <= Ring; ++OffsetY)
			{
				for (int32 OffsetX = -Ring; OffsetX <= Ring; ++OffsetX)
				{
					if (Ring > 0 && FMath::Abs(OffsetX) != Ring && FMath::Abs(OffsetY) != Ring)
					{
						continue;
					}

					const int32 FileX = OriginX + OffsetX;
					const int32 FileY = OriginY + OffsetY;
					if (IsUsableTransportLandTile(FileX, FileY, bRequireEmptyTile))
					{
						Candidates.Add(FIntPoint(FileX, FileY));
					}
				}
			}

			if (Candidates.Num() > 0)
			{
				const FIntPoint Pick = Candidates[RandomStream.RandRange(0, Candidates.Num() - 1)];
				OutX = Pick.X;
				OutY = Pick.Y;
				return true;
			}
		}
	}

	OutX = OriginX;
	OutY = OriginY;
	return false;
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

int32 ASimCopterTrafficSystemActor::GetXbldTileId(int32 FileX, int32 FileY) const
{
	if (XbldTileIds.Num() != FSimCity2000City::TileCount ||
		FileX < 0 || FileX >= FSimCity2000City::MapSize ||
		FileY < 0 || FileY >= FSimCity2000City::MapSize)
	{
		return 0;
	}

	return int32(XbldTileIds[FileY * FSimCity2000City::MapSize + FileX]);
}

int32 ASimCopterTrafficSystemActor::GetZoneTileId(int32 FileX, int32 FileY) const
{
	if (ZoneTileIds.Num() != FSimCity2000City::TileCount ||
		FileX < 0 || FileX >= FSimCity2000City::MapSize ||
		FileY < 0 || FileY >= FSimCity2000City::MapSize)
	{
		return 0;
	}

	return int32(ZoneTileIds[FileY * FSimCity2000City::MapSize + FileX]);
}

int32 ASimCopterTrafficSystemActor::GetBuildingFootprintSize(int32 FileX, int32 FileY) const
{
	return FMath::Max(1, FSimCopterPeopleCityRules::GetFootprintSizeForBuildingId(uint8(GetXbldTileId(FileX, FileY))));
}

void ASimCopterTrafficSystemActor::ClearXbldTiles(const TArray<FIntPoint>& Tiles)
{
	if (XbldTileIds.Num() != FSimCity2000City::TileCount)
	{
		return;
	}

	for (const FIntPoint& Tile : Tiles)
	{
		if (Tile.X < 0 || Tile.X >= FSimCity2000City::MapSize ||
			Tile.Y < 0 || Tile.Y >= FSimCity2000City::MapSize)
		{
			continue;
		}
		XbldTileIds[Tile.Y * FSimCity2000City::MapSize + Tile.X] = 0;
	}
}

ASimCity2000CityActor* ASimCopterTrafficSystemActor::GetCityActor() const
{
	return ResolveSourceCityActor();
}

bool ASimCopterTrafficSystemActor::TryGetAirportPadWorldLocation(int32 PadIndex, FVector& OutWorldLocation) const
{
	OutWorldLocation = FVector::ZeroVector;
	if (ActiveTileSize <= KINDA_SMALL_NUMBER || TileCenterWorldZ.Num() != FSimCity2000City::TileCount)
	{
		return false;
	}

	const FIntPoint PadTile = SimCopterAirport::GetPadTile(AirportOriginTile, PadIndex);
	if (PadTile.X == INDEX_NONE)
	{
		return false;
	}

	// FUN_004829f0 copies one height-map sample - the terminal's own tile - across the whole
	// block before it creates any cell, so every pad in an airport sits at exactly that height,
	// however the ground under it sloped beforehand. The fallback block is off the map entirely
	// and has no sample to read; clamping to the nearest real tile keeps it on the ground.
	const FIntPoint TerminalTile = SimCopterAirport::GetTerminalTile(AirportOriginTile);
	const int32 HeightX = FMath::Clamp(TerminalTile.X, 0, FSimCity2000City::MapSize - 1);
	const int32 HeightY = FMath::Clamp(TerminalTile.Y, 0, FSimCity2000City::MapSize - 1);

	// The XY formula is the one every tile uses and extrapolates past the map on its own, which
	// is what the fallback block needs.
	const float HalfMapSize = FSimCity2000City::MapSize * ActiveTileSize * 0.5f;
	const float LocalX = GetWorldTileCenterCoordinate(static_cast<float>(PadTile.X), ActiveTileSize, HalfMapSize);
	const float LocalY = -GetWorldTileCenterCoordinate(static_cast<float>(PadTile.Y), ActiveTileSize, HalfMapSize);

	OutWorldLocation = ActiveCityToWorldTransform.TransformPosition(FVector(LocalX, LocalY, 0.0f));
	OutWorldLocation.Z = TileCenterWorldZ[HeightY * FSimCity2000City::MapSize + HeightX];
	return true;
}

bool ASimCopterTrafficSystemActor::TryGetTileCenterWorldLocation(int32 FileX, int32 FileY, FVector& OutWorldLocation) const
{
	OutWorldLocation = FVector::ZeroVector;
	if (ActiveTileSize <= KINDA_SMALL_NUMBER ||
		FileX < 0 || FileX >= FSimCity2000City::MapSize ||
		FileY < 0 || FileY >= FSimCity2000City::MapSize)
	{
		return false;
	}

	const float HalfMapSize = FSimCity2000City::MapSize * ActiveTileSize * 0.5f;
	const float LocalX = GetWorldTileCenterCoordinate(static_cast<float>(FileX), ActiveTileSize, HalfMapSize);
	const float LocalY = -GetWorldTileCenterCoordinate(static_cast<float>(FileY), ActiveTileSize, HalfMapSize);
	const int32 TileIndex = FileY * FSimCity2000City::MapSize + FileX;
	const float WorldZ = TileCenterWorldZ.IsValidIndex(TileIndex) ? TileCenterWorldZ[TileIndex] : 0.0f;

	OutWorldLocation = ActiveCityToWorldTransform.TransformPosition(FVector(LocalX, LocalY, 0.0f));
	OutWorldLocation.Z = WorldZ;
	return true;
}

FVector ASimCopterTrafficSystemActor::ConvertOriginalOffsetToWorld(
	int32 X1616,
	int32 Y1616,
	int32 Z1616) const
{
	return ActiveCityToWorldTransform.TransformVector(
		SimCopterEffectFX::OriginalOffsetToCityLocalCm(X1616, Y1616, Z1616));
}

void ASimCopterTrafficSystemActor::ConvertWorldOffsetToOriginal(
	const FVector& WorldOffset,
	int32& OutX1616,
	int32& OutY1616,
	int32& OutZ1616) const
{
	// Undo the actor transform, then the (X, Y-up, Z) -> (-Z, -X, Y) axis mapping that
	// SimCopterEffectFX::OriginalOffsetToCityLocalCm applies.
	const FVector LocalCm = ActiveCityToWorldTransform.InverseTransformVector(WorldOffset);
	OutX1616 = static_cast<int32>(-LocalCm.Y / SimCopterEffectFX::Fixed1616ToCm);
	OutY1616 = static_cast<int32>(LocalCm.Z / SimCopterEffectFX::Fixed1616ToCm);
	OutZ1616 = static_cast<int32>(-LocalCm.X / SimCopterEffectFX::Fixed1616ToCm);
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
	if (PedestrianAgents.Num() < MaxPedestrianAgents && CountAmbientPedestrians() < OriginalAmbientRandomCap)
	{
		TryRunOriginalAmbientPedestrianScan(FocusLocation, SpawnAttemptsRemaining);
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

		// Anyone who belongs to a mission is not ambient population and is never culled for
		// distance: the mission layer owns them and takes them away itself (rescued, delivered,
		// killed, or removed with the vehicle they were on). Culling them stranded every victim
		// of a mission that starts away from the player - a train rescue puts its passengers on
		// a train that can be anywhere on the map, and they were being destroyed the same frame.
		if (Agent->MissionEventId != INDEX_NONE)
		{
			continue;
		}

		float ActiveDespawnRadiusCm = DespawnRadiusCm;
		if (Agent->GetAgentKind() == ESimCopterGroundAgentKind::Pedestrian)
		{
			ActiveDespawnRadiusCm = FMath::Max(1.0f, OriginalAmbientDespawnRadiusTiles * ActiveTileSize);
		}

		const float DistanceSq = FVector::DistSquared2D(Agent->GetActorLocation(), FocusLocation);
		if (DistanceSq > FMath::Square(ActiveDespawnRadiusCm))
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

bool ASimCopterTrafficSystemActor::IsMissionGroundSpawnValid(const FVector& SpawnLocation) const
{
	if (IsPedestrianSpawnLocationOpen(SpawnLocation))
	{
		return true;
	}

	// Road exception: injured people from car accidents may lie on the road surface even where the
	// open-space probe would reject the point.
	int32 FileX = INDEX_NONE;
	int32 FileY = INDEX_NONE;
	if (TryGetPeopleTileCoordinateAtWorldLocation(SpawnLocation, FileX, FileY))
	{
		return IsPedestrianRoadTile(uint8(GetXbldTileId(FileX, FileY)));
	}

	return false;
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

	if (RoadNodes.IsValidIndex(PreviousIndex) && RoadNodes[PreviousIndex].Neighbors.Num() == 1)
	{
		const FVector ToStart = SegmentStart - VehicleLocation;
		const FVector CrossDirection = GetRightHandLaneSideLocalDirection(SegmentDirection);
		const float RemainingCrossDistance = FVector::DotProduct(FVector(ToStart.X, ToStart.Y, 0.0f), CrossDirection);

		if (RemainingCrossDistance > ActiveTileSize * 0.15f)
		{
			OutTarget = SegmentStart;
			OutDistanceFromLane = 0.0f;
			bOutTraversingDiagonalRoad = true;
			return true;
		}
	}

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

int32 ASimCopterTrafficSystemActor::CountAmbientPedestrians() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : PedestrianAgents)
	{
		const ASimCopterGroundAgent* Agent = AgentPtr.Get();
		if (Agent != nullptr && Agent->MissionEventId == INDEX_NONE)
		{
			++Count;
		}
	}
	return Count;
}

bool ASimCopterTrafficSystemActor::TryResolvePedestrianNodeForTile(int32 TileX, int32 TileY, int32& OutNodeIndex) const
{
	OutNodeIndex = INDEX_NONE;
	if (const int32* Found = PedestrianNodeIndexByTile.Find(FIntPoint(TileX, TileY)))
	{
		OutNodeIndex = *Found;
		return PedestrianNodes.IsValidIndex(OutNodeIndex);
	}
	return false;
}

bool ASimCopterTrafficSystemActor::IsOriginalAmbientTileGateOpen(int32 TileX, int32 TileY) const
{
	if (PeopleTerrainTypes.Num() != FSimCity2000City::TileCount ||
		TileX < 0 || TileX >= FSimCity2000City::MapSize ||
		TileY < 0 || TileY >= FSimCity2000City::MapSize)
	{
		return false;
	}

	int32 NodeIndex = INDEX_NONE;
	if (!TryResolvePedestrianNodeForTile(TileX, TileY, NodeIndex))
	{
		return false;
	}

	const uint8 TerrainType = PeopleTerrainTypes[TileY * FSimCity2000City::MapSize + TileX];
	return IsOriginalAmbientTerrainType(TerrainType);
}

bool ASimCopterTrafficSystemActor::HasAmbientPedestrianNearTile(int32 TileX, int32 TileY, float RadiusTiles) const
{
	FVector TileCenter = FVector::ZeroVector;
	if (!TryGetTileCenterWorldLocation(TileX, TileY, TileCenter))
	{
		return false;
	}

	const float RadiusSq = FMath::Square(FMath::Max(0.5f, RadiusTiles) * ActiveTileSize);
	for (const TWeakObjectPtr<ASimCopterGroundAgent>& AgentPtr : PedestrianAgents)
	{
		const ASimCopterGroundAgent* Agent = AgentPtr.Get();
		if (Agent != nullptr &&
			Agent->MissionEventId == INDEX_NONE &&
			FVector::DistSquared2D(Agent->GetActorLocation(), TileCenter) <= RadiusSq)
		{
			return true;
		}
	}
	return false;
}

bool ASimCopterTrafficSystemActor::TryRunOriginalAmbientPedestrianScan(const FVector& FocusLocation, int32 MaxSpawnAttempts)
{
	if (GetWorld() == nullptr || GroundAgentClass == nullptr || PedestrianNodes.Num() == 0 || MaxSpawnAttempts <= 0)
	{
		return false;
	}
	if (PedestrianMeshNames.Num() == 0)
	{
		if (!bLoggedMissingPedestrianMeshes)
		{
			bLoggedMissingPedestrianMeshes = true;
			UE_LOG(LogSimCopterTrafficSystem, Log, TEXT("Pedestrian spawning is paused until original pedestrian model names/frames are decoded from X/privanim.df."));
		}
		return false;
	}
	if (CountAmbientPedestrians() > OriginalAmbientPeriodCap)
	{
		return false;
	}

	int32 FocusTileX = INDEX_NONE;
	int32 FocusTileY = INDEX_NONE;
	if (!TryGetPeopleTileCoordinateAtWorldLocation(FocusLocation, FocusTileX, FocusTileY))
	{
		return false;
	}
	if (FocusTileX == LastAmbientScanTileX && FocusTileY == LastAmbientScanTileY)
	{
		return false;
	}

	const int32 PreviousTileX = LastAmbientScanTileX;
	const int32 PreviousTileY = LastAmbientScanTileY;
	LastAmbientScanTileX = FocusTileX;
	LastAmbientScanTileY = FocusTileY;

	const int32 Radius = FMath::Clamp(OriginalAmbientScanRadiusTiles, 1, FSimCity2000City::MapSize);
	const bool bHasPreviousTile = PreviousTileX != INDEX_NONE && PreviousTileY != INDEX_NONE;
	const int32 DeltaX = bHasPreviousTile ? FocusTileX - PreviousTileX : 0;
	const int32 DeltaY = bHasPreviousTile ? FocusTileY - PreviousTileY : 0;
	bool bTouchedTile = false;

	auto TryTile = [this, &MaxSpawnAttempts, &bTouchedTile](int32 TileX, int32 TileY)
	{
		if (MaxSpawnAttempts <= 0 || CountAmbientPedestrians() > OriginalAmbientPeriodCap)
		{
			return;
		}
		bTouchedTile |= TryRunAmbientTileSpawn(TileX, TileY, 1, MaxSpawnAttempts);
	};

	for (int32 Y = FocusTileY - Radius; Y <= FocusTileY + Radius; ++Y)
	{
		for (int32 X = FocusTileX - Radius; X <= FocusTileX + Radius; ++X)
		{
			bool bIsNewlyExposed = false;
			if (!bHasPreviousTile || (DeltaX == 0 && DeltaY == 0))
			{
				// On initialization, only scan the perimeter to avoid popping people right next to the player.
				bIsNewlyExposed = (X == FocusTileX - Radius || X == FocusTileX + Radius || Y == FocusTileY - Radius || Y == FocusTileY + Radius);
			}
			else
			{
				// When moving, scan any tile that entered the radius (i.e. was not in the previous radius bounds).
				bIsNewlyExposed = (X < PreviousTileX - Radius || X > PreviousTileX + Radius || Y < PreviousTileY - Radius || Y > PreviousTileY + Radius);
			}

			if (bIsNewlyExposed)
			{
				TryTile(X, Y);
			}
		}
	}

	return bTouchedTile;
}

bool ASimCopterTrafficSystemActor::TryRunAmbientTileSpawn(int32 TileX, int32 TileY, int32 SpawnAttemptCount, int32& AttemptsRemaining)
{
	bool bSpawned = TrySpawnSpecialBuildingPeople(TileX, TileY, AttemptsRemaining) > 0;
	if (AttemptsRemaining <= 0 ||
		SpawnAttemptCount <= 0 ||
		CountAmbientPedestrians() >= FMath::Min(OriginalAmbientRandomCap, MaxPedestrianAgents))
	{
		return bSpawned;
	}

	constexpr int32 InitialAmbientDetail = 0x0c;
	const uint16 GateBound = uint16(FMath::Max(1, 0x0d - InitialAmbientDetail));
	if (FSimCopterPeopleCityRules::NextPeopleRandomBounded(PeopleRandomState, GateBound) >= 3)
	{
		return bSpawned;
	}
	if (!IsOriginalAmbientTileGateOpen(TileX, TileY))
	{
		return bSpawned;
	}

	while (SpawnAttemptCount-- > 0 &&
		AttemptsRemaining > 0 &&
		CountAmbientPedestrians() < FMath::Min(OriginalAmbientRandomCap, MaxPedestrianAgents))
	{
		--AttemptsRemaining;
		bSpawned |= TryGenericAmbientSpawnAtTile(TileX, TileY);
	}
	return bSpawned;
}

bool ASimCopterTrafficSystemActor::TryGenericAmbientSpawnAtTile(int32 TileX, int32 TileY)
{
	if (!IsOriginalAmbientTileGateOpen(TileX, TileY))
	{
		return false;
	}

	int32 NodeIndex = INDEX_NONE;
	if (!TryResolvePedestrianNodeForTile(TileX, TileY, NodeIndex))
	{
		return false;
	}

	const int32 TileClass = PedestrianNodes[NodeIndex].PeopleTileClass;
	const int32 BehaviorClass = FSimCopterPeopleCityRules::ChooseAmbientBehaviorClassForTileClass(TileClass, PeopleRandomState);
	if (BehaviorClass == INDEX_NONE)
	{
		return false;
	}

	return TrySpawnOriginalPersonAtTile(TileX, TileY, BehaviorClass, 0, INDEX_NONE, nullptr, INDEX_NONE);
}

int32 ASimCopterTrafficSystemActor::TrySpawnSpecialBuildingPeople(int32 TileX, int32 TileY, int32& AttemptsRemaining)
{
	if (AttemptsRemaining <= 0 ||
		TileX < 0 || TileX >= FSimCity2000City::MapSize ||
		TileY < 0 || TileY >= FSimCity2000City::MapSize)
	{
		return 0;
	}

	const uint8 BuildingId = uint8(GetXbldTileId(TileX, TileY));
	if (BuildingId != 0xD1 && BuildingId != 0xD2 && BuildingId != 0xD7 && BuildingId != 0xDB)
	{
		return 0;
	}
	if (HasAmbientPedestrianNearTile(TileX, TileY, float(FMath::Max(1, GetBuildingFootprintSize(TileX, TileY)))))
	{
		return 0;
	}

	int32 Spawned = 0;
	auto TryOne = [this, TileX, TileY, &AttemptsRemaining, &Spawned](
		int32 BehaviorClass,
		int32 InitialState,
		int32 ProgramId,
		const FVector2D* ExplicitOffset,
		int32 ClothesOffset)
	{
		if (AttemptsRemaining <= 0 || PedestrianAgents.Num() >= MaxPedestrianAgents)
		{
			return;
		}
		--AttemptsRemaining;
		if (TrySpawnOriginalPersonAtTile(TileX, TileY, BehaviorClass, InitialState, ProgramId, ExplicitOffset, ClothesOffset))
		{
			++Spawned;
		}
	};

	auto ChooseScriptedBehaviorClass = [this, TileX, TileY]() -> int32
	{
		int32 NodeIndex = INDEX_NONE;
		if (TryResolvePedestrianNodeForTile(TileX, TileY, NodeIndex))
		{
			const int32 Chosen = FSimCopterPeopleCityRules::ChooseAmbientBehaviorClassForTileClass(PedestrianNodes[NodeIndex].PeopleTileClass, PeopleRandomState);
			if (Chosen != INDEX_NONE)
			{
				return Chosen;
			}
		}
		return int32(FSimCopterPeopleCityRules::NextPeopleRandomBounded(PeopleRandomState, 10));
	};

	switch (BuildingId)
	{
	case 0xD1:
	case 0xD2:
	{
		int32 SpawnCount = 1;
		if (FSimCopterPeopleCityRules::NextPeopleRandomBounded(PeopleRandomState, uint16(65000 >> 2)) == 0)
		{
			SpawnCount = int32(FSimCopterPeopleCityRules::NextPeopleRandomBounded(PeopleRandomState, 0x001e)) + 1;
		}

		const int32 BehaviorClass = BuildingId == 0xD1 ? 0x0c : 0x0e;
		const int32 InitialState = BuildingId == 0xD1 ? 5 : 7;
		while (SpawnCount-- > 0)
		{
			TryOne(BehaviorClass, InitialState, INDEX_NONE, nullptr, INDEX_NONE);
		}
		break;
	}
	case 0xD7:
	{
		const int32 BatterClothesOffset = int32(FSimCopterPeopleCityRules::NextPeopleRandomBounded(PeopleRandomState, 10));
		const int32 TeamClothesOffset = int32(FSimCopterPeopleCityRules::NextPeopleRandomBounded(PeopleRandomState, 10));
		const int32 BehaviorClass = ChooseScriptedBehaviorClass();
		const FVector2D BatterOffset(70.0f, -70.0f);
		TryOne(BehaviorClass, 0, 0x04b5, &BatterOffset, BatterClothesOffset);

		const FVector2D FielderOffsets[] = {
			FVector2D(20.0f, -70.0f),
			FVector2D(20.0f, -20.0f),
			FVector2D(70.0f, -20.0f),
			FVector2D(45.0f, -45.0f),
			FVector2D(-40.0f, -40.0f),
			FVector2D(40.0f, 40.0f),
			FVector2D(-40.0f, 40.0f),
		};
		for (const FVector2D& Offset : FielderOffsets)
		{
			TryOne(BehaviorClass, 0, 0x04b6, &Offset, TeamClothesOffset);
		}
		break;
	}
	case 0xDB:
	{
		const int32 BehaviorClass = ChooseScriptedBehaviorClass();
		const FVector2D ParkOffset(8.0f, 32.0f);
		TryOne(BehaviorClass, 0, 0x04b2, &ParkOffset, INDEX_NONE);
		break;
	}
	default:
		break;
	}

	return Spawned;
}

bool ASimCopterTrafficSystemActor::TrySpawnOriginalPersonAtTile(
	int32 TileX,
	int32 TileY,
	int32 BehaviorClass,
	int32 InitialState,
	int32 InitialProgramId,
	const FVector2D* ExplicitOriginalOffset,
	int32 ClothesOffset)
{
	if (GetWorld() == nullptr || GroundAgentClass == nullptr || PedestrianAgents.Num() >= MaxPedestrianAgents)
	{
		return false;
	}

	int32 NodeIndex = INDEX_NONE;
	if (!TryResolvePedestrianNodeForTile(TileX, TileY, NodeIndex))
	{
		return false;
	}

	const FSimCopterGroundRouteNode& Node = PedestrianNodes[NodeIndex];
	FVector SpawnBaseLocation = Node.Location;
	bool bFoundSpawnLocation = false;

	if (ExplicitOriginalOffset != nullptr)
	{
		SpawnBaseLocation = Node.Location + MakePeopleSpawnOffsetWorldFromOriginalUnits(
			ActiveCityToWorldTransform,
			ActiveTileSize,
			*ExplicitOriginalOffset);
		bFoundSpawnLocation = IsPedestrianSpawnLocationOpen(SpawnBaseLocation);
	}
	else
	{
		for (int32 Attempt = 0; Attempt < 2; ++Attempt)
		{
			const FVector CandidateLocation = Node.Location + MakePeopleSpawnOffsetWorld(
				ActiveCityToWorldTransform,
				ActiveTileSize,
				Node.PeopleFootprintSize,
				Node.PeoplePlacementMode,
				PeopleRandomState);
			if (IsPedestrianSpawnLocationOpen(CandidateLocation))
			{
				SpawnBaseLocation = CandidateLocation;
				bFoundSpawnLocation = true;
				break;
			}
		}
	}

	if (!bFoundSpawnLocation)
	{
		return false;
	}

	int32 ResolvedBehaviorClass = BehaviorClass;
	if (ResolvedBehaviorClass == INDEX_NONE)
	{
		ResolvedBehaviorClass = FSimCopterPeopleCityRules::ChooseAmbientBehaviorClassForTileClass(Node.PeopleTileClass, PeopleRandomState);
		if (ResolvedBehaviorClass == INDEX_NONE)
		{
			return false;
		}
	}

	const FVector SpawnLocation = SpawnBaseLocation + FVector::UpVector * 92.0f;
	const FRotator SpawnRotation(0.0f, RandomStream.FRandRange(0.0f, 360.0f), 0.0f);
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ASimCopterGroundAgent* Agent = GetWorld()->SpawnActor<ASimCopterGroundAgent>(GroundAgentClass, SpawnLocation, SpawnRotation, SpawnParams);
	if (Agent == nullptr)
	{
		return false;
	}

	const FString MeshName = PedestrianMeshNames.Num() > 0
		? PedestrianMeshNames[RandomStream.RandRange(0, PedestrianMeshNames.Num() - 1)]
		: FString();

	Agent->InitialPersonState = FMath::Clamp(InitialState, 0, 20);
	Agent->SetInitialBehaviorClass(ResolvedBehaviorClass);
	if (InitialProgramId != INDEX_NONE)
	{
		Agent->SetInitialBehaviorProgramId(InitialProgramId);
	}
	if (ClothesOffset != INDEX_NONE)
	{
		Agent->SetPedestrianFigureClothesOffset(ClothesOffset);
	}

	Agent->ConfigureAgent(
		ESimCopterGroundAgentKind::Pedestrian,
		MeshName,
		ActiveOriginalGameRootPath.IsEmpty() ? ResolveOriginalGameRoot() : ActiveOriginalGameRootPath,
		PedestrianSpeedCmPerSec);

	if (bRequireOriginalPopulationMeshes && !Agent->IsUsingOriginalMesh())
	{
		UE_LOG(LogSimCopterTrafficSystem, Warning, TEXT("Discarding pedestrian population agent because original mesh '%s' could not be loaded."), *MeshName);
		Agent->Destroy();
		return false;
	}

	Agent->SnapToGroundImmediate();
	Agent->SetRouteState(NodeIndex, INDEX_NONE);
	Agent->ClearMoveTarget();
	PedestrianAgents.Add(Agent);
	return true;
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
		bVehicle ? DrawVehicleSpeedCmPerSec() : PedestrianSpeedCmPerSec);

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
	else if (Nodes.IsValidIndex(NextIndex) && NextIndex != PreviousIndex)
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

	FVector LaneSide = GetRightHandLaneSideLocalDirection(Direction);
	if (LaneSide.IsNearlyZero())
	{
		return Point.Location;
	}

	FVector PointLocalLocation = Point.LocalLocation;

	if (Point.Neighbors.Num() == 1)
	{
		const int32 NeighborIndex = Point.Neighbors[0];
		if (Nodes.IsValidIndex(NeighborIndex))
		{
			FVector DeadEndDir = Point.LocalLocation - Nodes[NeighborIndex].LocalLocation;
			DeadEndDir.Z = 0.0f;
			DeadEndDir = DeadEndDir.GetSafeNormal();

			PointLocalLocation += DeadEndDir * (ActiveTileSize * 0.35f);
		}
	}

	const FVector LaneLocalLocation = PointLocalLocation + LaneSide * (ActiveTileSize * VehicleLaneOffsetTileFraction);
	return ActiveCityToWorldTransform.TransformPosition(LaneLocalLocation);
}
