// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterGroundAgent.h"

#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Formats/MaxisMeshLibrary.h"
#include "Formats/MaxisProceduralMeshBuilder.h"
#include "Ground/SimCopterPopulationBody.h"
#include "Ground/SimCopterPopulationSprite.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterGroundAgent, Log, All);

namespace
{
// The city (and its cars/helicopters) is rendered at 0.25x of real-cm so a 400cm remake tile
// stands in for the original 1600-unit tile. The population capsules and bodies were authored in
// real cm, which made pedestrians (and the on-foot player) read ~4x too tall next to the shrunk
// world. Everything visual/collision below is multiplied by this to match.
constexpr float PopulationWorldScale = 0.25f;

constexpr float VehicleCapsuleRadiusCm = 135.0f;
constexpr float VehicleCapsuleHalfHeightCm = 82.0f;
constexpr float PedestrianCapsuleRadiusCm = 32.0f;
constexpr float PedestrianCapsuleHalfHeightCm = 88.0f;
constexpr float TargetStopDistanceCm = 75.0f;
constexpr float VehicleFallbackZCm = 52.0f;
constexpr float PedestrianFallbackZCm = 88.0f;
constexpr float PedestrianSpriteHeightCm = 162.0f;
constexpr float PedestrianBodyHeightCm = 176.0f;
constexpr const TCHAR* SpriteMaterialPath = TEXT("/Game/Materials/M_SimCopterSpriteTexture.M_SimCopterSpriteTexture");

UMaterialInterface* LoadSpriteMaterialNoWarn()
{
	return Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, SpriteMaterialPath, nullptr, LOAD_NoWarn));
}
}

ASimCopterGroundAgent::ASimCopterGroundAgent()
{
	PrimaryActorTick.bCanEverTick = true;

	CollisionComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CollisionComponent"));
	CollisionComponent->InitCapsuleSize(PedestrianCapsuleRadiusCm, PedestrianCapsuleHalfHeightCm);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionComponent->SetCollisionObjectType(ECC_Pawn);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	CollisionComponent->SetCanEverAffectNavigation(false);
	SetRootComponent(CollisionComponent);

	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(CollisionComponent);

	OriginalMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("OriginalMesh"));
	OriginalMeshComponent->SetupAttachment(VisualRoot);
	OriginalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OriginalMeshComponent->SetCanEverAffectNavigation(false);
	OriginalMeshComponent->SetVisibility(false);

	ProxyMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProxyMesh"));
	ProxyMeshComponent->SetupAttachment(VisualRoot);
	ProxyMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProxyMeshComponent->SetCanEverAffectNavigation(false);

	// Headlights are cheap dynamic spotlights (no shadows) - there can be many cars on screen.
	HeadlightLeft = CreateDefaultSubobject<USpotLightComponent>(TEXT("HeadlightLeft"));
	HeadlightLeft->SetupAttachment(VisualRoot);
	HeadlightLeft->CastShadows = false;
	HeadlightLeft->SetVisibility(false);

	HeadlightRight = CreateDefaultSubobject<USpotLightComponent>(TEXT("HeadlightRight"));
	HeadlightRight->SetupAttachment(VisualRoot);
	HeadlightRight->CastShadows = false;
	HeadlightRight->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		ProxyMeshComponent->SetStaticMesh(CubeMeshFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ModelMaterialFinder(TEXT("/Game/Materials/M_SimCopterLitVertexColor.M_SimCopterLitVertexColor"));
	if (ModelMaterialFinder.Succeeded())
	{
		VertexColorMaterial = ModelMaterialFinder.Object;
	}

	SpriteMaterial = LoadSpriteMaterialNoWarn();

	OriginalGameRoot.Path = TEXT("../Reference/SimCopterOriginalGame");
	AnimationPhase = FMath::FRandRange(0.0f, UE_TWO_PI);
	ApplyAgentShape();
}

void ASimCopterGroundAgent::BeginPlay()
{
	Super::BeginPlay();

	ApplyAgentShape();
	if (AgentKind == ESimCopterGroundAgentKind::Pedestrian)
	{
		BuildPedestrianBody();
	}
	else if (!MeshTableName.IsEmpty())
	{
		LoadOriginalMeshFromOriginalGameRoot();
	}
}

void ASimCopterGroundAgent::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (AvoidanceMoveTimeRemainingSeconds > 0.0f)
	{
		AvoidanceMoveTimeRemainingSeconds = FMath::Max(0.0f, AvoidanceMoveTimeRemainingSeconds - DeltaSeconds);
	}
	if (AvoidancePathOffsetTimeRemainingSeconds > 0.0f)
	{
		AvoidancePathOffsetTimeRemainingSeconds = FMath::Max(0.0f, AvoidancePathOffsetTimeRemainingSeconds - DeltaSeconds);
	}
	if (GuidanceMoveTargetTimeRemainingSeconds > 0.0f)
	{
		GuidanceMoveTargetTimeRemainingSeconds = FMath::Max(0.0f, GuidanceMoveTargetTimeRemainingSeconds - DeltaSeconds);
	}
	UpdateMovement(DeltaSeconds);
	if (bSnapToGround)
	{
		UpdateGroundSnap();
	}
	UpdateJankyAnimation(DeltaSeconds);
}

void ASimCopterGroundAgent::ConfigureAgent(
	ESimCopterGroundAgentKind NewAgentKind,
	const FString& NewMeshTableName,
	const FString& NewOriginalGameRoot,
	float NewMovementSpeedCmPerSec)
{
	AgentKind = NewAgentKind;
	MeshTableName = NewMeshTableName;
	OriginalGameRoot.Path = NewOriginalGameRoot;
	MovementSpeedCmPerSec = FMath::Max(1.0f, NewMovementSpeedCmPerSec);
	AnimationPhase = FMath::FRandRange(0.0f, UE_TWO_PI);
	ApplyAgentShape();

	if (AgentKind == ESimCopterGroundAgentKind::Pedestrian)
	{
		BuildPedestrianBody();
	}
	else if (!MeshTableName.IsEmpty())
	{
		LoadOriginalMeshFromOriginalGameRoot();
	}
	else
	{
		DisableVehicleHeadlights();
		ShowOriginalMesh(false);
	}
}

bool ASimCopterGroundAgent::LoadOriginalMeshFromOriginalGameRoot()
{
	if (AgentKind == ESimCopterGroundAgentKind::Pedestrian && FSimCopterPopulationSprite::IsPeople1Name(MeshTableName))
	{
		return LoadOriginalPedestrianSpriteFromOriginalGameRoot();
	}

	LastMeshLoadError.Reset();
	OriginalMeshComponent->ClearAllMeshSections();
	bUsingPedestrianSprite = false;

	const FString RootPath = ResolveOriginalGameRoot();
	if (RootPath.IsEmpty())
	{
		LastMeshLoadError = TEXT("Original game root is empty.");
		ShowOriginalMesh(false);
		return false;
	}

	FMaxisMeshLibrary MeshLibrary;
	FString Error;
	if (!MeshLibrary.LoadFromOriginalGameRoot(RootPath, Error))
	{
		LastMeshLoadError = Error;
		UE_LOG(LogSimCopterGroundAgent, Warning, TEXT("%s"), *LastMeshLoadError);
		ShowOriginalMesh(false);
		return false;
	}

	const TArray<FColor>* ColorMap = nullptr;
	const FMaxisMeshObject* MeshObject = MeshLibrary.FindObjectByTableName(MeshTableName, &ColorMap);
	if (MeshObject == nullptr)
	{
		LastMeshLoadError = FString::Printf(TEXT("Could not find ground-agent mesh '%s' in '%s'."), *MeshTableName, *RootPath);
		UE_LOG(LogSimCopterGroundAgent, Warning, TEXT("%s"), *LastMeshLoadError);
		ShowOriginalMesh(false);
		return false;
	}

	const float ModelScale = AgentKind == ESimCopterGroundAgentKind::Vehicle ? VehicleModelScale : PedestrianModelScale;
	const FLinearColor FallbackColor = AgentKind == ESimCopterGroundAgentKind::Vehicle
		? FLinearColor(0.33f, 0.34f, 0.35f)
		: FLinearColor(0.72f, 0.63f, 0.52f);

	// The original cars carry translucent "headlight beam" cards (Maxis face type 11) projecting
	// off the front of the body. Route those into a throwaway translucent section so they are
	// dropped from the rendered vehicle - real spotlights below take their place. (Without this
	// the single-section build renders them as opaque grey/blue blocks: the "opaque headlights".)
	FMaxisMeshSection MeshSection;
	FMaxisMeshSection DiscardedBeamSection;
	FMaxisProceduralMeshBuilder::BuildPaletteColoredSections(
		*MeshObject,
		ColorMap,
		ModelUnitsPerCentimeter,
		ModelScale,
		bRenderModelBackfaces,
		FallbackColor,
		MeshSection,
		&DiscardedBeamSection);

	if (MeshSection.IsEmpty())
	{
		LastMeshLoadError = FString::Printf(TEXT("Ground-agent mesh '%s' built no triangles."), *MeshTableName);
		UE_LOG(LogSimCopterGroundAgent, Warning, TEXT("%s"), *LastMeshLoadError);
		ShowOriginalMesh(false);
		return false;
	}

	OriginalMeshComponent->CreateMeshSection_LinearColor(
		0,
		MeshSection.Vertices,
		MeshSection.Triangles,
		MeshSection.Normals,
		MeshSection.UVs,
		MeshSection.VertexColors,
		MeshSection.Tangents,
		false);

	if (VertexColorMaterial != nullptr)
	{
		OriginalMeshComponent->SetMaterial(0, VertexColorMaterial);
	}

	const float HalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	OriginalMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -HalfHeight - MeshSection.LocalBounds.Min.Z));
	ShowOriginalMesh(true);

	if (AgentKind == ESimCopterGroundAgentKind::Vehicle)
	{
		ConfigureVehicleHeadlights(MeshSection.LocalBounds);
	}
	else
	{
		DisableVehicleHeadlights();
	}

	return true;
}

bool ASimCopterGroundAgent::LoadOriginalPedestrianSpriteFromOriginalGameRoot()
{
	LastMeshLoadError.Reset();
	bUsingPedestrianSprite = false;
	PedestrianSpriteTexture = nullptr;
	SpriteMaterialInstance = nullptr;
	OriginalMeshComponent->ClearAllMeshSections();

	const FString RootPath = ResolveOriginalGameRoot();
	if (RootPath.IsEmpty())
	{
		LastMeshLoadError = TEXT("Original game root is empty.");
		ShowOriginalMesh(false);
		return false;
	}

	if (SpriteMaterial == nullptr)
	{
		SpriteMaterial = LoadSpriteMaterialNoWarn();
	}

	if (SpriteMaterial == nullptr)
	{
		LastMeshLoadError = FString::Printf(TEXT("Missing %s for original pedestrian sprites."), SpriteMaterialPath);
		UE_LOG(LogSimCopterGroundAgent, Warning, TEXT("%s"), *LastMeshLoadError);
		ShowOriginalMesh(false);
		return false;
	}

	UTexture2D* LoadedTexture = nullptr;
	FString Error;
	if (!FSimCopterPopulationSprite::LoadPeople1Texture(this, RootPath, LoadedTexture, Error))
	{
		LastMeshLoadError = Error;
		UE_LOG(LogSimCopterGroundAgent, Warning, TEXT("%s"), *LastMeshLoadError);
		ShowOriginalMesh(false);
		return false;
	}

	PedestrianSpriteTexture = LoadedTexture;
	PedestrianSpriteColumn = FSimCopterPopulationSprite::ResolvePeople1Column(MeshTableName, this);
	PedestrianSpriteRow = 0;
	FSimCopterPopulationSprite::BuildPeople1FrameQuad(OriginalMeshComponent, PedestrianSpriteColumn, PedestrianSpriteRow, PedestrianSpriteHeightCm);

	SpriteMaterialInstance = UMaterialInstanceDynamic::Create(SpriteMaterial, this);
	if (SpriteMaterialInstance != nullptr)
	{
		SpriteMaterialInstance->SetTextureParameterValue(TEXT("Texture"), PedestrianSpriteTexture);
		OriginalMeshComponent->SetMaterial(0, SpriteMaterialInstance);
	}

	const float HalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	OriginalMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -HalfHeight));
	bUsingPedestrianSprite = true;
	ShowOriginalMesh(true);
	return true;
}

bool ASimCopterGroundAgent::BuildPedestrianBody()
{
	LastMeshLoadError.Reset();
	bUsingPedestrianSprite = false;
	bUsingPedestrianBody = false;
	PedestrianSpriteTexture = nullptr;
	SpriteMaterialInstance = nullptr;
	OriginalMeshComponent->ClearAllMeshSections();
	DisableVehicleHeadlights();

	PedestrianOutfitIndex = FSimCopterPopulationBody::ResolveOutfitIndex(this);
	FSimCopterPopulationBody::BuildPerson(OriginalMeshComponent, PedestrianOutfitIndex, PedestrianBodyHeightCm * PopulationWorldScale);

	if (VertexColorMaterial != nullptr)
	{
		OriginalMeshComponent->SetMaterial(0, VertexColorMaterial);
	}

	const float HalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	// Feet sit at the capsule bottom; the body is modelled from Z=0 (feet) upward.
	OriginalMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -HalfHeight));
	bUsingPedestrianBody = true;
	ShowOriginalMesh(true);
	return true;
}

void ASimCopterGroundAgent::ConfigureVehicleHeadlights(const FBox& VehicleLocalBounds)
{
	if (HeadlightLeft == nullptr || HeadlightRight == nullptr)
	{
		return;
	}

	const float HalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	// Positions are in VisualRoot space. The car's nose is the mesh's max-X; headlights sit just
	// above the ground at the front corners and aim forward (+X), slightly downward.
	const float FrontX = VehicleLocalBounds.Max.X;
	const float SideY = FMath::Max(VehicleLocalBounds.Max.Y, 10.0f) * 0.62f;
	const float BodyHeight = VehicleLocalBounds.Max.Z - VehicleLocalBounds.Min.Z;
	const float HeadlightZ = -HalfHeight + FMath::Clamp(BodyHeight * 0.25f, 8.0f, 30.0f);
	const FRotator BeamRotation(-9.0f, 0.0f, 0.0f);

	USpotLightComponent* Lights[2] = {HeadlightLeft, HeadlightRight};
	const float SideSigns[2] = {-1.0f, 1.0f};
	for (int32 Index = 0; Index < 2; ++Index)
	{
		USpotLightComponent* Light = Lights[Index];
		Light->SetRelativeLocation(FVector(FrontX, SideSigns[Index] * SideY, HeadlightZ));
		Light->SetRelativeRotation(BeamRotation);
		Light->SetIntensity(HeadlightIntensity);
		Light->SetAttenuationRadius(HeadlightAttenuationRadiusCm);
		Light->SetInnerConeAngle(11.0f);
		Light->SetOuterConeAngle(26.0f);
		Light->SetLightColor(FLinearColor(HeadlightColor));
		Light->SetVisibility(bEnableVehicleHeadlights);
	}
}

void ASimCopterGroundAgent::DisableVehicleHeadlights()
{
	if (HeadlightLeft != nullptr)
	{
		HeadlightLeft->SetVisibility(false);
	}
	if (HeadlightRight != nullptr)
	{
		HeadlightRight->SetVisibility(false);
	}
}

void ASimCopterGroundAgent::SnapToGroundImmediate()
{
	FVector GroundedLocation;
	if (TraceGround(GroundedLocation))
	{
		SetActorLocation(GroundedLocation, false);
	}
}

void ASimCopterGroundAgent::SetMoveTarget(const FVector& NewTargetLocation)
{
	MoveTargetLocation = NewTargetLocation;
	GuidanceMoveTargetTimeRemainingSeconds = 0.0f;
	bHasMoveTarget = true;
}

void ASimCopterGroundAgent::ClearMoveTarget()
{
	bHasMoveTarget = false;
	CurrentVelocityCmPerSec = FVector::ZeroVector;
	AvoidancePathOffsetTimeRemainingSeconds = 0.0f;
	AvoidancePathOffsetSpeedMultiplier = 1.0f;
	GuidanceMoveTargetTimeRemainingSeconds = 0.0f;
}

bool ASimCopterGroundAgent::IsNearMoveTarget(float DistanceCm) const
{
	if (IsAvoidanceMoveActive())
	{
		return false;
	}

	if (!bHasMoveTarget)
	{
		return true;
	}

	const FVector EffectiveTarget = IsAvoidancePathOffsetActive() ? MoveTargetLocation + AvoidancePathOffset : MoveTargetLocation;
	const FVector Delta = EffectiveTarget - GetActorLocation();
	return FVector(Delta.X, Delta.Y, 0.0f).SizeSquared() <= FMath::Square(DistanceCm);
}

float ASimCopterGroundAgent::GetCollisionRadiusCm() const
{
	return CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleRadius() : 0.0f;
}

void ASimCopterGroundAgent::SetTrafficSpeedScale(float NewSpeedScale)
{
	TrafficSpeedScale = FMath::Clamp(NewSpeedScale, 0.0f, 1.75f);
}

void ASimCopterGroundAgent::LimitTrafficSpeedScale(float MaxSpeedScale)
{
	TrafficSpeedScale = FMath::Min(TrafficSpeedScale, FMath::Clamp(MaxSpeedScale, 0.0f, 1.75f));
}

void ASimCopterGroundAgent::ApplyTrafficBrake(float MaxSpeedScale, float DeltaSeconds, float BrakeRate)
{
	const float ClampedSpeedScale = FMath::Clamp(MaxSpeedScale, 0.0f, 1.75f);
	LimitTrafficSpeedScale(ClampedSpeedScale);

	const float BrakeAlpha = FMath::Clamp(1.0f - ClampedSpeedScale, 0.0f, 1.0f);
	const float EffectiveBrakeRate = FMath::Max(0.0f, BrakeRate) * BrakeAlpha;
	if (EffectiveBrakeRate <= 0.0f || DeltaSeconds <= 0.0f)
	{
		return;
	}

	CurrentVelocityCmPerSec = FMath::VInterpTo(CurrentVelocityCmPerSec, FVector::ZeroVector, DeltaSeconds, EffectiveBrakeRate);
	ExternalVelocityCmPerSec = FMath::VInterpTo(ExternalVelocityCmPerSec, FVector::ZeroVector, DeltaSeconds, EffectiveBrakeRate);
}

void ASimCopterGroundAgent::AddTrafficVelocityImpulse(const FVector& ImpulseCmPerSec)
{
	ExternalVelocityCmPerSec += FVector(ImpulseCmPerSec.X, ImpulseCmPerSec.Y, 0.0f);
}

void ASimCopterGroundAgent::MoveByTrafficSeparation(const FVector& WorldDelta)
{
	if (!WorldDelta.IsNearlyZero())
	{
		AddActorWorldOffset(WorldDelta, false);
		if (bSnapToGround)
		{
			UpdateGroundSnap();
		}
	}
}

void ASimCopterGroundAgent::SetAvoidanceMoveTarget(const FVector& NewTargetLocation, float DurationSeconds, float SpeedMultiplier)
{
	AvoidanceMoveTargetLocation = NewTargetLocation;
	AvoidanceMoveTimeRemainingSeconds = FMath::Max(0.0f, DurationSeconds);
	AvoidanceSpeedMultiplier = FMath::Clamp(SpeedMultiplier, 0.25f, 2.5f);
}

void ASimCopterGroundAgent::SetAvoidancePathOffset(const FVector& NewWorldOffset, float DurationSeconds, float SpeedMultiplier)
{
	AvoidancePathOffset = FVector(NewWorldOffset.X, NewWorldOffset.Y, 0.0f);
	AvoidancePathOffsetTimeRemainingSeconds = FMath::Max(0.0f, DurationSeconds);
	AvoidancePathOffsetSpeedMultiplier = FMath::Clamp(SpeedMultiplier, 0.25f, 2.5f);
}

void ASimCopterGroundAgent::SetGuidanceMoveTarget(const FVector& NewTargetLocation, float DurationSeconds)
{
	GuidanceMoveTargetLocation = NewTargetLocation;
	GuidanceMoveTargetTimeRemainingSeconds = FMath::Max(0.0f, DurationSeconds);
}

void ASimCopterGroundAgent::ApplyAgentShape()
{
	if (CollisionComponent == nullptr || ProxyMeshComponent == nullptr)
	{
		return;
	}

	if (AgentKind == ESimCopterGroundAgentKind::Vehicle)
	{
		CollisionComponent->SetCapsuleSize(VehicleCapsuleRadiusCm * PopulationWorldScale, VehicleCapsuleHalfHeightCm * PopulationWorldScale);
		ProxyMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, (-VehicleCapsuleHalfHeightCm + VehicleFallbackZCm) * PopulationWorldScale));
		ProxyMeshComponent->SetRelativeScale3D(FVector(2.8f, 1.25f, 0.55f) * PopulationWorldScale);
		MovementSpeedCmPerSec = FMath::Max(MovementSpeedCmPerSec, 520.0f);
	}
	else
	{
		CollisionComponent->SetCapsuleSize(PedestrianCapsuleRadiusCm * PopulationWorldScale, PedestrianCapsuleHalfHeightCm * PopulationWorldScale);
		ProxyMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, (-PedestrianCapsuleHalfHeightCm + PedestrianFallbackZCm) * PopulationWorldScale));
		ProxyMeshComponent->SetRelativeScale3D(FVector(0.28f, 0.18f, 1.76f) * PopulationWorldScale);
		MovementSpeedCmPerSec = FMath::Clamp(MovementSpeedCmPerSec, 130.0f, 520.0f);
	}
}

void ASimCopterGroundAgent::UpdateMovement(float DeltaSeconds)
{
	if (RootComponent == nullptr)
	{
		return;
	}

	const bool bUsingAvoidanceTarget = IsAvoidanceMoveActive();
	if (!bHasMoveTarget && !bUsingAvoidanceTarget)
	{
		CurrentVelocityCmPerSec = FMath::VInterpTo(CurrentVelocityCmPerSec, FVector::ZeroVector, DeltaSeconds, 6.0f);
		ExternalVelocityCmPerSec = FMath::VInterpTo(ExternalVelocityCmPerSec, FVector::ZeroVector, DeltaSeconds, 8.0f);
		if (!ExternalVelocityCmPerSec.IsNearlyZero())
		{
			RootComponent->MoveComponent(ExternalVelocityCmPerSec * DeltaSeconds, GetActorQuat(), false);
		}
		return;
	}

	const FVector CurrentLocation = GetActorLocation();
	const bool bUsingAvoidancePathOffset = IsAvoidancePathOffsetActive();
	bool bUsingGuidanceTarget = IsGuidanceMoveTargetActive();
	FVector BaseMoveTarget = bUsingGuidanceTarget ? GuidanceMoveTargetLocation : MoveTargetLocation;
	FVector ActiveMoveTarget = bUsingAvoidanceTarget
		? AvoidanceMoveTargetLocation
		: BaseMoveTarget + (bUsingAvoidancePathOffset ? AvoidancePathOffset : FVector::ZeroVector);
	FVector ToTarget = ActiveMoveTarget - CurrentLocation;
	FVector FlatToTarget(ToTarget.X, ToTarget.Y, 0.0f);
	float DistanceToTarget = FlatToTarget.Size();
	if (bUsingGuidanceTarget && DistanceToTarget <= TargetStopDistanceCm)
	{
		GuidanceMoveTargetTimeRemainingSeconds = 0.0f;
		bUsingGuidanceTarget = false;
		BaseMoveTarget = MoveTargetLocation;
		ActiveMoveTarget = MoveTargetLocation + (bUsingAvoidancePathOffset ? AvoidancePathOffset : FVector::ZeroVector);
		ToTarget = ActiveMoveTarget - CurrentLocation;
		FlatToTarget = FVector(ToTarget.X, ToTarget.Y, 0.0f);
		DistanceToTarget = FlatToTarget.Size();
	}
	if (DistanceToTarget <= TargetStopDistanceCm)
	{
		if (bUsingAvoidanceTarget)
		{
			AvoidanceMoveTimeRemainingSeconds = 0.0f;
			AvoidanceSpeedMultiplier = 1.0f;
		}
		else
		{
			ClearMoveTarget();
		}
		return;
	}

	const FVector DesiredDirection = FlatToTarget / DistanceToTarget;
	const float EffectiveSpeedScale = TrafficSpeedScale * (bUsingAvoidanceTarget
		? AvoidanceSpeedMultiplier
		: (bUsingAvoidancePathOffset ? AvoidancePathOffsetSpeedMultiplier : 1.0f));
	const FVector DesiredVelocity = DesiredDirection * MovementSpeedCmPerSec * EffectiveSpeedScale;
	CurrentVelocityCmPerSec = FMath::VInterpTo(CurrentVelocityCmPerSec, DesiredVelocity, DeltaSeconds, AgentKind == ESimCopterGroundAgentKind::Vehicle ? 3.0f : 9.0f);

	const FVector Delta = (CurrentVelocityCmPerSec + ExternalVelocityCmPerSec) * DeltaSeconds;
	const FRotator CurrentRotation = GetActorRotation();
	const FRotator DesiredRotation(0.0f, DesiredDirection.Rotation().Yaw, 0.0f);
	const FRotator NewRotation = FMath::RInterpConstantTo(CurrentRotation, DesiredRotation, DeltaSeconds, TurnRateDegPerSec);

	// Move kinematically (no collision sweep). The traffic system handles road-following,
	// separation, and bump responses between agents; sweeping here would make capsules catch on
	// building corners and stall instead of continuing along the road graph.
	RootComponent->MoveComponent(Delta, NewRotation.Quaternion(), false);
	ExternalVelocityCmPerSec = FMath::VInterpTo(ExternalVelocityCmPerSec, FVector::ZeroVector, DeltaSeconds, 8.0f);
}

bool ASimCopterGroundAgent::TraceGround(FVector& OutGroundLocation) const
{
	if (GetWorld() == nullptr || CollisionComponent == nullptr)
	{
		return false;
	}

	const float HalfHeight = CollisionComponent->GetScaledCapsuleHalfHeight();
	const FVector CurrentLocation = GetActorLocation();
	// Trace well above to well below the agent so placement survives any gap between the traffic
	// spawner's terrain estimate and the city's actual rendered surface (the cause of the old
	// hovering). The Camera channel is blocked by the city terrain/mesh but ignored by every
	// pedestrian/vehicle/player capsule, so agents never snap onto one another.
	const FVector Start = CurrentLocation + FVector::UpVector * GroundProbeUpCm;
	const FVector End = CurrentLocation - FVector::UpVector * (HalfHeight + GroundProbeDistanceCm);
	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterGroundAgentSnap), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Camera, QueryParams) && Hit.bBlockingHit)
	{
		OutGroundLocation = FVector(CurrentLocation.X, CurrentLocation.Y, Hit.ImpactPoint.Z + HalfHeight + 1.0f);
		return true;
	}

	return false;
}

void ASimCopterGroundAgent::UpdateGroundSnap()
{
	FVector GroundedLocation;
	if (TraceGround(GroundedLocation))
	{
		SetActorLocation(GroundedLocation, false);
	}
}

void ASimCopterGroundAgent::UpdateJankyAnimation(float DeltaSeconds)
{
	if (!bEnableJankyAnimation || VisualRoot == nullptr)
	{
		return;
	}

	AnimationTimeSeconds += DeltaSeconds;
	const float SpeedAlpha = FMath::Clamp(CurrentVelocityCmPerSec.Size() / FMath::Max(1.0f, MovementSpeedCmPerSec), 0.0f, 1.0f);
	const float Wave = FMath::Sin(AnimationTimeSeconds * JankyAnimationRate + AnimationPhase);

	if (AgentKind == ESimCopterGroundAgentKind::Vehicle)
	{
		const float Roll = Wave * 1.0f * SpeedAlpha;
		const float Pitch = FMath::Sin(AnimationTimeSeconds * JankyAnimationRate * 0.7f + AnimationPhase) * 0.65f * SpeedAlpha;
		VisualRoot->SetRelativeRotation(FRotator(Pitch, 0.0f, Roll));
		VisualRoot->SetRelativeLocation(FVector::ZeroVector);
	}
	else
	{
		const float Lean = Wave * 7.0f * SpeedAlpha; // degrees of roll (scale-independent)
		const float Bob = FMath::Abs(Wave) * 7.0f * SpeedAlpha * PopulationWorldScale;
		VisualRoot->SetRelativeRotation(FRotator(0.0f, 0.0f, Lean));
		VisualRoot->SetRelativeLocation(FVector(0.0f, 0.0f, Bob));

		if (bUsingPedestrianSprite)
		{
			const int32 DesiredSpriteRow = SpeedAlpha > 0.12f
				? FMath::FloorToInt(AnimationTimeSeconds * JankyAnimationRate) % FSimCopterPopulationSprite::People1Rows
				: 0;
			if (DesiredSpriteRow != PedestrianSpriteRow)
			{
				PedestrianSpriteRow = DesiredSpriteRow;
				FSimCopterPopulationSprite::BuildPeople1FrameQuad(OriginalMeshComponent, PedestrianSpriteColumn, PedestrianSpriteRow, PedestrianSpriteHeightCm);
				if (SpriteMaterialInstance != nullptr)
				{
					OriginalMeshComponent->SetMaterial(0, SpriteMaterialInstance);
				}
			}
		}
	}
}

void ASimCopterGroundAgent::ShowOriginalMesh(bool bUseOriginalMesh)
{
	bUsingOriginalMesh = bUseOriginalMesh;
	if (!bUseOriginalMesh)
	{
		bUsingPedestrianSprite = false;
		bUsingPedestrianBody = false;
	}
	if (OriginalMeshComponent != nullptr)
	{
		OriginalMeshComponent->SetVisibility(bUseOriginalMesh, true);
	}
	if (ProxyMeshComponent != nullptr)
	{
		ProxyMeshComponent->SetVisibility(!bUseOriginalMesh, true);
	}
}

FString ASimCopterGroundAgent::ResolveOriginalGameRoot() const
{
	const FString ConfiguredPath = OriginalGameRoot.Path.TrimStartAndEnd();
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
