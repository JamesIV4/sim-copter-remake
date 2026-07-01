// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/SimCopterPrivAnimReader.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
// Big-endian field readers over the raw file bytes. Every offset rule below cites the
// decompiled SimCopter.exe function it was derived from (see Docs/OriginalGameFileFormats.md).
uint32 ReadU32BE(const TArray<uint8>& D, int64 Offset)
{
	return (uint32(D[Offset]) << 24) | (uint32(D[Offset + 1]) << 16) | (uint32(D[Offset + 2]) << 8) | uint32(D[Offset + 3]);
}

uint16 ReadU16BE(const TArray<uint8>& D, int64 Offset)
{
	return uint16((uint16(D[Offset]) << 8) | uint16(D[Offset + 1]));
}

float ReadF32BE(const TArray<uint8>& D, int64 Offset)
{
	const uint32 Bits = ReadU32BE(D, Offset);
	float Value;
	FMemory::Memcpy(&Value, &Bits, sizeof(float));
	return Value;
}

FString ReadChars(const TArray<uint8>& D, int64 Offset, int32 Count)
{
	FString Out;
	Out.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Out.AppendChar(TCHAR(D[Offset + Index]));
	}
	return Out;
}

struct FDirEntry
{
	uint16 Id = 0;
	FString Name;
	uint8 Flags = 0;
	uint32 ChunkOffset = 0;
};

struct FDougDirectory
{
	uint32 DataBase = 0;
	// Section tag -> entries (in directory order).
	TMap<FString, TArray<FDirEntry>> Sections;

	const FDirEntry* FindEntry(const FString& Tag, const FString& Name) const
	{
		if (const TArray<FDirEntry>* Entries = Sections.Find(Tag))
		{
			for (const FDirEntry& Entry : *Entries)
			{
				if (Entry.Name == Name)
				{
					return &Entry;
				}
			}
		}
		return nullptr;
	}
};

// The record arrays for a node are keyed by replacing the 4th character of the node name:
// figure 'c' -> ARCP skeleton, 'L' -> ARLU clip map, clip 'i' -> ARPP poses (FUN_004cfed0 /
// FUN_004d18e0 write the substitute char at name[3]).
FString SubstituteKeyChar(const FString& Name, TCHAR KeyChar)
{
	FString Key = Name;
	if (Key.Len() > 3)
	{
		Key[3] = KeyChar;
	}
	return Key;
}

// Parses the container directory (header FUN_004cd3e0; blob load FUN_004cdb50/FUN_004cda40;
// section swap FUN_004cdfe0; entry swap FUN_004ce010; names FUN_004cdfa0).
bool ParseDirectory(const TArray<uint8>& D, FDougDirectory& OutDir, FString& OutError)
{
	if (D.Num() < 0x20)
	{
		OutError = TEXT("File too small for a DF header.");
		return false;
	}

	OutDir.DataBase = ReadU32BE(D, 0);
	const uint32 DirOffset = ReadU32BE(D, 4);
	const uint32 DirSize = ReadU32BE(D, 12);
	if (DirOffset + 0x1eu >= uint32(D.Num()) || DirSize < 0x1eu)
	{
		OutError = TEXT("DF directory header out of range.");
		return false;
	}

	const int32 SectionCount = int32(ReadU16BE(D, DirOffset + 0x1c)) + 1;
	const int64 Blob = int64(DirOffset) + 0x1e;
	if (Blob + SectionCount * 8 > D.Num())
	{
		OutError = TEXT("DF section table out of range.");
		return false;
	}

	struct FSectionHeader
	{
		FString Tag;
		int32 Count = 0;
		int32 EntryOffset = 0;
	};
	TArray<FSectionHeader> Headers;
	int32 TotalEntries = 0;
	for (int32 Index = 0; Index < SectionCount; ++Index)
	{
		const int64 At = Blob + Index * 8;
		FSectionHeader Header;
		Header.Tag = ReadChars(D, At, 4);
		Header.Count = ReadU16BE(D, At + 4);
		Header.EntryOffset = ReadU16BE(D, At + 6);
		TotalEntries += Header.Count + 1; // +1 separator slot per section
		Headers.Add(MoveTemp(Header));
	}

	const int64 StringTable = Blob + SectionCount * 8 + int64(TotalEntries) * 12;
	if (StringTable > D.Num())
	{
		OutError = TEXT("DF string table out of range.");
		return false;
	}

	for (const FSectionHeader& Header : Headers)
	{
		TArray<FDirEntry>& Entries = OutDir.Sections.Add(Header.Tag);
		for (int32 Index = 0; Index < Header.Count; ++Index)
		{
			const int64 At = Blob + Header.EntryOffset - 2 + int64(Index) * 12;
			if (At + 12 > D.Num())
			{
				OutError = FString::Printf(TEXT("DF node entry out of range in section %s."), *Header.Tag);
				return false;
			}
			FDirEntry Entry;
			Entry.Id = ReadU16BE(D, At);
			const uint16 NameOffset = ReadU16BE(D, At + 2);
			Entry.Flags = D[At + 4];
			Entry.ChunkOffset = (uint32(D[At + 5]) << 16) | (uint32(D[At + 6]) << 8) | uint32(D[At + 7]);

			const int64 NameAt = StringTable + NameOffset;
			if (NameAt < D.Num())
			{
				const int32 NameLen = D[NameAt];
				if (NameAt + 1 + NameLen <= D.Num())
				{
					Entry.Name = ReadChars(D, NameAt + 1, NameLen);
				}
			}
			Entries.Add(MoveTemp(Entry));
		}
	}
	return true;
}

// Resolves a record-array chunk `[BE u32 len][u16 recSize][u16 rows][u16 cols][2][rows*4][data]`
// (FUN_004cdcb0 chunk read; FUN_004d1a00 header; FUN_004d1df0/FUN_004d1d70 layout).
bool ResolveRecordArray(
	const TArray<uint8>& D,
	const FDougDirectory& Dir,
	const FDirEntry& Entry,
	int32 ExpectedRecordSize,
	int32& OutRows,
	int32& OutCols,
	int64& OutDataOffset,
	FString& OutError)
{
	const int64 ChunkAt = int64(Dir.DataBase) + Entry.ChunkOffset;
	if (ChunkAt + 4 > D.Num())
	{
		OutError = FString::Printf(TEXT("Chunk for %s out of range."), *Entry.Name);
		return false;
	}
	const uint32 Length = ReadU32BE(D, ChunkAt);
	const int64 Payload = ChunkAt + 4;
	if (Payload + int64(Length) > int64(D.Num()) || Length < 8)
	{
		OutError = FString::Printf(TEXT("Chunk payload for %s out of range."), *Entry.Name);
		return false;
	}

	const int32 RecordSize = ReadU16BE(D, Payload);
	OutRows = ReadU16BE(D, Payload + 2);
	OutCols = ReadU16BE(D, Payload + 4);
	const int64 Expected = 8 + int64(OutRows) * 4 + int64(OutRows) * OutCols * RecordSize;
	if (RecordSize != ExpectedRecordSize || Expected != Length)
	{
		OutError = FString::Printf(
			TEXT("Record array %s has recSize=%d rows=%d cols=%d len=%u (expected recSize=%d len=%lld)."),
			*Entry.Name, RecordSize, OutRows, OutCols, Length, ExpectedRecordSize, Expected);
		return false;
	}
	OutDataOffset = Payload + 8 + int64(OutRows) * 4;
	return true;
}
} // namespace

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
