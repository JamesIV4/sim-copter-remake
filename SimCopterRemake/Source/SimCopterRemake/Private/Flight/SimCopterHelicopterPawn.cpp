// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/SimCopterHelicopterPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Formats/MaxisMeshLibrary.h"
#include "Formats/MaxisProceduralMeshBuilder.h"
#include "Formats/SimCopterTweakReader.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Ground/SimCopterOnFootPawn.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterHelicopterPawn, Log, All);

namespace
{
constexpr float MaxSubstepSeconds = 1.0f / 60.0f;
constexpr float MaxTickSeconds = 0.1f;
constexpr float MinImpactDamageSpeedCmPerSec = 260.0f;
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

bool NameSuggestsWater(const FName Name)
{
	const FString StringName = Name.ToString();
	return StringName.Contains(TEXT("Water"), ESearchCase::IgnoreCase);
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

	BucketMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Bucket"));
	BucketMeshComponent->SetupAttachment(CollisionComponent);
	BucketMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BucketMeshComponent->SetCanEverAffectNavigation(false);
	BucketMeshComponent->SetRelativeScale3D(FVector(0.28f, 0.28f, 0.22f));

	SearchLightComponent = CreateDefaultSubobject<USpotLightComponent>(TEXT("SearchLight"));
	SearchLightComponent->SetupAttachment(ModelPivot);
	SearchLightComponent->SetRelativeLocation(FVector(95.0f, 0.0f, -35.0f));
	SearchLightComponent->SetRelativeRotation(FRotator(-35.0f, 0.0f, 0.0f));
	SearchLightComponent->Intensity = 22000.0f;
	SearchLightComponent->AttenuationRadius = 4200.0f;
	SearchLightComponent->InnerConeAngle = 8.0f;
	SearchLightComponent->OuterConeAngle = 20.0f;
	SearchLightComponent->SetVisibility(false);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(CollisionComponent);
	CameraBoom->TargetArmLength = 900.0f;
	CameraBoom->TargetOffset = FVector(0.0f, 0.0f, ChaseCameraTargetHeightCm);
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->ProbeSize = 18.0f;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 9.5f;
	CameraBoom->bEnableCameraRotationLag = true;
	CameraBoom->CameraRotationLagSpeed = 8.0f;
	CameraBoom->SetRelativeRotation(FRotator(-16.0f, 0.0f, 0.0f));

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
	}

	// Lit vertex-colour material shared with the city renderer so the palette-coloured
	// helicopter responds to scene lighting instead of rendering fullbright.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ModelMaterialFinder(TEXT("/Game/Materials/M_SimCopterLitVertexColor.M_SimCopterLitVertexColor"));
	if (ModelMaterialFinder.Succeeded())
	{
		ModelVertexColorMaterial = ModelMaterialFinder.Object;
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
	}

	UpdateGroundProbe();
	UpdateForwardProbe();
	UpdateRopeAndBucket(0.0f);
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
	PlayerInputComponent->BindAction(TEXT("SimCopterBucketFill"), IE_Pressed, this, &ASimCopterHelicopterPawn::StartBucketFill);
	PlayerInputComponent->BindAction(TEXT("SimCopterBucketFill"), IE_Released, this, &ASimCopterHelicopterPawn::StopBucketFill);
	PlayerInputComponent->BindAction(TEXT("SimCopterBucketDump"), IE_Pressed, this, &ASimCopterHelicopterPawn::StartBucketDump);
	PlayerInputComponent->BindAction(TEXT("SimCopterBucketDump"), IE_Released, this, &ASimCopterHelicopterPawn::StopBucketDump);
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
		float RawFillRate = RopeTuning.BucketFillPerSec * 100.0f;
		if (ReadControlValue(*RopeSection, TEXT("Bucket Fill Rate"), RawFillRate))
		{
			RopeTuning.BucketFillPerSec = RawFillRate / 100.0f;
		}
		float RawDumpRate = RopeTuning.BucketDumpPerSec * 100.0f;
		if (ReadControlValue(*RopeSection, TEXT("Bucket Dump Rate"), RawDumpRate))
		{
			RopeTuning.BucketDumpPerSec = RawDumpRate / 100.0f;
		}
		ReadFloatControl(*RopeSection, TEXT("Rope Load Factor"), RopeTuning.RopeLoadFactor);
		ReadFloatControl(*RopeSection, TEXT("Rope Tension"), RopeTuning.RopeTension);
		float RawWaterThrow = RopeTuning.WaterThrowCmPerSec / TweakClimbToCmPerSec;
		if (ReadControlValue(*RopeSection, TEXT("Water Throw"), RawWaterThrow))
		{
			RopeTuning.WaterThrowCmPerSec = RawWaterThrow * TweakClimbToCmPerSec;
		}
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
	// type 11), so the spinning disc reads as a faint grey haze instead of a solid plate.
	auto BuildRotorMesh = [this, &FallbackColor](UProceduralMeshComponent* Component, const FMaxisMeshObject& Object, const TArray<FColor>* ColorMap)
	{
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
			Component->CreateMeshSection_LinearColor(SectionIndex, DiscSection.Vertices, DiscSection.Triangles, DiscSection.Normals, DiscSection.UVs, DiscSection.VertexColors, DiscSection.Tangents, false);
			UMaterialInterface* const DiscMaterial = RotorDiscMaterial != nullptr ? RotorDiscMaterial.Get() : ModelVertexColorMaterial.Get();
			if (DiscMaterial != nullptr)
			{
				Component->SetMaterial(SectionIndex, DiscMaterial);
			}
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

	// Sit the lowest fuselage vertex at the bottom of the collision capsule so the skids
	// rest near the ground contact point the flight probes use.
	const float CapsuleHalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	const float VerticalOffset = -CapsuleHalfHeight - BodySection.LocalBounds.Min.Z;
	HeliBodyMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, VerticalOffset));

	// Main rotor: authored around the mast at local X=Y=0, so spinning the component about
	// its own Z axis at the body origin sweeps the blades correctly.
	const TArray<FColor>* MainRotorColorMap = nullptr;
	const FMaxisMeshObject* MainRotorObject = MeshLibrary.FindObjectByTableName(MainRotorName, &MainRotorColorMap);
	if (MainRotorObject != nullptr && BuildRotorMesh(HeliMainRotorMeshComponent, *MainRotorObject, MainRotorColorMap))
	{
		HeliMainRotorMeshComponent->SetRelativeLocation(FVector::ZeroVector);
	}

	// Tail rotor: the shared ROTORTL object is authored centred on its own hub, so place it
	// near the rear tip of the fuselage and spin it about the lateral (Y) axis.
	HeliTailRotorMeshComponent->ClearAllMeshSections();
	if (bShowSeparateTailRotor)
	{
		const TArray<FColor>* TailRotorColorMap = nullptr;
		const FMaxisMeshObject* TailRotorObject = MeshLibrary.FindObjectByTableName(TEXT("ROTORTL"), &TailRotorColorMap);
		if (TailRotorObject != nullptr && BuildRotorMesh(HeliTailRotorMeshComponent, *TailRotorObject, TailRotorColorMap))
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
		TEXT("Loaded SimCopter helicopter model '%s' (body '%s', rotor '%s') from '%s'."),
		*HelicopterTypeName,
		*BodyName,
		*MainRotorName,
		*RootPath);
	return true;
}

void ASimCopterHelicopterPawn::ResetAircraft()
{
	VelocityCmPerSec = FVector::ZeroVector;
	CurrentPitchDeg = 0.0f;
	CurrentRollDeg = 0.0f;
	CurrentYawRateDegPerSec = 0.0f;
	bEngineRunning = false;
	bEngineStartHeld = false;
	bEngineShutdownHeld = false;
	EngineStartHoldElapsed = 0.0f;
	EngineShutdownHoldElapsed = 0.0f;
	EngineStartHoldAlpha = 0.0f;
	EngineShutdownHoldAlpha = 0.0f;
	CurrentDamage = 0.0f;
	CurrentFuelGallons = HelicopterTuning.FuelGallons;
	BucketWaterFraction = 0.0f;
	bIsLanded = false;
	SetActorRotation(FRotator(0.0f, GetActorRotation().Yaw, 0.0f));
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
}

bool ASimCopterHelicopterPawn::CanExitHelicopter() const
{
	return bIsLanded && !bEngineRunning && GroundClearanceCm <= GroundContactTolerance + 18.0f;
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
	bRopeDeployed = !bRopeDeployed;
}

void ASimCopterHelicopterPawn::StartBucketFill()
{
	bBucketFillHeld = true;
}

void ASimCopterHelicopterPawn::StopBucketFill()
{
	bBucketFillHeld = false;
}

void ASimCopterHelicopterPawn::StartBucketDump()
{
	bBucketDumpHeld = true;
}

void ASimCopterHelicopterPawn::StopBucketDump()
{
	bBucketDumpHeld = false;
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
			CurrentYawRateDegPerSec = 0.0f;
			CollectiveInput = 0.0f;
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

	const bool bFlyable = bEngineRunning && CurrentFuelGallons > 0.01f && CurrentDamage < static_cast<float>(HelicopterTuning.MaxDamage);
	const float EffectivePitchInput = bFlyable ? PitchInput : 0.0f;
	const float EffectiveRollInput = bFlyable ? RollInput : 0.0f;
	const float EffectiveYawInput = bFlyable ? YawInput : 0.0f;
	const float EffectiveCollectiveInput = bFlyable ? CollectiveInput : (bIsLanded ? 0.0f : -0.22f);

	const float TargetPitchDeg = -EffectivePitchInput * HelicopterTuning.MaxPitchDeg;
	const float TargetRollDeg = EffectiveRollInput * HelicopterTuning.MaxBankDeg;
	CurrentPitchDeg = FMath::FInterpConstantTo(CurrentPitchDeg, TargetPitchDeg, DeltaSeconds, FMath::Max(1.0f, HelicopterTuning.PitchRateDegPerSec));
	CurrentRollDeg = FMath::FInterpConstantTo(CurrentRollDeg, TargetRollDeg, DeltaSeconds, FMath::Max(1.0f, HelicopterTuning.RollRateDegPerSec));

	const float TargetYawRate = EffectiveYawInput * HelicopterTuning.MaxYawRateDegPerSec;
	CurrentYawRateDegPerSec = FMath::FInterpConstantTo(
		CurrentYawRateDegPerSec,
		TargetYawRate,
		DeltaSeconds,
		FMath::Max(20.0f, HelicopterTuning.YawAccelDegPerSec * 3.0f));

	FRotator DesiredRotation = GetActorRotation();
	DesiredRotation.Pitch = 0.0f;
	DesiredRotation.Roll = 0.0f;
	DesiredRotation.Yaw += CurrentYawRateDegPerSec * DeltaSeconds;

	const FRotationMatrix DesiredYawFrame(FRotator(0.0f, DesiredRotation.Yaw, 0.0f));
	const FVector Forward = DesiredYawFrame.GetUnitAxis(EAxis::X);
	const FVector Right = DesiredYawFrame.GetUnitAxis(EAxis::Y);
	const float ForwardSpeedLimit = EffectivePitchInput >= 0.0f ? MaxForwardSpeedCmPerSec : MaxForwardSpeedCmPerSec * MaxReverseSpeedFraction;
	const FVector DesiredHorizontalVelocity =
		Forward * (EffectivePitchInput * ForwardSpeedLimit) +
		Right * (EffectiveRollInput * MaxSlideSpeedCmPerSec);

	const float LiftLoad = bRopeDeployed ? BucketWaterFraction * (RopeTuning.RopeLoadFactor / 100.0f) : 0.0f;
	const float LoadMultiplier = FMath::Clamp(1.0f - LiftLoad * 0.32f, 0.45f, 1.0f);
	const float HorizontalResponse = FMath::Max(1.5f, HelicopterTuning.SlideResponse / 24.0f);
	const FVector HorizontalVelocity = FVector(VelocityCmPerSec.X, VelocityCmPerSec.Y, 0.0f);
	const FVector NewHorizontalVelocity = FMath::VInterpTo(HorizontalVelocity, DesiredHorizontalVelocity * LoadMultiplier, DeltaSeconds, HorizontalResponse);

	float TargetVerticalVelocity = EffectiveCollectiveInput * HelicopterTuning.ClimbRateCmPerSec * LoadMultiplier;
	if (TargetVerticalVelocity < 0.0f)
	{
		TargetVerticalVelocity *= 1.15f;
	}
	if (bIsLanded && EffectiveCollectiveInput <= 0.12f)
	{
		TargetVerticalVelocity = 0.0f;
	}
	VelocityCmPerSec = FVector(NewHorizontalVelocity.X, NewHorizontalVelocity.Y, FMath::FInterpTo(VelocityCmPerSec.Z, TargetVerticalVelocity, DeltaSeconds, HoverDamping));

	if (bIsLanded)
	{
		VelocityCmPerSec.X = FMath::FInterpTo(VelocityCmPerSec.X, 0.0f, DeltaSeconds, 8.0f);
		VelocityCmPerSec.Y = FMath::FInterpTo(VelocityCmPerSec.Y, 0.0f, DeltaSeconds, 8.0f);
	}

	MoveWithCollision(VelocityCmPerSec * DeltaSeconds, DesiredRotation, DeltaSeconds);
	UpdateGroundProbe();
	UpdateForwardProbe();
	UpdateLandingState(DeltaSeconds);
	UpdateFuel(DeltaSeconds);
	UpdateRopeAndBucket(DeltaSeconds);
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

void ASimCopterHelicopterPawn::UpdateLandingState(float DeltaSeconds)
{
	if (!LastGroundHit.bBlockingHit || CollisionComponent == nullptr)
	{
		bIsLanded = false;
		return;
	}

	const bool bTouchingSurface = GroundClearanceCm <= GroundContactTolerance;
	const bool bSurfaceCanLand = FVector::DotProduct(LastGroundHit.ImpactNormal, FVector::UpVector) >= 0.62f;
	const float HorizontalSpeed = FVector(VelocityCmPerSec.X, VelocityCmPerSec.Y, 0.0f).Size();
	const float VerticalSpeed = FMath::Abs(VelocityCmPerSec.Z);
	const float DescentSpeed = FMath::Max(0.0f, -VelocityCmPerSec.Z);
	const bool bAttitudeSafe =
		FMath::Abs(CurrentPitchDeg) <= LandingTuning.MaxPitchDeg &&
		FMath::Abs(CurrentRollDeg) <= LandingTuning.MaxRollDeg;
	const bool bSpeedSafe =
		HorizontalSpeed <= LandingTuning.MaxHorizontalSpeedCmPerSec &&
		VerticalSpeed <= LandingTuning.MaxVerticalSpeedCmPerSec &&
		DescentSpeed <= LandingTuning.MaxDescentRateCmPerSec;

	if (bTouchingSurface && bSurfaceCanLand && bAttitudeSafe && bSpeedSafe)
	{
		bIsLanded = CollectiveInput <= 0.15f;
		if (bIsLanded)
		{
			const float CapsuleHalfHeight = CollisionComponent->GetScaledCapsuleHalfHeight();
			const FVector CurrentLocation = GetActorLocation();
			const FVector LandedLocation(CurrentLocation.X, CurrentLocation.Y, LastGroundHit.ImpactPoint.Z + CapsuleHalfHeight + 2.0f);
			SetActorLocation(LandedLocation, false);
			VelocityCmPerSec.Z = 0.0f;
			CurrentPitchDeg = FMath::FInterpTo(CurrentPitchDeg, 0.0f, DeltaSeconds, 6.0f);
			CurrentRollDeg = FMath::FInterpTo(CurrentRollDeg, 0.0f, DeltaSeconds, 6.0f);
		}
	}
	else if (bIsLanded && CollectiveInput > 0.2f)
	{
		bIsLanded = false;
	}
	else if (!bTouchingSurface)
	{
		bIsLanded = false;
	}
}

void ASimCopterHelicopterPawn::MoveWithCollision(const FVector& DeltaLocation, const FRotator& DesiredRotation, float DeltaSeconds)
{
	if (RootComponent == nullptr)
	{
		return;
	}

	FHitResult Hit;
	RootComponent->MoveComponent(DeltaLocation, DesiredRotation.Quaternion(), true, &Hit);
	if (Hit.IsValidBlockingHit())
	{
		HandleBlockingHit(Hit, DeltaSeconds);
		const FVector RemainingDelta = DeltaLocation * (1.0f - Hit.Time);
		const FVector SlideDelta = FVector::VectorPlaneProject(RemainingDelta, Hit.Normal) * 0.55f;
		FHitResult SlideHit;
		RootComponent->MoveComponent(SlideDelta, DesiredRotation.Quaternion(), true, &SlideHit);
		if (SlideHit.IsValidBlockingHit())
		{
			HandleBlockingHit(SlideHit, DeltaSeconds);
		}
	}
}

void ASimCopterHelicopterPawn::HandleBlockingHit(const FHitResult& Hit, float DeltaSeconds)
{
	const float IntoSurfaceSpeed = FMath::Max(0.0f, -FVector::DotProduct(VelocityCmPerSec, Hit.Normal));
	if (IntoSurfaceSpeed > MinImpactDamageSpeedCmPerSec && DeltaSeconds > 0.0f)
	{
		const float DamageAmount = ((IntoSurfaceSpeed - MinImpactDamageSpeedCmPerSec) / 500.0f) * DamageTuning.CollisionDamageScale;
		CurrentDamage = FMath::Clamp(CurrentDamage + DamageAmount, 0.0f, static_cast<float>(HelicopterTuning.MaxDamage));
	}

	VelocityCmPerSec = FVector::VectorPlaneProject(VelocityCmPerSec, Hit.Normal) * 0.42f;
	if (Hit.Normal.Z > 0.5f && VelocityCmPerSec.Z < 0.0f)
	{
		VelocityCmPerSec.Z = 0.0f;
	}
}

void ASimCopterHelicopterPawn::UpdateFuel(float DeltaSeconds)
{
	if (!bEngineRunning)
	{
		return;
	}

	if (CurrentFuelGallons <= 0.0f)
	{
		CurrentFuelGallons = 0.0f;
		return;
	}

	const float InputLoad = FMath::Clamp(
		FMath::Max(FMath::Abs(PitchInput), FMath::Max(FMath::Abs(RollInput), FMath::Max(FMath::Abs(YawInput), FMath::Abs(CollectiveInput)))),
		0.0f,
		1.0f);
	const float BurnMultiplier = 0.35f + InputLoad * 0.65f;
	CurrentFuelGallons = FMath::Max(0.0f, CurrentFuelGallons - (HelicopterTuning.FuelRateGallonsPerHour / 3600.0f) * BurnMultiplier * DeltaSeconds);
}

void ASimCopterHelicopterPawn::UpdateRopeAndBucket(float DeltaSeconds)
{
	if (bRopeDeployed)
	{
		RopeLengthCm = FMath::Clamp(RopeLengthCm + RopeAdjustInput * RopeAdjustCmPerSec * DeltaSeconds, MinRopeLengthCm, MaxRopeLengthCm);

		if (bBucketFillHeld && ProbeBucketWater(BucketMeshComponent != nullptr ? BucketMeshComponent->GetComponentLocation() : GetActorLocation()))
		{
			BucketWaterFraction = FMath::Clamp(BucketWaterFraction + RopeTuning.BucketFillPerSec * DeltaSeconds, 0.0f, 1.0f);
		}
	}
	else
	{
		bBucketFillHeld = false;
	}

	if (bBucketDumpHeld)
	{
		BucketWaterFraction = FMath::Clamp(BucketWaterFraction - RopeTuning.BucketDumpPerSec * DeltaSeconds, 0.0f, 1.0f);
	}

	if (RopeMeshComponent != nullptr)
	{
		RopeMeshComponent->SetVisibility(bRopeDeployed);
		RopeMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -RopeLengthCm * 0.5f));
		RopeMeshComponent->SetRelativeScale3D(FVector(0.025f, 0.025f, RopeLengthCm / 100.0f));
	}
	if (BucketMeshComponent != nullptr)
	{
		BucketMeshComponent->SetVisibility(bRopeDeployed);
		BucketMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, -RopeLengthCm - 26.0f));
		const float BucketLoadScale = FMath::Lerp(0.8f, 1.05f, BucketWaterFraction);
		BucketMeshComponent->SetRelativeScale3D(FVector(0.28f, 0.28f, 0.22f * BucketLoadScale));
	}
}

void ASimCopterHelicopterPawn::UpdateVisuals(float DeltaSeconds)
{
	if (ModelPivot != nullptr)
	{
		ModelPivot->SetRelativeRotation(FRotator(CurrentPitchDeg, 0.0f, CurrentRollDeg));
	}

	const float RunningDegPerSec = MainRotorRevsPerSec * 360.0f;
	const float RotorSpeed = bEngineRunning
		? RunningDegPerSec
		: (bEngineStartHeld && CurrentFuelGallons > 0.0f ? RunningDegPerSec * 0.35f : 0.0f);
	RotorSpinDeg = FMath::Fmod(RotorSpinDeg + RotorSpeed * DeltaSeconds, 360.0f);

	// Main rotor spins about the vertical mast (yaw); tail rotor spins about the lateral
	// axis (pitch). Applied to both the placeholder and original-mesh rotors; only the
	// active set is visible.
	const FRotator MainRotorRotation(0.0f, RotorSpinDeg, 0.0f);
	const FRotator TailRotorRotation(RotorSpinDeg * TailRotorSpeedMultiplier, 0.0f, 0.0f);

	if (MainRotorMeshComponent != nullptr)
	{
		MainRotorMeshComponent->SetRelativeRotation(MainRotorRotation);
	}
	if (TailRotorMeshComponent != nullptr)
	{
		TailRotorMeshComponent->SetRelativeRotation(TailRotorRotation);
	}
	if (HeliMainRotorMeshComponent != nullptr)
	{
		HeliMainRotorMeshComponent->SetRelativeRotation(MainRotorRotation);
	}
	if (HeliTailRotorMeshComponent != nullptr)
	{
		HeliTailRotorMeshComponent->SetRelativeRotation(TailRotorRotation);
	}
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
	CameraBoom->TargetArmLength = ArmLength;
	CameraBoom->TargetOffset = TargetOffset;
	CameraBoom->SetRelativeRotation(FRotator(FMath::Clamp(ViewPitch + CameraPitchOffsetDeg, -78.0f, 2.0f), RelativeYaw, 0.0f));
}

bool ASimCopterHelicopterPawn::ProbeBucketWater(const FVector& BucketWorldLocation) const
{
	if (GetWorld() == nullptr)
	{
		return false;
	}

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterBucketWaterProbe), false, this);
	const FVector Start = BucketWorldLocation + FVector::UpVector * 45.0f;
	const FVector End = BucketWorldLocation - FVector::UpVector * 120.0f;
	GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams);
	if (Hit.bBlockingHit)
	{
		const bool bNamedWater =
			(Hit.GetActor() != nullptr && NameSuggestsWater(Hit.GetActor()->GetFName())) ||
			(Hit.GetComponent() != nullptr && NameSuggestsWater(Hit.GetComponent()->GetFName()));
		return bNamedWater || Hit.ImpactPoint.Z <= WaterFillWorldZ + 50.0f;
	}

	return BucketWorldLocation.Z <= WaterFillWorldZ;
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
	MaxForwardSpeedCmPerSec = FMath::Max(1200.0f, 1200.0f + HelicopterTuning.MaxPitchDeg * 86.0f);
	MaxSlideSpeedCmPerSec = FMath::Max(650.0f, 350.0f + HelicopterTuning.MaxSlideDeg * 72.0f);
	HelicopterTuning.SlideResponse = FMath::Max(20.0f, HelicopterTuning.SlideResponse);
	HelicopterTuning.MaxYawRateDegPerSec = FMath::Max(5.0f, HelicopterTuning.MaxYawRateDegPerSec);
	LandingTuning.MaxHorizontalSpeedCmPerSec = FMath::Max(100.0f, LandingTuning.MaxHorizontalSpeedCmPerSec);
	LandingTuning.MaxVerticalSpeedCmPerSec = FMath::Max(100.0f, LandingTuning.MaxVerticalSpeedCmPerSec);
	LandingTuning.MaxDescentRateCmPerSec = FMath::Max(100.0f, LandingTuning.MaxDescentRateCmPerSec);
	RopeLengthCm = FMath::Clamp(RopeLengthCm, MinRopeLengthCm, MaxRopeLengthCm);
}
