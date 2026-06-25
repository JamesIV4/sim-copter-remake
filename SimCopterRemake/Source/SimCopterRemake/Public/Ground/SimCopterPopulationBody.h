// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UProceduralMeshComponent;

// Builds the blocky, low-poly 3D "people" bodies that stand in for SimCopter's original
// crowd/pedestrian figures.
//
// SimCopter's people are not stored in the GEO mesh packs (those hold only vehicles,
// buildings, roads and props) - they are drawn at runtime from the articulated PrivAnim.df
// figures. Until those records are fully decoded, the remake reproduces the same flat-shaded,
// boxy "charm" with a procedurally-generated humanoid: stacked colored boxes for legs, torso,
// arms and head, with a few police/civilian outfit variants matched to the original art.
//
// The body is emitted into a UProceduralMeshComponent as a single vertex-colored section
// (feet at local Z=0, facing +X, total height HeightCm) and is meant to be drawn with the
// project's lit vertex-color material.
class FSimCopterPopulationBody
{
public:
	// Police uniform plus civilian colourways; matches the mix seen in the original crowds.
	static constexpr int32 OutfitCount = 6;

	// Stable per-agent outfit selection so a given pedestrian keeps the same clothes for its life.
	static int32 ResolveOutfitIndex(const UObject* StableObject);

	// True when the outfit wears a cap (police) - lets callers add matching props if desired.
	static bool OutfitHasHat(int32 OutfitIndex);

	static void BuildPerson(UProceduralMeshComponent* MeshComponent, int32 OutfitIndex, float HeightCm);
};
