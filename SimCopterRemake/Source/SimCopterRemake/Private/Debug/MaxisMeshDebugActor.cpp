// Copyright Epic Games, Inc. All Rights Reserved.

#include "Debug/MaxisMeshDebugActor.h"

#include "Formats/MaxisMeshReader.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogMaxisMeshDebugActor, Log, All);

AMaxisMeshDebugActor::AMaxisMeshDebugActor()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MeshComponent"));
	SetRootComponent(MeshComponent);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> VertexColorMaterialFinder(TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
	if (VertexColorMaterialFinder.Succeeded())
	{
		VertexColorMaterial = VertexColorMaterialFinder.Object;
		MeshComponent->SetMaterial(0, VertexColorMaterial);
	}

	MeshFile.FilePath = TEXT("../Reference/SimCopterOriginalGame/GEO/sim3d1.max");
}

void AMaxisMeshDebugActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildMesh();
}

void AMaxisMeshDebugActor::RebuildMesh()
{
	LastLoadError.Reset();
	LastLoadedMeshName.Reset();
	MeshComponent->ClearAllMeshSections();
	MeshComponent->SetCollisionEnabled(bCreateCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);

	const FString ResolvedMeshPath = ResolveMeshPath();
	if (ResolvedMeshPath.IsEmpty())
	{
		LastLoadError = TEXT("No Maxis mesh file is configured.");
		UE_LOG(LogMaxisMeshDebugActor, Warning, TEXT("%s"), *LastLoadError);
		return;
	}

	FMaxisMeshFile MeshPack;
	FString Error;
	if (!FMaxisMeshReader::LoadMeshFileFromFile(ResolvedMeshPath, MeshPack, Error))
	{
		LastLoadError = Error;
		UE_LOG(LogMaxisMeshDebugActor, Warning, TEXT("%s"), *LastLoadError);
		return;
	}

	const FMaxisMeshObject* MeshObject = MeshPack.FindObjectByTableName(MeshName);
	if (MeshObject == nullptr)
	{
		LastLoadError = FString::Printf(TEXT("Mesh '%s' was not found in '%s'."), *MeshName, *ResolvedMeshPath);
		UE_LOG(LogMaxisMeshDebugActor, Warning, TEXT("%s"), *LastLoadError);
		return;
	}

	LastLoadedMeshName = MeshObject->Header.TableName;

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;

	const FVector Origin = bCenterOnObjectOrigin && MeshObject->Vertices.Num() > 0
		? FMaxisMeshReader::ConvertMaxisVertexToUnreal(MeshObject->Vertices[0], MeshUnitsPerCentimeter)
		: FVector::ZeroVector;

	int32 PolygonFaceCount = 0;
	for (const FMaxisMeshFace& Face : MeshObject->Faces)
	{
		if (Face.VertexIndices.Num() < 3)
		{
			continue;
		}

		const int32 FaceVertexStart = Vertices.Num();
		const FLinearColor FaceColor = ResolveFaceColor(MeshPack.ColorMap, Face.FaceType, Face.MaterialIndex);

		for (int32 FaceVertexIndex = 0; FaceVertexIndex < Face.VertexIndices.Num(); ++FaceVertexIndex)
		{
			const uint16 SourceVertexIndex = Face.VertexIndices[FaceVertexIndex];
			if (!MeshObject->Vertices.IsValidIndex(SourceVertexIndex))
			{
				continue;
			}

			Vertices.Add(FMaxisMeshReader::ConvertMaxisVertexToUnreal(MeshObject->Vertices[SourceVertexIndex], MeshUnitsPerCentimeter) - Origin);
			UVs.Add(Face.RawUVs.IsValidIndex(FaceVertexIndex) ? FMaxisMeshReader::ConvertMaxisUVToUnreal(Face.RawUVs[FaceVertexIndex]) : FVector2D::ZeroVector);
			VertexColors.Add(FaceColor);
			Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
		}

		const int32 FaceVertexCount = Vertices.Num() - FaceVertexStart;
		if (FaceVertexCount < 3)
		{
			continue;
		}

		const FVector FaceNormal = FVector::CrossProduct(
			Vertices[FaceVertexStart + 1] - Vertices[FaceVertexStart],
			Vertices[FaceVertexStart + 2] - Vertices[FaceVertexStart]).GetSafeNormal();

		for (int32 Index = 0; Index < FaceVertexCount; ++Index)
		{
			Normals.Add(FaceNormal);
		}

		for (int32 TriangleIndex = 1; TriangleIndex < FaceVertexCount - 1; ++TriangleIndex)
		{
			Triangles.Add(FaceVertexStart);
			Triangles.Add(FaceVertexStart + TriangleIndex);
			Triangles.Add(FaceVertexStart + TriangleIndex + 1);

			if (bRenderBackfaces)
			{
				Triangles.Add(FaceVertexStart);
				Triangles.Add(FaceVertexStart + TriangleIndex + 1);
				Triangles.Add(FaceVertexStart + TriangleIndex);
			}
		}

		++PolygonFaceCount;
	}

	MeshComponent->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, bCreateCollision);
	if (VertexColorMaterial != nullptr)
	{
		MeshComponent->SetMaterial(0, VertexColorMaterial);
	}

	UE_LOG(
		LogMaxisMeshDebugActor,
		Display,
		TEXT("Rendered Maxis mesh '%s' from '%s': sourceVertices=%d sourceFaces=%d polygonFaces=%d renderVertices=%d triangles=%d"),
		*MeshObject->Header.TableName,
		*ResolvedMeshPath,
		MeshObject->Vertices.Num(),
		MeshObject->Faces.Num(),
		PolygonFaceCount,
		Vertices.Num(),
		Triangles.Num() / 3);
}

FString AMaxisMeshDebugActor::ResolveMeshPath() const
{
	const FString ConfiguredPath = MeshFile.FilePath.TrimStartAndEnd();
	if (ConfiguredPath.IsEmpty())
	{
		return FString();
	}

	if (FPaths::IsRelative(ConfiguredPath))
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), ConfiguredPath));
	}

	return FPaths::ConvertRelativePathToFull(ConfiguredPath);
}

FLinearColor AMaxisMeshDebugActor::ResolveFaceColor(const TArray<FColor>& ColorMap, uint8 FaceType, uint8 MaterialIndex) const
{
	if (FaceType == 13 || FaceType == 18)
	{
		return TexturedFaceFallbackColor;
	}

	if (ColorMap.IsValidIndex(MaterialIndex))
	{
		return FLinearColor(ColorMap[MaterialIndex]);
	}

	return FLinearColor::White;
}
