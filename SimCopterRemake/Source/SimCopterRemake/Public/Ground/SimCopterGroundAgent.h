// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/NoExportTypes.h"
#include "SimCopterGroundAgent.generated.h"

class UCapsuleComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class USceneComponent;
class USpotLightComponent;
class UStaticMeshComponent;
class UTexture2D;

UENUM(BlueprintType)
enum class ESimCopterGroundAgentKind : uint8
{
	Pedestrian,
	Vehicle
};

UCLASS()
class SIMCOPTERREMAKE_API ASimCopterGroundAgent : public AActor
{
	GENERATED_BODY()

public:
	ASimCopterGroundAgent();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	void ConfigureAgent(
		ESimCopterGroundAgentKind NewAgentKind,
		const FString& NewMeshTableName,
		const FString& NewOriginalGameRoot,
		float NewMovementSpeedCmPerSec);

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	bool LoadOriginalMeshFromOriginalGameRoot();

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	bool LoadOriginalPedestrianSpriteFromOriginalGameRoot();

	// Builds the procedural low-poly 3D pedestrian body (replaces the old flat sprite).
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	bool BuildPedestrianBody();

	// Immediately drops the agent onto the ground beneath it (used right after spawn so the
	// very first frame is grounded instead of floating).
	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	void SnapToGroundImmediate();

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	void SetMoveTarget(const FVector& NewTargetLocation);

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	void ClearMoveTarget();

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	bool HasMoveTarget() const { return bHasMoveTarget; }

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	bool IsNearMoveTarget(float DistanceCm = 90.0f) const;

	FVector GetMoveTargetLocation() const { return MoveTargetLocation; }
	FVector GetCurrentVelocityCmPerSec() const { return CurrentVelocityCmPerSec; }
	float GetCollisionRadiusCm() const;

	void SetTrafficSpeedScale(float NewSpeedScale);
	void AddTrafficVelocityImpulse(const FVector& ImpulseCmPerSec);
	void MoveByTrafficSeparation(const FVector& WorldDelta);
	void SetAvoidanceMoveTarget(const FVector& NewTargetLocation, float DurationSeconds, float SpeedMultiplier = 1.0f);
	bool IsAvoidanceMoveActive() const { return AvoidanceMoveTimeRemainingSeconds > 0.0f; }

	// Road/sidewalk graph route state, driven by ASimCopterTrafficSystemActor. TargetNode is the
	// graph node the agent is currently driving toward; PrevNode is where it came from (used to
	// avoid immediate U-turns). INDEX_NONE means "unset / re-acquire nearest node".
	void SetRouteState(int32 TargetNode, int32 PrevNode)
	{
		RouteTargetNodeIndex = TargetNode;
		RoutePrevNodeIndex = PrevNode;
	}
	int32 GetRouteTargetNode() const { return RouteTargetNodeIndex; }
	int32 GetRoutePrevNode() const { return RoutePrevNodeIndex; }

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	ESimCopterGroundAgentKind GetAgentKind() const { return AgentKind; }

	UFUNCTION(BlueprintCallable, Category = "SimCopter|Ground Agent")
	bool IsUsingOriginalMesh() const { return bUsingOriginalMesh; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UCapsuleComponent> CollisionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<USceneComponent> VisualRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UProceduralMeshComponent> OriginalMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UStaticMeshComponent> ProxyMeshComponent;

	// Vehicle headlights. The original cars carry translucent "headlight beam" cards (Maxis
	// face type 11) in front of the body; the remake strips those and drives real spotlights
	// instead, so the beams light the road at night rather than rendering as opaque blocks.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<USpotLightComponent> HeadlightLeft;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<USpotLightComponent> HeadlightRight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Ground Agent")
	ESimCopterGroundAgentKind AgentKind = ESimCopterGroundAgentKind::Pedestrian;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Ground Agent")
	FString MeshTableName;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Assets")
	FDirectoryPath OriginalGameRoot;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Assets", meta = (ClampMin = "1.0"))
	float ModelUnitsPerCentimeter = 2621.44f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Assets", meta = (ClampMin = "0.001"))
	float VehicleModelScale = 0.25f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Assets", meta = (ClampMin = "0.001"))
	float PedestrianModelScale = 0.25f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Assets")
	bool bRenderModelBackfaces = true;

	// Vehicle headlight spotlights (replace the removed translucent beam cards).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Headlights")
	bool bEnableVehicleHeadlights = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Headlights", meta = (ClampMin = "0.0"))
	float HeadlightIntensity = 9000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Headlights", meta = (ClampMin = "100.0"))
	float HeadlightAttenuationRadiusCm = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Headlights")
	FColor HeadlightColor = FColor(255, 244, 214);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float MovementSpeedCmPerSec = 420.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float TurnRateDegPerSec = 420.0f;

	// Downward reach of the ground probe, below the capsule bottom.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float GroundProbeDistanceCm = 4000.0f;

	// How far above the agent the ground probe starts. Kept large so placement survives a
	// mismatch between the spawner's terrain estimate and the city's actual rendered surface
	// (the reason agents used to hover in the air).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement", meta = (ClampMin = "1.0"))
	float GroundProbeUpCm = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Movement")
	bool bSnapToGround = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Animation")
	bool bEnableJankyAnimation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Animation", meta = (ClampMin = "0.0"))
	float JankyAnimationRate = 7.5f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	bool bUsingOriginalMesh = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "SimCopter|Runtime")
	FString LastMeshLoadError;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> VertexColorMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SpriteMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SpriteMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> PedestrianSpriteTexture;

private:
	FVector MoveTargetLocation = FVector::ZeroVector;
	FVector CurrentVelocityCmPerSec = FVector::ZeroVector;
	FVector ExternalVelocityCmPerSec = FVector::ZeroVector;
	FVector AvoidanceMoveTargetLocation = FVector::ZeroVector;
	bool bHasMoveTarget = false;
	float TrafficSpeedScale = 1.0f;
	float AvoidanceMoveTimeRemainingSeconds = 0.0f;
	float AvoidanceSpeedMultiplier = 1.0f;
	float AnimationTimeSeconds = 0.0f;
	float AnimationPhase = 0.0f;
	int32 PedestrianSpriteColumn = 0;
	int32 PedestrianSpriteRow = INDEX_NONE;
	int32 PedestrianOutfitIndex = 0;
	int32 RouteTargetNodeIndex = INDEX_NONE;
	int32 RoutePrevNodeIndex = INDEX_NONE;
	bool bUsingPedestrianSprite = false;
	bool bUsingPedestrianBody = false;

	void ApplyAgentShape();
	void UpdateMovement(float DeltaSeconds);
	void UpdateGroundSnap();
	void UpdateJankyAnimation(float DeltaSeconds);
	void ShowOriginalMesh(bool bUseOriginalMesh);
	void ConfigureVehicleHeadlights(const FBox& VehicleLocalBounds);
	void DisableVehicleHeadlights();
	bool TraceGround(FVector& OutGroundLocation) const;
	FString ResolveOriginalGameRoot() const;
};
