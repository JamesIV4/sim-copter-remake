// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Formats/MaxisTextureReader.h"

class FMaxisWindowsBitmapReader
{
public:
	static constexpr int32 NoTransparentPaletteIndex = INDEX_NONE;

	static bool LoadPalettedBitmapFromFile(
		const FString& FilePath,
		FMaxisTextureImage& OutImage,
		FString& OutError,
		int32 TransparentPaletteIndex = NoTransparentPaletteIndex);

	static bool LoadPalettedBitmapFromBytes(
		const TArray<uint8>& FileData,
		const FString& SourceName,
		FMaxisTextureImage& OutImage,
		FString& OutError,
		int32 TransparentPaletteIndex = NoTransparentPaletteIndex);
};
