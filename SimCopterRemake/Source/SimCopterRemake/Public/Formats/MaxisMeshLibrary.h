// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Formats/MaxisMeshReader.h"

struct FMaxisMeshLibraryObjectKey
{
	int32 MeshFileIndex = INDEX_NONE;
	int32 ObjectIndex = INDEX_NONE;

	bool IsValid() const
	{
		return MeshFileIndex != INDEX_NONE && ObjectIndex != INDEX_NONE;
	}
};

class FMaxisMeshLibrary
{
public:
	bool LoadFromOriginalGameRoot(const FString& OriginalGameRoot, FString& OutError);

	const FMaxisMeshObject* FindObjectByTileId(int32 TileId, const TArray<FColor>** OutColorMap = nullptr) const;
	const FMaxisMeshObject* FindObjectByTableName(const FString& TableName, const TArray<FColor>** OutColorMap = nullptr) const;

	// Resolves a mesh object by its globally-unique object Id, reproducing the original
	// game's FUN_00470571. The SimCopter city builder (FUN_0047c0c0) dispatches tiles -
	// roads, bridges, etc. - to specific object Ids rather than to the heuristic XBLD
	// table used above, so this is the exact path for those cases.
	const FMaxisMeshObject* FindObjectByObjectId(int32 ObjectId, const TArray<FColor>** OutColorMap = nullptr) const;
	const TArray<FColor>* GetSharedColorMap() const;

	int32 GetMeshFileCount() const { return MeshFiles.Num(); }
	int32 GetTileMappingCount() const { return ObjectsByTileId.Num(); }

private:
	TArray<FMaxisMeshFile> MeshFiles;
	TMap<int32, FMaxisMeshLibraryObjectKey> ObjectsByTileId;
	TMap<int32, int32> TileMappingScores;
	TMap<FString, FMaxisMeshLibraryObjectKey> ObjectsByTableName;

	void RegisterMeshFile(int32 MeshFileIndex);
	void RegisterTileMapping(int32 TileId, const FMaxisMeshLibraryObjectKey& Key, int32 Score);

	const FMaxisMeshObject* GetObject(const FMaxisMeshLibraryObjectKey& Key, const TArray<FColor>** OutColorMap = nullptr) const;
};
