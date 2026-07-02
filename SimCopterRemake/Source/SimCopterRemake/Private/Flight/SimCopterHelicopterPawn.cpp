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
	SeedFlightModelFromActor();
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

	// The rope load rides along in the original weight budget (person + cargo
	// pounds against seats*120 + MaxLoad + 30).
	FlightModel.LoadPounds = bRopeDeployed ? FMath::RoundToInt(BucketWaterFraction * static_cast<float>(HelicopterTuning.MaxLoadPounds)) : 0;

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
		bEngineRunning = false;
		SeedFlightModelFromActor();
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

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterFlightSurface), false, this);
	const FVector Start(Location.X, Location.Y, Location.Z + 20000.0f);
	const FVector End(Location.X, Location.Y, Location.Z - 30000.0f);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams) && Hit.bBlockingHit)
	{
		const int32 SurfaceHeight = SimCopterFixed::FromFloat(Hit.ImpactPoint.Z / Unit);
		Environment.TerrainHeight = SurfaceHeight;
		Environment.SurfaceHeight = SurfaceHeight;
		Environment.bTerrainFlat = Hit.ImpactNormal.Z >= LandingFlatNormalZ;

		const bool bWater =
			(Hit.GetActor() != nullptr && NameSuggestsWater(Hit.GetActor()->GetFName())) ||
			(Hit.GetComponent() != nullptr && NameSuggestsWater(Hit.GetComponent()->GetFName())) ||
			Hit.ImpactPoint.Z <= WaterFillWorldZ + 5.0f;
		if (bWater)
		{
			// Original: tile class < 10 with nothing built - splash bounce, no
			// landing (FUN_00487160 clears the flat flag on those tiles).
			Environment.bHostileSurface = true;
			Environment.bTerrainFlat = false;
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
	// The original's airspeed equals the smoothed pitch in tenth-degrees; each
	// unit of speed moves 40000/65536 world units per second (FUN_00486e90).
	MaxForwardSpeedCmPerSec = FMath::Max(
		1.0f,
		HelicopterTuning.MaxPitchDeg * 10.0f * (40000.0f / 65536.0f) * OriginalUnitToCm);
	RopeLengthCm = FMath::Clamp(RopeLengthCm, MinRopeLengthCm, MaxRopeLengthCm);
	ApplyFlightTuningToModel();
}
