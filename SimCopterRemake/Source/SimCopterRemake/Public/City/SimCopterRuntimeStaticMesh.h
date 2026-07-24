// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

class UMaterialInterface;
class UStaticMesh;

// One material's worth of triangles for a runtime-built static mesh. The field list mirrors
// the city builder's own section struct so a model's geometry can be handed over unchanged.
struct SIMCOPTERREMAKE_API FSimCopterRuntimeMeshSection
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	// Atlas cell column/row for M_SimCopterCityAtlas. Empty for sections that do not use a
	// page atlas; those get zeroes, which their materials ignore.
	TArray<FVector2D> UV1;
	TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;
	TObjectPtr<UMaterialInterface> Material = nullptr;
};

namespace SimCopterRuntimeStaticMesh
{
	// Builds a UStaticMesh from already-triangulated sections, one material slot per section.
	//
	// Uses UStaticMesh::BuildFromMeshDescriptions' fast path, which copies the supplied
	// normals, tangents, colours and UVs straight into the render buffers - no reprocessing -
	// so a model built this way is vertex-for-vertex what the merged procedural mesh produced.
	//
	// bWithComplexCollision cooks the triangle mesh once and hangs it off the static mesh's
	// body setup as complex-as-simple. Every instance placed from this mesh then shares that
	// one cook, which is what makes per-building placement affordable.
	//
	// The returned mesh is transient and duplicate-transient. Its fast-built render data cannot
	// be copied safely into a PIE world, so the city actor deliberately rebuilds it there.
	//
	// Returns nullptr when the sections hold no triangles.
	SIMCOPTERREMAKE_API UStaticMesh* Build(
		UObject* Outer,
		const TArray<FSimCopterRuntimeMeshSection>& Sections,
		bool bWithComplexCollision);
}
