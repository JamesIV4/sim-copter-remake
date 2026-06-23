// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/MaxisMeshLibrary.h"

#include "Misc/Paths.h"

namespace
{
const TCHAR* ExpectedMeshPacks[] = {
	TEXT("sim3d1.max"),
	TEXT("SIM3D2.MAX"),
	TEXT("SIM3D3.MAX"),
};

struct FKnownXbldMapping
{
	const TCHAR* PackName;
	int32 TableIndex;
	int32 TileId;
};

// Static city-object mappings transcribed from the public SimCopter mesh to
// SimCity 2000 XBLD mapping notes. Object names are still used as a fallback
// for road/highway pieces whose XBLD value is omitted from that table.
const FKnownXbldMapping KnownXbldMappings[] = {
	{ TEXT("SIM3D1"), 1, 210 },
	{ TEXT("SIM3D1"), 6, 138 },
	{ TEXT("SIM3D1"), 7, 139 },
	{ TEXT("SIM3D1"), 8, 170 },
	{ TEXT("SIM3D1"), 9, 171 },
	{ TEXT("SIM3D1"), 10, 172 },
	{ TEXT("SIM3D1"), 11, 173 },
	{ TEXT("SIM3D1"), 12, 221 },
	{ TEXT("SIM3D1"), 13, 221 },
	{ TEXT("SIM3D1"), 14, 222 },
	{ TEXT("SIM3D1"), 17, 224 },
	{ TEXT("SIM3D1"), 18, 225 },
	{ TEXT("SIM3D1"), 19, 226 },
	{ TEXT("SIM3D1"), 20, 227 },
	{ TEXT("SIM3D1"), 21, 228 },
	{ TEXT("SIM3D1"), 22, 229 },
	{ TEXT("SIM3D1"), 23, 230 },
	{ TEXT("SIM3D1"), 24, 231 },
	{ TEXT("SIM3D1"), 25, 232 },
	{ TEXT("SIM3D1"), 26, 246 },
	{ TEXT("SIM3D1"), 27, 251 },
	{ TEXT("SIM3D1"), 28, 254 },
	{ TEXT("SIM3D1"), 29, 255 },
	{ TEXT("SIM3D1"), 30, 136 },
	{ TEXT("SIM3D1"), 31, 137 },
	{ TEXT("SIM3D1"), 32, 168 },
	{ TEXT("SIM3D1"), 33, 150 },
	{ TEXT("SIM3D1"), 34, 151 },
	{ TEXT("SIM3D1"), 35, 182 },
	{ TEXT("SIM3D1"), 36, 183 },
	{ TEXT("SIM3D1"), 37, 184 },
	{ TEXT("SIM3D1"), 38, 187 },
	{ TEXT("SIM3D1"), 39, 175 },
	{ TEXT("SIM3D1"), 40, 177 },
	{ TEXT("SIM3D1"), 41, 208 },
	{ TEXT("SIM3D1"), 42, 152 },
	{ TEXT("SIM3D1"), 43, 154 },
	{ TEXT("SIM3D1"), 44, 155 },
	{ TEXT("SIM3D1"), 45, 156 },
	{ TEXT("SIM3D1"), 46, 157 },
	{ TEXT("SIM3D1"), 47, 178 },
	{ TEXT("SIM3D1"), 48, 247 },
	{ TEXT("SIM3D1"), 49, 250 },
	{ TEXT("SIM3D1"), 50, 209 },
	{ TEXT("SIM3D1"), 51, 245 },
	{ TEXT("SIM3D1"), 52, 212 },
	{ TEXT("SIM3D1"), 53, 141 },
	{ TEXT("SIM3D1"), 54, 142 },
	{ TEXT("SIM3D1"), 55, 214 },
	{ TEXT("SIM3D1"), 56, 235 },
	{ TEXT("SIM3D1"), 57, 145 },
	{ TEXT("SIM3D1"), 58, 148 },
	{ TEXT("SIM3D1"), 59, 215 },
	{ TEXT("SIM3D1"), 60, 29 },
	{ TEXT("SIM3D1"), 61, 30 },
	{ TEXT("SIM3D1"), 62, 31 },
	{ TEXT("SIM3D1"), 63, 32 },
	{ TEXT("SIM3D1"), 64, 33 },
	{ TEXT("SIM3D1"), 65, 34 },
	{ TEXT("SIM3D1"), 66, 35 },
	{ TEXT("SIM3D1"), 67, 36 },
	{ TEXT("SIM3D1"), 68, 37 },
	{ TEXT("SIM3D1"), 69, 38 },
	{ TEXT("SIM3D1"), 70, 39 },
	{ TEXT("SIM3D1"), 71, 40 },
	{ TEXT("SIM3D1"), 72, 41 },
	{ TEXT("SIM3D1"), 73, 42 },
	{ TEXT("SIM3D1"), 74, 43 },
	{ TEXT("SIM3D1"), 88, 67 },
	{ TEXT("SIM3D1"), 89, 68 },
	{ TEXT("SIM3D1"), 90, 14 },
	{ TEXT("SIM3D1"), 91, 15 },
	{ TEXT("SIM3D1"), 92, 16 },
	{ TEXT("SIM3D1"), 93, 17 },
	{ TEXT("SIM3D1"), 94, 18 },
	{ TEXT("SIM3D1"), 95, 19 },
	{ TEXT("SIM3D1"), 96, 20 },
	{ TEXT("SIM3D1"), 97, 21 },
	{ TEXT("SIM3D1"), 98, 22 },
	{ TEXT("SIM3D1"), 99, 23 },
	{ TEXT("SIM3D1"), 100, 24 },
	{ TEXT("SIM3D1"), 101, 25 },
	{ TEXT("SIM3D1"), 102, 26 },
	{ TEXT("SIM3D1"), 103, 27 },
	{ TEXT("SIM3D1"), 104, 28 },
	{ TEXT("SIM3D1"), 127, 67 },
	{ TEXT("SIM3D1"), 128, 68 },
	{ TEXT("SIM3D2"), 1, 132 },
	{ TEXT("SIM3D2"), 2, 135 },
	{ TEXT("SIM3D2"), 3, 158 },
	{ TEXT("SIM3D2"), 4, 159 },
	{ TEXT("SIM3D2"), 5, 160 },
	{ TEXT("SIM3D2"), 6, 161 },
	{ TEXT("SIM3D2"), 7, 162 },
	{ TEXT("SIM3D2"), 8, 163 },
	{ TEXT("SIM3D2"), 9, 164 },
	{ TEXT("SIM3D2"), 40, 14 },
	{ TEXT("SIM3D2"), 41, 15 },
	{ TEXT("SIM3D2"), 42, 16 },
	{ TEXT("SIM3D2"), 43, 17 },
	{ TEXT("SIM3D2"), 44, 18 },
	{ TEXT("SIM3D2"), 45, 19 },
	{ TEXT("SIM3D2"), 46, 20 },
	{ TEXT("SIM3D2"), 47, 21 },
	{ TEXT("SIM3D2"), 48, 22 },
	{ TEXT("SIM3D2"), 49, 23 },
	{ TEXT("SIM3D2"), 50, 24 },
	{ TEXT("SIM3D2"), 51, 25 },
	{ TEXT("SIM3D2"), 52, 26 },
	{ TEXT("SIM3D2"), 53, 27 },
	{ TEXT("SIM3D2"), 54, 28 },
	{ TEXT("SIM3D2"), 55, 88 },
	{ TEXT("SIM3D2"), 56, 88 },
	{ TEXT("SIM3D2"), 57, 81 },
	{ TEXT("SIM3D2"), 58, 81 },
	{ TEXT("SIM3D2"), 59, 82 },
	{ TEXT("SIM3D2"), 60, 82 },
	{ TEXT("SIM3D2"), 61, 83 },
	{ TEXT("SIM3D2"), 62, 83 },
	{ TEXT("SIM3D2"), 63, 84 },
	{ TEXT("SIM3D2"), 64, 84 },
	{ TEXT("SIM3D2"), 65, 85 },
	{ TEXT("SIM3D2"), 66, 85 },
	{ TEXT("SIM3D2"), 67, 86 },
	{ TEXT("SIM3D2"), 68, 86 },
	{ TEXT("SIM3D2"), 71, 92 },
	{ TEXT("SIM3D2"), 72, 92 },
	{ TEXT("SIM3D2"), 85, 6 },
	{ TEXT("SIM3D2"), 86, 7 },
	{ TEXT("SIM3D2"), 87, 8 },
	{ TEXT("SIM3D2"), 88, 9 },
	{ TEXT("SIM3D2"), 89, 10 },
	{ TEXT("SIM3D2"), 90, 11 },
	{ TEXT("SIM3D2"), 91, 12 },
	{ TEXT("SIM3D3"), 1, 165 },
	{ TEXT("SIM3D3"), 2, 188 },
	{ TEXT("SIM3D3"), 3, 190 },
	{ TEXT("SIM3D3"), 4, 130 },
	{ TEXT("SIM3D3"), 5, 179 },
	{ TEXT("SIM3D3"), 6, 180 },
	{ TEXT("SIM3D3"), 7, 181 },
	{ TEXT("SIM3D3"), 8, 186 },
	{ TEXT("SIM3D3"), 9, 112 },
	{ TEXT("SIM3D3"), 10, 113 },
	{ TEXT("SIM3D3"), 11, 114 },
	{ TEXT("SIM3D3"), 12, 115 },
	{ TEXT("SIM3D3"), 13, 116 },
	{ TEXT("SIM3D3"), 14, 117 },
	{ TEXT("SIM3D3"), 15, 118 },
	{ TEXT("SIM3D3"), 16, 119 },
	{ TEXT("SIM3D3"), 17, 120 },
	{ TEXT("SIM3D3"), 18, 121 },
	{ TEXT("SIM3D3"), 19, 122 },
	{ TEXT("SIM3D3"), 20, 123 },
	{ TEXT("SIM3D3"), 21, 124 },
	{ TEXT("SIM3D3"), 22, 125 },
	{ TEXT("SIM3D3"), 23, 126 },
	{ TEXT("SIM3D3"), 24, 127 },
	{ TEXT("SIM3D3"), 25, 128 },
	{ TEXT("SIM3D3"), 26, 129 },
	{ TEXT("SIM3D3"), 27, 131 },
	{ TEXT("SIM3D3"), 28, 133 },
	{ TEXT("SIM3D3"), 29, 134 },
	{ TEXT("SIM3D3"), 30, 140 },
	{ TEXT("SIM3D3"), 31, 143 },
	{ TEXT("SIM3D3"), 32, 144 },
	{ TEXT("SIM3D3"), 33, 146 },
	{ TEXT("SIM3D3"), 34, 147 },
	{ TEXT("SIM3D3"), 35, 149 },
	{ TEXT("SIM3D3"), 36, 153 },
	{ TEXT("SIM3D3"), 37, 166 },
	{ TEXT("SIM3D3"), 38, 167 },
	{ TEXT("SIM3D3"), 39, 169 },
	{ TEXT("SIM3D3"), 40, 174 },
	{ TEXT("SIM3D3"), 41, 176 },
	{ TEXT("SIM3D3"), 42, 185 },
	{ TEXT("SIM3D3"), 43, 189 },
	{ TEXT("SIM3D3"), 44, 191 },
	{ TEXT("SIM3D3"), 45, 192 },
	{ TEXT("SIM3D3"), 46, 193 },
	{ TEXT("SIM3D3"), 47, 194 },
	{ TEXT("SIM3D3"), 48, 195 },
	{ TEXT("SIM3D3"), 49, 196 },
	{ TEXT("SIM3D3"), 50, 197 },
	{ TEXT("SIM3D3"), 51, 198 },
	{ TEXT("SIM3D3"), 52, 199 },
	{ TEXT("SIM3D3"), 53, 200 },
	{ TEXT("SIM3D3"), 54, 201 },
	{ TEXT("SIM3D3"), 55, 202 },
	{ TEXT("SIM3D3"), 56, 203 },
	{ TEXT("SIM3D3"), 57, 204 },
	{ TEXT("SIM3D3"), 58, 205 },
	{ TEXT("SIM3D3"), 59, 206 },
	{ TEXT("SIM3D3"), 60, 207 },
	{ TEXT("SIM3D3"), 61, 211 },
	{ TEXT("SIM3D3"), 62, 216 },
	{ TEXT("SIM3D3"), 63, 217 },
	{ TEXT("SIM3D3"), 64, 219 },
	{ TEXT("SIM3D3"), 65, 220 },
	{ TEXT("SIM3D3"), 66, 233 },
	{ TEXT("SIM3D3"), 67, 234 },
	{ TEXT("SIM3D3"), 68, 236 },
	{ TEXT("SIM3D3"), 69, 237 },
	{ TEXT("SIM3D3"), 70, 238 },
	{ TEXT("SIM3D3"), 71, 239 },
	{ TEXT("SIM3D3"), 72, 240 },
	{ TEXT("SIM3D3"), 73, 241 },
	{ TEXT("SIM3D3"), 74, 242 },
	{ TEXT("SIM3D3"), 75, 243 },
	{ TEXT("SIM3D3"), 76, 244 },
	{ TEXT("SIM3D3"), 77, 248 },
	{ TEXT("SIM3D3"), 78, 249 },
	{ TEXT("SIM3D3"), 79, 252 },
	{ TEXT("SIM3D3"), 80, 253 },
	{ TEXT("SIM3D3"), 104, 13 },
	{ TEXT("SIM3D3"), 105, 213 },
	{ TEXT("SIM3D3"), 106, 13 },
	{ TEXT("SIM3D3"), 107, 213 },
	{ TEXT("SIM3D3"), 113, 218 },
};

FString NormalizePackName(const FString& SourceFile)
{
	return FPaths::GetBaseFilename(SourceFile).ToUpper();
}

bool TryKnownXbldMapping(const FString& PackName, int32 TableIndex, int32& OutTileId)
{
	for (const FKnownXbldMapping& Mapping : KnownXbldMappings)
	{
		if (Mapping.TableIndex == TableIndex && PackName.Equals(Mapping.PackName, ESearchCase::IgnoreCase))
		{
			OutTileId = Mapping.TileId;
			return true;
		}
	}

	return false;
}

bool TryExtractTableNameTileId(const FString& TableName, int32& OutTileId)
{
	const FString Name = TableName.ToUpper();
	if (Name.StartsWith(TEXT("BASE")))
	{
		return false;
	}

	int32 LastDigit = INDEX_NONE;
	for (int32 Index = Name.Len() - 1; Index >= 0; --Index)
	{
		if (FChar::IsDigit(Name[Index]))
		{
			LastDigit = Index;
			break;
		}
	}

	if (LastDigit == INDEX_NONE)
	{
		return false;
	}

	int32 FirstDigit = LastDigit;
	while (FirstDigit > 0 && FChar::IsDigit(Name[FirstDigit - 1]))
	{
		--FirstDigit;
	}

	if (FirstDigit == 0)
	{
		return false;
	}

	const FString Prefix = Name.Left(FirstDigit);
	static const TSet<FString> StaticCityPrefixes = {
		TEXT("AB"), TEXT("AP"), TEXT("AR"), TEXT("BR"), TEXT("BS"), TEXT("CA"), TEXT("CH"),
		TEXT("CO"), TEXT("CR"), TEXT("DS"), TEXT("FS"), TEXT("HO"), TEXT("IN"), TEXT("LI"),
		TEXT("LP"), TEXT("MA"), TEXT("MH"), TEXT("ML"), TEXT("MS"), TEXT("MU"), TEXT("PK"),
		TEXT("PO"), TEXT("PP"), TEXT("PR"), TEXT("RD"), TEXT("RE"), TEXT("RL"), TEXT("SB"),
		TEXT("SC"), TEXT("SP"), TEXT("ST"), TEXT("TL"), TEXT("TR"), TEXT("TREE"), TEXT("UN"),
		TEXT("WR"), TEXT("WT"), TEXT("ZO")
	};

	if (!StaticCityPrefixes.Contains(Prefix))
	{
		return false;
	}

	const int32 Number = FCString::Atoi(*Name.Mid(FirstDigit, LastDigit - FirstDigit + 1));
	if (Number <= 0 || Number > 255)
	{
		return false;
	}

	OutTileId = Number;
	return true;
}

int32 MappingVariantPenalty(const FString& TableName)
{
	const FString Name = TableName.ToUpper();
	return (Name.EndsWith(TEXT("F")) || Name.EndsWith(TEXT("H")) || Name.EndsWith(TEXT("L"))) ? 5 : 0;
}
}

bool FMaxisMeshLibrary::LoadFromOriginalGameRoot(const FString& OriginalGameRoot, FString& OutError)
{
	MeshFiles.Reset();
	ObjectsByTileId.Reset();
	TileMappingScores.Reset();
	ObjectsByTableName.Reset();

	const FString TrimmedRoot = OriginalGameRoot.TrimStartAndEnd();
	if (TrimmedRoot.IsEmpty())
	{
		OutError = TEXT("No original game root is configured.");
		return false;
	}

	const FString ResolvedRoot = FPaths::ConvertRelativePathToFull(
		FPaths::IsRelative(TrimmedRoot) ? FPaths::Combine(FPaths::ProjectDir(), TrimmedRoot) : TrimmedRoot);
	const FString GeoPath = FPaths::Combine(ResolvedRoot, TEXT("GEO"));
	if (!FPaths::DirectoryExists(GeoPath))
	{
		OutError = FString::Printf(TEXT("Original game GEO folder was not found at '%s'."), *GeoPath);
		return false;
	}

	MeshFiles.Reserve(UE_ARRAY_COUNT(ExpectedMeshPacks));
	for (const TCHAR* PackName : ExpectedMeshPacks)
	{
		const FString PackPath = FPaths::Combine(GeoPath, PackName);
		FMaxisMeshFile MeshFile;
		if (!FMaxisMeshReader::LoadMeshFileFromFile(PackPath, MeshFile, OutError))
		{
			return false;
		}

		const int32 MeshFileIndex = MeshFiles.Add(MoveTemp(MeshFile));
		RegisterMeshFile(MeshFileIndex);
	}

	return true;
}

const FMaxisMeshObject* FMaxisMeshLibrary::FindObjectByTileId(int32 TileId, const TArray<FColor>** OutColorMap) const
{
	if (OutColorMap != nullptr)
	{
		*OutColorMap = nullptr;
	}

	if (const FMaxisMeshLibraryObjectKey* Key = ObjectsByTileId.Find(TileId))
	{
		return GetObject(*Key, OutColorMap);
	}

	return nullptr;
}

const FMaxisMeshObject* FMaxisMeshLibrary::FindObjectByTableName(const FString& TableName, const TArray<FColor>** OutColorMap) const
{
	if (OutColorMap != nullptr)
	{
		*OutColorMap = nullptr;
	}

	const FString NormalizedTableName = TableName.ToUpper();
	if (const FMaxisMeshLibraryObjectKey* Key = ObjectsByTableName.Find(NormalizedTableName))
	{
		return GetObject(*Key, OutColorMap);
	}

	return nullptr;
}

const TArray<FColor>* FMaxisMeshLibrary::GetSharedColorMap() const
{
	return MeshFiles.Num() > 0 ? &MeshFiles[0].ColorMap : nullptr;
}

void FMaxisMeshLibrary::RegisterMeshFile(int32 MeshFileIndex)
{
	if (!MeshFiles.IsValidIndex(MeshFileIndex))
	{
		return;
	}

	const FMaxisMeshFile& MeshFile = MeshFiles[MeshFileIndex];
	const FString PackName = NormalizePackName(MeshFile.SourceFile);

	for (int32 ObjectIndex = 0; ObjectIndex < MeshFile.Objects.Num(); ++ObjectIndex)
	{
		const FMaxisMeshObject& Object = MeshFile.Objects[ObjectIndex];
		const FMaxisMeshLibraryObjectKey Key{ MeshFileIndex, ObjectIndex };
		const FString NormalizedTableName = Object.Header.TableName.ToUpper();
		ObjectsByTableName.Add(NormalizedTableName, Key);

		const int32 TableIndex = MeshFile.GeometryEntries.IsValidIndex(ObjectIndex + 1)
			? MeshFile.GeometryEntries[ObjectIndex + 1].TableIndex
			: INDEX_NONE;

		int32 TileId = INDEX_NONE;
		if (TryKnownXbldMapping(PackName, TableIndex, TileId))
		{
			RegisterTileMapping(TileId, Key, MappingVariantPenalty(Object.Header.TableName));
		}

		if (TryExtractTableNameTileId(Object.Header.TableName, TileId))
		{
			RegisterTileMapping(TileId, Key, 100 + MappingVariantPenalty(Object.Header.TableName));
		}
	}
}

void FMaxisMeshLibrary::RegisterTileMapping(int32 TileId, const FMaxisMeshLibraryObjectKey& Key, int32 Score)
{
	if (TileId <= 0 || TileId > 255 || !Key.IsValid())
	{
		return;
	}

	if (const int32* ExistingScore = TileMappingScores.Find(TileId))
	{
		if (*ExistingScore <= Score)
		{
			return;
		}
	}

	ObjectsByTileId.Add(TileId, Key);
	TileMappingScores.Add(TileId, Score);
}

const FMaxisMeshObject* FMaxisMeshLibrary::GetObject(const FMaxisMeshLibraryObjectKey& Key, const TArray<FColor>** OutColorMap) const
{
	if (OutColorMap != nullptr)
	{
		*OutColorMap = nullptr;
	}

	if (!MeshFiles.IsValidIndex(Key.MeshFileIndex))
	{
		return nullptr;
	}

	const FMaxisMeshFile& MeshFile = MeshFiles[Key.MeshFileIndex];
	if (!MeshFile.Objects.IsValidIndex(Key.ObjectIndex))
	{
		return nullptr;
	}

	if (OutColorMap != nullptr)
	{
		*OutColorMap = &MeshFile.ColorMap;
	}

	return &MeshFile.Objects[Key.ObjectIndex];
}
