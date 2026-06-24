// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/MaxisProceduralMeshBuilder.h"

namespace
{
FLinearColor ResolveFaceColor(const TArray<FColor>* ColorMap, uint8 FaceType, uint8 MaterialIndex, const FLinearColor& FallbackColor)
{
	// Face types 13/18 are textured in the city geometry; the helicopter meshes do
	// not use them, but fall back to a neutral colour if one ever appears.
	if (FaceType == 13 || FaceType == 18)
	{
		return FallbackColor;
	}

	if (ColorMap != nullptr && ColorMap->IsValidIndex(MaterialIndex))
	{
		return FLinearColor((*ColorMap)[MaterialIndex]);
	}

	return FallbackColor;
}

void AppendFaceToSection(
	const FMaxisMeshObject& Object,
	const FMaxisMeshFace& Face,
	const FVector& ObjectCenter,
	const TArray<FColor>* ColorMap,
	float EffectiveUnits,
	float Scale,
	bool bAddBackfaces,
	const FLinearColor& FallbackColor,
	FMaxisMeshSection& Section)
{
	if (Face.VertexIndices.Num() < 3)
	{
		return;
	}

	const int32 FaceVertexStart = Section.Vertices.Num();
	const FLinearColor FaceColor = ResolveFaceColor(ColorMap, Face.FaceType, Face.MaterialIndex, FallbackColor);

	for (int32 FaceVertexIndex = 0; FaceVertexIndex < Face.VertexIndices.Num(); ++FaceVertexIndex)
	{
		const uint16 SourceVertexIndex = Face.VertexIndices[FaceVertexIndex];
		if (!Object.Vertices.IsValidIndex(SourceVertexIndex))
		{
			continue;
		}

		const FVector LocalVertex = FMaxisMeshReader::ConvertMaxisVertexToUnreal(Object.Vertices[SourceVertexIndex], EffectiveUnits) * Scale;
		Section.Vertices.Add(LocalVertex);
		Section.UVs.Add(FVector2D::ZeroVector);
		Section.VertexColors.Add(FaceColor);
		Section.Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
		Section.LocalBounds += LocalVertex;
	}

	const int32 FaceVertexCount = Section.Vertices.Num() - FaceVertexStart;
	if (FaceVertexCount < 3)
	{
		Section.Vertices.SetNum(FaceVertexStart);
		Section.UVs.SetNum(FaceVertexStart);
		Section.VertexColors.SetNum(FaceVertexStart);
		Section.Tangents.SetNum(FaceVertexStart);
		return;
	}

	FVector FaceNormal = FVector::CrossProduct(
		Section.Vertices[FaceVertexStart + 1] - Section.Vertices[FaceVertexStart],
		Section.Vertices[FaceVertexStart + 2] - Section.Vertices[FaceVertexStart]).GetSafeNormal();

	FVector FaceCenter = FVector::ZeroVector;
	for (int32 Index = 0; Index < FaceVertexCount; ++Index)
	{
		FaceCenter += Section.Vertices[FaceVertexStart + Index];
	}
	FaceCenter /= static_cast<float>(FaceVertexCount);
	if (FVector::DotProduct(FaceNormal, FaceCenter - ObjectCenter) < 0.0f)
	{
		FaceNormal = -FaceNormal;
	}

	for (int32 Index = 0; Index < FaceVertexCount; ++Index)
	{
		Section.Normals.Add(FaceNormal);
	}

	for (int32 TriangleIndex = 1; TriangleIndex < FaceVertexCount - 1; ++TriangleIndex)
	{
		Section.Triangles.Add(FaceVertexStart);
		Section.Triangles.Add(FaceVertexStart + TriangleIndex);
		Section.Triangles.Add(FaceVertexStart + TriangleIndex + 1);

		if (bAddBackfaces)
		{
			Section.Triangles.Add(FaceVertexStart);
			Section.Triangles.Add(FaceVertexStart + TriangleIndex + 1);
			Section.Triangles.Add(FaceVertexStart + TriangleIndex);
		}
	}
}
}

void FMaxisProceduralMeshBuilder::BuildPaletteColoredSection(
	const FMaxisMeshObject& Object,
	const TArray<FColor>* ColorMap,
	float UnitsPerCentimeter,
	float Scale,
	bool bAddBackfaces,
	const FLinearColor& FallbackColor,
	FMaxisMeshSection& OutSection)
{
	BuildPaletteColoredSections(Object, ColorMap, UnitsPerCentimeter, Scale, bAddBackfaces, FallbackColor, OutSection, nullptr);
}

void FMaxisProceduralMeshBuilder::BuildPaletteColoredSections(
	const FMaxisMeshObject& Object,
	const TArray<FColor>* ColorMap,
	float UnitsPerCentimeter,
	float Scale,
	bool bAddBackfaces,
	const FLinearColor& FallbackColor,
	FMaxisMeshSection& OutOpaqueSection,
	FMaxisMeshSection* OutTranslucentSection)
{
	OutOpaqueSection.Reset();
	if (OutTranslucentSection != nullptr)
	{
		OutTranslucentSection->Reset();
	}

	const float EffectiveUnits = UnitsPerCentimeter > 0.0f ? UnitsPerCentimeter : FMaxisMeshReader::MeshUnitsPerCentimeter;

	// Object centroid in the same converted/scaled local space the vertices use below,
	// so per-face normals can be flipped to face away from it.
	FVector ObjectCenter = FVector::ZeroVector;
	if (Object.Vertices.Num() > 0)
	{
		for (const FMaxisMeshVertex& Vertex : Object.Vertices)
		{
			ObjectCenter += FMaxisMeshReader::ConvertMaxisVertexToUnreal(Vertex, EffectiveUnits) * Scale;
		}
		ObjectCenter /= static_cast<float>(Object.Vertices.Num());
	}

	for (const FMaxisMeshFace& Face : Object.Faces)
	{
		const bool bTranslucent = OutTranslucentSection != nullptr && IsTranslucentFaceType(Face.FaceType);
		FMaxisMeshSection& Target = bTranslucent ? *OutTranslucentSection : OutOpaqueSection;
		// The translucent disc is drawn with a two-sided material, so it needs no reversed
		// backface triangles - adding them would double-blend the disc and make it look solid.
		const bool bFaceBackfaces = bTranslucent ? false : bAddBackfaces;
		AppendFaceToSection(Object, Face, ObjectCenter, ColorMap, EffectiveUnits, Scale, bFaceBackfaces, FallbackColor, Target);
	}
}
