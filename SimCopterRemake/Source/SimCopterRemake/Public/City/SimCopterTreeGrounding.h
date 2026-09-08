#pragma once

#include "CoreMinimal.h"
#include "Formats/MaxisTextureReader.h"
#include "Components/PrimitiveComponent.h"

namespace SimCopterTreeGrounding
{
// Remake crossed-card placement. V=0 is the card bottom in AppendMaxisSpriteCard;
// use only its lowest occupied row. Branch undersides are not ground contacts.
inline TArray<FVector> BuildBottomSamples(const FMaxisTextureImage& Image,
	const FVector& Center, float HalfWidth, float HalfHeight)
{
	TArray<FVector> Samples;
	if (Image.Width <= 0 || Image.Height <= 0 || Image.Pixels.Num() != Image.Width * Image.Height) return Samples;
	for (int32 Y = 0; Y < Image.Height; ++Y)
	{
		for (int32 X = 0; X < Image.Width; ++X)
		{
			if (Image.Pixels[Y * Image.Width + X].A == 0) continue;
			const float Across = ((X + 0.5f) / Image.Width * 2.0f - 1.0f) * HalfWidth;
			const float Height = (static_cast<float>(Y) / Image.Height * 2.0f - 1.0f) * HalfHeight;
			// Both quads run from positive to negative world X/Y after city yaw.
			Samples.Add(Center + FVector(-Across, 0, Height));
			Samples.Add(Center + FVector(0, -Across, Height));
		}
		if (!Samples.IsEmpty()) break;
	}
	return Samples;
}

inline float PlacementOffset(const TArray<FVector>& Samples, const FVector& Origin,
	TFunctionRef<float(const FVector&)> TerrainHeight)
{
	if (Samples.IsEmpty()) return 0.0f;
	float Offset = TNumericLimits<float>::Max();
	for (const FVector& Sample : Samples)
	{
		const FVector Point = Origin + Sample;
		Offset = FMath::Min(Offset, TerrainHeight(Point) - static_cast<float>(Point.Z));
	}
	// The largest gap closes exactly; every other bottom pixel is at/below terrain.
	// Positive offsets also lift an over-buried tree. Never solve quads separately.
	return Offset;
}

inline bool TracePlacementOffset(UPrimitiveComponent& Terrain, const TArray<FVector>& Samples,
	const FVector& Origin, float& OutOffset)
{
	OutOffset = 0;
	if (Samples.IsEmpty()) return false;
	const FTransform Transform = Terrain.GetComponentTransform();
	const FBox Bounds = Terrain.Bounds.GetBox();
	float Offset = TNumericLimits<float>::Max();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TreeGrounding), true);
	for (const FVector& Sample : Samples)
	{
		const FVector Local = Origin + Sample;
		const FVector World = Transform.TransformPosition(Local);
		FHitResult Hit;
		// Trace this component only: trees, roofs and other cards cannot become ground.
		if (!Terrain.LineTraceComponent(Hit, FVector(World.X, World.Y, Bounds.Max.Z + 1000),
			FVector(World.X, World.Y, Bounds.Min.Z - 1000), Params)) return false;
		Offset = FMath::Min(Offset, static_cast<float>(Transform.InverseTransformPosition(Hit.ImpactPoint).Z - Local.Z));
	}
	OutOffset = Offset;
	return true;
}
}
