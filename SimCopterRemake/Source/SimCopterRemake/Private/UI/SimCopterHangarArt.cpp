// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SimCopterHangarArt.h"

#include "Engine/Texture2D.h"
#include "Flight/SimCopterHelicopterRegistry.h"
#include "Formats/MaxisWindowsBitmapReader.h"
#include "Ground/SimCopterPopulationSprite.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Styling/SlateBrush.h"

namespace
{
// cat_<model>.bmp, in catalog row order.
const TCHAR* const CatalogDrawingFiles[SimCopterHangarLayout::CatalogTabCount] = {
	TEXT("CAT_SCHW.BMP"), // 0 Schweizer 300
	TEXT("CAT_JET.BMP"),  // 1 Jet Ranger
	TEXT("CAT_HUGH.BMP"), // 2 MD 500 (the exe calls it Hughes 500)
	TEXT("CAT_MD5.BMP"),  // 3 MD 520
	TEXT("CAT_BELL.BMP"), // 4 Bell 212
	TEXT("CAT_AUG.BMP"),  // 5 Agusta
	TEXT("CAT_DAUP.BMP"), // 6 Dauphin
	TEXT("CAT_MDE.BMP"),  // 7 MD Explorer
};

// cat_<model>t.bmp, same order, plus the upgrades page's strip.
const TCHAR* const CatalogTabStripFiles[SimCopterHangarLayout::CatalogTabCount] = {
	TEXT("CAT_SCHT.BMP"),
	TEXT("CAT_JETT.BMP"),
	TEXT("CAT_HUGT.BMP"),
	TEXT("CAT_MD5T.BMP"),
	TEXT("CAT_BELT.BMP"),
	TEXT("CAT_AUGT.BMP"),
	TEXT("CAT_DAUT.BMP"),
	TEXT("CAT_MDET.BMP"),
};

const TCHAR* const UpgradesTabStripFile = TEXT("CAT_EQUT.BMP");
}

void USimCopterHangarArt::SetOriginalGameRoot(const FString& InOriginalGameRoot)
{
	if (OriginalGameRoot == InOriginalGameRoot)
	{
		return;
	}

	OriginalGameRoot = InOriginalGameRoot;
	Textures.Reset();
	Brushes.Reset();
}

bool USimCopterHangarArt::IsUsable() const
{
	return !OriginalGameRoot.IsEmpty() && FPaths::DirectoryExists(FPaths::Combine(OriginalGameRoot, TEXT("BMP")));
}

FString USimCopterHangarArt::ResolveBitmapPath(const FString& FileName) const
{
	if (OriginalGameRoot.IsEmpty() || FileName.IsEmpty())
	{
		return FString();
	}

	// The shipped folder is upper case on the reference install, but a copy made on a
	// case-sensitive filesystem may not be, so try the obvious spellings before giving up.
	const TCHAR* const Folders[] = { TEXT("BMP"), TEXT("bmp") };
	const FString Names[] = { FileName, FileName.ToUpper(), FileName.ToLower() };

	for (const TCHAR* Folder : Folders)
	{
		for (const FString& Name : Names)
		{
			const FString Candidate = FPaths::Combine(OriginalGameRoot, Folder, Name);
			if (IFileManager::Get().FileExists(*Candidate))
			{
				return Candidate;
			}
		}
	}

	return FString();
}

const FSlateBrush* USimCopterHangarArt::GetBitmap(const FString& FileName, const bool bColorKeyed)
{
	return BuildBrush(FileName, FileName, bColorKeyed, 0, 1);
}

const FSlateBrush* USimCopterHangarArt::GetStripFrame(
	const FString& FileName,
	const int32 FrameIndex,
	const int32 FrameCount)
{
	if (FrameCount <= 0 || FrameIndex < 0 || FrameIndex >= FrameCount)
	{
		return nullptr;
	}

	const FString CacheKey = FString::Printf(TEXT("%s#%d/%d"), *FileName, FrameIndex, FrameCount);
	return BuildBrush(CacheKey, FileName, /*bColorKeyed=*/true, FrameIndex, FrameCount);
}

const FSlateBrush* USimCopterHangarArt::GetCatalogDrawing(const int32 CatalogRow)
{
	if (CatalogRow < 0 || CatalogRow >= SimCopterHangarLayout::CatalogTabCount)
	{
		return nullptr;
	}
	return GetBitmap(CatalogDrawingFiles[CatalogRow]);
}

const FSlateBrush* USimCopterHangarArt::GetCatalogTabStrip(const int32 CatalogRow)
{
	const bool bUpgrades = CatalogRow < 0 || CatalogRow >= SimCopterHangarLayout::CatalogTabCount;
	return GetBitmap(bUpgrades ? UpgradesTabStripFile : CatalogTabStripFiles[CatalogRow]);
}

const FSlateBrush* USimCopterHangarArt::BuildBrush(
	const FString& CacheKey,
	const FString& FileName,
	const bool bColorKeyed,
	const int32 FrameIndex,
	const int32 FrameCount)
{
	if (const TSharedPtr<FSlateBrush>* Existing = Brushes.Find(CacheKey))
	{
		// A cached miss is stored as an empty entry so a missing file is only looked for once.
		return Existing->IsValid() ? Existing->Get() : nullptr;
	}

	Brushes.Add(CacheKey, nullptr);

	const FString Path = ResolveBitmapPath(FileName);
	if (Path.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("SimCopter hangar art: '%s' is not in the original game's BMP folder."), *FileName);
		return nullptr;
	}

	FMaxisTextureImage Image;
	FString Error;
	if (!FMaxisWindowsBitmapReader::LoadPalettedBitmapFromFile(
			Path,
			Image,
			Error,
			bColorKeyed ? TransparentPaletteIndex : FMaxisWindowsBitmapReader::NoTransparentPaletteIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("SimCopter hangar art: %s"), *Error);
		return nullptr;
	}

	if (FrameCount > 1)
	{
		// Horizontal strip: keep only the requested frame.
		const int32 FrameWidth = Image.Width / FrameCount;
		if (FrameWidth <= 0)
		{
			return nullptr;
		}

		FMaxisTextureImage Frame;
		Frame.Width = FrameWidth;
		Frame.Height = Image.Height;
		Frame.Pixels.SetNumUninitialized(FrameWidth * Image.Height);
		const int32 SourceX = FrameIndex * FrameWidth;
		for (int32 Y = 0; Y < Image.Height; ++Y)
		{
			FMemory::Memcpy(
				&Frame.Pixels[Y * FrameWidth],
				&Image.Pixels[Y * Image.Width + SourceX],
				FrameWidth * sizeof(FColor));
		}
		Image = MoveTemp(Frame);
	}

	UTexture2D* Texture = FSimCopterPopulationSprite::CreateTextureFromImage(this, Image, TEXT("SimCopterHangarArt"));
	if (Texture == nullptr)
	{
		return nullptr;
	}

	// The sprite path wants nearest for 27x33 people; a full page scaled up to a modern viewport
	// wants the opposite, so re-upload with filtering on.
	Texture->Filter = TF_Bilinear;
	Texture->UpdateResource();

	Textures.Add(CacheKey, Texture);

	TSharedRef<FSlateBrush> Brush = MakeShared<FSlateBrush>();
	Brush->SetResourceObject(Texture);
	Brush->ImageSize = FVector2D(Image.Width, Image.Height);
	Brush->DrawAs = ESlateBrushDrawType::Image;
	Brushes.Add(CacheKey, Brush);

	return &Brush.Get();
}

namespace SimCopterHangarLayout
{
int32 GetCatalogRowForTypeIndex(const int32 TypeIndex)
{
	const FSimCopterHelicopterDefinition* Definition = SimCopterHelicopterRegistry::FindByTypeIndex(TypeIndex);
	return Definition != nullptr ? Definition->CatalogIndex : INDEX_NONE;
}

int32 GetTypeIndexForCatalogRow(const int32 CatalogRow)
{
	if (CatalogRow < 0 || CatalogRow >= CatalogTabCount)
	{
		return INDEX_NONE;
	}

	for (const FSimCopterHelicopterDefinition& Definition : SimCopterHelicopterRegistry::GetDefinitions())
	{
		if (Definition.CatalogIndex == CatalogRow)
		{
			return Definition.InternalTypeIndex;
		}
	}

	return INDEX_NONE;
}
}
