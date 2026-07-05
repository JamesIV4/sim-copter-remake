// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterFireRenderComponent.h"
#include "Formats/MaxisMeshLibrary.h"
#include "Formats/MaxisMeshReader.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

// FIREPTS: the original flame GEO object (SIM3D2.MAX, Object.Header.Id 0x120). Its 22 faces are
// single-vertex point sprites - a cloud of palette-coloured fire points - not a solid mesh. Car
// fires use the same object in the remake. See Docs/scratchpad/ghidra/out_effect_pool_init.txt.
namespace
{
	constexpr int32 FirePtsObjectId = 0x120;
}

USimCopterFireRenderComponent::USimCopterFireRenderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool USimCopterFireRenderComponent::InitFireAssets(const FString& OriginalGameRoot, UMaterialInterface* InFlameMaterial, FString& OutError)
{
	if (bAssetsReady)
	{
		return true;
	}

	FlameMaterial = InFlameMaterial;

	if (OriginalGameRoot.IsEmpty())
	{
		OutError = TEXT("Original game root is empty.");
		return false;
	}

	FMaxisMeshLibrary MeshLibrary;
	if (!MeshLibrary.LoadFromOriginalGameRoot(OriginalGameRoot, OutError))
	{
		return false;
	}

	const TArray<FColor>* ColorMap = nullptr;
	const FMaxisMeshObject* FlameObject = MeshLibrary.FindObjectByObjectId(FirePtsObjectId, &ColorMap);
	if (FlameObject == nullptr)
	{
		OutError = FString::Printf(TEXT("FIREPTS flame object (id 0x%x) not found in '%s'."), FirePtsObjectId, *OriginalGameRoot);
		return false;
	}

	// Extract the point sprites: one per single-vertex face, positioned at its vertex and coloured
	// by the face's SIM3D palette index (bright fire hues authored into the object).
	const FLinearColor FallbackColor(1.0f, 0.45f, 0.06f);
	FirePoints.Reset();
	FBox PointBounds(ForceInit);
	for (const FMaxisMeshFace& Face : FlameObject->Faces)
	{
		if (Face.VertexIndices.Num() < 1)
		{
			continue;
		}
		const int32 VertexIndex = Face.VertexIndices[0];
		if (!FlameObject->Vertices.IsValidIndex(VertexIndex))
		{
			continue;
		}

		FFirePoint Point;
		Point.LocalOffset = FMaxisMeshReader::ConvertMaxisVertexToUnreal(FlameObject->Vertices[VertexIndex], ModelUnitsPerCentimeter) * FlameModelScale;
		Point.Color = (ColorMap != nullptr && ColorMap->IsValidIndex(Face.MaterialIndex))
			? FLinearColor((*ColorMap)[Face.MaterialIndex])
			: FallbackColor;
		FirePoints.Add(Point);
		PointBounds += Point.LocalOffset;
	}

	if (FirePoints.Num() == 0)
	{
		OutError = TEXT("FIREPTS produced no fire points.");
		return false;
	}

	// Seat the base of the point cloud at Z = 0 so the owner drops flames onto the surface, and
	// size each sprite from the cloud extent so the points overlap into a continuous flame.
	const float BaseZ = PointBounds.Min.Z;
	for (FFirePoint& Point : FirePoints)
	{
		Point.LocalOffset.Z -= BaseZ;
	}
	const float Radius = FMath::Max(static_cast<float>(PointBounds.GetExtent().Size2D()), 1.0f);
	FireSpriteHalfSizeCm = FMath::Clamp(Radius * 0.55f, 22.0f, 140.0f);
	FireMaxLocalZ = FMath::Max(PointBounds.Max.Z - PointBounds.Min.Z, 1.0f);

	bAssetsReady = true;
	return true;
}

void USimCopterFireRenderComponent::SyncFlames(const TArray<FSimCopterFlameVisual>& Visuals, float TimeSeconds, const FVector& CameraLocation)
{
	if (!bAssetsReady)
	{
		return;
	}

	if (MeshComponent == nullptr)
	{
		MeshComponent = NewObject<UProceduralMeshComponent>(this, TEXT("FireCards"));
		MeshComponent->SetupAttachment(this);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComponent->SetCanEverAffectNavigation(false);
		MeshComponent->bUseAsyncCooking = false;
		MeshComponent->SetCastShadow(false);
		MeshComponent->RegisterComponent();
	}

	if (Visuals.Num() == 0)
	{
		if (MeshComponent->GetNumSections() > 0)
		{
			MeshComponent->ClearAllMeshSections();
		}
		return;
	}

	const FTransform WorldToLocal = GetComponentTransform().Inverse();

	// Camera billboard basis (shared by every sprite).
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	// The original fire is a chaotic mass of dithered red/orange/yellow specks with dark grey smoke
	// near the top, jittering every frame. Emit several sub-sprites per fire point and randomise
	// size / offset / colour each frame for that turbulent look.
	constexpr int32 SubSpritesPerPoint = 3;
	const int32 SpriteCount = Visuals.Num() * FirePoints.Num() * SubSpritesPerPoint;
	Vertices.Reserve(SpriteCount * 4);
	Triangles.Reserve(SpriteCount * 6);
	Normals.Reserve(SpriteCount * 4);
	UVs.Reserve(SpriteCount * 4);
	Colors.Reserve(SpriteCount * 4);
	Tangents.Reserve(SpriteCount * 4);

	const FVector LocalNormalBase = WorldToLocal.TransformVectorNoScale(FVector::UpVector);

	for (const FSimCopterFlameVisual& Visual : Visuals)
	{
		for (const FFirePoint& Point : FirePoints)
		{
			const float HeightFrac = FMath::Clamp(Point.LocalOffset.Z / FireMaxLocalZ, 0.0f, 1.0f);

			for (int32 Sub = 0; Sub < SubSpritesPerPoint; ++Sub)
			{
				// Chaotic per-frame jitter; upper points wander more and lift.
				const FVector Jitter(
					FMath::FRandRange(-18.0f, 18.0f),
					FMath::FRandRange(-18.0f, 18.0f),
					FMath::FRandRange(-8.0f, 26.0f) * (0.4f + HeightFrac));
				const FVector Center = Visual.World + (Point.LocalOffset + Jitter) * Visual.Scale;

				FVector Forward = CameraLocation - Center;
				if (!Forward.Normalize())
				{
					Forward = FVector::UpVector;
				}
				FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
				if (!Right.Normalize())
				{
					Right = FVector::RightVector;
				}
				const FVector Up = FVector::CrossProduct(Forward, Right).GetSafeNormal();

				// Small<->large chaos.
				const float Half = FireSpriteHalfSizeCm * Visual.Scale * FMath::FRandRange(0.4f, 1.3f);

				// Colour by height with randomness: deep red/orange low, orange mid, yellow high;
				// dark grey smoke near the top.
				FLinearColor Color;
				const float H = FMath::Clamp(HeightFrac + FMath::FRandRange(-0.18f, 0.18f), 0.0f, 1.0f);
				if (HeightFrac > 0.62f && FMath::FRand() < 0.45f)
				{
					const float Grey = FMath::FRandRange(0.12f, 0.3f);
					Color = FLinearColor(Grey, Grey, Grey, 0.85f); // smoke
				}
				else if (H < 0.34f)
				{
					Color = FLinearColor(1.0f, FMath::FRandRange(0.15f, 0.38f), 0.03f, 0.92f);
				}
				else if (H < 0.66f)
				{
					Color = FLinearColor(1.0f, FMath::FRandRange(0.5f, 0.72f), FMath::FRandRange(0.0f, 0.12f), 0.92f);
				}
				else
				{
					Color = FLinearColor(1.0f, FMath::FRandRange(0.82f, 0.95f), FMath::FRandRange(0.2f, 0.45f), 0.92f);
				}

				const int32 Base = Vertices.Num();
				Vertices.Add(WorldToLocal.TransformPosition(Center - Right * Half - Up * Half));
				Vertices.Add(WorldToLocal.TransformPosition(Center + Right * Half - Up * Half));
				Vertices.Add(WorldToLocal.TransformPosition(Center + Right * Half + Up * Half));
				Vertices.Add(WorldToLocal.TransformPosition(Center - Right * Half + Up * Half));

				const FProcMeshTangent Tangent(WorldToLocal.TransformVectorNoScale(Right), false);
				for (int32 n = 0; n < 4; ++n)
				{
					Normals.Add(LocalNormalBase);
					Colors.Add(Color);
					Tangents.Add(Tangent);
				}
				UVs.Add(FVector2D(0.0f, 1.0f));
				UVs.Add(FVector2D(1.0f, 1.0f));
				UVs.Add(FVector2D(1.0f, 0.0f));
				UVs.Add(FVector2D(0.0f, 0.0f));

				Triangles.Add(Base + 0);
				Triangles.Add(Base + 1);
				Triangles.Add(Base + 2);
				Triangles.Add(Base + 0);
				Triangles.Add(Base + 2);
				Triangles.Add(Base + 3);
			}
		}
	}

	MeshComponent->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
	if (FlameMaterial != nullptr)
	{
		MeshComponent->SetMaterial(0, FlameMaterial);
	}
}
