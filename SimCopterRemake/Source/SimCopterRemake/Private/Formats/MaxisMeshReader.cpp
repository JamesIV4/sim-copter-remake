// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/MaxisMeshReader.h"

#include "Algo/Sort.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
bool CanRead(const TArray<uint8>& Data, int32 Offset, int32 Size)
{
	return Offset >= 0 && Size >= 0 && Offset <= Data.Num() && Size <= Data.Num() - Offset;
}

uint16 ReadUInt16LE(const TArray<uint8>& Data, int32 Offset)
{
	return static_cast<uint16>(Data[Offset]) | (static_cast<uint16>(Data[Offset + 1]) << 8);
}

uint32 ReadUInt32LE(const TArray<uint8>& Data, int32 Offset)
{
	return static_cast<uint32>(Data[Offset]) |
		(static_cast<uint32>(Data[Offset + 1]) << 8) |
		(static_cast<uint32>(Data[Offset + 2]) << 16) |
		(static_cast<uint32>(Data[Offset + 3]) << 24);
}

int32 ReadInt32LE(const TArray<uint8>& Data, int32 Offset)
{
	return static_cast<int32>(ReadUInt32LE(Data, Offset));
}

FString ReadFourCC(const TArray<uint8>& Data, int32 Offset)
{
	ANSICHAR Buffer[5] = {
		static_cast<ANSICHAR>(Data[Offset]),
		static_cast<ANSICHAR>(Data[Offset + 1]),
		static_cast<ANSICHAR>(Data[Offset + 2]),
		static_cast<ANSICHAR>(Data[Offset + 3]),
		'\0'
	};

	return FString(ANSI_TO_TCHAR(Buffer));
}

FString ReadFixedAsciiString(const TArray<uint8>& Data, int32 Offset, int32 MaxLength)
{
	ANSICHAR Buffer[128] = {};
	const int32 CopyLength = FMath::Min(MaxLength, static_cast<int32>(UE_ARRAY_COUNT(Buffer) - 1));
	for (int32 Index = 0; Index < CopyLength && Offset + Index < Data.Num(); ++Index)
	{
		const uint8 Value = Data[Offset + Index];
		if (Value == 0)
		{
			break;
		}

		Buffer[Index] = static_cast<ANSICHAR>(Value);
	}

	return FString(ANSI_TO_TCHAR(Buffer));
}

bool RequireFourCC(const TArray<uint8>& Data, int32 Offset, const TCHAR* Expected, const FString& SourceName, FString& OutError)
{
	if (!CanRead(Data, Offset, 4))
	{
		OutError = FString::Printf(TEXT("'%s' is truncated before expected marker '%s' at byte %d."), *SourceName, Expected, Offset);
		return false;
	}

	const FString Found = ReadFourCC(Data, Offset);
	if (!Found.Equals(Expected, ESearchCase::CaseSensitive))
	{
		OutError = FString::Printf(TEXT("'%s' expected marker '%s' at byte %d but found '%s'."), *SourceName, Expected, Offset, *Found);
		return false;
	}

	return true;
}

bool ParseColorMap(const TArray<uint8>& FileData, FMaxisMeshFile& OutMeshFile, FString& OutError)
{
	if (!RequireFourCC(FileData, 28, TEXT("CMAP"), OutMeshFile.SourceFile, OutError))
	{
		return false;
	}

	const int32 ColorMapSectionSize = static_cast<int32>(ReadUInt32LE(FileData, 32));
	const int32 ColorDataOffset = static_cast<int32>(ReadUInt32LE(FileData, 57));
	if (ColorMapSectionSize < 801 || !CanRead(FileData, ColorDataOffset, 256 * 3))
	{
		OutError = FString::Printf(TEXT("'%s' has an invalid CMAP section."), *OutMeshFile.SourceFile);
		return false;
	}

	OutMeshFile.ColorMap.Reset();
	OutMeshFile.ColorMap.Reserve(256);
	for (int32 ColorIndex = 0; ColorIndex < 256; ++ColorIndex)
	{
		const int32 Offset = ColorDataOffset + ColorIndex * 3;
		OutMeshFile.ColorMap.Add(FColor(FileData[Offset], FileData[Offset + 1], FileData[Offset + 2], 255));
	}

	return true;
}

bool ParseGeometryTables(const TArray<uint8>& FileData, FMaxisMeshFile& OutMeshFile, FString& OutError)
{
	const int32 GeometryOffset = OutMeshFile.GeometryTableOffset;
	if (!RequireFourCC(FileData, GeometryOffset, TEXT("GEOM"), OutMeshFile.SourceFile, OutError) || !CanRead(FileData, GeometryOffset, 24))
	{
		return false;
	}

	OutMeshFile.GeometryTableSize = static_cast<int32>(ReadUInt32LE(FileData, GeometryOffset + 4));
	OutMeshFile.GeometryEntryCount = static_cast<int32>(ReadUInt32LE(FileData, GeometryOffset + 8));
	OutMeshFile.ObjectCount = static_cast<int32>(ReadUInt32LE(FileData, GeometryOffset + 12));
	const int32 GeometryEntryOffset = static_cast<int32>(ReadUInt32LE(FileData, GeometryOffset + 16));
	const int32 DuplicateEntryOffset = static_cast<int32>(ReadUInt32LE(FileData, GeometryOffset + 20));

	if (OutMeshFile.GeometryEntryCount <= 0 || OutMeshFile.ObjectCount != OutMeshFile.GeometryEntryCount - 1)
	{
		OutError = FString::Printf(TEXT("'%s' has inconsistent GEOM counts: entries=%d objects=%d."),
			*OutMeshFile.SourceFile,
			OutMeshFile.GeometryEntryCount,
			OutMeshFile.ObjectCount);
		return false;
	}

	if (!CanRead(FileData, GeometryEntryOffset, OutMeshFile.GeometryEntryCount * 53) ||
		!CanRead(FileData, DuplicateEntryOffset, OutMeshFile.ObjectCount * 36))
	{
		OutError = FString::Printf(TEXT("'%s' has geometry table ranges outside the file."), *OutMeshFile.SourceFile);
		return false;
	}

	OutMeshFile.GeometryEntries.Reset();
	OutMeshFile.GeometryEntries.Reserve(OutMeshFile.GeometryEntryCount);
	for (int32 EntryIndex = 0; EntryIndex < OutMeshFile.GeometryEntryCount; ++EntryIndex)
	{
		const int32 Offset = GeometryEntryOffset + EntryIndex * 53;

		FMaxisMeshGeometryEntry Entry;
		Entry.TableIndex = EntryIndex;
		Entry.Name = ReadFixedAsciiString(FileData, Offset, 17);
		Entry.ObjectOffset = static_cast<int32>(ReadUInt32LE(FileData, Offset + 17));
		Entry.ObjectCount = static_cast<int32>(ReadUInt32LE(FileData, Offset + 21));
		Entry.RenderedVertexCount = static_cast<int32>(ReadUInt32LE(FileData, Offset + 29));
		Entry.FaceCount = static_cast<int32>(ReadUInt32LE(FileData, Offset + 41));
		Entry.UniqueVertexCount = static_cast<int32>(ReadUInt32LE(FileData, Offset + 45));
		OutMeshFile.GeometryEntries.Add(MoveTemp(Entry));
	}

	OutMeshFile.DuplicateGeometryEntries.Reset();
	OutMeshFile.DuplicateGeometryEntries.Reserve(OutMeshFile.ObjectCount);
	for (int32 EntryIndex = 0; EntryIndex < OutMeshFile.ObjectCount; ++EntryIndex)
	{
		const int32 Offset = DuplicateEntryOffset + EntryIndex * 36;

		FMaxisMeshDuplicateGeometryEntry Entry;
		Entry.TableIndex = EntryIndex;
		Entry.Id = ReadInt32LE(FileData, Offset);
		Entry.ObjectOffset = static_cast<int32>(ReadUInt32LE(FileData, Offset + 4));
		Entry.RenderedVertexCount = static_cast<int32>(ReadUInt32LE(FileData, Offset + 12));
		Entry.FaceCount = static_cast<int32>(ReadUInt32LE(FileData, Offset + 24));
		Entry.UniqueVertexCount = static_cast<int32>(ReadUInt32LE(FileData, Offset + 28));
		OutMeshFile.DuplicateGeometryEntries.Add(MoveTemp(Entry));
	}

	return true;
}

int32 FindNextObjectOffset(const TArray<int32>& SortedObjectOffsets, int32 CurrentObjectOffset, int32 FileSize)
{
	for (const int32 ObjectOffset : SortedObjectOffsets)
	{
		if (ObjectOffset > CurrentObjectOffset)
		{
			return ObjectOffset;
		}
	}

	return FileSize;
}

bool ParseFace(const TArray<uint8>& FileData, int32 FaceOffset, int32 ObjectBoundary, const FString& SourceName, FMaxisMeshFace& OutFace, FString& OutError)
{
	if (!RequireFourCC(FileData, FaceOffset, TEXT("FACE"), SourceName, OutError) || !CanRead(FileData, FaceOffset, 21))
	{
		return false;
	}

	OutFace.Offset = FaceOffset;
	OutFace.SizeBytes = static_cast<int32>(ReadUInt32LE(FileData, FaceOffset + 4));
	OutFace.VertexCount = ReadUInt16LE(FileData, FaceOffset + 8);
	OutFace.Flags = ReadUInt16LE(FileData, FaceOffset + 10);
	OutFace.LightType = ReadUInt16LE(FileData, FaceOffset + 12);
	OutFace.FaceInfo = ReadInt32LE(FileData, FaceOffset + 14);
	OutFace.FaceType = FileData[FaceOffset + 18];
	OutFace.MaterialIndex = FileData[FaceOffset + 19];
	OutFace.TextureAtlasIndex = FileData[FaceOffset + 20];

	const int32 MinimumFaceSize = 21 + static_cast<int32>(OutFace.VertexCount) * 2 + static_cast<int32>(OutFace.VertexCount) * 8;
	if (OutFace.SizeBytes < MinimumFaceSize || FaceOffset + OutFace.SizeBytes > ObjectBoundary)
	{
		OutError = FString::Printf(TEXT("'%s' has invalid FACE record at byte %d: size=%d min=%d boundary=%d."),
			*SourceName,
			FaceOffset,
			OutFace.SizeBytes,
			MinimumFaceSize,
			ObjectBoundary);
		return false;
	}

	OutFace.VertexIndices.Reset();
	OutFace.VertexIndices.Reserve(OutFace.VertexCount);
	int32 Cursor = FaceOffset + 21;
	for (int32 VertexIndex = 0; VertexIndex < OutFace.VertexCount; ++VertexIndex)
	{
		OutFace.VertexIndices.Add(ReadUInt16LE(FileData, Cursor));
		Cursor += 2;
	}

	OutFace.RawUVs.Reset();
	OutFace.RawUVs.Reserve(OutFace.VertexCount);
	for (int32 VertexIndex = 0; VertexIndex < OutFace.VertexCount; ++VertexIndex)
	{
		OutFace.RawUVs.Add(FIntPoint(ReadInt32LE(FileData, Cursor), ReadInt32LE(FileData, Cursor + 4)));
		Cursor += 8;
	}

	return true;
}

bool ParseObjects(const TArray<uint8>& FileData, FMaxisMeshFile& OutMeshFile, FString& OutError)
{
	TArray<int32> ObjectOffsets;
	ObjectOffsets.Reserve(OutMeshFile.ObjectCount);
	for (int32 EntryIndex = 1; EntryIndex < OutMeshFile.GeometryEntries.Num(); ++EntryIndex)
	{
		ObjectOffsets.AddUnique(OutMeshFile.GeometryEntries[EntryIndex].ObjectOffset);
	}
	Algo::Sort(ObjectOffsets);

	OutMeshFile.Objects.Reset();
	OutMeshFile.Objects.Reserve(OutMeshFile.ObjectCount);

	for (int32 EntryIndex = 1; EntryIndex < OutMeshFile.GeometryEntries.Num(); ++EntryIndex)
	{
		const FMaxisMeshGeometryEntry& GeometryEntry = OutMeshFile.GeometryEntries[EntryIndex];
		const int32 ObjectOffset = GeometryEntry.ObjectOffset;
		if (!RequireFourCC(FileData, ObjectOffset, TEXT("OBJX"), OutMeshFile.SourceFile, OutError) || !CanRead(FileData, ObjectOffset, 124))
		{
			return false;
		}

		FMaxisMeshObject Object;
		Object.Header.TableName = GeometryEntry.Name;
		Object.Header.ObjectName = ReadFixedAsciiString(FileData, ObjectOffset + 24, 88);
		Object.Header.Offset = ObjectOffset;
		Object.Header.DeclaredSizeBytes = static_cast<int32>(ReadUInt32LE(FileData, ObjectOffset + 4)) + 12;
		Object.Header.VertexCount = ReadUInt16LE(FileData, ObjectOffset + 8);
		Object.Header.FaceCount = ReadUInt16LE(FileData, ObjectOffset + 10);
		Object.Header.AttributeFlags = ReadUInt32LE(FileData, ObjectOffset + 12);
		Object.Header.Radius = ReadInt32LE(FileData, ObjectOffset + 16);
		Object.Header.YRadius = ReadInt32LE(FileData, ObjectOffset + 20);
		Object.Header.AnimationCount = ReadInt32LE(FileData, ObjectOffset + 112);
		Object.Header.AnimationPointer = ReadInt32LE(FileData, ObjectOffset + 116);
		Object.Header.Id = ReadInt32LE(FileData, ObjectOffset + 120);
		Object.Header.TableSizeBytes = FindNextObjectOffset(ObjectOffsets, ObjectOffset, FileData.Num()) - ObjectOffset;

		const int32 ObjectBoundary = ObjectOffset + Object.Header.TableSizeBytes;
		if (Object.Header.TableSizeBytes <= 0 || ObjectBoundary > FileData.Num())
		{
			OutError = FString::Printf(TEXT("'%s' object '%s' has invalid table extent at byte %d."),
				*OutMeshFile.SourceFile,
				*Object.Header.TableName,
				ObjectOffset);
			return false;
		}

		const int32 VertexOffset = ObjectOffset + 124;
		if (!CanRead(FileData, VertexOffset, static_cast<int32>(Object.Header.VertexCount) * 12))
		{
			OutError = FString::Printf(TEXT("'%s' object '%s' has vertices outside the file."),
				*OutMeshFile.SourceFile,
				*Object.Header.TableName);
			return false;
		}

		Object.Vertices.Reserve(Object.Header.VertexCount);
		for (int32 VertexIndex = 0; VertexIndex < Object.Header.VertexCount; ++VertexIndex)
		{
			const int32 Offset = VertexOffset + VertexIndex * 12;
			FMaxisMeshVertex Vertex;
			Vertex.X = ReadInt32LE(FileData, Offset);
			Vertex.Y = ReadInt32LE(FileData, Offset + 4);
			Vertex.Z = ReadInt32LE(FileData, Offset + 8);
			Object.Vertices.Add(Vertex);
		}

		int32 FaceOffset = VertexOffset + static_cast<int32>(Object.Header.VertexCount) * 12;
		Object.Faces.Reserve(Object.Header.FaceCount);
		for (int32 FaceIndex = 0; FaceIndex < Object.Header.FaceCount; ++FaceIndex)
		{
			FMaxisMeshFace Face;
			if (!ParseFace(FileData, FaceOffset, ObjectBoundary, OutMeshFile.SourceFile, Face, OutError))
			{
				OutError = FString::Printf(TEXT("%s Object '%s' face %d."), *OutError, *Object.Header.TableName, FaceIndex);
				return false;
			}

			FaceOffset += Face.SizeBytes;
			Object.Faces.Add(MoveTemp(Face));
		}

		Object.Header.ParsedSizeBytes = FaceOffset - ObjectOffset;
		OutMeshFile.Objects.Add(MoveTemp(Object));
	}

	return true;
}
}

const FMaxisMeshObject* FMaxisMeshFile::FindObjectByTableName(const FString& ObjectName) const
{
	for (const FMaxisMeshObject& Object : Objects)
	{
		if (Object.Header.TableName.Equals(ObjectName, ESearchCase::IgnoreCase))
		{
			return &Object;
		}
	}

	return nullptr;
}

const FMaxisMeshObject* FMaxisMeshFile::FindObjectById(int32 ObjectId) const
{
	for (const FMaxisMeshObject& Object : Objects)
	{
		if (Object.Header.Id == ObjectId)
		{
			return &Object;
		}
	}

	return nullptr;
}

bool FMaxisMeshReader::LoadMeshFileFromFile(const FString& FilePath, FMaxisMeshFile& OutMeshFile, FString& OutError)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		OutError = FString::Printf(TEXT("Could not read Maxis mesh file '%s'."), *FilePath);
		return false;
	}

	return LoadMeshFileFromBytes(FileData, FilePath, OutMeshFile, OutError);
}

bool FMaxisMeshReader::LoadMeshFileFromBytes(const TArray<uint8>& FileData, const FString& SourceName, FMaxisMeshFile& OutMeshFile, FString& OutError)
{
	OutMeshFile = FMaxisMeshFile();
	OutMeshFile.SourceFile = SourceName;
	OutMeshFile.FileSize = FileData.Num();

	if (!CanRead(FileData, 0, 28) ||
		!RequireFourCC(FileData, 0, TEXT("DIRC"), SourceName, OutError) ||
		!RequireFourCC(FileData, 12, TEXT("CMAP"), SourceName, OutError) ||
		!RequireFourCC(FileData, 20, TEXT("GEOM"), SourceName, OutError))
	{
		return false;
	}

	const int32 DeclaredFileSize = static_cast<int32>(ReadUInt32LE(FileData, 4));
	if (DeclaredFileSize != FileData.Num())
	{
		OutError = FString::Printf(TEXT("'%s' declares file size %d but actual size is %d."), *SourceName, DeclaredFileSize, FileData.Num());
		return false;
	}

	OutMeshFile.ColorMapOffset = static_cast<int32>(ReadUInt32LE(FileData, 16));
	OutMeshFile.GeometryTableOffset = static_cast<int32>(ReadUInt32LE(FileData, 24));

	if (OutMeshFile.ColorMapOffset != 28 || OutMeshFile.GeometryTableOffset <= 0 || OutMeshFile.GeometryTableOffset >= FileData.Num())
	{
		OutError = FString::Printf(TEXT("'%s' has invalid CMAP/GEOM addresses."), *SourceName);
		return false;
	}

	return ParseColorMap(FileData, OutMeshFile, OutError) &&
		ParseGeometryTables(FileData, OutMeshFile, OutError) &&
		ParseObjects(FileData, OutMeshFile, OutError);
}

FVector FMaxisMeshReader::ConvertMaxisVertexToUnreal(const FMaxisMeshVertex& Vertex, float UnitsPerCentimeter)
{
	const float Scale = UnitsPerCentimeter > 0.0f ? 1.0f / UnitsPerCentimeter : 1.0f / MeshUnitsPerCentimeter;
	return FVector(
		static_cast<float>(Vertex.Z) * Scale,
		static_cast<float>(Vertex.X) * Scale,
		static_cast<float>(Vertex.Y) * Scale);
}

FVector2D FMaxisMeshReader::ConvertMaxisUVToUnreal(const FIntPoint& RawUV)
{
	if (RawUV.X == MIN_int32 || RawUV.Y == MIN_int32)
	{
		return FVector2D::ZeroVector;
	}

	return FVector2D(
		static_cast<float>(RawUV.X) / 65536.0f,
		1.0f - static_cast<float>(RawUV.Y) / 65536.0f);
}
