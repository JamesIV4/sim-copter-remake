// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/MaxisProceduralMeshBuilder.h"

namespace
{
struct FMaxisFaceNormalData
{
	FVector Normal = FVector::UpVector;
	FVector WeightedNormal = FVector::UpVector;
	bool bValid = false;
};

uint32 MakeSourceEdgeKey(uint16 VertexA, uint16 VertexB)
{
	const uint16 MinVertex = FMath::Min(VertexA, VertexB);
	const uint16 MaxVertex = FMath::Max(VertexA, VertexB);
	return (static_cast<uint32>(MinVertex) << 16) | static_cast<uint32>(MaxVertex);
}

int32 FindSetRoot(TArray<int32>& Parents, int32 Index)
{
	int32 Root = Index;
	while (Parents[Root] != Root)
	{
		Root = Parents[Root];
	}

	while (Parents[Index] != Index)
	{
		const int32 Next = Parents[Index];
		Parents[Index] = Root;
		Index = Next;
	}
	return Root;
}

void UnionSets(TArray<int32>& Parents, int32 IndexA, int32 IndexB)
{
	const int32 RootA = FindSetRoot(Parents, IndexA);
	const int32 RootB = FindSetRoot(Parents, IndexB);
	if (RootA != RootB)
	{
		Parents[RootB] = RootA;
	}
}

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
	const TArray<FVector>& CornerNormals,
	const TArray<FColor>* ColorMap,
	float EffectiveUnits,
	float Scale,
	bool bAddBackfaces,
	const FLinearColor& FallbackColor,
	FMaxisMeshSection& Section)
{
	if (Face.VertexIndices.Num() < 2)
	{
		return;
	}

	const int32 FaceVertexStart = Section.Vertices.Num();
	const FLinearColor FaceColor = ResolveFaceColor(ColorMap, Face.FaceType, Face.MaterialIndex, FallbackColor);

	if (Face.VertexIndices.Num() == 2)
	{
		const uint16 IndexA = Face.VertexIndices[0];
		const uint16 IndexB = Face.VertexIndices[1];
		if (!Object.Vertices.IsValidIndex(IndexA) || !Object.Vertices.IsValidIndex(IndexB))
		{
			return;
		}

		const FVector A = FMaxisMeshReader::ConvertMaxisVertexToUnreal(Object.Vertices[IndexA], EffectiveUnits) * Scale;
		const FVector B = FMaxisMeshReader::ConvertMaxisVertexToUnreal(Object.Vertices[IndexB], EffectiveUnits) * Scale;

		const FVector Dir = (B - A).GetSafeNormal();
		if (Dir.IsNearlyZero())
		{
			return;
		}

		FVector Perp1 = FVector::CrossProduct(Dir, FVector::UpVector);
		if (Perp1.IsNearlyZero())
		{
			Perp1 = FVector::CrossProduct(Dir, FVector::RightVector);
		}
		Perp1.Normalize();
		const FVector Perp2 = FVector::CrossProduct(Dir, Perp1).GetSafeNormal();

		const float HalfWidth = 2.5f * Scale; // 5.0f width * Scale

		// Build two intersecting quads (a cross)
		const FVector Offsets[4] = { Perp1 * HalfWidth, -Perp1 * HalfWidth, Perp2 * HalfWidth, -Perp2 * HalfWidth };
		for (int32 QuadIndex = 0; QuadIndex < 2; ++QuadIndex)
		{
			const FVector& O1 = Offsets[QuadIndex * 2];
			const FVector& O2 = Offsets[QuadIndex * 2 + 1];

			const int32 VStart = Section.Vertices.Num();
			Section.Vertices.Add(A + O1);
			Section.Vertices.Add(B + O1);
			Section.Vertices.Add(B + O2);
			Section.Vertices.Add(A + O2);

			for (int32 i = 0; i < 4; ++i)
			{
				Section.UVs.Add(FVector2D::ZeroVector);
				Section.VertexColors.Add(FaceColor);
				Section.Tangents.Add(FProcMeshTangent(Dir.X, Dir.Y, Dir.Z));
				Section.LocalBounds += Section.Vertices.Last();
				// Use outward normals for the cross
				FVector N = (QuadIndex == 0) ? Perp1 : Perp2;
				if (i == 2 || i == 3) N = -N;
				Section.Normals.Add(N);
			}

			Section.Triangles.Add(VStart);
			Section.Triangles.Add(VStart + 1);
			Section.Triangles.Add(VStart + 2);
			Section.Triangles.Add(VStart);
			Section.Triangles.Add(VStart + 2);
			Section.Triangles.Add(VStart + 3);

			// Always add backfaces for lines so they are solid
			Section.Triangles.Add(VStart);
			Section.Triangles.Add(VStart + 2);
			Section.Triangles.Add(VStart + 1);
			Section.Triangles.Add(VStart);
			Section.Triangles.Add(VStart + 3);
			Section.Triangles.Add(VStart + 2);
		}

		return;
	}

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
		Section.Normals.Add(
			CornerNormals.IsValidIndex(FaceVertexIndex)
				? CornerNormals[FaceVertexIndex]
				: FVector::UpVector);
		Section.LocalBounds += LocalVertex;
	}

	const int32 FaceVertexCount = Section.Vertices.Num() - FaceVertexStart;
	if (FaceVertexCount < 3)
	{
		Section.Vertices.SetNum(FaceVertexStart);
		Section.UVs.SetNum(FaceVertexStart);
		Section.VertexColors.SetNum(FaceVertexStart);
		Section.Tangents.SetNum(FaceVertexStart);
		Section.Normals.SetNum(FaceVertexStart);
		return;
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

void FMaxisProceduralMeshBuilder::BuildAutoSmoothCornerNormals(
	const FMaxisMeshObject& Object,
	const TArray<FVector>& VertexPositions,
	bool bForceHorizontalFacesUp,
	float SmoothAngleDegrees,
	TArray<TArray<FVector>>& OutCornerNormals)
{
	OutCornerNormals.Reset();
	OutCornerNormals.SetNum(Object.Faces.Num());

	TArray<FMaxisFaceNormalData> FaceNormalData;
	FaceNormalData.SetNum(Object.Faces.Num());

	TMap<uint16, TArray<int32>> FacesByVertex;
	TMap<uint32, TArray<int32>> FacesByEdge;

	FVector ObjectCenter = FVector::ZeroVector;
	for (const FVector& VertexPosition : VertexPositions)
	{
		ObjectCenter += VertexPosition;
	}
	if (VertexPositions.Num() > 0)
	{
		ObjectCenter /= static_cast<float>(VertexPositions.Num());
	}

	for (int32 FaceIndex = 0; FaceIndex < Object.Faces.Num(); ++FaceIndex)
	{
		const FMaxisMeshFace& Face = Object.Faces[FaceIndex];
		TArray<FVector>& CornerNormals = OutCornerNormals[FaceIndex];
		CornerNormals.Init(FVector::UpVector, Face.VertexIndices.Num());
		if (Face.VertexIndices.Num() < 3)
		{
			continue;
		}

		TArray<uint16> ValidVertexIndices;
		ValidVertexIndices.Reserve(Face.VertexIndices.Num());
		FVector FaceCenter = FVector::ZeroVector;
		for (const uint16 SourceVertexIndex : Face.VertexIndices)
		{
			if (!VertexPositions.IsValidIndex(SourceVertexIndex))
			{
				continue;
			}

			ValidVertexIndices.Add(SourceVertexIndex);
			FaceCenter += VertexPositions[SourceVertexIndex];
			FacesByVertex.FindOrAdd(SourceVertexIndex).AddUnique(FaceIndex);
		}

		if (ValidVertexIndices.Num() < 3)
		{
			continue;
		}
		FaceCenter /= static_cast<float>(ValidVertexIndices.Num());

		// Newell's method uses the whole polygon, so faces whose first three
		// corners are collinear still receive a stable area-weighted normal.
		FVector WeightedNormal = FVector::ZeroVector;
		for (int32 CornerIndex = 0; CornerIndex < ValidVertexIndices.Num(); ++CornerIndex)
		{
			const FVector& Current = VertexPositions[ValidVertexIndices[CornerIndex]];
			const FVector& Next = VertexPositions[ValidVertexIndices[(CornerIndex + 1) % ValidVertexIndices.Num()]];
			WeightedNormal.X += (Current.Y - Next.Y) * (Current.Z + Next.Z);
			WeightedNormal.Y += (Current.Z - Next.Z) * (Current.X + Next.X);
			WeightedNormal.Z += (Current.X - Next.X) * (Current.Y + Next.Y);
		}

		FVector FaceNormal = WeightedNormal.GetSafeNormal();
		if (FaceNormal.IsNearlyZero())
		{
			FaceNormal = FVector::UpVector;
			WeightedNormal = FaceNormal;
		}

		bool bFlipNormal = false;
		if (bForceHorizontalFacesUp && FMath::Abs(FaceNormal.Z) > 0.85f)
		{
			bFlipNormal = FaceNormal.Z < 0.0f;
		}
		else
		{
			bFlipNormal = FVector::DotProduct(FaceNormal, FaceCenter - ObjectCenter) < 0.0f;
		}
		if (bFlipNormal)
		{
			FaceNormal = -FaceNormal;
			WeightedNormal = -WeightedNormal;
		}

		FMaxisFaceNormalData& NormalData = FaceNormalData[FaceIndex];
		NormalData.Normal = FaceNormal;
		NormalData.WeightedNormal = WeightedNormal;
		NormalData.bValid = true;
		CornerNormals.Init(FaceNormal, Face.VertexIndices.Num());

		for (int32 CornerIndex = 0; CornerIndex < Face.VertexIndices.Num(); ++CornerIndex)
		{
			const uint16 VertexA = Face.VertexIndices[CornerIndex];
			const uint16 VertexB = Face.VertexIndices[(CornerIndex + 1) % Face.VertexIndices.Num()];
			if (VertexA == VertexB ||
				!VertexPositions.IsValidIndex(VertexA) ||
				!VertexPositions.IsValidIndex(VertexB))
			{
				continue;
			}
			FacesByEdge.FindOrAdd(MakeSourceEdgeKey(VertexA, VertexB)).AddUnique(FaceIndex);
		}
	}

	const float ClampedSmoothAngle = FMath::Clamp(SmoothAngleDegrees, 0.0f, 180.0f);
	const float SmoothDotThreshold = FMath::Cos(FMath::DegreesToRadians(ClampedSmoothAngle));
	TMap<uint16, TArray<FIntPoint>> SmoothFacePairsByVertex;
	for (const TPair<uint32, TArray<int32>>& EdgeEntry : FacesByEdge)
	{
		const uint16 VertexA = static_cast<uint16>(EdgeEntry.Key >> 16);
		const uint16 VertexB = static_cast<uint16>(EdgeEntry.Key & 0xffffu);
		const TArray<int32>& EdgeFaces = EdgeEntry.Value;
		for (int32 FaceAIndex = 0; FaceAIndex < EdgeFaces.Num(); ++FaceAIndex)
		{
			for (int32 FaceBIndex = FaceAIndex + 1; FaceBIndex < EdgeFaces.Num(); ++FaceBIndex)
			{
				const int32 FaceA = EdgeFaces[FaceAIndex];
				const int32 FaceB = EdgeFaces[FaceBIndex];
				if (!FaceNormalData[FaceA].bValid || !FaceNormalData[FaceB].bValid ||
					FVector::DotProduct(FaceNormalData[FaceA].Normal, FaceNormalData[FaceB].Normal) <= SmoothDotThreshold)
				{
					continue;
				}

				SmoothFacePairsByVertex.FindOrAdd(VertexA).Emplace(FaceA, FaceB);
				SmoothFacePairsByVertex.FindOrAdd(VertexB).Emplace(FaceA, FaceB);
			}
		}
	}

	// Build a separate connected face fan at each source vertex. This matters on
	// gradually curved meshes: each neighboring edge may be under 35 degrees even
	// when the normals at opposite ends of the fan differ by more than 35 degrees.
	for (const TPair<uint16, TArray<int32>>& VertexEntry : FacesByVertex)
	{
		const uint16 SourceVertexIndex = VertexEntry.Key;
		const TArray<int32>& VertexFaces = VertexEntry.Value;
		if (VertexFaces.Num() == 0)
		{
			continue;
		}

		TMap<int32, int32> LocalIndexByFace;
		TArray<int32> Parents;
		Parents.SetNumUninitialized(VertexFaces.Num());
		for (int32 LocalIndex = 0; LocalIndex < VertexFaces.Num(); ++LocalIndex)
		{
			LocalIndexByFace.Add(VertexFaces[LocalIndex], LocalIndex);
			Parents[LocalIndex] = LocalIndex;
		}

		if (const TArray<FIntPoint>* SmoothPairs = SmoothFacePairsByVertex.Find(SourceVertexIndex))
		{
			for (const FIntPoint& SmoothPair : *SmoothPairs)
			{
				const int32* LocalFaceA = LocalIndexByFace.Find(SmoothPair.X);
				const int32* LocalFaceB = LocalIndexByFace.Find(SmoothPair.Y);
				if (LocalFaceA != nullptr && LocalFaceB != nullptr)
				{
					UnionSets(Parents, *LocalFaceA, *LocalFaceB);
				}
			}
		}

		TMap<int32, FVector> WeightedNormalsByRoot;
		for (int32 LocalIndex = 0; LocalIndex < VertexFaces.Num(); ++LocalIndex)
		{
			const int32 Root = FindSetRoot(Parents, LocalIndex);
			WeightedNormalsByRoot.FindOrAdd(Root) += FaceNormalData[VertexFaces[LocalIndex]].WeightedNormal;
		}

		for (int32 LocalIndex = 0; LocalIndex < VertexFaces.Num(); ++LocalIndex)
		{
			const int32 FaceIndex = VertexFaces[LocalIndex];
			const int32 Root = FindSetRoot(Parents, LocalIndex);
			const FVector SmoothedNormal = WeightedNormalsByRoot[Root].GetSafeNormal(
				SMALL_NUMBER,
				FaceNormalData[FaceIndex].Normal);

			const FMaxisMeshFace& Face = Object.Faces[FaceIndex];
			for (int32 CornerIndex = 0; CornerIndex < Face.VertexIndices.Num(); ++CornerIndex)
			{
				if (Face.VertexIndices[CornerIndex] == SourceVertexIndex)
				{
					OutCornerNormals[FaceIndex][CornerIndex] = SmoothedNormal;
				}
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

	TArray<FVector> VertexPositions;
	VertexPositions.Reserve(Object.Vertices.Num());
	for (const FMaxisMeshVertex& Vertex : Object.Vertices)
	{
		VertexPositions.Add(FMaxisMeshReader::ConvertMaxisVertexToUnreal(Vertex, EffectiveUnits) * Scale);
	}

	TArray<TArray<FVector>> CornerNormals;
	BuildAutoSmoothCornerNormals(
		Object,
		VertexPositions,
		false,
		DefaultSmoothAngleDegrees,
		CornerNormals);

	for (int32 FaceIndex = 0; FaceIndex < Object.Faces.Num(); ++FaceIndex)
	{
		const FMaxisMeshFace& Face = Object.Faces[FaceIndex];
		const bool bTranslucent = OutTranslucentSection != nullptr && IsTranslucentFaceType(Face.FaceType);
		FMaxisMeshSection& Target = bTranslucent ? *OutTranslucentSection : OutOpaqueSection;
		// The translucent disc is drawn with a two-sided material, so it needs no reversed
		// backface triangles - adding them would double-blend the disc and make it look solid.
		const bool bFaceBackfaces = bTranslucent ? false : bAddBackfaces;
		AppendFaceToSection(
			Object,
			Face,
			CornerNormals[FaceIndex],
			ColorMap,
			EffectiveUnits,
			Scale,
			bFaceBackfaces,
			FallbackColor,
			Target);
	}
}
