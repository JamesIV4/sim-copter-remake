// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Formats/MaxisMeshReader.h"
#include "ProceduralMeshComponent.h"

// Triangulated, vertex-colored geometry for a single Maxis mesh object, ready to
// hand to UProceduralMeshComponent::CreateMeshSection_LinearColor. The helicopter
// fuselage and rotor objects in the SimCopter GEO packs are all palette-colored
// (no textured faces), so this single-section builder is sufficient for them.
struct FMaxisMeshSection
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;

	// Local-space bounds of the built geometry (after unit/scale conversion).
	FBox LocalBounds = FBox(ForceInit);

	bool IsEmpty() const { return Vertices.Num() == 0 || Triangles.Num() < 3; }

	void Reset()
	{
		Vertices.Reset();
		Triangles.Reset();
		Normals.Reset();
		UVs.Reset();
		VertexColors.Reset();
		Tangents.Reset();
		LocalBounds = FBox(ForceInit);
	}
};

class SIMCOPTERREMAKE_API FMaxisProceduralMeshBuilder
{
public:
	// Maxis Sim3D face type 11 is the alpha-blended "disc" surface - used for the spinning
	// rotor blur, which the original game draws as a near-translucent disc over the opaque
	// blades. Callers separate these faces so they can apply a translucent material.
	static bool IsTranslucentFaceType(uint8 FaceType) { return FaceType == 11; }

	// Builds a vertex-colored section from a palette-colored Maxis object.
	//
	// Vertices are converted to Unreal space via FMaxisMeshReader::ConvertMaxisVertexToUnreal
	// (which yields centimetres) and then multiplied by Scale. No global yaw is applied:
	// SimCopter moving objects face +Z in Maxis space, which maps to Unreal +X (the pawn's
	// forward axis), so the model already faces forward.
	//
	// Face normals are oriented to point away from the object centroid so the visible
	// exterior is lit regardless of the source winding (the raw Maxis winding produces
	// inward normals for exterior faces - the same correction the city renderer applies).
	static void BuildPaletteColoredSection(
		const FMaxisMeshObject& Object,
		const TArray<FColor>* ColorMap,
		float UnitsPerCentimeter,
		float Scale,
		bool bAddBackfaces,
		const FLinearColor& FallbackColor,
		FMaxisMeshSection& OutSection);

	// Same as above, but routes translucent (disc) faces into OutTranslucentSection instead of
	// OutOpaqueSection. Pass a null OutTranslucentSection to keep all faces in OutOpaqueSection.
	static void BuildPaletteColoredSections(
		const FMaxisMeshObject& Object,
		const TArray<FColor>* ColorMap,
		float UnitsPerCentimeter,
		float Scale,
		bool bAddBackfaces,
		const FLinearColor& FallbackColor,
		FMaxisMeshSection& OutOpaqueSection,
		FMaxisMeshSection* OutTranslucentSection);
};
