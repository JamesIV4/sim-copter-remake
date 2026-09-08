#pragma once

#include "CoreMinimal.h"

class ASimCopterHangar;
class ASimCopterHelicopterPawn;

namespace SimCopterHelicopterParking
{
SIMCOPTERREMAKE_API ASimCopterHelicopterPawn* ResolveCurrentAircraft(const UObject* WorldContext);
struct FPad
{
	int32 Index = INDEX_NONE;
	FVector Surface = FVector::ZeroVector;
};

// The eight edge-adjacent tiles around the 2x2 hangar, excluding the four airport corners.
SIMCOPTERREMAKE_API bool IsHangarPad(int32 PadIndex);
SIMCOPTERREMAKE_API void SortByDoorDistance(TArray<FPad>& Pads, const FVector& Door);
SIMCOPTERREMAKE_API bool OverlapsParkedAircraft(const FBox& Candidate, const TArray<FBox>& Occupants);

// Creates a separate, unpossessed airframe. No money/ownership changes on failure.
SIMCOPTERREMAKE_API ASimCopterHelicopterPawn* SpawnOnFreePad(
	ASimCopterHangar* Hangar, ASimCopterHelicopterPawn* Existing, int32 TypeIndex, FString& OutError);
}
