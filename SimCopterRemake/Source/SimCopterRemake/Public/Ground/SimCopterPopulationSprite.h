// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UProceduralMeshComponent;
class UTexture2D;

class FSimCopterPopulationSprite
{
public:
	static constexpr int32 People1FrameWidth = 27;
	static constexpr int32 People1FrameHeight = 33;
	static constexpr int32 People1Columns = 12;
	static constexpr int32 People1Rows = 3;
	static constexpr int32 People1TransparentPaletteIndex = 254;

	static bool IsPeople1Name(const FString& AssetName);
	static int32 ResolvePeople1Column(const FString& AssetName, const UObject* StableObject);
	static FString ResolvePeople1BitmapPath(const FString& OriginalGameRoot);

	static bool LoadPeople1Texture(
		UObject* Outer,
		const FString& OriginalGameRoot,
		UTexture2D*& OutTexture,
		FString& OutError);

	static void BuildPeople1FrameQuad(
		UProceduralMeshComponent* MeshComponent,
		int32 FrameColumn,
		int32 FrameRow,
		float HeightCm);
};
