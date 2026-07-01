// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/SimCopterDougContainerInternal.h"

namespace SimCopterDoug
{
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

bool ResolveChunk(const TArray<uint8>& D, const FDougDirectory& Dir, const FDirEntry& Entry, int64& OutPayload, uint32& OutLength, FString& OutError)
{
	const int64 ChunkAt = int64(Dir.DataBase) + Entry.ChunkOffset;
	if (ChunkAt + 4 > D.Num())
	{
		OutError = FString::Printf(TEXT("Chunk for %s out of range."), *Entry.Name);
		return false;
	}
	OutLength = ReadU32BE(D, ChunkAt);
	OutPayload = ChunkAt + 4;
	if (OutPayload + int64(OutLength) > int64(D.Num()))
	{
		OutError = FString::Printf(TEXT("Chunk payload for %s out of range."), *Entry.Name);
		return false;
	}
	return true;
}

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
	int64 Payload = 0;
	uint32 Length = 0;
	if (!ResolveChunk(D, Dir, Entry, Payload, Length, OutError))
	{
		return false;
	}
	if (Length < 8)
	{
		OutError = FString::Printf(TEXT("Chunk %s too small for a record array."), *Entry.Name);
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
} // namespace SimCopterDoug
