// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterDispatch.h"

namespace SimCopterDispatch
{

// SCHOOK: IsRoadTileId 0x004bc110 (shared predicate, also inlined in FUN_004bc680)
bool IsRoadTileId(int32 XbldId)
{
	return (XbldId >= 0x1d && XbldId <= 0x2b)
		|| (XbldId >= 0x3f && XbldId <= 0x46)
		|| (XbldId >= 0x51 && XbldId <= 0x59);
}

// SCHOOK: IsIntersectionTileId 0x004bb900
bool IsIntersectionTileId(int32 XbldId)
{
	if (XbldId == 0x69)
	{
		return false;
	}
	return XbldId >= 0x27 && XbldId <= 0x2b;
}

// SCHOOK: TileCost 0x004bc250 (also FUN_004bf2c0 heuristic, FUN_0049b060)
int32 TileCost(const FIntPoint& A, const FIntPoint& B)
{
	const int32 Dx = FMath::Abs(A.X - B.X);
	const int32 Dy = FMath::Abs(A.Y - B.Y);
	// The original writes the two branches out longhand; both are "larger + (smaller >> 1)"
	// using an arithmetic shift, which never sees a negative here.
	return (Dy < Dx) ? Dx + (Dy >> 1) : Dy + (Dx >> 1);
}

bool IsTileInBounds(const FIntPoint& Tile)
{
	return Tile.X >= 0 && Tile.X < MapTiles && Tile.Y >= 0 && Tile.Y < MapTiles;
}

// SCHOOK: SpiralStep 0x004bedd0
bool FSpiralWalker::Step(FIntPoint& Tile)
{
	for (;;)
	{
		if (LegLength > MaxLegLength)
		{
			return false;
		}

		if (StepInLeg >= LegLength)
		{
			Direction = (Direction + 1) & 3;
			StepInLeg = 0;
			// The leg grows on the -Y and +Y legs only, which is what makes the walk a
			// square spiral rather than a diamond.
			if (Direction == 0 || Direction == 2)
			{
				++LegLength;
			}
		}

		switch (Direction)
		{
		case 0: --Tile.Y; break;
		case 1: ++Tile.X; break;
		case 2: ++Tile.Y; break;
		default: --Tile.X; break;
		}
		++StepInLeg;

		// The original keeps walking until it lands back inside the map, so tiles that
		// wander off the edge are skipped rather than ending the scan.
		if (IsTileInBounds(Tile))
		{
			return true;
		}
	}
}

bool TryFindNearestRoadTile(
	const TFunctionRef<int32(int32, int32)>& GetXbldTileId,
	const FIntPoint& Origin,
	int32 Radius,
	FIntPoint& OutTile)
{
	FIntPoint Tile = Origin;
	if (IsTileInBounds(Tile) && IsRoadTileId(GetXbldTileId(Tile.X, Tile.Y)))
	{
		OutTile = Tile;
		return true;
	}

	FSpiralWalker Walker(Radius);
	while (Walker.Step(Tile))
	{
		if (IsRoadTileId(GetXbldTileId(Tile.X, Tile.Y)))
		{
			OutTile = Tile;
			return true;
		}
	}
	return false;
}

int32 GetServiceMessageId(EService Service)
{
	switch (Service)
	{
	case EService::FireTruck: return MessageIdFire;
	case EService::Police: return MessageIdPolice;
	case EService::Ambulance: return MessageIdHospital;
	default: return 0;
	}
}

int32 GetServiceStationXbldId(EService Service)
{
	switch (Service)
	{
	case EService::FireTruck: return XbldFireStation;
	case EService::Police: return XbldPoliceStation;
	case EService::Ambulance: return XbldHospital;
	default: return 0;
	}
}

// SCHOOK: FindStationRoadAccess 0x004bc110
int32 FindStationRoadAccess(
	const TFunctionRef<int32(int32, int32)>& GetXbldTileId,
	const FIntPoint& StationTile,
	FIntPoint& OutRoadTile)
{
	// The starting direction is derived from the tile, not rolled: two stations on the
	// same diagonal always probe in the same order.
	int32 Direction = (StationTile.X + StationTile.Y) & 3;

	for (int32 Distance = StationRoadSearchMinDistance; Distance <= StationRoadSearchMaxDistance; ++Distance)
	{
		for (int32 Attempt = 0; Attempt < 4; ++Attempt)
		{
			FIntPoint Candidate = StationTile;
			switch (Direction)
			{
			case 0: Candidate.Y -= Distance; break;
			case 1: Candidate.X += Distance; break;
			case 2: Candidate.Y += Distance; break;
			default: Candidate.X -= Distance; break;
			}

			if (IsTileInBounds(Candidate) && IsRoadTileId(GetXbldTileId(Candidate.X, Candidate.Y)))
			{
				OutRoadTile = Candidate;
				return Direction;
			}

			Direction = (Direction + 1) & 3;
		}
	}

	return INDEX_NONE;
}

// SCHOOK: ScanStations 0x004bcc80
void ScanStations(
	const TFunctionRef<int32(int32, int32)>& GetXbldTileId,
	EService Service,
	TArray<FStation>& OutStations)
{
	OutStations.Reset();

	const int32 WantedId = GetServiceStationXbldId(Service);
	if (WantedId == 0)
	{
		return;
	}

	// The original copies the XBLD grid into a scratch buffer so it can blank the rest of
	// each 3x3 footprint as it goes; the remake keeps a consumed mask instead, which has
	// the same effect without touching the live grid.
	TBitArray<> Consumed(false, MapTiles * MapTiles);

	for (int32 Y = 0; Y < MapTiles; ++Y)
	{
		for (int32 X = 0; X < MapTiles; ++X)
		{
			const int32 Flat = Y * MapTiles + X;
			if (Consumed[Flat] || GetXbldTileId(X, Y) != WantedId)
			{
				continue;
			}

			// Blank the remaining tiles of this 3x3 building so it yields one record.
			for (int32 Dy = 0; Dy < StationFootprintTiles; ++Dy)
			{
				for (int32 Dx = 0; Dx < StationFootprintTiles; ++Dx)
				{
					const int32 Nx = X + Dx;
					const int32 Ny = Y + Dy;
					if (Nx < MapTiles && Ny < MapTiles)
					{
						Consumed[Ny * MapTiles + Nx] = true;
					}
				}
			}

			// The record is anchored on the centre of the footprint, not the corner the
			// scan matched.
			FStation Station;
			Station.Service = Service;
			Station.Tile = FIntPoint(X + 1, Y + 1);

			FIntPoint RoadTile;
			const int32 Direction = FindStationRoadAccess(GetXbldTileId, Station.Tile, RoadTile);
			if (Direction == INDEX_NONE)
			{
				// A station with no road access never enters the registry.
				continue;
			}

			Station.RoadTile = RoadTile;
			Station.Direction = Direction;
			OutStations.Add(Station);
		}
	}
}

// SCHOOK: BuildCandidates 0x004bc250
bool BuildCandidates(
	const TArray<FStation>& Stations,
	const TArray<FVehicleSlotView>& Slots,
	const FIntPoint& TargetTile,
	TArray<FCandidate>& OutCandidates,
	int32& OutFreeSlotIndex)
{
	OutCandidates.Reset();
	OutFreeSlotIndex = INDEX_NONE;

	for (int32 Index = 0; Index < Stations.Num(); ++Index)
	{
		const FStation& Station = Stations[Index];
		if (Station.Outstanding > 0)
		{
			continue;
		}

		FCandidate Candidate;
		Candidate.Cost = TileCost(Station.Tile, TargetTile);
		Candidate.Kind = ECandidateKind::Station;
		Candidate.Index = Index;
		OutCandidates.Add(Candidate);
	}

	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FVehicleSlotView& Slot = Slots[Index];
		if (!Slot.bSpawned)
		{
			// Not a candidate itself - this is the slot a station spawn will occupy. The
			// original keeps the last one it sees, so do the same.
			OutFreeSlotIndex = Index;
			continue;
		}

		if (!Slot.bIdle)
		{
			continue;
		}

		FCandidate Candidate;
		Candidate.Cost = TileCost(Slot.Tile, TargetTile);
		Candidate.Kind = ECandidateKind::Vehicle;
		Candidate.Index = Index;
		OutCandidates.Add(Candidate);
	}

	return OutCandidates.Num() > 0;
}

bool PopCheapestCandidate(TArray<FCandidate>& Candidates, FCandidate& OutCandidate)
{
	if (Candidates.Num() == 0)
	{
		return false;
	}

	int32 Best = 0;
	for (int32 Index = 1; Index < Candidates.Num(); ++Index)
	{
		if (Candidates[Index].Cost < Candidates[Best].Cost)
		{
			Best = Index;
		}
	}

	OutCandidate = Candidates[Best];
	Candidates.RemoveAt(Best);
	return true;
}

// SCHOOK: Dispatch 0x004bc680
FDispatchOutcome Dispatch(
	const ISimCopterDispatchWorld& World,
	TArray<FStation>& Stations,
	const TArray<FVehicleSlotView>& Slots,
	const FIntPoint& RequestedTile)
{
	FDispatchOutcome Outcome;

	// FUN_004be910 rejects out-of-range tiles before the manager is ever touched.
	if (!IsTileInBounds(RequestedTile))
	{
		Outcome.Result = EDispatchResult::InvalidTarget;
		return Outcome;
	}

	auto GetTileId = [&World](int32 X, int32 Y) { return World.GetXbldTileId(X, Y); };

	TArray<FCandidate> Candidates;
	int32 FreeSlotIndex = INDEX_NONE;
	if (!BuildCandidates(Stations, Slots, RequestedTile, Candidates, FreeSlotIndex))
	{
		Outcome.Result = EDispatchResult::NoUnitAvailable;
		return Outcome;
	}

	FIntPoint DestinationTile;
	if (!TryFindNearestRoadTile(GetTileId, RequestedTile, TargetRoadSnapRadius, DestinationTile))
	{
		Outcome.Result = EDispatchResult::CannotReach;
		Outcome.bNoRoadNearTarget = true;
		return Outcome;
	}
	Outcome.DestinationTile = DestinationTile;

	FCandidate Candidate;
	while (PopCheapestCandidate(Candidates, Candidate))
	{
		if (Candidate.Kind == ECandidateKind::Station)
		{
			// A station with no free pool slot cannot launch anything; the original skips
			// it (manager[0x30] == 32000 -> `continue`) rather than failing the request.
			if (FreeSlotIndex == INDEX_NONE)
			{
				continue;
			}

			const FStation& Station = Stations[Candidate.Index];
			if (!World.CanRouteBetween(Station.RoadTile, DestinationTile))
			{
				continue;
			}

			Stations[Candidate.Index].Outstanding++;

			Outcome.Result = EDispatchResult::Dispatched;
			Outcome.SlotIndex = FreeSlotIndex;
			Outcome.StationIndex = Candidate.Index;
			Outcome.DestinationTile = DestinationTile;
			Outcome.bRedirectedExistingVehicle = false;
			return Outcome;
		}

		const FVehicleSlotView& Slot = Slots[Candidate.Index];
		if (!World.CanRouteBetween(Slot.Tile, DestinationTile))
		{
			continue;
		}

		Outcome.Result = EDispatchResult::Dispatched;
		Outcome.SlotIndex = Candidate.Index;
		Outcome.StationIndex = INDEX_NONE;
		Outcome.DestinationTile = DestinationTile;
		Outcome.bRedirectedExistingVehicle = true;
		return Outcome;
	}

	Outcome.Result = EDispatchResult::CannotReach;
	return Outcome;
}

} // namespace SimCopterDispatch
