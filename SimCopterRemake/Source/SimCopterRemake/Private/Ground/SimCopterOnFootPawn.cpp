// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterOnFootPawn.h"

#include "Audio/SimCopterAudioSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Formats/SimCopterOriginalGamePaths.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "Ground/SimCopterAmbientVehicles.h"
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
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UI/SSimCopterControllerOverlay.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/SOverlay.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterOnFootPawn, Log, All);

namespace
{
constexpr uint32 OnFootRuntimeSaveMagic = 0x464f4f54; // 'FOOT'
constexpr int32 OnFootRuntimeSaveVersion = 1;

void SerializeOnFootBool(FArchive& Archive, bool& Value)
{
	uint8 Byte = Value ? 1 : 0;
	Archive << Byte;
	if (Archive.IsLoading()) Value = Byte != 0;
}

// Matches ASimCopterGroundAgent::PopulationWorldScale - the on-foot avatar was authored in real
// cm and read ~4x too tall next to the 0.25x-scaled city, cars and NPC pedestrians.
constexpr float PopulationWorldScale = 0.25f;

constexpr float OnFootCapsuleRadiusCm = 38.0f;
constexpr float OnFootCapsuleHalfHeightCm = 92.0f;
constexpr float OnFootBodyHeightCm = 184.0f;
// Masked and chroma-keyed like the unlit sprite material the head used to share with the effect
// cards, but Default Lit - see ASimCopterGroundAgent::FigureHeadMaterial.
constexpr const TCHAR* FigureHeadMaterialPath = TEXT("/Game/Materials/M_SimCopterLitSpriteTexture.M_SimCopterLitSpriteTexture");

UMaterialInterface* LoadFigureHeadMaterialNoWarn()
{
	return Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, FigureHeadMaterialPath, nullptr, LOAD_NoWarn));
}
}

ASimCopterOnFootPawn::ASimCopterOnFootPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessPlayer = EAutoReceiveInput::Player0;

	// Drive the avatar with the inherited character capsule + movement component: gravity, jumping,
	// air control and automatic step-up over curbs/road lips all come for free and robustly.
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	Capsule->InitCapsuleSize(OnFootCapsuleRadiusCm * PopulationWorldScale, OnFootCapsuleHalfHeightCm * PopulationWorldScale);
	Capsule->SetCollisionObjectType(ECC_Pawn);
	Capsule->SetCollisionResponseToAllChannels(ECR_Block);
	Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Capsule->SetCanEverAffectNavigation(false);

	// We render the original sprite/figure, not the default skeletal mesh.
	if (USkeletalMeshComponent* CharacterMesh = GetMesh())
	{
		CharacterMesh->SetVisibility(false);
		CharacterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->bOrientRotationToMovement = false;
		Move->bUseControllerDesiredRotation = false;
		Move->RotationRate = FRotator::ZeroRotator;
		Move->bConstrainToPlane = false;
		Move->SetWalkableFloorAngle(52.0f);
	}
	// We steer the avatar's yaw ourselves from the look input, not the controller rotation.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	BodyProxyComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyProxy"));
	BodyProxyComponent->SetupAttachment(Capsule);
	BodyProxyComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyProxyComponent->SetCanEverAffectNavigation(false);
	BodyProxyComponent->SetRelativeLocation(FVector(0.0f, 0.0f, (-OnFootCapsuleHalfHeightCm + 86.0f) * PopulationWorldScale));
	BodyProxyComponent->SetRelativeScale3D(FVector(0.28f, 0.2f, 1.7f) * PopulationWorldScale);

	OriginalBodySpriteComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("OriginalBodySprite"));
	OriginalBodySpriteComponent->SetupAttachment(Capsule);
	OriginalBodySpriteComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OriginalBodySpriteComponent->SetCanEverAffectNavigation(false);
	OriginalBodySpriteComponent->SetVisibility(false);
	OriginalBodySpriteComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -OnFootCapsuleHalfHeightCm * PopulationWorldScale));
	// Per-object shadow rather than the sun's cascades - see the same call in
	// ASimCopterGroundAgent: the figure is far too small for a CSM texel and self-shadows into
	// acne across its overlapping parts. bExcludeFromLightAttachmentGroup is needed because the
	// capsule we hang off is the attachment root, and a light attachment group would otherwise
	// discard this component's shadow settings.
	OriginalBodySpriteComponent->bCastInsetShadow = true;
	OriginalBodySpriteComponent->bExcludeFromLightAttachmentGroup = true;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(Capsule);
	CameraBoom->TargetArmLength = 520.0f * PopulationWorldScale;
	CameraBoom->TargetOffset = FVector(0.0f, 0.0f, 82.0f * PopulationWorldScale);
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->ProbeChannel = ECC_Camera;
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

	FigureHeadMaterial = LoadFigureHeadMaterialNoWarn();

	OriginalGameRoot.Path = TEXT("../Reference/SimCopterOriginalGame");
	HelicopterClass = ASimCopterHelicopterPawn::StaticClass();
}

void ASimCopterOnFootPawn::BeginPlay()
{
	Super::BeginPlay();

	// Apply the tunable movement settings (so editor edits take effect) and keep us walking.
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = WalkSpeedCmPerSec;
		Move->MaxAcceleration = MaxAccelerationCmPerSec2;
		Move->BrakingDecelerationWalking = MaxAccelerationCmPerSec2;
		Move->MaxStepHeight = MaxStepHeightCm;
		Move->JumpZVelocity = JumpZVelocityCmPerSec;
		Move->AirControl = AirControl;
		Move->GravityScale = GravityScale;
		Move->SetMovementMode(MOVE_Walking);
	}
	JumpMaxCount = 1;

	SnapToGround(); // initial placement; the movement component keeps us grounded thereafter
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
	EnsureControllerOverlayWidget();
	// Same reason as the helicopter's: the overlay above is a Slate widget, and a focused widget
	// takes the keys before the pawn's axis bindings do.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void ASimCopterOnFootPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	// Same stuck-key flush as the helicopter: a key held across the swap otherwise keeps
	// reporting its held value on the pawn you just moved to.
	if (APlayerController* PlayerController = Cast<APlayerController>(NewController))
	{
		PlayerController->FlushPressedKeys();
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void ASimCopterOnFootPawn::UnPossessed()
{
	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		PlayerController->FlushPressedKeys();
	}
	Super::UnPossessed();
}

void ASimCopterOnFootPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveControllerOverlayWidget();
	Super::EndPlay(EndPlayReason);
}

void ASimCopterOnFootPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (MissionPickupCooldownSeconds > 0.0f)
	{
		MissionPickupCooldownSeconds = FMath::Max(0.0f, MissionPickupCooldownSeconds - DeltaSeconds);
	}

	UpdateLookYaw(DeltaSeconds);
	TryAutoEnterHelicopter();
	if (IsActorBeingDestroyed())
	{
		return;
	}
	UpdateBodySprite(DeltaSeconds);
	UpdateCamera(DeltaSeconds);

	// The listener has to follow whoever the player is. Every distance in the mixer is measured
	// against it (DAT_0061a748 in the original), so leaving it parked at the helicopter would
	// attenuate the whole city against a point the player walked away from.
	if (IsLocallyControlled() && CameraComponent != nullptr)
	{
		if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
		{
			Audio->SetListener(
				CameraComponent->GetComponentLocation(),
				CameraComponent->GetComponentRotation());
		}
	}
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
	PlayerInputComponent->BindAxis(TEXT("SimCopterControllerLeftY"), this, &ASimCopterOnFootPawn::MoveForward);
	PlayerInputComponent->BindAxis(TEXT("SimCopterControllerLeftX"), this, &ASimCopterOnFootPawn::MoveRight);
	PlayerInputComponent->BindAxis(TEXT("SimCopterControllerRightX"), this, &ASimCopterOnFootPawn::LookYaw);
	PlayerInputComponent->BindAxis(TEXT("SimCopterControllerRightY"), this, &ASimCopterOnFootPawn::ControllerLookPitch);

	PlayerInputComponent->BindAction(TEXT("SimCopterInteract"), IE_Pressed, this, &ASimCopterOnFootPawn::Interact);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Top, IE_Pressed, this, &ASimCopterOnFootPawn::Interact);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Left, IE_Pressed, this, &ASimCopterOnFootPawn::DropCarriedMissionPerson);

	// Drop a carried person on the ground (e.g. when the helicopter is full and you need to come
	// back for them later). Bound directly to F so no input-mapping config edit is required.
	PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &ASimCopterOnFootPawn::DropCarriedMissionPerson);

	// Jump with air control (works while carrying someone - the carried person is attached to the
	// capsule). Bound directly to Space so no input-mapping config edit is required.
	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Released, this, &ACharacter::StopJumping);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Bottom, IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Bottom, IE_Released, this, &ACharacter::StopJumping);

	FInputKeyBinding& PauseBinding = PlayerInputComponent->BindKey(
		EKeys::Gamepad_Special_Right,
		IE_Pressed,
		this,
		&ASimCopterOnFootPawn::ToggleGamePause);
	PauseBinding.bExecuteWhenPaused = true;
}

void ASimCopterOnFootPawn::MoveForward(float Value)
{
	if (!FMath::IsNearlyZero(Value))
	{
		const FRotator YawRotation(0.0f, GetActorRotation().Yaw, 0.0f);
		AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), FMath::Clamp(Value, -1.0f, 1.0f));
	}
}

void ASimCopterOnFootPawn::MoveRight(float Value)
{
	if (!FMath::IsNearlyZero(Value))
	{
		// Character movement caps at MaxWalkSpeed scaled by the input magnitude, so a scaled
		// strafe input is also a scaled strafe speed - no second speed setting needed.
		const FRotator YawRotation(0.0f, GetActorRotation().Yaw, 0.0f);
		AddMovementInput(
			FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y),
			FMath::Clamp(Value, -1.0f, 1.0f) * StrafeSpeedScale);
	}
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

void ASimCopterOnFootPawn::ControllerLookPitch(float Value)
{
	// The existing camera subtracts its look input. Invert the raw gamepad axis so stick-up
	// raises the view, matching the old Gamepad_RightY mapping.
	LookPitch(-Value);
}

void ASimCopterOnFootPawn::Interact()
{
	TryEnterHelicopter(HelicopterInteractionReachCm);
}

void ASimCopterOnFootPawn::ToggleGamePause()
{
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	UWorld* World = GetWorld();
	if (PlayerController != nullptr && World != nullptr)
	{
		PlayerController->SetPause(!World->IsPaused());
	}
}

void ASimCopterOnFootPawn::EnsureControllerOverlayWidget()
{
	if (ControllerOverlayWidget.IsValid() || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	ControllerOverlayWidget =
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SSimCopterControllerOverlay)
				.Pawn(nullptr)
		];
	GEngine->GameViewport->AddViewportWidgetContent(ControllerOverlayWidget.ToSharedRef(), 60);
}

void ASimCopterOnFootPawn::RemoveControllerOverlayWidget()
{
	if (GEngine != nullptr && GEngine->GameViewport != nullptr && ControllerOverlayWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(ControllerOverlayWidget.ToSharedRef());
	}
	ControllerOverlayWidget.Reset();
}

void ASimCopterOnFootPawn::DropCarriedMissionPerson()
{
	if (!CarriedMissionPerson.IsValid())
	{
		return;
	}

	ASimCopterGroundAgent* Patient = ConsumeCarriedMissionPerson();
	if (Patient == nullptr)
	{
		return;
	}

	// Set them down just in front of the avatar (not on the player's head), still an injured
	// pickup so they can be collected again later.
	const FRotationMatrix YawFrame(FRotator(0.0f, GetActorRotation().Yaw, 0.0f));
	const FVector DropLocation = GetActorLocation() + YawFrame.GetUnitAxis(EAxis::X) * 60.0f;
	Patient->SetDroppedInjuredOnGround(DropLocation);

	// Don't let the auto-pickup logic re-grab them on the very next frame.
	MissionPickupCooldownSeconds = MissionDropRepickupCooldownSeconds;
}

bool ASimCopterOnFootPawn::PickUpMissionPerson(ASimCopterGroundAgent* MissionPerson)
{
	if (MissionPerson == nullptr || CarriedMissionPerson.IsValid() || GetCapsuleComponent() == nullptr)
	{
		return false;
	}

	CarriedMissionPerson = MissionPerson;
	CarriedMissionEventId = MissionPerson->MissionEventId;
	MissionPerson->SetCarriedBy(GetCapsuleComponent(), CarriedMissionPersonOffsetCm, FRotator(0.0f, 90.0f, 88.0f));
	return true;
}

ASimCopterGroundAgent* ASimCopterOnFootPawn::ConsumeCarriedMissionPerson()
{
	ASimCopterGroundAgent* MissionPerson = CarriedMissionPerson.Get();
	CarriedMissionPerson.Reset();
	CarriedMissionEventId = INDEX_NONE;
	return MissionPerson;
}

bool ASimCopterOnFootPawn::CaptureRuntimeSaveState(TArray<uint8>& OutData) const
{
	OutData.Reset();
	FMemoryWriter Writer(OutData, true);
	uint32 Magic = OnFootRuntimeSaveMagic;
	int32 Version = OnFootRuntimeSaveVersion;
	Writer << Magic;
	Writer << Version;

	FTransform Transform = GetActorTransform();
	FVector Velocity = GetVelocity();
	uint8 MovementMode = MOVE_None;
	uint8 CustomMovementMode = 0;
	if (const UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Velocity = Move->Velocity;
		MovementMode = static_cast<uint8>(Move->MovementMode);
		CustomMovementMode = Move->CustomMovementMode;
	}
	Writer << Transform;
	Writer << Velocity;
	Writer << MovementMode;
	Writer << CustomMovementMode;

	float SavedCameraPitchDeg = CameraPitchDeg;
	int32 SavedBodySpriteRow = BodySpriteRow;
	float SavedBodySpriteTimeSeconds = BodySpriteTimeSeconds;
	FString FigureMnemonic = FigureAnim.Mnemonic;
	int32 FigureFrame = FigureAnim.CurrentFrame;
	float FigureFrameTime = FigureAnim.FrameTime;
	Writer << SavedCameraPitchDeg;
	Writer << SavedBodySpriteRow;
	Writer << SavedBodySpriteTimeSeconds;
	Writer << FigureMnemonic;
	Writer << FigureFrame;
	Writer << FigureFrameTime;

	FName CarriedPersonName = NAME_None;
	if (const ASimCopterGroundAgent* CarriedPerson = CarriedMissionPerson.Get())
	{
		CarriedPersonName = CarriedPerson->GetRuntimeSaveIdentityName();
	}
	int32 SavedCarriedMissionEventId = CarriedMissionEventId;
	float SavedPickupCooldownSeconds = MissionPickupCooldownSeconds;
	Writer << CarriedPersonName;
	Writer << SavedCarriedMissionEventId;
	Writer << SavedPickupCooldownSeconds;

	FRotator ControlRotation = FRotator::ZeroRotator;
	bool bHasControlRotation = false;
	if (const AController* OwningController = GetController())
	{
		ControlRotation = OwningController->GetControlRotation();
		bHasControlRotation = true;
	}
	SerializeOnFootBool(Writer, bHasControlRotation);
	Writer << ControlRotation;

	bool bSavedPressedJump = bPressedJump != 0;
	bool bSavedWasJumping = bWasJumping != 0;
	float SavedJumpKeyHoldTime = JumpKeyHoldTime;
	float SavedJumpForceTimeRemaining = JumpForceTimeRemaining;
	int32 SavedJumpCurrentCount = JumpCurrentCount;
	int32 SavedJumpCurrentCountPreJump = JumpCurrentCountPreJump;
	SerializeOnFootBool(Writer, bSavedPressedJump);
	SerializeOnFootBool(Writer, bSavedWasJumping);
	Writer << SavedJumpKeyHoldTime;
	Writer << SavedJumpForceTimeRemaining;
	Writer << SavedJumpCurrentCount;
	Writer << SavedJumpCurrentCountPreJump;
	return !Writer.IsError();
}

bool ASimCopterOnFootPawn::RestoreRuntimeSaveState(const TArray<uint8>& Data)
{
	FMemoryReader Reader(Data, true);
	uint32 Magic = 0;
	int32 Version = 0;
	Reader << Magic;
	Reader << Version;
	if (Reader.IsError() || Magic != OnFootRuntimeSaveMagic || Version != OnFootRuntimeSaveVersion)
	{
		return false;
	}

	FTransform Transform = FTransform::Identity;
	FVector Velocity = FVector::ZeroVector;
	uint8 MovementMode = MOVE_None;
	uint8 CustomMovementMode = 0;
	Reader << Transform;
	Reader << Velocity;
	Reader << MovementMode;
	Reader << CustomMovementMode;

	float SavedCameraPitchDeg = 0.0f;
	int32 SavedBodySpriteRow = INDEX_NONE;
	float SavedBodySpriteTimeSeconds = 0.0f;
	FString FigureMnemonic;
	int32 FigureFrame = 0;
	float FigureFrameTime = 0.0f;
	Reader << SavedCameraPitchDeg;
	Reader << SavedBodySpriteRow;
	Reader << SavedBodySpriteTimeSeconds;
	Reader << FigureMnemonic;
	Reader << FigureFrame;
	Reader << FigureFrameTime;

	FName CarriedPersonName = NAME_None;
	int32 SavedCarriedMissionEventId = INDEX_NONE;
	float SavedPickupCooldownSeconds = 0.0f;
	Reader << CarriedPersonName;
	Reader << SavedCarriedMissionEventId;
	Reader << SavedPickupCooldownSeconds;

	bool bHasControlRotation = false;
	FRotator ControlRotation = FRotator::ZeroRotator;
	SerializeOnFootBool(Reader, bHasControlRotation);
	Reader << ControlRotation;

	bool bSavedPressedJump = false;
	bool bSavedWasJumping = false;
	float SavedJumpKeyHoldTime = 0.0f;
	float SavedJumpForceTimeRemaining = 0.0f;
	int32 SavedJumpCurrentCount = 0;
	int32 SavedJumpCurrentCountPreJump = 0;
	SerializeOnFootBool(Reader, bSavedPressedJump);
	SerializeOnFootBool(Reader, bSavedWasJumping);
	Reader << SavedJumpKeyHoldTime;
	Reader << SavedJumpForceTimeRemaining;
	Reader << SavedJumpCurrentCount;
	Reader << SavedJumpCurrentCountPreJump;

	if (Reader.IsError() || Reader.Tell() != Data.Num() || Transform.ContainsNaN() || Velocity.ContainsNaN() ||
		MovementMode >= MOVE_MAX || !FMath::IsFinite(SavedCameraPitchDeg) ||
		SavedCameraPitchDeg < -90.0f || SavedCameraPitchDeg > 90.0f ||
		!FMath::IsFinite(SavedBodySpriteTimeSeconds) || SavedBodySpriteTimeSeconds < 0.0f ||
		FigureMnemonic.Len() > 16 || FigureFrame < 0 || !FMath::IsFinite(FigureFrameTime) || FigureFrameTime < 0.0f ||
		!FMath::IsFinite(SavedPickupCooldownSeconds) || SavedPickupCooldownSeconds < 0.0f ||
		(bHasControlRotation && ControlRotation.ContainsNaN()) ||
		!FMath::IsFinite(SavedJumpKeyHoldTime) || SavedJumpKeyHoldTime < 0.0f ||
		!FMath::IsFinite(SavedJumpForceTimeRemaining) || SavedJumpForceTimeRemaining < 0.0f ||
		SavedJumpCurrentCount < 0 || SavedJumpCurrentCount > JumpMaxCount ||
		SavedJumpCurrentCountPreJump < 0 || SavedJumpCurrentCountPreJump > JumpMaxCount)
	{
		return false;
	}

	ASimCopterGroundAgent* CarriedPerson = nullptr;
	if (!CarriedPersonName.IsNone())
	{
		for (TActorIterator<ASimCopterGroundAgent> It(GetWorld()); It; ++It)
		{
			if (It->GetRuntimeSaveIdentityName() == CarriedPersonName)
			{
				CarriedPerson = *It;
				break;
			}
		}
		if (CarriedPerson == nullptr || CarriedPerson->MissionEventId != SavedCarriedMissionEventId)
		{
			return false;
		}
	}

	SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->SetMovementMode(static_cast<EMovementMode>(MovementMode), CustomMovementMode);
		Move->Velocity = Velocity;
	}
	CameraPitchDeg = SavedCameraPitchDeg;
	if (CameraBoom != nullptr)
	{
		CameraBoom->SetRelativeRotation(FRotator(CameraPitchDeg, 0.0f, 0.0f));
	}
	BodySpriteRow = SavedBodySpriteRow;
	BodySpriteTimeSeconds = SavedBodySpriteTimeSeconds;

	if (bUsingOriginalFigure && !FigureMnemonic.IsEmpty() && RebuildPlayerFigureClip(FigureMnemonic))
	{
		FigureAnim.FrameTime = FigureFrameTime;
		FigureAnim.CurrentFrame = FMath::Clamp(FigureFrame, 0, FMath::Max(0, FigureAnim.FrameCount - 1));
		FSimCopterPopulationFigure::ShowFrame(
			OriginalBodySpriteComponent,
			FigureAnim.FrameCount,
			FigureAnim.CurrentFrame,
			FigureAnim.bHasHeadSection);
	}

	CarriedMissionPerson.Reset();
	CarriedMissionEventId = INDEX_NONE;
	if (CarriedPerson != nullptr && !PickUpMissionPerson(CarriedPerson))
	{
		return false;
	}
	MissionPickupCooldownSeconds = SavedPickupCooldownSeconds;
	if (bHasControlRotation && GetController() != nullptr)
	{
		GetController()->SetControlRotation(ControlRotation);
	}
	bPressedJump = bSavedPressedJump;
	bWasJumping = bSavedWasJumping;
	JumpKeyHoldTime = SavedJumpKeyHoldTime;
	JumpForceTimeRemaining = SavedJumpForceTimeRemaining;
	JumpCurrentCount = SavedJumpCurrentCount;
	JumpCurrentCountPreJump = SavedJumpCurrentCountPreJump;
	return true;
}

void ASimCopterOnFootPawn::SimBoardHelicopter()
{
	ASimCopterHelicopterPawn* Helicopter = FindNearestHelicopter(TNumericLimits<float>::Max());
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (Helicopter == nullptr || PlayerController == nullptr)
	{
		UE_LOG(LogTemp, Display, TEXT("SimBoardHelicopter: no helicopter or no player controller."));
		return;
	}

	if (!TryBoardCarriedMissionPerson(Helicopter))
	{
		UE_LOG(LogTemp, Display, TEXT("SimBoardHelicopter: carried patient could not board."));
		return;
	}

	UE_LOG(LogTemp, Display, TEXT("SimBoardHelicopter: possessing %s."), *Helicopter->GetName());
	RemoveControllerOverlayWidget();
	Helicopter->EnterHelicopter(PlayerController);
}

void ASimCopterOnFootPawn::SimStartMission(int32 TypeMask)
{
	ASimCopterMissionSystemActor* Mission = Cast<ASimCopterMissionSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass()));
	if (Mission == nullptr)
	{
		UE_LOG(LogTemp, Display, TEXT("SimStartMission: no mission system actor."));
		return;
	}

	const int32 EventId = Mission->StartMissionNow(TypeMask);
	UE_LOG(LogTemp, Display, TEXT("SimStartMission: mask 0x%x -> event %d."), TypeMask, EventId);
}

void ASimCopterOnFootPawn::SimDumpAmbientVehicles()
{
	ASimCopterMissionSystemActor* Mission = Cast<ASimCopterMissionSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass()));
	if (Mission == nullptr)
	{
		UE_LOG(LogTemp, Display, TEXT("SimDumpAmbientVehicles: no mission system actor."));
		return;
	}

	if (ASimCopterAmbientVehiclesActor* Vehicles = Mission->ResolveAmbientVehicles())
	{
		UE_LOG(LogTemp, Display, TEXT("SimDumpAmbientVehicles: %s"), *Vehicles->GetStatusLine());
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("SimDumpAmbientVehicles: no ambient vehicles actor."));
	}
}

void ASimCopterOnFootPawn::SimGotoAmbient(int32 Which)
{
	ASimCopterMissionSystemActor* Mission = Cast<ASimCopterMissionSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass()));
	ASimCopterAmbientVehiclesActor* Vehicles = Mission != nullptr ? Mission->ResolveAmbientVehicles() : nullptr;
	FVector Target = FVector::ZeroVector;
	if (Vehicles == nullptr || !Vehicles->TryGetDebugViewTarget(Which, Target))
	{
		UE_LOG(LogTemp, Display, TEXT("SimGotoAmbient %d: nothing there right now."), Which);
		return;
	}

	const FVector Offset(700.0f, -700.0f, 500.0f);
	SetActorLocation(Target + Offset, false);
	if (AController* OwningController = GetController())
	{
		OwningController->SetControlRotation((Target - (Target + Offset)).Rotation());
	}
	UE_LOG(LogTemp, Display, TEXT("SimGotoAmbient %d: %s"), Which, *Target.ToCompactString());
}

bool ASimCopterOnFootPawn::TryBoardCarriedMissionPerson(ASimCopterHelicopterPawn* Helicopter)
{
	if (!CarriedMissionPerson.IsValid())
	{
		return true;
	}
	if (Helicopter == nullptr || Helicopter->GetAvailablePassengerSeats() <= 0)
	{
		return false;
	}

	ASimCopterGroundAgent* MissionPerson = CarriedMissionPerson.Get();
	if (MissionPerson == nullptr || !MissionPerson->BoardCarrier(Helicopter, /*bAsHarnessRider*/ false))
	{
		return false;
	}

	// BoardCarrier detached and preserved the actual actor, claimed the seat, and notified the
	// mission action service. Only clear the on-foot weak reference here.
	ConsumeCarriedMissionPerson();
	return true;
}

void ASimCopterOnFootPawn::TryAutoEnterHelicopter()
{
	TryEnterHelicopter(HelicopterAutoEnterReachCm);
}

void ASimCopterOnFootPawn::TryEnterHelicopter(const float ReachCm)
{
	ASimCopterHelicopterPawn* Helicopter = FindHelicopterWithinReach(ReachCm);
	if (Helicopter == nullptr)
	{
		return;
	}

	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (!TryBoardCarriedMissionPerson(Helicopter))
		{
			return;
		}

		RemoveControllerOverlayWidget();
		Helicopter->EnterHelicopter(PlayerController);
	}
}

void ASimCopterOnFootPawn::UpdateLookYaw(float DeltaSeconds)
{
	// Steer the avatar (and therefore the movement/camera frame) from the look input. Works the
	// same on the ground and mid-jump, giving air control when combined with the movement input.
	const float YawDeltaDeg = (LookYawInput + MouseLookYawInput) * LookYawSpeedDegPerSec * DeltaSeconds;
	if (!FMath::IsNearlyZero(YawDeltaDeg))
	{
		AddActorWorldRotation(FRotator(0.0f, YawDeltaDeg, 0.0f));
	}
}

void ASimCopterOnFootPawn::UpdateCamera(float DeltaSeconds)
{
	if (CameraBoom == nullptr)
	{
		return;
	}

	CameraPitchDeg = FMath::Clamp(
		CameraPitchDeg - (LookPitchInput + MouseLookPitchInput) * LookPitchSpeedDegPerSec * DeltaSeconds,
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

	const float SpeedAlpha = FMath::Clamp(GetVelocity().Size2D() / FMath::Max(1.0f, WalkSpeedCmPerSec), 0.0f, 1.0f);

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
			FigureAnim.FrameTime += DeltaSeconds * (bWalking ? FigureWalkFrameRate : FigureIdleFrameRate);
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

	if (LoadOriginalBodyFigure())
	{
		return;
	}

	UE_LOG(LogSimCopterOnFootPawn, Error, TEXT("Failed to load original body figure for player."));
}

bool ASimCopterOnFootPawn::LoadOriginalBodyFigure()
{
	bUsingOriginalFigure = false;
	if (PlayerFigureName.IsEmpty())
	{
		return false;
	}

	FString RootPath;
	const FString ConfiguredPath = OriginalGameRoot.Path.TrimStartAndEnd();
	if (!ConfiguredPath.IsEmpty())
	{
		const FString FullPath = FPaths::IsRelative(ConfiguredPath)
			? FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), ConfiguredPath))
			: FPaths::ConvertRelativePathToFull(ConfiguredPath);
		if (FPaths::DirectoryExists(FullPath) && !FSimCopterPrivAnimReader::ResolvePrivAnimPath(FullPath).IsEmpty())
		{
			RootPath = FullPath;
		}
	}

	if (RootPath.IsEmpty())
	{
		// The figure needs privanim specifically, so a root that merely looks like an install is
		// not good enough here.
		RootPath = SimCopterOriginalGame::ResolveRootBy([](const FString& Root)
		{
			return !FSimCopterPrivAnimReader::ResolvePrivAnimPath(Root).IsEmpty();
		});
	}

	if (RootPath.IsEmpty())
	{
		return false;
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
	if (FigureHeadMaterial != nullptr && FigureHeadMaterialInstance == nullptr)
	{
		const TArray<int32>& HeadTable = FSimCopterPopulationFigure::GetHeadImageTable();
		if (const FMaxisTextureImage* HeadImage = FigureShared->HeadImages.Find(HeadTable[0]))
		{
			FigureHeadTexture = FSimCopterPopulationSprite::CreateTextureFromImage(this, *HeadImage, TEXT("SimCopterPlayerHead"));
			if (FigureHeadTexture != nullptr)
			{
				FigureHeadMaterialInstance = UMaterialInstanceDynamic::Create(FigureHeadMaterial, this);
				if (FigureHeadMaterialInstance != nullptr)
				{
					FigureHeadMaterialInstance->SetTextureParameterValue(TEXT("Texture"), FigureHeadTexture);
					// The head is a ball with real normals, not a tree's crossed vertical quads, so
					// the material's world-up normal bias has to be off. Same reason as the NPCs'.
					FigureHeadMaterialInstance->SetScalarParameterValue(TEXT("CardNormalUpBias"), 0.0f);
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
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (Capsule != nullptr && ResolveGroundedLocation(GetActorLocation(), Capsule->GetScaledCapsuleHalfHeight(), GroundedLocation))
	{
		SetActorLocation(GroundedLocation, false);
	}
}

void ASimCopterOnFootPawn::FindOrSpawnParkedHelicopter()
{
	ParkedHelicopter = FindNearestHelicopter(ParkedHelicopterSearchRadiusCm);
	if (ParkedHelicopter != nullptr || GetWorld() == nullptr || HelicopterClass == nullptr || GetCapsuleComponent() == nullptr)
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

ASimCopterHelicopterPawn* ASimCopterOnFootPawn::FindHelicopterWithinReach(const float ReachCm) const
{
	if (GetWorld() == nullptr)
	{
		return nullptr;
	}

	TArray<AActor*> Helicopters;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ASimCopterHelicopterPawn::StaticClass(), Helicopters);

	// The avatar's own body counts: the reach starts at its skin, not at its centre line.
	const float BodyRadiusCm =
		GetCapsuleComponent() != nullptr ? GetCapsuleComponent()->GetScaledCapsuleRadius() : 0.0f;
	const float LimitCm = FMath::Max(0.0f, ReachCm);

	ASimCopterHelicopterPawn* BestHelicopter = nullptr;
	float BestGapCm = TNumericLimits<float>::Max();
	for (AActor* Actor : Helicopters)
	{
		ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(Actor);
		if (Helicopter == nullptr)
		{
			continue;
		}

		// Full 3D: a horizontal-only gap would board an aircraft hovering overhead the moment the
		// avatar walked underneath it.
		const float GapCm = FMath::Max(
			0.0f,
			Helicopter->GetDistanceToAirframeCm(GetActorLocation()) - BodyRadiusCm);
		if (GapCm <= LimitCm && GapCm < BestGapCm)
		{
			BestGapCm = GapCm;
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

	// DOWNWARD ONLY, from just above where the caller wanted us. Starting 2000 cm up meant the first
	// blocking hit on the way down could be the ROOF of whatever we were standing next to, which put
	// the avatar high in the air the moment they stepped out of the helicopter beside a building.
	// The caller has already picked the height it wants; this only ever settles us onto that surface
	// or a lower one.
	const FVector Start = DesiredLocation + FVector::UpVector * GroundProbeLiftCm;
	const FVector End = DesiredLocation - FVector::UpVector * GroundProbeDistanceCm;
	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterOnFootGroundSnap), false, this);
	// ECC_Camera is the channel walkable surfaces answer on in this project - the ground agents, the
	// traffic system and the helicopter's own exit probe all use it. ECC_Visibility additionally hits
	// things nobody stands on, which is how this landed the avatar in mid-air with nothing under him.
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Camera, QueryParams) && Hit.bBlockingHit)
	{
		OutLocation = FVector(DesiredLocation.X, DesiredLocation.Y, Hit.ImpactPoint.Z + ActorHalfHeight + 2.0f);
		return true;
	}

	return false;
}
