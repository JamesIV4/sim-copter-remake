#pragma once

#include "CoreMinimal.h"

struct SIMCOPTERREMAKE_API FSimCopterFigurePieceAxes
{
	FVector X = FVector::ForwardVector;
	FVector Y = FVector::RightVector;
	FVector Z = FVector::UpVector;
	static FSimCopterFigurePieceAxes Stroke(const FVector& A, const FVector& B, float DepthRatio);
	FVector ToLocal(const FVector& Point) const;
	FVector ToFigure(const FVector& Point) const;
	FVector ScaleNormal(const FVector& Normal, const FVector& Scale) const;
};

// Remake-only art overrides. Scale uses piece depth/width/length axes.
// Offset uses original model units so edits survive population/character height changes.
struct SIMCOPTERREMAKE_API FSimCopterFigurePartAdjustment
{
	FVector Offset = FVector::ZeroVector;
	FVector Scale = FVector::OneVector;
	bool bVisible = true;

	FVector Apply(const FVector& Vertex, const FVector& Center, float CmPerUnit,
		const FSimCopterFigurePieceAxes& Axes = FSimCopterFigurePieceAxes()) const;
};

class SIMCOPTERREMAKE_API FSimCopterFigureAdjustments
{
public:
	TMap<FString, TMap<FString, FSimCopterFigurePartAdjustment>> Figures;
	bool Parse(const FString& Json, FString& OutError);
	static const FSimCopterFigureAdjustments& Get();
};
