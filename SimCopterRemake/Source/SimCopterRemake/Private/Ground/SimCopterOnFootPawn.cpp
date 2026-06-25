// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterOnFootPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Ground/SimCopterPopulationBody.h"
#include "Ground/SimCopterPopulationSprite.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterOnFootPawn, Log, All);

namespace
{
// Matches ASimCopterGroundAgent::PopulationWorldScale - the on-foot avatar was authored in real
// cm and read ~4x too tall next to the 0.25x-scaled city, cars and NPC pedestrians.
constexpr float PopulationWorldScale = 0.25f;

constexpr float OnFootCapsuleRadiusCm = 38.0f;
constexpr float OnFootCapsuleHalfHeightCm = 92.0f;
constexpr float OnFootBodyHeightCm = 184.0f;
constexpr const TCHAR* SpriteMaterialPath = TEXT("/Game/Materials/M_SimCopterSpriteTexture.M_SimCopterSpriteTexture");

UMaterialInterface* LoadSpriteMaterialNoWarn()
{
	return Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, SpriteMaterialPath, nullptr, LOAD_NoWarn));
}
}

ASimCopterOnFootPawn::ASimCopterOnFootPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessPlayer = EAutoReceiveInput::Player0;

	CollisionComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CollisionComponent"));
	CollisionComponent->InitCapsuleSize(OnFootCapsuleRadiusCm * PopulationWorldScale, OnFootCapsuleHalfHeightCm * PopulationWorldScale);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionComponent->SetCollisionObjectType(ECC_Pawn);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	CollisionComponent->SetCanEverAffectNavigation(false);
	SetRootComponent(CollisionComponent);

	BodyProxyComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyProxy"));
	BodyProxyComponent->SetupAttachment(CollisionComponent);
	BodyProxyComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyProxyComponent->SetCanEverAffectNavigation(false);
	BodyProxyComponent->SetRelativeLocation(FVector(0.0f, 0.0f, (-OnFootCapsuleHalfHeightCm + 86.0f) * PopulationWorldScale));
	BodyProxyComponent->SetRelativeScale3D(FVector(0.28f, 0.2f, 1.7f) * PopulationWorldScale);

	OriginalBodySpriteComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("OriginalBodySprite"));
	OriginalBodySpriteComponent->SetupAttachment(CollisionComponent);
	OriginalBodySpriteComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OriginalBodySpriteComponent->SetCanEverAffectNavigation(false);
	OriginalBodySpriteComponent->SetVisibility(false);
	OriginalBodySpriteComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -OnFootCapsuleHalfHeightCm * PopulationWorldScale));

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(CollisionComponent);
	CameraBoom->TargetArmLength = 520.0f * PopulationWorldScale;
	CameraBoom->TargetOffset = FVector(0.0f, 0.0f, 82.0f * PopulationWorldScale);
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->ProbeSize = 16.0f * PopulationWorldScale;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 12.0f;
	CameraBoom->SetRelativeRotation(FRotator(CameraPitchDeg, 0.0f, 0.0f));

	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	CameraComponent->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	CameraComponent->FieldOfView = 78.0f;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		BodyProxyComponent->SetStaticMesh(CubeMeshFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BodyMaterialFinder(TEXT("/Game/Materials/M_SimCopterLitVertexColor.M_SimCopterLitVertexColor"));
	if (BodyMaterialFinder.Succeeded())
	{
		BodyVertexColorMaterial = BodyMaterialFinder.Object;
	}

	SpriteMaterial = LoadSpriteMaterialNoWarn();

	OriginalGameRoot.Path = TEXT("../Reference/SimCopterOriginalGame");
	HelicopterClass = ASimCopterHelicopterPawn::StaticClass();
}

void ASimCopterOnFootPawn::BeginPlay()
{
	Super::BeginPlay();

	SnapToGround();
	LoadOriginalBodySprite();
	if (bFindOrSpawnParkedHelicopterOnBeginPlay)
	{
		FindOrSpawnParkedHelicopter();
	}

	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		PlayerController->bShowMouseCursor = false;
		FInputModeGameOnly InputMode;
		PlayerController->SetInputMode(InputMode);
	}
}

void ASimCopterOnFootPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateMovement(DeltaSeconds);
	SnapToGround();
	UpdateBodySprite(DeltaSeconds);
	UpdateCamera(DeltaSeconds);
}

void ASimCopterOnFootPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindAxis(TEXT("SimCopterPitch"), this, &ASimCopterOnFootPawn::MoveForward);
	PlayerInputComponent->BindAxis(TEXT("SimCopterRoll"), this, &ASimCopterOnFootPawn::MoveRight);
	PlayerInputComponent->BindAxis(TEXT("SimCopterLookYaw"), this, &ASimCopterOnFootPawn::LookYaw);
	PlayerInputComponent->BindAxis(TEXT("SimCopterLookPitch"), this, &ASimCopterOnFootPawn::LookPitch);
	PlayerInputComponent->BindAxis(TEXT("SimCopterMouseLookYaw"), this, &ASimCopterOnFootPawn::MouseLookYaw);
	PlayerInputComponent->BindAxis(TEXT("SimCopterMouseLookPitch"), this, &ASimCopterOnFootPawn::MouseLookPitch);

	PlayerInputComponent->BindAction(TEXT("SimCopterInteract"), IE_Pressed, this, &ASimCopterOnFootPawn::Interact);
}

void ASimCopterOnFootPawn::MoveForward(float Value)
{
	MoveForwardInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void ASimCopterOnFootPawn::MoveRight(float Value)
{
	MoveRightInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void ASimCopterOnFootPawn::LookYaw(float Value)
{
	LookYawInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void ASimCopterOnFootPawn::LookPitch(float Value)
{
	LookPitchInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void ASimCopterOnFootPawn::MouseLookYaw(float Value)
{
	MouseLookYawInput = Value;
}

void ASimCopterOnFootPawn::MouseLookPitch(float Value)
{
	MouseLookPitchInput = Value;
}

void ASimCopterOnFootPawn::Interact()
{
	ASimCopterHelicopterPawn* Helicopter = FindNearestHelicopter(HelicopterInteractionRadiusCm);
	if (Helicopter == nullptr)
	{
		return;
	}

	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		Helicopter->EnterHelicopter(PlayerController);
		Destroy();
	}
}

void ASimCopterOnFootPawn::UpdateMovement(float DeltaSeconds)
{
	if (RootComponent == nullptr)
	{
		return;
	}

	const FRotator ActorRotation = GetActorRotation();
	const FRotationMatrix YawFrame(FRotator(0.0f, ActorRotation.Yaw, 0.0f));
	const FVector DesiredVelocity =
		YawFrame.GetUnitAxis(EAxis::X) * (MoveForwardInput * WalkSpeedCmPerSec) +
		YawFrame.GetUnitAxis(EAxis::Y) * (MoveRightInput * WalkSpeedCmPerSec);

	CurrentVelocityCmPerSec = FMath::VInterpTo(CurrentVelocityCmPerSec, DesiredVelocity, DeltaSeconds, AccelerationInterpSpeed);
	const FRotator NewRotation(0.0f, ActorRotation.Yaw + (LookYawInput + MouseLookYawInput) * LookYawSpeedDegPerSec * DeltaSeconds, 0.0f);

	FHitResult Hit;
	RootComponent->MoveComponent(CurrentVelocityCmPerSec * DeltaSeconds, NewRotation.Quaternion(), true, &Hit);
	if (Hit.IsValidBlockingHit())
	{
		CurrentVelocityCmPerSec = FVector::VectorPlaneProject(CurrentVelocityCmPerSec, Hit.Normal) * 0.4f;
	}
}

void ASimCopterOnFootPawn::UpdateCamera(float DeltaSeconds)
{
	if (CameraBoom == nullptr)
	{
		return;
	}

	CameraPitchDeg = FMath::Clamp(
		CameraPitchDeg + (LookPitchInput + MouseLookPitchInput) * LookPitchSpeedDegPerSec * DeltaSeconds,
		-62.0f,
		14.0f);
	CameraBoom->SetRelativeRotation(FRotator(CameraPitchDeg, 0.0f, 0.0f));
}

void ASimCopterOnFootPawn::UpdateBodySprite(float DeltaSeconds)
{
	if (!bUsingOriginalBodySprite || OriginalBodySpriteComponent == nullptr)
	{
		return;
	}

	// The 3D body is static geometry; give it a little walk bob/lean so it doesn't read as a
	// rigid statue while moving (matching the NPC pedestrians' jank).
	BodySpriteTimeSeconds += DeltaSeconds;
	const float SpeedAlpha = FMath::Clamp(CurrentVelocityCmPerSec.Size() / FMath::Max(1.0f, WalkSpeedCmPerSec), 0.0f, 1.0f);
	const float Wave = FMath::Sin(BodySpriteTimeSeconds * 7.5f);
	const float Bob = FMath::Abs(Wave) * 4.0f * SpeedAlpha * PopulationWorldScale;
	const float Lean = Wave * 4.0f * SpeedAlpha; // degrees of roll (scale-independent)
	OriginalBodySpriteComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -OnFootCapsuleHalfHeightCm * PopulationWorldScale + Bob));
	OriginalBodySpriteComponent->SetRelativeRotation(FRotator(0.0f, 0.0f, Lean));
}

void ASimCopterOnFootPawn::LoadOriginalBodySprite()
{
	if (OriginalBodySpriteComponent == nullptr)
	{
		return;
	}

	// Build the same blocky low-poly 3D body the NPC pedestrians use (the old flat sprite had
	// "no body"). The player keeps a stable outfit for the session.
	const int32 OutfitIndex = FSimCopterPopulationBody::ResolveOutfitIndex(this);
	FSimCopterPopulationBody::BuildPerson(OriginalBodySpriteComponent, OutfitIndex, OnFootBodyHeightCm * PopulationWorldScale);

	if (BodyVertexColorMaterial != nullptr)
	{
		OriginalBodySpriteComponent->SetMaterial(0, BodyVertexColorMaterial);
	}

	OriginalBodySpriteComponent->SetVisibility(true, true);
	if (BodyProxyComponent != nullptr)
	{
		BodyProxyComponent->SetVisibility(false, true);
	}
	bUsingOriginalBodySprite = true;
}

void ASimCopterOnFootPawn::SnapToGround()
{
	FVector GroundedLocation;
	if (CollisionComponent != nullptr && ResolveGroundedLocation(GetActorLocation(), CollisionComponent->GetScaledCapsuleHalfHeight(), GroundedLocation))
	{
		SetActorLocation(GroundedLocation, false);
	}
}

void ASimCopterOnFootPawn::FindOrSpawnParkedHelicopter()
{
	ParkedHelicopter = FindNearestHelicopter(ParkedHelicopterSearchRadiusCm);
	if (ParkedHelicopter != nullptr || GetWorld() == nullptr || HelicopterClass == nullptr || CollisionComponent == nullptr)
	{
		return;
	}

	const FRotationMatrix YawFrame(FRotator(0.0f, GetActorRotation().Yaw, 0.0f));
	const FVector DesiredLocation =
		GetActorLocation() +
		YawFrame.GetUnitAxis(EAxis::X) * ParkedHelicopterOffset.X +
		YawFrame.GetUnitAxis(EAxis::Y) * ParkedHelicopterOffset.Y;

	FVector SpawnLocation = DesiredLocation;
	ResolveGroundedLocation(DesiredLocation, 86.0f, SpawnLocation);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ParkedHelicopter = GetWorld()->SpawnActor<ASimCopterHelicopterPawn>(
		HelicopterClass,
		SpawnLocation,
		FRotator(0.0f, GetActorRotation().Yaw, 0.0f),
		SpawnParams);

	if (ParkedHelicopter != nullptr)
	{
		UE_LOG(LogSimCopterOnFootPawn, Display, TEXT("Spawned parked helicopter at %s."), *SpawnLocation.ToCompactString());
	}
}

ASimCopterHelicopterPawn* ASimCopterOnFootPawn::FindNearestHelicopter(float SearchRadiusCm) const
{
	if (GetWorld() == nullptr)
	{
		return nullptr;
	}

	TArray<AActor*> Helicopters;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ASimCopterHelicopterPawn::StaticClass(), Helicopters);

	ASimCopterHelicopterPawn* BestHelicopter = nullptr;
	float BestDistanceSq = FMath::Square(SearchRadiusCm);
	for (AActor* Actor : Helicopters)
	{
		ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(Actor);
		if (Helicopter == nullptr)
		{
			continue;
		}

		const float DistanceSq = FVector::DistSquared(Helicopter->GetActorLocation(), GetActorLocation());
		if (DistanceSq <= BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			BestHelicopter = Helicopter;
		}
	}

	return BestHelicopter;
}

bool ASimCopterOnFootPawn::ResolveGroundedLocation(const FVector& DesiredLocation, float ActorHalfHeight, FVector& OutLocation) const
{
	if (GetWorld() == nullptr)
	{
		return false;
	}

	const FVector Start = DesiredLocation + FVector::UpVector * 2000.0f;
	const FVector End = DesiredLocation - FVector::UpVector * GroundProbeDistanceCm;
	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterOnFootGroundSnap), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams) && Hit.bBlockingHit)
	{
		OutLocation = FVector(DesiredLocation.X, DesiredLocation.Y, Hit.ImpactPoint.Z + ActorHalfHeight + 2.0f);
		return true;
	}

	return false;
}
