// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/SimCopterPrivAnimReader.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Formats/SimCopterDougContainerInternal.h"

using namespace SimCopterDoug;

int32 FPrivAnimModel::FindFigureIndex(const FString& FigureName) const
{
	for (int32 Index = 0; Index < Figures.Num(); ++Index)
	{
		if (Figures[Index].Name == FigureName)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

const FPrivAnimClip* FPrivAnimModel::FindClip(const FPrivAnimFigure& Figure, const FString& Mnemonic) const
{
	if (const int32* ClipIndex = Figure.ClipIndexByMnemonic.Find(Mnemonic))
	{
		if (Clips.IsValidIndex(*ClipIndex))
		{
			return &Clips[*ClipIndex];
		}
	}
	return nullptr;
}

FString FSimCopterPrivAnimReader::ResolvePrivAnimPath(const FString& OriginalGameRoot)
{
	const TCHAR* Candidates[] = {TEXT("X/privanim.df"), TEXT("X/PrivAnim.df"), TEXT("X/PRIVANIM.DF")};
	for (const TCHAR* Candidate : Candidates)
	{
		const FString Path = FPaths::Combine(OriginalGameRoot, Candidate);
		if (FPaths::FileExists(Path))
		{
			return Path;
		}
	}
	return FString();
}

bool FSimCopterPrivAnimReader::LoadFromFile(const FString& FilePath, FPrivAnimModel& OutModel, FString& OutError)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		OutError = FString::Printf(TEXT("Failed to read '%s'."), *FilePath);
		return false;
	}
	return LoadFromBytes(FileData, OutModel, OutError);
}

bool FSimCopterPrivAnimReader::LoadFromBytes(const TArray<uint8>& FileData, FPrivAnimModel& OutModel, FString& OutError)
{
	OutModel = FPrivAnimModel();

	FDougDirectory Dir;
	if (!ParseDirectory(FileData, Dir, OutError))
	{
		return false;
	}

	const TArray<FDirEntry>* FigureEntries = Dir.Sections.Find(TEXT("BODC"));
	if (FigureEntries == nullptr || FigureEntries->Num() == 0)
	{
		OutError = TEXT("No BODC figure section found (is this privanim.df?).");
		return false;
	}

	TMap<FString, int32> ClipIndexByName;

	for (const FDirEntry& FigureEntry : *FigureEntries)
	{
		FPrivAnimFigure Figure;
		Figure.Name = FigureEntry.Name;

		// --- ARCP skeleton (0x28-byte part records; field map from swap handler FUN_004d0090
		// and the renderer FUN_004cfb30/FUN_004cf8f0) -------------------------------------------
		const FDirEntry* SkeletonEntry = Dir.FindEntry(TEXT("ARCP"), SubstituteKeyChar(Figure.Name, TEXT('c')));
		if (SkeletonEntry == nullptr)
		{
			OutError = FString::Printf(TEXT("Figure %s has no ARCP skeleton."), *Figure.Name);
			return false;
		}
		int32 Rows = 0, Cols = 0;
		int64 Data = 0;
		if (!ResolveRecordArray(FileData, Dir, *SkeletonEntry, 0x28, Rows, Cols, Data, OutError))
		{
			return false;
		}
		TMap<FString, int32> PartIndexByName;
		Figure.Parts.Reserve(Cols);
		for (int32 PartIndex = 0; PartIndex < Cols; ++PartIndex)
		{
			const int64 At = Data + int64(PartIndex) * 0x28;
			FPrivAnimPart Part;
			Part.Type = FileData[At];
			Part.Ref = FileData[At + 1];
			Part.Seq = FileData[At + 2];
			Part.ColorIndex = FileData[At + 3];
			Part.LodMask = FileData[At + 4];
			Part.FixedColor = FileData[At + 5];
			Part.Name = ReadChars(FileData, At + 8, 4);
			Part.Parent = ReadChars(FileData, At + 0xc, 4);
			Part.Dims = FVector3f(ReadF32BE(FileData, At + 0x1c), ReadF32BE(FileData, At + 0x20), ReadF32BE(FileData, At + 0x24));
			PartIndexByName.Add(Part.Name, PartIndex);
			Figure.Parts.Add(MoveTemp(Part));
		}
		for (FPrivAnimPart& Part : Figure.Parts)
		{
			if (const int32* Found = PartIndexByName.Find(Part.Parent))
			{
				Part.ParentIndex = *Found;
			}
		}

		// --- ARLU: mnemonic -> clip name (8-byte records [char4 mnemonic][char4 clip]) ----------
		const FDirEntry* ClipMapEntry = Dir.FindEntry(TEXT("ARLU"), SubstituteKeyChar(Figure.Name, TEXT('L')));
		if (ClipMapEntry == nullptr)
		{
			OutError = FString::Printf(TEXT("Figure %s has no ARLU clip map."), *Figure.Name);
			return false;
		}
		if (!ResolveRecordArray(FileData, Dir, *ClipMapEntry, 8, Rows, Cols, Data, OutError))
		{
			return false;
		}
		for (int32 MapIndex = 0; MapIndex < Cols; ++MapIndex)
		{
			const int64 At = Data + int64(MapIndex) * 8;
			const FString Mnemonic = ReadChars(FileData, At, 4);
			const FString ClipName = ReadChars(FileData, At + 4, 4);

			int32 ClipIndex;
			if (const int32* Existing = ClipIndexByName.Find(ClipName))
			{
				ClipIndex = *Existing;
			}
			else
			{
				// --- ARPP: frames x parts 8-byte pose records (raw bytes; empty swap handler
				// FUN_004cea20). Each record = the part's segment endpoints for that frame. ------
				const FDirEntry* PoseEntry = Dir.FindEntry(TEXT("ARPP"), SubstituteKeyChar(ClipName, TEXT('i')));
				if (PoseEntry == nullptr)
				{
					continue; // tolerate dangling clip references
				}
				int32 ClipRows = 0, ClipCols = 0;
				int64 ClipData = 0;
				if (!ResolveRecordArray(FileData, Dir, *PoseEntry, 8, ClipRows, ClipCols, ClipData, OutError))
				{
					return false;
				}
				FPrivAnimClip Clip;
				Clip.Name = ClipName;
				Clip.FrameCount = ClipRows;
				Clip.PartCount = ClipCols;
				Clip.Segments.Reserve(ClipRows * ClipCols);
				for (int32 RecordIndex = 0; RecordIndex < ClipRows * ClipCols; ++RecordIndex)
				{
					const int64 RecordAt = ClipData + int64(RecordIndex) * 8;
					FPrivAnimSegment Segment;
					Segment.A = {int8(FileData[RecordAt]), int8(FileData[RecordAt + 1]), int8(FileData[RecordAt + 2]), FileData[RecordAt + 3]};
					Segment.B = {int8(FileData[RecordAt + 4]), int8(FileData[RecordAt + 5]), int8(FileData[RecordAt + 6]), FileData[RecordAt + 7]};
					Clip.Segments.Add(Segment);
				}
				ClipIndex = OutModel.Clips.Add(MoveTemp(Clip));
				ClipIndexByName.Add(ClipName, ClipIndex);
			}
			Figure.ClipIndexByMnemonic.Add(Mnemonic, ClipIndex);
		}

		OutModel.Figures.Add(MoveTemp(Figure));
	}

	if (OutModel.Figures.Num() == 0)
	{
		OutError = TEXT("privanim.df contained no figures.");
		return false;
	}
	return true;
}
