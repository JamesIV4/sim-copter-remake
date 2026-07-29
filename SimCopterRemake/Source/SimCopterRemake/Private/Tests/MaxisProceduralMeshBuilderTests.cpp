// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Formats/MaxisMeshLibrary.h"
#include "Formats/MaxisProceduralMeshBuilder.h"
#include "Formats/MaxisMeshReader.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

namespace
{
FMaxisMeshObject MakeSingleQuadObject()
{
	// A unit quad in the Maxis XY plane (Z = 0). With UnitsPerCentimeter = 1 the converter
	// maps Maxis (X,Y,Z) to Unreal (Z,X,Y), so this becomes a quad in the Unreal YZ... no:
	// Unreal X=Z=0, Y=X, Z=Y, i.e. a quad in the Unreal YZ plane spanning Y,Z in [0,100].
	FMaxisMeshObject Object;
	Object.Header.TableName = TEXT("TESTQUAD");
	Object.Header.VertexCount = 4;
	Object.Header.FaceCount = 1;
	Object.Vertices = {
		{ 0, 0, 0 },
		{ 100, 0, 0 },
		{ 100, 100, 0 },
		{ 0, 100, 0 },
	};

	FMaxisMeshFace Face;
	Face.VertexCount = 4;
	Face.FaceType = 0;
	Face.MaterialIndex = 1;
	Face.VertexIndices = { 0, 1, 2, 3 };
	Object.Faces.Add(MoveTemp(Face));

	return Object;
}

void MakeBentTrianglePair(
	float AngleDegrees,
	FMaxisMeshObject& OutObject,
	TArray<FVector>& OutVertexPositions)
{
	const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);
	const float CosAngle = FMath::Cos(AngleRadians);
	const float SinAngle = FMath::Sin(AngleRadians);

	// Two outward-facing triangles share edge 0-1. Their normals are +Z and
	// (0, -sin(angle), cos(angle)), respectively.
	OutVertexPositions = {
		FVector(0.0f, 0.0f, 0.0f),
		FVector(1.0f, 0.0f, 0.0f),
		FVector(0.0f, 1.0f, 0.0f),
		FVector(0.0f, -CosAngle, -SinAngle),
	};

	OutObject = FMaxisMeshObject();
	OutObject.Header.VertexCount = 4;
	OutObject.Header.FaceCount = 2;
	OutObject.Vertices.SetNum(4);

	FMaxisMeshFace FaceA;
	FaceA.VertexCount = 3;
	FaceA.VertexIndices = { 0, 1, 2 };
	OutObject.Faces.Add(MoveTemp(FaceA));

	FMaxisMeshFace FaceB;
	FaceB.VertexCount = 3;
	FaceB.VertexIndices = { 0, 3, 1 };
	OutObject.Faces.Add(MoveTemp(FaceB));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMaxisProceduralMeshBuilderQuadTest,
	"SimCopter.Formats.MaxisMesh.ProceduralMeshBuilder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMaxisProceduralMeshBuilderQuadTest::RunTest(const FString& Parameters)
{
	const FMaxisMeshObject Object = MakeSingleQuadObject();

	TArray<FColor> ColorMap;
	ColorMap.SetNum(256);
	ColorMap[1] = FColor(10, 20, 30, 255);

	FMaxisMeshSection Section;
	FMaxisProceduralMeshBuilder::BuildPaletteColoredSection(Object, &ColorMap, 1.0f, 1.0f, false, FLinearColor::White, Section);

	TestEqual(TEXT("Quad emits four vertices"), Section.Vertices.Num(), 4);
	TestEqual(TEXT("Quad emits two front triangles"), Section.Triangles.Num(), 6);
	TestEqual(TEXT("Each vertex has a normal"), Section.Normals.Num(), 4);
	TestEqual(TEXT("Each vertex has a colour"), Section.VertexColors.Num(), 4);
	if (Section.VertexColors.Num() == 4)
	{
		TestEqual(TEXT("Vertex colour comes from the palette"), Section.VertexColors[0], FLinearColor(FColor(10, 20, 30, 255)));
	}
	TestTrue(TEXT("Normals are unit length"), Section.Normals.Num() == 4 && FMath::IsNearlyEqual(Section.Normals[0].Size(), 1.0f, 0.001f));

	// Maxis (X,Y,Z) -> Unreal (Z,X,Y): the quad spans Unreal Y,Z in [0,100], X stays 0.
	TestTrue(TEXT("Bounds min"), Section.LocalBounds.Min.Equals(FVector(0.0, 0.0, 0.0), 0.01));
	TestTrue(TEXT("Bounds max"), Section.LocalBounds.Max.Equals(FVector(0.0, 100.0, 100.0), 0.01));

	FMaxisMeshSection DoubleSided;
	FMaxisProceduralMeshBuilder::BuildPaletteColoredSection(Object, &ColorMap, 1.0f, 1.0f, true, FLinearColor::White, DoubleSided);
	TestEqual(TEXT("Backfaces double the triangle count"), DoubleSided.Triangles.Num(), 12);

	FMaxisMeshSection Scaled;
	FMaxisProceduralMeshBuilder::BuildPaletteColoredSection(Object, &ColorMap, 1.0f, 0.25f, false, FLinearColor::White, Scaled);
	TestTrue(TEXT("Scale shrinks bounds"), Scaled.LocalBounds.Max.Equals(FVector(0.0, 25.0, 25.0), 0.01));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMaxisProceduralMeshBuilderAutoSmoothTest,
	"SimCopter.Formats.MaxisMesh.AutoSmoothNormals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMaxisProceduralMeshBuilderAutoSmoothTest::RunTest(const FString& Parameters)
{
	FMaxisMeshObject SmoothObject;
	TArray<FVector> SmoothPositions;
	MakeBentTrianglePair(20.0f, SmoothObject, SmoothPositions);

	TArray<TArray<FVector>> SmoothCornerNormals;
	FMaxisProceduralMeshBuilder::BuildAutoSmoothCornerNormals(
		SmoothObject,
		SmoothPositions,
		false,
		35.0f,
		SmoothCornerNormals);

	const FVector SmoothFaceBNormal(
		0.0f,
		-FMath::Sin(FMath::DegreesToRadians(20.0f)),
		FMath::Cos(FMath::DegreesToRadians(20.0f)));
	const FVector ExpectedSmoothNormal = (FVector::UpVector + SmoothFaceBNormal).GetSafeNormal();
	TestTrue(
		TEXT("Faces below 35 degrees share one shading normal at their common edge"),
		SmoothCornerNormals.Num() == 2 &&
			SmoothCornerNormals[0].Num() == 3 &&
			SmoothCornerNormals[1].Num() == 3 &&
			SmoothCornerNormals[0][0].Equals(ExpectedSmoothNormal, 0.001f) &&
			SmoothCornerNormals[1][0].Equals(ExpectedSmoothNormal, 0.001f));
	TestTrue(
		TEXT("A boundary corner keeps its polygon normal"),
		SmoothCornerNormals.Num() == 2 &&
			SmoothCornerNormals[0].Num() == 3 &&
			SmoothCornerNormals[0][2].Equals(FVector::UpVector, 0.001f));

	FMaxisMeshObject HardObject;
	TArray<FVector> HardPositions;
	MakeBentTrianglePair(50.0f, HardObject, HardPositions);

	TArray<TArray<FVector>> HardCornerNormals;
	FMaxisProceduralMeshBuilder::BuildAutoSmoothCornerNormals(
		HardObject,
		HardPositions,
		false,
		35.0f,
		HardCornerNormals);

	const FVector HardFaceBNormal(
		0.0f,
		-FMath::Sin(FMath::DegreesToRadians(50.0f)),
		FMath::Cos(FMath::DegreesToRadians(50.0f)));
	TestTrue(
		TEXT("Faces above 35 degrees keep distinct normals at their common edge"),
		HardCornerNormals.Num() == 2 &&
			HardCornerNormals[0].Num() == 3 &&
			HardCornerNormals[1].Num() == 3 &&
			HardCornerNormals[0][0].Equals(FVector::UpVector, 0.001f) &&
			HardCornerNormals[1][0].Equals(HardFaceBNormal, 0.001f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMaxisProceduralMeshBuilderHelicopterTest,
	"SimCopter.Formats.MaxisMesh.HelicopterModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMaxisProceduralMeshBuilderHelicopterTest::RunTest(const FString& Parameters)
{
	const FString OriginalGameRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame")));
	if (!FPaths::DirectoryExists(FPaths::Combine(OriginalGameRoot, TEXT("GEO"))))
	{
		AddWarning(FString::Printf(TEXT("Skipping optional helicopter model test because '%s' is not present."), *OriginalGameRoot));
		return true;
	}

	FMaxisMeshLibrary MeshLibrary;
	FString Error;
	if (!TestTrue(TEXT("Loads original game mesh library"), MeshLibrary.LoadFromOriginalGameRoot(OriginalGameRoot, Error)))
	{
		AddError(Error);
		return false;
	}

	// One body, one main rotor, the shared tail rotor, and the authored water bucket
	// must all resolve and build.
	const TCHAR* RequiredObjects[] = {
		TEXT("JETRANG"),
		TEXT("JETRROTR"),
		TEXT("ROTORTL"),
		TEXT("BUCKET"),
	};
	for (const TCHAR* ObjectName : RequiredObjects)
	{
		const TArray<FColor>* ColorMap = nullptr;
		const FMaxisMeshObject* Object = MeshLibrary.FindObjectByTableName(ObjectName, &ColorMap);
		if (!TestNotNull(FString::Printf(TEXT("Resolves '%s'"), ObjectName), Object))
		{
			continue;
		}

		FMaxisMeshSection Section;
		FMaxisProceduralMeshBuilder::BuildPaletteColoredSection(*Object, ColorMap, 2621.44f, 0.25f, true, FLinearColor::White, Section);
		TestFalse(FString::Printf(TEXT("'%s' builds a non-empty section"), ObjectName), Section.IsEmpty());
		TestTrue(FString::Printf(TEXT("'%s' triangle indices are multiples of 3"), ObjectName), Section.Triangles.Num() % 3 == 0);
	}

	const FMaxisMeshObject* BucketById = MeshLibrary.FindObjectByObjectId(0x7b);
	if (TestNotNull(TEXT("GEO object 0x7b resolves"), BucketById))
	{
		TestEqual(TEXT("GEO object 0x7b is the authored BUCKET"), BucketById->Header.TableName, FString(TEXT("BUCKET")));
	}

	// Rotor discs are authored around the mast, so their bounds should be roughly centred
	// on local X/Y = 0 (the spin axis), which is what lets the pawn spin them in place.
	const TArray<FColor>* RotorColorMap = nullptr;
	const FMaxisMeshObject* RotorObject = MeshLibrary.FindObjectByTableName(TEXT("JETRROTR"), &RotorColorMap);
	if (RotorObject != nullptr)
	{
		FMaxisMeshSection RotorSection;
		FMaxisProceduralMeshBuilder::BuildPaletteColoredSection(*RotorObject, RotorColorMap, 2621.44f, 0.25f, true, FLinearColor::White, RotorSection);
		const FVector Center = RotorSection.LocalBounds.GetCenter();
		TestTrue(TEXT("Rotor hub is near the mast axis"), FMath::Abs(Center.X) < 5.0 && FMath::Abs(Center.Y) < 5.0);

		// JETRROTR carries two type-11 polygons ~3.8 cm apart in height, which is why the
		// unfiltered blur reads as two stacked circles. The first-face-only build must keep a
		// single flat disc.
		FMaxisMeshSection BothOpaque;
		FMaxisMeshSection BothDiscs;
		FMaxisProceduralMeshBuilder::BuildPaletteColoredSections(
			*RotorObject, RotorColorMap, 2621.44f, 0.25f, true, FLinearColor::White, BothOpaque, &BothDiscs, false);
		FMaxisMeshSection SingleOpaque;
		FMaxisMeshSection SingleDisc;
		FMaxisProceduralMeshBuilder::BuildPaletteColoredSections(
			*RotorObject, RotorColorMap, 2621.44f, 0.25f, true, FLinearColor::White, SingleOpaque, &SingleDisc, true);

		TestTrue(TEXT("Unfiltered blur keeps both disc polygons"), BothDiscs.Triangles.Num() > SingleDisc.Triangles.Num());
		TestFalse(TEXT("Filtered blur still has a disc"), SingleDisc.IsEmpty());
		TestEqual(TEXT("Filtered blur is exactly one polygon"), SingleDisc.Triangles.Num() * 2, BothDiscs.Triangles.Num());
		TestTrue(
			TEXT("Dropping the second polygon does not change the opaque blades"),
			SingleOpaque.Triangles.Num() == BothOpaque.Triangles.Num());

		auto HeightSpread = [](const FMaxisMeshSection& Section)
		{
			return Section.LocalBounds.Max.Z - Section.LocalBounds.Min.Z;
		};
		TestTrue(TEXT("One disc is flat"), HeightSpread(SingleDisc) < 0.1);
		TestTrue(TEXT("Two discs are stacked"), HeightSpread(BothDiscs) > 1.0);
	}

	return true;
}

#endif
