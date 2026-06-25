// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterPopulationBody.h"

#include "ProceduralMeshComponent.h"

namespace
{
struct FPersonOutfit
{
	FLinearColor Skin;
	FLinearColor Shirt;
	FLinearColor Pants;
	FLinearColor Hat;
	bool bHasHat = false;
};

// Flat, slightly desaturated colours chosen to echo the original low-colour palette people:
// police in dark blue with a cap, plus a spread of drab civilian outfits.
const FPersonOutfit GPersonOutfits[FSimCopterPopulationBody::OutfitCount] = {
	// Police - dark blue uniform, black trousers, navy cap.
	{ FLinearColor(0.80f, 0.62f, 0.47f), FLinearColor(0.12f, 0.22f, 0.55f), FLinearColor(0.05f, 0.05f, 0.10f), FLinearColor(0.09f, 0.16f, 0.42f), true },
	// Civilian - tan shirt, brown trousers.
	{ FLinearColor(0.82f, 0.64f, 0.49f), FLinearColor(0.64f, 0.52f, 0.30f), FLinearColor(0.20f, 0.14f, 0.09f), FLinearColor::Black, false },
	// Civilian - rust/brown jacket, dark trousers.
	{ FLinearColor(0.74f, 0.55f, 0.42f), FLinearColor(0.55f, 0.27f, 0.16f), FLinearColor(0.10f, 0.10f, 0.12f), FLinearColor::Black, false },
	// Civilian - grey shirt, blue jeans.
	{ FLinearColor(0.84f, 0.66f, 0.52f), FLinearColor(0.55f, 0.56f, 0.58f), FLinearColor(0.16f, 0.22f, 0.36f), FLinearColor::Black, false },
	// Civilian - muted green shirt, khaki trousers.
	{ FLinearColor(0.78f, 0.60f, 0.46f), FLinearColor(0.27f, 0.42f, 0.27f), FLinearColor(0.38f, 0.34f, 0.22f), FLinearColor::Black, false },
	// Civilian - maroon shirt, grey trousers.
	{ FLinearColor(0.80f, 0.62f, 0.48f), FLinearColor(0.45f, 0.16f, 0.22f), FLinearColor(0.28f, 0.28f, 0.30f), FLinearColor::Black, false },
};

void AppendBox(
	const FVector& Center,
	const FVector& HalfExtents,
	const FLinearColor& Color,
	TArray<FVector>& Vertices,
	TArray<int32>& Triangles,
	TArray<FVector>& Normals,
	TArray<FVector2D>& UVs,
	TArray<FLinearColor>& VertexColors,
	TArray<FProcMeshTangent>& Tangents)
{
	// Six axis-aligned faces. Each face gets its own 4 verts so the flat-shaded normals stay
	// crisp. Triangles are emitted both ways so a single-sided material never culls a face -
	// the box interior is never visible, so the duplicated winding costs nothing visually.
	const FVector FaceNormals[6] = {
		FVector(1, 0, 0), FVector(-1, 0, 0),
		FVector(0, 1, 0), FVector(0, -1, 0),
		FVector(0, 0, 1), FVector(0, 0, -1)
	};

	// Tangent (U direction) per face, kept perpendicular to the normal.
	const FVector FaceTangents[6] = {
		FVector(0, 1, 0), FVector(0, 1, 0),
		FVector(1, 0, 0), FVector(1, 0, 0),
		FVector(1, 0, 0), FVector(1, 0, 0)
	};

	for (int32 Face = 0; Face < 6; ++Face)
	{
		const FVector N = FaceNormals[Face];
		// Build an in-plane basis (U, V) so the four corners wind correctly around the normal.
		const FVector U = FaceTangents[Face];
		const FVector V = FVector::CrossProduct(N, U);

		const FVector FaceCenter = Center + N * (HalfExtents | N.GetAbs());
		const FVector UExtent = U * (HalfExtents | U.GetAbs());
		const FVector VExtent = V * (HalfExtents | V.GetAbs());

		const int32 Base = Vertices.Num();
		Vertices.Add(FaceCenter - UExtent - VExtent);
		Vertices.Add(FaceCenter + UExtent - VExtent);
		Vertices.Add(FaceCenter + UExtent + VExtent);
		Vertices.Add(FaceCenter - UExtent + VExtent);

		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			Normals.Add(N);
			VertexColors.Add(Color);
			Tangents.Add(FProcMeshTangent(U, false));
		}
		UVs.Add(FVector2D(0.0f, 0.0f));
		UVs.Add(FVector2D(1.0f, 0.0f));
		UVs.Add(FVector2D(1.0f, 1.0f));
		UVs.Add(FVector2D(0.0f, 1.0f));

		Triangles.Add(Base + 0); Triangles.Add(Base + 1); Triangles.Add(Base + 2);
		Triangles.Add(Base + 0); Triangles.Add(Base + 2); Triangles.Add(Base + 3);
		// Reverse winding (interior) so the face is visible from either side.
		Triangles.Add(Base + 0); Triangles.Add(Base + 2); Triangles.Add(Base + 1);
		Triangles.Add(Base + 0); Triangles.Add(Base + 3); Triangles.Add(Base + 2);
	}
}
}

int32 FSimCopterPopulationBody::ResolveOutfitIndex(const UObject* StableObject)
{
	const uint32 Hash = StableObject != nullptr ? GetTypeHash(StableObject->GetFName()) : 0u;
	// Roughly one in three pedestrians is a police officer, matching the original's crowd mix.
	return static_cast<int32>(Hash % OutfitCount);
}

bool FSimCopterPopulationBody::OutfitHasHat(int32 OutfitIndex)
{
	const int32 Clamped = FMath::Clamp(OutfitIndex, 0, OutfitCount - 1);
	return GPersonOutfits[Clamped].bHasHat;
}

void FSimCopterPopulationBody::BuildPerson(UProceduralMeshComponent* MeshComponent, int32 OutfitIndex, float HeightCm)
{
	if (MeshComponent == nullptr)
	{
		return;
	}

	const FPersonOutfit& Outfit = GPersonOutfits[FMath::Clamp(OutfitIndex, 0, OutfitCount - 1)];
	const float H = FMath::Max(HeightCm, 40.0f);

	// All sizes are fractions of the total height so the proportions hold at any scale.
	// X is forward/back (body depth), Y is left/right (body width), Z is up.
	const float LegHeight = H * 0.46f;
	const float LegHalfW = H * 0.060f;   // half width per leg (Y)
	const float LegHalfD = H * 0.075f;   // half depth (X)
	const float LegGapY = H * 0.030f;    // gap between the inner faces of the legs

	const float TorsoBottom = LegHeight;
	const float TorsoTop = H * 0.80f;
	const float TorsoHalfW = H * 0.150f;
	const float TorsoHalfD = H * 0.090f;

	const float ArmHeight = (TorsoTop - TorsoBottom) * 0.92f;
	const float ArmHalf = H * 0.040f;

	const float HeadBottom = H * 0.79f; // slight overlap with the torso top so the head never floats
	const float HeadSize = H * 0.150f; // full size (cube-ish)
	const float HeadHalf = HeadSize * 0.5f;

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;

	auto Box = [&](const FVector& Center, const FVector& HalfExtents, const FLinearColor& Color)
	{
		AppendBox(Center, HalfExtents, Color, Vertices, Triangles, Normals, UVs, VertexColors, Tangents);
	};

	// Legs.
	const float LegCenterY = LegGapY + LegHalfW;
	Box(FVector(0.0f, -LegCenterY, LegHeight * 0.5f), FVector(LegHalfD, LegHalfW, LegHeight * 0.5f), Outfit.Pants);
	Box(FVector(0.0f, LegCenterY, LegHeight * 0.5f), FVector(LegHalfD, LegHalfW, LegHeight * 0.5f), Outfit.Pants);

	// Torso.
	Box(FVector(0.0f, 0.0f, (TorsoBottom + TorsoTop) * 0.5f), FVector(TorsoHalfD, TorsoHalfW, (TorsoTop - TorsoBottom) * 0.5f), Outfit.Shirt);

	// Arms hang at the sides of the torso (shirt-coloured, with skin "hands" at the bottom).
	const float ArmCenterZ = TorsoTop - ArmHeight * 0.5f;
	const float ArmY = TorsoHalfW + ArmHalf;
	Box(FVector(0.0f, -ArmY, ArmCenterZ), FVector(ArmHalf, ArmHalf, ArmHeight * 0.5f), Outfit.Shirt);
	Box(FVector(0.0f, ArmY, ArmCenterZ), FVector(ArmHalf, ArmHalf, ArmHeight * 0.5f), Outfit.Shirt);
	Box(FVector(0.0f, -ArmY, TorsoBottom + ArmHalf), FVector(ArmHalf * 0.9f, ArmHalf * 0.9f, ArmHalf), Outfit.Skin);
	Box(FVector(0.0f, ArmY, TorsoBottom + ArmHalf), FVector(ArmHalf * 0.9f, ArmHalf * 0.9f, ArmHalf), Outfit.Skin);

	// Head.
	Box(FVector(0.0f, 0.0f, HeadBottom + HeadHalf), FVector(HeadHalf * 0.85f, HeadHalf, HeadHalf), Outfit.Skin);

	// Optional cap.
	if (Outfit.bHasHat)
	{
		const float HatZ = HeadBottom + HeadSize;
		Box(FVector(HeadHalf * 0.15f, 0.0f, HatZ + HeadHalf * 0.18f), FVector(HeadHalf * 1.05f, HeadHalf * 1.1f, HeadHalf * 0.22f), Outfit.Hat);
		// Cap brim, jutting forward (+X).
		Box(FVector(HeadHalf * 1.05f, 0.0f, HatZ), FVector(HeadHalf * 0.55f, HeadHalf * 0.95f, HeadHalf * 0.10f), Outfit.Hat);
	}

	MeshComponent->ClearAllMeshSections();
	MeshComponent->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, false);
}
