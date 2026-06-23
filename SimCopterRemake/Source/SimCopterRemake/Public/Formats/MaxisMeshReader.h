// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FMaxisMeshGeometryEntry
{
	int32 TableIndex = INDEX_NONE;
	FString Name;
	int32 ObjectOffset = 0;
	int32 ObjectCount = 0;
	int32 RenderedVertexCount = 0;
	int32 FaceCount = 0;
	int32 UniqueVertexCount = 0;
};

struct FMaxisMeshDuplicateGeometryEntry
{
	int32 TableIndex = INDEX_NONE;
	int32 Id = 0;
	int32 ObjectOffset = 0;
	int32 RenderedVertexCount = 0;
	int32 FaceCount = 0;
	int32 UniqueVertexCount = 0;
};

struct FMaxisMeshVertex
{
	int32 X = 0;
	int32 Y = 0;
	int32 Z = 0;
};

struct FMaxisMeshFace
{
	int32 Offset = 0;
	int32 SizeBytes = 0;
	uint16 VertexCount = 0;
	uint16 Flags = 0;
	uint16 LightType = 0;
	int32 FaceInfo = 0;
	uint8 FaceType = 0;
	uint8 MaterialIndex = 0;
	uint8 TextureAtlasIndex = 0;
	TArray<uint16> VertexIndices;
	TArray<FIntPoint> RawUVs;
};

struct FMaxisMeshObjectHeader
{
	FString TableName;
	FString ObjectName;
	int32 Id = 0;
	int32 Offset = 0;
	int32 DeclaredSizeBytes = 0;
	int32 TableSizeBytes = 0;
	int32 ParsedSizeBytes = 0;
	uint16 VertexCount = 0;
	uint16 FaceCount = 0;
	uint32 AttributeFlags = 0;
	int32 Radius = 0;
	int32 YRadius = 0;
	int32 AnimationCount = 0;
	int32 AnimationPointer = 0;
};

struct FMaxisMeshObject
{
	FMaxisMeshObjectHeader Header;
	TArray<FMaxisMeshVertex> Vertices;
	TArray<FMaxisMeshFace> Faces;
};

struct FMaxisMeshFile
{
	FString SourceFile;
	int32 FileSize = 0;
	int32 ColorMapOffset = 0;
	int32 GeometryTableOffset = 0;
	int32 GeometryTableSize = 0;
	int32 GeometryEntryCount = 0;
	int32 ObjectCount = 0;

	TArray<FColor> ColorMap;
	TArray<FMaxisMeshGeometryEntry> GeometryEntries;
	TArray<FMaxisMeshDuplicateGeometryEntry> DuplicateGeometryEntries;
	TArray<FMaxisMeshObject> Objects;

	const FMaxisMeshObject* FindObjectByTableName(const FString& ObjectName) const;
	const FMaxisMeshObject* FindObjectById(int32 ObjectId) const;
};

class FMaxisMeshReader
{
public:
	static constexpr float MeshUnitsPerMeter = 262144.0f;
	static constexpr float MeshUnitsPerCentimeter = MeshUnitsPerMeter / 100.0f;

	static bool LoadMeshFileFromFile(const FString& FilePath, FMaxisMeshFile& OutMeshFile, FString& OutError);
	static bool LoadMeshFileFromBytes(const TArray<uint8>& FileData, const FString& SourceName, FMaxisMeshFile& OutMeshFile, FString& OutError);

	static FVector ConvertMaxisVertexToUnreal(const FMaxisMeshVertex& Vertex, float UnitsPerCentimeter = MeshUnitsPerCentimeter);
	static FVector2D ConvertMaxisUVToUnreal(const FIntPoint& RawUV);
};
