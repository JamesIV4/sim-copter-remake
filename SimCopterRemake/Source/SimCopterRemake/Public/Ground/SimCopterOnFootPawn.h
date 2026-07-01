// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Ground/SimCopterPopulationFigure.h"
#include "SimCopterOnFootPawn.generated.h"

class ASimCopterHelicopterPawn;
class UCameraComponent;
class UCapsuleComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class UTexture2D;

UCLASS()
class SIMCOPTERREMAKE_API ASimCopterOnFootPawn : public APawn
{
	GENERATED_BODY()

public:
	ASimCopterOnFootPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UCapsuleComponent> CollisionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UStaticMeshComponent> BodyProxyComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UProceduralMeshComponent> OriginalBodySpriteComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UCameraComponent> CameraComponent;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Ground Start")
	TSubclassOf<ASimCopterHelicopterPawn> HelicopterClass;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Ground Start")
	bool bFindOrSpawnParkedHelicopterOnBeginPlay = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Ground Start", meta = (ClampMin = "100.0"))
	float ParkedHelicopterSearchRadiusCm = 7000.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Ground Start")
	FVector ParkedHelicopterOffset = FVector(160.0f, 60.0f, 0.0f);

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Assets")
	FDirectoryPath OriginalGameRoot;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Interaction", meta = (ClampMin = "100.0"))
	float HelicopterInteractionRadiusCm = 620.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float WalkSpeedCmPerSec = 540.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float AccelerationInterpSpeed = 12.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float GroundProbeDistanceCm = 25000.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "1.0"))
	float LookYawSpeedDegPerSec = 155.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Camera", meta = (ClampMin = "1.0"))
	float LookPitchSpeedDegPerSec = 95.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	TObjectPtr<ASimCopterHelicopterPawn> ParkedHelicopter;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SpriteMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SpriteMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BodySpriteTexture;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BodyVertexColorMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FigureHeadTexture;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FigureHeadMaterialInstance;

	// The player's original figure ("pilot" - you are the SimCopter pilot). Empty disables.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Assets")
	FString PlayerFigureName = TEXT("pilot");

private:
	FVector CurrentVelocityCmPerSec = FVector::ZeroVector;
	float MoveForwardInput = 0.0f;
	float MoveRightInput = 0.0f;
	float LookYawInput = 0.0f;
	float LookPitchInput = 0.0f;
	float MouseLookYawInput = 0.0f;
	float MouseLookPitchInput = 0.0f;
	float CameraPitchDeg = -12.0f;
	int32 BodySpriteRow = INDEX_NONE;
	float BodySpriteTimeSeconds = 0.0f;
	bool bUsingOriginalBodySprite = false;

	// Original pilot-figure state (mirrors the ground agent's figure path).
	TSharedPtr<FSimCopterPrivAnimShared> FigureShared;
	struct FSimCopterFigureAnimState
	{
		FString Mnemonic;
		int32 FigureIndex = INDEX_NONE;
		int32 FrameCount = 0;
		int32 CurrentFrame = 0;
		float FrameTime = 0.0f;
		bool bHasHeadSection = false;
	} FigureAnim;
	bool bUsingOriginalFigure = false;

	bool LoadOriginalBodyFigure();
	bool RebuildPlayerFigureClip(const FString& Mnemonic);

	void MoveForward(float Value);
	void MoveRight(float Value);
	void LookYaw(float Value);
	void LookPitch(float Value);
	void MouseLookYaw(float Value);
	void MouseLookPitch(float Value);
	void Interact();

	void UpdateMovement(float DeltaSeconds);
	void UpdateCamera(float DeltaSeconds);
	void UpdateBodySprite(float DeltaSeconds);
	void LoadOriginalBodySprite();
	void SnapToGround();
	void FindOrSpawnParkedHelicopter();
	ASimCopterHelicopterPawn* FindNearestHelicopter(float SearchRadiusCm) const;
	bool ResolveGroundedLocation(const FVector& DesiredLocation, float ActorHalfHeight, FVector& OutLocation) const;
};
