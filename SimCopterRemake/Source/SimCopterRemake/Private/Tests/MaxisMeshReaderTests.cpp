// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Formats/MaxisMeshLibrary.h"
#include "Formats/MaxisMeshReader.h"
#include "Formats/MaxisTextureReader.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMaxisMeshUvConversionTest,
	"SimCopter.Formats.MaxisMesh.UvConversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMaxisMeshUvConversionTest::RunTest(const FString& Parameters)
{
	const FVector2D BottomLeft = FMaxisMeshReader::ConvertMaxisUVToUnreal(FIntPoint(0, 0));
	TestEqual(TEXT("Maxis bottom-left U"), BottomLeft.X, 0.0);
	TestEqual(TEXT("Maxis bottom-left becomes Unreal bottom V"), BottomLeft.Y, 1.0);

	const FVector2D TopRight = FMaxisMeshReader::ConvertMaxisUVToUnreal(FIntPoint(65536, 65536));
	TestEqual(TEXT("Maxis top-right U"), TopRight.X, 1.0);
	TestEqual(TEXT("Maxis top-right becomes Unreal top V"), TopRight.Y, 0.0);

	const FVector2D Repeating = FMaxisMeshReader::ConvertMaxisUVToUnreal(FIntPoint(-7 * 65536, 2 * 65536));
	TestEqual(TEXT("Repeating U remains outside 0..1"), Repeating.X, -7.0);
	TestEqual(TEXT("Repeating V remains outside 0..1 after flip"), Repeating.Y, -1.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMaxisMeshReferencePacksTest,
	"SimCopter.Formats.MaxisMesh.ReferencePacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMaxisMeshReferencePacksTest::RunTest(const FString& Parameters)
{
	const FString GeoPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame/GEO")));
	if (!FPaths::DirectoryExists(GeoPath))
	{
		AddWarning(FString::Printf(TEXT("Skipping optional Maxis mesh test because '%s' is not present."), *GeoPath));
		return true;
	}

	struct FExpectedPack
	{
		const TCHAR* FileName;
		int32 GeometryEntryCount;
		int32 ObjectCount;
		const TCHAR* ProbeObjectName;
		int32 ProbeVertexCount;
		int32 ProbeFaceCount;
	};

	const FExpectedPack ExpectedPacks[] = {
		{ TEXT("sim3d1.max"), 144, 143, TEXT("RD29"), 23, 17 },
		{ TEXT("SIM3D2.MAX"), 145, 144, TEXT("EXPLODE"), 3, 1 },
		{ TEXT("SIM3D3.MAX"), 114, 113, TEXT("IN165"), 51, 45 },
	};

	for (const FExpectedPack& Expected : ExpectedPacks)
	{
		const FString MeshFilePath = FPaths::Combine(GeoPath, Expected.FileName);
		FMaxisMeshFile MeshFile;
		FString Error;
		if (!TestTrue(FString::Printf(TEXT("Loads '%s'"), *MeshFilePath), FMaxisMeshReader::LoadMeshFileFromFile(MeshFilePath, MeshFile, Error)))
		{
			AddError(Error);
			return false;
		}

		TestEqual(FString::Printf(TEXT("%s color count"), Expected.FileName), MeshFile.ColorMap.Num(), 256);
		TestEqual(FString::Printf(TEXT("%s geometry entries"), Expected.FileName), MeshFile.GeometryEntries.Num(), Expected.GeometryEntryCount);
		TestEqual(FString::Printf(TEXT("%s objects"), Expected.FileName), MeshFile.Objects.Num(), Expected.ObjectCount);

		const FMaxisMeshObject* ProbeObject = MeshFile.FindObjectByTableName(Expected.ProbeObjectName);
		TestNotNull(FString::Printf(TEXT("%s contains %s"), Expected.FileName, Expected.ProbeObjectName), ProbeObject);
		if (ProbeObject != nullptr)
		{
			TestEqual(FString::Printf(TEXT("%s vertex count"), Expected.ProbeObjectName), ProbeObject->Vertices.Num(), Expected.ProbeVertexCount);
			TestEqual(FString::Printf(TEXT("%s face count"), Expected.ProbeObjectName), ProbeObject->Faces.Num(), Expected.ProbeFaceCount);
			TestEqual(FString::Printf(TEXT("%s id lookup"), Expected.ProbeObjectName), MeshFile.FindObjectById(ProbeObject->Header.Id), ProbeObject);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMaxisMeshLibraryTileMappingTest,
	"SimCopter.Formats.MaxisMesh.TileMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMaxisMeshLibraryTileMappingTest::RunTest(const FString& Parameters)
{
	const FString OriginalGameRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame")));
	if (!FPaths::DirectoryExists(FPaths::Combine(OriginalGameRoot, TEXT("GEO"))))
	{
		AddWarning(FString::Printf(TEXT("Skipping optional Maxis mesh library test because '%s' is not present."), *OriginalGameRoot));
		return true;
	}

	FMaxisMeshLibrary MeshLibrary;
	FString Error;
	if (!TestTrue(TEXT("Loads original game mesh library"), MeshLibrary.LoadFromOriginalGameRoot(OriginalGameRoot, Error)))
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("Loaded mesh pack count"), MeshLibrary.GetMeshFileCount(), 3);
	TestTrue(TEXT("Has a useful tile mapping table"), MeshLibrary.GetTileMappingCount() > 100);

	struct FExpectedTileMapping
	{
		int32 TileId;
		const TCHAR* TableName;
	};

	const FExpectedTileMapping ExpectedMappings[] = {
		{ 13, TEXT("LP13") },
		{ 14, TEXT("WR14") },
		{ 29, TEXT("RD29") },
		{ 88, TEXT("RD87") },
		{ 165, TEXT("IN165") },
		{ 210, TEXT("PO210") },
		{ 213, TEXT("LP213") },
	};

	for (const FExpectedTileMapping& Expected : ExpectedMappings)
	{
		const FMaxisMeshObject* MeshObject = MeshLibrary.FindObjectByTileId(Expected.TileId);
		TestNotNull(FString::Printf(TEXT("Tile %d resolves"), Expected.TileId), MeshObject);
		if (MeshObject != nullptr)
		{
			TestEqual(FString::Printf(TEXT("Tile %d object"), Expected.TileId), MeshObject->Header.TableName, FString(Expected.TableName));
		}
	}

	struct FExpectedObjectIdMapping
	{
		int32 ObjectId;
		const TCHAR* TableName;
	};

	const FExpectedObjectIdMapping ExpectedBridgeObjects[] = {
		{ 0x1D, TEXT("RD29") },
		{ 0x1E, TEXT("RD30") },
		{ 0x2C, TEXT("RL44") },
		{ 0x2D, TEXT("RL45") },
		{ 0x3B, TEXT("RD29L") },
		{ 0x3C, TEXT("RD30L") },
		{ 0x128, TEXT("RD67") },
		{ 0x129, TEXT("RD68") },
		{ 0x178, TEXT("TL63") },
		{ 0x179, TEXT("TL64") },
		{ 0x17A, TEXT("TL65") },
		{ 0x17B, TEXT("TL66") },
		{ 0x17D, TEXT("RL71") },
		{ 0x17E, TEXT("RL72") },
		{ 0x17F, TEXT("RD67H") },
		{ 0x180, TEXT("RD68H") },
		{ 0x64, TEXT("RD87") },
		{ 0x65, TEXT("RD87F") },
		{ 0x66, TEXT("BR81") },
		{ 0x67, TEXT("BR81F") },
		{ 0x68, TEXT("BR82") },
		{ 0x69, TEXT("BR82F") },
		{ 0x6A, TEXT("BR83") },
		{ 0x6B, TEXT("BR83F") },
		{ 0x6C, TEXT("BR84") },
		{ 0x6D, TEXT("BR84F") },
		{ 0x6E, TEXT("BR85") },
		{ 0x6F, TEXT("BR85F") },
		{ 0x70, TEXT("BR86") },
		{ 0x71, TEXT("BR86F") },
		{ 0x72, TEXT("RL90") },
		{ 0x73, TEXT("RL90F") },
		{ 0x74, TEXT("WR92") },
		{ 0x75, TEXT("WR92F") },
		{ 0xF7, TEXT("RD73") },
		{ 0xF8, TEXT("RD74") },
		{ 0xF9, TEXT("RD75") },
		{ 0xFA, TEXT("RD76") },
		{ 0xFB, TEXT("RD93") },
		{ 0xFC, TEXT("RD93F") },
		{ 0xFD, TEXT("RD94") },
		{ 0xFE, TEXT("RD94F") },
		{ 0xFF, TEXT("RD95") },
		{ 0x100, TEXT("RD95F") },
		{ 0x101, TEXT("RD96") },
		{ 0x102, TEXT("RD96F") },
		{ 0x103, TEXT("RD97") },
		{ 0x104, TEXT("RD98") },
		{ 0x105, TEXT("RD99") },
		{ 0x106, TEXT("RD100") },
		{ 0x107, TEXT("RD101") },
		{ 0x108, TEXT("RD102") },
		{ 0x109, TEXT("RD103") },
		{ 0x10A, TEXT("RD104") },
		{ 0x10B, TEXT("RD105") },
		{ 0x114, TEXT("RD106") },
		{ 0x115, TEXT("RD106F") },
	};

	for (const FExpectedObjectIdMapping& Expected : ExpectedBridgeObjects)
	{
		const FMaxisMeshObject* MeshObject = MeshLibrary.FindObjectByObjectId(Expected.ObjectId);
		TestNotNull(FString::Printf(TEXT("Object id 0x%x resolves"), Expected.ObjectId), MeshObject);
		if (MeshObject != nullptr)
		{
			TestEqual(FString::Printf(TEXT("Object id 0x%x table"), Expected.ObjectId), MeshObject->Header.TableName, FString(Expected.TableName));
			TestEqual(FString::Printf(TEXT("Object id 0x%x header id"), Expected.ObjectId), MeshObject->Header.Id, Expected.ObjectId);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMaxisTextureAtlasTileExtractionTest,
	"SimCopter.Formats.MaxisTexture.AtlasTileExtraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMaxisTextureAtlasTileExtractionTest::RunTest(const FString& Parameters)
{
	FMaxisTextureImage Atlas;
	Atlas.Width = 256;
	Atlas.Height = 256;
	Atlas.Pixels.SetNumUninitialized(Atlas.Width * Atlas.Height);
	for (int32 Y = 0; Y < Atlas.Height; ++Y)
	{
		for (int32 X = 0; X < Atlas.Width; ++X)
		{
			Atlas.Pixels[Y * Atlas.Width + X] = FColor(static_cast<uint8>(X), static_cast<uint8>(Y), 0, 255);
		}
	}

	FString Error;
	FMaxisTextureImage BottomLeftTile;
	if (!TestTrue(TEXT("Extracts bottom-left atlas tile"), FMaxisTextureReader::ExtractAtlasTile(Atlas, 0, BottomLeftTile, Error)))
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("Tile width"), BottomLeftTile.Width, 32);
	TestEqual(TEXT("Tile height"), BottomLeftTile.Height, 32);
	TestEqual(TEXT("Tile 0 source X"), BottomLeftTile.Pixels[0].R, static_cast<uint8>(0));
	TestEqual(TEXT("Tile 0 source Y"), BottomLeftTile.Pixels[0].G, static_cast<uint8>(224));
	TestEqual(TEXT("Tile 0 bottom-right source X"), BottomLeftTile.Pixels.Last().R, static_cast<uint8>(31));
	TestEqual(TEXT("Tile 0 bottom-right source Y"), BottomLeftTile.Pixels.Last().G, static_cast<uint8>(255));

	FMaxisTextureImage TopRightTile;
	if (!TestTrue(TEXT("Extracts top-right atlas tile"), FMaxisTextureReader::ExtractAtlasTile(Atlas, 63, TopRightTile, Error)))
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("Tile 63 source X"), TopRightTile.Pixels[0].R, static_cast<uint8>(224));
	TestEqual(TEXT("Tile 63 source Y"), TopRightTile.Pixels[0].G, static_cast<uint8>(0));

	FMaxisTextureImage InvalidTile;
	TestFalse(TEXT("Rejects out-of-range atlas tile"), FMaxisTextureReader::ExtractAtlasTile(Atlas, 64, InvalidTile, Error));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMaxisCompositeBitmapReferenceTextureTest,
	"SimCopter.Formats.MaxisTexture.ReferenceCompositeBitmap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMaxisCompositeBitmapReferenceTextureTest::RunTest(const FString& Parameters)
{
	const FString OriginalGameRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame")));
	const FString TexturePath = FPaths::Combine(OriginalGameRoot, TEXT("BMP/SIM3D.BMP"));
	if (!FPaths::FileExists(TexturePath))
	{
		AddWarning(FString::Printf(TEXT("Skipping optional Maxis composite bitmap test because '%s' is not present."), *TexturePath));
		return true;
	}

	FMaxisMeshLibrary MeshLibrary;
	FString Error;
	if (!TestTrue(TEXT("Loads mesh library for shared palette"), MeshLibrary.LoadFromOriginalGameRoot(OriginalGameRoot, Error)))
	{
		AddError(Error);
		return false;
	}

	const TArray<FColor>* Palette = MeshLibrary.GetSharedColorMap();
	if (!TestNotNull(TEXT("Shared palette exists"), Palette))
	{
		return false;
	}

	FMaxisCompositeBitmap Bitmap;
	if (!TestTrue(TEXT("Loads SIM3D composite bitmap"), FMaxisTextureReader::LoadCompositeBitmapFromFile(TexturePath, *Palette, Bitmap, Error)))
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("SIM3D image count"), Bitmap.ImageCount, 68);
	TestEqual(TEXT("SIM3D decoded images"), Bitmap.Images.Num(), 68);

	const FMaxisTextureImage* Image39 = Bitmap.FindImage(39);
	const FMaxisTextureImage* Image40 = Bitmap.FindImage(40);
	TestNotNull(TEXT("Texture image 39 exists"), Image39);
	TestNotNull(TEXT("Texture image 40 exists"), Image40);
	if (Image39 != nullptr)
	{
		TestEqual(TEXT("Texture image 39 width"), Image39->Width, 256);
		TestEqual(TEXT("Texture image 39 height"), Image39->Height, 256);
		TestEqual(TEXT("Texture image 39 pixels"), Image39->Pixels.Num(), 256 * 256);
	}
	if (Image40 != nullptr)
	{
		TestEqual(TEXT("Texture image 40 width"), Image40->Width, 256);
		TestEqual(TEXT("Texture image 40 height"), Image40->Height, 256);
		TestEqual(TEXT("Texture image 40 pixels"), Image40->Pixels.Num(), 256 * 256);
	}

	FMaxisCompositeBitmap SkyBitmap;
	const FString SkyTexturePath = FPaths::Combine(OriginalGameRoot, TEXT("BMP/SKY.BMP"));
	if (!TestTrue(TEXT("Loads SKY composite bitmap"), FMaxisTextureReader::LoadCompositeBitmapFromFile(SkyTexturePath, *Palette, SkyBitmap, Error)))
	{
		AddError(Error);
		return false;
	}

	const FMaxisTextureImage* SkyGroundAtlas = SkyBitmap.FindImage(4);
	TestNotNull(TEXT("SKY image 4 ground atlas exists"), SkyGroundAtlas);
	if (SkyGroundAtlas != nullptr)
	{
		TestEqual(TEXT("SKY image 4 width"), SkyGroundAtlas->Width, 256);
		TestEqual(TEXT("SKY image 4 height"), SkyGroundAtlas->Height, 256);
	}

	return true;
}

#endif
