// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterPopulationSprite.h"

#include "Engine/Texture2D.h"
#include "Formats/MaxisTextureReader.h"
#include "Formats/MaxisWindowsBitmapReader.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"

namespace
{
UTexture2D* CreateTextureFromImage(const FMaxisTextureImage& Image, UObject* Outer)
{
	if (Image.Width <= 0 || Image.Height <= 0 || Image.Pixels.Num() != Image.Width * Image.Height)
	{
		return nullptr;
	}

	UTexture2D* Texture = UTexture2D::CreateTransient(Image.Width, Image.Height, PF_B8G8R8A8);
	if (Texture == nullptr || Texture->GetPlatformData() == nullptr || Texture->GetPlatformData()->Mips.Num() == 0)
	{
		return nullptr;
	}

	if (Outer != nullptr)
	{
		Texture->Rename(*MakeUniqueObjectName(Outer, UTexture2D::StaticClass(), TEXT("SimCopterPeople1")).ToString(), Outer);
	}

	Texture->CompressionSettings = TC_VectorDisplacementmap;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->NeverStream = true;
	Texture->SRGB = true;
	Texture->Filter = TF_Nearest;

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, Image.Pixels.GetData(), Image.Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Texture->UpdateResource();

	return Texture;
}
}

bool FSimCopterPopulationSprite::IsPeople1Name(const FString& AssetName)
{
	return AssetName.Equals(TEXT("PEOPLE1"), ESearchCase::IgnoreCase) ||
		AssetName.StartsWith(TEXT("PEOPLE1:"), ESearchCase::IgnoreCase) ||
		AssetName.StartsWith(TEXT("PEOPLE1_"), ESearchCase::IgnoreCase);
}

int32 FSimCopterPopulationSprite::ResolvePeople1Column(const FString& AssetName, const UObject* StableObject)
{
	int32 RequestedColumn = INDEX_NONE;
	FString Prefix;
	FString Suffix;
	if (AssetName.Split(TEXT(":"), &Prefix, &Suffix) || AssetName.Split(TEXT("_"), &Prefix, &Suffix))
	{
		if (Prefix.Equals(TEXT("PEOPLE1"), ESearchCase::IgnoreCase))
		{
			RequestedColumn = FCString::Atoi(*Suffix);
		}
	}

	if (RequestedColumn != INDEX_NONE)
	{
		return FMath::Clamp(RequestedColumn, 0, People1Columns - 1);
	}

	const uint32 StableHash = StableObject != nullptr ? GetTypeHash(StableObject->GetFName()) : 0u;
	// Column 0 has a different non-cyan backdrop in the shipped sheet; reserve it
	// until the original draw flags are decoded so ambient crowds stay clean.
	return 1 + static_cast<int32>(StableHash % (People1Columns - 1));
}

FString FSimCopterPopulationSprite::ResolvePeople1BitmapPath(const FString& OriginalGameRoot)
{
	const FString TrimmedRoot = OriginalGameRoot.TrimStartAndEnd();
	if (TrimmedRoot.IsEmpty())
	{
		return FString();
	}

	const FString FullRoot = FPaths::IsRelative(TrimmedRoot)
		? FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TrimmedRoot))
		: FPaths::ConvertRelativePathToFull(TrimmedRoot);

	return FPaths::Combine(FullRoot, TEXT("BMP"), TEXT("PEOPLE1.BMP"));
}

bool FSimCopterPopulationSprite::LoadPeople1Texture(
	UObject* Outer,
	const FString& OriginalGameRoot,
	UTexture2D*& OutTexture,
	FString& OutError)
{
	OutTexture = nullptr;

	const FString BitmapPath = ResolvePeople1BitmapPath(OriginalGameRoot);
	if (BitmapPath.IsEmpty())
	{
		OutError = TEXT("Original game root is empty.");
		return false;
	}

	FMaxisTextureImage Image;
	if (!FMaxisWindowsBitmapReader::LoadPalettedBitmapFromFile(BitmapPath, Image, OutError, People1TransparentPaletteIndex))
	{
		return false;
	}

	if (Image.Width != People1FrameWidth * People1Columns || Image.Height != People1FrameHeight * People1Rows)
	{
		OutError = FString::Printf(TEXT("'%s' decoded to %dx%d, expected the PEOPLE1 sheet size %dx%d."),
			*BitmapPath,
			Image.Width,
			Image.Height,
			People1FrameWidth * People1Columns,
			People1FrameHeight * People1Rows);
		return false;
	}

	OutTexture = CreateTextureFromImage(Image, Outer);
	if (OutTexture == nullptr)
	{
		OutError = FString::Printf(TEXT("Could not create runtime texture for '%s'."), *BitmapPath);
		return false;
	}

	return true;
}

void FSimCopterPopulationSprite::BuildPeople1FrameQuad(
	UProceduralMeshComponent* MeshComponent,
	int32 FrameColumn,
	int32 FrameRow,
	float HeightCm)
{
	if (MeshComponent == nullptr)
	{
		return;
	}

	const int32 ClampedColumn = FMath::Clamp(FrameColumn, 0, People1Columns - 1);
	const int32 ClampedRow = FMath::Clamp(FrameRow, 0, People1Rows - 1);
	const float WidthCm = HeightCm * (static_cast<float>(People1FrameWidth) / static_cast<float>(People1FrameHeight));
	const float HalfWidthCm = WidthCm * 0.5f;

	const float U0 = static_cast<float>(ClampedColumn * People1FrameWidth) / static_cast<float>(People1FrameWidth * People1Columns);
	const float U1 = static_cast<float>((ClampedColumn + 1) * People1FrameWidth) / static_cast<float>(People1FrameWidth * People1Columns);
	const float V0 = static_cast<float>(ClampedRow * People1FrameHeight) / static_cast<float>(People1FrameHeight * People1Rows);
	const float V1 = static_cast<float>((ClampedRow + 1) * People1FrameHeight) / static_cast<float>(People1FrameHeight * People1Rows);

	TArray<FVector> Vertices;
	Vertices.Reserve(4);
	Vertices.Add(FVector(0.0f, -HalfWidthCm, 0.0f));
	Vertices.Add(FVector(0.0f, HalfWidthCm, 0.0f));
	Vertices.Add(FVector(0.0f, HalfWidthCm, HeightCm));
	Vertices.Add(FVector(0.0f, -HalfWidthCm, HeightCm));

	TArray<int32> Triangles = {0, 1, 2, 0, 2, 3};
	TArray<FVector> Normals;
	Normals.Init(FVector::ForwardVector, 4);
	TArray<FVector2D> UVs = {
		FVector2D(U0, V1),
		FVector2D(U1, V1),
		FVector2D(U1, V0),
		FVector2D(U0, V0)
	};
	TArray<FLinearColor> VertexColors;
	VertexColors.Init(FLinearColor::White, 4);
	TArray<FProcMeshTangent> Tangents;
	Tangents.Init(FProcMeshTangent(0.0f, 1.0f, 0.0f), 4);

	MeshComponent->ClearAllMeshSections();
	MeshComponent->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, false);
}
