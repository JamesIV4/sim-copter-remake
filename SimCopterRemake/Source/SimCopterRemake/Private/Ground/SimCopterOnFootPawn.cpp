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
#include "Ground/SimCopterGroundAgent.h"
#include "Ground/SimCopterPopulationBody.h"
#include "Ground/SimCopterPopulationFigure.h"
#include "Ground/SimCopterPopulationSprite.h"
#include "Missions/SimCopterMissionSystemActor.h"
#include "Misc/Paths.h"
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
	TryAutoEnterHelicopter();
	if (IsActorBeingDestroyed())
	{
		return;
	}
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
	TryAutoEnterHelicopter();
}

bool ASimCopterOnFootPawn::PickUpMissionPerson(ASimCopterGroundAgent* MissionPerson)
{
	if (MissionPerson == nullptr || CarriedMissionPerson.IsValid() || CollisionComponent == nullptr)
	{
		return false;
	}

	CarriedMissionPerson = MissionPerson;
	CarriedMissionEventId = MissionPerson->MissionEventId;
	MissionPerson->SetCarriedBy(CollisionComponent, FVector(48.0f, 0.0f, -7.0f), FRotator(0.0f, 90.0f, 88.0f));
	return true;
}

ASimCopterGroundAgent* ASimCopterOnFootPawn::ConsumeCarriedMissionPerson()
{
	ASimCopterGroundAgent* MissionPerson = CarriedMissionPerson.Get();
	CarriedMissionPerson.Reset();
	CarriedMissionEventId = INDEX_NONE;
	return MissionPerson;
}

void ASimCopterOnFootPawn::TryAutoEnterHelicopter()
{
	ASimCopterHelicopterPawn* Helicopter = FindNearestHelicopter(HelicopterAutoEnterRadiusCm);
	if (Helicopter == nullptr)
	{
		return;
	}

	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (CarriedMissionPerson.IsValid())
		{
			if (Helicopter->GetAvailablePassengerSeats() <= 0)
			{
				return;
			}

			const int32 EventId = CarriedMissionEventId;
			ASimCopterGroundAgent* MissionPerson = ConsumeCarriedMissionPerson();
			if (MissionPerson != nullptr)
			{
				MissionPerson->Destroy();
			}

			const int32 Boarded = Helicopter->AddMissionPassengersForMission(1, EventId, ESimCopterMissionPassengerKind::Medevac);
			if (Boarded > 0)
			{
				if (ASimCopterMissionSystemActor* MissionActor = Cast<ASimCopterMissionSystemActor>(
					UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass())))
				{
					MissionActor->NotifyMedevacVictimBoarded(EventId, Boarded);
				}
			}
		}

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

	const float SpeedAlpha = FMath::Clamp(CurrentVelocityCmPerSec.Size() / FMath::Max(1.0f, WalkSpeedCmPerSec), 0.0f, 1.0f);

	if (bUsingOriginalFigure)
	{
		// The pilot figure animates through the original clip frames - no bob/lean overlay.
		OriginalBodySpriteComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -OnFootCapsuleHalfHeightCm * PopulationWorldScale));
		OriginalBodySpriteComponent->SetRelativeRotation(FRotator::ZeroRotator);

		const bool bWalking = SpeedAlpha > 0.12f;
		const FString Desired = bWalking ? TEXT("1Wal") : TEXT("NoMo");
		if (Desired != FigureAnim.Mnemonic)
		{
			RebuildPlayerFigureClip(Desired);
		}
		if (FigureAnim.FrameCount > 1)
		{
			FigureAnim.FrameTime += DeltaSeconds * (bWalking ? 8.0f : 4.0f);
			const int32 DesiredFrame = FMath::FloorToInt(FigureAnim.FrameTime) % FigureAnim.FrameCount;
			if (DesiredFrame != FigureAnim.CurrentFrame)
			{
				FigureAnim.CurrentFrame = DesiredFrame;
				FSimCopterPopulationFigure::ShowFrame(OriginalBodySpriteComponent, FigureAnim.FrameCount, FigureAnim.CurrentFrame, FigureAnim.bHasHeadSection);
			}
		}
		return;
	}

	// The box body is static geometry; give it a little walk bob/lean so it doesn't read as a
	// rigid statue while moving (matching the NPC pedestrians' jank).
	BodySpriteTimeSeconds += DeltaSeconds;
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

	// Preferred: the original privanim "pilot" figure with its real animation clips.
	if (LoadOriginalBodyFigure())
	{
		return;
	}

	// Fallback: the blocky low-poly 3D body the NPC pedestrians used before the figure decode.
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

bool ASimCopterOnFootPawn::LoadOriginalBodyFigure()
{
	bUsingOriginalFigure = false;
	if (PlayerFigureName.IsEmpty())
	{
		return false;
	}

	FString RootPath = OriginalGameRoot.Path.TrimStartAndEnd();
	if (RootPath.IsEmpty())
	{
		return false;
	}
	if (FPaths::IsRelative(RootPath))
	{
		RootPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), RootPath));
	}

	FString Error;
	FigureShared = FSimCopterPopulationFigure::GetShared(RootPath, Error);
	if (!FigureShared.IsValid())
	{
		UE_LOG(LogSimCopterOnFootPawn, Warning, TEXT("Player figure unavailable: %s"), *Error);
		return false;
	}
	FigureAnim.FigureIndex = FigureShared->Model.FindFigureIndex(PlayerFigureName);
	if (FigureAnim.FigureIndex == INDEX_NONE)
	{
		return false;
	}

	// Pilot head: the first entry of the head-image table suits the player; texture optional.
	if (SpriteMaterial != nullptr && FigureHeadMaterialInstance == nullptr)
	{
		const TArray<int32>& HeadTable = FSimCopterPopulationFigure::GetHeadImageTable();
		if (const FMaxisTextureImage* HeadImage = FigureShared->HeadImages.Find(HeadTable[0]))
		{
			FigureHeadTexture = FSimCopterPopulationSprite::CreateTextureFromImage(this, *HeadImage, TEXT("SimCopterPlayerHead"));
			if (FigureHeadTexture != nullptr)
			{
				FigureHeadMaterialInstance = UMaterialInstanceDynamic::Create(SpriteMaterial, this);
				if (FigureHeadMaterialInstance != nullptr)
				{
					FigureHeadMaterialInstance->SetTextureParameterValue(TEXT("Texture"), FigureHeadTexture);
				}
			}
		}
	}

	bUsingOriginalFigure = true;
	if (!RebuildPlayerFigureClip(TEXT("NoMo")))
	{
		bUsingOriginalFigure = false;
		return false;
	}

	OriginalBodySpriteComponent->SetVisibility(true, true);
	if (BodyProxyComponent != nullptr)
	{
		BodyProxyComponent->SetVisibility(false, true);
	}
	bUsingOriginalBodySprite = true;
	return true;
}

bool ASimCopterOnFootPawn::RebuildPlayerFigureClip(const FString& Mnemonic)
{
	if (!bUsingOriginalFigure || !FigureShared.IsValid() || !FigureShared->Model.Figures.IsValidIndex(FigureAnim.FigureIndex))
	{
		return false;
	}
	const FPrivAnimFigure& Figure = FigureShared->Model.Figures[FigureAnim.FigureIndex];
	const FPrivAnimClip* Clip = FigureShared->Model.FindClip(Figure, Mnemonic);
	if (Clip == nullptr)
	{
		Clip = FigureShared->Model.FindClip(Figure, TEXT("NoMo"));
	}
	if (Clip == nullptr)
	{
		return false;
	}

	const float HeightCm = OnFootBodyHeightCm * PopulationWorldScale;
	const FPrivAnimClip* StandingClip = FigureShared->Model.FindClip(Figure, TEXT("1Wal"));
	const FSimCopterPopulationFigure::FCalibration Calibration =
		FSimCopterPopulationFigure::Calibrate(StandingClip != nullptr ? *StandingClip : *Clip, HeightCm);

	FSimCopterPopulationFigure::FBuildParams Params;
	Params.HeightCm = HeightCm;
	Params.ClothesOffset = 0;
	Params.bTexturedHead = FigureHeadMaterialInstance != nullptr;

	if (!FSimCopterPopulationFigure::BuildClipSections(
			OriginalBodySpriteComponent, Figure, *Clip, FigureShared->Palette, Params, Calibration, FigureAnim.bHasHeadSection))
	{
		return false;
	}
	for (int32 Frame = 0; Frame < Clip->FrameCount; ++Frame)
	{
		if (BodyVertexColorMaterial != nullptr)
		{
			OriginalBodySpriteComponent->SetMaterial(Frame * 2, BodyVertexColorMaterial);
		}
		OriginalBodySpriteComponent->SetMaterial(
			Frame * 2 + 1,
			FigureHeadMaterialInstance != nullptr ? static_cast<UMaterialInterface*>(FigureHeadMaterialInstance) : BodyVertexColorMaterial.Get());
	}

	FigureAnim.Mnemonic = Mnemonic;
	FigureAnim.FrameCount = Clip->FrameCount;
	FigureAnim.CurrentFrame = 0;
	FigureAnim.FrameTime = 0.0f;
	FSimCopterPopulationFigure::ShowFrame(OriginalBodySpriteComponent, FigureAnim.FrameCount, 0, FigureAnim.bHasHeadSection);
	return true;
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
