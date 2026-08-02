// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SimCopterHangarArt.h"

#include "Engine/Texture2D.h"
#include "Flight/SimCopterHelicopterRegistry.h"
#include "Formats/MaxisWindowsBitmapReader.h"
#include "Ground/SimCopterPopulationSprite.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
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
	StopMenuSkyMovie();
	MenuSkyMovieBrush.Reset();
	MenuSkyTexture = nullptr;
	MenuSkyPlayer = nullptr;
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
	return BuildBrush(FileName, FileName, bColorKeyed, FIntRect(), ESimCopterArtRotation::None);
}

void USimCopterHangarArt::RegisterRuntimeTexture(const FString& Key, UTexture2D* Texture)
{
	if (Texture != nullptr)
	{
		Textures.Add(Key, Texture);
	}
}

UTexture2D* USimCopterHangarArt::FindRuntimeTexture(const FString& Key) const
{
	const TObjectPtr<UTexture2D>* Found = Textures.Find(Key);
	return (Found != nullptr) ? Found->Get() : nullptr;
}

const FSlateBrush* USimCopterHangarArt::GetBundledSlateImage(const FString& FileName)
{
	const FString CacheKey = FString::Printf(TEXT("Slate/%s"), *FileName);
	if (const TSharedPtr<FSlateBrush>* Existing = Brushes.Find(CacheKey))
	{
		return Existing->IsValid() ? Existing->Get() : nullptr;
	}

	Brushes.Add(CacheKey, nullptr);

	const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Slate"), FileName);
	UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(Path);
	if (Texture == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("SimCopter bundled art: could not load '%s'."), *Path);
		return nullptr;
	}

	Texture->Filter = TF_Bilinear;
	Texture->UpdateResource();
	Textures.Add(CacheKey, Texture);

	TSharedRef<FSlateBrush> Brush = MakeShared<FSlateBrush>();
	Brush->SetResourceObject(Texture);
	Brush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
	Brush->DrawAs = ESlateBrushDrawType::Image;
	Brushes.Add(CacheKey, Brush);

	return &Brush.Get();
}

FString USimCopterHangarArt::ResolveMenuSkyMoviePath() const
{
	// Generated is intentionally gitignored alongside the user's original art. The bake tool
	// preserves the original frame count and timing while changing only the unsupported Smacker
	// container/codec into a Media Foundation-readable MP4.
	TArray<FString, TInlineAllocator<3>> Candidates;
	Candidates.Add(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Generated/Movies/MENUSKY.mp4")));
	Candidates.Add(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Movies/MENUSKY.mp4")));
	if (!OriginalGameRoot.IsEmpty())
	{
		Candidates.Add(FPaths::Combine(OriginalGameRoot, TEXT("SMK/MENUSKY.mp4")));
	}

	for (FString Candidate : Candidates)
	{
		Candidate = FPaths::ConvertRelativePathToFull(Candidate);
		FPaths::NormalizeFilename(Candidate);
		if (FPaths::FileExists(Candidate))
		{
			return Candidate;
		}
	}
	return FString();
}

const FSlateBrush* USimCopterHangarArt::GetMenuSkyMovieBrush()
{
	const FString MoviePath = ResolveMenuSkyMoviePath();
	if (MoviePath.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SimCopter front end: MENUSKY.mp4 has not been baked; run Tools/Unreal/BakeMenuSky.py."));
		return nullptr;
	}

	if (MenuSkyPlayer == nullptr)
	{
		MenuSkyPlayer = NewObject<UMediaPlayer>(this, TEXT("OriginalMenuSkyPlayer"));
		MenuSkyPlayer->PlayOnOpen = true;
	}
	if (MenuSkyTexture == nullptr)
	{
		MenuSkyTexture = NewObject<UMediaTexture>(this, TEXT("OriginalMenuSkyTexture"));
		MenuSkyTexture->AutoClear = true;
		MenuSkyTexture->ClearColor = FLinearColor(0.28f, 0.58f, 0.72f, 1.0f);
		MenuSkyTexture->NewStyleOutput = true;
		MenuSkyTexture->Filter = TF_Bilinear;
		MenuSkyTexture->SetMediaPlayer(MenuSkyPlayer);
		MenuSkyTexture->UpdateResource();
	}
	if (!MenuSkyMovieBrush.IsValid())
	{
		MenuSkyMovieBrush = MakeShared<FSlateBrush>();
		MenuSkyMovieBrush->SetResourceObject(MenuSkyTexture);
		MenuSkyMovieBrush->ImageSize = FVector2D(640.0f, 480.0f);
		MenuSkyMovieBrush->DrawAs = ESlateBrushDrawType::Image;
	}

	// SCHOOK: MainMenuSkyMovie 0x0044d070. The original opens MENUSKY.SMK, binds the display
	// palette, then writes 1 to movie+8: its loop flag. OpenFile is asynchronous, just as the
	// original movie object is; PlayOnOpen starts it once Media Foundation has the first sample.
	MenuSkyPlayer->SetLooping(true);
	if (!MenuSkyPlayer->OpenFile(MoviePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("SimCopter front end: could not open '%s'."), *MoviePath);
		return nullptr;
	}

	return MenuSkyMovieBrush.Get();
}

void USimCopterHangarArt::StopMenuSkyMovie()
{
	if (MenuSkyPlayer != nullptr)
	{
		MenuSkyPlayer->Close();
	}
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

	// The frame width is only known once the bitmap is loaded, so the split stays in BuildBrush;
	// an all-zero rect with a frame count encoded in the key would not survive the cache. Load
	// the whole bitmap once to size the frame.
	const FSlateBrush* Whole = GetBitmap(FileName, /*bColorKeyed=*/true);
	if (Whole == nullptr)
	{
		return nullptr;
	}

	const int32 FrameWidth = FMath::FloorToInt(Whole->ImageSize.X) / FrameCount;
	if (FrameWidth <= 0)
	{
		return nullptr;
	}

	return GetSubImage(
		FileName,
		FIntRect(FrameIndex * FrameWidth, 0, (FrameIndex + 1) * FrameWidth, FMath::FloorToInt(Whole->ImageSize.Y)),
		/*bColorKeyed=*/true);
}

const FSlateBrush* USimCopterHangarArt::GetSubImage(
	const FString& FileName,
	const FIntRect& Source,
	const bool bColorKeyed,
	const ESimCopterArtRotation Rotation)
{
	if (Source.Min.X < 0 || Source.Min.Y < 0 || Source.Width() <= 0 || Source.Height() <= 0)
	{
		return nullptr;
	}

	const FString CacheKey = FString::Printf(
		TEXT("%s@%d,%d,%d,%d%s r%d"),
		*FileName,
		Source.Min.X,
		Source.Min.Y,
		Source.Max.X,
		Source.Max.Y,
		bColorKeyed ? TEXT("k") : TEXT(""),
		static_cast<int32>(Rotation));
	return BuildBrush(CacheKey, FileName, bColorKeyed, Source, Rotation);
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
	const FIntRect& Source,
	const ESimCopterArtRotation Rotation)
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

	if (Source.Width() > 0 && Source.Height() > 0)
	{
		// A sub-rectangle: one frame of a button strip, or one cell of a page.
		const FIntRect Clipped(
			FMath::Clamp(Source.Min.X, 0, Image.Width),
			FMath::Clamp(Source.Min.Y, 0, Image.Height),
			FMath::Clamp(Source.Max.X, 0, Image.Width),
			FMath::Clamp(Source.Max.Y, 0, Image.Height));
		if (Clipped.Width() <= 0 || Clipped.Height() <= 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("SimCopter art: rect (%d,%d)-(%d,%d) is outside '%s' (%dx%d)."),
				Source.Min.X, Source.Min.Y, Source.Max.X, Source.Max.Y, *FileName, Image.Width, Image.Height);
			return nullptr;
		}

		FMaxisTextureImage Frame;
		Frame.Width = Clipped.Width();
		Frame.Height = Clipped.Height();
		Frame.Pixels.SetNumUninitialized(Frame.Width * Frame.Height);
		for (int32 Y = 0; Y < Frame.Height; ++Y)
		{
			FMemory::Memcpy(
				&Frame.Pixels[Y * Frame.Width],
				&Image.Pixels[(Clipped.Min.Y + Y) * Image.Width + Clipped.Min.X],
				Frame.Width * sizeof(FColor));
		}
		Image = MoveTemp(Frame);
	}

	if (Rotation != ESimCopterArtRotation::None)
	{
		FMaxisTextureImage Turned;
		Turned.Width = Image.Height;
		Turned.Height = Image.Width;
		Turned.Pixels.SetNumUninitialized(Turned.Width * Turned.Height);
		for (int32 Y = 0; Y < Image.Height; ++Y)
		{
			for (int32 X = 0; X < Image.Width; ++X)
			{
				const int32 TurnedX = Rotation == ESimCopterArtRotation::Clockwise90
					? Image.Height - 1 - Y
					: Y;
				const int32 TurnedY = Rotation == ESimCopterArtRotation::Clockwise90
					? X
					: Image.Width - 1 - X;
				Turned.Pixels[TurnedY * Turned.Width + TurnedX] = Image.Pixels[Y * Image.Width + X];
			}
		}
		Image = MoveTemp(Turned);
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
