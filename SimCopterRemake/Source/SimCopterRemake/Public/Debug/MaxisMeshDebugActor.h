// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/SoftObjectPath.h"
#include "MaxisMeshDebugActor.generated.h"

class UMaterialInterface;
class UProceduralMeshComponent;

UCLASS()
class SIMCOPTERREMAKE_API AMaxisMeshDebugActor : public AActor
{
	GENERATED_BODY()

public:
	AMaxisMeshDebugActor();

	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "SimCopter|Maxis Mesh")
	void RebuildMesh();

private:
	UPROPERTY(VisibleAnywhere, Category = "SimCopter|Maxis Mesh")
	TObjectPtr<UProceduralMeshComponent> MeshComponent;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Maxis Mesh", meta = (FilePathFilter = "max"))
	FFilePath MeshFile;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Maxis Mesh")
	FString MeshName = TEXT("RD29");

	UPROPERTY(EditAnywhere, Category = "SimCopter|Maxis Mesh", meta = (ClampMin = "1.0"))
	float MeshUnitsPerCentimeter = 2621.44f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Maxis Mesh")
	bool bCenterOnObjectOrigin = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Maxis Mesh")
	bool bCreateCollision = false;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Maxis Mesh")
	bool bRenderBackfaces = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Maxis Mesh")
	FLinearColor TexturedFaceFallbackColor = FLinearColor(0.62f, 0.62f, 0.58f, 1.0f);

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	FString LastLoadedMeshName;

	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Debug")
	FString LastLoadError;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> VertexColorMaterial;

	FString ResolveMeshPath() const;
	FLinearColor ResolveFaceColor(const TArray<FColor>& ColorMap, uint8 FaceType, uint8 MaterialIndex) const;
};
