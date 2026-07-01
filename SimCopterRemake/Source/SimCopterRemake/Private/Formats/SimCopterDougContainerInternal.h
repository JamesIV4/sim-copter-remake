// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Shared internals for reading Maxis "Doug" DF containers (privanim.df, people.df).
// Every rule cites the decompiled SimCopter.exe function it was derived from
// (see Docs/OriginalGameFileFormats.md "Exact Container Spec").
namespace SimCopterDoug
{
inline uint32 ReadU32BE(const TArray<uint8>& D, int64 Offset)
{
	return (uint32(D[Offset]) << 24) | (uint32(D[Offset + 1]) << 16) | (uint32(D[Offset + 2]) << 8) | uint32(D[Offset + 3]);
}

inline uint16 ReadU16BE(const TArray<uint8>& D, int64 Offset)
{
	return uint16((uint16(D[Offset]) << 8) | uint16(D[Offset + 1]));
}

inline float ReadF32BE(const TArray<uint8>& D, int64 Offset)
{
	const uint32 Bits = ReadU32BE(D, Offset);
	float Value;
	FMemory::Memcpy(&Value, &Bits, sizeof(float));
	return Value;
}

inline FString ReadChars(const TArray<uint8>& D, int64 Offset, int32 Count)
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
inline FString SubstituteKeyChar(const FString& Name, TCHAR KeyChar)
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
bool ParseDirectory(const TArray<uint8>& D, FDougDirectory& OutDir, FString& OutError);

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
	FString& OutError);

// Resolves a raw chunk: returns (payload offset, payload length) or false.
bool ResolveChunk(const TArray<uint8>& D, const FDougDirectory& Dir, const FDirEntry& Entry, int64& OutPayload, uint32& OutLength, FString& OutError);
} // namespace SimCopterDoug
