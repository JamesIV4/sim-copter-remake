// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/SimCopterHelicopterPawn.h"

#include "Camera/CameraComponent.h"
#include "City/SimCity2000CityActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Formats/MaxisMeshLibrary.h"
#include "Formats/MaxisProceduralMeshBuilder.h"
#include "Formats/MaxisTextureReader.h"
#include "Formats/MaxisWindowsBitmapReader.h"
#include "Formats/SimCopterTweakReader.h"
#include "Flight/SimCopterWaterGameplay.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Ground/SimCopterGroundAgent.h"
#include "Ground/SimCopterOnFootPawn.h"
#include "Ground/SimCopterParticleFX.h"
#include "Ground/SimCopterEffectFX.h"
#include "Ground/SimCopterPopulationSprite.h"
#include "Ground/SimCopterTrafficSystemActor.h"
#include "InputCoreTypes.h"
#include "Input/Reply.h"
#include "Missions/SimCopterMissionSystemActor.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterHelicopterPawn, Log, All);

namespace
{
constexpr float MaxSubstepSeconds = 1.0f / 60.0f;
constexpr float MaxTickSeconds = 0.1f;
constexpr int32 MaxTweakControls = 64;

bool ReadControlValue(const FSimCopterTweakSection& Section, const TCHAR* LabelPrefix, float& OutValue)
{
	for (int32 ControlIndex = 0; ControlIndex < MaxTweakControls; ++ControlIndex)
	{
		const FString LabelKey = FString::Printf(TEXT("Ctrl%d_Label"), ControlIndex);
		const FString ValueKey = FString::Printf(TEXT("Ctrl%d_Value"), ControlIndex);
		const FString Label = Section.GetString(LabelKey);
		if (Label.IsEmpty())
		{
			continue;
		}

		if (Label.StartsWith(LabelPrefix, ESearchCase::IgnoreCase))
		{
			return Section.TryGetFloat(ValueKey, OutValue);
		}
	}

	return false;
}

void ReadAngleControl(const FSimCopterTweakSection& Section, const TCHAR* LabelPrefix, float AngleScale, float& InOutDegrees)
{
	float RawValue = 0.0f;
	if (ReadControlValue(Section, LabelPrefix, RawValue))
	{
		InOutDegrees = RawValue * AngleScale;
	}
}

void ReadFloatControl(const FSimCopterTweakSection& Section, const TCHAR* LabelPrefix, float& InOutValue)
{
	float RawValue = 0.0f;
	if (ReadControlValue(Section, LabelPrefix, RawValue))
	{
		InOutValue = RawValue;
	}
}

}

ASimCopterHelicopterPawn::ASimCopterHelicopterPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;

	CollisionComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CollisionComponent"));
	CollisionComponent->InitCapsuleSize(95.0f, 82.0f);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionComponent->SetCollisionObjectType(ECC_Pawn);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	CollisionComponent->SetCanEverAffectNavigation(false);
	SetRootComponent(CollisionComponent);

	// Shared tilt pivot so both the placeholder and the original-mesh geometry bank together.
	ModelPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ModelPivot"));
	ModelPivot->SetupAttachment(CollisionComponent);

	BodyMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMeshComponent->SetupAttachment(ModelPivot);
	BodyMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMeshComponent->SetCanEverAffectNavigation(false);
	BodyMeshComponent->SetRelativeScale3D(FVector(2.1f, 0.62f, 0.38f));

	MainRotorMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MainRotor"));
	MainRotorMeshComponent->SetupAttachment(BodyMeshComponent);
	MainRotorMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MainRotorMeshComponent->SetCanEverAffectNavigation(false);
	MainRotorMeshComponent->SetRelativeLocation(FVector(5.0f, 0.0f, 74.0f));
	MainRotorMeshComponent->SetRelativeScale3D(FVector(5.2f, 0.045f, 0.018f));

	TailRotorMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TailRotor"));
	TailRotorMeshComponent->SetupAttachment(BodyMeshComponent);
	TailRotorMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TailRotorMeshComponent->SetCanEverAffectNavigation(false);
	TailRotorMeshComponent->SetRelativeLocation(FVector(-176.0f, 0.0f, 25.0f));
	TailRotorMeshComponent->SetRelativeScale3D(FVector(0.035f, 0.92f, 0.035f));

	// Original SimCopter fuselage + rotor meshes. Hidden until the GEO assets load; the
	// placeholder geometry above stays visible as a fallback when they are unavailable.
	HeliBodyMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("HeliBodyMesh"));
	HeliBodyMeshComponent->SetupAttachment(ModelPivot);
	HeliBodyMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeliBodyMeshComponent->SetCanEverAffectNavigation(false);
	HeliBodyMeshComponent->SetVisibility(false);

	HeliMainRotorMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("HeliMainRotor"));
	HeliMainRotorMeshComponent->SetupAttachment(HeliBodyMeshComponent);
	HeliMainRotorMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeliMainRotorMeshComponent->SetCanEverAffectNavigation(false);

	HeliTailRotorMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("HeliTailRotor"));
	HeliTailRotorMeshComponent->SetupAttachment(HeliBodyMeshComponent);
	HeliTailRotorMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeliTailRotorMeshComponent->SetCanEverAffectNavigation(false);

	RopeMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Rope"));
	RopeMeshComponent->SetupAttachment(CollisionComponent);
	RopeMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RopeMeshComponent->SetCanEverAffectNavigation(false);
	RopeMeshComponent->SetVisibility(false);

	for (int32 SegmentIndex = 0;
		SegmentIndex < SimCopterWaterGameplay::RopeNodeCount - 1;
		++SegmentIndex)
	{
		USplineMeshComponent* Segment = CreateDefaultSubobject<USplineMeshComponent>(
			FName(*FString::Printf(TEXT("RopeSegment%02d"), SegmentIndex)));
		Segment->SetupAttachment(CollisionComponent);
		Segment->SetMobility(EComponentMobility::Movable);
		Segment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Segment->SetCanEverAffectNavigation(false);
		Segment->SetCastShadow(false);
		Segment->SetForwardAxis(ESplineMeshAxis::Z, false);
		Segment->SetVisibility(false);
		Segment->SetHiddenInGame(true);
		RopeSegmentComponents.Add(Segment);
	}

	BucketMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Bucket"));
	BucketMeshComponent->SetupAttachment(CollisionComponent);
	BucketMeshComponent->SetMobility(EComponentMobility::Movable);
	BucketMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BucketMeshComponent->SetCanEverAffectNavigation(false);
	BucketMeshComponent->SetRelativeScale3D(FVector(0.30f, 0.34f, 0.36f));

	OriginalBucketMeshComponent =
		CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("OriginalBucket"));
	OriginalBucketMeshComponent->SetupAttachment(CollisionComponent);
	OriginalBucketMeshComponent->SetMobility(EComponentMobility::Movable);
	OriginalBucketMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OriginalBucketMeshComponent->SetCanEverAffectNavigation(false);
	OriginalBucketMeshComponent->SetCastShadow(true);
	OriginalBucketMeshComponent->SetVisibility(false);

	WaterFXComponent = CreateDefaultSubobject<USimCopterParticleFXComponent>(TEXT("WaterFX"));
	WaterFXComponent->SetupAttachment(CollisionComponent);

	SearchLightComponent = CreateDefaultSubobject<USpotLightComponent>(TEXT("SearchLight"));
	SearchLightComponent->SetupAttachment(ModelPivot);
	SearchLightComponent->SetRelativeLocation(FVector(95.0f, 0.0f, -35.0f));
	SearchLightComponent->SetRelativeRotation(FRotator(-35.0f, 0.0f, 0.0f));
	SearchLightComponent->Intensity = SearchLightIntensity;
	SearchLightComponent->AttenuationRadius = SearchLightRangeCm;
	SearchLightComponent->InnerConeAngle = 8.0f;
	SearchLightComponent->OuterConeAngle = 20.0f;
	SearchLightComponent->SetLightColor(SearchLightBeamColor.ToFColor(true));
	SearchLightComponent->SetVisibility(bSearchLightStartsEnabled);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(CollisionComponent);
	CameraBoom->TargetArmLength = 900.0f;
	CameraBoom->TargetOffset = FVector(0.0f, 0.0f, ChaseCameraTargetHeightCm);
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->ProbeChannel = ECC_Camera;
	CameraBoom->ProbeSize = 18.0f;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 9.5f;
	CameraBoom->bEnableCameraRotationLag = true;
	CameraBoom->CameraRotationLagSpeed = 8.0f;
	CameraBoom->SetRelativeRotation(FRotator(-16.0f, 0.0f, 0.0f));
	CurrentCameraArmLengthCm = CameraBoom->TargetArmLength;

	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	CameraComponent->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	CameraComponent->FieldOfView = 78.0f;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMeshFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CubeMeshFinder.Succeeded())
	{
		BodyMeshComponent->SetStaticMesh(CubeMeshFinder.Object);
		MainRotorMeshComponent->SetStaticMesh(CubeMeshFinder.Object);
		TailRotorMeshComponent->SetStaticMesh(CubeMeshFinder.Object);
		BucketMeshComponent->SetStaticMesh(CubeMeshFinder.Object);
	}
	if (CylinderMeshFinder.Succeeded())
	{
		RopeMeshComponent->SetStaticMesh(CylinderMeshFinder.Object);
		for (USplineMeshComponent* Segment : RopeSegmentComponents)
		{
			if (Segment != nullptr)
			{
				Segment->SetStaticMesh(CylinderMeshFinder.Object);
			}
		}
	}

	// Lit vertex-colour material shared with the city renderer so the palette-coloured
	// helicopter responds to scene lighting instead of rendering fullbright.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ModelMaterialFinder(TEXT("/Game/Materials/M_SimCopterLitVertexColor.M_SimCopterLitVertexColor"));
	if (ModelMaterialFinder.Succeeded())
	{
		ModelVertexColorMaterial = ModelMaterialFinder.Object;
		for (USplineMeshComponent* Segment : RopeSegmentComponents)
		{
			if (Segment != nullptr)
			{
				Segment->SetMaterial(0, ModelVertexColorMaterial);
			}
		}
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RotorDiscMaterialFinder(TEXT("/Game/Materials/M_SimCopterRotorDisc.M_SimCopterRotorDisc"));
	if (RotorDiscMaterialFinder.Succeeded())
	{
		RotorDiscMaterial = RotorDiscMaterialFinder.Object;
	}

	OriginalGameRoot.Path = TEXT("../Reference/SimCopterOriginalGame");
	ExitPawnClass = ASimCopterOnFootPawn::StaticClass();
	CurrentFuelGallons = HelicopterTuning.FuelGallons;
	ApplyDerivedTuning();
}

void ASimCopterHelicopterPawn::BeginPlay()
{
	Super::BeginPlay();

	if (bLoadTuningOnBeginPlay)
	{
		LoadTuningFromOriginalGameRoot();
	}
	else if (CurrentFuelGallons <= 0.0f)
	{
		CurrentFuelGallons = HelicopterTuning.FuelGallons;
	}

	if (bLoadHelicopterMeshOnBeginPlay)
	{
		LoadHelicopterMeshFromOriginalGameRoot();
	}
	if (WaterFXComponent != nullptr)
	{
		FString EffectError;
		if (!WaterFXComponent->InitEffectAssets(ResolveOriginalGameRoot(), EffectError))
		{
			UE_LOG(LogTemp, Warning, TEXT("SimCopter effect palette unavailable: %s"), *EffectError);
		}
	}

	// Keep the cursor free so the player can use on-screen buttons; mouse-look is only
	// engaged while a mouse button is held (see Start/StopCameraDrag). GameAndUI routes
	// mouse moves to the game only while the viewport is captured during a click-drag.
	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		PlayerController->bShowMouseCursor = true;
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		PlayerController->SetInputMode(InputMode);
		EnsurePassengerSlotsWidget();
		EnsureWaterControlsWidget();
	}

	UpdateGroundProbe();
	UpdateForwardProbe();
	UpdateRopeAndBucket(0.0f);
	if (SearchLightComponent != nullptr)
	{
		SearchLightComponent->SetVisibility(bSearchLightStartsEnabled);
	}
	UpdateSearchLightEffect();
	SeedFlightModelFromActor();
}

void ASimCopterHelicopterPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemovePassengerSlotsWidget();
	RemoveWaterControlsWidget();
	Super::EndPlay(EndPlayReason);
}

void ASimCopterHelicopterPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	float RemainingSeconds = FMath::Clamp(DeltaSeconds, 0.0f, MaxTickSeconds);
	while (RemainingSeconds > UE_SMALL_NUMBER)
	{
		const float StepSeconds = FMath::Min(RemainingSeconds, MaxSubstepSeconds);
		SimulateFlightStep(StepSeconds);
		RemainingSeconds -= StepSeconds;
	}

	UpdateVisuals(DeltaSeconds);
	UpdateRotorWash(DeltaSeconds);
	UpdateCamera(DeltaSeconds);
}

void ASimCopterHelicopterPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindAxis(TEXT("SimCopterPitch"), this, &ASimCopterHelicopterPawn::MovePitch);
	PlayerInputComponent->BindAxis(TEXT("SimCopterRoll"), this, &ASimCopterHelicopterPawn::MoveRoll);
	PlayerInputComponent->BindAxis(TEXT("SimCopterYaw"), this, &ASimCopterHelicopterPawn::MoveYaw);
	PlayerInputComponent->BindAxis(TEXT("SimCopterCollective"), this, &ASimCopterHelicopterPawn::MoveCollective);
	PlayerInputComponent->BindAxis(TEXT("SimCopterLookYaw"), this, &ASimCopterHelicopterPawn::LookYaw);
	PlayerInputComponent->BindAxis(TEXT("SimCopterLookPitch"), this, &ASimCopterHelicopterPawn::LookPitch);
	PlayerInputComponent->BindAxis(TEXT("SimCopterMouseLookYaw"), this, &ASimCopterHelicopterPawn::MouseLookYaw);
	PlayerInputComponent->BindAxis(TEXT("SimCopterMouseLookPitch"), this, &ASimCopterHelicopterPawn::MouseLookPitch);
	PlayerInputComponent->BindAxis(TEXT("SimCopterCameraZoom"), this, &ASimCopterHelicopterPawn::ZoomCamera);
	PlayerInputComponent->BindAxis(TEXT("SimCopterRopeAdjust"), this, &ASimCopterHelicopterPawn::AdjustRope);

	PlayerInputComponent->BindAction(TEXT("SimCopterToggleRope"), IE_Pressed, this, &ASimCopterHelicopterPawn::ToggleRope);
	PlayerInputComponent->BindAction(TEXT("SimCopterBucketDump"), IE_Pressed, this, &ASimCopterHelicopterPawn::StartBucketDump);
	PlayerInputComponent->BindAction(TEXT("SimCopterBucketDump"), IE_Released, this, &ASimCopterHelicopterPawn::StopBucketDump);
	PlayerInputComponent->BindAction(TEXT("SimCopterWaterCannon"), IE_Pressed, this, &ASimCopterHelicopterPawn::StartWaterCannon);
	PlayerInputComponent->BindAction(TEXT("SimCopterWaterCannon"), IE_Released, this, &ASimCopterHelicopterPawn::StopWaterCannon);
	PlayerInputComponent->BindAction(TEXT("SimCopterEngineStart"), IE_Pressed, this, &ASimCopterHelicopterPawn::StartEngineHold);
	PlayerInputComponent->BindAction(TEXT("SimCopterEngineStart"), IE_Released, this, &ASimCopterHelicopterPawn::StopEngineHold);
	PlayerInputComponent->BindAction(TEXT("SimCopterEngineShutdown"), IE_Pressed, this, &ASimCopterHelicopterPawn::StartEngineShutdownHold);
	PlayerInputComponent->BindAction(TEXT("SimCopterEngineShutdown"), IE_Released, this, &ASimCopterHelicopterPawn::StopEngineShutdownHold);
	PlayerInputComponent->BindAction(TEXT("SimCopterInteract"), IE_Pressed, this, &ASimCopterHelicopterPawn::Interact);
	PlayerInputComponent->BindAction(TEXT("SimCopterCameraDrag"), IE_Pressed, this, &ASimCopterHelicopterPawn::StartCameraDrag);
	PlayerInputComponent->BindAction(TEXT("SimCopterCameraDrag"), IE_Released, this, &ASimCopterHelicopterPawn::StopCameraDrag);
	PlayerInputComponent->BindAction(TEXT("SimCopterCycleCamera"), IE_Pressed, this, &ASimCopterHelicopterPawn::CycleCameraMode);
	PlayerInputComponent->BindAction(TEXT("SimCopterSearchLight"), IE_Pressed, this, &ASimCopterHelicopterPawn::ToggleSearchLight);
	PlayerInputComponent->BindAction(TEXT("SimCopterResetAircraft"), IE_Pressed, this, &ASimCopterHelicopterPawn::ResetAircraft);

	// Megaphone: talk to the cars/people below (used to clear traffic jams). Bound directly to M so
	// no input-mapping config edit is required; the on-screen prompt shows the same key.
	PlayerInputComponent->BindKey(EKeys::M, IE_Pressed, this, &ASimCopterHelicopterPawn::UseMegaphone);
}

bool ASimCopterHelicopterPawn::LoadTuningFromOriginalGameRoot()
{
	LastTuningLoadError.Reset();

	const FString RootPath = ResolveOriginalGameRoot();
	const FString HeliTweakPath = FPaths::Combine(RootPath, TEXT("tweak/heli.twk"));
	FSimCopterTweakFile TweakFile;
	FString Error;
	if (!FSimCopterTweakReader::LoadTweakFileFromFile(HeliTweakPath, TweakFile, Error))
	{
		LastTuningLoadError = Error;
		UE_LOG(LogSimCopterHelicopterPawn, Warning, TEXT("%s"), *LastTuningLoadError);
		return false;
	}

	const FSimCopterTweakSection* HeliSection = TweakFile.FindSection(HelicopterTypeName);
	if (HeliSection == nullptr)
	{
		LastTuningLoadError = FString::Printf(TEXT("Could not find helicopter tuning section '%s' in '%s'."), *HelicopterTypeName, *HeliTweakPath);
		UE_LOG(LogSimCopterHelicopterPawn, Warning, TEXT("%s"), *LastTuningLoadError);
		return false;
	}

	ReadAngleControl(*HeliSection, TEXT("MaxBank"), TweakAngleScale, HelicopterTuning.MaxBankDeg);
	ReadAngleControl(*HeliSection, TEXT("MaxSlide"), TweakAngleScale, HelicopterTuning.MaxSlideDeg);
	ReadAngleControl(*HeliSection, TEXT("MaxPitch"), TweakAngleScale, HelicopterTuning.MaxPitchDeg);
	ReadAngleControl(*HeliSection, TEXT("PitchRate"), TweakAngleScale, HelicopterTuning.PitchRateDegPerSec);
	ReadFloatControl(*HeliSection, TEXT("YawRate"), HelicopterTuning.YawAccelDegPerSec);
	ReadAngleControl(*HeliSection, TEXT("RollRate"), TweakAngleScale, HelicopterTuning.RollRateDegPerSec);
	ReadFloatControl(*HeliSection, TEXT("SlideRate"), HelicopterTuning.SlideResponse);
	float RawClimbRate = HelicopterTuning.ClimbRateCmPerSec / TweakClimbToCmPerSec;
	if (ReadControlValue(*HeliSection, TEXT("ClimbRate"), RawClimbRate))
	{
		HelicopterTuning.ClimbRateCmPerSec = RawClimbRate * TweakClimbToCmPerSec;
	}
	float FloatMaxLoad = static_cast<float>(HelicopterTuning.MaxLoadPounds);
	if (ReadControlValue(*HeliSection, TEXT("Max Load"), FloatMaxLoad))
	{
		HelicopterTuning.MaxLoadPounds = FMath::RoundToInt(FloatMaxLoad);
	}
	ReadFloatControl(*HeliSection, TEXT("Max YawRate"), HelicopterTuning.MaxYawRateDegPerSec);
	ReadFloatControl(*HeliSection, TEXT("Fuel Rate"), HelicopterTuning.FuelRateGallonsPerHour);
	float FloatNewCost = static_cast<float>(HelicopterTuning.NewCostDollars);
	if (ReadControlValue(*HeliSection, TEXT("New Cost"), FloatNewCost))
	{
		HelicopterTuning.NewCostDollars = FMath::RoundToInt(FloatNewCost);
	}
	float FloatMaxDamage = static_cast<float>(HelicopterTuning.MaxDamage);
	if (ReadControlValue(*HeliSection, TEXT("Max Damage"), FloatMaxDamage))
	{
		HelicopterTuning.MaxDamage = FMath::RoundToInt(FloatMaxDamage);
	}
	ReadFloatControl(*HeliSection, TEXT("Fuel ("), HelicopterTuning.FuelGallons);
	ReadFloatControl(*HeliSection, TEXT("Repair Rate"), HelicopterTuning.RepairRatePerDamage);
	ReadFloatControl(*HeliSection, TEXT("Fuel Cost"), HelicopterTuning.FuelCostPerGallon);

	if (const FSimCopterTweakSection* LandingSection = TweakFile.FindSection(TEXT("Heli Landing")))
	{
		ReadAngleControl(*LandingSection, TEXT("Pitch"), TweakAngleScale, LandingTuning.MaxPitchDeg);
		ReadAngleControl(*LandingSection, TEXT("Slide"), TweakAngleScale, LandingTuning.MaxRollDeg);
		float RawLandingSpeed = LandingTuning.MaxHorizontalSpeedCmPerSec / TweakSpeedToCmPerSec;
		if (ReadControlValue(*LandingSection, TEXT("Speed"), RawLandingSpeed))
		{
			LandingTuning.MaxHorizontalSpeedCmPerSec = RawLandingSpeed * TweakSpeedToCmPerSec;
		}
		float RawVerticalSpeed = LandingTuning.MaxVerticalSpeedCmPerSec / TweakSpeedToCmPerSec;
		if (ReadControlValue(*LandingSection, TEXT("Y Speed"), RawVerticalSpeed))
		{
			LandingTuning.MaxVerticalSpeedCmPerSec = RawVerticalSpeed * TweakSpeedToCmPerSec;
		}
		float RawDescentRate = LandingTuning.MaxDescentRateCmPerSec / TweakSpeedToCmPerSec;
		if (ReadControlValue(*LandingSection, TEXT("Max Descent Rate"), RawDescentRate))
		{
			LandingTuning.MaxDescentRateCmPerSec = RawDescentRate * TweakSpeedToCmPerSec;
		}
	}

	if (const FSimCopterTweakSection* RopeSection = TweakFile.FindSection(TEXT("Heli Ropestuff")))
	{
		ReadFloatControl(*RopeSection, TEXT("Bucket Fill Rate"), RopeTuning.BucketFillPoundsPerFrame);
		ReadFloatControl(*RopeSection, TEXT("Bucket Dump Rate"), RopeTuning.BucketDumpPoundsPerFrame);
		ReadFloatControl(*RopeSection, TEXT("Rope Load Factor"), RopeTuning.RopeLoadFactor);
		ReadFloatControl(*RopeSection, TEXT("Rope Tension"), RopeTuning.RopeTension);
		ReadFloatControl(*RopeSection, TEXT("Water Throw"), RopeTuning.WaterThrow);
		ReadFloatControl(*RopeSection, TEXT("Cannon Force"), RopeTuning.CannonForce);
	}

	if (const FSimCopterTweakSection* DamageSection = TweakFile.FindSection(TEXT("Heli Damage")))
	{
		float RawMinFireAltitude = DamageTuning.MinFireAltitudeCm / TweakAltitudeToCm;
		if (ReadControlValue(*DamageSection, TEXT("Min Fire Alt"), RawMinFireAltitude))
		{
			DamageTuning.MinFireAltitudeCm = RawMinFireAltitude * TweakAltitudeToCm;
		}
		float RawMaxFireAltitude = DamageTuning.MaxFireAltitudeCm / TweakAltitudeToCm;
		if (ReadControlValue(*DamageSection, TEXT("Max Fire Alt"), RawMaxFireAltitude))
		{
			DamageTuning.MaxFireAltitudeCm = RawMaxFireAltitude * TweakAltitudeToCm;
		}
		ReadFloatControl(*DamageSection, TEXT("Depreciate"), DamageTuning.DepreciateDollarsPerSec);
		ReadFloatControl(*DamageSection, TEXT("Collision Subtract Val"), DamageTuning.CollisionDamageScale);
		ReadFloatControl(*DamageSection, TEXT("Repair Dist. Val"), DamageTuning.RepairDistanceValue);
		ReadFloatControl(*DamageSection, TEXT("Fuel Dist. Val"), DamageTuning.FuelDistanceValue);
	}

	ApplyDerivedTuning();
	CurrentFuelGallons = HelicopterTuning.FuelGallons;

	UE_LOG(LogSimCopterHelicopterPawn, Display, TEXT("Loaded SimCopter helicopter tuning '%s' from '%s'."), *HelicopterTypeName, *HeliTweakPath);
	return true;
}

bool ASimCopterHelicopterPawn::GetHelicopterMeshNames(const FString& TypeName, FString& OutBodyName, FString& OutMainRotorName)
{
	// Maps the flyable helicopter type names from heli.twk to their GEO-pack table names.
	// Each helicopter is a fuselage object plus a separate main-rotor object that the
	// original engine spins about the mast; the meshes carry no skeletal animation.
	struct FHelicopterMeshNames
	{
		const TCHAR* TypeName;
		const TCHAR* BodyName;
		const TCHAR* MainRotorName;
	};

	static const FHelicopterMeshNames Table[] = {
		{ TEXT("Jet Ranger"), TEXT("JETRANG"), TEXT("JETRROTR") },
		{ TEXT("Hughes 500"), TEXT("HUGH500"), TEXT("H500ROTR") },
		{ TEXT("Bell 212"), TEXT("BELL212"), TEXT("BELLROTR") },
		{ TEXT("Schwiezer 300"), TEXT("SCWZR300"), TEXT("SCWZROTR") },
		{ TEXT("Apache"), TEXT("APACHE"), TEXT("APACROTR") },
		{ TEXT("Agusta"), TEXT("AGUSTA"), TEXT("AGUSROTR") },
		{ TEXT("Dauphin"), TEXT("DAUPHIN"), TEXT("DAUPROTR") },
		{ TEXT("MDEXPLORER"), TEXT("MDEXPLRR"), TEXT("MDEXROTR") },
		{ TEXT("MD520"), TEXT("MD520"), TEXT("MD52ROTR") },
	};

	const FString Trimmed = TypeName.TrimStartAndEnd();
	for (const FHelicopterMeshNames& Entry : Table)
	{
		if (Trimmed.Equals(Entry.TypeName, ESearchCase::IgnoreCase))
		{
			OutBodyName = Entry.BodyName;
			OutMainRotorName = Entry.MainRotorName;
			return true;
		}
	}

	return false;
}

void ASimCopterHelicopterPawn::ShowOriginalMesh(bool bUseOriginalMesh)
{
	bUsingOriginalMesh = bUseOriginalMesh;

	if (HeliBodyMeshComponent != nullptr)
	{
		HeliBodyMeshComponent->SetVisibility(bUseOriginalMesh, true);
	}
	if (BodyMeshComponent != nullptr)
	{
		BodyMeshComponent->SetVisibility(!bUseOriginalMesh, true);
	}
}

bool ASimCopterHelicopterPawn::LoadHelicopterMeshFromOriginalGameRoot()
{
	LastModelLoadError.Reset();
	bUsingOriginalBucketMesh = false;
	if (OriginalBucketMeshComponent != nullptr)
	{
		OriginalBucketMeshComponent->ClearAllMeshSections();
		OriginalBucketMeshComponent->SetVisibility(false);
	}

	FString BodyName;
	FString MainRotorName;
	if (!GetHelicopterMeshNames(HelicopterTypeName, BodyName, MainRotorName))
	{
		LastModelLoadError = FString::Printf(TEXT("'%s' is not a known flyable helicopter type; keeping placeholder model."), *HelicopterTypeName);
		UE_LOG(LogSimCopterHelicopterPawn, Warning, TEXT("%s"), *LastModelLoadError);
		ShowOriginalMesh(false);
		return false;
	}

	const FString RootPath = ResolveOriginalGameRoot();
	FMaxisMeshLibrary MeshLibrary;
	FString Error;
	if (!MeshLibrary.LoadFromOriginalGameRoot(RootPath, Error))
	{
		LastModelLoadError = Error;
		UE_LOG(LogSimCopterHelicopterPawn, Warning, TEXT("%s"), *LastModelLoadError);
		ShowOriginalMesh(false);
		return false;
	}

	const FLinearColor FallbackColor(0.6f, 0.6f, 0.62f);

	auto BuildComponentMesh = [this, &FallbackColor](UProceduralMeshComponent* Component, const FMaxisMeshObject& Object, const TArray<FColor>* ColorMap, FMaxisMeshSection& OutSection)
	{
		FMaxisProceduralMeshBuilder::BuildPaletteColoredSection(Object, ColorMap, ModelUnitsPerCentimeter, ModelScale, bRenderModelBackfaces, FallbackColor, OutSection);
		Component->ClearAllMeshSections();
		if (OutSection.IsEmpty())
		{
			return false;
		}

		Component->CreateMeshSection_LinearColor(0, OutSection.Vertices, OutSection.Triangles, OutSection.Normals, OutSection.UVs, OutSection.VertexColors, OutSection.Tangents, false);
		if (ModelVertexColorMaterial != nullptr)
		{
			Component->SetMaterial(0, ModelVertexColorMaterial);
		}
		return true;
	};

	// Rotors split into an opaque blade section and a translucent disc section (Maxis face
	// type 11). The original engine keeps those faces hidden until the rotor reaches lift
	// speed (RPM 300), so the disc section index is recorded for the visibility toggle.
	auto BuildRotorMesh = [this, &FallbackColor](UProceduralMeshComponent* Component, const FMaxisMeshObject& Object, const TArray<FColor>* ColorMap, int32& OutDiscSectionIndex)
	{
		OutDiscSectionIndex = INDEX_NONE;
		FMaxisMeshSection OpaqueSection;
		FMaxisMeshSection DiscSection;
		FMaxisProceduralMeshBuilder::BuildPaletteColoredSections(Object, ColorMap, ModelUnitsPerCentimeter, ModelScale, bRenderModelBackfaces, FallbackColor, OpaqueSection, &DiscSection);
		Component->ClearAllMeshSections();
		if (OpaqueSection.IsEmpty() && DiscSection.IsEmpty())
		{
			return false;
		}

		int32 SectionIndex = 0;
		if (!OpaqueSection.IsEmpty())
		{
			Component->CreateMeshSection_LinearColor(SectionIndex, OpaqueSection.Vertices, OpaqueSection.Triangles, OpaqueSection.Normals, OpaqueSection.UVs, OpaqueSection.VertexColors, OpaqueSection.Tangents, false);
			if (ModelVertexColorMaterial != nullptr)
			{
				Component->SetMaterial(SectionIndex, ModelVertexColorMaterial);
			}
			++SectionIndex;
		}
		if (!DiscSection.IsEmpty())
		{
			for (FLinearColor& VertexColor : DiscSection.VertexColors)
			{
				VertexColor.A = FMath::Clamp(VertexColor.A * RotorDiscAlphaScale, 0.0f, 1.0f);
			}
			Component->CreateMeshSection_LinearColor(SectionIndex, DiscSection.Vertices, DiscSection.Triangles, DiscSection.Normals, DiscSection.UVs, DiscSection.VertexColors, DiscSection.Tangents, false);
			UMaterialInterface* const DiscMaterial = RotorDiscMaterial != nullptr ? RotorDiscMaterial.Get() : ModelVertexColorMaterial.Get();
			if (DiscMaterial != nullptr)
			{
				Component->SetMaterial(SectionIndex, DiscMaterial);
			}
			Component->SetMeshSectionVisible(SectionIndex, false);
			OutDiscSectionIndex = SectionIndex;
			++SectionIndex;
		}
		return true;
	};

	const TArray<FColor>* BodyColorMap = nullptr;
	const FMaxisMeshObject* BodyObject = MeshLibrary.FindObjectByTableName(BodyName, &BodyColorMap);
	FMaxisMeshSection BodySection;
	if (BodyObject == nullptr || !BuildComponentMesh(HeliBodyMeshComponent, *BodyObject, BodyColorMap, BodySection))
	{
		LastModelLoadError = FString::Printf(TEXT("Could not build helicopter body mesh '%s' from '%s'."), *BodyName, *RootPath);
		UE_LOG(LogSimCopterHelicopterPawn, Warning, TEXT("%s"), *LastModelLoadError);
		ShowOriginalMesh(false);
		return false;
	}

	// FUN_00483c20 binds object 0x7b to the rope-end render node when the bucket
	// attachment is selected. Load that authored BUCKET mesh instead of leaving the
	// engine cube in place.
	const TArray<FColor>* BucketColorMap = nullptr;
	const FMaxisMeshObject* BucketObject =
		MeshLibrary.FindObjectByObjectId(0x7b, &BucketColorMap);
	FMaxisMeshSection BucketSection;
	bUsingOriginalBucketMesh =
		BucketObject != nullptr &&
		BuildComponentMesh(
			OriginalBucketMeshComponent,
			*BucketObject,
			BucketColorMap,
			BucketSection);
	if (!bUsingOriginalBucketMesh)
	{
		UE_LOG(
			LogSimCopterHelicopterPawn,
			Warning,
			TEXT("Could not build original BUCKET mesh (GEO id 0x7b); using the fallback bucket."));
	}

	// Sit the lowest fuselage vertex at the bottom of the collision capsule so the skids
	// rest near the ground contact point the flight probes use.
	const float CapsuleHalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	const float VerticalOffset = -CapsuleHalfHeight - BodySection.LocalBounds.Min.Z;
	HeliBodyMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, VerticalOffset));

	// Main rotor: authored around the mast at local X=Y=0, so spinning the component about
	// its own Z axis at the body origin sweeps the blades correctly.
	const TArray<FColor>* MainRotorColorMap = nullptr;
	const FMaxisMeshObject* MainRotorObject = MeshLibrary.FindObjectByTableName(MainRotorName, &MainRotorColorMap);
	MainRotorDiscSectionIndex = INDEX_NONE;
	if (MainRotorObject != nullptr && BuildRotorMesh(HeliMainRotorMeshComponent, *MainRotorObject, MainRotorColorMap, MainRotorDiscSectionIndex))
	{
		HeliMainRotorMeshComponent->SetRelativeLocation(FVector::ZeroVector);
	}

	// Tail rotor: the shared ROTORTL object is authored centred on its own hub, so place it
	// near the rear tip of the fuselage and spin it about the lateral (Y) axis.
	HeliTailRotorMeshComponent->ClearAllMeshSections();
	TailRotorDiscSectionIndex = INDEX_NONE;
	if (bShowSeparateTailRotor && !FlightModel.Tuning.bNoTailRotor)
	{
		const TArray<FColor>* TailRotorColorMap = nullptr;
		const FMaxisMeshObject* TailRotorObject = MeshLibrary.FindObjectByTableName(TEXT("ROTORTL"), &TailRotorColorMap);
		if (TailRotorObject != nullptr && BuildRotorMesh(HeliTailRotorMeshComponent, *TailRotorObject, TailRotorColorMap, TailRotorDiscSectionIndex))
		{
			const FVector BodyMin = BodySection.LocalBounds.Min;
			const FVector BodyMax = BodySection.LocalBounds.Max;
			const float TailX = BodyMin.X + (BodyMax.X - BodyMin.X) * 0.05f;
			const float TailZ = BodyMin.Z + (BodyMax.Z - BodyMin.Z) * 0.55f;
			HeliTailRotorMeshComponent->SetRelativeLocation(FVector(TailX, 0.0f, TailZ));
		}
	}

	ShowOriginalMesh(true);
	UE_LOG(
		LogSimCopterHelicopterPawn,
		Display,
		TEXT("Loaded SimCopter helicopter model '%s' (body '%s', rotor '%s', bucket=%s) from '%s'."),
		*HelicopterTypeName,
		*BodyName,
		*MainRotorName,
		bUsingOriginalBucketMesh ? TEXT("BUCKET/0x7b") : TEXT("fallback"),
		*RootPath);
	return true;
}

void ASimCopterHelicopterPawn::ResetAircraft()
{
	VelocityCmPerSec = FVector::ZeroVector;
	CurrentPitchDeg = 0.0f;
	CurrentRollDeg = 0.0f;
	bEngineRunning = false;
	bEngineStartHeld = false;
	bEngineShutdownHeld = false;
	EngineStartHoldElapsed = 0.0f;
	EngineShutdownHoldElapsed = 0.0f;
	EngineStartHoldAlpha = 0.0f;
	EngineShutdownHoldAlpha = 0.0f;
	CurrentDamage = 0.0f;
	CurrentFuelGallons = HelicopterTuning.FuelGallons;
	BucketWaterPounds = 0;
	BucketWaterFraction = 0.0f;
	RopeFirstActiveNode = SimCopterWaterGameplay::RopeStowedFirstActiveNode;
	bRopeDeployed = false;
	bRopeStateInitialized = false;
	bWaterCannonHeld = false;
	bIsLanded = false;
	SetActorRotation(FRotator(0.0f, GetActorRotation().Yaw, 0.0f));
	SeedFlightModelFromActor();
	UpdateGroundProbe();
}

float ASimCopterHelicopterPawn::GetFuelFraction() const
{
	return HelicopterTuning.FuelGallons > 0.0f ? FMath::Clamp(CurrentFuelGallons / HelicopterTuning.FuelGallons, 0.0f, 1.0f) : 0.0f;
}

float ASimCopterHelicopterPawn::GetDamageFraction() const
{
	return HelicopterTuning.MaxDamage > 0 ? FMath::Clamp(CurrentDamage / static_cast<float>(HelicopterTuning.MaxDamage), 0.0f, 1.0f) : 0.0f;
}

bool ASimCopterHelicopterPawn::CanBeEnteredBy(const FVector& WorldLocation, float RadiusCm) const
{
	return FVector::DistSquared(WorldLocation, GetActorLocation()) <= FMath::Square(RadiusCm);
}

void ASimCopterHelicopterPawn::EnterHelicopter(APlayerController* PlayerController)
{
	if (PlayerController == nullptr)
	{
		return;
	}

	PlayerController->Possess(this);
	PlayerController->bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PlayerController->SetInputMode(InputMode);
	EnsurePassengerSlotsWidget();
	EnsureWaterControlsWidget();
}

bool ASimCopterHelicopterPawn::CanExitHelicopter() const
{
	return bIsLanded && !bEngineRunning && GroundClearanceCm <= GroundContactTolerance + 18.0f;
}

bool ASimCopterHelicopterPawn::CanTransferMissionPassengers() const
{
	return bIsLanded && GroundClearanceCm <= GroundContactTolerance + 60.0f;
}

int32 ASimCopterHelicopterPawn::GetAvailablePassengerSeats() const
{
	return FMath::Max(0, FlightModel.Tuning.PassengerSeats - MissionPassengerSlots.Num());
}

int32 ASimCopterHelicopterPawn::AddMissionPassengers(int32 Count)
{
	return AddMissionPassengersForMission(Count, INDEX_NONE, ESimCopterMissionPassengerKind::Transport);
}

int32 ASimCopterHelicopterPawn::RemoveMissionPassengers(int32 Count)
{
	const int32 Removed = FMath::Clamp(Count, 0, MissionPassengerSlots.Num());
	if (Removed > 0)
	{
		MissionPassengerSlots.RemoveAt(FMath::Max(0, MissionPassengerSlots.Num() - Removed), Removed);
		SyncPassengerFlightModelCount();
		RefreshPassengerSlotsWidget();
	}
	return Removed;
}

int32 ASimCopterHelicopterPawn::AddMissionPassengersForMission(int32 Count, int32 EventId, ESimCopterMissionPassengerKind Kind)
{
	const int32 Added = FMath::Clamp(Count, 0, GetAvailablePassengerSeats());
	for (int32 Index = 0; Index < Added; ++Index)
	{
		FSimCopterMissionPassengerSlot Slot;
		Slot.EventId = EventId;
		Slot.Kind = Kind;
		MissionPassengerSlots.Add(Slot);
	}
	if (Added > 0)
	{
		SyncPassengerFlightModelCount();
		RefreshPassengerSlotsWidget();
	}
	return Added;
}

int32 ASimCopterHelicopterPawn::RemoveMissionPassengersForMission(int32 Count, int32 EventId, ESimCopterMissionPassengerKind Kind)
{
	int32 Removed = 0;
	for (int32 Index = MissionPassengerSlots.Num() - 1; Index >= 0 && Removed < Count; --Index)
	{
		const FSimCopterMissionPassengerSlot& Slot = MissionPassengerSlots[Index];
		if (Slot.EventId == EventId && Slot.Kind == Kind)
		{
			MissionPassengerSlots.RemoveAt(Index);
			Removed++;
		}
	}
	if (Removed > 0)
	{
		SyncPassengerFlightModelCount();
		RefreshPassengerSlotsWidget();
	}
	return Removed;
}

int32 ASimCopterHelicopterPawn::GetMissionPassengerCount(int32 EventId, ESimCopterMissionPassengerKind Kind) const
{
	int32 Count = 0;
	for (const FSimCopterMissionPassengerSlot& Slot : MissionPassengerSlots)
	{
		if (Slot.EventId == EventId && Slot.Kind == Kind)
		{
			Count++;
		}
	}
	return Count;
}

bool ASimCopterHelicopterPawn::DropPassengerAtSlot(int32 SlotIndex)
{
	if (!MissionPassengerSlots.IsValidIndex(SlotIndex) || GetWorld() == nullptr)
	{
		return false;
	}

	ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterTrafficSystemActor::StaticClass()));
	if (TrafficSystem == nullptr)
	{
		return false;
	}

	const FSimCopterMissionPassengerSlot Slot = MissionPassengerSlots[SlotIndex];
	const int32 SpawnMode = Slot.Kind == ESimCopterMissionPassengerKind::Medevac ? 6 : 0;
	ASimCopterGroundAgent* DroppedPassenger = TrafficSystem->SpawnFallingMissionPassengerAtWorldLocation(
		GetPassengerAirDropWorldLocation(SlotIndex),
		Slot.EventId,
		SpawnMode,
		-1,
		PassengerFallInjuryDistanceCm);
	if (DroppedPassenger == nullptr)
	{
		return false;
	}
	if (Slot.Kind == ESimCopterMissionPassengerKind::Transport)
	{
		DroppedPassenger->SetMissionPickupCreditAwarded(true);
	}

	MissionPassengerSlots.RemoveAt(SlotIndex);
	SyncPassengerFlightModelCount();
	RefreshPassengerSlotsWidget();

	if (ASimCopterMissionSystemActor* MissionActor = Cast<ASimCopterMissionSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass())))
	{
		MissionActor->NotifyPassengerDroppedFromHelicopter(Slot.EventId, Slot.Kind, 1);
	}
	return true;
}

FVector ASimCopterHelicopterPawn::GetPassengerDropWorldLocation(int32 SlotIndex) const
{
	const FRotationMatrix YawFrame(FRotator(0.0f, GetActorRotation().Yaw, 0.0f));
	const float SlotSide = (SlotIndex % 2 == 0) ? 1.0f : -1.0f;
	const float SlotRowOffset = SlotIndex >= 0 ? float(SlotIndex / 2) * 32.0f : 0.0f;
	FVector DropLocation =
		GetActorLocation() +
		YawFrame.GetUnitAxis(EAxis::Y) * (175.0f * SlotSide) -
		YawFrame.GetUnitAxis(EAxis::X) * (35.0f + SlotRowOffset);

	if (GetWorld() != nullptr)
	{
		const FVector TraceStart = DropLocation + FVector::UpVector * 900.0f;
		const FVector TraceEnd = DropLocation - FVector::UpVector * 1800.0f;
		FHitResult Hit;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterPassengerDrop), false, this);
		if (GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams) && Hit.bBlockingHit)
		{
			DropLocation.Z = Hit.ImpactPoint.Z + 24.0f;
		}
	}

	return DropLocation;
}

FVector ASimCopterHelicopterPawn::GetPassengerAirDropWorldLocation(int32 SlotIndex) const
{
	const int32 SlotCount = FMath::Max(1, MissionPassengerSlots.Num());
	const int32 FirstRightSlot = (SlotCount + 1) / 2;
	const bool bLeftSide = SlotIndex < FirstRightSlot;
	const int32 SideIndex = bLeftSide ? SlotIndex : SlotIndex - FirstRightSlot;
	const float SlotSide = bLeftSide ? -1.0f : 1.0f;
	const FRotationMatrix YawFrame(FRotator(0.0f, GetActorRotation().Yaw, 0.0f));

	return GetActorLocation() +
		YawFrame.GetUnitAxis(EAxis::Y) * (PassengerDropSideOffsetCm * SlotSide) -
		YawFrame.GetUnitAxis(EAxis::X) * (PassengerDropForwardOffsetCm + float(SideIndex) * 32.0f) -
		FVector::UpVector * PassengerDropVerticalOffsetCm;
}

void ASimCopterHelicopterPawn::SyncPassengerFlightModelCount()
{
	FlightModel.Passengers = MissionPassengerSlots.Num();
}

void ASimCopterHelicopterPawn::EnsurePassengerSlotsWidget()
{
	if (PassengerSlotsWidget.IsValid() || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	LoadPassengerSlotIconTexture();

	TSharedRef<SHorizontalBox> SlotBox = SNew(SHorizontalBox);
	PassengerSlotsBox = SlotBox;
	PassengerSlotsWidget =
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 24.0f))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.58f))
			.Padding(FMargin(7.0f, 5.0f))
			[
				SlotBox
			]
		];

	GEngine->GameViewport->AddViewportWidgetContent(PassengerSlotsWidget.ToSharedRef(), 25);
	RefreshPassengerSlotsWidget();
}

void ASimCopterHelicopterPawn::RemovePassengerSlotsWidget()
{
	if (GEngine != nullptr && GEngine->GameViewport != nullptr && PassengerSlotsWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(PassengerSlotsWidget.ToSharedRef());
	}

	PassengerSlotsBox.Reset();
	PassengerSlotsWidget.Reset();
}

void ASimCopterHelicopterPawn::EnsureWaterControlsWidget()
{
	if (WaterControlsWidget.IsValid() || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	TSharedRef<STextBlock> ControlsText =
		SNew(STextBlock)
		.Justification(ETextJustify::Left)
		.ColorAndOpacity(FLinearColor(0.91f, 0.96f, 1.0f, 0.96f))
		.ShadowOffset(FVector2D(1.0f, 1.0f))
		.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f))
		.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14));
	WaterControlsText = ControlsText;
	WaterControlsWidget =
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(0.0f, 0.0f, 22.0f, 22.0f))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(FLinearColor(0.015f, 0.035f, 0.055f, 0.76f))
			.Padding(FMargin(10.0f, 7.0f))
			[
				ControlsText
			]
		];

	GEngine->GameViewport->AddViewportWidgetContent(WaterControlsWidget.ToSharedRef(), 24);
	RefreshWaterControlsWidget();
}

void ASimCopterHelicopterPawn::RemoveWaterControlsWidget()
{
	if (GEngine != nullptr && GEngine->GameViewport != nullptr && WaterControlsWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(WaterControlsWidget.ToSharedRef());
	}

	WaterControlsText.Reset();
	WaterControlsWidget.Reset();
}

void ASimCopterHelicopterPawn::RefreshWaterControlsWidget()
{
	if (!WaterControlsText.IsValid())
	{
		return;
	}

	const int32 CapacityPounds = FMath::Max(0, HelicopterTuning.MaxLoadPounds);
	FString BucketState;
	if (!bRopeDeployed)
	{
		BucketState = TEXT("STOWED");
	}
	else if (BucketWaterPounds <= 0)
	{
		BucketState = FString::Printf(
			TEXT("EMPTY  |  ROPE %.1f m  |  lower into water to fill"),
			RopeLengthCm / 100.0f);
	}
	else
	{
		BucketState = FString::Printf(
			TEXT("%d / %d lb  |  ROPE %.1f m"),
			BucketWaterPounds,
			CapacityPounds,
			RopeLengthCm / 100.0f);
	}

	WaterControlsText->SetText(FText::FromString(FString::Printf(
		TEXT("WATER BUCKET  %s\n[R] deploy/stow   [PAGE UP] raise   [PAGE DOWN] lower   [G] dump"),
		*BucketState)));
}

void ASimCopterHelicopterPawn::RefreshPassengerSlotsWidget()
{
	if (!PassengerSlotsBox.IsValid())
	{
		return;
	}

	PassengerSlotsBox->ClearChildren();
	const int32 SeatCount = FMath::Max(0, GetPassengerSeatCount());
	for (int32 SlotIndex = 0; SlotIndex < SeatCount; ++SlotIndex)
	{
		const bool bFull = MissionPassengerSlots.IsValidIndex(SlotIndex);
		TSharedRef<SWidget> SlotContent =
			SNew(STextBlock)
			.Text(FText::FromString(bFull ? TEXT("●") : TEXT("○")))
			.Justification(ETextJustify::Center)
			.ColorAndOpacity(bFull ? FLinearColor(0.95f, 0.9f, 0.58f, 1.0f) : FLinearColor(0.9f, 0.95f, 1.0f, 0.92f))
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 24));

		if (bFull && PassengerSlotIconBrush.IsValid())
		{
			SlotContent =
				SNew(SBox)
				.WidthOverride(24.0f)
				.HeightOverride(30.0f)
				[
					SNew(SImage)
					.Image(PassengerSlotIconBrush.Get())
				];
		}

		PassengerSlotsBox->AddSlot()
		.AutoWidth()
		.Padding(FMargin(3.0f, 0.0f))
		[
			SNew(SBox)
			.WidthOverride(38.0f)
			.HeightOverride(38.0f)
			[
				SNew(SButton)
				.ContentPadding(FMargin(1.0f))
				.ToolTipText(FText::FromString(bFull ? TEXT("Drop passenger") : TEXT("Empty seat")))
				.ButtonColorAndOpacity(bFull ? FLinearColor(0.09f, 0.12f, 0.14f, 0.95f) : FLinearColor(0.04f, 0.06f, 0.08f, 0.78f))
				.OnClicked(FOnClicked::CreateUObject(this, &ASimCopterHelicopterPawn::HandlePassengerSlotClicked, SlotIndex))
				[
					SlotContent
				]
			]
		];
	}
}

bool ASimCopterHelicopterPawn::LoadPassengerSlotIconTexture()
{
	if (PassengerSlotIconTexture != nullptr && PassengerSlotIconBrush.IsValid())
	{
		return true;
	}

	const FString PeoplePath = FSimCopterPopulationSprite::ResolvePeople1BitmapPath(ResolveOriginalGameRoot());
	if (PeoplePath.IsEmpty())
	{
		return false;
	}

	FMaxisTextureImage SheetImage;
	FString Error;
	if (!FMaxisWindowsBitmapReader::LoadPalettedBitmapFromFile(
			PeoplePath,
			SheetImage,
			Error,
			FSimCopterPopulationSprite::People1TransparentPaletteIndex))
	{
		UE_LOG(LogSimCopterHelicopterPawn, Warning, TEXT("Could not load passenger slot icon from PEOPLE1.BMP: %s"), *Error);
		return false;
	}

	constexpr int32 SourceColumn = 1;
	constexpr int32 SourceRow = 0;
	constexpr int32 IconWidth = FSimCopterPopulationSprite::People1FrameWidth;
	constexpr int32 IconHeight = FSimCopterPopulationSprite::People1FrameHeight;
	const int32 SourceX = SourceColumn * IconWidth;
	const int32 SourceY = SourceRow * IconHeight;
	if (SheetImage.Width < SourceX + IconWidth || SheetImage.Height < SourceY + IconHeight)
	{
		return false;
	}

	FMaxisTextureImage IconImage;
	IconImage.Width = IconWidth;
	IconImage.Height = IconHeight;
	IconImage.Pixels.SetNumUninitialized(IconWidth * IconHeight);
	for (int32 Y = 0; Y < IconHeight; ++Y)
	{
		for (int32 X = 0; X < IconWidth; ++X)
		{
			IconImage.Pixels[Y * IconWidth + X] = SheetImage.Pixels[(SourceY + Y) * SheetImage.Width + SourceX + X];
		}
	}

	PassengerSlotIconTexture = FSimCopterPopulationSprite::CreateTextureFromImage(this, IconImage, TEXT("SimCopterPassengerSlotIcon"));
	if (PassengerSlotIconTexture == nullptr)
	{
		return false;
	}

	PassengerSlotIconBrush = MakeShared<FSlateBrush>();
	PassengerSlotIconBrush->SetResourceObject(PassengerSlotIconTexture);
	PassengerSlotIconBrush->ImageSize = FVector2D(24.0f, 30.0f);
	PassengerSlotIconBrush->DrawAs = ESlateBrushDrawType::Image;
	return true;
}

FReply ASimCopterHelicopterPawn::HandlePassengerSlotClicked(int32 SlotIndex)
{
	DropPassengerAtSlot(SlotIndex);
	return FReply::Handled();
}

void ASimCopterHelicopterPawn::ExitHelicopter()
{
	if (!CanExitHelicopter() || GetWorld() == nullptr || ExitPawnClass == nullptr)
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (PlayerController == nullptr)
	{
		return;
	}
	RemovePassengerSlotsWidget();
	RemoveWaterControlsWidget();

	const FRotationMatrix YawFrame(FRotator(0.0f, GetActorRotation().Yaw, 0.0f));
	FVector ExitLocation =
		GetActorLocation() +
		YawFrame.GetUnitAxis(EAxis::X) * ExitOffset.X +
		YawFrame.GetUnitAxis(EAxis::Y) * ExitOffset.Y;

	const FVector TraceStart = ExitLocation + FVector::UpVector * 1200.0f;
	const FVector TraceEnd = ExitLocation - FVector::UpVector * 2200.0f;
	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterHelicopterExit), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams) && Hit.bBlockingHit)
	{
		ExitLocation.Z = Hit.ImpactPoint.Z + 94.0f;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ASimCopterOnFootPawn* OnFootPawn = GetWorld()->SpawnActor<ASimCopterOnFootPawn>(
		ExitPawnClass,
		ExitLocation,
		FRotator(0.0f, GetActorRotation().Yaw, 0.0f),
		SpawnParams);
	if (OnFootPawn != nullptr)
	{
		PlayerController->Possess(OnFootPawn);
	}
}

void ASimCopterHelicopterPawn::MovePitch(float Value)
{
	PitchInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void ASimCopterHelicopterPawn::MoveRoll(float Value)
{
	RollInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void ASimCopterHelicopterPawn::MoveYaw(float Value)
{
	YawInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void ASimCopterHelicopterPawn::MoveCollective(float Value)
{
	CollectiveInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void ASimCopterHelicopterPawn::LookYaw(float Value)
{
	CameraYawInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void ASimCopterHelicopterPawn::LookPitch(float Value)
{
	CameraPitchInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void ASimCopterHelicopterPawn::MouseLookYaw(float Value)
{
	MouseLookYawInput = Value;
}

void ASimCopterHelicopterPawn::MouseLookPitch(float Value)
{
	MouseLookPitchInput = Value;
}

void ASimCopterHelicopterPawn::StartCameraDrag()
{
	++CameraDragButtonCount;
	bCameraDragActive = true;
}

void ASimCopterHelicopterPawn::StopCameraDrag()
{
	CameraDragButtonCount = FMath::Max(0, CameraDragButtonCount - 1);
	bCameraDragActive = CameraDragButtonCount > 0;
}

void ASimCopterHelicopterPawn::ZoomCamera(float Value)
{
	if (!FMath::IsNearlyZero(Value))
	{
		CameraZoomAlpha = FMath::Clamp(CameraZoomAlpha - Value * 0.08f, 0.0f, 1.0f);
	}
}

void ASimCopterHelicopterPawn::AdjustRope(float Value)
{
	RopeAdjustInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void ASimCopterHelicopterPawn::ToggleRope()
{
	RopeFirstActiveNode = bRopeDeployed
		? SimCopterWaterGameplay::RopeStowedFirstActiveNode
		: SimCopterWaterGameplay::RopeMinimumFirstActiveNode;
	bRopeDeployed = RopeFirstActiveNode < SimCopterWaterGameplay::RopeStowedFirstActiveNode;
	RefreshWaterControlsWidget();
}

void ASimCopterHelicopterPawn::StartBucketDump()
{
	bBucketDumpHeld = true;
}

void ASimCopterHelicopterPawn::StopBucketDump()
{
	bBucketDumpHeld = false;
}

void ASimCopterHelicopterPawn::StartWaterCannon()
{
	bWaterCannonHeld = true;
}

void ASimCopterHelicopterPawn::StopWaterCannon()
{
	bWaterCannonHeld = false;
}

void ASimCopterHelicopterPawn::StartEngineHold()
{
	bEngineStartHeld = true;
}

void ASimCopterHelicopterPawn::StopEngineHold()
{
	bEngineStartHeld = false;
	EngineStartHoldElapsed = 0.0f;
	EngineStartHoldAlpha = 0.0f;
}

void ASimCopterHelicopterPawn::StartEngineShutdownHold()
{
	bEngineShutdownHeld = true;
}

void ASimCopterHelicopterPawn::StopEngineShutdownHold()
{
	bEngineShutdownHeld = false;
	EngineShutdownHoldElapsed = 0.0f;
	EngineShutdownHoldAlpha = 0.0f;
}

void ASimCopterHelicopterPawn::Interact()
{
	if (CanExitHelicopter())
	{
		ExitHelicopter();
	}
}

void ASimCopterHelicopterPawn::UseMegaphone()
{
	if (GetWorld() == nullptr)
	{
		return;
	}
	if (ASimCopterMissionSystemActor* MissionActor = Cast<ASimCopterMissionSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass())))
	{
		MissionActor->TryUseMegaphone(GetActorLocation());
	}
}

void ASimCopterHelicopterPawn::CycleCameraMode()
{
	switch (CameraMode)
	{
	case ESimCopterCameraMode::Chase:
		CameraMode = ESimCopterCameraMode::Orbit;
		break;
	case ESimCopterCameraMode::Orbit:
		CameraMode = ESimCopterCameraMode::Rescue;
		break;
	default:
		CameraMode = ESimCopterCameraMode::Chase;
		CameraYawOffsetDeg = 0.0f;
		CameraPitchOffsetDeg = 0.0f;
		break;
	}
}

void ASimCopterHelicopterPawn::ToggleSearchLight()
{
	if (SearchLightComponent != nullptr)
	{
		SearchLightComponent->ToggleVisibility();
	}
	UpdateSearchLightEffect();
}

void ASimCopterHelicopterPawn::UpdateSearchLightEffect()
{
	if (SearchLightComponent == nullptr)
	{
		return;
	}

	const float BeamLength = FMath::Max(100.0f, SearchLightBeamLengthCm);
	const float BeamWidth = FMath::Max(20.0f, SearchLightBeamWidthCm);
	const float OuterConeAngleDeg = FMath::Clamp(
		FMath::RadiansToDegrees(FMath::Atan2(BeamWidth * 0.5f, BeamLength)),
		2.0f,
		80.0f);

	SearchLightComponent->SetIntensity(SearchLightIntensity);
	SearchLightComponent->AttenuationRadius = FMath::Max(SearchLightRangeCm, BeamLength);
	SearchLightComponent->OuterConeAngle = OuterConeAngleDeg;
	SearchLightComponent->InnerConeAngle = FMath::Clamp(OuterConeAngleDeg * 0.45f, 1.0f, OuterConeAngleDeg);
	SearchLightComponent->SetLightColor(SearchLightBeamColor.ToFColor(true));
}

void ASimCopterHelicopterPawn::UpdateEngineState(float DeltaSeconds)
{
	if (bEngineStartHeld && !bEngineRunning && CurrentFuelGallons > 0.01f && CurrentDamage < static_cast<float>(HelicopterTuning.MaxDamage))
	{
		EngineStartHoldElapsed += DeltaSeconds;
		EngineStartHoldAlpha = EngineStartHoldSeconds > 0.0f ? FMath::Clamp(EngineStartHoldElapsed / EngineStartHoldSeconds, 0.0f, 1.0f) : 1.0f;
		if (EngineStartHoldElapsed >= EngineStartHoldSeconds)
		{
			bEngineRunning = true;
			EngineStartHoldElapsed = 0.0f;
			EngineStartHoldAlpha = 0.0f;
		}
	}
	else if (!bEngineStartHeld)
	{
		EngineStartHoldElapsed = 0.0f;
		EngineStartHoldAlpha = 0.0f;
	}

	if (bEngineShutdownHeld && bEngineRunning && bIsLanded)
	{
		EngineShutdownHoldElapsed += DeltaSeconds;
		EngineShutdownHoldAlpha = EngineShutdownHoldSeconds > 0.0f ? FMath::Clamp(EngineShutdownHoldElapsed / EngineShutdownHoldSeconds, 0.0f, 1.0f) : 1.0f;
		if (EngineShutdownHoldElapsed >= EngineShutdownHoldSeconds)
		{
			bEngineRunning = false;
			EngineShutdownHoldElapsed = 0.0f;
			EngineShutdownHoldAlpha = 0.0f;
		}
	}
	else if (!bEngineShutdownHeld || !bIsLanded)
	{
		EngineShutdownHoldElapsed = 0.0f;
		EngineShutdownHoldAlpha = 0.0f;
	}
}

void ASimCopterHelicopterPawn::SimulateFlightStep(float DeltaSeconds)
{
	UpdateEngineState(DeltaSeconds);

	if (!bFlightModelSeeded)
	{
		SeedFlightModelFromActor();
	}

	// SCHOOK: HelicopterWaterLoad 0x00484d20
	// heli[0x74] is the actual number of pounds in the attachment, not a normalized fill amount.
	FlightModel.LoadPounds = BucketWaterPounds;
	if (bWaterCannonHeld && bWaterCannonInstalled && BucketWaterPounds > 0)
	{
		FlightModel.PitchTarget -= SimCopterWaterGameplay::FixedMul(
			SimCopterFixed::FromFloat(DeltaSeconds),
			SimCopterFixed::FromFloat(RopeTuning.CannonForce));
	}

	const FSimCopterFlightInputs Inputs = BuildFlightInputs();
	const FSimCopterFlightEnvironment Environment = BuildFlightEnvironment();
	FlightModel.Step(DeltaSeconds, Inputs, Environment, LastFlightEvents);

	ApplyFlightModelToActor(DeltaSeconds);
	UpdateGroundProbe();
	UpdateForwardProbe();
	UpdateRopeAndBucket(DeltaSeconds);

	// Mirror the simulation status onto the pawn's HUD-facing state.
	bIsLanded = FlightModel.State == ESimCopterFlightState::Parked;
	CurrentFuelGallons = SimCopterFixed::ToFloat(FlightModel.Fuel);
	CurrentDamage = FMath::Clamp(
		static_cast<float>(FlightModel.Tuning.MaxDamage - FlightModel.HitPoints),
		0.0f,
		static_cast<float>(FlightModel.Tuning.MaxDamage));

	// The original respawns a destroyed helicopter at the nearest pad; the
	// remake has no pad registry yet, so it repairs in place where it crashed.
	if (LastFlightEvents.bCrashed)
	{
		if (WaterFXComponent != nullptr)
		{
			// Phase-one FUN_0048a8b0 landing effects are visual only; the flight model remains the
			// authority for the crash/repair transition.
			WaterFXComponent->SpawnHardLanding(GetActorLocation(), ProbeBucketWater(GetActorLocation()));
		}
		bEngineRunning = false;
		SeedFlightModelFromActor();
	}
	else if ((LastFlightEvents.bGroundBounce || LastFlightEvents.bSplashBounce) && WaterFXComponent != nullptr)
	{
		// FUN_0048a8b0 enters its phase-one effect state on the hard impact itself, not only
		// after the later destroyed-helicopter transition.
		WaterFXComponent->SpawnHardLanding(
			GetActorLocation(),
			LastFlightEvents.bSplashBounce || ProbeBucketWater(GetActorLocation()));
	}
}

void ASimCopterHelicopterPawn::SeedFlightModelFromActor()
{
	ApplyFlightTuningToModel();

	const float CapsuleHalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	const FVector Location = GetActorLocation();
	const float Unit = FMath::Max(OriginalUnitToCm, 0.01f);
	FlightModel.ResetOnSurface(
		SimCopterFixed::FromFloat(Location.Y / Unit),
		SimCopterFixed::FromFloat(Location.X / Unit),
		SimCopterFixed::FromFloat((Location.Z - CapsuleHalfHeight) / Unit) - 0x13333);
	FlightModel.Heading = SimCopterFixed::WrapAngle(SimCopterFixed::FromFloat(FRotator::ClampAxis(GetActorRotation().Yaw) * 10.0f));
	bFlightModelSeeded = true;
}

void ASimCopterHelicopterPawn::ApplyFlightTuningToModel()
{
	// Convert the tweak-derived float tuning back to the original units the
	// decompiled model runs in: tenth-degrees, world units (tile/64), 16.16.
	FSimCopterFlightTuning Tuning;
	const float AngleScale = 10.0f; // degrees -> tenth-degrees
	Tuning.MaxBank = SimCopterFixed::FromFloat(HelicopterTuning.MaxBankDeg * AngleScale);
	Tuning.MaxSlide = SimCopterFixed::FromFloat(HelicopterTuning.MaxSlideDeg * AngleScale);
	Tuning.MaxPitch = SimCopterFixed::FromFloat(HelicopterTuning.MaxPitchDeg * AngleScale);
	Tuning.PitchRate = SimCopterFixed::FromFloat(HelicopterTuning.PitchRateDegPerSec * AngleScale);
	Tuning.YawRate = SimCopterFixed::FromFloat(HelicopterTuning.YawAccelDegPerSec);
	Tuning.RollRate = SimCopterFixed::FromFloat(HelicopterTuning.RollRateDegPerSec * AngleScale);
	Tuning.SlideRate = SimCopterFixed::FromFloat(HelicopterTuning.SlideResponse);
	Tuning.ClimbRate = SimCopterFixed::FromFloat(HelicopterTuning.ClimbRateCmPerSec / FMath::Max(TweakClimbToCmPerSec, 1.0f));
	Tuning.MaxLoadPounds = HelicopterTuning.MaxLoadPounds;
	Tuning.MaxYawRate = SimCopterFixed::FromFloat(HelicopterTuning.MaxYawRateDegPerSec);
	Tuning.FuelRateGalPerHour = SimCopterFixed::FromFloat(HelicopterTuning.FuelRateGallonsPerHour);
	Tuning.MaxDamage = HelicopterTuning.MaxDamage;
	Tuning.FuelGallons = SimCopterFixed::FromFloat(HelicopterTuning.FuelGallons);

	Tuning.LandMaxPitch = SimCopterFixed::FromFloat(LandingTuning.MaxPitchDeg * AngleScale);
	Tuning.LandMaxSlide = SimCopterFixed::FromFloat(LandingTuning.MaxRollDeg * AngleScale);
	Tuning.LandMaxSpeed = SimCopterFixed::FromFloat(LandingTuning.MaxHorizontalSpeedCmPerSec / FMath::Max(TweakSpeedToCmPerSec, 1.0f));
	Tuning.LandMaxYSpeed = SimCopterFixed::FromFloat(LandingTuning.MaxVerticalSpeedCmPerSec / FMath::Max(TweakSpeedToCmPerSec, 1.0f));
	Tuning.MaxDescentRate = SimCopterFixed::FromFloat(LandingTuning.MaxDescentRateCmPerSec / FMath::Max(TweakSpeedToCmPerSec, 1.0f));

	Tuning.MinFireAlt = SimCopterFixed::FromFloat(DamageTuning.MinFireAltitudeCm / FMath::Max(TweakAltitudeToCm, 1.0f));
	Tuning.MaxFireAlt = SimCopterFixed::FromFloat(DamageTuning.MaxFireAltitudeCm / FMath::Max(TweakAltitudeToCm, 1.0f));
	Tuning.CollisionSubtract = FMath::RoundToInt(DamageTuning.CollisionDamageScale);

	// Static per-type data from the executable's tuning block (DAT_005040e4 +
	// type*0x5c): passenger seats at +0x00 and the no-tail-rotor flag at +0x38
	// (set for the NOTAR airframes).
	struct FHelicopterTypeStats
	{
		const TCHAR* TypeName;
		int32 PassengerSeats;
		bool bNoTailRotor;
	};
	static const FHelicopterTypeStats TypeStats[] = {
		{ TEXT("Jet Ranger"), 4, false },
		{ TEXT("Hughes 500"), 4, false },
		{ TEXT("Apache"), 0, false },
		{ TEXT("Bell 212"), 14, false },
		{ TEXT("Schwiezer 300"), 2, false },
		{ TEXT("Agusta"), 7, false },
		{ TEXT("Dauphin"), 13, false },
		{ TEXT("MDEXPLORER"), 7, true },
		{ TEXT("MD520"), 4, true },
	};
	const FString Trimmed = HelicopterTypeName.TrimStartAndEnd();
	for (const FHelicopterTypeStats& Stats : TypeStats)
	{
		if (Trimmed.Equals(Stats.TypeName, ESearchCase::IgnoreCase))
		{
			Tuning.PassengerSeats = Stats.PassengerSeats;
			Tuning.bNoTailRotor = Stats.bNoTailRotor;
			break;
		}
	}

	FlightModel.Tuning = Tuning;
	FlightModel.HitPoints = FMath::Min(FlightModel.HitPoints, Tuning.MaxDamage);
	FlightModel.Fuel = FMath::Min(FlightModel.Fuel, Tuning.FuelGallons);
}

FSimCopterFlightInputs ASimCopterHelicopterPawn::BuildFlightInputs() const
{
	// Maps the pawn's input axes onto the original virtual controls read by
	// FUN_00485f50. Digital axes act as the original keyboard keys, which RAMP
	// the attitude targets (the classic trim-and-hold feel) rather than seek a
	// stick-proportional target.
	FSimCopterFlightInputs Inputs;
	if (!bEngineRunning)
	{
		return Inputs; // controls dead; the rotor spools down in the model
	}

	constexpr float KeyThreshold = 0.25f;
	Inputs.bPitchForwardKey = PitchInput > KeyThreshold;
	Inputs.bPitchBackKey = PitchInput < -KeyThreshold;
	Inputs.bTurnRightKey = RollInput > KeyThreshold;
	Inputs.bTurnLeftKey = RollInput < -KeyThreshold;
	Inputs.bSlideRightKey = YawInput > KeyThreshold;
	Inputs.bSlideLeftKey = YawInput < -KeyThreshold;
	if (CollectiveInput > KeyThreshold)
	{
		Inputs.ClimbCommand = 1;
	}
	else if (CollectiveInput < -KeyThreshold)
	{
		Inputs.ClimbCommand = -1;
	}
	return Inputs;
}

FSimCopterFlightEnvironment ASimCopterHelicopterPawn::BuildFlightEnvironment() const
{
	// Backs the original's heightfield/object queries with a downward trace,
	// the same compromise the ground agents use: the first surface below is
	// both the terrain and the landing surface, and building sides are handled
	// by the movement sweep instead of tile-object boxes.
	FSimCopterFlightEnvironment Environment;
	const float Unit = FMath::Max(OriginalUnitToCm, 0.01f);
	const FVector Location = GetActorLocation();
	const int32 FallbackHeight = SimCopterFixed::FromFloat((Location.Z - 10000.0f) / Unit);
	Environment.TerrainHeight = FallbackHeight;
	Environment.SurfaceHeight = FallbackHeight;

	if (GetWorld() == nullptr)
	{
		return Environment;
	}

	bool bHasTerrainGrid = false;
	bool bWaterTerrain = false;
	if (ASimCity2000CityActor* City = ResolveCityActor())
	{
		float TerrainWorldZ = 0.0f;
		uint8 TerrainClass = 0xff;
		FIntPoint TerrainTile = FIntPoint::ZeroValue;
		if (City->TryGetWaterGameplaySurface(
			Location,
			TerrainWorldZ,
			TerrainClass,
			&TerrainTile))
		{
			bHasTerrainGrid = true;
			Environment.TerrainHeight =
				SimCopterFixed::FromFloat(TerrainWorldZ / Unit);
			Environment.SurfaceHeight = Environment.TerrainHeight;
			bWaterTerrain =
				SimCopterWaterGameplay::IsWaterTerrainClass(TerrainClass) &&
				!City->HasStandingBuildingAtTile(TerrainTile.X, TerrainTile.Y);
		}
	}

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterFlightSurface), false, this);
	const FVector Start(Location.X, Location.Y, Location.Z + 20000.0f);
	const FVector End(Location.X, Location.Y, Location.Z - 30000.0f);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams) && Hit.bBlockingHit)
	{
		const int32 SurfaceHeight = SimCopterFixed::FromFloat(Hit.ImpactPoint.Z / Unit);
		if (!bHasTerrainGrid)
		{
			Environment.TerrainHeight = SurfaceHeight;
		}
		Environment.SurfaceHeight = SurfaceHeight;
		Environment.bTerrainFlat = Hit.ImpactNormal.Z >= LandingFlatNormalZ;

		if (bWaterTerrain)
		{
			// Original: tile class < 10 with nothing built - splash bounce, no
			// landing (FUN_00487160 clears the flat flag on those tiles).
			Environment.bHostileSurface = true;
			Environment.bTerrainFlat = false;
		}
		else if (bEngineShutdownHeld)
		{
			Environment.bTerrainFlat = true;
		}
	}

	return Environment;
}

void ASimCopterHelicopterPawn::ApplyFlightModelToActor(float DeltaSeconds)
{
	if (RootComponent == nullptr)
	{
		return;
	}

	// Axis mapping: sim X/Z are the original world axes with forward =
	// (sin Heading, cos Heading); placing sim Z on UE X and sim X on UE Y makes
	// UE yaw = Heading/10 with the turn/slide signs matching the lean.
	const float Unit = FMath::Max(OriginalUnitToCm, 0.01f);
	const float CapsuleHalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	const FVector NewLocation(
		SimCopterFixed::ToFloat(FlightModel.PosZ) * Unit,
		SimCopterFixed::ToFloat(FlightModel.PosX) * Unit,
		SimCopterFixed::ToFloat(FlightModel.Altitude) * Unit + CapsuleHalfHeight);
	const FRotator NewRotation(0.0f, SimCopterFixed::ToFloat(FlightModel.Heading) / 10.0f, 0.0f);

	FHitResult BlockingHit;
	RootComponent->MoveComponent(NewLocation - GetActorLocation(), NewRotation.Quaternion(), true, &BlockingHit);
	if (BlockingHit.IsValidBlockingHit() && FMath::Abs(BlockingHit.Normal.Z) < 0.6f)
	{
		// Hit a wall: run the original object-collision response (damage plus
		// an attitude kick away from the motion direction and a bounce up).
		FlightModel.NotifyObjectCollision(LastFlightEvents);
	}

	// Write the possibly blocked position back so the simulation stays in
	// lockstep with the actor.
	const FVector Applied = GetActorLocation();
	FlightModel.PosZ = SimCopterFixed::FromFloat(Applied.X / Unit);
	FlightModel.PosX = SimCopterFixed::FromFloat(Applied.Y / Unit);
	FlightModel.Altitude = SimCopterFixed::FromFloat((Applied.Z - CapsuleHalfHeight) / Unit);

	// Display attitude: the original builds the render matrix from the pitch
	// *target* and the smoothed bank (which inherits the slide when larger).
	CurrentPitchDeg = -SimCopterFixed::ToFloat(FlightModel.DisplayPitchTenthDeg()) / 10.0f;
	CurrentRollDeg = -SimCopterFixed::ToFloat(FlightModel.DisplayBankTenthDeg()) / 10.0f;

	// HUD/back-compat velocity in UE space. Horizontal motion integrates at
	// 40000/65536 units per unit of speed; climb integrates directly.
	constexpr float PositionScale = 40000.0f / 65536.0f;
	VelocityCmPerSec = FVector(
		SimCopterFixed::ToFloat(FlightModel.VelZ) * PositionScale * Unit,
		SimCopterFixed::ToFloat(FlightModel.VelX) * PositionScale * Unit,
		SimCopterFixed::ToFloat(FlightModel.ClimbSpeed) * Unit);
}

void ASimCopterHelicopterPawn::UpdateGroundProbe()
{
	LastGroundHit = FHitResult();
	GroundClearanceCm = LandingProbeDistance;

	if (GetWorld() == nullptr || CollisionComponent == nullptr)
	{
		return;
	}

	const float CapsuleHalfHeight = CollisionComponent->GetScaledCapsuleHalfHeight();
	const FVector Start = GetActorLocation();
	const FVector End = Start - FVector::UpVector * (CapsuleHalfHeight + LandingProbeDistance);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterGroundProbe), false, this);
	GetWorld()->LineTraceSingleByChannel(LastGroundHit, Start, End, ECC_Visibility, QueryParams);
	if (LastGroundHit.bBlockingHit)
	{
		GroundClearanceCm = FMath::Max(0.0f, LastGroundHit.Distance - CapsuleHalfHeight);
	}

	if (bDrawDebugProbes)
	{
		DrawDebugLine(GetWorld(), Start, End, LastGroundHit.bBlockingHit ? FColor::Green : FColor::Red, false, 0.0f, 0, 2.0f);
	}
}

void ASimCopterHelicopterPawn::UpdateForwardProbe()
{
	LastForwardProbeHit = FHitResult();
	ForwardObstacleDistanceCm = CollisionProbeDistance;

	if (GetWorld() == nullptr)
	{
		return;
	}

	const FVector Start = GetActorLocation() + FVector::UpVector * 18.0f;
	const FVector End = Start + GetActorForwardVector() * CollisionProbeDistance;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterForwardProbe), false, this);
	GetWorld()->SweepSingleByChannel(
		LastForwardProbeHit,
		Start,
		End,
		FQuat::Identity,
		ECC_Visibility,
		FCollisionShape::MakeSphere(CollisionProbeRadius),
		QueryParams);
	if (LastForwardProbeHit.bBlockingHit)
	{
		ForwardObstacleDistanceCm = LastForwardProbeHit.Distance;
	}

	if (bDrawDebugProbes)
	{
		DrawDebugLine(GetWorld(), Start, End, LastForwardProbeHit.bBlockingHit ? FColor::Yellow : FColor::Blue, false, 0.0f, 0, 2.0f);
		DrawDebugSphere(GetWorld(), LastForwardProbeHit.bBlockingHit ? LastForwardProbeHit.ImpactPoint : End, CollisionProbeRadius, 12, FColor::Yellow, false, 0.0f);
	}
}

void ASimCopterHelicopterPawn::UpdateRopeAndBucket(float)
{
	InitializeRopeState();
	const bool bCollisionSpill = StepRopeState();
	UpdateRopeVisuals();

	const FVector BucketWorld = RopeNodeWorldPositions.Last();
	const FVector EndDirection =
		(RopeNodeWorldPositions.Last() - RopeNodeWorldPositions[SimCopterWaterGameplay::RopeNodeCount - 2])
		.GetSafeNormal(SMALL_NUMBER, -FVector::UpVector);

	// SCHOOK: BucketFill 0x00487bb0
	// Filling is automatic while the active attachment is at a class-0..9 surface.
	if (bRopeDeployed)
	{
		float SurfaceWorldZ = 0.0f;
		uint8 TerrainClass = 0xff;
		FIntPoint SurfaceCell = FIntPoint::ZeroValue;
		if (ASimCity2000CityActor* City = ResolveCityActor();
			City != nullptr &&
			City->TryGetWaterGameplaySurface(BucketWorld, SurfaceWorldZ, TerrainClass, &SurfaceCell))
		{
			const float SafeUnit = FMath::Max(OriginalUnitToCm, 0.01f);
			const int32 BucketHeight1616 =
				SimCopterFixed::FromFloat(BucketWorld.Z / SafeUnit);
			const int32 SurfaceHeight1616 =
				SimCopterFixed::FromFloat(SurfaceWorldZ / SafeUnit);
			if (SimCopterWaterGameplay::CanFillBucket(
				true,
				BucketHeight1616,
				SurfaceHeight1616,
				TerrainClass))
			{
				const int32 PreviousWaterPounds = BucketWaterPounds;
				BucketWaterPounds = SimCopterWaterGameplay::FillBucketFrame(
					BucketWaterPounds,
					HelicopterTuning.MaxLoadPounds,
					FMath::RoundToInt(RopeTuning.BucketFillPoundsPerFrame));
				if (WaterFXComponent != nullptr)
				{
					const FVector SurfaceWorld(BucketWorld.X, BucketWorld.Y, SurfaceWorldZ);
					WaterFXComponent->SpawnTilePuff(
						SurfaceWorld,
						3,
						SurfaceCell.X,
						SurfaceCell.Y);
					if (PreviousWaterPounds < HelicopterTuning.MaxLoadPounds)
					{
						WaterFXComponent->SpawnTilePuff(
							SurfaceWorld,
							8,
							SurfaceCell.X,
							SurfaceCell.Y);
					}
				}
			}
		}
	}

	if (bRopeDeployed && (bBucketDumpHeld || bCollisionSpill))
	{
		EmitBucketWaterFrame(bCollisionSpill);
	}
	EmitWaterCannonFrame();

	BucketWaterFraction = HelicopterTuning.MaxLoadPounds > 0
		? FMath::Clamp(
			static_cast<float>(BucketWaterPounds) /
				static_cast<float>(HelicopterTuning.MaxLoadPounds),
			0.0f,
			1.0f)
		: 0.0f;
	PreviousBucketWorld = BucketWorld;
	PreviousRopeEndDirection = EndDirection;
	RefreshWaterControlsWidget();
}

FVector ASimCopterHelicopterPawn::GetRopeAnchorWorldLocation() const
{
	const FTransform AnchorTransform = ModelPivot != nullptr
		? ModelPivot->GetComponentTransform()
		: GetActorTransform();
	return AnchorTransform.TransformPosition(RopeAnchorOffsetCm);
}

void ASimCopterHelicopterPawn::InitializeRopeState()
{
	if (bRopeStateInitialized &&
		RopeNodeWorldPositions.Num() == SimCopterWaterGameplay::RopeNodeCount)
	{
		return;
	}

	const float SegmentLengthCm =
		static_cast<float>(SimCopterWaterGameplay::RopeSegmentLength1616) *
		SimCopterEffectFX::Fixed1616ToCm;
	const FVector Anchor = GetRopeAnchorWorldLocation();
	RopeNodeWorldPositions.SetNum(SimCopterWaterGameplay::RopeNodeCount);
	for (int32 NodeIndex = 0; NodeIndex < RopeNodeWorldPositions.Num(); ++NodeIndex)
	{
		RopeNodeWorldPositions[NodeIndex] =
			Anchor - FVector::UpVector * SegmentLengthCm * static_cast<float>(NodeIndex);
	}
	PreviousRopeAnchorWorld = Anchor;
	PreviousBucketWorld = RopeNodeWorldPositions.Last();
	PreviousRopeEndDirection = -FVector::UpVector;
	bRopeStateInitialized = true;
}

bool ASimCopterHelicopterPawn::StepRopeState()
{
	if (RopeAdjustInput > 0.25f)
	{
		++RopeFirstActiveNode;
	}
	else if (RopeAdjustInput < -0.25f)
	{
		--RopeFirstActiveNode;
	}
	RopeFirstActiveNode = FMath::Clamp(
		RopeFirstActiveNode,
		SimCopterWaterGameplay::RopeMinimumFirstActiveNode,
		SimCopterWaterGameplay::RopeStowedFirstActiveNode);
	bRopeDeployed =
		RopeFirstActiveNode < SimCopterWaterGameplay::RopeStowedFirstActiveNode;

	const FVector Anchor = GetRopeAnchorWorldLocation();
	if (!bRopeDeployed)
	{
		for (FVector& Node : RopeNodeWorldPositions)
		{
			Node = Anchor;
		}
		PreviousRopeAnchorWorld = Anchor;
		return false;
	}

	// SCHOOK: RopeSimulation 0x004883a0
	const float SegmentLengthCm =
		static_cast<float>(SimCopterWaterGameplay::RopeSegmentLength1616) *
		SimCopterEffectFX::Fixed1616ToCm;
	for (int32 NodeIndex = 0; NodeIndex <= RopeFirstActiveNode; ++NodeIndex)
	{
		RopeNodeWorldPositions[NodeIndex] = Anchor;
	}
	const float LoadFraction = HelicopterTuning.MaxLoadPounds > 0
		? static_cast<float>(BucketWaterPounds) /
			static_cast<float>(HelicopterTuning.MaxLoadPounds)
		: 0.0f;
	const float LoadDivisor = FMath::Max(
		1.0f,
		8.0f + LoadFraction * RopeTuning.RopeLoadFactor);
	const FVector AnchorTravel = Anchor - PreviousRopeAnchorWorld;
	const bool bMovingFastEnoughToSpill =
		FMath::Abs(AnchorTravel.X) + FMath::Abs(AnchorTravel.Y) >
		SimCopterEffectFX::OriginalUnitToCm;
	bool bBucketCollision = false;

	UWorld* World = GetWorld();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterRope), false, this);
	const float CollisionRadiusCm = SimCopterEffectFX::OriginalUnitToCm;
	for (int32 NodeIndex = RopeFirstActiveNode + 1;
		NodeIndex < SimCopterWaterGameplay::RopeNodeCount;
		++NodeIndex)
	{
		const FVector PreviousNode = RopeNodeWorldPositions[NodeIndex - 1];
		FVector CurrentOffset = RopeNodeWorldPositions[NodeIndex] - Anchor;
		const FVector PreviousOffset = PreviousNode - Anchor;
		const FVector HorizontalDelta(
			CurrentOffset.X - PreviousOffset.X,
			CurrentOffset.Y - PreviousOffset.Y,
			0.0f);
		CurrentOffset.X -= HorizontalDelta.X * RopeTuning.RopeTension;
		CurrentOffset.Y -= HorizontalDelta.Y * RopeTuning.RopeTension;
		const float LoadSag = FMath::Min(
			SegmentLengthCm,
			(FMath::Abs(HorizontalDelta.X) + FMath::Abs(HorizontalDelta.Y)) /
				LoadDivisor);
		CurrentOffset.Z = PreviousOffset.Z + LoadSag - SegmentLengthCm;

		FVector Desired = Anchor + CurrentOffset;
		bool bNodeCollision = false;
		if (World != nullptr)
		{
			FHitResult Hit;
			if (World->SweepSingleByChannel(
				Hit,
				RopeNodeWorldPositions[NodeIndex],
				Desired,
				FQuat::Identity,
				ECC_Visibility,
				FCollisionShape::MakeSphere(CollisionRadiusCm),
				QueryParams))
			{
				Desired = Hit.Location + Hit.Normal * CollisionRadiusCm;
				bNodeCollision = true;
			}
		}

		float SurfaceZ = 0.0f;
		uint8 TerrainClass = 0xff;
		if (ASimCity2000CityActor* City = ResolveCityActor();
			City != nullptr &&
			City->TryGetWaterGameplaySurface(Desired, SurfaceZ, TerrainClass) &&
			Desired.Z < SurfaceZ + CollisionRadiusCm)
		{
			Desired.Z = SurfaceZ + CollisionRadiusCm;
			bNodeCollision = true;
		}

		// The original resolves terrain/object penetration first, then normalizes this node back
		// to the fixed four-unit segment length.
		const FVector ResolvedSegment = Desired - PreviousNode;
		Desired = PreviousNode +
			(ResolvedSegment.IsNearlyZero()
				? -FVector::UpVector * SegmentLengthCm
				: ResolvedSegment.GetSafeNormal() * SegmentLengthCm);
		RopeNodeWorldPositions[NodeIndex] = Desired;
		bBucketCollision |=
			bNodeCollision &&
			bMovingFastEnoughToSpill &&
			NodeIndex == SimCopterWaterGameplay::RopeNodeCount - 1;
	}
	PreviousRopeAnchorWorld = Anchor;
	return bBucketCollision;
}

void ASimCopterHelicopterPawn::UpdateRopeVisuals()
{
	if (RopeMeshComponent != nullptr)
	{
		RopeMeshComponent->SetVisibility(false);
	}
	const FTransform RopeTransform = CollisionComponent != nullptr
		? CollisionComponent->GetComponentTransform()
		: GetActorTransform();
	for (int32 SegmentIndex = 0;
		SegmentIndex < RopeSegmentComponents.Num();
		++SegmentIndex)
	{
		USplineMeshComponent* SegmentComponent = RopeSegmentComponents[SegmentIndex];
		if (SegmentComponent == nullptr)
		{
			continue;
		}
		const bool bVisible =
			bRopeDeployed &&
			SegmentIndex >= RopeFirstActiveNode &&
			RopeNodeWorldPositions.IsValidIndex(SegmentIndex + 1);
		SegmentComponent->SetVisibility(bVisible);
		SegmentComponent->SetHiddenInGame(!bVisible);
		if (!bVisible)
		{
			continue;
		}

		const FVector Start = RopeTransform.InverseTransformPosition(
			RopeNodeWorldPositions[SegmentIndex]);
		const FVector End = RopeTransform.InverseTransformPosition(
			RopeNodeWorldPositions[SegmentIndex + 1]);
		const FVector Tangent = End - Start;
		SegmentComponent->SetStartScale(FVector2D(0.04f, 0.04f), false);
		SegmentComponent->SetEndScale(FVector2D(0.04f, 0.04f), false);
		SegmentComponent->SetStartAndEnd(Start, Tangent, End, Tangent, true);
	}

	const float SegmentLengthCm =
		static_cast<float>(SimCopterWaterGameplay::RopeSegmentLength1616) *
		SimCopterEffectFX::Fixed1616ToCm;
	RopeLengthCm =
		static_cast<float>(
			SimCopterWaterGameplay::RopeNodeCount - 1 - RopeFirstActiveNode) *
		SegmentLengthCm;

	const FVector BucketAttachmentWorld = RopeNodeWorldPositions.Last();
	const FVector EndDirection =
		(RopeNodeWorldPositions.Last() -
			RopeNodeWorldPositions[SimCopterWaterGameplay::RopeNodeCount - 2])
		.GetSafeNormal(SMALL_NUMBER, -FVector::UpVector);
	const FQuat BucketRotation = FQuat::FindBetweenNormals(-FVector::UpVector, EndDirection);

	const bool bShowOriginalBucket = bRopeDeployed && bUsingOriginalBucketMesh;
	if (OriginalBucketMeshComponent != nullptr)
	{
		OriginalBucketMeshComponent->SetVisibility(bShowOriginalBucket);
		OriginalBucketMeshComponent->SetHiddenInGame(!bShowOriginalBucket);
		OriginalBucketMeshComponent->SetWorldLocation(BucketAttachmentWorld);
		OriginalBucketMeshComponent->SetWorldRotation(BucketRotation);
		OriginalBucketMeshComponent->SetRelativeScale3D(FVector::OneVector);
	}

	if (BucketMeshComponent != nullptr)
	{
		const bool bShowFallbackBucket = bRopeDeployed && !bUsingOriginalBucketMesh;
		BucketMeshComponent->SetVisibility(bShowFallbackBucket);
		BucketMeshComponent->SetHiddenInGame(!bShowFallbackBucket);
		BucketMeshComponent->SetWorldLocation(
			BucketAttachmentWorld + EndDirection * 18.0f);
		BucketMeshComponent->SetWorldRotation(BucketRotation);
		BucketMeshComponent->SetRelativeScale3D(FVector(0.30f, 0.34f, 0.36f));
	}
}

void ASimCopterHelicopterPawn::EmitBucketWaterFrame(bool)
{
	if (BucketWaterPounds <= 0 || WaterFXComponent == nullptr)
	{
		return;
	}

	const FVector BucketWorld = RopeNodeWorldPositions.Last();
	const FVector EndDirection =
		(RopeNodeWorldPositions.Last() -
			RopeNodeWorldPositions[SimCopterWaterGameplay::RopeNodeCount - 2])
		.GetSafeNormal(SMALL_NUMBER, -FVector::UpVector);
	const float SwingTerm = FMath::Max(
		0.0f,
		PreviousRopeEndDirection.Z - EndDirection.Z) * RopeTuning.WaterThrow;
	const FVector BucketTravel = PreviousBucketWorld - BucketWorld;
	const FVector SwingOffset(
		BucketTravel.X * (FMath::RandBool() ? 1.0f : -1.0f) * SwingTerm,
		BucketTravel.Y * (FMath::RandBool() ? 1.0f : -1.0f) * SwingTerm,
		0.0f);
	const FVector SpawnWorld =
		BucketWorld +
		EndDirection * (
			static_cast<float>(SimCopterWaterGameplay::BucketEmissionOffset1616) *
			SimCopterEffectFX::Fixed1616ToCm) +
		SwingOffset;
	const float SpeedCmPerSec =
		static_cast<float>(SimCopterWaterGameplay::BucketEmissionSpeed1616) *
		SimCopterEffectFX::Fixed1616ToCm;

	BucketWaterPounds = SimCopterWaterGameplay::DumpBucketFrame(
		BucketWaterPounds,
		FMath::RoundToInt(RopeTuning.BucketDumpPoundsPerFrame));
	WaterFXComponent->SpawnEffect(
		ESimCopterEffectType::BucketDrip,
		SpawnWorld,
		EndDirection * SpeedCmPerSec);
}

void ASimCopterHelicopterPawn::EmitWaterCannonFrame()
{
	if (!bWaterCannonHeld)
	{
		return;
	}
	if (!bWaterCannonInstalled || BucketWaterPounds <= 0)
	{
		return;
	}
	if (WaterFXComponent == nullptr)
	{
		return;
	}

	// SCHOOK: WaterCannonEmitter 0x00484d20
	const FVector Direction = ModelPivot != nullptr
		? ModelPivot->GetForwardVector().GetSafeNormal()
		: GetActorForwardVector();
	const FVector SpawnWorld =
		(ModelPivot != nullptr ? ModelPivot->GetComponentLocation() : GetActorLocation()) +
		GetActorUpVector() * (3.0f * OriginalUnitToCm);
	const float ForwardSpeedOriginal =
		SimCopterFixed::ToFloat(FlightModel.ForwardSpeed);
	const float EmissionSpeedCmPerSec =
		(ForwardSpeedOriginal + RopeTuning.CannonForce) * OriginalUnitToCm;

	BucketWaterPounds = SimCopterWaterGameplay::DumpBucketFrame(
		BucketWaterPounds,
		SimCopterWaterGameplay::CannonPoundsPerFrame);
	WaterFXComponent->SpawnEffect(
		ESimCopterEffectType::Spray,
		SpawnWorld,
		Direction * EmissionSpeedCmPerSec);
}

void ASimCopterHelicopterPawn::SimForceFire()
{
	if (ASimCopterMissionSystemActor* MissionSystem = ResolveMissionSystem())
	{
		MissionSystem->SimForceFire();
	}
}

void ASimCopterHelicopterPawn::SimForceCarFire()
{
	if (ASimCopterMissionSystemActor* MissionSystem = ResolveMissionSystem())
	{
		MissionSystem->SimForceCarFire();
	}
}

ASimCopterMissionSystemActor* ASimCopterHelicopterPawn::ResolveMissionSystem()
{
	if (CachedMissionSystem.IsValid())
	{
		return CachedMissionSystem.Get();
	}
	if (UWorld* World = GetWorld())
	{
		AActor* Found = UGameplayStatics::GetActorOfClass(World, ASimCopterMissionSystemActor::StaticClass());
		CachedMissionSystem = Cast<ASimCopterMissionSystemActor>(Found);
	}
	return CachedMissionSystem.Get();
}

ASimCity2000CityActor* ASimCopterHelicopterPawn::ResolveCityActor() const
{
	if (CachedCityActor.IsValid())
	{
		return CachedCityActor.Get();
	}
	if (UWorld* World = GetWorld())
	{
		CachedCityActor = Cast<ASimCity2000CityActor>(
			UGameplayStatics::GetActorOfClass(
				World,
				ASimCity2000CityActor::StaticClass()));
	}
	return CachedCityActor.Get();
}

void ASimCopterHelicopterPawn::UpdateRotorWash(float DeltaSeconds)
{
	// FUN_004881b0 has one visual: a class-8 SMOKE puff.  Surface-dependent interpretation comes
	// from the scene underneath it; do not branch into invented dust/water palettes here.
	if (!bEnableRotorWash || WaterFXComponent == nullptr || !FlightModel.bRotorBlurDisc)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const FVector Location = GetActorLocation();
	FHitResult Hit;
	FCollisionQueryParams QueryParams(FName(TEXT("SimCopterRotorWash")), /*bTraceComplex*/ false, this);
	const FVector Start(Location.X, Location.Y, Location.Z + 500.0f);
	const FVector End(Location.X, Location.Y, Location.Z - 20000.0f);
	if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams) || !Hit.bBlockingHit)
	{
		return;
	}

	const float SurfaceZ = Hit.ImpactPoint.Z;
	const float Height = Location.Z - SurfaceZ;

	// +0x158 is rotor speed ([0x56]), not world altitude. The previous comparison made the
	// near-surface and altitude gates mutually exclusive, so this effect could never fire.
	if (!USimCopterParticleFXComponent::IsRotorWashEligible(Height, FlightModel.RotorSpeed))
	{
		return;
	}

	const float RandomYawDegrees = FMath::FRandRange(-150.0f, 150.0f);
	const FVector Offset = (-GetActorRightVector()).RotateAngleAxis(RandomYawDegrees, FVector::UpVector) *
		(32.0f * SimCopterEffectFX::OriginalUnitToCm);
	const FVector GroundPoint(Location.X, Location.Y, SurfaceZ);
	WaterFXComponent->SpawnTilePuff(GroundPoint + Offset, 8);
}

void ASimCopterHelicopterPawn::UpdateVisuals(float DeltaSeconds)
{
	if (ModelPivot != nullptr)
	{
		ModelPivot->SetRelativeRotation(FRotator(CurrentPitchDeg, 0.0f, CurrentRollDeg));
	}

	// Rotor animation comes from the decompiled model (FUN_00487740): the
	// blade angle advances proportionally while spooling and then strobes at a
	// fixed 39.1 degrees per simulation step once the rotor passes 250. Both
	// rotors share the step; the main rotor sweeps the mast (yaw), the tail
	// rotor its lateral hub (pitch).
	const float MainAngleDeg = SimCopterFixed::ToFloat(FlightModel.MainRotorAngle) / 10.0f;
	const float TailAngleDeg = SimCopterFixed::ToFloat(FlightModel.TailRotorAngle) / 10.0f;
	const FRotator MainRotorRotation(0.0f, MainAngleDeg, 0.0f);
	const FRotator TailRotorRotation(TailAngleDeg, 0.0f, 0.0f);

	if (MainRotorMeshComponent != nullptr)
	{
		MainRotorMeshComponent->SetRelativeRotation(MainRotorRotation);
	}
	if (TailRotorMeshComponent != nullptr)
	{
		TailRotorMeshComponent->SetRelativeRotation(TailRotorRotation);
		TailRotorMeshComponent->SetVisibility(!FlightModel.Tuning.bNoTailRotor && !bUsingOriginalMesh);
	}
	if (HeliMainRotorMeshComponent != nullptr)
	{
		HeliMainRotorMeshComponent->SetRelativeRotation(MainRotorRotation);
		if (MainRotorDiscSectionIndex != INDEX_NONE)
		{
			// The original toggles the face-type-11 blur faces on at lift RPM.
			HeliMainRotorMeshComponent->SetMeshSectionVisible(MainRotorDiscSectionIndex, FlightModel.bRotorBlurDisc);
		}
	}
	if (HeliTailRotorMeshComponent != nullptr)
	{
		HeliTailRotorMeshComponent->SetRelativeRotation(TailRotorRotation);
		if (bUsingOriginalMesh)
		{
			HeliTailRotorMeshComponent->SetVisibility(bShowSeparateTailRotor && !FlightModel.Tuning.bNoTailRotor);
		}
		if (TailRotorDiscSectionIndex != INDEX_NONE)
		{
			HeliTailRotorMeshComponent->SetMeshSectionVisible(TailRotorDiscSectionIndex, FlightModel.bRotorBlurDisc);
		}
	}

	UpdateSearchLightEffect();
}

void ASimCopterHelicopterPawn::UpdateCamera(float DeltaSeconds)
{
	if (CameraBoom == nullptr)
	{
		return;
	}

	// Gamepad look drives the camera continuously; mouse look only contributes while a mouse
	// button is held (a click-drag). Dragging, then the CameraRecenterDelaySeconds window
	// after release, holds the offset; gamepad look recenters immediately on release.
	float YawLookInput = CameraYawInput;
	float PitchLookInput = CameraPitchInput;
	if (bCameraDragActive)
	{
		YawLookInput += MouseLookYawInput;
		PitchLookInput += MouseLookPitchInput;
		CameraRecenterDelayRemaining = CameraRecenterDelaySeconds;
	}
	else if (CameraRecenterDelayRemaining > 0.0f)
	{
		CameraRecenterDelayRemaining = FMath::Max(0.0f, CameraRecenterDelayRemaining - DeltaSeconds);
	}

	CameraYawOffsetDeg += YawLookInput * CameraYawSpeedDegPerSec * DeltaSeconds;
	CameraPitchOffsetDeg = FMath::Clamp(CameraPitchOffsetDeg + PitchLookInput * CameraPitchSpeedDegPerSec * DeltaSeconds, -28.0f, 18.0f);

	const bool bHoldOffset = bCameraDragActive || CameraRecenterDelayRemaining > 0.0f;
	const bool bRecenterYaw = !bHoldOffset && FMath::IsNearlyZero(CameraYawInput, 0.01f);

	const float HorizontalSpeed = FVector(VelocityCmPerSec.X, VelocityCmPerSec.Y, 0.0f).Size();
	const float SpeedAlpha = FMath::Clamp(HorizontalSpeed / FMath::Max(1.0f, MaxForwardSpeedCmPerSec), 0.0f, 1.0f);
	const float ActorYaw = GetActorRotation().Yaw;
	float ViewYaw = ActorYaw;
	float ViewPitch = ChaseCameraBasePitch - SpeedAlpha * 4.0f;
	float ArmLength = FMath::Lerp(ChaseCameraMinDistance, ChaseCameraMaxDistance, CameraZoomAlpha) + SpeedAlpha * ChaseSpeedPullbackCm;
	FVector TargetOffset(0.0f, 0.0f, ChaseCameraTargetHeightCm + SpeedAlpha * ChaseCameraSpeedTargetLiftCm);

	if (CameraMode == ESimCopterCameraMode::Chase)
	{
		// Keep the camera behind the helicopter for forward, reverse, and strafe: it no
		// longer swings to face the travel direction.
		if (bRecenterYaw)
		{
			CameraYawOffsetDeg = FMath::FInterpTo(CameraYawOffsetDeg, 0.0f, DeltaSeconds, 1.35f);
		}
	}
	else if (CameraMode == ESimCopterCameraMode::Orbit)
	{
		ViewYaw = ActorYaw;
		ViewPitch = -18.0f;
		ArmLength = FMath::Lerp(640.0f, 1400.0f, CameraZoomAlpha);
		TargetOffset = FVector(0.0f, 0.0f, 120.0f);
	}
	else
	{
		ViewYaw = ActorYaw;
		ViewPitch = RescueCameraPitch;
		ArmLength = FMath::Lerp(860.0f, 1500.0f, CameraZoomAlpha);
		TargetOffset = FVector(0.0f, 0.0f, 30.0f);
		if (bRecenterYaw)
		{
			CameraYawOffsetDeg = FMath::FInterpTo(CameraYawOffsetDeg, 0.0f, DeltaSeconds, 0.8f);
		}
	}

	const float RelativeYaw = FRotator::NormalizeAxis(ViewYaw + CameraYawOffsetDeg - ActorYaw);
	constexpr float MinCameraPitchDeg = -78.0f;
	constexpr float MaxCameraPitchDeg = 2.0f;
	const float DesiredPitchDeg = FMath::Clamp(ViewPitch + CameraPitchOffsetDeg, MinCameraPitchDeg, MaxCameraPitchDeg);
	const float WorldYawDeg = ActorYaw + RelativeYaw;
	const FRotator CameraWorldRotation(DesiredPitchDeg, WorldYawDeg, 0.0f);
	const FVector UnliftedBoomOrigin = GetActorLocation() + TargetOffset;
	float RequiredGroundLiftCm = 0.0f;
	const float DesiredGroundLiftCm = ResolveCameraGroundLift(
		UnliftedBoomOrigin,
		ArmLength,
		CameraWorldRotation,
		RequiredGroundLiftCm);
	CurrentCameraGroundLiftCm = FMath::Max(
		RequiredGroundLiftCm,
		FMath::FInterpTo(CurrentCameraGroundLiftCm, DesiredGroundLiftCm, DeltaSeconds, CameraGroundLiftLerpSpeed));
	TargetOffset.Z += CurrentCameraGroundLiftCm;

	const FVector BoomOrigin = GetActorLocation() + TargetOffset;
	const float ObstructionSafeArmLength = ResolveCameraArmLengthForObstruction(BoomOrigin, ArmLength, CameraWorldRotation);
	const float ArmLerpSpeed =
		ObstructionSafeArmLength < CurrentCameraArmLengthCm
			? CameraObstructionPullInLerpSpeed
			: CameraObstructionReleaseLerpSpeed;
	CurrentCameraArmLengthCm = FMath::FInterpTo(CurrentCameraArmLengthCm, ObstructionSafeArmLength, DeltaSeconds, ArmLerpSpeed);

	CameraBoom->TargetArmLength = CurrentCameraArmLengthCm;
	CameraBoom->TargetOffset = TargetOffset;
	CameraBoom->SetRelativeRotation(FRotator(DesiredPitchDeg, RelativeYaw, 0.0f));
}

float ASimCopterHelicopterPawn::ResolveCameraGroundLift(
	const FVector& BoomOrigin,
	float ArmLength,
	const FRotator& WorldRotation,
	float& OutRequiredLiftCm) const
{
	OutRequiredLiftCm = 0.0f;
	if (GetWorld() == nullptr || ArmLength <= UE_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const FVector DesiredCameraLocation = BoomOrigin - WorldRotation.Vector() * ArmLength;
	const FVector TraceStart = DesiredCameraLocation + FVector::UpVector * CameraGroundProbeUpCm;
	const FVector TraceEnd = DesiredCameraLocation - FVector::UpVector * CameraGroundProbeDownCm;

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterCameraGroundProbe), false, this);
	if (!GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Camera, QueryParams) || !Hit.bBlockingHit)
	{
		return 0.0f;
	}

	const float MinCameraZ = Hit.ImpactPoint.Z + CameraGroundClearanceCm;
	const float DistanceAboveGroundCm = DesiredCameraLocation.Z - MinCameraZ;
	OutRequiredLiftCm = FMath::Max(0.0f, -DistanceAboveGroundCm);
	if (DistanceAboveGroundCm >= CameraGroundLiftProbeRangeCm)
	{
		return OutRequiredLiftCm;
	}

	const float ProximityAlpha = 1.0f - FMath::Clamp(
		DistanceAboveGroundCm / FMath::Max(1.0f, CameraGroundLiftProbeRangeCm),
		0.0f,
		1.0f);
	return FMath::Max(OutRequiredLiftCm, CameraGroundLiftHeightCm * ProximityAlpha);
}

float ASimCopterHelicopterPawn::ResolveCameraArmLengthForObstruction(
	const FVector& BoomOrigin,
	float DesiredArmLength,
	const FRotator& WorldRotation) const
{
	if (GetWorld() == nullptr || CameraBoom == nullptr || DesiredArmLength <= UE_SMALL_NUMBER)
	{
		return DesiredArmLength;
	}

	const FVector DesiredCameraLocation = BoomOrigin - WorldRotation.Vector() * DesiredArmLength;
	const float ProbeRadius = FMath::Max(1.0f, CameraBoom->ProbeSize);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterCameraObstructionProbe), false, this);
	TArray<FHitResult> Hits;
	GetWorld()->SweepMultiByChannel(
		Hits,
		BoomOrigin,
		DesiredCameraLocation,
		FQuat::Identity,
		ECC_Camera,
		FCollisionShape::MakeSphere(ProbeRadius),
		QueryParams);

	Hits.Sort([](const FHitResult& Left, const FHitResult& Right)
	{
		return Left.Distance < Right.Distance;
	});

	const FHitResult* BuildingHit = nullptr;
	for (const FHitResult& Hit : Hits)
	{
		if (!Hit.bBlockingHit)
		{
			continue;
		}

		const ASimCity2000CityActor* CityActor = Cast<ASimCity2000CityActor>(Hit.GetActor());
		if (CityActor == nullptr || !CityActor->IsBuildingCollisionHit(Hit.GetComponent(), Hit.ImpactPoint))
		{
			continue;
		}

		BuildingHit = &Hit;
		break;
	}

	if (BuildingHit == nullptr)
	{
		return DesiredArmLength;
	}

	const float SafeDistance = FMath::Max(
		CameraMinObstructedArmLengthCm,
		BuildingHit->Distance - ProbeRadius - CameraObstructionPaddingCm);
	return FMath::Min(DesiredArmLength, SafeDistance);
}

bool ASimCopterHelicopterPawn::ProbeBucketWater(const FVector& BucketWorldLocation)
{
	ASimCity2000CityActor* City = ResolveCityActor();
	if (City == nullptr)
	{
		return false;
	}

	float SurfaceWorldZ = 0.0f;
	uint8 TerrainClass = 0xff;
	return City->TryGetWaterGameplaySurface(
			BucketWorldLocation,
			SurfaceWorldZ,
			TerrainClass) &&
		SimCopterWaterGameplay::IsWaterTerrainClass(TerrainClass);
}

FString ASimCopterHelicopterPawn::ResolveOriginalGameRoot() const
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

void ASimCopterHelicopterPawn::ApplyDerivedTuning()
{
	// The original's airspeed equals the smoothed pitch in tenth-degrees; each
	// unit of speed moves 40000/65536 world units per second (FUN_00486e90).
	MaxForwardSpeedCmPerSec = FMath::Max(
		1.0f,
		HelicopterTuning.MaxPitchDeg * 10.0f * (40000.0f / 65536.0f) * OriginalUnitToCm);
	ApplyFlightTuningToModel();
}
