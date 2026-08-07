// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterRoadDecorations.h"

#include "Formats/MaxisMeshReader.h"
#include "Ground/SimCopterEffectFX.h"

namespace SimCopterRoadDecorations
{
EStreetFurniture GetStreetFurnitureForRoll(const int32 Roll)
{
	switch (Roll & 0xf)
	{
	case 0:
	case 1:
	case 2:
		return EStreetFurniture::FireHydrant;
	case 3:
	case 4:
	case 5:
		return EStreetFurniture::PhoneBooth;
	case 6:
	case 7:
	case 8:
		return EStreetFurniture::MailBox;
	default:
		return EStreetFurniture::TrashCan;
	}
}

int32 GetStreetFurnitureObjectId(const EStreetFurniture Kind, const bool bEastWestRoad)
{
	switch (Kind)
	{
	case EStreetFurniture::FireHydrant:
		return bEastWestRoad ? FireHydrant30ObjectId : FireHydrant29ObjectId;
	case EStreetFurniture::PhoneBooth:
		return bEastWestRoad ? Phone30ObjectId : Phone29ObjectId;
	case EStreetFurniture::MailBox:
		return bEastWestRoad ? Mail30ObjectId : Mail29ObjectId;
	case EStreetFurniture::TrashCan:
	default:
		return bEastWestRoad ? Trash30ObjectId : Trash29ObjectId;
	}
}

int32 GetRoadDecorationObjectId(
	const uint8 BuildingId,
	const int32 TileX,
	const int32 TileY,
	const bool bTileIsFlat,
	const int32 FurnitureRoll)
{
	// Every decoration case sits inside the four-corner flatness test, so a road running up a
	// grade carries nothing at all.
	if (!bTileIsFlat)
	{
		return INDEX_NONE;
	}

	const bool bOddTile = (TileX & 1) != 0 && (TileY & 1) != 0;

	switch (BuildingId)
	{
	// Straight roads. The two ids are the two orientations, and each has its own set of four props.
	case 0x1d:
		return bOddTile
			? GetStreetFurnitureObjectId(GetStreetFurnitureForRoll(FurnitureRoll), /*bEastWestRoad=*/false)
			: INDEX_NONE;
	case 0x1e:
		return bOddTile
			? GetStreetFurnitureObjectId(GetStreetFurnitureForRoll(FurnitureRoll), /*bEastWestRoad=*/true)
			: INDEX_NONE;

	// T junctions take a street light, one lamp per junction orientation, and only on every fourth
	// tile in both axes - which is what keeps a city from growing a lamp on every corner.
	case 0x23:
	case 0x24:
	case 0x25:
	case 0x26:
		return ((TileX & 3) == 3 && (TileY & 3) == 3)
			? Lamp35ObjectId + (static_cast<int32>(BuildingId) - 0x23)
			: INDEX_NONE;

	// Crossroads take a signal on odd/odd...
	case 0x27:
	case 0x28:
	case 0x29:
	case 0x2a:
		return bOddTile ? Signal1ObjectId : INDEX_NONE;

	// ...except 0x2b, the full four-way, which takes one unconditionally.
	case 0x2b:
		return Signal1ObjectId;

	default:
		return INDEX_NONE;
	}
}

int32 MakeStreetFurnitureRoll(const int32 TileX, const int32 TileY, const int32 CitySeed)
{
	// Any well-mixed hash does; this is the usual 32-bit finaliser so neighbouring tiles do not
	// correlate and a street does not come out as one repeated prop.
	uint32 Hash = static_cast<uint32>(TileX) * 0x9e3779b9u ^
		static_cast<uint32>(TileY) * 0x85ebca6bu ^
		static_cast<uint32>(CitySeed) * 0xc2b2ae35u;
	Hash ^= Hash >> 16;
	Hash *= 0x7feb352du;
	Hash ^= Hash >> 15;
	return static_cast<int32>(Hash & 0xf);
}

bool IsTrafficSignalObjectId(const int32 ObjectId)
{
	return ObjectId == Signal1ObjectId;
}

bool IsStreetLightObjectId(const int32 ObjectId)
{
	return ObjectId >= Lamp35ObjectId && ObjectId <= Lamp38ObjectId;
}

bool TryGetStreetLightEmitter(
	const FMaxisMeshObject& Object,
	const float ModelUnitsPerCentimeter,
	const float ModelScale,
	const bool bApplyCityMeshOrientation,
	FStreetLightEmitter& OutEmitter)
{
	// Collect the painted cone. Three stacked bands of six quads hang under the head and a wide
	// 14-vertex pool lies on the ground; between them they give the apex, the throw and the spread.
	TArray<FVector> CardPoints;
	for (const FMaxisMeshFace& Face : Object.Faces)
	{
		if (Face.FaceType != LightCardFaceType)
		{
			continue;
		}
		for (const uint16 VertexIndex : Face.VertexIndices)
		{
			if (!Object.Vertices.IsValidIndex(VertexIndex))
			{
				continue;
			}
			FVector Point = FMaxisMeshReader::ConvertMaxisVertexToUnreal(
				Object.Vertices[VertexIndex], ModelUnitsPerCentimeter) * ModelScale;
			if (bApplyCityMeshOrientation)
			{
				Point = SimCopterEffectFX::ApplyCityMeshOrientation(Point);
			}
			CardPoints.Add(Point);
		}
	}

	if (CardPoints.Num() < 2)
	{
		return false;
	}

	float TopZ = -TNumericLimits<float>::Max();
	float BottomZ = TNumericLimits<float>::Max();
	for (const FVector& Point : CardPoints)
	{
		TopZ = FMath::Max(TopZ, Point.Z);
		BottomZ = FMath::Min(BottomZ, Point.Z);
	}

	const float ConeLength = TopZ - BottomZ;
	if (ConeLength <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// The apex is where the cards start, and the lamp arm reaches out over the road, so its XY has
	// to come from the top band rather than from the pole or from the object's centre. Anything
	// within the top tenth of the cone counts as the top band.
	const float TopBandZ = TopZ - ConeLength * 0.1f;
	FVector ApexAccumulator = FVector::ZeroVector;
	int32 ApexCount = 0;
	// ...and the spread is how far the pool reaches from that apex.
	const float PoolBandZ = BottomZ + ConeLength * 0.1f;
	float PoolRadius = 0.0f;
	for (const FVector& Point : CardPoints)
	{
		if (Point.Z >= TopBandZ)
		{
			ApexAccumulator += FVector(Point.X, Point.Y, 0.0f);
			++ApexCount;
		}
	}
	if (ApexCount == 0)
	{
		return false;
	}
	const FVector ApexXY = ApexAccumulator / static_cast<float>(ApexCount);
	for (const FVector& Point : CardPoints)
	{
		if (Point.Z <= PoolBandZ)
		{
			PoolRadius = FMath::Max(PoolRadius, FVector::Dist2D(Point, ApexXY));
		}
	}

	OutEmitter.LocalOffset = FVector(ApexXY.X, ApexXY.Y, TopZ);
	OutEmitter.ConeLengthCm = ConeLength;
	OutEmitter.ConeHalfAngleDegrees = FMath::RadiansToDegrees(FMath::Atan2(PoolRadius, ConeLength));
	return true;
}
}
