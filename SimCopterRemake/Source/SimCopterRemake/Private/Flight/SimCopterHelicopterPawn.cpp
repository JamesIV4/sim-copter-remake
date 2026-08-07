// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/SimCopterHelicopterPawn.h"

#include "Audio/SimCopterAudioSubsystem.h"
#include "Audio/SimCopterRadio.h"
#include "Camera/CameraComponent.h"
#include "City/SimCopterAirport.h"
#include "City/SimCity2000CityActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
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
#include "Formats/SimCity2000Reader.h"
#include "Formats/SimCopterOriginalGamePaths.h"
#include "Formats/SimCopterTweakReader.h"
#include "Flight/SimCopterControllerInput.h"
#include "Flight/SimCopterHelicopterRegistry.h"
#include "Flight/SimCopterPreparedHelicopterModel.h"
#include "Flight/SimCopterTearGas.h"
#include "Flight/SimCopterWaterGameplay.h"
#include "Game/SimCopterCareerSubsystem.h"
#include "Game/SimCopterSettings.h"
#include "Game/SimCopterVehicleMaterialSubsystem.h"
#include "Debug/SSimCopterHelicopterDebugPanel.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Ground/SimCopterAmbientVehicles.h"
#include "Ground/SimCopterFlashingLights.h"
#include "Ground/SimCopterGroundAgent.h"
#include "Ground/SimCopterOnFootPawn.h"
#include "Ground/SimCopterParticleFX.h"
#include "Ground/SimCopterEffectFX.h"
#include "Ground/SimCopterInteraction.h"
#include "Ground/SimCopterApachePool.h"
#include "Ground/SimCopterPopulationSprite.h"
#include "Ground/SimCopterTearGasPool.h"
#include "EngineUtils.h"
#include "Ground/SimCopterTrafficSystemActor.h"
#include "HAL/FileManager.h"
#include "InputCoreTypes.h"
#include "Input/Reply.h"
#include "Missions/SimCopterMissionSystemActor.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/Archive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "UI/SimCopterHangarArt.h"
#include "UI/SSimCopterControllerOverlay.h"
#include "UI/SSimCopterDashboard.h"
#include "UI/SSimCopterMapPanel.h"
#include "UI/SSimCopterCheckupMenu.h"
#include "UI/SSimCopterToolFlaps.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterHelicopterPawn, Log, All);

namespace
{
constexpr float MaxSubstepSeconds = 1.0f / 60.0f;
constexpr float MaxTickSeconds = 0.1f;
constexpr int32 MaxTweakControls = 64;
constexpr TCHAR CameraDebugConfigSection[] = TEXT("SimCopter.CameraViews");
constexpr TCHAR RotorDiscConfigSection[] = TEXT("SimCopter.RotorDisc");
constexpr TCHAR CockpitViewConfigSection[] = TEXT("SimCopter.CockpitView");
constexpr TCHAR FlightModelConfigSection[] = TEXT("SimCopter.FlightModel");
constexpr TCHAR CameraGroundLiftConfigSection[] = TEXT("SimCopter.CameraGroundLift");
constexpr uint32 AircraftRuntimeSaveMagic = 0x48454c49; // 'HELI'
constexpr int32 AircraftRuntimeSaveVersion = 1;

void SerializeArchiveBool(FArchive& Archive, bool& Value)
{
	uint8 Byte = Value ? 1 : 0;
	Archive << Byte;
	if (Archive.IsLoading())
	{
		Value = Byte != 0;
	}
}

void SerializeFlightTuning(FArchive& Archive, FSimCopterFlightTuning& Tuning)
{
	Archive << Tuning.MaxBank << Tuning.MaxSlide << Tuning.MaxPitch << Tuning.PitchRate;
	Archive << Tuning.YawRate << Tuning.RollRate << Tuning.SlideRate << Tuning.ClimbRate;
	Archive << Tuning.MaxLoadPounds << Tuning.MaxYawRate << Tuning.FuelRateGalPerHour;
	Archive << Tuning.MaxDamage << Tuning.FuelGallons << Tuning.PassengerSeats;
	SerializeArchiveBool(Archive, Tuning.bNoTailRotor);
	Archive << Tuning.LandMaxPitch << Tuning.LandMaxSlide << Tuning.LandMaxSpeed;
	Archive << Tuning.LandMaxYSpeed << Tuning.MaxDescentRate << Tuning.MinFireAlt;
	Archive << Tuning.MaxFireAlt << Tuning.CollisionSubtract;
}

void SerializeFlightModel(FArchive& Archive, FSimCopterFlightModel& Model)
{
	SerializeFlightTuning(Archive, Model.Tuning);
	SerializeArchiveBool(Archive, Model.bEasyFlightModel);
	uint8 State = static_cast<uint8>(Model.State);
	Archive << State;
	if (Archive.IsLoading()) Model.State = static_cast<ESimCopterFlightState>(State);
	Archive << Model.PosX << Model.Altitude << Model.PosZ << Model.Heading;
	Archive << Model.PitchTarget << Model.PitchSmoothed << Model.BankTarget << Model.BankSmoothed;
	Archive << Model.SlideTarget << Model.SlideSmoothed << Model.YawRateTarget << Model.YawRateSmoothed;
	Archive << Model.ForwardSpeed << Model.ClimbSpeed << Model.SpeedDelta << Model.HorizontalSpeed;
	Archive << Model.VelX << Model.VelZ << Model.DeltaX << Model.DeltaZ << Model.AboveGround;
	Archive << Model.RotorSpeed << Model.MainRotorAngle << Model.TailRotorAngle;
	SerializeArchiveBool(Archive, Model.bRotorBlurDisc);
	Archive << Model.HitPoints << Model.Fuel << Model.FlightSeconds << Model.LoadPounds;
	Archive << Model.Passengers << Model.LoadFactor << Model.BounceTimer << Model.DyingTimer;
	for (int32& Value : Model.TurbPitchSamples) Archive << Value;
	for (int32& Value : Model.TurbSlideSamples) Archive << Value;
	for (int32& Value : Model.TurbYawSamples) Archive << Value;
	Archive << Model.TurbPitch << Model.TurbSlide << Model.TurbYaw;
	Archive << Model.TurbPitchPrev << Model.TurbSlidePrev << Model.TurbYawPrev;
	Archive << Model.TurbPitchNext << Model.TurbSlideNext << Model.TurbYawNext;
	Archive << Model.TurbulenceClock << Model.TurbulenceFrameSeconds;
	Archive << Model.ReferenceFrameSeconds << Model.SpeedChaseFrameSeconds;
	Archive << Model.RotorVisualMultiplier << Model.FireDamageAccrued << Model.RandState;
}
// Parameters authored by Tools/Unreal/CreateSimCopterMaterials.py.
const FName RotorDiscOpacityParameterName(TEXT("DiscOpacity"));
const FName RotorDiscColorParameterName(TEXT("DiscColor"));
constexpr double MaxCameraDebugTranslationCm = 10000.0;
constexpr float MaxCameraZoomFramingStrength = 2.0f;
constexpr float MaxCameraZoomDistanceCm = 10000.0f;
const FName CrosshairScreenLayerName(TEXT("SimCopterCrosshairLayer"));

// Cross-section of a winch rope segment. Halved from 0.04 - the cable the original draws is a
// thin line, and both the bucket's and the harness's read as pipes at the old size.
constexpr float RopeSegmentScale = 0.02f;

// The engine's basic cylinder is 100 units across, so the drawn cable's radius is half of that
// times the scale above. Used to stand the harness anchor off by one rope width.
constexpr float RopeRadiusCm = 50.0f * RopeSegmentScale;

// FUN_004af100's `4 << (param5 & 0x1f)`: both helicopter impact arms pass 0x80000001, so the
// column is scale 1 (an 8-unit burst), not the scale-4 splash a ditching throws.
constexpr int32 ImpactColumnScaleExponent = 1;

int32 GetCameraModeIndex(ESimCopterCameraMode Mode)
{
	switch (Mode)
	{
	case ESimCopterCameraMode::Chase: return 0;
	case ESimCopterCameraMode::Orbit: return 1;
	case ESimCopterCameraMode::Rescue: return 2;
	case ESimCopterCameraMode::Cockpit: return 3;
	default: return 0;
	}
}


// The cockpit view rides at the pilot's eye instead of on a boom, which changes how the
// framing, zoom and collision handling below apply.
bool CameraModeIsFirstPerson(ESimCopterCameraMode Mode)
{
	return Mode == ESimCopterCameraMode::Cockpit;
}

const TCHAR* GetCameraModeConfigName(ESimCopterCameraMode Mode)
{
	switch (Mode)
	{
	case ESimCopterCameraMode::Chase: return TEXT("Chase");
	case ESimCopterCameraMode::Orbit: return TEXT("Orbit");
	case ESimCopterCameraMode::Rescue: return TEXT("Rescue");
	case ESimCopterCameraMode::Cockpit: return TEXT("Cockpit");
	default: return TEXT("Chase");
	}
}

FSimCopterCameraViewDebugOffset GetDefaultCameraViewDebugOffset(ESimCopterCameraMode Mode)
{
	FSimCopterCameraViewDebugOffset Offset;
	switch (Mode)
	{
	case ESimCopterCameraMode::Chase:
		Offset.TranslationCm = FVector(197.0, 0.0, -126.0);
		Offset.RotationDeg = FRotator(-6.0, 0.0, 0.0);
		break;
	case ESimCopterCameraMode::Orbit:
		Offset.TranslationCm = FVector(124.0, 0.0, -234.0);
		Offset.RotationDeg = FRotator(3.5, 0.0, 0.0);
		break;
	case ESimCopterCameraMode::Rescue:
		Offset.TranslationCm = FVector(-59.0, 0.0, -440.0);
		Offset.RotationDeg = FRotator(-24.5, 0.0, 0.0);
		break;
	case ESimCopterCameraMode::Cockpit:
		// The pilot's seat, measured off CameraAnchor (which sits on the cabin roof). 60 cm
		// down puts the eye level with the cabin; the CANNON object occupies X 27..58 at
		// Z ~8 in the same body frame, about 69 cm below the roof, so from here it sits just
		// under the crosshair. Tune from the debug panel's POSITION CM row.
		Offset.TranslationCm = FVector(10.0, 0.0, -60.0);
		// Level with the nose: the crosshair then marks the model's forward axis, which is the
		// direction every tool fires along (EmitWaterCannonFrame and friends).
		Offset.RotationDeg = FRotator::ZeroRotator;
		// Framing compensation swings the eye point around when you look; a seat should stay
		// put, so the cockpit opts out.
		Offset.ZoomVerticalFramingStrength = 0.0f;
		break;
	default:
		break;
	}
	return Offset;
}

FString MakeCameraDebugConfigKey(ESimCopterCameraMode Mode, const TCHAR* Suffix)
{
	return FString::Printf(TEXT("%s.%s"), GetCameraModeConfigName(Mode), Suffix);
}

double SanitizeCameraDebugTranslation(double Value)
{
	return FMath::IsFinite(Value)
		? FMath::Clamp(Value, -MaxCameraDebugTranslationCm, MaxCameraDebugTranslationCm)
		: 0.0;
}

double SanitizeCameraDebugRotation(double Value)
{
	return FMath::IsFinite(Value) ? FRotator::NormalizeAxis(Value) : 0.0;
}

FVector SanitizeCameraDebugTranslation(const FVector& TranslationCm)
{
	return FVector(
		SanitizeCameraDebugTranslation(TranslationCm.X),
		SanitizeCameraDebugTranslation(TranslationCm.Y),
		SanitizeCameraDebugTranslation(TranslationCm.Z));
}

bool ResolveCannonBarrelTipLocal(
	const FMaxisMeshSection& CannonSection,
	FVector& OutTipLocalCm)
{
	OutTipLocalCm = FVector::ZeroVector;
	if (CannonSection.IsEmpty() || !CannonSection.LocalBounds.IsValid)
	{
		return false;
	}

	// CANNON is authored down +X. Average the forward-most vertex ring rather than using the
	// complete bounds centre, because the rear mount is wider than the barrel mouth.
	const double MaxX = CannonSection.LocalBounds.Max.X;
	const double TipToleranceCm = FMath::Max(
		0.25,
		static_cast<double>(CannonSection.LocalBounds.GetSize().X) * 0.02);
	FVector TipSum = FVector::ZeroVector;
	int32 TipVertexCount = 0;
	for (const FVector& Vertex : CannonSection.Vertices)
	{
		if (Vertex.X >= MaxX - TipToleranceCm)
		{
			TipSum += Vertex;
			++TipVertexCount;
		}
	}
	if (TipVertexCount <= 0)
	{
		return false;
	}

	OutTipLocalCm = TipSum / static_cast<double>(TipVertexCount);
	OutTipLocalCm.X = MaxX;
	return true;
}

// Where the harness cable leaves the airframe, in the shared body/bracket local frame.
//
// BRACKET is one GEO used by all nine helicopters, so its own tip is only far enough out for the
// small ones - on a Bell 212 or a Dauphin the cable ends up inside the fuselage. Sample both: the
// bracket gives the height and the fore-aft station, and whichever of the bracket tip or the
// BODY's own widest point is further out gives the side, so the rope always clears the hull.
bool ResolveHarnessRopeAnchorLocal(
	const FMaxisMeshSection& BracketSection,
	const FMaxisMeshSection& BodySection,
	const float CableRadiusCm,
	FVector& OutAnchorLocalCm)
{
	OutAnchorLocalCm = FVector::ZeroVector;
	if (BracketSection.IsEmpty() || !BracketSection.LocalBounds.IsValid)
	{
		return false;
	}

	const FBox& Bounds = BracketSection.LocalBounds;
	const bool bOutboardIsPositiveY = FMath::Abs(Bounds.Max.Y) >= FMath::Abs(Bounds.Min.Y);
	const double BracketOutboardY = bOutboardIsPositiveY ? Bounds.Max.Y : Bounds.Min.Y;
	const double ToleranceCm = FMath::Max(0.25, static_cast<double>(Bounds.GetSize().Y) * 0.05);

	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	double LowestZ = TNumericLimits<double>::Max();
	for (const FVector& Vertex : BracketSection.Vertices)
	{
		if (FMath::Abs(Vertex.Y - BracketOutboardY) > ToleranceCm)
		{
			continue;
		}
		Sum += Vertex;
		++Count;
		LowestZ = FMath::Min(LowestZ, Vertex.Z);
	}
	if (Count == 0)
	{
		return false;
	}

	OutAnchorLocalCm = Sum / static_cast<double>(Count);
	OutAnchorLocalCm.Z = LowestZ;

	// The hull's own half-width on the bracket's side. Sampled at the bracket's height rather
	// than from the whole-body bounds, because the widest part of a fuselage is not always level
	// with the door.
	double HullOutboardY = 0.0;
	if (BodySection.LocalBounds.IsValid)
	{
		const double BandHalfHeightCm =
			FMath::Max(8.0, static_cast<double>(Bounds.GetSize().Z) * 0.5);
		for (const FVector& Vertex : BodySection.Vertices)
		{
			if (FMath::Abs(Vertex.Z - OutAnchorLocalCm.Z) > BandHalfHeightCm)
			{
				continue;
			}
			HullOutboardY = FMath::Max(HullOutboardY, FMath::Abs(Vertex.Y));
		}
		// Nothing at that height (a very shallow hull): fall back to the widest point there is.
		if (HullOutboardY <= 0.0)
		{
			HullOutboardY = FMath::Max(
				FMath::Abs(BodySection.LocalBounds.Max.Y),
				FMath::Abs(BodySection.LocalBounds.Min.Y));
		}
	}

	// Stand off inboard by the rope's full DIAMETER, not its radius. Half a rope's worth put the
	// cable's outboard edge on the anchor line, which still read as hanging off the end of the
	// frame; a whole width tucks it against the inside of the tip, which is where a winch cable
	// would actually run.
	const double OutboardMagnitude =
		FMath::Max(FMath::Abs(BracketOutboardY), HullOutboardY) - CableRadiusCm * 2.0;
	OutAnchorLocalCm.Y = bOutboardIsPositiveY ? OutboardMagnitude : -OutboardMagnitude;
	return true;
}

FMaxisMeshSection BuildExtendedCockpitCannonSection(
	const FMaxisMeshSection& Source,
	const float RearExtensionCm)
{
	FMaxisMeshSection Extended = Source;
	if (Extended.IsEmpty() ||
		!Extended.LocalBounds.IsValid ||
		RearExtensionCm <= UE_SMALL_NUMBER)
	{
		return Extended;
	}

	// The builder emits split face-corner vertices, but every corner on the rear cap shares the
	// same minimum X. Moving that entire ring backward stretches the connecting faces into a
	// closed one-metre extension without changing the untouched world cannon.
	const double MinX = Extended.LocalBounds.Min.X;
	const double RearToleranceCm = FMath::Max(
		0.25,
		static_cast<double>(Extended.LocalBounds.GetSize().X) * 0.02);
	for (FVector& Vertex : Extended.Vertices)
	{
		if (Vertex.X <= MinX + RearToleranceCm)
		{
			Vertex.X -= RearExtensionCm;
		}
	}

	Extended.LocalBounds = FBox(ForceInit);
	for (const FVector& Vertex : Extended.Vertices)
	{
		Extended.LocalBounds += Vertex;
	}
	return Extended;
}

FRotator SanitizeCameraDebugRotation(const FRotator& RotationDeg)
{
	return FRotator(
		SanitizeCameraDebugRotation(RotationDeg.Pitch),
		SanitizeCameraDebugRotation(RotationDeg.Yaw),
		SanitizeCameraDebugRotation(RotationDeg.Roll));
}

float SanitizeCameraZoomFramingStrength(float Strength)
{
	return FMath::IsFinite(Strength)
		? FMath::Clamp(Strength, 0.0f, MaxCameraZoomFramingStrength)
		: 1.0f;
}

float SanitizeCameraMaxZoomDistanceOverride(float DistanceCm)
{
	if (!FMath::IsFinite(DistanceCm) || DistanceCm <= 0.0f)
	{
		return 0.0f;
	}
	return FMath::Clamp(DistanceCm, 100.0f, MaxCameraZoomDistanceCm);
}

void BlendPossessionViewTarget(
	APlayerController* PlayerController,
	AActor* OutgoingViewTarget,
	APawn* IncomingPawn,
	float BlendSeconds)
{
	if (PlayerController == nullptr ||
		OutgoingViewTarget == nullptr ||
		IncomingPawn == nullptr ||
		OutgoingViewTarget == IncomingPawn ||
		BlendSeconds <= UE_SMALL_NUMBER)
	{
		return;
	}

	// Possess immediately hands over input and also immediately selects the new pawn's camera.
	// Restore the outgoing view in the same frame, then ease to the already-possessed pawn.
	PlayerController->SetViewTarget(OutgoingViewTarget);
	PlayerController->SetViewTargetWithBlend(
		IncomingPawn,
		BlendSeconds,
		VTBlend_Cubic,
		2.0f,
		/*bLockOutgoing*/ true);
}

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

// Views that draw the aiming crosshair: Cockpit and Rescue normally, plus Chase and Orbit
// for the Apache so its missiles and gun can be aimed from behind the airframe.
bool CameraModeShowsCrosshair(ESimCopterCameraMode Mode, bool bIsApache)
{
	if (bIsApache && (Mode == ESimCopterCameraMode::Chase || Mode == ESimCopterCameraMode::Orbit))
	{
		return true;
	}
	return Mode == ESimCopterCameraMode::Cockpit || Mode == ESimCopterCameraMode::Rescue;
}

ASimCopterHelicopterPawn::ASimCopterHelicopterPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	CameraViewDebugOffsets[GetCameraModeIndex(ESimCopterCameraMode::Chase)] =
		GetDefaultCameraViewDebugOffset(ESimCopterCameraMode::Chase);
	CameraViewDebugOffsets[GetCameraModeIndex(ESimCopterCameraMode::Orbit)] =
		GetDefaultCameraViewDebugOffset(ESimCopterCameraMode::Orbit);
	CameraViewDebugOffsets[GetCameraModeIndex(ESimCopterCameraMode::Rescue)] =
		GetDefaultCameraViewDebugOffset(ESimCopterCameraMode::Rescue);
	CameraViewDebugOffsets[GetCameraModeIndex(ESimCopterCameraMode::Cockpit)] =
		GetDefaultCameraViewDebugOffset(ESimCopterCameraMode::Cockpit);

	CollisionComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CollisionComponent"));
	CollisionComponent->InitCapsuleSize(95.0f, 82.0f);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionComponent->SetCollisionObjectType(ECC_Pawn);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	// InitCapsuleSize clamps the half height up to the radius, so this shape is a 190 cm sphere -
	// sized for the flight impact sweep against city geometry (see ApplyFlightModelToActor), not
	// for the fuselage, which is a fraction of that across. Blocking pawns with it walled the
	// airframe off behind a metre of invisible air: the avatar was stopped short of the aircraft
	// on every side, so "walk up to the helicopter and get in" could only ever be a bubble test.
	// People are not obstacles to an aircraft in the original either - FUN_0048ad50 answers a
	// person with damage and a bounce, never with a stop - so pawns overlap instead.
	CollisionComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CollisionComponent->SetCanEverAffectNavigation(false);
	SetRootComponent(CollisionComponent);

	// Shared tilt pivot so both the placeholder and the original-mesh geometry bank together.
	ModelPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ModelPivot"));
	ModelPivot->SetupAttachment(CollisionComponent);

	CameraAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("CameraAnchor"));
	CameraAnchor->SetupAttachment(ModelPivot);
	CameraAnchor->SetRelativeLocation(
		FVector(0.0f, 0.0f, CollisionComponent->GetUnscaledCapsuleHalfHeight()));

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

	// Parented to the body so the markers keep the local frame PrepareHelicopterModel read them in,
	// including the skid-to-capsule offset ApplyPreparedModelMeshes puts on the body.
	FlashingLightsComponent = CreateDefaultSubobject<USimCopterFlashingLightsComponent>(TEXT("FlashingLights"));
	FlashingLightsComponent->SetupAttachment(HeliBodyMeshComponent);
	// Uncapped like the city's (MaxPointLights stays 0); an airframe only carries four markers
	// anyway - one white, two red, one green - and just one colour is lit at a time.
	// Close range: these are position lights on a small aircraft, not searchlights. The spotlight
	// (SimCopterSpotlight) is the thing that is supposed to illuminate the ground.
	FlashingLightsComponent->PointLightAttenuationRadiusCm = 600.0f;
	FlashingLightsComponent->PointLightIntensity = 6.0f;

	// The CANNON object's vertices are authored in fuselage-local space (barrel at X 27..58,
	// Y +/-2.5, Z 5..11 cm), so parenting it to the body at zero offset puts it exactly where
	// the original draws it. Only visible while the water cannon is installed.
	HeliCannonMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("HeliCannon"));
	HeliCannonMeshComponent->SetupAttachment(HeliBodyMeshComponent);
	HeliCannonMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// BRACKET (0x16c): the rescue harness's mount on the right flank. FUN_00483c20 builds it for
	// every helicopter; the remake only shows it when the harness is actually aboard.
	HeliBracketMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("HeliBracketMesh"));
	HeliBracketMeshComponent->SetupAttachment(HeliBodyMeshComponent);
	HeliBracketMeshComponent->SetMobility(EComponentMobility::Movable);
	HeliBracketMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeliBracketMeshComponent->SetCanEverAffectNavigation(false);
	HeliBracketMeshComponent->SetCastShadow(true);
	HeliBracketMeshComponent->SetVisibility(false);
	HeliCannonMeshComponent->SetCanEverAffectNavigation(false);
	HeliCannonMeshComponent->SetVisibility(false);

	// Cockpit view model: the same CANNON geometry, camera-parented for an exact positional
	// lock. Its rotation is absolute because user look pitch must sweep past the cannon rather
	// than tilt the aircraft-mounted barrel.
	CockpitCannonMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CockpitCannon"));
	CockpitCannonMeshComponent->SetUsingAbsoluteRotation(true);
	CockpitCannonMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CockpitCannonMeshComponent->SetCanEverAffectNavigation(false);
	CockpitCannonMeshComponent->SetCastShadow(false);
	CockpitCannonMeshComponent->SetVisibility(false);

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

	// The rope end renders exactly one of BUCKET/HARNESS at a time (FUN_00487bb0 swaps
	// heli[0x32] / heli[0x33] into the rope-end node when the winch reaches node 0x10).
	OriginalHarnessMeshComponent =
		CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("OriginalHarness"));
	OriginalHarnessMeshComponent->SetupAttachment(CollisionComponent);
	OriginalHarnessMeshComponent->SetMobility(EComponentMobility::Movable);
	OriginalHarnessMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OriginalHarnessMeshComponent->SetCanEverAffectNavigation(false);
	OriginalHarnessMeshComponent->SetCastShadow(true);
	OriginalHarnessMeshComponent->SetVisibility(false);

	WaterFXComponent = CreateDefaultSubobject<USimCopterParticleFXComponent>(TEXT("WaterFX"));
	WaterFXComponent->SetupAttachment(CollisionComponent);

	TearGasPool = CreateDefaultSubobject<USimCopterTearGasPoolComponent>(TEXT("TearGasPool"));
	TearGasPool->SetupAttachment(CollisionComponent);

	ApachePool = CreateDefaultSubobject<USimCopterApachePoolComponent>(TEXT("ApachePool"));
	ApachePool->SetupAttachment(CollisionComponent);

	SearchLightComponent = CreateDefaultSubobject<USpotLightComponent>(TEXT("SearchLight"));
	SearchLightComponent->SetupAttachment(ModelPivot);
	SearchLightComponent->SetRelativeLocation(FVector(95.0f, 0.0f, -35.0f));
	SearchLightComponent->SetRelativeRotation(FRotator(-35.0f, 0.0f, 0.0f));
	SearchLightComponent->Intensity = SearchLightIntensity;
	SearchLightComponent->AttenuationRadius = SearchLightRangeCm;
	SearchLightComponent->InnerConeAngle = 8.0f;
	SearchLightComponent->OuterConeAngle = 20.0f;
	SearchLightComponent->SetLightColor(SearchLightBeamColor.ToFColor(true));
	// See SearchLightExposureCompensation: without this the beam is invisible under the day
	// sequence's physically scaled sun. Set on the property directly - the setter is a no-op on a
	// default subobject, before the component is registered.
	SearchLightComponent->InverseExposureBlend = SearchLightExposureCompensation;
	// Set on the property for the same reason as the line above - the setter no-ops on an
	// unregistered default subobject. See SearchLightIndirectLightingIntensity for why it is not 1.
	SearchLightComponent->IndirectLightingIntensity = SearchLightIndirectLightingIntensity;
	SearchLightComponent->SetVisibility(bSearchLightStartsEnabled);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(CameraAnchor);
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->TargetArmLength = 900.0f;
	CameraBoom->TargetOffset = FVector::ZeroVector;
	// The spring arm's own collision test is OFF on purpose. UpdateCamera does all three of the
	// jobs it would do - least-angle avoidance (FindCameraAvoidanceOffset), pull-in along the
	// roof-to-camera segment (ResolveCameraPullInAlpha) and ground clearance
	// (ResolveCameraGroundLift) - and the engine's version actively fought the last of them:
	//
	// it sweeps from ArmOrigin, which is GetComponentLocation() + TargetOffset, and this camera
	// keeps TargetOffset at zero and puts the whole framing translation in SocketOffset, applied
	// *after* the arm. So the engine swept from the fuselage roof straight to the final offset
	// position and clamped the camera to the first thing it grazed. Near the ground that clamp
	// won every time, which is why the ground lift appeared to do nothing however it was tuned.
	//
	// ProbeChannel/ProbeSize are still read by ResolveCameraArmLengthForObstruction and the
	// pull-in probe, so they stay.
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->ProbeChannel = ECC_Camera;
	CameraBoom->ProbeSize = 18.0f;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 9.5f;
	CameraBoom->bEnableCameraRotationLag = true;
	CameraBoom->CameraRotationLagSpeed = 8.0f;
	CameraBoom->SetRelativeRotation(FRotator(-16.0f, 0.0f, 0.0f));

	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	CameraComponent->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	CameraComponent->FieldOfView = 78.0f;
	CockpitCannonMeshComponent->SetupAttachment(CameraComponent);

	// Screen space is deliberate: the component still follows a world location, but it is
	// composited after the world with no depth test and retains an invariant Slate pixel size.
	CrosshairComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("Crosshair"));
	CrosshairComponent->SetupAttachment(CollisionComponent);
	CrosshairComponent->SetUsingAbsoluteLocation(true);
	CrosshairComponent->SetWidgetSpace(EWidgetSpace::Screen);
	CrosshairComponent->SetDrawAtDesiredSize(true);
	CrosshairComponent->SetPivot(FVector2D(0.5f, 0.5f));
	CrosshairComponent->SetWindowFocusable(false);
	CrosshairComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CrosshairComponent->SetCanEverAffectNavigation(false);
	CrosshairComponent->SetCastShadow(false);
	CrosshairComponent->SetInitialSharedLayerName(CrosshairScreenLayerName);
	CrosshairComponent->SetInitialLayerZOrder(10);
	CrosshairComponent->SetVisibility(false);

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
	}

	// The stock cylinder has no authored vertex colour, so the shared vertex-colour material
	// rendered the rope as a bright placeholder. The original winch line reads as black.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RopeMaterialFinder(
		TEXT("/Engine/EngineDebugMaterials/BlackUnlitMaterial.BlackUnlitMaterial"));
	if (RopeMaterialFinder.Succeeded())
	{
		RopeMeshComponent->SetMaterial(0, RopeMaterialFinder.Object);
		for (USplineMeshComponent* Segment : RopeSegmentComponents)
		{
			if (Segment != nullptr)
			{
				Segment->SetMaterial(0, RopeMaterialFinder.Object);
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

	// The Settings screen's HUD Scale row.
	if (USimCopterSettings* Settings = USimCopterSettings::Get(this))
	{
		HudScaleHandle = Settings->OnHudScaleChanged.AddWeakLambda(
			this, [this](float) { RebuildCockpitOverlays(); });
	}

	// Swap the raw material asset for the fleet-wide instance, so the debug panel's metallic
	// slider reaches the fuselage. Everything downstream still just assigns ModelVertexColorMaterial.
	if (USimCopterVehicleMaterialSubsystem* VehicleMaterials = USimCopterVehicleMaterialSubsystem::Get(this))
	{
		if (UMaterialInstanceDynamic* Shared = VehicleMaterials->GetVehicleMaterial(ModelVertexColorMaterial))
		{
			ModelVertexColorMaterial = Shared;
		}
	}
	LoadCameraViewDebugOffsets();
	LoadCockpitStabilization();
	LoadRotorDiscAppearance();
	LoadCameraGroundLift();
	LoadEasyFlightModel();
	// Playable starting point for the frame-rate assumption, over the model's own
	// defaults, which are the executable's figures. These are feel, not fidelity, and
	// exist to be dialled in from the debug panel - the ini below wins once they have
	// been. 20 fps everywhere with a x1 rotor is the documented-faithful setting to go
	// back to; the shake is the one that stays there, because 60 makes it far too busy.
	//
	// Promoted 2026-07-30 from the current debug-panel values in GameUserSettings.
	FlightModel.TurbulenceFrameSeconds = SimCopterFixed::FromFloat(1.0f / 20.0f);
	FlightModel.ReferenceFrameSeconds = SimCopterFixed::FromFloat(1.0f / 60.0f);
	FlightModel.SpeedChaseFrameSeconds = SimCopterFixed::FromFloat(1.0f / 60.0f);
	FlightModel.RotorVisualMultiplier = SimCopterFixed::FromFloat(4.0f);
	LoadFlightRateTuning();

	// The editor property is a name; the registry index is what the runtime uses from here on.
	if (const FSimCopterHelicopterDefinition* Seed =
			SimCopterHelicopterRegistry::FindByDisplayName(HelicopterTypeName))
	{
		ActiveHelicopterTypeIndex = Seed->InternalTypeIndex;
		HelicopterTypeName = Seed->DisplayName;
	}
	else
	{
		UE_LOG(
			LogSimCopterHelicopterPawn,
			Warning,
			TEXT("'%s' is not a known helicopter type; falling back to starting runtime type %d."),
			*HelicopterTypeName,
			USimCopterCareerSubsystem::StartingHelicopterTypeIndex);
		ActiveHelicopterTypeIndex = USimCopterCareerSubsystem::StartingHelicopterTypeIndex;
		HelicopterTypeName = SimCopterHelicopterRegistry::GetDefinitions()[ActiveHelicopterTypeIndex].DisplayName;
	}

	// Career layer (Phase 7) will own this; for now the map seeds it and debug grants overlay it.
	EquipmentState.CareerEquipmentMask =
		StartingCareerEquipmentMask & SimCopterHelicopterRegistry::AllCareerEquipmentBits;
	EquipmentState.CareerTearGasRounds = FMath::Clamp(
		StartingTearGasRounds, 0, SimCopterHelicopterRegistry::TearGasCapacity);
	EquipmentState.ClearDebugOverlay();
	if (bWaterCannonInstalled)
	{
		// Preserve the pre-registry authoring switch as a career bit rather than a side flag.
		EquipmentState.CareerEquipmentMask |=
			SimCopterHelicopterRegistry::GetToolCareerBit(ESimCopterHelicopterTool::WaterCannon);
	}
	bWaterCannonInstalled = IsToolAvailable(ESimCopterHelicopterTool::WaterCannon);
	RecomputeActiveToolFallback();

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
	UpdateCameraAnchorFromVisibleBody();
	if (WaterFXComponent != nullptr)
	{
		FString EffectError;
		if (!WaterFXComponent->InitEffectAssets(ResolveOriginalGameRoot(), EffectError))
		{
			UE_LOG(LogTemp, Warning, TEXT("SimCopter effect palette unavailable: %s"), *EffectError);
		}
	}
	if (TearGasPool != nullptr)
	{
		// Trail smoke and gas puffs are ordinary FUN_004af220 tile puffs, so the pool borrows the
		// same renderer the bucket and the rotor wash already use.
		TearGasPool->SetOriginalGameRoot(ResolveOriginalGameRoot());
		TearGasPool->SetEffectComponent(WaterFXComponent);
	}
	if (ApachePool != nullptr)
	{
		ApachePool->SetOriginalGameRoot(ResolveOriginalGameRoot());
		ApachePool->SetEffectComponent(WaterFXComponent);
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
		EnsureDashboardWidget();
		EnsureMapWidget();
		EnsureWaterControlsWidget();
		EnsureToolFlapsWidget();
		EnsureCrosshairWidget();
		EnsureControllerOverlayWidget();
		EnsureHelicopterDebugPanel();
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
	if (USimCopterSettings* Settings = USimCopterSettings::Get(this); Settings != nullptr && HudScaleHandle.IsValid())
	{
		Settings->OnHudScaleChanged.Remove(HudScaleHandle);
		HudScaleHandle.Reset();
	}

	RemoveDashboardWidget();
	RemoveMapWidget();
	RemoveWaterControlsWidget();
	RemoveToolFlapsWidget();
	RemoveCrosshairWidget();
	RemoveControllerOverlayWidget();
	RemoveHelicopterDebugPanel();
	Super::EndPlay(EndPlayReason);
}

void ASimCopterHelicopterPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateControllerInput(DeltaSeconds);
	UpdateToolDispatch(DeltaSeconds);

	float RemainingSeconds = FMath::Clamp(DeltaSeconds, 0.0f, MaxTickSeconds);
	while (RemainingSeconds > UE_SMALL_NUMBER)
	{
		const float StepSeconds = FMath::Min(RemainingSeconds, MaxSubstepSeconds);
		SimulateFlightStep(StepSeconds);
		RemainingSeconds -= StepSeconds;
	}

	UpdateVisuals(DeltaSeconds);
	UpdateRotorWash(DeltaSeconds);
	UpdateHelicopterAudio(DeltaSeconds);
	UpdateCheckupOffer();
	// Only the GEO fuselage carries blink markers; the placeholder body has none to show.
	if (FlashingLightsComponent != nullptr && bUsingOriginalMesh && GetWorld() != nullptr)
	{
		FlashingLightsComponent->SyncLightsFromPlayerCamera(GetWorld()->GetTimeSeconds());
	}
	// Semantic targeting keeps running whether or not the cone is drawn (FUN_00489250).
	UpdateSpotlightTarget(DeltaSeconds);
	UpdateCamera(DeltaSeconds);
	UpdateCrosshairWorldLocation();
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
	PlayerInputComponent->BindAxis(TEXT("SimCopterControllerLeftX"), this, &ASimCopterHelicopterPawn::ControllerLeftX);
	PlayerInputComponent->BindAxis(TEXT("SimCopterControllerLeftY"), this, &ASimCopterHelicopterPawn::ControllerLeftY);
	PlayerInputComponent->BindAxis(TEXT("SimCopterControllerRightX"), this, &ASimCopterHelicopterPawn::ControllerRightX);
	PlayerInputComponent->BindAxis(TEXT("SimCopterControllerRightY"), this, &ASimCopterHelicopterPawn::ControllerRightY);
	PlayerInputComponent->BindAxis(TEXT("SimCopterControllerRightTrigger"), this, &ASimCopterHelicopterPawn::ControllerRightTrigger);

	PlayerInputComponent->BindAction(TEXT("SimCopterToggleRope"), IE_Pressed, this, &ASimCopterHelicopterPawn::ToggleRope);
	// Left click is the common primary action for every selected tool; the legacy action
	// name stays so existing input mappings keep working.
	PlayerInputComponent->BindAction(TEXT("SimCopterReleaseWater"), IE_Pressed, this, &ASimCopterHelicopterPawn::StartPrimaryToolUse);
	PlayerInputComponent->BindAction(TEXT("SimCopterReleaseWater"), IE_Released, this, &ASimCopterHelicopterPawn::StopPrimaryToolUse);
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
	PlayerInputComponent->BindAction(TEXT("SimCopterCameraPan"), IE_Pressed, this, &ASimCopterHelicopterPawn::StartCameraPanDrag);
	PlayerInputComponent->BindAction(TEXT("SimCopterCameraPan"), IE_Released, this, &ASimCopterHelicopterPawn::StopCameraPanDrag);
	PlayerInputComponent->BindAction(TEXT("SimCopterCycleCamera"), IE_Pressed, this, &ASimCopterHelicopterPawn::CycleCameraMode);
	PlayerInputComponent->BindAction(TEXT("SimCopterSearchLight"), IE_Pressed, this, &ASimCopterHelicopterPawn::ToggleSearchLight);
	PlayerInputComponent->BindAction(TEXT("SimCopterResetAircraft"), IE_Pressed, this, &ASimCopterHelicopterPawn::ResetAircraft);

	// The map's zoom, bound directly because the keys are ground truth rather than a preference:
	// the shipped input.cfg maps command 0x1b (FUN_004a3d50, zoom in) to '=' and 0x1c
	// (FUN_004a3d80, zoom out) to '-'. The numpad pair is the collective, not the map.
	PlayerInputComponent->BindKey(EKeys::Equals, IE_Pressed, this, &ASimCopterHelicopterPawn::MapZoomIn);
	PlayerInputComponent->BindKey(EKeys::Hyphen, IE_Pressed, this, &ASimCopterHelicopterPawn::MapZoomOut);

	// Controller contexts are direct key bindings rather than static action mappings: LB/LT/R3
	// deliberately change what A/X/B, the right stick, RB/RT, and the D-pad mean.
	PlayerInputComponent->BindKey(EKeys::Gamepad_LeftShoulder, IE_Pressed, this, &ASimCopterHelicopterPawn::ControllerDispatchWheelPressed);
	PlayerInputComponent->BindKey(EKeys::Gamepad_LeftShoulder, IE_Released, this, &ASimCopterHelicopterPawn::ControllerDispatchWheelReleased);
	PlayerInputComponent->BindKey(EKeys::Gamepad_LeftTrigger, IE_Pressed, this, &ASimCopterHelicopterPawn::ControllerToolWheelPressed);
	PlayerInputComponent->BindKey(EKeys::Gamepad_LeftTrigger, IE_Released, this, &ASimCopterHelicopterPawn::ControllerToolWheelReleased);
	PlayerInputComponent->BindKey(EKeys::Gamepad_RightThumbstick, IE_Pressed, this, &ASimCopterHelicopterPawn::ControllerCameraAdjustPressed);
	PlayerInputComponent->BindKey(EKeys::Gamepad_RightThumbstick, IE_Released, this, &ASimCopterHelicopterPawn::ControllerCameraAdjustReleased);
	PlayerInputComponent->BindKey(EKeys::Gamepad_RightShoulder, IE_Pressed, this, &ASimCopterHelicopterPawn::ControllerRightShoulderPressed);
	PlayerInputComponent->BindKey(EKeys::Gamepad_RightShoulder, IE_Released, this, &ASimCopterHelicopterPawn::ControllerRightShoulderReleased);

	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Bottom, IE_Pressed, this, &ASimCopterHelicopterPawn::ControllerPrimaryPressed);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Bottom, IE_Released, this, &ASimCopterHelicopterPawn::ControllerPrimaryReleased);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Left, IE_Pressed, this, &ASimCopterHelicopterPawn::ControllerPassengerPressed);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Right, IE_Pressed, this, &ASimCopterHelicopterPawn::ControllerCancelPressed);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Top, IE_Pressed, this, &ASimCopterHelicopterPawn::ControllerEnterExitPressed);
	PlayerInputComponent->BindKey(EKeys::Gamepad_Special_Left, IE_Pressed, this, &ASimCopterHelicopterPawn::ControllerBackPressed);
	PlayerInputComponent->BindKey(EKeys::Gamepad_LeftThumbstick, IE_Pressed, this, &ASimCopterHelicopterPawn::ControllerSearchLightPressed);

	PlayerInputComponent->BindKey(EKeys::Gamepad_DPad_Up, IE_Pressed, this, &ASimCopterHelicopterPawn::ControllerDPadUpPressed);
	PlayerInputComponent->BindKey(EKeys::Gamepad_DPad_Up, IE_Released, this, &ASimCopterHelicopterPawn::ControllerDPadUpReleased);
	PlayerInputComponent->BindKey(EKeys::Gamepad_DPad_Down, IE_Pressed, this, &ASimCopterHelicopterPawn::ControllerDPadDownPressed);
	PlayerInputComponent->BindKey(EKeys::Gamepad_DPad_Down, IE_Released, this, &ASimCopterHelicopterPawn::ControllerDPadDownReleased);
	PlayerInputComponent->BindKey(EKeys::Gamepad_DPad_Left, IE_Pressed, this, &ASimCopterHelicopterPawn::ControllerDPadLeftPressed);
	PlayerInputComponent->BindKey(EKeys::Gamepad_DPad_Left, IE_Released, this, &ASimCopterHelicopterPawn::ControllerDPadLeftReleased);
	PlayerInputComponent->BindKey(EKeys::Gamepad_DPad_Right, IE_Pressed, this, &ASimCopterHelicopterPawn::ControllerDPadRightPressed);
	PlayerInputComponent->BindKey(EKeys::Gamepad_DPad_Right, IE_Released, this, &ASimCopterHelicopterPawn::ControllerDPadRightReleased);

	FInputKeyBinding& PauseBinding = PlayerInputComponent->BindKey(
		EKeys::Gamepad_Special_Right,
		IE_Pressed,
		this,
		&ASimCopterHelicopterPawn::ToggleGamePause);
	PauseBinding.bExecuteWhenPaused = true;

	// Spotlight aim (original input actions 0x2e..0x31). Bound to the numpad arrows directly so
	// no input-mapping config edit is required.
	PlayerInputComponent->BindKey(EKeys::NumPadEight, IE_Pressed, this, &ASimCopterHelicopterPawn::AimSpotlightPitchDown);
	PlayerInputComponent->BindKey(EKeys::NumPadEight, IE_Released, this, &ASimCopterHelicopterPawn::StopAimSpotlightPitch);
	PlayerInputComponent->BindKey(EKeys::NumPadTwo, IE_Pressed, this, &ASimCopterHelicopterPawn::AimSpotlightPitchUp);
	PlayerInputComponent->BindKey(EKeys::NumPadTwo, IE_Released, this, &ASimCopterHelicopterPawn::StopAimSpotlightPitch);
	PlayerInputComponent->BindKey(EKeys::NumPadFour, IE_Pressed, this, &ASimCopterHelicopterPawn::AimSpotlightYawLeft);
	PlayerInputComponent->BindKey(EKeys::NumPadFour, IE_Released, this, &ASimCopterHelicopterPawn::StopAimSpotlightYaw);
	PlayerInputComponent->BindKey(EKeys::NumPadSix, IE_Pressed, this, &ASimCopterHelicopterPawn::AimSpotlightYawRight);
	PlayerInputComponent->BindKey(EKeys::NumPadSix, IE_Released, this, &ASimCopterHelicopterPawn::StopAimSpotlightYaw);
	PlayerInputComponent->BindKey(EKeys::NumPadFive, IE_Pressed, this, &ASimCopterHelicopterPawn::ResetSpotlightAim);

	// Emergency dispatch, original command ids 0x16..0x19 (FUN_0048a580). The help
	// (09tut.htm) pins the keys: F2 fire truck, F3 ambulance, F4 police, F5 chase; holding
	// Shift clears that service's dispatch instead of issuing one.
	PlayerInputComponent->BindKey(EKeys::F2, IE_Pressed, this, &ASimCopterHelicopterPawn::DispatchFireTruckKey);
	PlayerInputComponent->BindKey(EKeys::F3, IE_Pressed, this, &ASimCopterHelicopterPawn::DispatchAmbulanceKey);
	PlayerInputComponent->BindKey(EKeys::F4, IE_Pressed, this, &ASimCopterHelicopterPawn::DispatchPoliceKey);
	PlayerInputComponent->BindKey(EKeys::F5, IE_Pressed, this, &ASimCopterHelicopterPawn::DispatchPoliceChaseKey);

	// Keep the original function keys free for dispatch and put the developer overlays behind a
	// deliberate chord that is unlikely to be pressed during flight.
	PlayerInputComponent->BindKey(
		FInputChord(EKeys::D, false, true, true, false),
		IE_Pressed,
		this,
		&ASimCopterHelicopterPawn::ToggleHelicopterDebugPanel);

	PlayerInputComponent->BindKey(
		FInputChord(EKeys::M, false, true, true, false),
		IE_Pressed,
		this,
		&ASimCopterHelicopterPawn::SimToggleFlapCalibration);
}

bool ASimCopterHelicopterPawn::LoadTuningFromOriginalGameRoot()
{
	LastTuningLoadError.Reset();

	const FSimCopterHelicopterDefinition* Definition = GetHelicopterDefinition();
	const FString SectionName = Definition != nullptr ? Definition->TweakSection : HelicopterTypeName;
	if (!ReadTuningForSection(
			SectionName,
			HelicopterTuning,
			LandingTuning,
			RopeTuning,
			DamageTuning,
			LastTuningLoadError))
	{
		UE_LOG(LogSimCopterHelicopterPawn, Warning, TEXT("%s"), *LastTuningLoadError);
		return false;
	}

	ApplyDerivedTuning();
	CurrentFuelGallons = HelicopterTuning.FuelGallons;

	UE_LOG(LogSimCopterHelicopterPawn, Display, TEXT("Loaded SimCopter helicopter tuning '%s'."), *SectionName);
	return true;
}

// Parses one heli.twk helicopter section plus the shared [Heli Landing]/[Heli Ropestuff]/
// [Heli Damage] blocks into caller-owned structs. Kept side-effect free so the model-switch
// transaction can stage tuning before it commits to anything (plan section 7).
bool ASimCopterHelicopterPawn::ReadTuningForSection(
	const FString& SectionName,
	FSimCopterHelicopterTypeTuning& OutHelicopterTuning,
	FSimCopterLandingTuning& OutLandingTuning,
	FSimCopterRopeTuning& OutRopeTuning,
	FSimCopterDamageTuning& OutDamageTuning,
	FString& OutError) const
{
	OutError.Reset();

	const FString RootPath = ResolveOriginalGameRoot();
	const FString HeliTweakPath = FPaths::Combine(RootPath, TEXT("tweak/heli.twk"));
	FSimCopterTweakFile TweakFile;
	FString Error;
	if (!FSimCopterTweakReader::LoadTweakFileFromFile(HeliTweakPath, TweakFile, Error))
	{
		OutError = Error;
		return false;
	}

	const FSimCopterTweakSection* HeliSection = TweakFile.FindSection(SectionName);
	if (HeliSection == nullptr)
	{
		OutError = FString::Printf(TEXT("Could not find helicopter tuning section '%s' in '%s'."), *SectionName, *HeliTweakPath);
		return false;
	}

	ReadAngleControl(*HeliSection, TEXT("MaxBank"), TweakAngleScale, OutHelicopterTuning.MaxBankDeg);
	ReadAngleControl(*HeliSection, TEXT("MaxSlide"), TweakAngleScale, OutHelicopterTuning.MaxSlideDeg);
	ReadAngleControl(*HeliSection, TEXT("MaxPitch"), TweakAngleScale, OutHelicopterTuning.MaxPitchDeg);
	ReadAngleControl(*HeliSection, TEXT("PitchRate"), TweakAngleScale, OutHelicopterTuning.PitchRateDegPerSec);
	ReadFloatControl(*HeliSection, TEXT("YawRate"), OutHelicopterTuning.YawAccelDegPerSec);
	ReadAngleControl(*HeliSection, TEXT("RollRate"), TweakAngleScale, OutHelicopterTuning.RollRateDegPerSec);
	ReadFloatControl(*HeliSection, TEXT("SlideRate"), OutHelicopterTuning.SlideResponse);
	float RawClimbRate = OutHelicopterTuning.ClimbRateCmPerSec / TweakClimbToCmPerSec;
	if (ReadControlValue(*HeliSection, TEXT("ClimbRate"), RawClimbRate))
	{
		OutHelicopterTuning.ClimbRateCmPerSec = RawClimbRate * TweakClimbToCmPerSec;
	}
	float FloatMaxLoad = static_cast<float>(OutHelicopterTuning.MaxLoadPounds);
	if (ReadControlValue(*HeliSection, TEXT("Max Load"), FloatMaxLoad))
	{
		OutHelicopterTuning.MaxLoadPounds = FMath::RoundToInt(FloatMaxLoad);
	}
	ReadFloatControl(*HeliSection, TEXT("Max YawRate"), OutHelicopterTuning.MaxYawRateDegPerSec);
	ReadFloatControl(*HeliSection, TEXT("Fuel Rate"), OutHelicopterTuning.FuelRateGallonsPerHour);
	float FloatNewCost = static_cast<float>(OutHelicopterTuning.NewCostDollars);
	if (ReadControlValue(*HeliSection, TEXT("New Cost"), FloatNewCost))
	{
		OutHelicopterTuning.NewCostDollars = FMath::RoundToInt(FloatNewCost);
	}
	float FloatMaxDamage = static_cast<float>(OutHelicopterTuning.MaxDamage);
	if (ReadControlValue(*HeliSection, TEXT("Max Damage"), FloatMaxDamage))
	{
		OutHelicopterTuning.MaxDamage = FMath::RoundToInt(FloatMaxDamage);
	}
	ReadFloatControl(*HeliSection, TEXT("Fuel ("), OutHelicopterTuning.FuelGallons);
	ReadFloatControl(*HeliSection, TEXT("Repair Rate"), OutHelicopterTuning.RepairRatePerDamage);
	ReadFloatControl(*HeliSection, TEXT("Fuel Cost"), OutHelicopterTuning.FuelCostPerGallon);

	if (const FSimCopterTweakSection* LandingSection = TweakFile.FindSection(TEXT("Heli Landing")))
	{
		ReadAngleControl(*LandingSection, TEXT("Pitch"), TweakAngleScale, OutLandingTuning.MaxPitchDeg);
		ReadAngleControl(*LandingSection, TEXT("Slide"), TweakAngleScale, OutLandingTuning.MaxRollDeg);
		float RawLandingSpeed = OutLandingTuning.MaxHorizontalSpeedCmPerSec / TweakSpeedToCmPerSec;
		if (ReadControlValue(*LandingSection, TEXT("Speed"), RawLandingSpeed))
		{
			OutLandingTuning.MaxHorizontalSpeedCmPerSec = RawLandingSpeed * TweakSpeedToCmPerSec;
		}
		float RawVerticalSpeed = OutLandingTuning.MaxVerticalSpeedCmPerSec / TweakSpeedToCmPerSec;
		if (ReadControlValue(*LandingSection, TEXT("Y Speed"), RawVerticalSpeed))
		{
			OutLandingTuning.MaxVerticalSpeedCmPerSec = RawVerticalSpeed * TweakSpeedToCmPerSec;
		}
		float RawDescentRate = OutLandingTuning.MaxDescentRateCmPerSec / TweakSpeedToCmPerSec;
		if (ReadControlValue(*LandingSection, TEXT("Max Descent Rate"), RawDescentRate))
		{
			OutLandingTuning.MaxDescentRateCmPerSec = RawDescentRate * TweakSpeedToCmPerSec;
		}
	}

	if (const FSimCopterTweakSection* RopeSection = TweakFile.FindSection(TEXT("Heli Ropestuff")))
	{
		ReadFloatControl(*RopeSection, TEXT("Bucket Fill Rate"), OutRopeTuning.BucketFillPoundsPerFrame);
		ReadFloatControl(*RopeSection, TEXT("Bucket Dump Rate"), OutRopeTuning.BucketDumpPoundsPerFrame);
		ReadFloatControl(*RopeSection, TEXT("Rope Load Factor"), OutRopeTuning.RopeLoadFactor);
		ReadFloatControl(*RopeSection, TEXT("Rope Tension"), OutRopeTuning.RopeTension);
		ReadFloatControl(*RopeSection, TEXT("Water Throw"), OutRopeTuning.WaterThrow);
		ReadFloatControl(*RopeSection, TEXT("Cannon Force"), OutRopeTuning.CannonForce);
	}

	if (const FSimCopterTweakSection* DamageSection = TweakFile.FindSection(TEXT("Heli Damage")))
	{
		float RawMinFireAltitude = OutDamageTuning.MinFireAltitudeCm / TweakAltitudeToCm;
		if (ReadControlValue(*DamageSection, TEXT("Min Fire Alt"), RawMinFireAltitude))
		{
			OutDamageTuning.MinFireAltitudeCm = RawMinFireAltitude * TweakAltitudeToCm;
		}
		float RawMaxFireAltitude = OutDamageTuning.MaxFireAltitudeCm / TweakAltitudeToCm;
		if (ReadControlValue(*DamageSection, TEXT("Max Fire Alt"), RawMaxFireAltitude))
		{
			OutDamageTuning.MaxFireAltitudeCm = RawMaxFireAltitude * TweakAltitudeToCm;
		}
		ReadFloatControl(*DamageSection, TEXT("Depreciate"), OutDamageTuning.DepreciateDollarsPerSec);
		ReadFloatControl(*DamageSection, TEXT("Collision Subtract Val"), OutDamageTuning.CollisionDamageScale);
		ReadFloatControl(*DamageSection, TEXT("Repair Dist. Val"), OutDamageTuning.RepairDistanceValue);
		ReadFloatControl(*DamageSection, TEXT("Fuel Dist. Val"), OutDamageTuning.FuelDistanceValue);
	}

	return true;
}

const FSimCopterHelicopterDefinition* ASimCopterHelicopterPawn::GetHelicopterDefinition() const
{
	if (const FSimCopterHelicopterDefinition* ByIndex =
			SimCopterHelicopterRegistry::FindByTypeIndex(ActiveHelicopterTypeIndex))
	{
		return ByIndex;
	}
	return SimCopterHelicopterRegistry::FindByDisplayName(HelicopterTypeName);
}

bool ASimCopterHelicopterPawn::IsApacheHelicopter() const
{
	const FSimCopterHelicopterDefinition* Definition = GetHelicopterDefinition();
	return Definition != nullptr && Definition->bApacheArmament;
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
	UpdateCameraAnchorFromVisibleBody();
}

void ASimCopterHelicopterPawn::UpdateCameraAnchorFromVisibleBody()
{
	if (CameraAnchor == nullptr)
	{
		return;
	}

	float RoofLocalZ = CollisionComponent != nullptr
		? CollisionComponent->GetUnscaledCapsuleHalfHeight()
		: 0.0f;
	const USceneComponent* VisibleBody =
		bUsingOriginalMesh
			? static_cast<const USceneComponent*>(HeliBodyMeshComponent.Get())
			: static_cast<const USceneComponent*>(BodyMeshComponent.Get());
	if (VisibleBody != nullptr)
	{
		// Supplying the body-to-ModelPivot transform makes these bounds directly usable as
		// CameraAnchor's local coordinates for both procedural and placeholder fuselages.
		const FBoxSphereBounds BodyBounds =
			VisibleBody->CalcBounds(VisibleBody->GetRelativeTransform());
		const float BodyTopLocalZ = BodyBounds.Origin.Z + BodyBounds.BoxExtent.Z;
		if (BodyBounds.SphereRadius > UE_SMALL_NUMBER && FMath::IsFinite(BodyTopLocalZ))
		{
			RoofLocalZ = BodyTopLocalZ;
		}
	}

	CameraAnchor->SetRelativeLocation(FVector(0.0f, 0.0f, RoofLocalZ));
	UE_LOG(
		LogSimCopterHelicopterPawn,
		Verbose,
		TEXT("Camera collision-path anchor set to fuselage roof Z %.1f cm (%s body)."),
		RoofLocalZ,
		bUsingOriginalMesh ? TEXT("original") : TEXT("fallback"));
}

// SCHOOK: HelicopterModelBuild 0x00483c20
// Stages every asset the target model needs. Nothing here touches a live component, so a
// failed prepare leaves the current helicopter fully intact (plan section 7 "Prepare").
void ASimCopterHelicopterPawn::PrepareHelicopterModel(
	int32 TypeIndex,
	FSimCopterPreparedHelicopterModel& OutPrepared) const
{
	OutPrepared.Definition = SimCopterHelicopterRegistry::FindByTypeIndex(TypeIndex);
	if (OutPrepared.Definition == nullptr)
	{
		OutPrepared.Errors.Add(FString::Printf(
			TEXT("No helicopter registry entry for runtime type %d."), TypeIndex));
		return;
	}

	const FSimCopterHelicopterDefinition& Definition = *OutPrepared.Definition;
	OutPrepared.TailRotorOffsetCm = Definition.ToTailRotorOffsetCm(OriginalUnitToCm);

	FString TuningError;
	OutPrepared.bTuningLoaded = ReadTuningForSection(
		Definition.TweakSection,
		OutPrepared.HelicopterTuning,
		OutPrepared.LandingTuning,
		OutPrepared.RopeTuning,
		OutPrepared.DamageTuning,
		TuningError);
	if (!OutPrepared.bTuningLoaded)
	{
		OutPrepared.Errors.Add(TuningError);
	}

	const FString RootPath = ResolveOriginalGameRoot();
	FMaxisMeshLibrary MeshLibrary;
	FString MeshError;
	if (!MeshLibrary.LoadFromOriginalGameRoot(RootPath, MeshError))
	{
		OutPrepared.Errors.Add(MeshError);
		return;
	}

	const FLinearColor FallbackColor(0.6f, 0.6f, 0.62f);
	auto BuildById =
		[this, &MeshLibrary, &FallbackColor](int32 ObjectId, FMaxisMeshSection& OutSection)
	{
		const TArray<FColor>* ColorMap = nullptr;
		const FMaxisMeshObject* Object = MeshLibrary.FindObjectByObjectId(ObjectId, &ColorMap);
		if (Object == nullptr)
		{
			return false;
		}
		FMaxisProceduralMeshBuilder::BuildPaletteColoredSection(
			*Object, ColorMap, ModelUnitsPerCentimeter, ModelScale, bRenderModelBackfaces, FallbackColor, OutSection);
		return !OutSection.IsEmpty();
	};

	// Rotors split into an opaque blade section and the face-type-11 blur disc the original
	// only shows at RPM >= 300 (FUN_00487740). The GEO gives every rotor two stacked type-11
	// polygons and the original draws both; we deliberately keep only the first, because at
	// this resolution the pair reads as two separate circles instead of one blur.
	auto BuildRotorById =
		[this, &MeshLibrary, &FallbackColor](
			int32 ObjectId, FMaxisMeshSection& OutOpaque, FMaxisMeshSection& OutDisc)
	{
		const TArray<FColor>* ColorMap = nullptr;
		const FMaxisMeshObject* Object = MeshLibrary.FindObjectByObjectId(ObjectId, &ColorMap);
		if (Object == nullptr)
		{
			return false;
		}
		FMaxisProceduralMeshBuilder::BuildPaletteColoredSections(
			*Object, ColorMap, ModelUnitsPerCentimeter, ModelScale, bRenderModelBackfaces, FallbackColor, OutOpaque, &OutDisc, true);
		return !OutOpaque.IsEmpty() || !OutDisc.IsEmpty();
	};

	if (!BuildById(Definition.BodyObjectId, OutPrepared.BodySection))
	{
		OutPrepared.Errors.Add(FString::Printf(
			TEXT("Could not build body mesh '%s' (GEO id 0x%03x) from '%s'."),
			*Definition.BodyObjectName,
			Definition.BodyObjectId,
			*RootPath));
	}

	// The blink markers are face type 25, which BuildPaletteColoredSection skips (one vertex, so it
	// is not a polygon or a line). They come out of the same object in the same local frame - see
	// FSimCopterFlashingLightSchedule for the original's colour-phase rule.
	OutPrepared.BodyLightPoints.Reset();
	{
		const TArray<FColor>* BodyColorMap = nullptr;
		if (const FMaxisMeshObject* BodyObject =
			MeshLibrary.FindObjectByObjectId(Definition.BodyObjectId, &BodyColorMap))
		{
			FSimCopterFlashingLightSchedule::ExtractLightPoints(
				*BodyObject,
				BodyColorMap,
				ModelUnitsPerCentimeter,
				ModelScale,
				/*bApplyCityMeshOrientation*/ false,
				OutPrepared.BodyLightPoints);
		}
	}

	OutPrepared.bHasMainRotor = BuildRotorById(
		Definition.MainRotorObjectId,
		OutPrepared.MainRotorOpaqueSection,
		OutPrepared.MainRotorDiscSection);
	if (!OutPrepared.bHasMainRotor)
	{
		OutPrepared.Errors.Add(FString::Printf(
			TEXT("Could not build main rotor mesh '%s' (GEO id 0x%03x)."),
			*Definition.MainRotorObjectName,
			Definition.MainRotorObjectId));
	}

	// NOTAR airframes hide the shared ROTORTL object entirely (static block +0x38).
	if (bShowSeparateTailRotor && !Definition.bNoTailRotor)
	{
		OutPrepared.bHasTailRotor = BuildRotorById(
			SimCopterHelicopterObjects::TailRotor,
			OutPrepared.TailRotorOpaqueSection,
			OutPrepared.TailRotorDiscSection);
	}

	OutPrepared.bHasBucket = BuildById(SimCopterHelicopterObjects::Bucket, OutPrepared.BucketSection);
	OutPrepared.bHasHarness = BuildById(SimCopterHelicopterObjects::Harness, OutPrepared.HarnessSection);
	OutPrepared.bHasCannon = BuildById(SimCopterHelicopterObjects::Cannon, OutPrepared.CannonSection);
	OutPrepared.bHasBracket = BuildById(SimCopterHelicopterObjects::Bracket, OutPrepared.BracketSection);
}

// Plan section 7 "Validate": refuse the switch outright rather than half-applying it.
bool ASimCopterHelicopterPawn::ValidateHelicopterModel(
	const FSimCopterPreparedHelicopterModel& Prepared,
	FString& OutReason) const
{
	OutReason.Reset();

	if (Prepared.Definition == nullptr)
	{
		OutReason = Prepared.Errors.Num() > 0 ? Prepared.DescribeErrors() : TEXT("Unknown helicopter type.");
		return false;
	}
	if (!Prepared.HasBody())
	{
		OutReason = FString::Printf(
			TEXT("%s: body mesh unavailable (%s)"),
			*Prepared.Definition->DisplayName,
			*Prepared.DescribeErrors());
		return false;
	}
	if (!Prepared.bHasMainRotor)
	{
		OutReason = FString::Printf(
			TEXT("%s: main rotor mesh unavailable (%s)"),
			*Prepared.Definition->DisplayName,
			*Prepared.DescribeErrors());
		return false;
	}
	if (!Prepared.bTuningLoaded)
	{
		OutReason = FString::Printf(
			TEXT("%s: heli.twk tuning unavailable (%s)"),
			*Prepared.Definition->DisplayName,
			*Prepared.DescribeErrors());
		return false;
	}

	const int32 OnboardPassengers = FMath::Max(FlightModel.Passengers, MissionPassengerSlots.Num());
	if (Prepared.Definition->PassengerSeats < OnboardPassengers)
	{
		OutReason = FString::Printf(
			TEXT("%s has %d seats but %d passenger(s) are aboard."),
			*Prepared.Definition->DisplayName,
			Prepared.Definition->PassengerSeats,
			OnboardPassengers);
		return false;
	}

	// Phase 4 hooks the real rope-end rider here; until then the harness is never occupied
	// and this only guards the placeholder state.
	if (bHarnessRiderAttached)
	{
		OutReason = TEXT("A Sim is riding the rescue harness; raise them aboard first.");
		return false;
	}

	return true;
}

void ASimCopterHelicopterPawn::ApplyPreparedModelMeshes(const FSimCopterPreparedHelicopterModel& Prepared)
{
	auto ApplySection =
		[this](UProceduralMeshComponent* Component, const FMaxisMeshSection& Section)
	{
		if (Component == nullptr)
		{
			return false;
		}
		Component->ClearAllMeshSections();
		if (Section.IsEmpty())
		{
			return false;
		}
		Component->CreateMeshSection_LinearColor(
			0, Section.Vertices, Section.Triangles, Section.Normals, Section.UVs, Section.VertexColors, Section.Tangents, false);
		if (ModelVertexColorMaterial != nullptr)
		{
			Component->SetMaterial(0, ModelVertexColorMaterial);
		}
		return true;
	};

	auto ApplyRotor =
		[this](
			UProceduralMeshComponent* Component,
			const FMaxisMeshSection& Opaque,
			const FMaxisMeshSection& Disc,
			int32& OutDiscSectionIndex)
	{
		OutDiscSectionIndex = INDEX_NONE;
		if (Component == nullptr)
		{
			return false;
		}
		Component->ClearAllMeshSections();
		if (Opaque.IsEmpty() && Disc.IsEmpty())
		{
			return false;
		}

		int32 SectionIndex = 0;
		if (!Opaque.IsEmpty())
		{
			Component->CreateMeshSection_LinearColor(
				SectionIndex, Opaque.Vertices, Opaque.Triangles, Opaque.Normals, Opaque.UVs, Opaque.VertexColors, Opaque.Tangents, false);
			if (ModelVertexColorMaterial != nullptr)
			{
				Component->SetMaterial(SectionIndex, ModelVertexColorMaterial);
			}
			++SectionIndex;
		}
		if (!Disc.IsEmpty())
		{
			Component->CreateMeshSection_LinearColor(
				SectionIndex, Disc.Vertices, Disc.Triangles, Disc.Normals, Disc.UVs, Disc.VertexColors, Disc.Tangents, false);
			// M_SimCopterRotorDisc drives opacity from its DiscOpacity scalar, not from vertex
			// alpha, so the debug slider needs a dynamic instance to write into. Both rotors
			// share one, which keeps them in step.
			UMaterialInstanceDynamic* const DiscInstance = GetOrCreateRotorDiscMaterialInstance();
			UMaterialInterface* const DiscMaterial =
				DiscInstance != nullptr ? static_cast<UMaterialInterface*>(DiscInstance) : ModelVertexColorMaterial.Get();
			if (DiscMaterial != nullptr)
			{
				Component->SetMaterial(SectionIndex, DiscMaterial);
			}
			Component->SetMeshSectionVisible(SectionIndex, false);
			OutDiscSectionIndex = SectionIndex;
		}
		return true;
	};

	ApplySection(HeliBodyMeshComponent, Prepared.BodySection);

	// Sit the lowest fuselage vertex at the bottom of the collision capsule so the skids rest
	// near the ground contact point the flight probes use.
	const float CapsuleHalfHeight =
		CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	const float VerticalOffset = -CapsuleHalfHeight - Prepared.BodySection.LocalBounds.Min.Z;
	if (HeliBodyMeshComponent != nullptr)
	{
		HeliBodyMeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, VerticalOffset));
	}

	// The nose, in ModelPivot's frame. FUN_00484d20 launches every emitter from the body node
	// lifted 3.0 units, but the remake's ModelPivot is the *capsule* centre and the fuselage is
	// pushed down from it so the skids meet the ground - so "pivot + 3 units up" comes out level
	// with the rotor mast, which is where the tear gas canister appeared to be thrown from. Take
	// the muzzle off the mesh instead: forward-most X, the fuselage's own mid-height, on centre.
	bHasNoseMuzzle = Prepared.BodySection.LocalBounds.IsValid != 0;
	if (bHasNoseMuzzle)
	{
		const FBox& Body = Prepared.BodySection.LocalBounds;
		NoseMuzzleLocalCm = FVector(
			Body.Max.X,
			0.0f,
			VerticalOffset + Body.GetCenter().Z);
	}

	ApplyRotor(
		HeliMainRotorMeshComponent,
		Prepared.MainRotorOpaqueSection,
		Prepared.MainRotorDiscSection,
		MainRotorDiscSectionIndex);
	if (HeliMainRotorMeshComponent != nullptr)
	{
		HeliMainRotorMeshComponent->SetRelativeLocation(FVector::ZeroVector);
	}

	// Authored mount from the per-type static block replaces the old bounds heuristic.
	TailRotorDiscSectionIndex = INDEX_NONE;
	if (HeliTailRotorMeshComponent != nullptr)
	{
		HeliTailRotorMeshComponent->ClearAllMeshSections();
		const bool bTailBuilt = Prepared.bHasTailRotor &&
			ApplyRotor(
				HeliTailRotorMeshComponent,
				Prepared.TailRotorOpaqueSection,
				Prepared.TailRotorDiscSection,
				TailRotorDiscSectionIndex);
		HeliTailRotorMeshComponent->SetVisibility(bTailBuilt, true);
		if (bTailBuilt)
		{
			HeliTailRotorMeshComponent->SetRelativeLocation(Prepared.TailRotorOffsetCm);
		}
	}

	// Rides the body at zero offset (see the component's construction comment); UpdateVisuals
	// decides each frame whether the player actually has the cannon fitted, and which of the
	// two representations - world or cockpit view model - the current view wants.
	// BRACKET rides the body at zero offset for the same reason the cannon does: the GEO is
	// authored in the fuselage's own frame, out on the right flank where a winched Sim comes
	// aboard, and nothing in the executable ever repositions heli[0x31] after construction.
	bUsingOriginalBracketMesh = ApplySection(HeliBracketMeshComponent, Prepared.BracketSection);
	bHasBracketRopeAnchor = ResolveHarnessRopeAnchorLocal(
		Prepared.BracketSection,
		Prepared.BodySection,
		RopeRadiusCm,
		BracketRopeAnchorLocalCm);
	if (HeliBracketMeshComponent != nullptr)
	{
		HeliBracketMeshComponent->SetRelativeLocation(FVector::ZeroVector);
	}

	bUsingOriginalCannonMesh = ApplySection(HeliCannonMeshComponent, Prepared.CannonSection);
	bHasCannonBarrelTip = ResolveCannonBarrelTipLocal(
		Prepared.CannonSection,
		CannonBarrelTipLocalCm);
	if (HeliCannonMeshComponent != nullptr)
	{
		HeliCannonMeshComponent->SetRelativeLocation(FVector::ZeroVector);
	}

	// Seat the searchlight a bit ahead of wherever this airframe's water cannon actually sits,
	// mirroring ResolveToolMuzzle's own priority (barrel tip, then nose muzzle, then the
	// "3 units up" fallback) so every helicopter type gets a beam that starts in front of its
	// nozzle instead of a single offset tuned for one mesh. CannonBarrelTipLocalCm is expressed
	// in HeliCannonMeshComponent's frame, which sits at (0, 0, VerticalOffset) off ModelPivot -
	// add that back in to land in ModelPivot's frame like NoseMuzzleLocalCm already is.
	if (SearchLightComponent != nullptr)
	{
		FVector MuzzleLocalCm(0.0f, 0.0f, 3.0f * OriginalUnitToCm);
		if (bUsingOriginalCannonMesh && bHasCannonBarrelTip)
		{
			MuzzleLocalCm = CannonBarrelTipLocalCm + FVector(0.0f, 0.0f, VerticalOffset);
		}
		else if (bHasNoseMuzzle)
		{
			MuzzleLocalCm = NoseMuzzleLocalCm;
		}
		SearchLightComponent->SetRelativeLocation(
			MuzzleLocalCm + FVector(SearchLightForwardOfCannonOffsetCm, 0.0f, 0.0f));
	}
	const FMaxisMeshSection CockpitCannonSection = BuildExtendedCockpitCannonSection(
		Prepared.CannonSection,
		CockpitCannonRearExtensionCm);
	ApplySection(CockpitCannonMeshComponent, CockpitCannonSection);

	if (FlashingLightsComponent != nullptr)
	{
		FlashingLightsComponent->SetLightPoints(Prepared.BodyLightPoints);
	}

	bUsingOriginalBucketMesh = ApplySection(OriginalBucketMeshComponent, Prepared.BucketSection);
	bUsingOriginalHarnessMesh = ApplySection(OriginalHarnessMeshComponent, Prepared.HarnessSection);
	if (OriginalHarnessMeshComponent != nullptr)
	{
		// The harness only becomes visible when the winch swaps the rope end (Phase 4).
		OriginalHarnessMeshComponent->SetVisibility(false);
	}

	ShowOriginalMesh(true);
}

// Plan section 7 "Commit": one logical operation that preserves flight/session state.
void ASimCopterHelicopterPawn::CommitHelicopterModel(FSimCopterPreparedHelicopterModel& Prepared)
{
	check(Prepared.Definition != nullptr);

	// Normalised carry-over so switching does not silently refuel or repair the aircraft.
	const float PreviousMaxFuel = FMath::Max(HelicopterTuning.FuelGallons, KINDA_SMALL_NUMBER);
	const float FuelFraction = FMath::Clamp(CurrentFuelGallons / PreviousMaxFuel, 0.0f, 1.0f);
	const float PreviousMaxDamage = FMath::Max(static_cast<float>(HelicopterTuning.MaxDamage), 1.0f);
	const float DamageFraction = FMath::Clamp(CurrentDamage / PreviousMaxDamage, 0.0f, 1.0f);
	const int32 PreviousHitPoints = FlightModel.HitPoints;
	const int32 PreviousMaxHitPoints = FMath::Max(FlightModel.Tuning.MaxDamage, 1);

	// Held primary input must not survive the swap (plan section 7 "Validate").
	bPrimaryToolUseHeld = false;
	bPrimaryToolUsePressed = false;
	bBucketDumpHeld = false;
	bWaterCannonHeld = false;

	ActiveHelicopterTypeIndex = Prepared.Definition->InternalTypeIndex;
	HelicopterTypeName = Prepared.Definition->DisplayName;
	HelicopterTuning = Prepared.HelicopterTuning;
	LandingTuning = Prepared.LandingTuning;
	RopeTuning = Prepared.RopeTuning;
	DamageTuning = Prepared.DamageTuning;

	ApplyPreparedModelMeshes(Prepared);
	ApplyDerivedTuning();
	ApplyFlightTuningToModel();

	CurrentFuelGallons = FuelFraction * HelicopterTuning.FuelGallons;
	CurrentDamage = DamageFraction * static_cast<float>(HelicopterTuning.MaxDamage);
	FlightModel.Fuel = SimCopterFixed::FromFloat(CurrentFuelGallons);
	FlightModel.HitPoints = FMath::Clamp(
		FMath::RoundToInt(
			static_cast<float>(PreviousHitPoints) / static_cast<float>(PreviousMaxHitPoints) *
			static_cast<float>(FlightModel.Tuning.MaxDamage)),
		0,
		FlightModel.Tuning.MaxDamage);

	// Water load is capacity-bound, so clamp it and say so rather than carrying it over.
	const int32 ClampedWater = FMath::Clamp(BucketWaterPounds, 0, FMath::Max(0, HelicopterTuning.MaxLoadPounds));
	const bool bWaterClamped = ClampedWater != BucketWaterPounds;
	const int32 DroppedWater = BucketWaterPounds - ClampedWater;
	BucketWaterPounds = ClampedWater;
	FlightModel.LoadPounds = SimCopterFixed::FromFloat(static_cast<float>(BucketWaterPounds));

	// A model without the Apache's weapons must not keep one selected as the active tool.
	RecomputeActiveToolFallback();

	LastModelLoadError.Reset();
	LastModelSwitchStatus = FString::Printf(
		TEXT("%s (type %d) %s/%s  seats %d  max load %d lb%s%s"),
		*Prepared.Definition->DisplayName,
		Prepared.Definition->InternalTypeIndex,
		*Prepared.Definition->BodyObjectName,
		*Prepared.Definition->MainRotorObjectName,
		Prepared.Definition->PassengerSeats,
		HelicopterTuning.MaxLoadPounds,
		Prepared.Definition->bNoTailRotor ? TEXT("  NOTAR") : TEXT(""),
		bWaterClamped ? *FString::Printf(TEXT("  (dumped %d lb over capacity)"), DroppedWater) : TEXT(""));

	SyncPassengerFlightModelCount();
	RefreshDashboardSeats();
	RefreshWaterControlsWidget();
	UpdateCrosshairVisibility();
}

bool ASimCopterHelicopterPawn::SwitchHelicopterModel(int32 TypeIndex)
{
	if (TypeIndex == ActiveHelicopterTypeIndex && bUsingOriginalMesh)
	{
		LastModelSwitchStatus = FString::Printf(TEXT("Already flying runtime type %d."), TypeIndex);
		return true;
	}

	FSimCopterPreparedHelicopterModel Prepared;
	PrepareHelicopterModel(TypeIndex, Prepared);

	FString Reason;
	if (!ValidateHelicopterModel(Prepared, Reason))
	{
		LastModelSwitchStatus = FString::Printf(TEXT("Switch refused: %s"), *Reason);
		UE_LOG(LogSimCopterHelicopterPawn, Warning, TEXT("%s"), *LastModelSwitchStatus);
		return false;
	}

	CommitHelicopterModel(Prepared);
	UE_LOG(LogSimCopterHelicopterPawn, Display, TEXT("Switched helicopter model: %s"), *LastModelSwitchStatus);
	return true;
}

bool ASimCopterHelicopterPawn::CycleHelicopterModel(int32 Delta)
{
	const int32 Count = SimCopterHelicopterRegistry::GetDefinitionCount();
	if (Count <= 0 || Delta == 0)
	{
		return false;
	}

	// Wrap through every registry entry, skipping targets that refuse to load so one bad
	// asset cannot strand the arrows.
	for (int32 Attempt = 1; Attempt <= Count; ++Attempt)
	{
		const int32 Candidate =
			((ActiveHelicopterTypeIndex + Delta * Attempt) % Count + Count) % Count;
		if (SwitchHelicopterModel(Candidate))
		{
			return true;
		}
	}

	return false;
}

bool ASimCopterHelicopterPawn::LoadHelicopterMeshFromOriginalGameRoot()
{
	LastModelLoadError.Reset();
	bUsingOriginalBucketMesh = false;
	bUsingOriginalHarnessMesh = false;

	FSimCopterPreparedHelicopterModel Prepared;
	PrepareHelicopterModel(ActiveHelicopterTypeIndex, Prepared);
	if (!Prepared.HasBody() || !Prepared.bHasMainRotor)
	{
		LastModelLoadError = Prepared.Errors.Num() > 0
			? Prepared.DescribeErrors()
			: FString::Printf(TEXT("No helicopter registry entry for runtime type %d."), ActiveHelicopterTypeIndex);
		UE_LOG(LogSimCopterHelicopterPawn, Warning, TEXT("%s"), *LastModelLoadError);
		ShowOriginalMesh(false);
		return false;
	}

	// Tuning is loaded separately on BeginPlay, so this path only applies the geometry.
	ApplyPreparedModelMeshes(Prepared);
	UE_LOG(
		LogSimCopterHelicopterPawn,
		Display,
		TEXT("Loaded SimCopter helicopter model '%s' (body '%s', rotor '%s', bucket=%s, harness=%s)."),
		*Prepared.Definition->DisplayName,
		*Prepared.Definition->BodyObjectName,
		*Prepared.Definition->MainRotorObjectName,
		bUsingOriginalBucketMesh ? TEXT("BUCKET/0x07b") : TEXT("fallback"),
		bUsingOriginalHarnessMesh ? TEXT("HARNESS/0x16d") : TEXT("missing"));
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
	bControllerEngineStartHeld = false;
	bControllerEngineShutdownHeld = false;
	bControllerRightShoulderHeld = false;
	bControllerCameraAdjustHeld = false;
	bControllerDPadUpHeld = false;
	bControllerDPadDownHeld = false;
	bControllerDPadLeftHeld = false;
	bControllerDPadRightHeld = false;
	EngineStartHoldElapsed = 0.0f;
	EngineShutdownHoldElapsed = 0.0f;
	EngineStartHoldAlpha = 0.0f;
	EngineShutdownHoldAlpha = 0.0f;
	CurrentDamage = 0.0f;
	CurrentFuelGallons = HelicopterTuning.FuelGallons;
	BucketWaterPounds = 0;
	BucketWaterFraction = 0.0f;
	WinchState = SimCopterWinch::FWinchState();
	PendingWinchCommand = SimCopterWinch::CommandIdle;
	WinchHeldDirection = 0;
	bHarnessRopeEndSelected = false;
	bHarnessRiderAttached = false;
	RopeFirstActiveNode = SimCopterWaterGameplay::RopeStowedFirstActiveNode;
	bRopeDeployed = false;
	bRopeStateInitialized = false;
	bWaterCannonHeld = false;
	bPrimaryToolUseHeld = false;
	bPrimaryToolUsePressed = false;
	ControllerMode = ESimCopterControllerMode::None;
	ControllerPassengerSlot = INDEX_NONE;
	SetWinchHeldInput(/*bHarness=*/false, /*Direction=*/0);
	ControllerAppliedWinchDirection = 0;
	bControllerAppliedWinchHarness = false;
	ControllerSpotlightAimPitchInput = 0.0f;
	ControllerSpotlightAimYawInput = 0.0f;
	ToolCooldownSeconds = 0.0f;
	bIsLanded = false;
	SetActorRotation(FRotator(0.0f, GetActorRotation().Yaw, 0.0f));
	SeedFlightModelFromActor();
	UpdateGroundProbe();
}

float ASimCopterHelicopterPawn::GetHelipadRestingOriginOffsetCm() const
{
	// SeedFlightModelFromActor reads the flight model's altitude back as
	// (actor Z - capsule half height), and a completed landing (FUN_00487160) leaves that
	// altitude 1.2 units above the surface it settled on.
	const float CapsuleHalfHeight = CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleHalfHeight() : 0.0f;
	return CapsuleHalfHeight + SimCopterFixed::ToFloat(0x13333) * OriginalUnitToCm;
}

void ASimCopterHelicopterPawn::PlaceOnHelipad(const FVector& PadSurfaceWorldLocation, float YawDegrees)
{
	// FUN_00484790 writes the pad cell's own position straight into the helicopter's nodes and
	// clears its orientation - there is no descent and no collision test.
	SetActorRotation(FRotator(0.0f, YawDegrees, 0.0f));
	SetActorLocation(
		PadSurfaceWorldLocation + FVector(0.0f, 0.0f, GetHelipadRestingOriginOffsetCm()),
		/*bSweep=*/false,
		/*OutSweepHitResult=*/nullptr,
		ETeleportType::TeleportPhysics);

	VelocityCmPerSec = FVector::ZeroVector;
	CurrentPitchDeg = 0.0f;
	CurrentRollDeg = 0.0f;
	bIsLanded = true;
	SeedFlightModelFromActor();
	UpdateGroundProbe();
}

bool ASimCopterHelicopterPawn::ReturnToAirportAfterCrash()
{
	// SCHOOK: HelicopterCrashRespawn 0x0048a8b0
	// The wreck goes back to the airport, which is where the player's next flight starts from and
	// where the check-up desk that repairs it lives. FUN_0048b000 picks the pad; an occupied one is
	// only a tie-break there, and after a crash the fleet is parked, so pad 0 is the usual answer.
	ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystemActor();
	if (TrafficSystem == nullptr)
	{
		return false;
	}

	// Anything else standing on a pad blocks it, so a wreck cannot be dropped onto a parked
	// aircraft. Pads are 1x1, so "occupied" is a tile match.
	TBitArray<> PadTaken(false, SimCopterAirport::PadCount);
	TArray<AActor*> HelicopterActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ASimCopterHelicopterPawn::StaticClass(), HelicopterActors);
	for (int32 PadIndex = 0; PadIndex < SimCopterAirport::PadCount; ++PadIndex)
	{
		FVector PadWorld = FVector::ZeroVector;
		if (!TrafficSystem->TryGetAirportPadWorldLocation(PadIndex, PadWorld))
		{
			PadTaken[PadIndex] = true;
			continue;
		}
		for (const AActor* Other : HelicopterActors)
		{
			if (Other != this &&
				Other != nullptr &&
				FVector::Dist2D(Other->GetActorLocation(), PadWorld) < CrashRespawnPadClearanceCm)
			{
				PadTaken[PadIndex] = true;
				break;
			}
		}
	}

	auto IsPadTaken = [&PadTaken](int32 PadIndex) { return PadTaken[PadIndex]; };
	const int32 FreePad = SimCopterAirport::FindFreePadIndex(IsPadTaken, IsPadTaken);
	FVector PadSurface = FVector::ZeroVector;
	if (FreePad == INDEX_NONE || !TrafficSystem->TryGetAirportPadWorldLocation(FreePad, PadSurface))
	{
		// No airport in this city, or every pad blocked. Leave the wreck where it fell rather than
		// teleporting it somewhere arbitrary - the flight model has already repaired it in place.
		UE_LOG(LogTemp, Warning, TEXT("SimCopter crash: no free helipad to return the aircraft to."));
		return false;
	}

	// FUN_00484790 clears the orientation outright; the pads have no facing.
	PlaceOnHelipad(PadSurface, 0.0f);
	StuckFallSeconds = 0.0f;

	// Anyone riding the wreck comes back with it. Attached passengers already follow the actor;
	// this is for a player who is somehow not in the cabin but still possessing this pawn.
	UE_LOG(LogTemp, Display, TEXT("SimCopter crash: aircraft returned to airport pad %d."), FreePad);
	return true;
}

void ASimCopterHelicopterPawn::UpdateStuckFallWatchdog(const float DeltaSeconds)
{
	// The crash above is raised when a Dying helicopter reaches the ground. It can fail to arrive:
	// off the edge of the map, or over a column whose surface height never resolves, the aircraft
	// keeps descending with no ground to meet. Nothing in the original can reach that state, so
	// there is no behaviour to port - this is a remake-only backstop that ends the fall the same
	// way an arrival would.
	if (FlightModel.State != ESimCopterFlightState::Dying)
	{
		StuckFallSeconds = 0.0f;
		return;
	}

	StuckFallSeconds += DeltaSeconds;
	if (StuckFallSeconds < StuckFallRecoverySeconds)
	{
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("SimCopter crash: aircraft fell for %.1fs without reaching the ground; recovering to the airport."),
		StuckFallSeconds);
	StuckFallSeconds = 0.0f;
	bEngineRunning = false;
	// ResetOnSurface puts the model back in Parked with full hit points and fuel, which is the
	// same repair the ordinary crash arm gets. Doing it here as well means a city with no airport
	// still ends the fall instead of dropping forever.
	SeedFlightModelFromActor();
	ReturnToAirportAfterCrash();
}

float ASimCopterHelicopterPawn::GetFuelFraction() const
{
	return HelicopterTuning.FuelGallons > 0.0f ? FMath::Clamp(CurrentFuelGallons / HelicopterTuning.FuelGallons, 0.0f, 1.0f) : 0.0f;
}

float ASimCopterHelicopterPawn::GetDamageFraction() const
{
	return HelicopterTuning.MaxDamage > 0 ? FMath::Clamp(CurrentDamage / static_cast<float>(HelicopterTuning.MaxDamage), 0.0f, 1.0f) : 0.0f;
}

void ASimCopterHelicopterPawn::RestoreSavedCareerState(
	const int32 TypeIndex,
	const int32 CareerEquipmentMask,
	const int32 CareerTearGasRounds,
	const float FuelFraction,
	const float DamageFraction,
	const int32 SelectedToolIndex)
{
	// A failed asset prepare leaves the existing aircraft untouched. The rest of the career state
	// is still valid and should load even when one local original-game mesh is unavailable.
	SwitchHelicopterModel(TypeIndex);

	EquipmentState.CareerEquipmentMask =
		CareerEquipmentMask & SimCopterHelicopterRegistry::AllCareerEquipmentBits;
	EquipmentState.CareerTearGasRounds = FMath::Clamp(
		CareerTearGasRounds, 0, SimCopterHelicopterRegistry::TearGasCapacity);
	EquipmentState.ClearDebugOverlay();

	// Canisters in the air belong to the session being replaced; the save carries the magazine,
	// not the pool. Leaving them running would gas the loaded city on the previous game's behalf.
	if (TearGasPool != nullptr)
	{
		TearGasPool->ClearAll();
	}

	CurrentFuelGallons = FMath::Clamp(FuelFraction, 0.0f, 1.0f) * HelicopterTuning.FuelGallons;
	CurrentDamage = FMath::Clamp(DamageFraction, 0.0f, 1.0f) * static_cast<float>(HelicopterTuning.MaxDamage);
	FlightModel.Fuel = SimCopterFixed::FromFloat(CurrentFuelGallons);
	FlightModel.HitPoints = FMath::Clamp(
		HelicopterTuning.MaxDamage - FMath::RoundToInt(CurrentDamage),
		0,
		HelicopterTuning.MaxDamage);

	const ESimCopterHelicopterTool SavedTool = static_cast<ESimCopterHelicopterTool>(SelectedToolIndex);
	SetSelectedTool(SavedTool);
	bWaterCannonInstalled = IsToolAvailable(ESimCopterHelicopterTool::WaterCannon);
	RecomputeActiveToolFallback();
	RefreshDashboardSeats();
	RefreshWaterControlsWidget();
}

bool ASimCopterHelicopterPawn::CaptureRuntimeSaveState(TArray<uint8>& OutData)
{
	OutData.Reset();
	FMemoryWriter Writer(OutData, true);
	uint32 Magic = AircraftRuntimeSaveMagic;
	int32 Version = AircraftRuntimeSaveVersion;
	Writer << Magic << Version;

	FTransform ActorTransform = GetActorTransform();
	Writer << ActorTransform;
	SerializeFlightModel(Writer, FlightModel);
	SerializeArchiveBool(Writer, bFlightModelSeeded);
	Writer << VelocityCmPerSec << CurrentPitchDeg << CurrentRollDeg;
	Writer << CurrentFuelGallons << CurrentDamage;
	SerializeArchiveBool(Writer, bEngineRunning);
	Writer << EngineStartHoldElapsed << EngineShutdownHoldElapsed;

	uint8 SavedCameraMode = static_cast<uint8>(CameraMode);
	Writer << SavedCameraMode << CameraYawOffsetDeg << CameraPitchOffsetDeg << CameraZoomAlpha;
	Writer << CurrentCameraGroundLiftCm << CurrentCameraPullInAlpha << CurrentCameraAvoidanceOffsetDeg;
	Writer << CockpitStabilizedAttitudeDeg;
	SerializeArchiveBool(Writer, bCockpitStabilizedAttitudeInitialized);
	for (float& PanOffset : CameraViewPanOffsetsCm) Writer << PanOffset;
	Writer << RopeAutoZoomAlpha << SmoothedCameraArmLengthCm << SmoothedCameraTranslationWorld;
	Writer << SmoothedCameraViewWorldRotation;
	SerializeArchiveBool(Writer, bCameraViewSmoothingInitialized);
	SerializeArchiveBool(Writer, bIsLanded);
	Writer << GroundClearanceCm << ForwardObstacleDistanceCm;
	SerializeArchiveBool(Writer, bCheckupAutoOpenArmed);
	SerializeArchiveBool(Writer, bCheckupOpenedThisLanding);

	uint8 Tool = static_cast<uint8>(SelectedTool);
	uint8 Megaphone = static_cast<uint8>(SelectedMegaphoneMessage);
	Writer << Tool << Megaphone << ToolCooldownSeconds;
	Writer << EquipmentState.CareerEquipmentMask << EquipmentState.CareerTearGasRounds;
	Writer << MegaphoneVoiceNextIndices;
	SerializeArchiveBool(Writer, bWaterCannonInstalled);

	Writer << BucketWaterFraction << BucketWaterPounds;
	SerializeArchiveBool(Writer, bRopeDeployed);
	SerializeArchiveBool(Writer, bHarnessRopeEndSelected);
	SerializeArchiveBool(Writer, bHarnessRiderAttached);
	Writer << WinchState.NodeCursor;
	SerializeArchiveBool(Writer, WinchState.bBucketStowed);
	SerializeArchiveBool(Writer, WinchState.bHarnessStowed);
	Writer << WinchState.Command;
	uint8 RopeEnd = static_cast<uint8>(WinchState.RopeEnd);
	Writer << RopeEnd << PendingWinchCommand << RopeFirstActiveNode;
	SerializeArchiveBool(Writer, bRopeStateInitialized);
	Writer << RopeNodeWorldPositions << PreviousRopeAnchorWorld << PreviousBucketWorld;
	Writer << PreviousRopeEndDirection;

	Writer << SpotlightAimPitch1616 << SpotlightAimYaw1616 << SpotlightDistance1616;
	SerializeArchiveBool(Writer, bSpotlightTargetFrozen);
	SerializeArchiveBool(Writer, SpotlightTarget.bValid);
	Writer << SpotlightTarget.WorldLocation << SpotlightTarget.WorldNormal << SpotlightTarget.Tile;
	Writer << SpotlightTarget.DistanceUnits << SpotlightTarget.Band << SpotlightTarget.InteractionRings;
	bool bSearchLightVisible = SearchLightComponent != nullptr && SearchLightComponent->IsVisible();
	SerializeArchiveBool(Writer, bSearchLightVisible);

	TArray<uint8> ParticleFxData;
	TArray<uint8> TearGasData;
	TArray<uint8> ApacheData;
	if (WaterFXComponent == nullptr || TearGasPool == nullptr || ApachePool == nullptr ||
		!WaterFXComponent->CaptureRuntimeSaveState(ParticleFxData) ||
		!TearGasPool->CaptureRuntimeSaveState(TearGasData) ||
		!ApachePool->CaptureRuntimeSaveState(ApacheData))
	{
		OutData.Reset();
		return false;
	}
	Writer << ParticleFxData << TearGasData << ApacheData;

	int32 PassengerCount = MissionPassengerSlots.Num();
	Writer << PassengerCount;
	for (FSimCopterMissionPassengerSlot& Slot : MissionPassengerSlots)
	{
		Writer << Slot.EventId;
		uint8 Kind = static_cast<uint8>(Slot.Kind);
		Writer << Kind << Slot.HeadImageIndex << Slot.PortraitState;
		FName PersonName = Slot.Person.IsValid()
			? Slot.Person->GetRuntimeSaveIdentityName()
			: NAME_None;
		Writer << PersonName;
	}
	if (Writer.IsError())
	{
		OutData.Reset();
		return false;
	}
	return true;
}

bool ASimCopterHelicopterPawn::RestoreRuntimeSaveState(const TArray<uint8>& Data)
{
	if (Data.IsEmpty())
	{
		return false;
	}
	FMemoryReader Reader(Data, true);
	uint32 Magic = 0;
	int32 Version = 0;
	Reader << Magic << Version;
	if (Magic != AircraftRuntimeSaveMagic || Version != AircraftRuntimeSaveVersion)
	{
		return false;
	}

	FTransform ActorTransform;
	Reader << ActorTransform;
	SerializeFlightModel(Reader, FlightModel);
	SerializeArchiveBool(Reader, bFlightModelSeeded);
	Reader << VelocityCmPerSec << CurrentPitchDeg << CurrentRollDeg;
	Reader << CurrentFuelGallons << CurrentDamage;
	SerializeArchiveBool(Reader, bEngineRunning);
	Reader << EngineStartHoldElapsed << EngineShutdownHoldElapsed;

	uint8 SavedCameraMode = 0;
	Reader << SavedCameraMode << CameraYawOffsetDeg << CameraPitchOffsetDeg << CameraZoomAlpha;
	Reader << CurrentCameraGroundLiftCm << CurrentCameraPullInAlpha << CurrentCameraAvoidanceOffsetDeg;
	Reader << CockpitStabilizedAttitudeDeg;
	SerializeArchiveBool(Reader, bCockpitStabilizedAttitudeInitialized);
	for (float& PanOffset : CameraViewPanOffsetsCm) Reader << PanOffset;
	Reader << RopeAutoZoomAlpha << SmoothedCameraArmLengthCm << SmoothedCameraTranslationWorld;
	Reader << SmoothedCameraViewWorldRotation;
	SerializeArchiveBool(Reader, bCameraViewSmoothingInitialized);
	SerializeArchiveBool(Reader, bIsLanded);
	Reader << GroundClearanceCm << ForwardObstacleDistanceCm;
	SerializeArchiveBool(Reader, bCheckupAutoOpenArmed);
	SerializeArchiveBool(Reader, bCheckupOpenedThisLanding);

	uint8 Tool = 0;
	uint8 Megaphone = 0;
	Reader << Tool << Megaphone << ToolCooldownSeconds;
	Reader << EquipmentState.CareerEquipmentMask << EquipmentState.CareerTearGasRounds;
	Reader << MegaphoneVoiceNextIndices;
	SerializeArchiveBool(Reader, bWaterCannonInstalled);

	Reader << BucketWaterFraction << BucketWaterPounds;
	SerializeArchiveBool(Reader, bRopeDeployed);
	SerializeArchiveBool(Reader, bHarnessRopeEndSelected);
	SerializeArchiveBool(Reader, bHarnessRiderAttached);
	Reader << WinchState.NodeCursor;
	SerializeArchiveBool(Reader, WinchState.bBucketStowed);
	SerializeArchiveBool(Reader, WinchState.bHarnessStowed);
	Reader << WinchState.Command;
	uint8 RopeEnd = 0;
	Reader << RopeEnd << PendingWinchCommand << RopeFirstActiveNode;
	SerializeArchiveBool(Reader, bRopeStateInitialized);
	Reader << RopeNodeWorldPositions << PreviousRopeAnchorWorld << PreviousBucketWorld;
	Reader << PreviousRopeEndDirection;

	Reader << SpotlightAimPitch1616 << SpotlightAimYaw1616 << SpotlightDistance1616;
	SerializeArchiveBool(Reader, bSpotlightTargetFrozen);
	SerializeArchiveBool(Reader, SpotlightTarget.bValid);
	Reader << SpotlightTarget.WorldLocation << SpotlightTarget.WorldNormal << SpotlightTarget.Tile;
	Reader << SpotlightTarget.DistanceUnits << SpotlightTarget.Band << SpotlightTarget.InteractionRings;
	bool bSearchLightVisible = false;
	SerializeArchiveBool(Reader, bSearchLightVisible);
	TArray<uint8> ParticleFxData;
	TArray<uint8> TearGasData;
	TArray<uint8> ApacheData;
	Reader << ParticleFxData << TearGasData << ApacheData;

	int32 PassengerCount = 0;
	Reader << PassengerCount;
	if (Reader.IsError() || SavedCameraMode >= CameraModeCount ||
		Tool >= static_cast<uint8>(ESimCopterHelicopterTool::Count) ||
		Megaphone >= static_cast<uint8>(ESimCopterMegaphoneMessage::Count) ||
		RopeEnd > static_cast<uint8>(SimCopterWinch::ERopeEnd::Harness) ||
		PassengerCount < 0 || PassengerCount > 16 ||
		ParticleFxData.IsEmpty() || TearGasData.IsEmpty() || ApacheData.IsEmpty())
	{
		return false;
	}

	MissionPassengerSlots.Reset(PassengerCount);
	PendingSavedPassengerActorNames.Reset(PassengerCount);
	for (int32 Index = 0; Index < PassengerCount; ++Index)
	{
		FSimCopterMissionPassengerSlot& Slot = MissionPassengerSlots.AddDefaulted_GetRef();
		uint8 Kind = 0;
		FName PersonName;
		Reader << Slot.EventId << Kind << Slot.HeadImageIndex << Slot.PortraitState << PersonName;
		if (Kind > static_cast<uint8>(ESimCopterMissionPassengerKind::Rescue))
		{
			return false;
		}
		Slot.Kind = static_cast<ESimCopterMissionPassengerKind>(Kind);
		Slot.Person.Reset();
		PendingSavedPassengerActorNames.Add(PersonName);
	}
	if (Reader.IsError() || Reader.Tell() != Reader.TotalSize())
	{
		return false;
	}

	CameraMode = static_cast<ESimCopterCameraMode>(SavedCameraMode);
	SelectedTool = static_cast<ESimCopterHelicopterTool>(Tool);
	SelectedMegaphoneMessage = static_cast<ESimCopterMegaphoneMessage>(Megaphone);
	WinchState.RopeEnd = static_cast<SimCopterWinch::ERopeEnd>(RopeEnd);
	SpotlightTarget.HitActor.Reset();
	if (SearchLightComponent != nullptr)
	{
		SearchLightComponent->SetVisibility(bSearchLightVisible);
	}
	if (WaterFXComponent == nullptr || TearGasPool == nullptr || ApachePool == nullptr ||
		!WaterFXComponent->RestoreRuntimeSaveState(ParticleFxData) ||
		!TearGasPool->RestoreRuntimeSaveState(TearGasData) ||
		!ApachePool->RestoreRuntimeSaveState(ApacheData))
	{
		return false;
	}
	BucketWaterFraction = FMath::Clamp(BucketWaterFraction, 0.0f, 1.0f);
	BucketWaterPounds = FMath::Max(0, BucketWaterPounds);
	FlightModel.Passengers = MissionPassengerSlots.Num();
	SyncPassengerFlightModelCount();
	ResetTransientInputState();
	SetActorTransform(ActorTransform, false, nullptr, ETeleportType::TeleportPhysics);
	RecomputeActiveToolFallback();
	UpdateRopeVisuals();
	RefreshDashboardSeats();
	RefreshWaterControlsWidget();
	return true;
}

void ASimCopterHelicopterPawn::RelinkSavedMissionPassenger(
	ASimCopterGroundAgent* Person,
	const FName SavedActorName)
{
	if (Person == nullptr)
	{
		return;
	}
	for (int32 Index = 0; Index < MissionPassengerSlots.Num() && Index < PendingSavedPassengerActorNames.Num(); ++Index)
	{
		const FName Identity = SavedActorName.IsNone() ? Person->GetFName() : SavedActorName;
		if (PendingSavedPassengerActorNames[Index] == Identity)
		{
			MissionPassengerSlots[Index].Person = Person;
			PendingSavedPassengerActorNames[Index] = NAME_None;
			break;
		}
	}
}

float ASimCopterHelicopterPawn::GetAltimeterUnits() const
{
	// Zeroed on the water. FlightModel.Altitude is the original node's Y, whose datum is the
	// bottom of the terrain range, so a helicopter sitting on the ocean reads about a hundred
	// units - the needle off its stop and the rollover already showing 1. Measuring from the
	// ocean surface instead is what a pilot expects, and costs nothing but the subtraction.
	if (const ASimCity2000CityActor* City =
			const_cast<ASimCopterHelicopterPawn*>(this)->ResolveCityActor())
	{
		float OceanSurfaceZ = 0.0f;
		if (City->TryGetOceanSurfaceWorldZ(OceanSurfaceZ))
		{
			return FMath::Max(
				0.0f,
				(static_cast<float>(GetActorLocation().Z) - OceanSurfaceZ) /
					FMath::Max(0.01f, OriginalUnitToCm));
		}
	}
	return SimCopterFixed::ToFloat(FlightModel.Altitude);
}

float ASimCopterHelicopterPawn::GetAirspeedDialKnots() const
{
	// [0x37], the |velocity| the original keeps for its HUD.
	return SimCopterFixed::ToFloat(FlightModel.HorizontalSpeed);
}

bool ASimCopterHelicopterPawn::TryGetAirframeLocalBoundsCm(FBox& OutLocalBoundsCm) const
{
	// Same source as UpdateCameraAnchorFromVisibleBody: whichever fuselage is on screen, measured
	// through its own relative transform so the answer is already in ModelPivot's coordinates.
	// Rotors are separate components and stay out of it - a 5.2 m disc is not something you board.
	const USceneComponent* VisibleBody =
		bUsingOriginalMesh
			? static_cast<const USceneComponent*>(HeliBodyMeshComponent.Get())
			: static_cast<const USceneComponent*>(BodyMeshComponent.Get());
	if (VisibleBody == nullptr)
	{
		return false;
	}

	const FBoxSphereBounds BodyBounds = VisibleBody->CalcBounds(VisibleBody->GetRelativeTransform());
	if (!(BodyBounds.SphereRadius > UE_SMALL_NUMBER) || BodyBounds.BoxExtent.ContainsNaN())
	{
		return false;
	}

	OutLocalBoundsCm = BodyBounds.GetBox();
	return OutLocalBoundsCm.IsValid != 0;
}

float ASimCopterHelicopterPawn::ComputeAirframeGapCm(
	const FBox& LocalBoundsCm,
	const FTransform& BodyFrame,
	const FVector& WorldLocation,
	const bool bHorizontalOnly)
{
	if (LocalBoundsCm.IsValid == 0)
	{
		return 0.0f;
	}

	FVector LocalPoint = BodyFrame.InverseTransformPosition(WorldLocation);
	if (bHorizontalOnly)
	{
		// Project onto the box's own vertical span: someone standing beside the skids and someone
		// level with the cabin roof are both "at" the aircraft.
		LocalPoint.Z = FMath::Clamp(LocalPoint.Z, LocalBoundsCm.Min.Z, LocalBoundsCm.Max.Z);
	}
	return static_cast<float>(FMath::Sqrt(LocalBoundsCm.ComputeSquaredDistanceToPoint(LocalPoint)));
}

float ASimCopterHelicopterPawn::GetDistanceToAirframeCm(
	const FVector& WorldLocation,
	const bool bHorizontalOnly) const
{
	FBox LocalBounds(ForceInit);
	if (!TryGetAirframeLocalBoundsCm(LocalBounds))
	{
		// No fuselage built yet (a headless test, or the frame before the GEO packs load). Fall
		// back to the collision capsule's radius so the answer is still a gap to a body rather
		// than a distance to a point.
		const float CapsuleRadius =
			CollisionComponent != nullptr ? CollisionComponent->GetScaledCapsuleRadius() : 0.0f;
		const FVector Delta = WorldLocation - GetActorLocation();
		const float Distance = static_cast<float>(bHorizontalOnly ? Delta.Size2D() : Delta.Size());
		return FMath::Max(0.0f, Distance - CapsuleRadius);
	}

	const FTransform BodyFrame =
		ModelPivot != nullptr ? ModelPivot->GetComponentTransform() : GetActorTransform();
	return ComputeAirframeGapCm(LocalBounds, BodyFrame, WorldLocation, bHorizontalOnly);
}

bool ASimCopterHelicopterPawn::CanBeEnteredBy(const FVector& WorldLocation, const float ToleranceCm) const
{
	// 3D on purpose: measuring across the deck only would let a body standing under a hovering
	// aircraft read as touching it.
	return GetDistanceToAirframeCm(WorldLocation) <= FMath::Max(0.0f, ToleranceCm);
}

void ASimCopterHelicopterPawn::EnterHelicopter(APlayerController* PlayerController, const bool bBlendView)
{
	if (PlayerController == nullptr)
	{
		return;
	}

	AActor* OutgoingViewTarget = PlayerController->GetViewTarget();
	ASimCopterOnFootPawn* OutgoingOnFootPawn =
		Cast<ASimCopterOnFootPawn>(PlayerController->GetPawn());
	PlayerController->Possess(this);
	if (bBlendView)
	{
		BlendPossessionViewTarget(
			PlayerController,
			OutgoingViewTarget,
			this,
			CameraPossessionBlendSeconds);
	}
	else
	{
		// A load resumes a saved camera frame; easing from the airport's temporary on-foot pawn
		// would visibly move through a view that was never part of the save.
		PlayerController->SetViewTarget(this);
	}

	// The outgoing pawn is also the outgoing camera when boarding. Keep it valid until the
	// blend completes, while removing its body and collision from play immediately.
	if (OutgoingOnFootPawn != nullptr)
	{
		OutgoingOnFootPawn->SetActorHiddenInGame(true);
		OutgoingOnFootPawn->SetActorEnableCollision(false);
		OutgoingOnFootPawn->SetActorTickEnabled(false);
		OutgoingOnFootPawn->SetLifeSpan(CameraPossessionBlendSeconds + 0.1f);
	}

	PlayerController->bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PlayerController->SetInputMode(InputMode);
	EnsureDashboardWidget();
	EnsureMapWidget();
	EnsureWaterControlsWidget();
	EnsureToolFlapsWidget();
	EnsureCrosshairWidget();
	EnsureControllerOverlayWidget();
	EnsureHelicopterDebugPanel();

	// The cockpit HUD goes into the viewport right above, and under GameAndUI whatever Slate
	// widget holds focus eats the keys before the axis bindings ever see them. Claim it back for
	// the viewport once the panels are up: without this, climbing back in after a job on foot can
	// leave the collective going nowhere, which reads as a helicopter that will not take off.
	RestoreGameViewportFocus();
}

void ASimCopterHelicopterPawn::RestoreGameViewportFocus()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void ASimCopterHelicopterPawn::ResetTransientInputState()
{
	// Everything here is "what is the player pressing right now", none of it is simulation state,
	// and all of it goes stale the instant the pawn is unpossessed - axis bindings stop firing and
	// action Released handlers never arrive. FlushPressedKeys cannot reach the bools below: they
	// belong to this pawn, and the release it synthesises is delivered through whatever input
	// stack is current, which during a possession swap is not this one.
	// Leaving the helicopter no longer requires holding down the shutdown key (Control);
	// landing/touchdown automatically powers down the engine and allows immediate exit.
	bEngineStartHeld = false;
	bEngineShutdownHeld = false;
	bControllerEngineStartHeld = false;
	bControllerEngineShutdownHeld = false;
	bControllerRightShoulderHeld = false;
	bControllerCameraAdjustHeld = false;
	bControllerDPadUpHeld = false;
	bControllerDPadDownHeld = false;
	bControllerDPadLeftHeld = false;
	bControllerDPadRightHeld = false;
	bPrimaryToolUseHeld = false;
	bPrimaryToolUsePressed = false;
	bBucketDumpHeld = false;
	bWaterCannonHeld = false;
	EngineStartHoldElapsed = 0.0f;
	EngineStartHoldAlpha = 0.0f;
	EngineShutdownHoldElapsed = 0.0f;
	EngineShutdownHoldAlpha = 0.0f;

	PitchInput = 0.0f;
	RollInput = 0.0f;
	YawInput = 0.0f;
	CollectiveInput = 0.0f;
	CameraYawInput = 0.0f;
	CameraPitchInput = 0.0f;
	MouseLookYawInput = 0.0f;
	MouseLookPitchInput = 0.0f;
	ControllerLeftXInput = 0.0f;
	ControllerLeftYInput = 0.0f;
	ControllerRightXInput = 0.0f;
	ControllerRightYInput = 0.0f;
	ControllerRightTriggerInput = 0.0f;
	LastClimbCommand = 0;
}

// The stuck-key half, which is the one that actually stalls a takeoff. A key held across a
// possession change can have its release delivered while the input stack is being rebuilt, and
// UPlayerInput keeps believing it is down: the axis then reports the held value forever. Landing
// ends on the descend key and stepping out is the possession change, so the collective sticks at
// -1, the model is told to descend, and `ClimbCommand > 0` can never pass - a helicopter that
// simply will not take off however long you hold the collective. Pressing the key again and
// releasing it re-syncs the state, which is exactly the workaround that was found by hand.
//
// FlushPressedKeys releases everything UPlayerInput thinks is held, so both pawns start their
// possession from a known-clear keyboard. A key genuinely still held has to be re-pressed, which
// is ordinary for a control-scheme switch and much better than one that never comes back.
void ASimCopterHelicopterPawn::FlushStuckKeys(AController* ForController)
{
	if (APlayerController* PlayerController = Cast<APlayerController>(ForController))
	{
		PlayerController->FlushPressedKeys();
	}
}

void ASimCopterHelicopterPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	FlushStuckKeys(NewController);
	ResetTransientInputState();
	if (Cast<APlayerController>(NewController) != nullptr)
	{
		if (USimCopterRadioSubsystem* Radio = USimCopterRadioSubsystem::Get(this))
		{
			Radio->SetPlayerInHelicopter(true);
		}
	}
	RestoreGameViewportFocus();
}

void ASimCopterHelicopterPawn::UnPossessed()
{
	// Controller is still valid until APawn::UnPossessed clears it, and flushing here is what
	// stops the outgoing pawn's held keys from following the player to the next one.
	FlushStuckKeys(GetController());
	ResetTransientInputState();
	if (Cast<APlayerController>(GetController()) != nullptr)
	{
		if (USimCopterRadioSubsystem* Radio = USimCopterRadioSubsystem::Get(this))
		{
			Radio->SetPlayerInHelicopter(false);
		}
	}
	Super::UnPossessed();
}

bool ASimCopterHelicopterPawn::CanExitHelicopter() const
{
	return bIsLanded && GroundClearanceCm <= GroundContactTolerance + 18.0f;
}

bool ASimCopterHelicopterPawn::CanTransferMissionPassengers() const
{
	// LOW OR LANDED, never "parked". The original has no flight-state gate on getting in or out:
	// FUN_004c9bc0 (opcodes 17/21, the alight) asks only for a standable tile and for the person to
	// be within six original units of the ground under them. Requiring ESimCopterFlightState::Parked
	// on top of that meant a fare who had walked up to a hovering helicopter with its skids a hand's
	// breadth off the road was refused, which is not how the game plays.
	//
	// A rider's Y *is* the aircraft's Y (FUN_004c6450 copies the carrier's position onto them every
	// tick) and GroundClearanceCm is that same aircraft-above-ground figure, so the shipped six units
	// come straight across with no tolerance term - see PassengerAlightClearanceCm. This used to add
	// GroundContactTolerance to a 60 cm band, 88 cm in all, which is nearly a metre of hover: enough
	// that a mission's passengers were out of the cabin before the pilot had finished the descent.
	//
	// Contact is still enforced, on the walker's side, by StepTowardSelectedObject's own airframe
	// test - so this stays a pure height band, exactly as the original's is.
	return GroundClearanceCm < PassengerAlightClearanceCm;
}

bool ASimCopterHelicopterPawn::CanBoardMissionPassengers() const
{
	// FUN_004ca940 (opcode 12, the walk-and-board every passenger program reaches) accepts the move
	// once the walker's body is in contact with the airframe AND the doorsill sits under
	// `(objectY - personY) & 0xffff0000 < 0x50000`. See PassengerBoardClearanceCm for why that is
	// eight units of aircraft-above-ground rather than five.
	return GroundClearanceCm < PassengerBoardClearanceCm;
}

bool ASimCopterHelicopterPawn::TryGetRopeEndWorldLocation(FVector& OutWorldLocation) const
{
	if (!bRopeDeployed || RopeNodeWorldPositions.Num() == 0)
	{
		return false;
	}
	OutWorldLocation = RopeNodeWorldPositions.Last();
	return true;
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
		RefreshDashboardSeats();
	}
	return Removed;
}

// SCHOOK: SeatManifestAdd 0x0048bff0
int32 ASimCopterHelicopterPawn::AddMissionPassengersForMission(
	int32 Count,
	int32 EventId,
	ESimCopterMissionPassengerKind Kind,
	ASimCopterGroundAgent* Person)
{
	const int32 Added = FMath::Clamp(Count, 0, GetAvailablePassengerSeats());
	for (int32 Index = 0; Index < Added; ++Index)
	{
		FSimCopterMissionPassengerSlot Slot;
		Slot.EventId = EventId;
		Slot.Kind = Kind;
		// FUN_004c6250 builds the record before adding it: head from person+0x18e, face 1, then
		// the person id. A seat with nobody behind it keeps the struct's own defaults.
		if (Person != nullptr)
		{
			Slot.HeadImageIndex = Person->GetHeadImageIndex();
			Slot.PortraitState = Person->GetSeatPortraitMood();
			Slot.Person = Person;
		}
		MissionPassengerSlots.Add(Slot);
	}
	if (Added > 0)
	{
		SyncPassengerFlightModelCount();
		RefreshDashboardSeats();
	}
	return Added;
}

// SCHOOK: SeatManifestRemove 0x0048c120
int32 ASimCopterHelicopterPawn::RemoveMissionPassengersForMission(
	int32 Count,
	int32 EventId,
	ESimCopterMissionPassengerKind Kind,
	const ASimCopterGroundAgent* Person)
{
	int32 Removed = 0;
	// FUN_0048c120 frees the record whose +0x0c matches the person, not just any record of the
	// right shape - releasing somebody else's seat would leave their portrait behind.
	if (Person != nullptr)
	{
		for (int32 Index = 0; Index < MissionPassengerSlots.Num(); ++Index)
		{
			if (MissionPassengerSlots[Index].Person.Get() == Person)
			{
				MissionPassengerSlots.RemoveAt(Index);
				Removed = 1;
				break;
			}
		}
	}
	for (int32 Index = MissionPassengerSlots.Num() - 1; Index >= 0 && Removed < Count; --Index)
	{
		const FSimCopterMissionPassengerSlot& Slot = MissionPassengerSlots[Index];
		// Never take a seat that belongs to a live passenger of its own when standing in for an
		// abstract count; theirs is released by the branch above when they actually alight.
		if (Slot.EventId == EventId && Slot.Kind == Kind && !Slot.Person.IsValid())
		{
			MissionPassengerSlots.RemoveAt(Index);
			Removed++;
		}
	}
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
		RefreshDashboardSeats();
	}
	return Removed;
}

// SCHOOK: SeatManifestSetFace 0x0048c0e0
bool ASimCopterHelicopterPawn::SetMissionPassengerPortraitState(
	const ASimCopterGroundAgent* Person,
	int32 PortraitState)
{
	if (Person == nullptr)
	{
		return false;
	}
	for (FSimCopterMissionPassengerSlot& Slot : MissionPassengerSlots)
	{
		if (Slot.Person.Get() != Person)
		{
			continue;
		}
		// FUN_004ccb40 writes the record and marks the window dirty on every pass, and BHAV 264
		// runs it several times a second; FUN_00453cb0 then re-blits two cells on every fourth
		// frame. Slate rebuilds a widget tree instead, so the refresh is gated on an actual change.
		if (Slot.PortraitState == PortraitState)
		{
			return true;
		}
		Slot.PortraitState = PortraitState;
		RefreshDashboardSeats();
		return true;
	}
	return false;
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

	// The behaviour VM boards real people and attaches them to this pawn, so dropping one has to
	// release that person. Spawning a replacement instead - which is all this used to do - left
	// the original still riding, invisible, holding a seat that had already been given back.
	// The seat record names its own passenger (FUN_0048bff0 stores the person id at +0x0c), so
	// dragging one portrait out cannot pick a different passenger who shares its mission and kind.
	ASimCopterGroundAgent* SeatOccupant = Slot.Person.Get();
	if (SeatOccupant == nullptr)
	{
		SeatOccupant = TrafficSystem->FindPersonAboardForEvent(this, Slot.EventId, Slot.Kind);
	}
	if (ASimCopterGroundAgent* Aboard = SeatOccupant)
	{
		const FVector DropLocation = GetPassengerAirDropWorldLocation(SlotIndex);
		Aboard->AlightFromCarrier(); // hands the seat back on its own
		Aboard->SetActorLocation(DropLocation, false);
		Aboard->SetMissionPickupCounted(false);
		Aboard->BeginPassengerFall(Slot.EventId, PassengerFallInjuryDistanceCm);
		SyncPassengerFlightModelCount();
		RefreshDashboardSeats();
		if (ASimCopterMissionSystemActor* MissionActor = Cast<ASimCopterMissionSystemActor>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass())))
		{
			MissionActor->NotifyPassengerDroppedFromHelicopter(Slot.EventId, Slot.Kind, 1);
		}
		return true;
	}

	// Nobody attached: a seat filled by something other than the VM. Fall back to the stand-in.
	const int32 SpawnMode =
		Slot.Kind == ESimCopterMissionPassengerKind::Medevac ? 6 :
		Slot.Kind == ESimCopterMissionPassengerKind::Rescue ? 1 :
		4;
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
	DroppedPassenger->SetMissionPickupCreditAwarded(true);
	DroppedPassenger->SetMissionPickupCounted(false);

	MissionPassengerSlots.RemoveAt(SlotIndex);
	SyncPassengerFlightModelCount();
	RefreshDashboardSeats();

	if (ASimCopterMissionSystemActor* MissionActor = Cast<ASimCopterMissionSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass())))
	{
		MissionActor->NotifyPassengerDroppedFromHelicopter(Slot.EventId, Slot.Kind, 1);
	}
	return true;
}

// The deck a passenger steps out ONTO: beside the aircraft, at the aircraft's own height. The
// caller lifts it by the person's capsule half height to place them and lets gravity close any
// remaining gap - stepping out is a drop, however short.
FVector ASimCopterHelicopterPawn::GetPassengerDropWorldLocation(int32 SlotIndex) const
{
	const FRotationMatrix YawFrame(FRotator(0.0f, GetActorRotation().Yaw, 0.0f));
	const float SlotSide = (SlotIndex % 2 == 0) ? 1.0f : -1.0f;
	const float SlotRowOffset = SlotIndex >= 0 ? float(SlotIndex / 2) * 32.0f : 0.0f;
	// Z is the flight model's own Altitude: ApplyFlightModelToActor puts the root sphere's BOTTOM
	// there, so this is where the skids meet whatever the aircraft is standing on, and the actor
	// origin is a whole 190 cm capsule radius above it.
	const double DeckZ = GetActorLocation().Z - static_cast<double>(CollisionComponent != nullptr
		? CollisionComponent->GetScaledCapsuleHalfHeight()
		: 0.0f);
	FVector DropLocation =
		GetActorLocation() +
		YawFrame.GetUnitAxis(EAxis::Y) * (175.0f * SlotSide) -
		YawFrame.GetUnitAxis(EAxis::X) * (35.0f + SlotRowOffset);
	DropLocation.Z = DeckZ;

	if (GetWorld() != nullptr)
	{
		// DOWNWARD ONLY, and from the aircraft's own height. This used to start 900 cm ABOVE the
		// actor origin - some eleven metres up - and take the first thing it hit on the way down, so
		// a fare let out beside a helicopter parked next to anything shorter than that was put on
		// its ROOF instead of on the ground next to the aircraft. Starting at the deck means the
		// probe can only ever find the surface the aircraft is on or one below it (stepping out over
		// the lip of a pad), never one above.
		//
		// ECC_Camera, not ECC_Visibility: that is the channel pedestrians actually walk on
		// (TryGetWalkSurfaceZAt, UpdateGroundSnap), and their own capsules ignore it - on Visibility
		// they block, so a queue of passengers could stack on each other's heads.
		const FVector TraceStart = DropLocation + FVector::UpVector * PassengerDropProbeLiftCm;
		const FVector TraceEnd = DropLocation - FVector::UpVector * PassengerDropProbeDepthCm;
		FHitResult Hit;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterPassengerDrop), false, this);
		if (GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Camera, QueryParams) &&
			Hit.bBlockingHit)
		{
			// Never above the aircraft: the lift above only exists so the probe starts clear of the
			// deck itself, and a hit inside it must not push the passenger up onto the skids.
			DropLocation.Z = FMath::Min(Hit.ImpactPoint.Z, DeckZ);
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

float ASimCopterHelicopterPawn::GetCockpitScale() const
{
	const USimCopterSettings* Settings = USimCopterSettings::Get(this);
	return ToolFlapScale * (Settings != nullptr ? Settings->GetHudScale() : 1.0f);
}

void ASimCopterHelicopterPawn::AppendMissionMarkerAvoidanceWidgets(TArray<TSharedPtr<SWidget>>& OutWidgets) const
{
	if (DashboardPanel.IsValid())
	{
		OutWidgets.Add(DashboardPanel);
	}
	if (MapPanel.IsValid())
	{
		OutWidgets.Add(MapPanel);
	}
	if (WaterControlsPanel.IsValid())
	{
		OutWidgets.Add(WaterControlsPanel);
	}
	if (HelicopterDebugPanel.IsValid())
	{
		OutWidgets.Add(HelicopterDebugPanel);
	}
	if (ToolFlapsPanel.IsValid())
	{
		ToolFlapsPanel->AppendMissionMarkerAvoidanceWidgets(OutWidgets);
	}
	if (ControllerOverlayPanel.IsValid())
	{
		ControllerOverlayPanel->AppendMissionMarkerAvoidanceWidgets(OutWidgets);
	}
}

void ASimCopterHelicopterPawn::RebuildCockpitOverlays()
{
	// Every panel takes its scale at construction, so there is nothing to poke at runtime - the
	// cheapest correct answer is to build them again. Only the widgets that are already up come
	// back, so this does not conjure a dashboard for a pawn that has none.
	const bool bHadDashboard = DashboardWidget.IsValid();
	const bool bHadMap = MapWidget.IsValid();
	const bool bHadWaterControls = WaterControlsWidget.IsValid();
	const bool bHadToolFlaps = ToolFlapsWidget.IsValid();

	RemoveDashboardWidget();
	RemoveMapWidget();
	RemoveWaterControlsWidget();
	RemoveToolFlapsWidget();

	if (bHadDashboard)      { EnsureDashboardWidget(); }
	if (bHadMap)            { EnsureMapWidget(); }
	if (bHadWaterControls)  { EnsureWaterControlsWidget(); }
	if (bHadToolFlaps)      { EnsureToolFlapsWidget(); }
}

void ASimCopterHelicopterPawn::EnsureDashboardWidget()
{
	if (DashboardWidget.IsValid() || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	if (FlapArt == nullptr)
	{
		FlapArt = NewObject<USimCopterHangarArt>(this, TEXT("FlapArt"));
	}
	FlapArt->SetOriginalGameRoot(ResolveOriginalGameRoot());
	if (!FlapArt->IsUsable())
	{
		// No BMP folder: the dashboard is nothing but its artwork, so draw none of it.
		return;
	}

	TSharedRef<SSimCopterDashboard> Dashboard =
		SNew(SSimCopterDashboard)
		.Pawn(this)
		.Art(FlapArt)
		.Scale(GetCockpitScale());
	DashboardPanel = Dashboard;

	// Bottom-right, where the original's cockpit puts it.
	DashboardWidget =
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Bottom)
		[
			Dashboard
		];

	GEngine->GameViewport->AddViewportWidgetContent(DashboardWidget.ToSharedRef(), 25);
}

void ASimCopterHelicopterPawn::RemoveDashboardWidget()
{
	if (GEngine != nullptr && GEngine->GameViewport != nullptr && DashboardWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(DashboardWidget.ToSharedRef());
	}

	DashboardPanel.Reset();
	DashboardWidget.Reset();
}

void ASimCopterHelicopterPawn::EnsureMapWidget()
{
	if (MapWidget.IsValid() || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	if (FlapArt == nullptr)
	{
		FlapArt = NewObject<USimCopterHangarArt>(this, TEXT("FlapArt"));
	}
	FlapArt->SetOriginalGameRoot(ResolveOriginalGameRoot());
	if (!FlapArt->IsUsable())
	{
		return;
	}

	TSharedRef<SSimCopterMapPanel> Map =
		SNew(SSimCopterMapPanel)
		.Pawn(this)
		.Art(FlapArt)
		.Scale(GetCockpitScale());
	MapPanel = Map;

	// Anchor the map itself directly to the lower-left corner.
	MapWidget =
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		[
			Map
		];

	GEngine->GameViewport->AddViewportWidgetContent(MapWidget.ToSharedRef(), 25);
}

void ASimCopterHelicopterPawn::RemoveMapWidget()
{
	if (GEngine != nullptr && GEngine->GameViewport != nullptr && MapWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MapWidget.ToSharedRef());
	}

	MapPanel.Reset();
	MapWidget.Reset();
}

void ASimCopterHelicopterPawn::MapZoomIn()
{
	if (MapPanel.IsValid())
	{
		MapPanel->ZoomIn();
	}
}

void ASimCopterHelicopterPawn::MapZoomOut()
{
	if (MapPanel.IsValid())
	{
		MapPanel->ZoomOut();
	}
}

void ASimCopterHelicopterPawn::EnsureWaterControlsWidget()
{
	if (!bShowHelicopterDebugPanel ||
		WaterControlsWidget.IsValid() ||
		GEngine == nullptr ||
		GEngine->GameViewport == nullptr)
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
	TSharedRef<STextBlock> CapacityText =
		SNew(STextBlock)
		.Justification(ETextJustify::Left)
		.ColorAndOpacity(FLinearColor(0.82f, 0.93f, 1.0f, 0.98f))
		.ShadowOffset(FVector2D(1.0f, 1.0f))
		.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f))
		.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11));
	TSharedRef<SProgressBar> CapacityBar =
		SNew(SProgressBar)
		.Percent(0.0f)
		.BarFillType(EProgressBarFillType::LeftToRight)
		.BarFillStyle(EProgressBarFillStyle::Scale)
		.FillColorAndOpacity(FLinearColor(0.08f, 0.55f, 0.96f, 1.0f))
		.BorderPadding(FVector2D(1.0f, 1.0f));
	WaterCapacityText = CapacityText;
	WaterCapacityBar = CapacityBar;
	WaterControlsText = ControlsText;
	TSharedRef<SBorder> ControlsPanel =
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(FLinearColor(0.015f, 0.035f, 0.055f, 0.76f))
		.Padding(FMargin(10.0f, 7.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CapacityText
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 4.0f, 0.0f, 7.0f))
			[
				SNew(SBox)
				.WidthOverride(330.0f)
				.HeightOverride(14.0f)
				[
					CapacityBar
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				ControlsText
			]
		];
	WaterControlsPanel = ControlsPanel;
	WaterControlsWidget =
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(22.0f, 0.0f, 0.0f, 22.0f))
		[
			ControlsPanel
		];

	GEngine->GameViewport->AddViewportWidgetContent(WaterControlsWidget.ToSharedRef(), 24);
	RefreshWaterControlsWidget();
}

void ASimCopterHelicopterPawn::EnsureCrosshairWidget()
{
	if (CrosshairWidget.IsValid() || CrosshairComponent == nullptr)
	{
		return;
	}

	// Two white bars with a gap in the middle so the bit being aimed at is never covered. The
	// host component handles world projection; these dimensions remain screen pixels.
	const FSlateBrush* const WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const FLinearColor CrosshairColor(1.0f, 1.0f, 1.0f, 0.85f);
	auto MakeBar = [WhiteBrush, CrosshairColor](float Width, float Height)
	{
		return SNew(SBox)
			.WidthOverride(Width)
			.HeightOverride(Height)
			[
				SNew(SImage)
				.Image(WhiteBrush)
				.ColorAndOpacity(CrosshairColor)
			];
	};

	constexpr float ArmLengthPx = 11.0f;
	constexpr float ThicknessPx = 2.0f;
	constexpr float GapPx = 5.0f;
	CrosshairWidget =
		SNew(SOverlay)
		.Visibility(EVisibility::HitTestInvisible)
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				MakeBar(ArmLengthPx, ThicknessPx)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(GapPx * 2.0f).HeightOverride(ThicknessPx)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				MakeBar(ArmLengthPx, ThicknessPx)
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeBar(ThicknessPx, ArmLengthPx)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).WidthOverride(ThicknessPx).HeightOverride(GapPx * 2.0f)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeBar(ThicknessPx, ArmLengthPx)
			]
		];

	CrosshairComponent->SetSlateWidget(CrosshairWidget);
	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		CrosshairComponent->SetOwnerPlayer(PlayerController->GetLocalPlayer());
	}
	UpdateCrosshairWorldLocation();
	UpdateCrosshairVisibility();
}

void ASimCopterHelicopterPawn::RemoveCrosshairWidget()
{
	if (CrosshairComponent != nullptr)
	{
		CrosshairComponent->SetVisibility(false);
		CrosshairComponent->SetSlateWidget(nullptr);
	}
	CrosshairWidget.Reset();
}

void ASimCopterHelicopterPawn::EnsureControllerOverlayWidget()
{
	if (ControllerOverlayWidget.IsValid() || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	ControllerOverlayPanel =
		SNew(SSimCopterControllerOverlay)
			.Pawn(this);
	ControllerOverlayWidget =
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			ControllerOverlayPanel.ToSharedRef()
		];
	GEngine->GameViewport->AddViewportWidgetContent(ControllerOverlayWidget.ToSharedRef(), 60);
}

void ASimCopterHelicopterPawn::RemoveControllerOverlayWidget()
{
	if (GEngine != nullptr && GEngine->GameViewport != nullptr && ControllerOverlayWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(ControllerOverlayWidget.ToSharedRef());
	}
	ControllerOverlayPanel.Reset();
	ControllerOverlayWidget.Reset();
}

void ASimCopterHelicopterPawn::RefreshControllerOverlayRadials()
{
	if (ControllerOverlayPanel.IsValid())
	{
		ControllerOverlayPanel->RefreshRadials();
	}
}

void ASimCopterHelicopterPawn::UpdateCrosshairVisibility()
{
	if (CrosshairComponent != nullptr)
	{
		CrosshairComponent->SetVisibility(
			CrosshairWidget.IsValid() && CameraModeShowsCrosshair(CameraMode, IsApacheHelicopter()));
	}
}

void ASimCopterHelicopterPawn::UpdateCrosshairWorldLocation()
{
	if (CrosshairComponent == nullptr)
	{
		return;
	}

	const float OffsetCm = FMath::Max(1.0f, CrosshairWorldOffsetCm);
	FVector AimPoint = GetActorLocation();
	if (CameraMode == ESimCopterCameraMode::Rescue)
	{
		// The overhead mark denotes the point directly beneath the airframe, independent of
		// helicopter bank and of any orbit/look adjustment.
		AimPoint -= FVector::UpVector * OffsetCm;
	}
	else if (CameraMode == ESimCopterCameraMode::Cockpit ||
		(IsApacheHelicopter() && (CameraMode == ESimCopterCameraMode::Chase || CameraMode == ESimCopterCameraMode::Orbit)))
	{
		// Orient crosshair along the exact tool/weapon firing vector (Muzzle + Direction * Offset).
		// Used for first-person cockpit view on all helicopters, and also for behind-airframe
		// views (Chase & Orbit) on the Apache.
		FVector Muzzle = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
		ResolveToolMuzzle(Muzzle, Direction);
		AimPoint = Muzzle + Direction * OffsetCm;
	}

	CrosshairComponent->SetWorldLocation(AimPoint);
}

void ASimCopterHelicopterPawn::RemoveWaterControlsWidget()
{
	if (GEngine != nullptr && GEngine->GameViewport != nullptr && WaterControlsWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(WaterControlsWidget.ToSharedRef());
	}

	WaterCapacityBar.Reset();
	WaterCapacityText.Reset();
	WaterControlsText.Reset();
	WaterControlsPanel.Reset();
	WaterControlsWidget.Reset();
}

void ASimCopterHelicopterPawn::RefreshWaterControlsWidget()
{
	if (!WaterCapacityBar.IsValid() ||
		!WaterCapacityText.IsValid() ||
		!WaterControlsText.IsValid())
	{
		return;
	}

	const int32 CapacityPounds = FMath::Max(0, HelicopterTuning.MaxLoadPounds);
	const float CapacityFraction = CapacityPounds > 0
		? FMath::Clamp(
			static_cast<float>(BucketWaterPounds) /
				static_cast<float>(CapacityPounds),
			0.0f,
			1.0f)
		: 0.0f;
	WaterCapacityBar->SetPercent(CapacityFraction);
	WaterCapacityText->SetText(FText::FromString(FString::Printf(
		TEXT("WATER CAPACITY  %d / %d LB  (%d%%)"),
		BucketWaterPounds,
		CapacityPounds,
		FMath::RoundToInt(CapacityFraction * 100.0f))));

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

	// Contextual hints for whichever tool left click currently drives (plan section 5.2).
	const ESimCopterHelicopterTool ActiveTool = GetActiveTool();
	const TCHAR* const ActiveToolName = SimCopterHelicopterRegistry::GetToolDisplayName(ActiveTool);
	FString Controls;
	switch (ActiveTool)
	{
	case ESimCopterHelicopterTool::WaterCannon:
		Controls = FString::Printf(
			TEXT("TOOL: %s (%s)   %s\n[LEFT CLICK] fire stream   [G] dump bucket\n[R] deploy/stow   [PAGE UP] raise   [PAGE DOWN] lower"),
			ActiveToolName,
			*DescribeToolAvailability(ActiveTool),
			*BucketState);
		break;
	case ESimCopterHelicopterTool::Megaphone:
		Controls = FString::Printf(
			TEXT("TOOL: %s (%s)   MESSAGE: %s\n[LEFT CLICK] broadcast"),
			ActiveToolName,
			*DescribeToolAvailability(ActiveTool),
			SimCopterHelicopterRegistry::GetMegaphoneMessageName(SelectedMegaphoneMessage));
		break;
	case ESimCopterHelicopterTool::TearGas:
		Controls = FString::Printf(
			TEXT("TOOL: %s (%s)   ROUNDS %d / %d\n[LEFT CLICK] fire (%.1fs cooldown)"),
			ActiveToolName,
			*DescribeToolAvailability(ActiveTool),
			EquipmentState.GetTearGasRounds(),
			SimCopterHelicopterRegistry::TearGasCapacity,
			ToolCooldownSeconds);
		break;
	case ESimCopterHelicopterTool::RescueHarness:
		Controls = FString::Printf(
			TEXT("TOOL: %s (%s)   %s\n[LEFT CLICK] deploy/stow   [PAGE UP] raise   [PAGE DOWN] lower"),
			ActiveToolName,
			*DescribeToolAvailability(ActiveTool),
			bRopeDeployed ? TEXT("LOWERED") : TEXT("STOWED"));
		break;
	case ESimCopterHelicopterTool::ApacheMissile:
	case ESimCopterHelicopterTool::ApacheMachineGun:
		Controls = FString::Printf(
			TEXT("TOOL: %s (MODEL)\n[LEFT CLICK] fire"),
			ActiveToolName);
		break;
	default:
		Controls = FString::Printf(
			TEXT("TOOL: %s (%s)   %s\n[LEFT CLICK / G] dump   [R] deploy/stow   [PAGE UP] raise   [PAGE DOWN] lower"),
			ActiveToolName,
			*DescribeToolAvailability(ActiveTool),
			*BucketState);
		break;
	}
	if (!LastToolStatus.IsEmpty())
	{
		Controls += TEXT("\n");
		Controls += LastToolStatus;
	}
	WaterControlsText->SetText(FText::FromString(Controls));
}

void ASimCopterHelicopterPawn::EnsureHelicopterDebugPanel()
{
#if !UE_BUILD_SHIPPING
	if (!bShowHelicopterDebugPanel ||
		HelicopterDebugPanelWidget.IsValid() ||
		GEngine == nullptr ||
		GEngine->GameViewport == nullptr)
	{
		return;
	}

	// Top-left, above the water capacity HUD, which sits at the bottom of the same edge.
	TSharedRef<SWidget> DebugPanel = SNew(SSimCopterHelicopterDebugPanel).Pawn(this);
	HelicopterDebugPanel = DebugPanel;
	HelicopterDebugPanelWidget =
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(FMargin(22.0f, 22.0f, 0.0f, 0.0f))
		[
			DebugPanel
		];

	GEngine->GameViewport->AddViewportWidgetContent(HelicopterDebugPanelWidget.ToSharedRef(), 26);
#endif
}

void ASimCopterHelicopterPawn::EnsureToolFlapsWidget(const bool bForceCreate)
{
	if (!bShowToolFlaps ||
		ToolFlapsWidget.IsValid() ||
		GEngine == nullptr ||
		GEngine->GameViewport == nullptr)
	{
		return;
	}

	if (FlapArt == nullptr)
	{
		FlapArt = NewObject<USimCopterHangarArt>(this, TEXT("FlapArt"));
	}
	FlapArt->SetOriginalGameRoot(ResolveOriginalGameRoot());
	if (!bForceCreate && !FlapArt->IsUsable())
	{
		// No BMP folder: the flaps would be four empty rectangles, so draw nothing at all and
		// leave the water HUD as the only tool readout.
		UE_LOG(LogTemp, Warning,
			TEXT("SimCopter cockpit flaps: '%s' has no BMP folder; the tool flaps are hidden."),
			*ResolveOriginalGameRoot());
		return;
	}

	// The original pins the flaps to the right edge of its 640x480 screen (FUN_004127d0); the
	// remake keeps that edge and stacks whatever the helicopter is carrying.
	TSharedRef<SSimCopterToolFlaps> ToolFlaps =
		SNew(SSimCopterToolFlaps)
		.Pawn(this)
		.Art(FlapArt)
		.Scale(GetCockpitScale());
	ToolFlapsPanel = ToolFlaps;
	ToolFlapsWidget =
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))
		[
			ToolFlaps
		];

	GEngine->GameViewport->AddViewportWidgetContent(ToolFlapsWidget.ToSharedRef(), 24);
}

void ASimCopterHelicopterPawn::RemoveToolFlapsWidget()
{
	if (GEngine != nullptr && GEngine->GameViewport != nullptr && ToolFlapsWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(ToolFlapsWidget.ToSharedRef());
	}
	ToolFlapsPanel.Reset();
	ToolFlapsWidget.Reset();
}

void ASimCopterHelicopterPawn::SetCalibrationMode(const bool bEnable)
{
	if (bEnable)
	{
		EnsureToolFlapsWidget(/*bForceCreate=*/true);
		if (ToolFlapsPanel.IsValid() && !ToolFlapsPanel->IsCalibrationMode())
		{
			ToolFlapsPanel->ToggleCalibrationMode();
		}
	}
	else
	{
		if (ToolFlapsPanel.IsValid() && ToolFlapsPanel->IsCalibrationMode())
		{
			ToolFlapsPanel->ToggleCalibrationMode();
		}
		if (FlapArt != nullptr && !FlapArt->IsUsable())
		{
			RemoveToolFlapsWidget();
		}
	}
}

void ASimCopterHelicopterPawn::ToggleHelicopterDebugPanel()
{
	bShowHelicopterDebugPanel = !bShowHelicopterDebugPanel;
	if (bShowHelicopterDebugPanel)
	{
		EnsureWaterControlsWidget();
		EnsureHelicopterDebugPanel();
	}
	else
	{
		SetCalibrationMode(false);
		RemoveWaterControlsWidget();
		RemoveHelicopterDebugPanel();
	}
}

void ASimCopterHelicopterPawn::SimToggleFlapCalibration()
{
	if (!bShowHelicopterDebugPanel)
	{
		bShowHelicopterDebugPanel = true;
		EnsureWaterControlsWidget();
		EnsureHelicopterDebugPanel();
	}

	if (HelicopterDebugPanel.IsValid())
	{
		SSimCopterHelicopterDebugPanel* DebugPanel = static_cast<SSimCopterHelicopterDebugPanel*>(HelicopterDebugPanel.Get());
		if (DebugPanel != nullptr)
		{
			if (DebugPanel->GetActiveTab() == SSimCopterHelicopterDebugPanel::ETab::Calibration)
			{
				ToggleHelicopterDebugPanel();
			}
			else
			{
				DebugPanel->SelectTab(SSimCopterHelicopterDebugPanel::ETab::Calibration);
			}
		}
	}
}

void ASimCopterHelicopterPawn::RemoveHelicopterDebugPanel()
{
	if (GEngine != nullptr && GEngine->GameViewport != nullptr && HelicopterDebugPanelWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(HelicopterDebugPanelWidget.ToSharedRef());
	}
	HelicopterDebugPanel.Reset();
	HelicopterDebugPanelWidget.Reset();
}

void ASimCopterHelicopterPawn::RefreshDashboardSeats()
{
	NormalizeControllerPassengerSelection();
	if (DashboardPanel.IsValid())
	{
		DashboardPanel->RefreshSeats();
	}
}

bool ASimCopterHelicopterPawn::IsPassengerSlotControllerSelected(const int32 SlotIndex) const
{
	return
		(ControllerMode == ESimCopterControllerMode::PassengerSelect ||
		 ControllerMode == ESimCopterControllerMode::PassengerConfirm) &&
		ControllerPassengerSlot == SlotIndex &&
		MissionPassengerSlots.IsValidIndex(SlotIndex);
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
	// SCHOOK: HelicopterDismountSound 0x0048a580 (command 0x1a)
	// The original plays both halves back to back on the way out - the door opens, the pilot
	// steps down, the door shuts - and stops the winch, which cannot run unattended.
	if (USimCopterAudioSubsystem* Audio = GetHelicopterAudio())
	{
		Audio->Play3D(SimCopterSound::SND_DOROPN, GetActorLocation());
		Audio->Play3D(SimCopterSound::SND_DORCLS, GetActorLocation());
		Audio->Stop(SimCopterSound::SND_WINCHLP);
		Audio->Stop(SimCopterSound::SND_WATERCAN);
		Audio->Stop(SimCopterSound::SND_MACHGUN1);
	}

	AActor* OutgoingViewTarget = PlayerController->GetViewTarget();
	RemoveDashboardWidget();
	RemoveMapWidget();
	RemoveWaterControlsWidget();
	RemoveToolFlapsWidget();
	RemoveCrosshairWidget();
	RemoveControllerOverlayWidget();
	RemoveHelicopterDebugPanel();

	// The pilot steps out of the cabin door, not onto a spot two and a half metres off the skid.
	// Measure the offset from the rendered fuselage's own box (the same source boarding uses in
	// GetDistanceToAirframeCm) so it lands just clear of whichever model is being flown: level with
	// the middle of the body fore-and-aft, and ExitClearanceCm outboard of its side. ExitOffset is
	// the fallback for a frame where no fuselage has been built yet - a headless test, or before the
	// GEO packs load.
	const FRotationMatrix YawFrame(FRotator(0.0f, GetActorRotation().Yaw, 0.0f));
	FVector2D DoorOffset(ExitOffset.X, ExitOffset.Y);
	FBox AirframeBounds(ForceInit);
	if (TryGetAirframeLocalBoundsCm(AirframeBounds))
	{
		DoorOffset.X = static_cast<float>(AirframeBounds.GetCenter().X);
		DoorOffset.Y = static_cast<float>(AirframeBounds.Max.Y) + ExitClearanceCm;
	}
	// The pilot steps out AT THE AIRCRAFT'S OWN HEIGHT. `ApplyFlightModelToActor` pins the root
	// sphere's bottom to the flight model's Altitude, so that is where the skids meet whatever the
	// machine is standing on; the actor origin is a whole 190 cm capsule radius above it and is not
	// a place anybody stands.
	double DeckZ = GetActorLocation().Z - static_cast<double>(CollisionComponent != nullptr
		? CollisionComponent->GetScaledCapsuleHalfHeight()
		: 0.0f);
	FVector ExitLocation =
		GetActorLocation() +
		YawFrame.GetUnitAxis(EAxis::X) * DoorOffset.X +
		YawFrame.GetUnitAxis(EAxis::Y) * DoorOffset.Y;

	// DOWNWARD ONLY, from just above that deck. This used to start 1200 cm ABOVE the actor origin -
	// some fourteen metres up - and take the first blocking hit on the way down, so stepping out
	// next to anything taller than the aircraft put the pilot on ITS roof, high in the air, falling.
	// Starting at the deck means the probe can only find the surface the aircraft is on, or a lower
	// one where the door opens over the lip of a pad. ECC_Camera is the channel walkable surfaces
	// answer here (the ground agents' own probes use it).
	const FVector TraceStart = FVector(ExitLocation.X, ExitLocation.Y, DeckZ + PassengerDropProbeLiftCm);
	const FVector TraceEnd = FVector(ExitLocation.X, ExitLocation.Y, DeckZ - PassengerDropProbeDepthCm);
	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterHelicopterExit), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Camera, QueryParams) &&
		Hit.bBlockingHit)
	{
		// Never above the aircraft: the lift only exists so the probe starts clear of the deck.
		DeckZ = FMath::Min(Hit.ImpactPoint.Z, DeckZ);
	}
	// Feet on that surface: the on-foot pawn's origin is the middle of its 92 cm capsule.
	ExitLocation.Z = DeckZ + 94.0;

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
		BlendPossessionViewTarget(
			PlayerController,
			OutgoingViewTarget,
			OnFootPawn,
			CameraPossessionBlendSeconds);
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

void ASimCopterHelicopterPawn::ControllerLeftX(float Value)
{
	ControllerLeftXInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void ASimCopterHelicopterPawn::ControllerLeftY(float Value)
{
	ControllerLeftYInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void ASimCopterHelicopterPawn::ControllerRightX(float Value)
{
	ControllerRightXInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void ASimCopterHelicopterPawn::ControllerRightY(float Value)
{
	ControllerRightYInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void ASimCopterHelicopterPawn::ControllerRightTrigger(float Value)
{
	ControllerRightTriggerInput = FMath::Clamp(Value, 0.0f, 1.0f);
}

void ASimCopterHelicopterPawn::ControllerDispatchWheelPressed()
{
	CloseControllerMode();
	ControllerMode = ESimCopterControllerMode::DispatchWheel;
	ControllerRadialIndex = FMath::Clamp(
		SelectedDispatchService,
		0,
		static_cast<int32>(SimCopterDispatch::EService::Count) - 1);
	RefreshControllerOverlayRadials();
	RefreshDashboardSeats();
}

void ASimCopterHelicopterPawn::ControllerDispatchWheelReleased()
{
	if (ControllerMode == ESimCopterControllerMode::DispatchWheel)
	{
		ControllerMode = ESimCopterControllerMode::None;
		RefreshDashboardSeats();
	}
}

void ASimCopterHelicopterPawn::ControllerToolWheelPressed()
{
	CloseControllerMode();
	RebuildControllerToolWheel();
	ControllerMode = ESimCopterControllerMode::ToolWheel;
	RefreshControllerOverlayRadials();
	RefreshDashboardSeats();
}

void ASimCopterHelicopterPawn::ControllerToolWheelReleased()
{
	if (ControllerMode != ESimCopterControllerMode::ToolWheel)
	{
		return;
	}

	if (ControllerToolWheelTools.IsValidIndex(ControllerRadialIndex))
	{
		SetSelectedTool(ControllerToolWheelTools[ControllerRadialIndex]);
	}
	ControllerMode = ESimCopterControllerMode::None;
	RefreshDashboardSeats();
}

void ASimCopterHelicopterPawn::ControllerCameraAdjustPressed()
{
	bControllerCameraAdjustHeld = true;
}

void ASimCopterHelicopterPawn::ControllerCameraAdjustReleased()
{
	bControllerCameraAdjustHeld = false;
}

void ASimCopterHelicopterPawn::ControllerRightShoulderPressed()
{
	bControllerRightShoulderHeld = true;
}

void ASimCopterHelicopterPawn::ControllerRightShoulderReleased()
{
	bControllerRightShoulderHeld = false;
}

void ASimCopterHelicopterPawn::ControllerPrimaryPressed()
{
	switch (ControllerMode)
	{
	case ESimCopterControllerMode::DispatchWheel:
		RequestDispatch(ControllerRadialIndex, /*bChaseSpotlight=*/false, /*bClearInstead=*/false);
		break;
	case ESimCopterControllerMode::PassengerSelect:
		if (MissionPassengerSlots.IsValidIndex(ControllerPassengerSlot))
		{
			ControllerPassengerConfirmChoice = 0;
			ControllerMode = ESimCopterControllerMode::PassengerConfirm;
			RefreshDashboardSeats();
		}
		break;
	case ESimCopterControllerMode::PassengerConfirm:
		ConfirmControllerPassengerAction();
		break;
	case ESimCopterControllerMode::ToolWheel:
		// Tool selection commits on LT release so A can never leak through as a tool fire.
		break;
	default:
		StartPrimaryToolUse();
		break;
	}
}

void ASimCopterHelicopterPawn::ControllerPrimaryReleased()
{
	// Safe for discrete tools as well as held ones, and clears a held tool if another controller
	// context was opened before A came back up.
	StopPrimaryToolUse();
}

void ASimCopterHelicopterPawn::ControllerPassengerPressed()
{
	if (ControllerMode == ESimCopterControllerMode::DispatchWheel)
	{
		RequestDispatch(ControllerRadialIndex, /*bChaseSpotlight=*/true, /*bClearInstead=*/false);
		return;
	}
	if (ControllerMode == ESimCopterControllerMode::ToolWheel)
	{
		return;
	}
	if (ControllerMode == ESimCopterControllerMode::PassengerSelect ||
		ControllerMode == ESimCopterControllerMode::PassengerConfirm)
	{
		ControllerMode = ESimCopterControllerMode::None;
		RefreshDashboardSeats();
		return;
	}

	NormalizeControllerPassengerSelection();
	ControllerMode = ESimCopterControllerMode::PassengerSelect;
	RefreshDashboardSeats();
}

void ASimCopterHelicopterPawn::ControllerCancelPressed()
{
	switch (ControllerMode)
	{
	case ESimCopterControllerMode::DispatchWheel:
		ClearAllDispatchVehicles();
		break;
	case ESimCopterControllerMode::ToolWheel:
		SetSelectedTool(ControllerToolWheelOriginal);
		ControllerMode = ESimCopterControllerMode::None;
		RefreshDashboardSeats();
		break;
	case ESimCopterControllerMode::PassengerConfirm:
		ControllerMode = ESimCopterControllerMode::PassengerSelect;
		RefreshDashboardSeats();
		break;
	case ESimCopterControllerMode::PassengerSelect:
		ControllerMode = ESimCopterControllerMode::None;
		RefreshDashboardSeats();
		break;
	default:
		break;
	}
}

void ASimCopterHelicopterPawn::ControllerEnterExitPressed()
{
	if (ControllerMode == ESimCopterControllerMode::None)
	{
		Interact();
	}
}

void ASimCopterHelicopterPawn::ControllerBackPressed()
{
	if (ControllerMode == ESimCopterControllerMode::None)
	{
		CycleCameraMode();
	}
}

void ASimCopterHelicopterPawn::ControllerSearchLightPressed()
{
	if (ControllerMode == ESimCopterControllerMode::None)
	{
		ToggleSearchLight();
	}
}

void ASimCopterHelicopterPawn::ControllerDPadUpPressed()
{
	bControllerDPadUpHeld = true;
	if (ControllerMode == ESimCopterControllerMode::PassengerConfirm)
	{
		ControllerPassengerConfirmChoice = 0;
	}
	else if (ControllerMode == ESimCopterControllerMode::None &&
		!bControllerCameraAdjustHeld &&
		GetActiveTool() == ESimCopterHelicopterTool::Megaphone)
	{
		CycleMegaphoneMessage(-1);
	}
}

void ASimCopterHelicopterPawn::ControllerDPadUpReleased()
{
	bControllerDPadUpHeld = false;
}

void ASimCopterHelicopterPawn::ControllerDPadDownPressed()
{
	bControllerDPadDownHeld = true;
	if (ControllerMode == ESimCopterControllerMode::PassengerConfirm)
	{
		ControllerPassengerConfirmChoice = 1;
	}
	else if (ControllerMode == ESimCopterControllerMode::None &&
		!bControllerCameraAdjustHeld &&
		GetActiveTool() == ESimCopterHelicopterTool::Megaphone)
	{
		CycleMegaphoneMessage(1);
	}
}

void ASimCopterHelicopterPawn::ControllerDPadDownReleased()
{
	bControllerDPadDownHeld = false;
}

void ASimCopterHelicopterPawn::ControllerDPadLeftPressed()
{
	bControllerDPadLeftHeld = true;
	if (ControllerMode == ESimCopterControllerMode::PassengerSelect)
	{
		StepControllerPassengerSelection(-1);
	}
	else if (ControllerMode == ESimCopterControllerMode::PassengerConfirm)
	{
		ControllerPassengerConfirmChoice =
			(ControllerPassengerConfirmChoice + 1) % 2;
	}
}

void ASimCopterHelicopterPawn::ControllerDPadLeftReleased()
{
	bControllerDPadLeftHeld = false;
}

void ASimCopterHelicopterPawn::ControllerDPadRightPressed()
{
	bControllerDPadRightHeld = true;
	if (ControllerMode == ESimCopterControllerMode::PassengerSelect)
	{
		StepControllerPassengerSelection(1);
	}
	else if (ControllerMode == ESimCopterControllerMode::PassengerConfirm)
	{
		ControllerPassengerConfirmChoice =
			(ControllerPassengerConfirmChoice + 1) % 2;
	}
}

void ASimCopterHelicopterPawn::ControllerDPadRightReleased()
{
	bControllerDPadRightHeld = false;
}

void ASimCopterHelicopterPawn::UpdateControllerInput(const float DeltaSeconds)
{
	UpdateControllerRadialSelection();

	const bool bRadialOwnsRightStick =
		ControllerMode == ESimCopterControllerMode::DispatchWheel ||
		ControllerMode == ESimCopterControllerMode::ToolWheel;
	const bool bCameraAdjust = bControllerCameraAdjustHeld && !bRadialOwnsRightStick;
	const SimCopterControllerInput::FFlightRouting Routing =
		SimCopterControllerInput::ResolveFlightRouting(
			ControllerLeftXInput,
			ControllerLeftYInput,
			ControllerRightYInput,
			bCameraAdjust,
			bControllerRightShoulderHeld,
			ControllerRightTriggerInput);

	bControllerEngineStartHeld = Routing.CollectiveCommand > 0;
	bControllerEngineShutdownHeld = Routing.CollectiveCommand < 0;

	if (bCameraAdjust)
	{
		if (!FMath::IsNearlyZero(Routing.CameraZoomCommand))
		{
			CameraZoomAlpha = FMath::Clamp(
				CameraZoomAlpha -
					Routing.CameraZoomCommand * ControllerCameraZoomAlphaPerSecond * DeltaSeconds,
				0.0f,
				1.0f);
		}

		if (!CameraModeIsFirstPerson(CameraMode) && Routing.CameraVerticalCommand != 0)
		{
			float& PanOffset = CameraViewPanOffsetsCm[GetCameraModeIndex(CameraMode)];
			PanOffset = FMath::Clamp(
				PanOffset -
					static_cast<float>(Routing.CameraVerticalCommand) *
						ControllerCameraPanCmPerSecond *
						DeltaSeconds,
				-CameraPanMaxOffsetCm,
				CameraPanMaxOffsetCm);
		}
	}

	UpdateControllerToolManipulation();
}

void ASimCopterHelicopterPawn::UpdateControllerRadialSelection()
{
	int32 SlotCount = 0;
	if (ControllerMode == ESimCopterControllerMode::DispatchWheel)
	{
		SlotCount = static_cast<int32>(SimCopterDispatch::EService::Count);
	}
	else if (ControllerMode == ESimCopterControllerMode::ToolWheel)
	{
		SlotCount = ControllerToolWheelTools.Num();
	}
	else
	{
		return;
	}

	const int32 NewIndex = SimCopterControllerInput::ResolveRadialIndex(
		FVector2D(ControllerRightXInput, ControllerRightYInput),
		SlotCount,
		ControllerRadialIndex);
	if (NewIndex == INDEX_NONE || NewIndex == ControllerRadialIndex)
	{
		return;
	}

	ControllerRadialIndex = NewIndex;
	if (ControllerMode == ESimCopterControllerMode::DispatchWheel)
	{
		SelectedDispatchService = ControllerRadialIndex;
	}
}

void ASimCopterHelicopterPawn::UpdateControllerToolManipulation()
{
	int32 WinchDirection = 0;
	bool bHarness = false;
	float SpotlightPitch = 0.0f;
	float SpotlightYaw = 0.0f;

	if (ControllerMode == ESimCopterControllerMode::None)
	{
		const int32 VerticalDPad =
			(bControllerDPadUpHeld ? 1 : 0) -
			(bControllerDPadDownHeld ? 1 : 0);
		const int32 HorizontalDPad =
			(bControllerDPadRightHeld ? 1 : 0) -
			(bControllerDPadLeftHeld ? 1 : 0);

		if (bControllerCameraAdjustHeld)
		{
			// R3+D-pad retains full two-axis spotlight aim even when the selected tool normally
			// consumes D-pad up/down for a winch or megaphone sub-selection.
			SpotlightPitch = static_cast<float>(VerticalDPad);
			SpotlightYaw = static_cast<float>(HorizontalDPad);
		}
		else
		{
			const ESimCopterHelicopterTool Tool = GetActiveTool();
			if (Tool == ESimCopterHelicopterTool::WaterBucket ||
				Tool == ESimCopterHelicopterTool::RescueHarness)
			{
				WinchDirection = VerticalDPad;
				bHarness = Tool == ESimCopterHelicopterTool::RescueHarness;
			}
			else if (Tool != ESimCopterHelicopterTool::Megaphone)
			{
				SpotlightPitch = static_cast<float>(VerticalDPad);
			}
			SpotlightYaw = static_cast<float>(HorizontalDPad);
		}
	}

	if (WinchDirection != ControllerAppliedWinchDirection ||
		bHarness != bControllerAppliedWinchHarness)
	{
		SetWinchHeldInput(bHarness, WinchDirection);
		ControllerAppliedWinchDirection = WinchDirection;
		bControllerAppliedWinchHarness = bHarness;
	}
	ControllerSpotlightAimPitchInput = SpotlightPitch;
	ControllerSpotlightAimYawInput = SpotlightYaw;
}

void ASimCopterHelicopterPawn::RebuildControllerToolWheel()
{
	ControllerToolWheelTools.Reset();
	ControllerToolWheelOriginal = SelectedTool;

	for (int32 Index = 0; Index < static_cast<int32>(ESimCopterHelicopterTool::Count); ++Index)
	{
		const ESimCopterHelicopterTool Tool = static_cast<ESimCopterHelicopterTool>(Index);
		if (IsToolSelectable(Tool) && IsToolAvailable(Tool))
		{
			ControllerToolWheelTools.Add(Tool);
		}
	}

	ControllerRadialIndex = ControllerToolWheelTools.IndexOfByKey(GetActiveTool());
	if (ControllerRadialIndex == INDEX_NONE)
	{
		ControllerRadialIndex = 0;
	}
}

void ASimCopterHelicopterPawn::CloseControllerMode()
{
	if (ControllerMode == ESimCopterControllerMode::ToolWheel)
	{
		SetSelectedTool(ControllerToolWheelOriginal);
	}
	ControllerMode = ESimCopterControllerMode::None;
	StopPrimaryToolUse();
}

void ASimCopterHelicopterPawn::NormalizeControllerPassengerSelection()
{
	if (MissionPassengerSlots.Num() == 0)
	{
		ControllerPassengerSlot = INDEX_NONE;
		return;
	}
	ControllerPassengerSlot = FMath::Clamp(ControllerPassengerSlot, 0, MissionPassengerSlots.Num() - 1);
}

void ASimCopterHelicopterPawn::StepControllerPassengerSelection(const int32 Delta)
{
	const int32 Count = MissionPassengerSlots.Num();
	if (Count <= 0 || Delta == 0)
	{
		ControllerPassengerSlot = INDEX_NONE;
		return;
	}
	NormalizeControllerPassengerSelection();
	ControllerPassengerSlot =
		((ControllerPassengerSlot + Delta) % Count + Count) % Count;
	RefreshDashboardSeats();
}

void ASimCopterHelicopterPawn::ConfirmControllerPassengerAction()
{
	if (ControllerPassengerConfirmChoice != 0)
	{
		ControllerMode = ESimCopterControllerMode::PassengerSelect;
		return;
	}

	if (MissionPassengerSlots.IsValidIndex(ControllerPassengerSlot))
	{
		DropPassengerAtSlot(ControllerPassengerSlot);
	}
	NormalizeControllerPassengerSelection();
	ControllerMode = ESimCopterControllerMode::PassengerSelect;
	RefreshDashboardSeats();
}

void ASimCopterHelicopterPawn::ToggleGamePause()
{
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	UWorld* World = GetWorld();
	if (PlayerController == nullptr || World == nullptr)
	{
		return;
	}

	const bool bShouldPause = !World->IsPaused();
	if (bShouldPause)
	{
		CloseControllerMode();
		bControllerEngineStartHeld = false;
		bControllerEngineShutdownHeld = false;
		bControllerRightShoulderHeld = false;
		bControllerCameraAdjustHeld = false;
		bControllerDPadUpHeld = false;
		bControllerDPadDownHeld = false;
		bControllerDPadLeftHeld = false;
		bControllerDPadRightHeld = false;
		UpdateControllerToolManipulation();
	}
	else
	{
		// Press/release bindings other than Start do not execute while paused. Resample the two
		// held modifiers that affect flight immediately so a release during pause cannot stick.
		bControllerRightShoulderHeld =
			PlayerController->IsInputKeyDown(EKeys::Gamepad_RightShoulder);
		bControllerCameraAdjustHeld =
			PlayerController->IsInputKeyDown(EKeys::Gamepad_RightThumbstick);
	}
	PlayerController->SetPause(bShouldPause);
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

void ASimCopterHelicopterPawn::StartCameraPanDrag()
{
	++CameraPanButtonCount;
	bCameraPanDragActive = true;
}

void ASimCopterHelicopterPawn::StopCameraPanDrag()
{
	CameraPanButtonCount = FMath::Max(0, CameraPanButtonCount - 1);
	bCameraPanDragActive = CameraPanButtonCount > 0;
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

void ASimCopterHelicopterPawn::ToggleRopeFromDebugPanel()
{
	ToggleRope();
}

void ASimCopterHelicopterPawn::ToggleRope()
{
	// Issues the same command the raise/lower keys would, for whichever attachment the active
	// tool selects, and lets the winch state machine run it out over the following frames.
	const bool bHarnessSelected = GetActiveTool() == ESimCopterHelicopterTool::RescueHarness;
	const bool bThisAttachmentOut =
		bHarnessSelected ? !WinchState.bHarnessStowed : !WinchState.bBucketStowed;

	if (bThisAttachmentOut)
	{
		PendingWinchCommand = bHarnessSelected
			? SimCopterWinch::ResolveRaiseHarnessCommand(WinchState)
			: SimCopterWinch::ResolveRaiseBucketCommand(WinchState);
	}
	else
	{
		PendingWinchCommand = bHarnessSelected
			? SimCopterWinch::ResolveLowerHarnessCommand(WinchState)
			: SimCopterWinch::ResolveLowerBucketCommand(WinchState);
	}
	RefreshWaterControlsWidget();
}

// SCHOOK: HelicopterToolInput 0x00485f50
// The original reads every tool from one control block per frame; left click and the debug
// panel's USE button both land here so there is exactly one dispatch path (plan 5.2).
void ASimCopterHelicopterPawn::StartPrimaryToolUse()
{
	if (bPrimaryToolUseHeld)
	{
		return;
	}
	bPrimaryToolUseHeld = true;
	bPrimaryToolUsePressed = true;
	RefreshWaterControlsWidget();
}

void ASimCopterHelicopterPawn::StopPrimaryToolUse()
{
	bPrimaryToolUseHeld = false;
	bPrimaryToolUsePressed = false;
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

// --- Tool identity and effective capability (plan section 5.1) ---

int32 ASimCopterHelicopterPawn::GetModelCapabilityMask() const
{
	// The Apache's weapons are model capabilities, never career equipment bits; they are
	// tracked as a separate mask so the career mask stays untouched by model switches.
	const FSimCopterHelicopterDefinition* Definition = GetHelicopterDefinition();
	return (Definition != nullptr && Definition->bApacheArmament) ? 1 : 0;
}

bool ASimCopterHelicopterPawn::IsToolSelectable(ESimCopterHelicopterTool Tool) const
{
	if (Tool == ESimCopterHelicopterTool::ApacheMissile ||
		Tool == ESimCopterHelicopterTool::ApacheMachineGun)
	{
		return GetModelCapabilityMask() != 0;
	}
	return Tool != ESimCopterHelicopterTool::Count;
}

ESimCopterToolAvailability ASimCopterHelicopterPawn::GetToolAvailability(ESimCopterHelicopterTool Tool) const
{
	if (Tool == ESimCopterHelicopterTool::ApacheMissile ||
		Tool == ESimCopterHelicopterTool::ApacheMachineGun)
	{
		return GetModelCapabilityMask() != 0
			? ESimCopterToolAvailability::Model
			: ESimCopterToolAvailability::Unavailable;
	}

	const int32 Bit = SimCopterHelicopterRegistry::GetToolCareerBit(Tool);
	if (EquipmentState.HasCareerBit(Bit))
	{
		return ESimCopterToolAvailability::Career;
	}
	if (EquipmentState.HasDebugBit(Bit))
	{
		return ESimCopterToolAvailability::DebugGrant;
	}
	return ESimCopterToolAvailability::Unavailable;
}

bool ASimCopterHelicopterPawn::IsToolAvailable(ESimCopterHelicopterTool Tool) const
{
	return GetToolAvailability(Tool) != ESimCopterToolAvailability::Unavailable;
}

FString ASimCopterHelicopterPawn::DescribeToolAvailability(ESimCopterHelicopterTool Tool) const
{
	switch (GetToolAvailability(Tool))
	{
	case ESimCopterToolAvailability::Career:
		return TEXT("CAREER");
	case ESimCopterToolAvailability::DebugGrant:
		return TEXT("DEBUG GRANT");
	case ESimCopterToolAvailability::Model:
		return TEXT("MODEL");
	default:
		return TEXT("UNAVAILABLE");
	}
}

void ASimCopterHelicopterPawn::SetSelectedTool(ESimCopterHelicopterTool Tool)
{
	if (Tool == ESimCopterHelicopterTool::Count)
	{
		return;
	}

	// The remembered selection is kept even when unavailable so the panel can offer a
	// session grant instead of silently jumping to another tool.
	SelectedTool = Tool;
	StopPrimaryToolUse();
	bWaterCannonHeld = false;
	bBucketDumpHeld = false;
	RefreshWaterControlsWidget();
}

void ASimCopterHelicopterPawn::CycleSelectedTool(int32 Delta)
{
	const int32 Count = static_cast<int32>(ESimCopterHelicopterTool::Count);
	if (Count <= 0 || Delta == 0)
	{
		return;
	}

	// Apache entries only exist in the ring while the Apache is active.
	for (int32 Attempt = 1; Attempt <= Count; ++Attempt)
	{
		const int32 Index = ((static_cast<int32>(SelectedTool) + Delta * Attempt) % Count + Count) % Count;
		const ESimCopterHelicopterTool Candidate = static_cast<ESimCopterHelicopterTool>(Index);
		if (IsToolSelectable(Candidate))
		{
			SetSelectedTool(Candidate);
			return;
		}
	}
}

ESimCopterHelicopterTool ASimCopterHelicopterPawn::GetActiveTool() const
{
	if (IsToolSelectable(SelectedTool))
	{
		return SelectedTool;
	}

	// Model-specific tool selected on a model that does not have it: fall back to the first
	// available normal tool for input while the selection itself is remembered.
	for (int32 Index = 0; Index < static_cast<int32>(ESimCopterHelicopterTool::Count); ++Index)
	{
		const ESimCopterHelicopterTool Candidate = static_cast<ESimCopterHelicopterTool>(Index);
		if (IsToolSelectable(Candidate) && IsToolAvailable(Candidate))
		{
			return Candidate;
		}
	}
	return ESimCopterHelicopterTool::WaterBucket;
}

void ASimCopterHelicopterPawn::RecomputeActiveToolFallback()
{
	if (!IsToolSelectable(SelectedTool))
	{
		LastToolStatus = FString::Printf(
			TEXT("%s is not available on this model; using %s."),
			SimCopterHelicopterRegistry::GetToolDisplayName(SelectedTool),
			SimCopterHelicopterRegistry::GetToolDisplayName(GetActiveTool()));
	}
}

void ASimCopterHelicopterPawn::SetDebugToolGrant(ESimCopterHelicopterTool Tool, bool bGranted)
{
	const int32 Bit = SimCopterHelicopterRegistry::GetToolCareerBit(Tool);
	if (Bit == 0)
	{
		// Apache weapons are not career equipment and cannot be granted.
		return;
	}

	if (bGranted)
	{
		EquipmentState.DebugGrantedEquipmentMask |= Bit;
		if (Tool == ESimCopterHelicopterTool::TearGas && EquipmentState.GetTearGasRounds() <= 0)
		{
			// FUN_0042d840 stocks ten rounds with the launcher; a grant matches that so the
			// tool is immediately testable without also editing ammo.
			EquipmentState.DebugRefillTearGas();
		}
	}
	else
	{
		EquipmentState.DebugGrantedEquipmentMask &= ~Bit;
	}

	// The already-shipped cannon path still reads this flag.
	bWaterCannonInstalled = IsToolAvailable(ESimCopterHelicopterTool::WaterCannon);
	RefreshWaterControlsWidget();
}

void ASimCopterHelicopterPawn::SetCareerEquipmentOwned(ESimCopterHelicopterTool Tool, bool bOwned)
{
	const int32 Bit = SimCopterHelicopterRegistry::GetToolCareerBit(Tool);
	if (Bit == 0)
	{
		// The Apache's armament comes from the model, not the shop.
		return;
	}

	if (bOwned)
	{
		EquipmentState.CareerEquipmentMask |= Bit;
		if (Tool == ESimCopterHelicopterTool::TearGas)
		{
			// FUN_0042d840: `career[0x54] = 10` with the launcher, not `+=`.
			EquipmentState.CareerTearGasRounds = SimCopterHelicopterRegistry::TearGasCapacity;
		}
	}
	else
	{
		EquipmentState.CareerEquipmentMask &= ~Bit;
		if (Tool == ESimCopterHelicopterTool::TearGas)
		{
			// FUN_0042d9f0 sells the rounds with the launcher.
			EquipmentState.CareerTearGasRounds = 0;
		}
	}

	bWaterCannonInstalled = IsToolAvailable(ESimCopterHelicopterTool::WaterCannon);
	RefreshWaterControlsWidget();
}

void ASimCopterHelicopterPawn::DebugRefillTearGas()
{
	EquipmentState.DebugRefillTearGas();
}

void ASimCopterHelicopterPawn::CycleMegaphoneMessage(int32 Delta)
{
	const int32 Count = static_cast<int32>(ESimCopterMegaphoneMessage::Count);
	if (Count <= 0)
	{
		return;
	}
	const int32 Index = ((static_cast<int32>(SelectedMegaphoneMessage) + Delta) % Count + Count) % Count;
	SelectedMegaphoneMessage = static_cast<ESimCopterMegaphoneMessage>(Index);
}

void ASimCopterHelicopterPawn::SetSelectedMegaphoneMessage(const ESimCopterMegaphoneMessage Message)
{
	if (Message < ESimCopterMegaphoneMessage::Count)
	{
		SelectedMegaphoneMessage = Message;
	}
}

bool ASimCopterHelicopterPawn::SendMegaphoneMessage(const ESimCopterMegaphoneMessage Message)
{
	if (Message >= ESimCopterMegaphoneMessage::Count)
	{
		return false;
	}

	// SCHOOK: MegaphoneCommand 0x0044ac80. F6-F10 select, voice and broadcast in the same
	// command dispatch. A popup choice is the mouse equivalent of one of those discrete keys.
	SetSelectedMegaphoneMessage(Message);
	SetSelectedTool(ESimCopterHelicopterTool::Megaphone);
	return TryBeginToolUse(ESimCopterHelicopterTool::Megaphone);
}

void ASimCopterHelicopterPawn::SetWinchHeldInput(const bool bHarness, const int32 Direction)
{
	WinchHeldDirection = FMath::Clamp(Direction, -1, 1);
	bWinchHeldHarness = bHarness;
	if (WinchHeldDirection != 0)
	{
		// Taking the rocker cancels a one-shot still running from ToggleRope, so the two
		// cannot fight over the cursor.
		PendingWinchCommand = SimCopterWinch::CommandIdle;
	}
	RefreshWaterControlsWidget();
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

// ---------------------------------------------------------------------------
// Emergency dispatch (F2-F5).
//
// SCHOOK: DispatchCommand 0x0048a580
// The original reads the target tile from the spotlight's ground node, not the
// helicopter, tests DAT_0051a078 (Shift) to decide dispatch vs release, and routes
// through FUN_004be910. Decode:
// Docs/scratchpad/ghidra/emergency_dispatch_decode_20260725.md sections 1 and 7.
// ---------------------------------------------------------------------------

bool ASimCopterHelicopterPawn::IsDispatchClearModifierHeld() const
{
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	return PlayerController != nullptr && PlayerController->IsInputKeyDown(EKeys::LeftShift);
}

void ASimCopterHelicopterPawn::RequestDispatch(int32 ServiceIndex, bool bChaseSpotlight, bool bClearInstead)
{
	const int32 ServiceCount = static_cast<int32>(SimCopterDispatch::EService::Count);
	if (ServiceIndex < 0 || ServiceIndex >= ServiceCount)
	{
		return;
	}
	const SimCopterDispatch::EService Service = static_cast<SimCopterDispatch::EService>(ServiceIndex);

	ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterTrafficSystemActor::StaticClass()));
	if (TrafficSystem == nullptr)
	{
		LastDispatchStatus = TEXT("No traffic system in this map - dispatch unavailable.");
		UE_LOG(LogSimCopterHelicopterPawn, Display, TEXT("Dispatch: %s"), *LastDispatchStatus);
		return;
	}

	// The dispatcher aims where the spotlight lands. Without a valid target there is no
	// tile to dispatch to at all; the original simply reads whatever the light node last
	// wrote, which is the same tile it publishes to DAT_005d70f0.
	if (!SpotlightTarget.HasTile())
	{
		LastDispatchStatus = TEXT("Spotlight has no ground target.");
		UE_LOG(LogSimCopterHelicopterPawn, Display, TEXT("Dispatch: %s"), *LastDispatchStatus);
		return;
	}
	const FIntPoint TargetTile = SpotlightTarget.Tile;

	// Chase-dispatched police follow this tile from here on.
	TrafficSystem->SetSpotlightChaseTile(TargetTile);

	const TCHAR* ServiceName =
		Service == SimCopterDispatch::EService::FireTruck ? TEXT("Fire truck") :
		Service == SimCopterDispatch::EService::Police ? TEXT("Police") : TEXT("Ambulance");

	if (bClearInstead)
	{
		const bool bCleared = TrafficSystem->ClearEmergencyDispatch(Service, TargetTile);
		LastDispatchStatus = bCleared
			? FString::Printf(TEXT("%s dispatch cleared."), ServiceName)
			: FString::Printf(TEXT("No %s unit at the spotlight to release."), ServiceName);
		UE_LOG(LogSimCopterHelicopterPawn, Display, TEXT("Dispatch: %s"), *LastDispatchStatus);
		return;
	}

	switch (TrafficSystem->RequestEmergencyDispatch(Service, TargetTile, bChaseSpotlight))
	{
	case SimCopterDispatch::EDispatchResult::Dispatched:
		LastDispatchStatus = bChaseSpotlight
			? FString::Printf(TEXT("%s following your spotlight."), ServiceName)
			: FString::Printf(TEXT("%s dispatched to (%d,%d)."), ServiceName, TargetTile.X, TargetTile.Y);
		break;
	case SimCopterDispatch::EDispatchResult::NoUnitAvailable:
		LastDispatchStatus = FString::Printf(TEXT("No %s unit available."), ServiceName);
		break;
	case SimCopterDispatch::EDispatchResult::CannotReach:
		LastDispatchStatus = FString::Printf(TEXT("%s cannot reach that location."), ServiceName);
		break;
	default:
		LastDispatchStatus = TEXT("Dispatch target is off the map.");
		break;
	}

	UE_LOG(LogSimCopterHelicopterPawn, Display, TEXT("Dispatch: %s"), *LastDispatchStatus);
}

void ASimCopterHelicopterPawn::ClearAllDispatchVehicles()
{
	ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterTrafficSystemActor::StaticClass()));
	if (TrafficSystem == nullptr)
	{
		LastDispatchStatus = TEXT("No traffic system in this map - dispatch unavailable.");
		UE_LOG(LogSimCopterHelicopterPawn, Display, TEXT("Dispatch: %s"), *LastDispatchStatus);
		return;
	}

	const int32 Cleared = TrafficSystem->ClearAllEmergencyDispatches();
	LastDispatchStatus = Cleared > 0
		? FString::Printf(TEXT("Cleared %d dispatch vehicle%s."), Cleared, Cleared == 1 ? TEXT("") : TEXT("s"))
		: TEXT("No dispatch vehicles to clear.");
	UE_LOG(LogSimCopterHelicopterPawn, Display, TEXT("Dispatch: %s"), *LastDispatchStatus);
}

void ASimCopterHelicopterPawn::DispatchFireTruckKey()
{
	RequestDispatch(static_cast<int32>(SimCopterDispatch::EService::FireTruck), false, IsDispatchClearModifierHeld());
}

void ASimCopterHelicopterPawn::DispatchAmbulanceKey()
{
	RequestDispatch(static_cast<int32>(SimCopterDispatch::EService::Ambulance), false, IsDispatchClearModifierHeld());
}

void ASimCopterHelicopterPawn::DispatchPoliceKey()
{
	RequestDispatch(static_cast<int32>(SimCopterDispatch::EService::Police), false, IsDispatchClearModifierHeld());
}

void ASimCopterHelicopterPawn::DispatchPoliceChaseKey()
{
	// Service type 3 in the original: the same police pool, launched in state 3.
	RequestDispatch(static_cast<int32>(SimCopterDispatch::EService::Police), true, IsDispatchClearModifierHeld());
}

void ASimCopterHelicopterPawn::SimDispatch(int32 Service)
{
	RequestDispatch(Service, false, false);
}

void ASimCopterHelicopterPawn::SimDispatchChase(int32 Service)
{
	RequestDispatch(Service, true, false);
}

void ASimCopterHelicopterPawn::SimDispatchClear(int32 Service)
{
	RequestDispatch(Service, false, true);
}

void ASimCopterHelicopterPawn::SimDispatchTile(int32 Service, int32 TileX, int32 TileY)
{
	ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterTrafficSystemActor::StaticClass()));
	if (TrafficSystem == nullptr)
	{
		UE_LOG(LogSimCopterHelicopterPawn, Display, TEXT("Dispatch: no traffic system."));
		return;
	}

	const int32 ServiceCount = static_cast<int32>(SimCopterDispatch::EService::Count);
	if (Service < 0 || Service >= ServiceCount)
	{
		UE_LOG(LogSimCopterHelicopterPawn, Display, TEXT("Dispatch: service %d out of range."), Service);
		return;
	}

	const SimCopterDispatch::EDispatchResult Result = TrafficSystem->RequestEmergencyDispatch(
		static_cast<SimCopterDispatch::EService>(Service),
		FIntPoint(TileX, TileY),
		false);
	UE_LOG(
		LogSimCopterHelicopterPawn,
		Display,
		TEXT("Dispatch: service %d to (%d,%d) -> result %d"),
		Service,
		TileX,
		TileY,
		static_cast<int32>(Result));
}

void ASimCopterHelicopterPawn::SimDumpDispatchState()
{
	UE_LOG(
		LogSimCopterHelicopterPawn,
		Display,
		TEXT("Dispatch state: spotlight tile=(%d,%d) valid=%d band=%d"),
		SpotlightTarget.Tile.X,
		SpotlightTarget.Tile.Y,
		SpotlightTarget.bValid ? 1 : 0,
		SpotlightTarget.Band);

	// Burning tiles: a fire truck only acts on flames within its scan radius of where it
	// parks, so this is what to compare its position against when it looks idle.
	if (const ASimCopterMissionSystemActor* Missions = Cast<ASimCopterMissionSystemActor>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass())))
	{
		TArray<TPair<FIntPoint, int32>> FlameTiles;
		Missions->GetActiveFlameTiles(FlameTiles);
		if (FlameTiles.Num() == 0)
		{
			UE_LOG(LogSimCopterHelicopterPawn, Display, TEXT("Dispatch state: nothing burning."));
		}
		for (const TPair<FIntPoint, int32>& Entry : FlameTiles)
		{
			UE_LOG(
				LogSimCopterHelicopterPawn,
				Display,
				TEXT("Dispatch state: burning tile (%d,%d) x%d flames"),
				Entry.Key.X,
				Entry.Key.Y,
				Entry.Value);
		}
	}

	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterTrafficSystemActor::StaticClass()));
	if (TrafficSystem == nullptr)
	{
		UE_LOG(LogSimCopterHelicopterPawn, Display, TEXT("Dispatch state: no traffic system."));
		return;
	}

	for (int32 Index = 0; Index < static_cast<int32>(SimCopterDispatch::EService::Count); ++Index)
	{
		const SimCopterDispatch::EService Service = static_cast<SimCopterDispatch::EService>(Index);
		UE_LOG(
			LogSimCopterHelicopterPawn,
			Display,
			TEXT("Dispatch state: service %d - %s"),
			Index,
			*TrafficSystem->GetDispatchStatusLine(Service));
	}
}

void ASimCopterHelicopterPawn::CycleSelectedDispatchService(int32 Delta)
{
	const int32 ServiceCount = static_cast<int32>(SimCopterDispatch::EService::Count);
	SelectedDispatchService = ((SelectedDispatchService + Delta) % ServiceCount + ServiceCount) % ServiceCount;
}

FString ASimCopterHelicopterPawn::GetSelectedDispatchServiceStatus() const
{
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterTrafficSystemActor::StaticClass()));
	if (TrafficSystem == nullptr)
	{
		return TEXT("no traffic system");
	}
	return TrafficSystem->GetDispatchStatusLine(static_cast<SimCopterDispatch::EService>(SelectedDispatchService));
}

int32 ASimCopterHelicopterPawn::DebugStartMission(int32 TypeMask)
{
	ASimCopterMissionSystemActor* Missions = ResolveMissionSystem();
	if (Missions == nullptr)
	{
		LastDebugMissionStatus = TEXT("No mission system in this map.");
		return INDEX_NONE;
	}

	const TCHAR* Name = SimCopterMissions::FSimCopterMissionSystem::GetTypeDisplayName(TypeMask);
	const int32 EventId = Missions->StartMissionNow(TypeMask);
	if (EventId == INDEX_NONE)
	{
		// The placer gets five tries (ten for a fire) at a tile near the camera; nothing near
		// the helicopter passed this type's tile test.
		LastDebugMissionStatus = FString::Printf(
			TEXT("%s (0x%x): the placer found no suitable tile near the camera."), Name, TypeMask);
	}
	else
	{
		LastDebugMissionStatus = FString::Printf(TEXT("%s (0x%x) -> event %d"), Name, TypeMask, EventId);
	}
	return EventId;
}

// SCHOOK: MegaphoneBroadcast 0x0048a800
// The megaphone is spotlight-directed and range-gated: FUN_0048a800 only broadcasts while
// heli[0x150] < 3, then runs FUN_0048ae70(2, spotlightTile, body, -1, messageIndex), which
// is a five-ring square spiral rather than a radius around the helicopter.
void ASimCopterHelicopterPawn::BroadcastMegaphoneMessage()
{
	const int32 MessageIndex = static_cast<int32>(SelectedMegaphoneMessage);
	PlayMegaphoneVoice(MessageIndex);

	if (!SpotlightTarget.bValid || SpotlightTarget.Band > SimCopterSpotlight::MegaphoneMaxBand)
	{
		LastToolStatus = TEXT("Spotlight target out of range for the megaphone (band 3).");
		return;
	}

	FSimCopterInteractionEvent Event;
	Event.Mode = ESimCopterInteractionMode::Megaphone;
	Event.Source = this;
	Event.TargetTile = SpotlightTarget.Tile;
	Event.TargetWorldLocation = SpotlightTarget.WorldLocation;
	Event.MessageIndex = MessageIndex;

	const int32 Affected = BroadcastInteraction(Event, SimCopterInteraction::MegaphoneRings);

	LastToolStatus = FString::Printf(
		TEXT("'%s' to tile (%d, %d): %d reacted."),
		SimCopterHelicopterRegistry::GetMegaphoneMessageName(SelectedMegaphoneMessage),
		SpotlightTarget.Tile.X,
		SpotlightTarget.Tile.Y,
		Affected);
}

void ASimCopterHelicopterPawn::PlayMegaphoneVoice(const int32 MessageIndex)
{
	USimCopterAudioSubsystem* Audio = GetHelicopterAudio();
	const int32 MessageCount = static_cast<int32>(ESimCopterMegaphoneMessage::Count);
	if (Audio == nullptr || MessageIndex < 0 || MessageIndex >= MessageCount)
	{
		return;
	}

	if (!bMegaphoneVoicesLoaded)
	{
		bMegaphoneVoicesLoaded = true;
		MegaphoneVoiceFilesByMessage.SetNum(MessageCount);
		MegaphoneVoiceNextIndices.SetNumZeroed(MessageCount);

		const FString LanguageDir = FPaths::Combine(Audio->GetSoundRoot(), TEXT("English"));
		for (int32 Index = 0; Index < MessageCount; ++Index)
		{
			TArray<FString> Files;
			IFileManager::Get().FindFiles(
				Files,
				*FPaths::Combine(LanguageDir, FString::Printf(TEXT("MG_%02d_*.WAV"), Index)),
				true,
				false);
			Files.Sort();
			for (const FString& File : Files)
			{
				MegaphoneVoiceFilesByMessage[Index].Add(FPaths::GetBaseFilename(File));
			}
		}
	}

	TArray<FString>& Lines = MegaphoneVoiceFilesByMessage[MessageIndex];
	if (Lines.Num() == 0)
	{
		return;
	}

	// SCHOOK: FUN_00424620 owns one cursor per message list and wraps it, so each message uses
	// its own MG_XX_YY family in sequence instead of choosing a random line from all five sets.
	int32& NextIndex = MegaphoneVoiceNextIndices[MessageIndex];
	NextIndex = FMath::Clamp(NextIndex, 0, Lines.Num() - 1);
	Audio->PlayFile2D(Lines[NextIndex], SimCopterSound::ESoundDir::Language);
	NextIndex = (NextIndex + 1) % Lines.Num();
}

// SCHOOK: InteractionBroadcast 0x0048ae70
// The shared object-class router: walk the original spiral, collect the ground agents whose
// tile is inside it, and hand each one the event. Buildings/vehicles/mission effects keep
// their own owners, exactly as FUN_0049a4f0 dispatches by object class.
int32 ASimCopterHelicopterPawn::BroadcastInteraction(const FSimCopterInteractionEvent& Event, int32 Rings)
{
	if (Event.TargetTile.X < 0 || Event.TargetTile.Y < 0)
	{
		return 0;
	}

	TArray<FIntPoint> Tiles;
	SimCopterInteraction::BuildSpiralTiles(Event.TargetTile, Rings, Tiles);
	if (Tiles.Num() == 0)
	{
		return 0;
	}

	return DeliverInteractionToTiles(Event, TSet<FIntPoint>(Tiles));
}

// The gas cloud gasses whoever is standing on the one tile its puff landed on, so it needs the
// tile walk without FUN_0048ae70's spiral in front of it.
int32 ASimCopterHelicopterPawn::DeliverInteractionToTile(const FSimCopterInteractionEvent& Event)
{
	if (Event.TargetTile.X < 0 || Event.TargetTile.Y < 0)
	{
		return 0;
	}
	TSet<FIntPoint> TileSet;
	TileSet.Add(Event.TargetTile);
	return DeliverInteractionToTiles(Event, TileSet);
}

int32 ASimCopterHelicopterPawn::DeliverInteractionToTiles(
	const FSimCopterInteractionEvent& Event,
	const TSet<FIntPoint>& TileSet)
{
	UWorld* World = GetWorld();
	if (World == nullptr || TileSet.Num() == 0)
	{
		return 0;
	}

	ASimCity2000CityActor* City = ResolveCityActor();
	int32 Affected = 0;

	for (TActorIterator<ASimCopterGroundAgent> It(World); It; ++It)
	{
		ASimCopterGroundAgent* Agent = *It;
		if (Agent == nullptr || Agent == Event.Source)
		{
			continue;
		}

		FIntPoint AgentTile(INDEX_NONE, INDEX_NONE);
		if (City != nullptr)
		{
			float SurfaceZ = 0.0f;
			uint8 TerrainClass = 0xff;
			if (!City->TryGetWaterGameplaySurface(Agent->GetActorLocation(), SurfaceZ, TerrainClass, &AgentTile))
			{
				continue;
			}
		}
		if (!TileSet.Contains(AgentTile))
		{
			continue;
		}

		ASimCopterTrafficSystemActor* TrafficSystem =
			Cast<ASimCopterTrafficSystemActor>(Agent->GetOwner());
		const bool bReacted = Agent->GetAgentKind() == ESimCopterGroundAgentKind::Vehicle
			? (TrafficSystem != nullptr && TrafficSystem->ApplyVehicleInteraction(*Agent, Event))
			: Agent->ApplyInteraction(Event);
		if (bReacted)
		{
			++Affected;
		}
	}

	return Affected;
}

// Where every forward-firing tool leaves the airframe. FUN_00484d20 uses one point for all of
// them - the body node lifted 3.0 units - and the remake keeps that as the last resort, but
// prefers the CANNON barrel tip when the cannon is fitted and the fuselage nose otherwise. The
// pivot-plus-3 fallback is only right in the original's frame: here ModelPivot is the capsule
// centre with the fuselage pushed down beneath it, so it sits up by the rotor mast.
bool ASimCopterHelicopterPawn::ResolveToolMuzzle(FVector& OutWorld, FVector& OutDirection) const
{
	const bool bUseOriginalBarrel =
		bUsingOriginalCannonMesh &&
		bHasCannonBarrelTip &&
		HeliCannonMeshComponent != nullptr;

	OutDirection = bUseOriginalBarrel
		? HeliCannonMeshComponent->GetForwardVector().GetSafeNormal()
		: (ModelPivot != nullptr
			? ModelPivot->GetForwardVector().GetSafeNormal()
			: GetActorForwardVector());

	if (bUseOriginalBarrel)
	{
		OutWorld = HeliCannonMeshComponent->GetComponentTransform().TransformPosition(
			CannonBarrelTipLocalCm);
		return true;
	}
	if (bHasNoseMuzzle && ModelPivot != nullptr)
	{
		OutWorld = ModelPivot->GetComponentTransform().TransformPosition(NoseMuzzleLocalCm);
		return true;
	}

	OutWorld = (ModelPivot != nullptr ? ModelPivot->GetComponentLocation() : GetActorLocation()) +
		GetActorUpVector() * (3.0f * OriginalUnitToCm);
	return false;
}

// SCHOOK: TearGasLaunch 0x00484d20
// The consumer of heli[0x57]: the direction is the airframe's own forward axis - NOT the
// spotlight's aim, which is why the launcher is fired by flying at what you want gassed - and the
// speed is heli[0x4e] plus a fixed 50.0 units/s.
bool ASimCopterHelicopterPawn::LaunchTearGasCanister()
{
	if (TearGasPool == nullptr)
	{
		return false;
	}

	FVector Muzzle = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	ResolveToolMuzzle(Muzzle, Direction);

	// The original passes -1 as the mission event, so a canister the player throws is credited to
	// whichever mission the person it gasses already belongs to.
	return TearGasPool->Launch(Muzzle, Direction, FlightModel.ForwardSpeed, INDEX_NONE);
}

// SCHOOK: HelicopterToolDispatch 0x00485f50
// One frame of tool handling: the edge-triggered tools consume the press, the held tools
// keep acting while the button is down, and the shared missile/tear-gas cooldown
// (DAT_00504570) counts down exactly as FUN_0048ed00 does.
void ASimCopterHelicopterPawn::UpdateToolDispatch(float DeltaSeconds)
{
	if (ToolCooldownSeconds > 0.0f)
	{
		ToolCooldownSeconds = FMath::Max(0.0f, ToolCooldownSeconds - DeltaSeconds);
	}

	if (bPrimaryToolUsePressed)
	{
		bPrimaryToolUsePressed = false;
		TryBeginToolUse(GetActiveTool());
	}
}

// Returns true when the action was accepted. Refusals record the original's
// missing-equipment message so the HUD and debug panel can explain them.
bool ASimCopterHelicopterPawn::TryBeginToolUse(ESimCopterHelicopterTool Tool)
{
	// SCHOOK: ToolRefusedSound 0x00485f50
	// Every capability gate in FUN_00485f50 does the same two things: post the missing-equipment
	// string and Play3D(0x80 NOEQUIP) at the helicopter. So does the hotkey path in
	// FUN_0044ac80, which plays it 2D instead because it runs off the menu.
	USimCopterAudioSubsystem* Audio = GetHelicopterAudio();

	if (!IsToolSelectable(Tool))
	{
		LastToolStatus = FString::Printf(
			TEXT("%s is not available on the %s."),
			SimCopterHelicopterRegistry::GetToolDisplayName(Tool),
			*HelicopterTypeName);
		if (Audio != nullptr)
		{
			Audio->Play3D(SimCopterSound::SND_NOEQUIP, GetActorLocation());
		}
		return false;
	}

	if (!IsToolAvailable(Tool))
	{
		const FSimCopterEquipmentDefinition* Equipment = SimCopterHelicopterRegistry::FindEquipment(Tool);
		LastToolStatus = FString::Printf(
			TEXT("%s not installed (message 0x%03x)."),
			SimCopterHelicopterRegistry::GetToolDisplayName(Tool),
			Equipment != nullptr ? Equipment->MissingMessageId : 0);
		if (Audio != nullptr)
		{
			Audio->Play3D(SimCopterSound::SND_NOEQUIP, GetActorLocation());
		}
		return false;
	}

	switch (Tool)
	{
	case ESimCopterHelicopterTool::WaterBucket:
		// Held: UpdateRopeAndBucket dumps a frame of water for as long as the button is down.
		if (BucketWaterPounds <= 0)
		{
			LastToolStatus = TEXT("Bucket empty (message 0x2a7).");
			return false;
		}
		LastToolStatus.Reset();
		return true;

	case ESimCopterHelicopterTool::WaterCannon:
		// Held: EmitWaterCannonFrame streams while the button is down.
		if (BucketWaterPounds <= 0)
		{
			LastToolStatus = TEXT("Out of water (message 0x2a7).");
			return false;
		}
		LastToolStatus.Reset();
		return true;

	case ESimCopterHelicopterTool::Megaphone:
		// Edge: one broadcast per press (FUN_0044ac80 is a discrete command id).
		BroadcastMegaphoneMessage();
		LastToolStatus = FString::Printf(
			TEXT("Broadcast '%s'."),
			SimCopterHelicopterRegistry::GetMegaphoneMessageName(SelectedMegaphoneMessage));
		return true;

	case ESimCopterHelicopterTool::TearGas:
		// Edge + cooldown-gated, and the round is only consumed on a successful shot.
		if (ToolCooldownSeconds > 0.0f)
		{
			LastToolStatus = FString::Printf(TEXT("Tear gas reloading (%.1fs)."), ToolCooldownSeconds);
			return false;
		}
		if (EquipmentState.GetTearGasRounds() <= 0)
		{
			LastToolStatus = TEXT("Out of tear gas rounds (message 0x2ac).");
			return false;
		}
		// FUN_0048e0b0 arms the shared cooldown *before* it looks for a free slot, so a shot that
		// finds the pool full still costs the player the second - but it never costs a round,
		// because the ammo decrement is inside the branch that found one.
		ToolCooldownSeconds = SimCopterToolTiming::ProjectileCooldownSeconds;
		if (!LaunchTearGasCanister())
		{
			LastToolStatus = TEXT("Tear gas pool full (ten canisters already in the air).");
			return false;
		}
		EquipmentState.ConsumeTearGasRound();
		// FUN_0048e0b0's tear-gas branch: the launch whoosh, at the helicopter.
		if (Audio != nullptr)
		{
			Audio->Play3D(SimCopterSound::SND_TGSHWH, GetActorLocation());
		}
		LastToolStatus = FString::Printf(
			TEXT("Tear gas fired (%d round(s) left)."), EquipmentState.GetTearGasRounds());
		return true;

	case ESimCopterHelicopterTool::RescueHarness:
		// Context action; raise/lower stay on the explicit winch controls.
		ToggleRope();
		LastToolStatus = bRopeDeployed ? TEXT("Harness lowering.") : TEXT("Harness stowing.");
		return true;

	case ESimCopterHelicopterTool::ApacheMissile:
	{
		if (ToolCooldownSeconds > 0.0f)
		{
			LastToolStatus = FString::Printf(TEXT("Missile reloading (%.1fs)."), ToolCooldownSeconds);
			return false;
		}
		// FUN_0048e0b0 arms the shared cooldown before it looks for a slot, so a full rack still
		// costs the second.
		ToolCooldownSeconds = SimCopterToolTiming::ProjectileCooldownSeconds;
		FVector Muzzle = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
		ResolveToolMuzzle(Muzzle, Direction);
		if (ApachePool == nullptr ||
			!ApachePool->LaunchMissile(Muzzle, Direction, FlightModel.ForwardSpeed))
		{
			LastToolStatus = TEXT("Missile rack full (ten already in the air).");
			return false;
		}
		// FUN_0048e0b0 type 1: one MISSILE per launch, no already-playing guard.
		if (Audio != nullptr)
		{
			Audio->Play3D(SimCopterSound::SND_MISSILE, GetActorLocation());
		}
		LastToolStatus = TEXT("Missile away.");
		return true;
	}

	case ESimCopterHelicopterTool::ApacheMachineGun:
	{
		// Held, no cooldown (FUN_0048e0b0 type 2 has no DAT_00504570 gate). MACHGUN1 is a LOOP
		// there, started once and left running until the emitter stops - which is why the
		// original guards it on IsPlaying and why StopHeldToolAudio has to stop it.
		FVector Muzzle = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
		ResolveToolMuzzle(Muzzle, Direction);
		if (ApachePool != nullptr)
		{
			ApachePool->LaunchBullet(Muzzle, Direction, FlightModel.ForwardSpeed);
		}
		if (Audio != nullptr && !Audio->IsPlaying(SimCopterSound::SND_MACHGUN1))
		{
			Audio->Play3D(SimCopterSound::SND_MACHGUN1, GetActorLocation(), SimCopterSoundFlags::Loop);
		}
		LastToolStatus = TEXT("Machine gun firing.");
		return true;
	}

	default:
		return false;
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
	case ESimCopterCameraMode::Rescue:
		CameraMode = ESimCopterCameraMode::Cockpit;
		break;
	default:
		CameraMode = ESimCopterCameraMode::Chase;
		CameraYawOffsetDeg = 0.0f;
		CameraPitchOffsetDeg = 0.0f;
		break;
	}
	UpdateCrosshairVisibility();
}

FSimCopterCameraViewDebugOffset ASimCopterHelicopterPawn::GetCameraViewDebugOffset(
	ESimCopterCameraMode Mode) const
{
	return CameraViewDebugOffsets[GetCameraModeIndex(Mode)];
}

void ASimCopterHelicopterPawn::SetCameraViewDebugTranslation(
	ESimCopterCameraMode Mode,
	const FVector& TranslationCm)
{
	CameraViewDebugOffsets[GetCameraModeIndex(Mode)].TranslationCm =
		SanitizeCameraDebugTranslation(TranslationCm);
	SaveCameraViewDebugOffset(Mode);
	if (Mode == CameraMode)
	{
		UpdateCamera(0.0f);
	}
}

void ASimCopterHelicopterPawn::SetCameraViewDebugRotation(
	ESimCopterCameraMode Mode,
	const FRotator& RotationDeg)
{
	CameraViewDebugOffsets[GetCameraModeIndex(Mode)].RotationDeg =
		SanitizeCameraDebugRotation(RotationDeg);
	SaveCameraViewDebugOffset(Mode);
	if (Mode == CameraMode)
	{
		UpdateCamera(0.0f);
	}
}

void ASimCopterHelicopterPawn::SetCameraViewZoomVerticalFramingStrength(
	ESimCopterCameraMode Mode,
	float Strength)
{
	CameraViewDebugOffsets[GetCameraModeIndex(Mode)].ZoomVerticalFramingStrength =
		SanitizeCameraZoomFramingStrength(Strength);
	SaveCameraViewDebugOffset(Mode);
	if (Mode == CameraMode)
	{
		UpdateCamera(0.0f);
	}
}

float ASimCopterHelicopterPawn::GetCameraViewMinZoomDistanceCm(
	ESimCopterCameraMode Mode) const
{
	switch (Mode)
	{
	case ESimCopterCameraMode::Chase: return ChaseCameraMinDistance;
	case ESimCopterCameraMode::Orbit: return 640.0f;
	// First person has no boom to zoom along.
	case ESimCopterCameraMode::Cockpit: return 0.0f;
	default: return 860.0f;
	}
}

bool ASimCopterHelicopterPawn::ShouldUseRopeAutoZoom(
	const ESimCopterCameraMode Mode,
	const float PlayerZoomAlpha,
	const int32 FirstActiveRopeNode)
{
	return Mode == ESimCopterCameraMode::Chase &&
		PlayerZoomAlpha <= KINDA_SMALL_NUMBER &&
		FirstActiveRopeNode <= SimCopterWinch::LoweredNode;
}

float ASimCopterHelicopterPawn::GetCameraViewMaxZoomDistanceCm(
	ESimCopterCameraMode Mode) const
{
	if (CameraModeIsFirstPerson(Mode))
	{
		return 0.0f;
	}

	const float MinDistanceCm = GetCameraViewMinZoomDistanceCm(Mode);
	const float OverrideDistance =
		CameraViewDebugOffsets[GetCameraModeIndex(Mode)].MaxZoomDistanceCm;
	if (OverrideDistance > 0.0f)
	{
		return FMath::Max(MinDistanceCm + 1.0f, OverrideDistance);
	}

	float AuthoredDistance = ChaseCameraMaxDistance;
	switch (Mode)
	{
	case ESimCopterCameraMode::Orbit:
		AuthoredDistance = OrbitCameraMaxDistance;
		break;
	case ESimCopterCameraMode::Rescue:
		AuthoredDistance = RescueCameraMaxDistance;
		break;
	default:
		break;
	}
	return FMath::Max(MinDistanceCm + 1.0f, AuthoredDistance);
}

void ASimCopterHelicopterPawn::SetCameraViewMaxZoomDistanceCm(
	ESimCopterCameraMode Mode,
	float DistanceCm)
{
	const float MinDistanceCm = GetCameraViewMinZoomDistanceCm(Mode);
	CameraViewDebugOffsets[GetCameraModeIndex(Mode)].MaxZoomDistanceCm =
		FMath::Clamp(
			SanitizeCameraMaxZoomDistanceOverride(DistanceCm),
			MinDistanceCm + 1.0f,
			MaxCameraZoomDistanceCm);
	SaveCameraViewDebugOffset(Mode);
	if (Mode == CameraMode)
	{
		UpdateCamera(0.0f);
	}
}

void ASimCopterHelicopterPawn::ResetCameraViewDebugOffset(ESimCopterCameraMode Mode)
{
	CameraViewDebugOffsets[GetCameraModeIndex(Mode)] = GetDefaultCameraViewDebugOffset(Mode);
	// Drop the session's middle-drag pan too, so the reset really does put the view back on its
	// authored framing instead of leaving it offset by however far the pan was left.
	CameraViewPanOffsetsCm[GetCameraModeIndex(Mode)] = 0.0f;
	SaveCameraViewDebugOffset(Mode);
	if (Mode == CameraMode)
	{
		UpdateCamera(0.0f);
	}
}

void ASimCopterHelicopterPawn::LoadCameraViewDebugOffsets()
{
	if (GConfig == nullptr || GGameUserSettingsIni.IsEmpty())
	{
		return;
	}

	const ESimCopterCameraMode Modes[] = {
		ESimCopterCameraMode::Chase,
		ESimCopterCameraMode::Orbit,
		ESimCopterCameraMode::Rescue,
		ESimCopterCameraMode::Cockpit
	};
	for (const ESimCopterCameraMode Mode : Modes)
	{
		FSimCopterCameraViewDebugOffset& Offset =
			CameraViewDebugOffsets[GetCameraModeIndex(Mode)];

		auto LoadDouble = [Mode](const TCHAR* Suffix, double& OutValue)
		{
			double Value = 0.0;
			if (GConfig->GetDouble(
					CameraDebugConfigSection,
					*MakeCameraDebugConfigKey(Mode, Suffix),
					Value,
					GGameUserSettingsIni))
			{
				OutValue = Value;
			}
		};

		LoadDouble(TEXT("TranslationX"), Offset.TranslationCm.X);
		LoadDouble(TEXT("TranslationY"), Offset.TranslationCm.Y);
		LoadDouble(TEXT("TranslationZ"), Offset.TranslationCm.Z);
		LoadDouble(TEXT("RotationPitch"), Offset.RotationDeg.Pitch);
		LoadDouble(TEXT("RotationYaw"), Offset.RotationDeg.Yaw);
		LoadDouble(TEXT("RotationRoll"), Offset.RotationDeg.Roll);
		double ZoomVerticalFramingStrength = Offset.ZoomVerticalFramingStrength;
		LoadDouble(TEXT("ZoomVerticalFramingStrength"), ZoomVerticalFramingStrength);
		double MaxZoomDistance = Offset.MaxZoomDistanceCm;
		LoadDouble(TEXT("MaxZoomDistanceCm"), MaxZoomDistance);
		Offset.TranslationCm = SanitizeCameraDebugTranslation(Offset.TranslationCm);
		Offset.RotationDeg = SanitizeCameraDebugRotation(Offset.RotationDeg);
		Offset.ZoomVerticalFramingStrength =
			SanitizeCameraZoomFramingStrength(static_cast<float>(ZoomVerticalFramingStrength));
		Offset.MaxZoomDistanceCm =
			SanitizeCameraMaxZoomDistanceOverride(static_cast<float>(MaxZoomDistance));
	}
}

void ASimCopterHelicopterPawn::SaveCameraViewDebugOffset(ESimCopterCameraMode Mode) const
{
	if (GConfig == nullptr || GGameUserSettingsIni.IsEmpty())
	{
		return;
	}

	const FSimCopterCameraViewDebugOffset& Offset =
		CameraViewDebugOffsets[GetCameraModeIndex(Mode)];
	auto SaveDouble = [Mode](const TCHAR* Suffix, double Value)
	{
		GConfig->SetDouble(
			CameraDebugConfigSection,
			*MakeCameraDebugConfigKey(Mode, Suffix),
			Value,
			GGameUserSettingsIni);
	};

	SaveDouble(TEXT("TranslationX"), Offset.TranslationCm.X);
	SaveDouble(TEXT("TranslationY"), Offset.TranslationCm.Y);
	SaveDouble(TEXT("TranslationZ"), Offset.TranslationCm.Z);
	SaveDouble(TEXT("RotationPitch"), Offset.RotationDeg.Pitch);
	SaveDouble(TEXT("RotationYaw"), Offset.RotationDeg.Yaw);
	SaveDouble(TEXT("RotationRoll"), Offset.RotationDeg.Roll);
	SaveDouble(TEXT("ZoomVerticalFramingStrength"), Offset.ZoomVerticalFramingStrength);
	SaveDouble(TEXT("MaxZoomDistanceCm"), Offset.MaxZoomDistanceCm);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void ASimCopterHelicopterPawn::SetCockpitAttitudeFollowStrength(float Strength)
{
	CockpitAttitudeFollowStrength = FMath::Clamp(Strength, 0.0f, 1.0f);
	SaveCockpitStabilization();
}

void ASimCopterHelicopterPawn::SetCockpitAttitudeLerpSpeed(float Speed)
{
	CockpitAttitudeLerpSpeed = FMath::Clamp(Speed, 0.1f, 30.0f);
	SaveCockpitStabilization();
}

void ASimCopterHelicopterPawn::SetCockpitCannonViewModelOffsetCm(const FVector& OffsetCm)
{
	CockpitCannonViewModelOffsetCm = SanitizeCameraDebugTranslation(OffsetCm);
	SaveCockpitStabilization();
}

void ASimCopterHelicopterPawn::LoadCockpitStabilization()
{
	if (GConfig == nullptr || GGameUserSettingsIni.IsEmpty())
	{
		return;
	}

	auto LoadOffsetAxis = [](const TCHAR* Key, double& OutValue)
	{
		double Value = OutValue;
		if (GConfig->GetDouble(CockpitViewConfigSection, Key, Value, GGameUserSettingsIni))
		{
			OutValue = SanitizeCameraDebugTranslation(Value);
		}
	};
	LoadOffsetAxis(TEXT("CannonViewModelX"), CockpitCannonViewModelOffsetCm.X);
	LoadOffsetAxis(TEXT("CannonViewModelY"), CockpitCannonViewModelOffsetCm.Y);
	LoadOffsetAxis(TEXT("CannonViewModelZ"), CockpitCannonViewModelOffsetCm.Z);

	double Strength = CockpitAttitudeFollowStrength;
	if (GConfig->GetDouble(CockpitViewConfigSection, TEXT("AttitudeFollowStrength"), Strength, GGameUserSettingsIni))
	{
		CockpitAttitudeFollowStrength = FMath::Clamp(static_cast<float>(Strength), 0.0f, 1.0f);
	}
	double Speed = CockpitAttitudeLerpSpeed;
	if (GConfig->GetDouble(CockpitViewConfigSection, TEXT("AttitudeLerpSpeed"), Speed, GGameUserSettingsIni))
	{
		CockpitAttitudeLerpSpeed = FMath::Clamp(static_cast<float>(Speed), 0.1f, 30.0f);
	}
}

void ASimCopterHelicopterPawn::SaveCockpitStabilization() const
{
	if (GConfig == nullptr || GGameUserSettingsIni.IsEmpty())
	{
		return;
	}

	GConfig->SetDouble(
		CockpitViewConfigSection, TEXT("AttitudeFollowStrength"), CockpitAttitudeFollowStrength, GGameUserSettingsIni);
	GConfig->SetDouble(
		CockpitViewConfigSection, TEXT("AttitudeLerpSpeed"), CockpitAttitudeLerpSpeed, GGameUserSettingsIni);
	GConfig->SetDouble(
		CockpitViewConfigSection, TEXT("CannonViewModelX"), CockpitCannonViewModelOffsetCm.X, GGameUserSettingsIni);
	GConfig->SetDouble(
		CockpitViewConfigSection, TEXT("CannonViewModelY"), CockpitCannonViewModelOffsetCm.Y, GGameUserSettingsIni);
	GConfig->SetDouble(
		CockpitViewConfigSection, TEXT("CannonViewModelZ"), CockpitCannonViewModelOffsetCm.Z, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

UMaterialInstanceDynamic* ASimCopterHelicopterPawn::GetOrCreateRotorDiscMaterialInstance()
{
	if (RotorDiscMaterialInstance == nullptr && RotorDiscMaterial != nullptr)
	{
		RotorDiscMaterialInstance = UMaterialInstanceDynamic::Create(RotorDiscMaterial, this);
		ApplyRotorDiscAppearance();
	}
	// Null when no disc material is configured; the caller falls back to the vertex-colour one.
	return RotorDiscMaterialInstance;
}

void ASimCopterHelicopterPawn::ApplyRotorDiscAppearance()
{
	if (RotorDiscMaterialInstance != nullptr)
	{
		RotorDiscMaterialInstance->SetScalarParameterValue(RotorDiscOpacityParameterName, RotorDiscOpacity);
		RotorDiscMaterialInstance->SetVectorParameterValue(RotorDiscColorParameterName, RotorDiscColor);
	}
}

void ASimCopterHelicopterPawn::SetRotorDiscOpacity(float Opacity)
{
	RotorDiscOpacity = FMath::Clamp(Opacity, 0.0f, 1.0f);
	ApplyRotorDiscAppearance();
	SaveRotorDiscAppearance();
}

void ASimCopterHelicopterPawn::SetRotorDiscColor(const FLinearColor& Color)
{
	RotorDiscColor = FLinearColor(
		FMath::Clamp(Color.R, 0.0f, 1.0f),
		FMath::Clamp(Color.G, 0.0f, 1.0f),
		FMath::Clamp(Color.B, 0.0f, 1.0f),
		1.0f);
	ApplyRotorDiscAppearance();
	SaveRotorDiscAppearance();
}

void ASimCopterHelicopterPawn::LoadRotorDiscAppearance()
{
	if (GConfig == nullptr || GGameUserSettingsIni.IsEmpty())
	{
		return;
	}

	double Opacity = RotorDiscOpacity;
	if (GConfig->GetDouble(RotorDiscConfigSection, TEXT("Opacity"), Opacity, GGameUserSettingsIni))
	{
		RotorDiscOpacity = FMath::Clamp(static_cast<float>(Opacity), 0.0f, 1.0f);
	}

	auto LoadChannel = [](const TCHAR* Key, float& OutValue)
	{
		double Value = OutValue;
		if (GConfig->GetDouble(RotorDiscConfigSection, Key, Value, GGameUserSettingsIni))
		{
			OutValue = FMath::Clamp(static_cast<float>(Value), 0.0f, 1.0f);
		}
	};
	LoadChannel(TEXT("ColorR"), RotorDiscColor.R);
	LoadChannel(TEXT("ColorG"), RotorDiscColor.G);
	LoadChannel(TEXT("ColorB"), RotorDiscColor.B);
	RotorDiscColor.A = 1.0f;

	ApplyRotorDiscAppearance();
}

void ASimCopterHelicopterPawn::SaveRotorDiscAppearance() const
{
	if (GConfig == nullptr || GGameUserSettingsIni.IsEmpty())
	{
		return;
	}

	GConfig->SetDouble(RotorDiscConfigSection, TEXT("Opacity"), RotorDiscOpacity, GGameUserSettingsIni);
	GConfig->SetDouble(RotorDiscConfigSection, TEXT("ColorR"), RotorDiscColor.R, GGameUserSettingsIni);
	GConfig->SetDouble(RotorDiscConfigSection, TEXT("ColorG"), RotorDiscColor.G, GGameUserSettingsIni);
	GConfig->SetDouble(RotorDiscConfigSection, TEXT("ColorB"), RotorDiscColor.B, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void ASimCopterHelicopterPawn::SetCameraGroundLiftHeightCm(const float ValueCm)
{
	CameraGroundLiftHeightCm = FMath::Clamp(ValueCm, 0.0f, 4000.0f);
	SaveCameraGroundLift();
}

void ASimCopterHelicopterPawn::SetCameraGroundLiftProbeRangeCm(const float ValueCm)
{
	CameraGroundLiftProbeRangeCm = FMath::Clamp(ValueCm, 1.0f, 20000.0f);
	SaveCameraGroundLift();
}

void ASimCopterHelicopterPawn::SetCameraGroundLiftFullDistanceCm(const float ValueCm)
{
	CameraGroundLiftFullDistanceCm = FMath::Clamp(ValueCm, 0.0f, 20000.0f);
	SaveCameraGroundLift();
}

void ASimCopterHelicopterPawn::LoadCameraGroundLift()
{
	if (GConfig == nullptr || GGameUserSettingsIni.IsEmpty())
	{
		return;
	}

	double Value = 0.0;
	if (GConfig->GetDouble(CameraGroundLiftConfigSection, TEXT("HeightCm"), Value, GGameUserSettingsIni))
	{
		CameraGroundLiftHeightCm = FMath::Clamp(static_cast<float>(Value), 0.0f, 4000.0f);
	}
	if (GConfig->GetDouble(CameraGroundLiftConfigSection, TEXT("ProbeRangeCm"), Value, GGameUserSettingsIni))
	{
		CameraGroundLiftProbeRangeCm = FMath::Clamp(static_cast<float>(Value), 1.0f, 20000.0f);
	}
	if (GConfig->GetDouble(CameraGroundLiftConfigSection, TEXT("FullDistanceCm"), Value, GGameUserSettingsIni))
	{
		CameraGroundLiftFullDistanceCm = FMath::Clamp(static_cast<float>(Value), 0.0f, 20000.0f);
	}
}

void ASimCopterHelicopterPawn::SaveCameraGroundLift() const
{
	if (GConfig == nullptr || GGameUserSettingsIni.IsEmpty())
	{
		return;
	}

	GConfig->SetDouble(
		CameraGroundLiftConfigSection, TEXT("HeightCm"), CameraGroundLiftHeightCm, GGameUserSettingsIni);
	GConfig->SetDouble(
		CameraGroundLiftConfigSection, TEXT("ProbeRangeCm"), CameraGroundLiftProbeRangeCm, GGameUserSettingsIni);
	GConfig->SetDouble(
		CameraGroundLiftConfigSection, TEXT("FullDistanceCm"), CameraGroundLiftFullDistanceCm, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void ASimCopterHelicopterPawn::SetEasyFlightModelEnabled(bool bEnabled)
{
	if (FlightModel.bEasyFlightModel == bEnabled)
	{
		return;
	}
	// Nothing to reconcile on the way across: both models read and write the same attitude
	// and speed state, they only weigh it differently, so the switch takes effect from the
	// next Step() with the helicopter left exactly as it was flying.
	FlightModel.bEasyFlightModel = bEnabled;
	SaveEasyFlightModel();
}

void ASimCopterHelicopterPawn::LoadEasyFlightModel()
{
	if (GConfig == nullptr || GGameUserSettingsIni.IsEmpty())
	{
		return;
	}

	bool bEnabled = FlightModel.bEasyFlightModel;
	if (GConfig->GetBool(FlightModelConfigSection, TEXT("EasyModel"), bEnabled, GGameUserSettingsIni))
	{
		FlightModel.bEasyFlightModel = bEnabled;
	}
}

void ASimCopterHelicopterPawn::SaveEasyFlightModel() const
{
	if (GConfig == nullptr || GGameUserSettingsIni.IsEmpty())
	{
		return;
	}

	GConfig->SetBool(
		FlightModelConfigSection, TEXT("EasyModel"), FlightModel.bEasyFlightModel, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

namespace
{
// The knobs are stored as frame periods in 16.16 seconds but read as frame rates,
// which is the way round that means something to whoever is flying.
float FramePeriodToFps(int32 PeriodSeconds)
{
	return PeriodSeconds > 0 ? 1.0f / SimCopterFixed::ToFloat(PeriodSeconds) : 0.0f;
}

int32 FpsToFramePeriod(float Fps)
{
	return SimCopterFixed::FromFloat(1.0f / FMath::Clamp(Fps, 1.0f, 480.0f));
}
}

float ASimCopterHelicopterPawn::GetTurbulenceReferenceFps() const
{
	return FramePeriodToFps(FlightModel.TurbulenceFrameSeconds);
}

void ASimCopterHelicopterPawn::SetTurbulenceReferenceFps(float Fps)
{
	FlightModel.TurbulenceFrameSeconds = FpsToFramePeriod(Fps);
	// The ring is part-way through an interval measured against the old period; clear
	// the clock so the next tick starts a clean one rather than a partial one of the
	// wrong length.
	FlightModel.TurbulenceClock = 0;
	SaveFlightRateTuning();
}

float ASimCopterHelicopterPawn::GetFlightReferenceFps() const
{
	return FramePeriodToFps(FlightModel.ReferenceFrameSeconds);
}

void ASimCopterHelicopterPawn::SetFlightReferenceFps(float Fps)
{
	FlightModel.ReferenceFrameSeconds = FpsToFramePeriod(Fps);
	SaveFlightRateTuning();
}

float ASimCopterHelicopterPawn::GetSpeedChaseReferenceFps() const
{
	return FramePeriodToFps(FlightModel.SpeedChaseFrameSeconds);
}

void ASimCopterHelicopterPawn::SetSpeedChaseReferenceFps(float Fps)
{
	FlightModel.SpeedChaseFrameSeconds = FpsToFramePeriod(Fps);
	SaveFlightRateTuning();
}

float ASimCopterHelicopterPawn::GetRotorVisualMultiplier() const
{
	return SimCopterFixed::ToFloat(FlightModel.RotorVisualMultiplier);
}

void ASimCopterHelicopterPawn::SetRotorVisualMultiplier(float Multiplier)
{
	FlightModel.RotorVisualMultiplier = SimCopterFixed::FromFloat(FMath::Clamp(Multiplier, 0.1f, 40.0f));
	SaveFlightRateTuning();
}

void ASimCopterHelicopterPawn::LoadFlightRateTuning()
{
	if (GConfig == nullptr || GGameUserSettingsIni.IsEmpty())
	{
		return;
	}

	double Value = 0.0;
	if (GConfig->GetDouble(FlightModelConfigSection, TEXT("TurbulenceFps"), Value, GGameUserSettingsIni))
	{
		FlightModel.TurbulenceFrameSeconds = FpsToFramePeriod(static_cast<float>(Value));
	}
	if (GConfig->GetDouble(FlightModelConfigSection, TEXT("ReferenceFps"), Value, GGameUserSettingsIni))
	{
		FlightModel.ReferenceFrameSeconds = FpsToFramePeriod(static_cast<float>(Value));
	}
	if (GConfig->GetDouble(FlightModelConfigSection, TEXT("SpeedChaseFps"), Value, GGameUserSettingsIni))
	{
		FlightModel.SpeedChaseFrameSeconds = FpsToFramePeriod(static_cast<float>(Value));
	}
	if (GConfig->GetDouble(FlightModelConfigSection, TEXT("RotorVisualMultiplier"), Value, GGameUserSettingsIni))
	{
		FlightModel.RotorVisualMultiplier =
			SimCopterFixed::FromFloat(FMath::Clamp(static_cast<float>(Value), 0.1f, 40.0f));
	}
}

void ASimCopterHelicopterPawn::SaveFlightRateTuning() const
{
	if (GConfig == nullptr || GGameUserSettingsIni.IsEmpty())
	{
		return;
	}

	GConfig->SetDouble(
		FlightModelConfigSection, TEXT("TurbulenceFps"), GetTurbulenceReferenceFps(), GGameUserSettingsIni);
	GConfig->SetDouble(
		FlightModelConfigSection, TEXT("ReferenceFps"), GetFlightReferenceFps(), GGameUserSettingsIni);
	GConfig->SetDouble(
		FlightModelConfigSection, TEXT("SpeedChaseFps"), GetSpeedChaseReferenceFps(), GGameUserSettingsIni);
	GConfig->SetDouble(
		FlightModelConfigSection, TEXT("RotorVisualMultiplier"), GetRotorVisualMultiplier(), GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void ASimCopterHelicopterPawn::ToggleSearchLight()
{
	if (SearchLightComponent != nullptr)
	{
		SearchLightComponent->ToggleVisibility();
	}
	UpdateSearchLightEffect();
}

// SCHOOK: SpotlightAimInput 0x00479060
// Input actions 0x2e..0x31 step the aim by 40.0 tenth-degrees per frame in each axis.
void ASimCopterHelicopterPawn::AimSpotlightPitch(float Value)
{
	SpotlightAimPitchInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void ASimCopterHelicopterPawn::AimSpotlightYaw(float Value)
{
	SpotlightAimYawInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

// SCHOOK: SpotlightAimAccumulate 0x00489730
void ASimCopterHelicopterPawn::AddSpotlightAim(int32 PitchDelta1616, int32 YawDelta1616)
{
	SpotlightAimPitch1616 = SimCopterSpotlight::ClampAim(SpotlightAimPitch1616 + PitchDelta1616);
	SpotlightAimYaw1616 = SimCopterSpotlight::ClampAim(SpotlightAimYaw1616 + YawDelta1616);
}

void ASimCopterHelicopterPawn::ResetSpotlightAim()
{
	SpotlightAimPitch1616 = 0;
	SpotlightAimYaw1616 = 0;
}

// The original builds identity -> rotateX(aimPitch - 360.0 tenth-deg) -> rotateY(aimYaw) and
// transforms a fixed base vector. In Unreal terms that is the helicopter's forward axis
// pitched 36 degrees down plus the accumulated aim, expressed in the banking model frame.
FVector ASimCopterHelicopterPawn::GetSpotlightAimDirection() const
{
	const float PitchDeg =
		SimCopterFixed::ToFloat(SpotlightAimPitch1616 - SimCopterSpotlight::BasePitch1616) / 10.0f;
	const float YawDeg = SimCopterFixed::ToFloat(SpotlightAimYaw1616) / 10.0f;

	const FTransform Frame = ModelPivot != nullptr
		? ModelPivot->GetComponentTransform()
		: GetActorTransform();
	const FRotator LocalAim(PitchDeg, YawDeg, 0.0f);
	return Frame.TransformVectorNoScale(LocalAim.Vector()).GetSafeNormal(SMALL_NUMBER, -FVector::UpVector);
}

// SCHOOK: SpotlightTargetService 0x00489250
// Semantic targeting runs every frame even when the cone is hidden (the original hides the
// node in cockpit view but still computes the target and broadcasts the interaction scan).
void ASimCopterHelicopterPawn::UpdateSpotlightTarget(float DeltaSeconds)
{
	// Aim accumulation first, so the march uses this frame's direction.
	const float CombinedPitchInput = FMath::Clamp(
		SpotlightAimPitchInput + ControllerSpotlightAimPitchInput,
		-1.0f,
		1.0f);
	const float CombinedYawInput = FMath::Clamp(
		SpotlightAimYawInput + ControllerSpotlightAimYawInput,
		-1.0f,
		1.0f);
	if (!FMath::IsNearlyZero(CombinedPitchInput) || !FMath::IsNearlyZero(CombinedYawInput))
	{
		AddSpotlightAim(
			FMath::RoundToInt(CombinedPitchInput * SimCopterSpotlight::AimStep1616),
			FMath::RoundToInt(CombinedYawInput * SimCopterSpotlight::AimStep1616));
	}

	if (bSpotlightTargetFrozen)
	{
		return;
	}

	const float Unit = FMath::Max(OriginalUnitToCm, 0.01f);
	const FVector Origin = ModelPivot != nullptr
		? ModelPivot->GetComponentLocation()
		: GetActorLocation();
	const FVector Direction = GetSpotlightAimDirection();

	// The original marches 16 fixed steps through the tile-object lists. The remake traces the
	// same total reach in one sweep against the built city collision, then quantises the hit
	// distance onto the same 32-unit step grid so the smoothing/banding math is unchanged.
	const float StepCm = SimCopterFixed::ToFloat(SimCopterSpotlight::MarchStep1616) * Unit;
	const float MaxReachCm = StepCm * SimCopterSpotlight::MaxMarchSteps;

	FSimCopterToolTarget NewTarget;
	NewTarget.InteractionRings = SimCopterInteraction::SpotlightRings;

	int32 RawDistance1616 = SimCopterSpotlight::MaxMarchSteps * SimCopterSpotlight::MarchStep1616;
	FVector HitWorld = Origin + Direction * MaxReachCm;
	FVector HitNormal = FVector::UpVector;

	if (UWorld* World = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterSpotlightMarch), false, this);
		if (World->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * MaxReachCm, ECC_Visibility, QueryParams) &&
			Hit.bBlockingHit)
		{
			// Round up to the step the original would have stopped on.
			const int32 Steps = FMath::Clamp(
				FMath::CeilToInt(Hit.Distance / FMath::Max(StepCm, KINDA_SMALL_NUMBER)),
				1,
				SimCopterSpotlight::MaxMarchSteps);
			RawDistance1616 = Steps * SimCopterSpotlight::MarchStep1616;
			HitWorld = Hit.ImpactPoint;
			HitNormal = Hit.ImpactNormal;
			NewTarget.HitActor = Hit.GetActor();
			NewTarget.bValid = true;
		}
	}

	RawDistance1616 = SimCopterSpotlight::ClampMarchDistance(RawDistance1616);
	const bool bFlying = FlightModel.State != ESimCopterFlightState::Parked;
	SpotlightDistance1616 = SimCopterSpotlight::SmoothDistance(SpotlightDistance1616, RawDistance1616, bFlying);

	NewTarget.DistanceUnits = SimCopterFixed::ToFloat(SpotlightDistance1616);
	NewTarget.Band = SimCopterSpotlight::SelectBand(SpotlightDistance1616);

	// The original derives the light node position from the *smoothed* distance rather than
	// the raw hit, which is why the cone lags slightly behind fast aim changes.
	const FVector SmoothedPoint = Origin + Direction * (NewTarget.DistanceUnits * Unit);
	NewTarget.WorldLocation = NewTarget.bValid ? HitWorld : SmoothedPoint;
	NewTarget.WorldNormal = HitNormal;

	if (ASimCity2000CityActor* City = ResolveCityActor())
	{
		float SurfaceZ = 0.0f;
		uint8 TerrainClass = 0xff;
		FIntPoint Tile = FIntPoint::ZeroValue;
		if (City->TryGetWaterGameplaySurface(NewTarget.WorldLocation, SurfaceZ, TerrainClass, &Tile))
		{
			NewTarget.Tile = Tile;
			if (!NewTarget.bValid)
			{
				// No collision hit but the beam is over the map: treat the terrain surface as
				// the target so the megaphone still has a tile.
				NewTarget.WorldLocation.Z = SurfaceZ;
				NewTarget.bValid = true;
			}
		}
	}

	SpotlightTarget = NewTarget;

	// Chase-dispatched police re-read the spotlight tile every frame (FUN_004b9e40 case 2
	// reads DAT_005040d0 + 0xc0 directly); the pawn publishes it instead. The same node is what
	// FUN_004a01f0 measures speeder cars against, so publish the ground point and band with it.
	if (ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterTrafficSystemActor::StaticClass())))
	{
		if (SpotlightTarget.HasTile())
		{
			TrafficSystem->SetSpotlightChaseTile(SpotlightTarget.Tile);
		}

		// The original zeroes every mark when the light is off (DAT_00503aa0 == 3); passing
		// bActive = false does the same here.
		const bool bMarkActive = SpotlightTarget.bValid &&
			SearchLightComponent != nullptr && SearchLightComponent->IsVisible();
		TrafficSystem->SetSpotlightMarkSource(
			SpotlightTarget.WorldLocation,
			SpotlightTarget.Band,
			bMarkActive);
	}

	// Point the Unreal light at the resolved target. Visual only - the target above is what
	// gameplay reads.
	if (SearchLightComponent != nullptr)
	{
		const FVector LightOrigin = SearchLightComponent->GetComponentLocation();
		SearchLightComponent->SetWorldRotation((SpotlightTarget.WorldLocation - LightOrigin).Rotation());
	}
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
	SearchLightComponent->SetInverseExposureBlend(SearchLightExposureCompensation);
	SearchLightComponent->AttenuationRadius = FMath::Max(SearchLightRangeCm, BeamLength);
	SearchLightComponent->OuterConeAngle = OuterConeAngleDeg;
	SearchLightComponent->InnerConeAngle = FMath::Clamp(OuterConeAngleDeg * 0.45f, 1.0f, OuterConeAngleDeg);
	SearchLightComponent->SetLightColor(SearchLightBeamColor.ToFColor(true));
}

ASimCopterHelicopterPawn::EEngineHoldAction ASimCopterHelicopterPawn::ResolveEngineHoldAction(
	const bool bStartInput,
	const bool bShutdownInput)
{
	if (bStartInput == bShutdownInput)
	{
		return EEngineHoldAction::None; // neither, or both fighting each other
	}
	return bStartInput ? EEngineHoldAction::Start : EEngineHoldAction::Shutdown;
}

void ASimCopterHelicopterPawn::UpdateEngineState(float DeltaSeconds)
{
	const bool bStartInput = bEngineStartHeld || bControllerEngineStartHeld;
	const bool bShutdownInput = bEngineShutdownHeld || bControllerEngineShutdownHeld;

	// Start and shutdown must never be live at the same time. With both held the two blocks below
	// take turns - start runs its timer out and sets the engine running, shutdown immediately runs
	// its own and clears it, forever - and the engine flaps on and off about once a second. That
	// is not a theoretical state: the collective keys drive both (`bControllerEngineStartHeld =
	// CollectiveCommand > 0`, shutdown `< 0`), and a shutdown key whose release went missing
	// across a possession change leaves it permanently asserted. While the engine is off
	// BuildFlightInputs returns dead controls, so the rotor sawtooths up and down instead of
	// spooling and the collective appears to do nothing.
	//
	// Conflicting input now does nothing at all, which is both unambiguous and impossible to
	// oscillate. The stuck bool itself is cleared by ResetTransientInputState on every possession
	// change; this is the guard that makes the state machine safe however it got there.
	const EEngineHoldAction HoldAction = ResolveEngineHoldAction(bStartInput, bShutdownInput);
	const bool bAnyEngineStartHeld = HoldAction == EEngineHoldAction::Start;
	const bool bAnyEngineShutdownHeld = HoldAction == EEngineHoldAction::Shutdown;

	if (bAnyEngineStartHeld && !bEngineRunning && CurrentFuelGallons > 0.01f && CurrentDamage < static_cast<float>(HelicopterTuning.MaxDamage))
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
	else if (!bAnyEngineStartHeld)
	{
		EngineStartHoldElapsed = 0.0f;
		EngineStartHoldAlpha = 0.0f;
	}

	if (bIsLanded && !bAnyEngineStartHeld)
	{
		bEngineRunning = false;
		EngineShutdownHoldElapsed = 0.0f;
		EngineShutdownHoldAlpha = 0.0f;
	}
	else if (bAnyEngineShutdownHeld && bEngineRunning && bIsLanded)
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
	else if (!bAnyEngineShutdownHeld || !bIsLanded)
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
	// FUN_00485f50 action 0x10: firing the cannon kicks the pitch target by one frame of
	// SlideRate * load, i.e. the recoil pushes the nose up.
	if ((bWaterCannonHeld ||
			(bPrimaryToolUseHeld && GetActiveTool() == ESimCopterHelicopterTool::WaterCannon)) &&
		IsToolAvailable(ESimCopterHelicopterTool::WaterCannon) &&
		BucketWaterPounds > 0)
	{
		int32 CannonKick = SimCopterWaterGameplay::FixedMul(
			SimCopterFixed::FromFloat(DeltaSeconds),
			SimCopterFixed::FromFloat(RopeTuning.CannonForce));
		if (FlightModel.bEasyFlightModel)
		{
			// The recoil reads the same `iVar2` the easy model already halved for the
			// pitch keys, so it is gentler under that model too.
			CannonKick >>= 1;
		}
		FlightModel.PitchTarget -= CannonKick;
	}

	const FSimCopterFlightInputs Inputs = BuildFlightInputs();
	const FSimCopterFlightEnvironment Environment = BuildFlightEnvironment();
	LastClimbCommand = Inputs.ClimbCommand;
	LastFlightEnvironmentFireDelta = Environment.FireHeightDelta;
	FlightModel.Step(DeltaSeconds, Inputs, Environment, LastFlightEvents);

	// ApplyFlightModelToActor is where the swept collider raises an object impact, so the audio
	// pass has to run after it, not before: Step() clears the event block at the top of every
	// frame, so a bPadBounce raised here used to be wiped before anything ever played it. That is
	// why flying into a building was silent even once the impact itself started firing.
	ApplyFlightModelToActor(DeltaSeconds);

	// Before anything downstream consumes or clears the events.
	PlayFlightEventAudio(LastFlightEvents);

	UpdateGroundProbe();
	UpdateForwardProbe();
	UpdateRopeAndBucket(DeltaSeconds);

	// BHAV 292 only asks "may I get out yet?" about every thirteenth tick, so the shipped game has
	// always had a beat between the skids arriving and the cabin emptying. The mission-side release
	// runs every mission tick and had none; this is where it gets one.
	SecondsWithinAlightClearance = CanTransferMissionPassengers()
		? SecondsWithinAlightClearance + DeltaSeconds
		: 0.0f;

	// Mirror the simulation status onto the pawn's HUD-facing state.
	bIsLanded = FlightModel.State == ESimCopterFlightState::Parked;
	CurrentFuelGallons = SimCopterFixed::ToFloat(FlightModel.Fuel);
	CurrentDamage = FMath::Clamp(
		static_cast<float>(FlightModel.Tuning.MaxDamage - FlightModel.HitPoints),
		0.0f,
		static_cast<float>(FlightModel.Tuning.MaxDamage));

	// SCHOOK: HelicopterCrashRespawn 0x0048a8b0
	// The original respawns a destroyed helicopter on an airport pad. The remake used to repair it
	// in place where it crashed, because the pad registry did not exist yet - it does now, and it
	// is the same one city entry parks the fleet on.
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
		ReturnToAirportAfterCrash();
	}
	else if ((LastFlightEvents.bGroundBounce || LastFlightEvents.bSplashBounce) && WaterFXComponent != nullptr)
	{
		// FUN_0048a8b0 enters its phase-one effect state on the hard impact itself, not only
		// after the later destroyed-helicopter transition.
		WaterFXComponent->SpawnHardLanding(
			GetActorLocation(),
			LastFlightEvents.bSplashBounce || ProbeBucketWater(GetActorLocation()));
	}

	// After the crash arm above, so a spiral that did reach the ground this frame has already been
	// dealt with and the watchdog simply resets.
	UpdateStuckFallWatchdog(DeltaSeconds);

	// Flying into something has its own visual, and it is not the landing one. Both impact arms
	// of FUN_00484d20 - the elevated-surface branch at LAB_00485605 and the object-overlap branch
	// behind FUN_0048ad50 - throw an impact column with FUN_004af100(..., 0x80000001, -1), whose
	// `4 << (param5 & 0x1f)` makes it a scale-1 burst rather than the big scale-4 water splash a
	// ditching uses.
	if (bHasPendingImpactEffect)
	{
		bHasPendingImpactEffect = false;
		if (!LastFlightEvents.bCrashed && WaterFXComponent != nullptr)
		{
			// At the contact point, and NOT submerged: the column's 32-unit drop exists so a
			// water splash rises through the surface it came out of, and on a building face it
			// just buries the burst below the impact.
			WaterFXComponent->SpawnSplashColumn(
				LastImpactWorldLocation,
				ImpactColumnScaleExponent,
				/*PaletteIndex=*/0xFF,
				INDEX_NONE,
				INDEX_NONE,
				/*bSubmergeOrigin=*/false);
		}
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

	// Static per-type data from the executable's tuning block (DAT_005040e4 + type*0x5c):
	// passenger seats at +0x00 and the no-tail-rotor flag at +0x38. One registry, no
	// duplicated switch (plan section 3).
	if (const FSimCopterHelicopterDefinition* Definition = GetHelicopterDefinition())
	{
		Tuning.PassengerSeats = Definition->PassengerSeats;
		Tuning.bNoTailRotor = Definition->bNoTailRotor;
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
	const bool bRadialOwnsRightStick =
		ControllerMode == ESimCopterControllerMode::DispatchWheel ||
		ControllerMode == ESimCopterControllerMode::ToolWheel;
	const SimCopterControllerInput::FFlightRouting ControllerRouting =
		SimCopterControllerInput::ResolveFlightRouting(
			ControllerLeftXInput,
			ControllerLeftYInput,
			ControllerRightYInput,
			bControllerCameraAdjustHeld && !bRadialOwnsRightStick,
			bControllerRightShoulderHeld,
			ControllerRightTriggerInput);
	Inputs.PitchAxis = ControllerRouting.PitchAxisPercent;
	Inputs.TurnAxis = ControllerRouting.TurnAxisPercent;
	Inputs.SlideAxis = ControllerRouting.SlideAxisPercent;
	if (CollectiveInput > KeyThreshold)
	{
		Inputs.ClimbCommand = 1;
	}
	else if (CollectiveInput < -KeyThreshold)
	{
		Inputs.ClimbCommand = -1;
	}
	else
	{
		Inputs.ClimbCommand = ControllerRouting.CollectiveCommand;
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
	// heli[0x59]: terrain and objects only. See TraceFlightSurface - a pedestrian's head is not a
	// landing surface, and this trace starts above the world so it would find one first.
	if (TraceFlightSurface(Start, End, QueryParams, Hit) && Hit.bBlockingHit)
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
		else if (bEngineShutdownHeld || bControllerEngineShutdownHeld)
		{
			Environment.bTerrainFlat = true;
		}
	}

	// FUN_004a5c10, the last input this environment was missing. Without it FireHeightDelta was
	// always zero, so StepTurbulence's fire arm never ran: flying into a burning building did no
	// damage, the airframe never shook near a fire, and FIREDMG never played.
	if (const ASimCopterMissionSystemActor* Missions = Cast<ASimCopterMissionSystemActor>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass())))
	{
		Environment.FireHeightDelta = Missions->GetFireHeightDelta1616(Location);
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

	// SCHOOK: HelicopterObjectCollision 0x0048ad50
	// The original has exactly one collision system and it never blocks. FUN_00484d20 writes the
	// simulated position into the node unconditionally; contact is found afterwards, either by the
	// height test against heli[0x59] or by FUN_0048ad50's AABB sweep of the objects on the
	// helicopter's own tile, and the response is always to *push the airframe away* - damage, an
	// attitude kick and a bounce at four times the climb rate. That is why the original cannot
	// wedge you against anything.
	//
	// The remake substitutes a swept capsule against real geometry for the tile-object boxes,
	// which is a fair trade, but it must keep the "never block" half of the bargain: a capsule
	// that stops the aircraft while the flight model keeps steering into the obstacle pins it
	// there, and the blocked position used to be written back into the model so the simulation
	// believed it too. The sweep is now a detector only.
	FHitResult BlockingHit;
	RootComponent->MoveComponent(NewLocation - GetActorLocation(), NewRotation.Quaternion(), true, &BlockingHit);
	if (BlockingHit.IsValidBlockingHit())
	{
		// The threshold is the flight model's own landing test, not a wall test. StepGroundImpact
		// treats terrain as landable only when the normal clears LandingFlatNormalZ and bounces
		// off everything else; the swept collider has to draw the line in the same place or the
		// two disagree. It used to use 0.6, which let every roof pitch and hillside through - the
		// skids would touch a slope at speed, the height test would not fire either because the
		// point directly below the origin was still clear, and nothing happened at all. Taking the
		// absolute value made it worse: a ceiling (normal.Z about -1) read as a floor.
		//
		// No speed gate, because the original has none: any contact with a surface it cannot land
		// on is an impact. BounceTimer is the original's own rate limit (0.2 s, set by the wall
		// kick), and it is what stops a scrape stacking an impact every frame.
		const bool bWreck = FlightModel.State == ESimCopterFlightState::Dying;
		if (bWreck)
		{
			// A wreck answers with movement only. Sim X is UE Y and sim Z is UE X (see the axis
			// note above NewLocation), so the surface normal converts straight across.
			const FVector Push = BlockingHit.ImpactNormal.GetSafeNormal();
			FlightModel.NotifyWreckCollision(
				FMath::RoundToInt(Push.Y * FSimCopterFlightModel::WreckImpactPush),
				FMath::RoundToInt(Push.X * FSimCopterFlightModel::WreckImpactPush),
				LastFlightEvents);
			LastImpactWorldLocation = BlockingHit.ImpactPoint;
			bHasPendingImpactEffect = true;
		}
		else if (BlockingHit.ImpactNormal.Z < LandingFlatNormalZ && FlightModel.BounceTimer <= 0)
		{
			FlightModel.NotifyObjectCollision(LastFlightEvents);
			// FUN_00484d20 puts the impact column at the helicopter pushed five units along its
			// motion, i.e. at the contact side. The remake has the real contact point, so it uses
			// that; spawning at the actor origin put the burst at the middle of the airframe and
			// then the column's own submerge offset dropped it two metres further.
			LastImpactWorldLocation = BlockingHit.ImpactPoint;
			bHasPendingImpactEffect = true;
		}

		// Hand the position back to the flight model. The bounce above is what clears the
		// obstacle; the collider's job ends at reporting it. A wreck's shove edits the model's
		// position directly, so re-read it rather than restoring the pre-impact point - otherwise
		// the push would not land until the following frame and a wreck wedged in a corner could
		// out-run it.
		const FVector RestoreLocation = bWreck
			? FVector(
				SimCopterFixed::ToFloat(FlightModel.PosZ) * Unit,
				SimCopterFixed::ToFloat(FlightModel.PosX) * Unit,
				SimCopterFixed::ToFloat(FlightModel.Altitude) * Unit + CapsuleHalfHeight)
			: NewLocation;
		SetActorLocation(RestoreLocation, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	}

	// SCHOOK: HelicopterObjectCollision 0x0048ad50 (people arm)
	// The other half of the original's contact pass, and the reason the swept capsule above no
	// longer blocks pawns: FUN_0048ad50 walks the objects overlapping the airframe and answers them
	// with FUN_0049a4f0(0xc, ...) - a reaction on THEM - while the aircraft flies on. Narrowed here
	// to uncaught criminals; ordinary pedestrians are not touched at all. Nothing in this call can
	// move the helicopter.
	if (ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystemActor())
	{
		TrafficSystem->RunOverCriminalsUnderHelicopter(*this);
	}

	// The model owns the position, so this is now a straight read-back; it stays because
	// SetActorLocation still clamps against the world's own limits.
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

ASimCopterTrafficSystemActor* ASimCopterHelicopterPawn::ResolveTrafficSystemActor() const
{
	// Cached like the city actor: the run-over pass asks for this every frame.
	if (ASimCopterTrafficSystemActor* Cached = CachedTrafficSystem.Get())
	{
		return Cached;
	}
	ASimCopterTrafficSystemActor* Found = Cast<ASimCopterTrafficSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterTrafficSystemActor::StaticClass()));
	CachedTrafficSystem = Found;
	return Found;
}

bool ASimCopterHelicopterPawn::TraceFlightSurface(
	const FVector& Start,
	const FVector& End,
	const FCollisionQueryParams& QueryParams,
	FHitResult& OutHit) const
{
	OutHit = FHitResult();
	if (GetWorld() == nullptr)
	{
		return false;
	}

	// A person is not a surface. The original's height test reads heli[0x59], the *terrain and
	// object* top under the aircraft; people are not in that number at all - they are answered
	// separately by FUN_0048ad50, with damage and a bounce, never with support. Tracing plain
	// Visibility put a pedestrian's capsule in the role of ground: walk under a descending
	// helicopter and it touched down on their head, reported itself landed, and sat there.
	//
	// Multi-trace and take the first blocking hit that is not a pawn. Pawns are all of it - other
	// helicopters included, which is the same rule: aircraft do not stack.
	TArray<FHitResult> Hits;
	if (!GetWorld()->LineTraceMultiByChannel(Hits, Start, End, ECC_Visibility, QueryParams))
	{
		return false;
	}
	for (const FHitResult& Hit : Hits)
	{
		if (!Hit.bBlockingHit)
		{
			continue;
		}
		const UPrimitiveComponent* HitComponent = Hit.GetComponent();
		if (HitComponent != nullptr && HitComponent->GetCollisionObjectType() == ECC_Pawn)
		{
			continue;
		}
		OutHit = Hit;
		return true;
	}
	return false;
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

	TraceFlightSurface(Start, End, QueryParams, LastGroundHit);
	if (LastGroundHit.bBlockingHit)
	{
		GroundClearanceCm = FMath::Max(0.0f, LastGroundHit.Distance - CapsuleHalfHeight);
	}

	if (bDrawDebugProbes)
	{
		DrawDebugLine(GetWorld(), Start, End, LastGroundHit.bBlockingHit ? FColor::Green : FColor::Red, false, 0.0f, 0, 2.0f);
	}
}

float ASimCopterHelicopterPawn::GetVehicleMetallic() const
{
	const USimCopterVehicleMaterialSubsystem* VehicleMaterials = USimCopterVehicleMaterialSubsystem::Get(this);
	return VehicleMaterials != nullptr ? VehicleMaterials->GetMetallic() : 0.0f;
}

void ASimCopterHelicopterPawn::SetVehicleMetallic(float Metallic)
{
	if (USimCopterVehicleMaterialSubsystem* VehicleMaterials = USimCopterVehicleMaterialSubsystem::Get(this))
	{
		VehicleMaterials->SetMetallic(Metallic);
	}
}

float ASimCopterHelicopterPawn::GetFlashingLightIntensityScale() const
{
	return FlashingLightsComponent != nullptr ? FlashingLightsComponent->PointLightIntensityScale : 1.0f;
}

void ASimCopterHelicopterPawn::SetFlashingLightIntensityScale(float Scale)
{
	const float Clamped = FMath::Max(Scale, 0.0f);
	if (FlashingLightsComponent != nullptr)
	{
		FlashingLightsComponent->PointLightIntensityScale = Clamped;
	}
	// The city's beacons are the ones you are usually looking at, so the one slider moves both.
	if (ASimCity2000CityActor* City = ResolveCityActor())
	{
		City->SetFlashingLightIntensityScale(Clamped);
	}
	// Written centrally rather than per component: a city that has not spawned yet still picks
	// this up, because every component reads the same key on its own BeginPlay.
	USimCopterFlashingLightsComponent::SaveIntensityScaleToConfig(Clamped);
}

float ASimCopterHelicopterPawn::GetWaterTextureFramesPerSecond() const
{
	const ASimCity2000CityActor* City = ResolveCityActor();
	return City != nullptr
		? City->GetWaterTextureFramesPerSecond()
		: ASimCity2000CityActor::DefaultWaterTextureFramesPerSecond;
}

void ASimCopterHelicopterPawn::SetWaterTextureFramesPerSecond(float FramesPerSecond)
{
	if (ASimCity2000CityActor* City = ResolveCityActor())
	{
		City->SetWaterTextureFramesPerSecond(FramesPerSecond);
	}
}

// SCHOOK: CheckupAtAirport 0x004823a0
bool ASimCopterHelicopterPawn::IsStandingOnAirport() const
{
	ASimCity2000CityActor* City = ResolveCityActor();
	ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterTrafficSystemActor::StaticClass()));
	if (City == nullptr || TrafficSystem == nullptr)
	{
		return false;
	}

	float SurfaceZ = 0.0f;
	uint8 TerrainClass = 0xff;
	FIntPoint Tile = FIntPoint::ZeroValue;
	if (!City->TryGetWaterGameplaySurface(GetActorLocation(), SurfaceZ, TerrainClass, &Tile))
	{
		return false;
	}

	// The original asks whether terminal XBLD 0xf6 is nearby. The remake has stronger ground
	// truth: the airport builder publishes the exact 4x4 block whose twelve perimeter tiles are
	// helipads around the middle 2x2 hangar plot. Testing the stamped result makes every one of
	// those pads eligible regardless of which building cells survived into the cached city grid.
	const FIntPoint AirportOrigin = TrafficSystem->GetAirportOriginTile();
	return SimCopterAirport::GetStampedXbldId(
		AirportOrigin,
		Tile.X,
		Tile.Y) == SimCopterAirport::PadXbldId;
}

FSimCopterCheckupState ASimCopterHelicopterPawn::BuildCheckupState() const
{
	FSimCopterCheckupState State;

	// heli[0x34] and DAT_0050412c. The flight model already names these the original's way.
	State.HitPoints = FlightModel.HitPoints;
	State.MaxHitPoints = FMath::Max(FlightModel.Tuning.MaxDamage, 1);
	// heli.twk "Repair Rate" / "Fuel Cost" are the per-type dollar rates (DAT_00504130/34).
	State.DollarsPerHitPoint1616 = SimCopterFixed::FromFloat(HelicopterTuning.RepairRatePerDamage);
	State.DollarsPerGallon1616 = SimCopterFixed::FromFloat(HelicopterTuning.FuelCostPerGallon);

	// heli[0xcc] and DAT_00504120, both 16.16 gallons.
	State.Fuel1616 = FlightModel.Fuel;
	State.FuelCapacity1616 = SimCopterFixed::FromFloat(HelicopterTuning.FuelGallons);

	State.bTearGasFitted = IsToolAvailable(ESimCopterHelicopterTool::TearGas);
	State.TearGasRounds = EquipmentState.GetTearGasRounds();

	State.bAtAirport = IsStandingOnAirport();

	if (const ASimCopterMissionSystemActor* Missions = Cast<ASimCopterMissionSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass())))
	{
		State.Funds = Missions->GetSessionCash();
	}

	return State;
}

// SCHOOK: CheckupApply 0x004385c0
void ASimCopterHelicopterPawn::ApplyCheckupOrder(const FSimCopterCheckupOrder& RawOrder)
{
	const FSimCopterCheckupState State = BuildCheckupState();
	const FSimCopterCheckupOrder Order = FSimCopterCheckup::ClampOrder(State, RawOrder);

	ASimCopterMissionSystemActor* Missions = Cast<ASimCopterMissionSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass()));

	// The original charges and applies each line separately, in this order, so a player who can
	// only afford part of the order still gets the earlier lines.
	if (Order.DamageDollars > 0)
	{
		if (Missions != nullptr)
		{
			Missions->AddSessionCash(-Order.DamageDollars);
		}
		FlightModel.HitPoints = FSimCopterCheckup::ApplyDamageRepair(State, Order.DamageDollars);
		// The remake also carries a float damage counter for the HUD; keep it in step with the
		// hit points the original actually repairs.
		CurrentDamage = static_cast<float>(FMath::Max(State.MaxHitPoints - FlightModel.HitPoints, 0));
	}
	if (Order.FuelDollars > 0)
	{
		if (Missions != nullptr)
		{
			Missions->AddSessionCash(-Order.FuelDollars);
		}
		FlightModel.Fuel = FSimCopterCheckup::ApplyFuel(State, Order.FuelDollars);
		CurrentFuelGallons = SimCopterFixed::ToFloat(FlightModel.Fuel);
	}
	if (Order.TearGasRounds > 0)
	{
		const int32 Dollars = FSimCopterCheckup::GetTearGasCostDollars(Order.TearGasRounds);
		if (Missions != nullptr)
		{
			Missions->AddSessionCash(-Dollars);
		}
		EquipmentState.CareerTearGasRounds =
			FSimCopterCheckup::ApplyTearGas(Dollars, EquipmentState.CareerTearGasRounds);
	}
}

bool ASimCopterHelicopterPawn::IsCheckupMenuOpen() const
{
	return CheckupWidget.IsValid();
}

bool ASimCopterHelicopterPawn::OpenCheckupMenu()
{
	if (CheckupWidget.IsValid())
	{
		return true;
	}
	if (GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return false;
	}

	// The panel is drawn from the original's own page art. Do not construct the plain fallback
	// during startup before possession has made the cockpit create its shared loader; initialise
	// it here too, and retry on a later frame if the BMP folder is not ready yet.
	if (FlapArt == nullptr)
	{
		FlapArt = NewObject<USimCopterHangarArt>(this, TEXT("FlapArt"));
	}
	FlapArt->SetOriginalGameRoot(ResolveOriginalGameRoot());
	if (!FlapArt->IsUsable())
	{
		return false;
	}

	CheckupWidget =
		SNew(SSimCopterCheckupMenu)
		.State(BuildCheckupState())
		.Art(FlapArt)
		.OnAccepted_Lambda([this](FSimCopterCheckupOrder Order)
		{
			ApplyCheckupOrder(Order);
			CloseCheckupMenu();
		})
		.OnCancelled_Lambda([this]()
		{
			CloseCheckupMenu();
		});

	// Above the dashboard and the debug panel: while it is up it is the only thing to click.
	GEngine->GameViewport->AddViewportWidgetContent(CheckupWidget.ToSharedRef(), 70);
	return true;
}

void ASimCopterHelicopterPawn::CloseCheckupMenu()
{
	if (GEngine != nullptr && GEngine->GameViewport != nullptr && CheckupWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(CheckupWidget.ToSharedRef());
	}
	CheckupWidget.Reset();
}

void ASimCopterHelicopterPawn::SimCheckup()
{
	if (!OpenCheckupMenu())
	{
		UE_LOG(LogSimCopterHelicopterPawn, Warning, TEXT("SimCheckup: no game viewport to open the panel in."));
	}
}

// SCHOOK: CheckupShouldOffer 0x00444750
void ASimCopterHelicopterPawn::UpdateCheckupOffer()
{
	// An unpossessed helicopter can BeginPlay already parked on an airport pad. It must not put a
	// menu over the on-foot player or the front end before its artwork and controls are ready.
	if (!IsPlayerControlled())
	{
		return;
	}

	// Auto-open only after this controlled helicopter has genuinely been airborne. Entering a
	// parked helicopter is not a landing, and neither is the initial session placement.
	if (!bIsLanded)
	{
		bCheckupAutoOpenArmed = true;
		bCheckupOpenedThisLanding = false;
		return;
	}

	if (!bAutoOpenCheckupOnLanding ||
		!bCheckupAutoOpenArmed ||
		bCheckupOpenedThisLanding ||
		CheckupWidget.IsValid())
	{
		return;
	}

	// Unlike FUN_00444750, the playable remake does not hide the panel behind a service-need
	// threshold. Any airport touchdown opens it, including a pristine aircraft, so players learn
	// where repair and refuelling live. Only consume the landing after the viewport accepts the
	// widget; otherwise a transient startup failure would make this landing silently miss it.
	if (FSimCopterCheckup::ShouldOpenOnAirportLanding(BuildCheckupState()) &&
		OpenCheckupMenu())
	{
		bCheckupAutoOpenArmed = false;
		bCheckupOpenedThisLanding = true;
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
	// Filling is automatic while the active attachment is at a class-0..9 surface - but only when
	// the thing on the end of the rope is the bucket. Dunking the rescue harness was scooping
	// water with it (heli[0x32] vs heli[0x33]: the original swaps the object on the rope, and only
	// one of the two holds anything).
	if (bRopeDeployed && !bHarnessRopeEndSelected)
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

				// FUN_00487bb0's else-branch: the scoop keeps splashing while there is still
				// room in the bucket. Once it is full the original takes the clamp branch
				// instead and the sound stops, which is the only cue that it is full.
				if (PreviousWaterPounds < HelicopterTuning.MaxLoadPounds)
				{
					if (USimCopterAudioSubsystem* Audio = GetHelicopterAudio())
					{
						if (!Audio->IsPlaying(SimCopterSound::SND_SPLISH))
						{
							Audio->Play3D(SimCopterSound::SND_SPLISH, BucketWorld);
						}
					}
				}
			}
		}
	}

	const ESimCopterHelicopterTool ActiveTool = GetActiveTool();
	if (bRopeDeployed &&
		(bBucketDumpHeld ||
			(bPrimaryToolUseHeld && ActiveTool == ESimCopterHelicopterTool::WaterBucket) ||
			bCollisionSpill))
	{
		EmitBucketWaterFrame(bCollisionSpill);
	}
	EmitWaterCannonFrame();
	EmitApacheMachineGunFrame();

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
	// Only the HARNESS hangs off BRACKET (heli[0x31]). The bucket keeps the belly point: it is
	// lowered to scoop water and dropped on fires straight below, so swinging it out to the door
	// puts it off the aircraft's centre for no reason. FUN_00487bb0 swaps which rope-end object
	// is drawn, and that same flag decides which anchor the cable leaves from here.
	if (bHarnessRopeEndSelected &&
		bUsingOriginalBracketMesh &&
		bHasBracketRopeAnchor &&
		HeliBracketMeshComponent != nullptr &&
		HeliBracketMeshComponent->IsVisible())
	{
		return HeliBracketMeshComponent->GetComponentTransform().TransformPosition(
			BracketRopeAnchorLocalCm);
	}

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

// SCHOOK: RopeWinchStep 0x00487bb0
bool ASimCopterHelicopterPawn::StepRopeState()
{
	// The winch is a state machine, not a free cursor: the raise/lower keys pick a command
	// for whichever attachment the active tool selects, and the "stow the other one first"
	// rule falls out of SimCopterWinch::Resolve*Command.
	const bool bHarnessSelected = GetActiveTool() == ESimCopterHelicopterTool::RescueHarness;
	int32 Command = SimCopterWinch::CommandIdle;
	if (WinchHeldDirection != 0)
	{
		// A flap rocker under the cursor. It names its own attachment, so it wins over the
		// key axis and over any one-shot still in flight.
		Command = WinchHeldDirection > 0
			? (bWinchHeldHarness
				? SimCopterWinch::ResolveRaiseHarnessCommand(WinchState)
				: SimCopterWinch::ResolveRaiseBucketCommand(WinchState))
			: (bWinchHeldHarness
				? SimCopterWinch::ResolveLowerHarnessCommand(WinchState)
				: SimCopterWinch::ResolveLowerBucketCommand(WinchState));
	}
	else if (RopeAdjustInput > 0.25f)
	{
		Command = bHarnessSelected
			? SimCopterWinch::ResolveRaiseHarnessCommand(WinchState)
			: SimCopterWinch::ResolveRaiseBucketCommand(WinchState);
	}
	else if (RopeAdjustInput < -0.25f)
	{
		Command = bHarnessSelected
			? SimCopterWinch::ResolveLowerHarnessCommand(WinchState)
			: SimCopterWinch::ResolveLowerBucketCommand(WinchState);
	}
	else if (PendingWinchCommand != SimCopterWinch::CommandIdle)
	{
		// A one-shot command from ToggleRope / the debug panel keeps running until the winch
		// reaches its limit, matching how the original holds heli[0x72] while the key is down.
		Command = PendingWinchCommand;
		const bool bRaising = PendingWinchCommand > 0;
		if ((bRaising && WinchState.NodeCursor >= SimCopterWinch::StowedNode) ||
			(!bRaising && WinchState.NodeCursor <= SimCopterWinch::LoweredNode))
		{
			PendingWinchCommand = SimCopterWinch::CommandIdle;
		}
	}

	WinchState.Command = Command;
	const bool bRopeEndChanged = SimCopterWinch::StepWinch(WinchState);
	if (bRopeEndChanged)
	{
		bHarnessRopeEndSelected = WinchState.RopeEnd == SimCopterWinch::ERopeEnd::Harness;
	}

	RopeFirstActiveNode = WinchState.NodeCursor;
	bRopeDeployed = WinchState.IsAnythingDeployed();

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
		SegmentComponent->SetStartScale(FVector2D(RopeSegmentScale, RopeSegmentScale), false);
		SegmentComponent->SetEndScale(FVector2D(RopeSegmentScale, RopeSegmentScale), false);
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

	// FUN_00487bb0 renders exactly one rope-end object: heli[0x32] (BUCKET) or heli[0x33]
	// (HARNESS), selected when the winch crosses node 0x10 on its way down.
	const bool bShowHarnessEnd = bRopeDeployed && bHarnessRopeEndSelected;
	const bool bShowBucketEnd = bRopeDeployed && !bHarnessRopeEndSelected;

	if (OriginalHarnessMeshComponent != nullptr)
	{
		const bool bVisible = bShowHarnessEnd && bUsingOriginalHarnessMesh;
		OriginalHarnessMeshComponent->SetVisibility(bVisible);
		OriginalHarnessMeshComponent->SetHiddenInGame(!bVisible);
		OriginalHarnessMeshComponent->SetWorldLocation(BucketAttachmentWorld);
		OriginalHarnessMeshComponent->SetWorldRotation(BucketRotation);
		OriginalHarnessMeshComponent->SetRelativeScale3D(FVector::OneVector);
	}

	const bool bShowOriginalBucket = bShowBucketEnd && bUsingOriginalBucketMesh;
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
		// The engine cube stands in for whichever rope end failed to load.
		const bool bShowFallbackBucket =
			(bShowBucketEnd && !bUsingOriginalBucketMesh) ||
			(bShowHarnessEnd && !bUsingOriginalHarnessMesh);
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
	const bool bCannonSelected = GetActiveTool() == ESimCopterHelicopterTool::WaterCannon;
	if (!bWaterCannonHeld && !(bPrimaryToolUseHeld && bCannonSelected))
	{
		return;
	}
	if (!IsToolAvailable(ESimCopterHelicopterTool::WaterCannon) || BucketWaterPounds <= 0)
	{
		return;
	}
	if (WaterFXComponent == nullptr)
	{
		return;
	}

	// SCHOOK: WaterCannonEmitter 0x00484d20
	FVector SpawnWorld = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	ResolveToolMuzzle(SpawnWorld, Direction);
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

// SCHOOK: ApacheMachineGunEmitter 0x00485f50 action 0x10
// The machine gun is level-triggered, not edge-triggered: FUN_00485f50 rewrites heli[0x57] = 2
// every frame the button is down and FUN_0048e0b0 type 2 has no cooldown, so it lays down a
// tracer per frame out of a seventy-slot pool.
void ASimCopterHelicopterPawn::EmitApacheMachineGunFrame()
{
	if (ApachePool == nullptr ||
		!bPrimaryToolUseHeld ||
		GetActiveTool() != ESimCopterHelicopterTool::ApacheMachineGun ||
		!IsToolAvailable(ESimCopterHelicopterTool::ApacheMachineGun))
	{
		return;
	}

	FVector Muzzle = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	ResolveToolMuzzle(Muzzle, Direction);
	ApachePool->LaunchBullet(Muzzle, Direction, FlightModel.ForwardSpeed);
}

// =================================================================================================
// Audio. Decode notes in Docs/memory/simcopter-sound.md.
// =================================================================================================

USimCopterAudioSubsystem* ASimCopterHelicopterPawn::GetHelicopterAudio() const
{
	// Every 2D helicopter sound in the original is gated on `heli[8] & 1`, the flag that marks
	// the aircraft the player is flying - an AI helicopter across the city must not put CHOPSTAR
	// in your ears. IsLocallyControlled() is the port of that bit.
	if (!IsLocallyControlled())
	{
		return nullptr;
	}
	return USimCopterAudioSubsystem::Get(this);
}

// SCHOOK: HelicopterRotorSound 0x00488fd0
// SCHOOK: HelicopterSpoolSound 0x00487160 (the state-0 CHOPSTAR/CHOPSTOP branch)
void ASimCopterHelicopterPawn::UpdateHelicopterAudio(float DeltaSeconds)
{
	USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this);
	if (Audio == nullptr)
	{
		return;
	}

	// The listener is the camera, which is also what DAT_0061a748 tracks in the original.
	if (IsLocallyControlled() && CameraComponent != nullptr)
	{
		Audio->SetListener(CameraComponent->GetComponentLocation(), CameraComponent->GetComponentRotation());
	}

	if (!IsLocallyControlled())
	{
		return;
	}

	// --- CHOPSTAR / CHOPSTOP, on the ground ---
	//
	// FUN_00487160: while parked, holding the collective up spools the rotor and plays
	// CHOPSTAR; releasing it plays CHOPSTOP exactly once, on the edge where CHOPSTAR was
	// still running. Neither is a loop - both clips are five seconds long and cover the spool.
	if (FlightModel.State == ESimCopterFlightState::Parked)
	{
		if (LastClimbCommand < 1)
		{
			if (Audio->IsPlaying(SimCopterSound::SND_CHOPSTAR))
			{
				Audio->Play2D(SimCopterSound::SND_CHOPSTOP);
				Audio->Stop(SimCopterSound::SND_CHOPSTAR);
			}
		}
		else
		{
			if (Audio->IsPlaying(SimCopterSound::SND_CHOPSTOP))
			{
				Audio->Stop(SimCopterSound::SND_CHOPSTOP);
			}
			Audio->Play2D(SimCopterSound::SND_CHOPSTAR);
		}
	}

	// --- held tools: the two looping emitters ---
	//
	// FUN_0048e0b0 starts MACHGUN1 and WATERCAN as loops when the emitter is created and
	// FUN_0048ed00 stops them when it dies, so the loop's lifetime is the stream's, not the
	// button's. Reproducing that means re-asserting it every frame from the same predicate the
	// emitter itself uses - Play3D on an already-playing slot only re-aims it.
	{
		const bool bCannonSelected = GetActiveTool() == ESimCopterHelicopterTool::WaterCannon;
		const bool bCannonStreaming =
			(bWaterCannonHeld || (bPrimaryToolUseHeld && bCannonSelected)) &&
			IsToolAvailable(ESimCopterHelicopterTool::WaterCannon) &&
			BucketWaterPounds > 0;
		if (bCannonStreaming)
		{
			Audio->Play3D(SimCopterSound::SND_WATERCAN, GetActorLocation(), SimCopterSoundFlags::Loop);
		}
		else if (Audio->IsPlaying(SimCopterSound::SND_WATERCAN))
		{
			Audio->Stop(SimCopterSound::SND_WATERCAN);
		}

		const bool bGunFiring =
			bPrimaryToolUseHeld &&
			GetActiveTool() == ESimCopterHelicopterTool::ApacheMachineGun &&
			IsToolAvailable(ESimCopterHelicopterTool::ApacheMachineGun);
		if (bGunFiring)
		{
			Audio->Play3D(SimCopterSound::SND_MACHGUN1, GetActorLocation(), SimCopterSoundFlags::Loop);
		}
		else if (Audio->IsPlaying(SimCopterSound::SND_MACHGUN1))
		{
			Audio->Stop(SimCopterSound::SND_MACHGUN1);
		}
	}

	// --- WINCHLP, the winch motor ---
	//
	// FUN_00487bb0 keys this on the sign of heli[0x72], the winch command: negative lowers and
	// positive raises, and only the raise adds the 0xa0 Hz offset that makes it strain.
	{
		const int32 WinchCommand = WinchState.Command;
		if (WinchCommand < 0)
		{
			Audio->Play2D(SimCopterSound::SND_WINCHLP, SimCopterSoundFlags::Loop);
			Audio->ResetFrequency(SimCopterSound::SND_WINCHLP);
		}
		else if (WinchCommand > 0)
		{
			Audio->Play2D(SimCopterSound::SND_WINCHLP, SimCopterSoundFlags::Loop);
			Audio->AddFrequency(SimCopterSound::SND_WINCHLP, 0xa0);
		}
		else if (Audio->IsPlaying(SimCopterSound::SND_WINCHLP))
		{
			Audio->Stop(SimCopterSound::SND_WINCHLP);
		}
	}

	// --- COPLOOP, the engine loop ---
	//
	// FUN_00488fd0 hangs off a counter that lets it run on every seventh 0.05 s frame, so the
	// port paces it on 0.35 s of real time instead of once per rendered frame. Doing it every
	// frame would not sound wrong, but the rate limit is the original's and it is free to keep.
	constexpr float RotorAudioPeriodSeconds = 7.0f * 0.05f;
	RotorAudioAccumulator += DeltaSeconds;
	if (RotorAudioAccumulator < RotorAudioPeriodSeconds)
	{
		return;
	}
	RotorAudioAccumulator = 0.0f;

	// `0x1dffff < heli[0x56]`, i.e. the loop starts once the rotor passes 30.
	constexpr int32 RotorSoundGate1616 = 0x1e0000;
	if (FlightModel.RotorSpeed < RotorSoundGate1616)
	{
		Audio->Stop(SimCopterSound::SND_COPLOOP);
		return;
	}

	if (!Audio->IsPlaying(SimCopterSound::SND_COPLOOP))
	{
		// `heli[0xcc] < 1` - a dry helicopter windmills silently.
		if (FlightModel.Fuel < 1)
		{
			return;
		}

		// FUN_00429ff0(0, DAT_005040e4[type] + 0x54): the engine loop is per model, which is
		// what COPLOOP2..COPLOOP6 are for. The registry already carries the name.
		if (const FSimCopterHelicopterDefinition* Definition =
				SimCopterHelicopterRegistry::FindByTypeIndex(ActiveHelicopterTypeIndex))
		{
			const FString Wav = FPaths::GetBaseFilename(Definition->EngineLoopSound);
			if (!Wav.IsEmpty() && Wav != ActiveEngineLoopSound)
			{
				if (Audio->SetFile(SimCopterSound::SND_COPLOOP, Wav, SimCopterSound::ESoundDir::Root))
				{
					ActiveEngineLoopSound = Wav;
				}
			}
		}
		Audio->Play2D(SimCopterSound::SND_COPLOOP, SimCopterSoundFlags::Loop);
	}

	// The rpm the two laws below read is the plain integer part of heli[0x56].
	const int32 Rpm = FlightModel.RotorSpeed >> 16;

	// FUN_0042a330(0, (rpm*4 - 0x5a0) * 0xf). 0x5a0 is 1440 = 4 * 360, so the delta is zero at
	// 360 rpm and negative below it: the loop is pitched DOWN as the rotor slows, reaching
	// about 0.67x at the 300 rpm lift gate. It is an absolute offset from the clip's own
	// 11025 Hz, not an accumulating one - see AddFrequency.
	Audio->AddFrequency(SimCopterSound::SND_COPLOOP, (Rpm * 4 - 0x5a0) * 0xf);

	// FUN_0042a360(0, (rpm - 0x168) / 4). Only about -80 across the whole range: the rotor's
	// audible character is carried by pitch, and volume barely moves.
	Audio->SetVolumeAdjust(SimCopterSound::SND_COPLOOP, (Rpm - 0x168) / 4);
}

// SCHOOK: HelicopterImpactSounds 0x00484d20 / 0x00489800 / 0x00489ac0 / 0x0048a8b0
void ASimCopterHelicopterPawn::PlayFlightEventAudio(const FSimCopterFlightEvents& Events)
{
	USimCopterAudioSubsystem* Audio = GetHelicopterAudio();
	if (Audio == nullptr)
	{
		return;
	}

	// FUN_00484d20 plays EXPLODE on every hard contact that is not water: rough ground, a
	// building face, and sinking through an elevated pad edge all reach the same call.
	if (Events.bGroundBounce || Events.bPadBounce)
	{
		Audio->Play2D(SimCopterSound::SND_EXPLODE);
	}

	// The water branch instead guards on the sound already running, because a helicopter
	// skipping across a lake re-enters it every frame.
	if (Events.bSplashBounce && !Audio->IsPlaying(SimCopterSound::SND_DOUSE))
	{
		Audio->Play2D(SimCopterSound::SND_DOUSE);
	}

	// FUN_00487160's landing branch: stop the spool-up, then bump or wind down depending on
	// whether there is fuel left to wind down.
	if (Events.bTouchedDown)
	{
		Audio->Stop(SimCopterSound::SND_CHOPSTAR);
		Audio->Play2D(FlightModel.Fuel < 1 ? SimCopterSound::SND_SOFTBMP2 : SimCopterSound::SND_CHOPSTOP);
	}

	// FUN_0048a8b0's phase one: the airframe goes up, and the rescue flyby siren starts.
	if (Events.bCrashed)
	{
		Audio->Play2D(SimCopterSound::SND_BOOM1);
		Audio->Stop(SimCopterSound::SND_COPLOOP);
		Audio->Stop(SimCopterSound::SND_CHOPSTAR);
		Audio->Play3D(SimCopterSound::SND_AMBSRN2, GetActorLocation(), SimCopterSoundFlags::Loop);
	}

	// FUN_00489ac0: past damage tier 7 the engine note goes bad and stays bad. The original
	// re-issues this every frame and relies on the already-playing check to rate-limit it.
	if (Events.DamageTaken > 0)
	{
		const int32 DamageTier = (FlightModel.Tuning.MaxDamage - FlightModel.HitPoints) >> 6;
		if (DamageTier > 7)
		{
			Audio->Play2D(SimCopterSound::SND_MOTOROLD);
		}

		// FUN_00489800: damage taken inside the fire altitude band is fire damage.
		if (LastFlightEnvironmentFireDelta != 0)
		{
			Audio->Play2D(SimCopterSound::SND_FIREDMG);
		}
	}

	// FUN_00484d20's `_DAT_00504014 = 0x2ad` branch: the tanks just ran dry.
	const bool bFuelStarved = FlightModel.Fuel < 1;
	if (bFuelStarved && !bAudioWasFuelStarved)
	{
		Audio->Play2D(SimCopterSound::SND_GASOUT);
	}
	bAudioWasFuelStarved = bFuelStarved;
}

void ASimCopterHelicopterPawn::SimRadio(const FString& Command)
{
	USimCopterRadioSubsystem* Radio = USimCopterRadioSubsystem::Get(this);
	if (Radio == nullptr || Radio->GetStationCount() == 0)
	{
		LastToolStatus = TEXT("No radio stations found under sound/radio.");
		UE_LOG(LogTemp, Display, TEXT("[Radio] %s"), *LastToolStatus);
		return;
	}

	const FString Arg = Command.TrimStartAndEnd();
	if (Arg.IsEmpty() || Arg.Equals(TEXT("next"), ESearchCase::IgnoreCase))
	{
		Radio->NextStation();
	}
	else if (Arg.Equals(TEXT("prev"), ESearchCase::IgnoreCase))
	{
		Radio->PreviousStation();
	}
	else if (Arg.Equals(TEXT("off"), ESearchCase::IgnoreCase))
	{
		Radio->SetPowered(false);
	}
	else if (Arg.Equals(TEXT("on"), ESearchCase::IgnoreCase))
	{
		Radio->SetPowered(true);
	}
	else if (Arg.IsNumeric())
	{
		Radio->SetStationIndex(FCString::Atoi(*Arg));
	}
	else
	{
		const TArray<FSimCopterRadioStation>& Stations = Radio->GetStations();
		const int32 Found = Stations.IndexOfByPredicate(
			[&Arg](const FSimCopterRadioStation& Station)
			{
				return Station.CallSign.Equals(Arg, ESearchCase::IgnoreCase);
			});
		if (Found == INDEX_NONE)
		{
			LastToolStatus = FString::Printf(TEXT("Unknown station '%s'."), *Arg);
			UE_LOG(LogTemp, Display, TEXT("[Radio] %s"), *LastToolStatus);
			return;
		}
		Radio->SetStationIndex(Found);
	}

	LastToolStatus = FString::Printf(
		TEXT("Radio %s, station %d/%d (%s)."),
		Radio->IsPowered() ? TEXT("on") : TEXT("off"),
		Radio->GetStationIndex() + 1,
		Radio->GetStationCount(),
		*Radio->GetStationCallSign());
	UE_LOG(LogTemp, Display, TEXT("[Radio] %s"), *LastToolStatus);
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

void ASimCopterHelicopterPawn::SimStartMission(int32 TypeMask)
{
	if (ASimCopterMissionSystemActor* MissionSystem = ResolveMissionSystem())
	{
		const int32 EventId = MissionSystem->StartMissionNow(TypeMask);
		UE_LOG(LogSimCopterHelicopterPawn, Display,
			TEXT("SimStartMission: mask 0x%x -> event %d."), TypeMask, EventId);
	}
}

void ASimCopterHelicopterPawn::SimDumpAmbientVehicles()
{
	if (ASimCopterMissionSystemActor* MissionSystem = ResolveMissionSystem())
	{
		if (ASimCopterAmbientVehiclesActor* Vehicles = MissionSystem->ResolveAmbientVehicles())
		{
			UE_LOG(LogSimCopterHelicopterPawn, Display,
				TEXT("SimDumpAmbientVehicles: %s"), *Vehicles->GetStatusLine());
			return;
		}
	}
	UE_LOG(LogSimCopterHelicopterPawn, Display, TEXT("SimDumpAmbientVehicles: unavailable."));
}

void ASimCopterHelicopterPawn::SimGotoBuilding(int32 XbldId)
{
	const ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterTrafficSystemActor::StaticClass()));
	if (TrafficSystem == nullptr)
	{
		UE_LOG(LogSimCopterHelicopterPawn, Display, TEXT("SimGotoBuilding: no traffic system."));
		return;
	}

	// Nearest tile carrying the id, by distance from where we are now.
	const FVector Here = GetActorLocation();
	FVector BestLocation = FVector::ZeroVector;
	FIntPoint BestTile(INDEX_NONE, INDEX_NONE);
	float BestDistanceSq = TNumericLimits<float>::Max();
	for (int32 TileY = 0; TileY < FSimCity2000City::MapSize; ++TileY)
	{
		for (int32 TileX = 0; TileX < FSimCity2000City::MapSize; ++TileX)
		{
			if (TrafficSystem->GetXbldTileId(TileX, TileY) != XbldId)
			{
				continue;
			}
			FVector TileCenter = FVector::ZeroVector;
			if (!TrafficSystem->TryGetTileCenterWorldLocation(TileX, TileY, TileCenter))
			{
				continue;
			}
			const float DistanceSq = FVector::DistSquared2D(TileCenter, Here);
			if (DistanceSq < BestDistanceSq)
			{
				BestDistanceSq = DistanceSq;
				BestLocation = TileCenter;
				BestTile = FIntPoint(TileX, TileY);
			}
		}
	}

	if (BestTile.X == INDEX_NONE)
	{
		UE_LOG(LogSimCopterHelicopterPawn, Display, TEXT("SimGotoBuilding 0x%02x: this city has none."), XbldId);
		return;
	}

	// The nearest matching tile is whichever corner of the footprint we approached from, so grow
	// it into the whole run of same-id tiles and aim at that block's middle - the roof.
	FIntPoint Min = BestTile;
	FIntPoint Max = BestTile;
	while (Min.X > 0 && TrafficSystem->GetXbldTileId(Min.X - 1, BestTile.Y) == XbldId) { --Min.X; }
	while (Max.X < FSimCity2000City::MapSize - 1 && TrafficSystem->GetXbldTileId(Max.X + 1, BestTile.Y) == XbldId) { ++Max.X; }
	while (Min.Y > 0 && TrafficSystem->GetXbldTileId(BestTile.X, Min.Y - 1) == XbldId) { --Min.Y; }
	while (Max.Y < FSimCity2000City::MapSize - 1 && TrafficSystem->GetXbldTileId(BestTile.X, Max.Y + 1) == XbldId) { ++Max.Y; }

	FVector CornerA = FVector::ZeroVector;
	FVector CornerB = FVector::ZeroVector;
	if (TrafficSystem->TryGetTileCenterWorldLocation(Min.X, Min.Y, CornerA) &&
		TrafficSystem->TryGetTileCenterWorldLocation(Max.X, Max.Y, CornerB))
	{
		BestLocation = (CornerA + CornerB) * 0.5f;
	}

	// Set down on whatever is on top: the roof if there is one, the street otherwise. This is a
	// teleport, so it goes through the helipad placement - a bare SetActorLocation is overwritten
	// by the flight model's own position on the very next tick.
	FVector Surface = BestLocation;
	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimGotoBuilding), false, this);
	if (GetWorld()->LineTraceSingleByChannel(
			Hit,
			BestLocation + FVector(0.0f, 0.0f, 60000.0f),
			BestLocation - FVector(0.0f, 0.0f, 2000.0f),
			ECC_Camera,
			QueryParams) &&
		Hit.bBlockingHit)
	{
		Surface = Hit.ImpactPoint;
	}

	PlaceOnHelipad(Surface, GetActorRotation().Yaw);
	UE_LOG(LogSimCopterHelicopterPawn, Display,
		TEXT("SimGotoBuilding 0x%02x: tiles (%d, %d)-(%d, %d), set down on %s (street Z %.0f)."),
		XbldId, Min.X, Min.Y, Max.X, Max.Y, *Surface.ToCompactString(), BestLocation.Z);
}

void ASimCopterHelicopterPawn::SimSwitchHeli(int32 TypeIndex)
{
	SwitchHelicopterModel(TypeIndex);
	UE_LOG(LogSimCopterHelicopterPawn, Display, TEXT("SimSwitchHeli %d: %s"), TypeIndex, *LastModelSwitchStatus);
}

void ASimCopterHelicopterPawn::SimCycleHeli(int32 Delta)
{
	CycleHelicopterModel(Delta != 0 ? Delta : 1);
	UE_LOG(LogSimCopterHelicopterPawn, Display, TEXT("SimCycleHeli: %s"), *LastModelSwitchStatus);
}

void ASimCopterHelicopterPawn::SimSelectTool(int32 ToolIndex)
{
	if (ToolIndex < 0 || ToolIndex >= static_cast<int32>(ESimCopterHelicopterTool::Count))
	{
		UE_LOG(LogSimCopterHelicopterPawn, Warning, TEXT("SimSelectTool: index %d out of range."), ToolIndex);
		return;
	}
	SetSelectedTool(static_cast<ESimCopterHelicopterTool>(ToolIndex));
	UE_LOG(
		LogSimCopterHelicopterPawn,
		Display,
		TEXT("SimSelectTool %d: %s (%s), active %s"),
		ToolIndex,
		SimCopterHelicopterRegistry::GetToolDisplayName(SelectedTool),
		*DescribeToolAvailability(SelectedTool),
		SimCopterHelicopterRegistry::GetToolDisplayName(GetActiveTool()));
}

void ASimCopterHelicopterPawn::SimGrantTool(int32 ToolIndex, int32 bGranted)
{
	if (ToolIndex < 0 || ToolIndex >= static_cast<int32>(ESimCopterHelicopterTool::Count))
	{
		return;
	}
	const ESimCopterHelicopterTool Tool = static_cast<ESimCopterHelicopterTool>(ToolIndex);
	SetDebugToolGrant(Tool, bGranted != 0);
	UE_LOG(
		LogSimCopterHelicopterPawn,
		Display,
		TEXT("SimGrantTool %s -> %s (career mask 0x%02x unchanged, effective 0x%02x)"),
		SimCopterHelicopterRegistry::GetToolDisplayName(Tool),
		*DescribeToolAvailability(Tool),
		EquipmentState.CareerEquipmentMask,
		EquipmentState.GetEffectiveEquipmentMask());
}

void ASimCopterHelicopterPawn::SimDumpHeliState()
{
	const FSimCopterHelicopterDefinition* Definition = GetHelicopterDefinition();
	UE_LOG(
		LogSimCopterHelicopterPawn,
		Display,
		TEXT("Model: %s (type %d) seats %d maxload %d notar %d apache %d mesh %d | ")
		TEXT("Tool: %s (%s) active %s | Career 0x%02x debug 0x%02x gas %d | ")
		TEXT("Rope node %d bucketStowed %d harnessStowed %d end %s | ")
		TEXT("Spotlight valid %d tile (%d,%d) band %d dist %.1f"),
		Definition != nullptr ? *Definition->DisplayName : TEXT("<none>"),
		ActiveHelicopterTypeIndex,
		FlightModel.Tuning.PassengerSeats,
		HelicopterTuning.MaxLoadPounds,
		Definition != nullptr ? Definition->bNoTailRotor : false,
		Definition != nullptr ? Definition->bApacheArmament : false,
		bUsingOriginalMesh,
		SimCopterHelicopterRegistry::GetToolDisplayName(SelectedTool),
		*DescribeToolAvailability(SelectedTool),
		SimCopterHelicopterRegistry::GetToolDisplayName(GetActiveTool()),
		EquipmentState.CareerEquipmentMask,
		EquipmentState.DebugGrantedEquipmentMask,
		EquipmentState.GetTearGasRounds(),
		WinchState.NodeCursor,
		WinchState.bBucketStowed,
		WinchState.bHarnessStowed,
		bHarnessRopeEndSelected ? TEXT("HARNESS") : TEXT("BUCKET"),
		SpotlightTarget.bValid,
		SpotlightTarget.Tile.X,
		SpotlightTarget.Tile.Y,
		SpotlightTarget.Band,
		SpotlightTarget.DistanceUnits);
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

// Cockpit stabilization. The eye rides the airframe's own pitch and roll, but through a damped,
// partial copy of it: CockpitAttitudeFollowStrength decides how much of the tilt reaches the
// view and CockpitAttitudeLerpSpeed how quickly, so a gust or a hard cyclic input does not snap
// the horizon over. It is a presentation filter shared by the cockpit camera, crosshair, and
// cannon view model; the flight model, ModelPivot, rope anchor, and real tool emission direction
// keep reading the true attitude, so handling and simulation aim are untouched.
//
// Advanced here, before the visuals and the camera consume it, so the stabilized props and the
// stabilized eye are placed from the same value on the same frame.
void ASimCopterHelicopterPawn::AdvanceCockpitStabilizedAttitude(float DeltaSeconds)
{
	const FRotator TargetAttitudeDeg(
		FMath::Clamp(
			CurrentPitchDeg * CockpitAttitudeFollowStrength,
			-CockpitAttitudeMaxDeg,
			CockpitAttitudeMaxDeg),
		0.0f,
		FMath::Clamp(
			CurrentRollDeg * CockpitAttitudeFollowStrength,
			-CockpitAttitudeMaxDeg,
			CockpitAttitudeMaxDeg));
	if (!bCockpitStabilizedAttitudeInitialized)
	{
		CockpitStabilizedAttitudeDeg = TargetAttitudeDeg;
		bCockpitStabilizedAttitudeInitialized = true;
		return;
	}

	CockpitStabilizedAttitudeDeg = FMath::RInterpTo(
		CockpitStabilizedAttitudeDeg,
		TargetAttitudeDeg,
		DeltaSeconds,
		CockpitAttitudeLerpSpeed);
}

void ASimCopterHelicopterPawn::UpdateVisuals(float DeltaSeconds)
{
	AdvanceCockpitStabilizedAttitude(DeltaSeconds);

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
	// From the pilot's seat the eye is inside a closed fuselage shell - the GEO has no
	// windscreen - so the cockpit view hides the fuselage itself. Visibility is per component
	// and does not cascade unless asked to, so the rotors and the cannon still draw, which is
	// what makes the equipment visible from in here.
	const bool bHideFuselageForView = CameraModeIsFirstPerson(CameraMode);
	if (HeliBodyMeshComponent != nullptr)
	{
		HeliBodyMeshComponent->SetVisibility(bUsingOriginalMesh && !bHideFuselageForView, false);
	}
	if (BodyMeshComponent != nullptr)
	{
		BodyMeshComponent->SetVisibility(!bUsingOriginalMesh && !bHideFuselageForView, false);
	}

	// The cannon is bolted on with the equipment, so it appears the moment the tool is fitted
	// (career purchase or a debug grant) rather than only while it is the selected tool. In the
	// cockpit the camera-carried view model stands in for it, so the world one steps aside.
	if (HeliCannonMeshComponent != nullptr)
	{
		const bool bShowCannon =
			bUsingOriginalMesh &&
			bUsingOriginalCannonMesh &&
			!CameraModeIsFirstPerson(CameraMode) &&
			IsToolAvailable(ESimCopterHelicopterTool::WaterCannon);
		HeliCannonMeshComponent->SetVisibility(bShowCannon);
		HeliCannonMeshComponent->SetHiddenInGame(!bShowCannon);
	}

	// Same rule for the harness bracket: bolted on with the equipment, gone in the cockpit view
	// where the fuselage itself is hidden. It is what the winched Sim is brought up alongside, so
	// it belongs to the harness rather than to the rope being out.
	if (HeliBracketMeshComponent != nullptr)
	{
		const bool bShowBracket =
			bUsingOriginalMesh &&
			bUsingOriginalBracketMesh &&
			!CameraModeIsFirstPerson(CameraMode) &&
			IsToolAvailable(ESimCopterHelicopterTool::RescueHarness);
		HeliBracketMeshComponent->SetVisibility(bShowBracket);
		HeliBracketMeshComponent->SetHiddenInGame(!bShowBracket);
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

	// The cockpit is a fixed station: the pilot may look up and down, but the seat neither
	// swivels nor slides. Yaw look and the middle-drag height pan are therefore off in that
	// view, which also keeps the crosshair on the aircraft's heading - the line the tools
	// actually fire along.
	const bool bFirstPersonView = CameraModeIsFirstPerson(CameraMode);
	// A cockpit eye has no boom to soften. Leaving the spring arm's own lag enabled meant
	// camera-parented props followed a different transform from the camera calculations below.
	CameraBoom->bEnableCameraLag = !bFirstPersonView;
	CameraBoom->bEnableCameraRotationLag = !bFirstPersonView;

	// Gamepad look drives the camera continuously; mouse look only contributes while a mouse
	// button is held (a click-drag). Dragging, then the CameraRecenterDelaySeconds window
	// after release, holds the offset; gamepad look recenters immediately on release.
	const bool bControllerRightStickLooks =
		!bControllerCameraAdjustHeld &&
		ControllerMode != ESimCopterControllerMode::DispatchWheel &&
		ControllerMode != ESimCopterControllerMode::ToolWheel;
	const float ControllerYawLookInput =
		bControllerRightStickLooks ? ControllerRightXInput : 0.0f;
	// Match right-mouse drag in boom views: stick-up drags the world down. The cockpit's
	// PitchLookSign below reverses it into ordinary head-look behavior.
	const float ControllerPitchLookInput =
		bControllerRightStickLooks ? -ControllerRightYInput : 0.0f;
	float YawLookInput =
		bFirstPersonView ? 0.0f : CameraYawInput + ControllerYawLookInput;
	float PitchLookInput = CameraPitchInput + ControllerPitchLookInput;
	if (bCameraDragActive)
	{
		if (!bFirstPersonView)
		{
			YawLookInput += MouseLookYawInput;
		}
		// A middle-drag in progress owns vertical mouse travel, so holding both buttons pans
		// instead of doing both at once - except in the cockpit, where there is no pan for it
		// to own and the look should keep working.
		if (!bCameraPanDragActive || bFirstPersonView)
		{
			PitchLookInput += MouseLookPitchInput;
		}
		CameraRecenterDelayRemaining = CameraRecenterDelaySeconds;
	}
	else if (CameraRecenterDelayRemaining > 0.0f)
	{
		CameraRecenterDelayRemaining = FMath::Max(0.0f, CameraRecenterDelayRemaining - DeltaSeconds);
	}

	// Middle-drag slides the camera along its own up axis, which walks the helicopter the
	// other way up the screen. Mouse axes are already per-frame deltas, so this is not scaled
	// by DeltaSeconds; the result is left exactly where the player dropped it (no recenter),
	// separately per camera view and only for this session.
	float& CameraViewPanOffsetCm = CameraViewPanOffsetsCm[GetCameraModeIndex(CameraMode)];
	if (!bFirstPersonView && bCameraPanDragActive && !FMath::IsNearlyZero(MouseLookPitchInput))
	{
		CameraViewPanOffsetCm = FMath::Clamp(
			CameraViewPanOffsetCm + MouseLookPitchInput * CameraPanCmPerMouseUnit,
			-CameraPanMaxOffsetCm,
			CameraPanMaxOffsetCm);
	}

	// Zeroed rather than merely frozen, so a yaw carried in from a boom view cannot leave the
	// cockpit staring off the nose.
	CameraYawOffsetDeg = bFirstPersonView
		? 0.0f
		: CameraYawOffsetDeg + YawLookInput * CameraYawSpeedDegPerSec * DeltaSeconds;
	// The boom views drag the world: pulling the mouse up swings the camera down over the
	// helicopter. From inside the cockpit that reads backwards - you are turning your head,
	// not the scene - so the vertical sense is inverted for that view only.
	const float PitchLookSign = CameraModeIsFirstPerson(CameraMode) ? -1.0f : 1.0f;
	const float MinPitchLookDeg = bFirstPersonView ? -89.0f : -28.0f;
	CameraPitchOffsetDeg = FMath::Clamp(
		CameraPitchOffsetDeg + PitchLookSign * PitchLookInput * CameraPitchSpeedDegPerSec * DeltaSeconds,
		MinPitchLookDeg,
		18.0f);

	const bool bHoldOffset = bCameraDragActive || CameraRecenterDelayRemaining > 0.0f;
	const bool bRecenterYaw =
		!bHoldOffset &&
		FMath::IsNearlyZero(CameraYawInput + ControllerYawLookInput, 0.01f);

	const float HorizontalSpeed = FVector(VelocityCmPerSec.X, VelocityCmPerSec.Y, 0.0f).Size();
	const float SpeedAlpha = FMath::Clamp(HorizontalSpeed / FMath::Max(1.0f, MaxForwardSpeedCmPerSec), 0.0f, 1.0f);
	const float ActorYaw = GetActorRotation().Yaw;
	const float TargetRopeAutoZoom = ShouldUseRopeAutoZoom(
		CameraMode, CameraZoomAlpha, RopeFirstActiveNode)
		? FullyLoweredRopeZoomOutAlpha
		: 0.0f;
	RopeAutoZoomAlpha = FMath::FInterpTo(
		RopeAutoZoomAlpha,
		TargetRopeAutoZoom,
		DeltaSeconds,
		RopeAutoZoomLerpSpeed);
	const float EffectiveCameraZoomAlpha = FMath::Clamp(
		CameraZoomAlpha +
			(CameraMode == ESimCopterCameraMode::Chase ? RopeAutoZoomAlpha : 0.0f),
		0.0f,
		1.0f);
	const float MaxZoomDistanceCmForView =
		GetCameraViewMaxZoomDistanceCm(CameraMode);
	float ViewYaw = ActorYaw;
	float ViewPitch =
		ChaseCameraBasePitch + SpeedAlpha * ChaseCameraForwardPitchLiftDeg;
	float ZoomArmLength =
		FMath::Lerp(ChaseCameraMinDistance, MaxZoomDistanceCmForView, EffectiveCameraZoomAlpha);
	float ReferenceZoomArmLength =
		FMath::Lerp(ChaseCameraMinDistance, MaxZoomDistanceCmForView, CameraDefaultZoomAlpha);
	float ArmLength = ZoomArmLength + SpeedAlpha * ChaseSpeedPullbackCm;
	FVector CameraTranslationWorld(
		0.0f,
		0.0f,
		ChaseCameraTargetHeightCm + SpeedAlpha * ChaseCameraSpeedTargetLiftCm);
	// Non-zero only for the cockpit: the boom views stay level with the horizon.
	FRotator ViewAttitudeTiltDeg = FRotator::ZeroRotator;

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
		ZoomArmLength = FMath::Lerp(640.0f, MaxZoomDistanceCmForView, EffectiveCameraZoomAlpha);
		ReferenceZoomArmLength = FMath::Lerp(640.0f, MaxZoomDistanceCmForView, CameraDefaultZoomAlpha);
		ArmLength = ZoomArmLength;
		CameraTranslationWorld = FVector(0.0f, 0.0f, 120.0f);
	}
	else if (CameraMode == ESimCopterCameraMode::Cockpit)
	{
		// No boom at all: the eye sits wherever the view offset puts it, looking straight ahead
		// down the nose. That is also what makes the crosshair line up with the tools, which
		// all fire along the model's forward axis.
		ViewYaw = ActorYaw;
		ViewPitch = 0.0f;
		ZoomArmLength = 0.0f;
		ReferenceZoomArmLength = 0.0f;
		ArmLength = 0.0f;
		CameraTranslationWorld = FVector::ZeroVector;
		// Bolt the eye to the airframe (as stabilized above) so the cockpit banks and pitches
		// with it instead of hanging level behind the nose.
		ViewAttitudeTiltDeg = CockpitStabilizedAttitudeDeg;
	}
	else
	{
		ViewYaw = ActorYaw;
		ViewPitch = RescueCameraPitch;
		ZoomArmLength = FMath::Lerp(860.0f, MaxZoomDistanceCmForView, EffectiveCameraZoomAlpha);
		ReferenceZoomArmLength = FMath::Lerp(860.0f, MaxZoomDistanceCmForView, CameraDefaultZoomAlpha);
		ArmLength = ZoomArmLength;
		CameraTranslationWorld = FVector(0.0f, 0.0f, 30.0f);
		if (bRecenterYaw)
		{
			CameraYawOffsetDeg = FMath::FInterpTo(CameraYawOffsetDeg, 0.0f, DeltaSeconds, 0.8f);
		}
	}

	const FSimCopterCameraViewDebugOffset& ViewDebugOffset =
		CameraViewDebugOffsets[GetCameraModeIndex(CameraMode)];
	// The view offset is authored in the aircraft's frame, so it is placed through the same
	// tilt the view adopts: in the cockpit the seat then stays put in the cabin as the
	// helicopter banks, rather than sliding across it. Zero tilt reduces this to the plain
	// yaw rotation the boom views have always used.
	const FQuat ViewFrameQuat =
		FQuat(FRotator(0.0f, ActorYaw, 0.0f)) * FQuat(ViewAttitudeTiltDeg);
	const FVector DebugTranslationWorld =
		ViewFrameQuat.RotateVector(ViewDebugOffset.TranslationCm);
	CameraTranslationWorld += DebugTranslationWorld;

	// Scaling the complete framing translation by distance preserves its projected screen
	// offset. At strength 1 the helicopter therefore stays at the same vertical position as
	// zoom changes, leaving the extra pulled-back area available primarily for more ground.
	// A boomless view (cockpit) has no reference distance to scale against, and dividing into
	// its zero-length arm would collapse the offset that positions the eye.
	const float ZoomDistanceRatio = ReferenceZoomArmLength > 1.0f
		? ZoomArmLength / ReferenceZoomArmLength
		: 1.0f;
	const float ZoomFramingScale = FMath::Lerp(
		1.0f,
		ZoomDistanceRatio,
		ViewDebugOffset.ZoomVerticalFramingStrength);
	CameraTranslationWorld *= ZoomFramingScale;

	const float BaselineRelativeYaw = FRotator::NormalizeAxis(
		ViewYaw - ActorYaw + ViewDebugOffset.RotationDeg.Yaw);
	const float TargetRelativeYaw = FRotator::NormalizeAxis(
		BaselineRelativeYaw + CameraYawOffsetDeg);
	const float MinCameraPitchDeg = bFirstPersonView ? -89.0f : -78.0f;
	constexpr float MaxCameraPitchDeg = 2.0f;
	const float BaselinePitchDeg = FRotator::NormalizeAxis(
		FMath::Clamp(
			ViewPitch,
			MinCameraPitchDeg,
			MaxCameraPitchDeg) +
		ViewDebugOffset.RotationDeg.Pitch);
	const float TargetPitchDeg = FRotator::NormalizeAxis(
		FMath::Clamp(
			ViewPitch + CameraPitchOffsetDeg,
			MinCameraPitchDeg,
			MaxCameraPitchDeg) +
		ViewDebugOffset.RotationDeg.Pitch);
	// The airframe tilt is composed in the view's own frame, so roll turns the horizon about
	// the direction of sight and pitch rides on top of the view pitch, which is what makes the
	// cockpit feel bolted to the model. It also keeps the tilt outside the level-view pitch
	// clamp above, so a steep nose-up attitude is not flattened.
	auto ApplyViewAttitudeTilt = [&ViewAttitudeTiltDeg](const FRotator& LevelRotation)
	{
		return ViewAttitudeTiltDeg.IsNearlyZero()
			? LevelRotation
			: (FQuat(LevelRotation) * FQuat(ViewAttitudeTiltDeg)).Rotator();
	};
	const FRotator TargetCameraWorldRotation = ApplyViewAttitudeTilt(FRotator(
		TargetPitchDeg,
		ActorYaw + TargetRelativeYaw,
		ViewDebugOffset.RotationDeg.Roll));
	const FRotator BaselineCameraWorldRotation = ApplyViewAttitudeTilt(FRotator(
		BaselinePitchDeg,
		ActorYaw + BaselineRelativeYaw,
		ViewDebugOffset.RotationDeg.Roll));

	// Keep the framing translation fixed in camera space while right-drag moves the view.
	// Rotating it from the baseline frame into the looked frame prevents the helicopter from
	// sliding vertically; the existing strength control blends the effect.
	const FVector LookCompensatedTranslationWorld =
		TargetCameraWorldRotation.RotateVector(
			BaselineCameraWorldRotation.UnrotateVector(CameraTranslationWorld));
	CameraTranslationWorld = FMath::Lerp(
		CameraTranslationWorld,
		LookCompensatedTranslationWorld,
		ViewDebugOffset.ZoomVerticalFramingStrength);

	const FVector CameraAnchorWorld = CameraAnchor != nullptr
		? CameraAnchor->GetComponentLocation()
		: GetActorLocation();

	// The pan offset is authored in camera space, so it needs no look compensation -- rotating
	// it out of the view frame is what keeps it fixed on screen while the view turns. Scaling
	// by the zoom ratio holds the same screen offset as the arm length changes.
	CameraTranslationWorld += TargetCameraWorldRotation.RotateVector(
		FVector(0.0f, 0.0f, CameraViewPanOffsetCm * ZoomDistanceRatio));

	// A rigidly mounted eye must not ease toward anything: easing is lag, and lag against a
	// helicopter that is already moving reads as the aircraft sliding around the view. The only
	// softening the cockpit gets is the attitude stabilization, which is already baked into the
	// target here and consumed by the cockpit presentation props, so they stay locked.
	if (!bCameraViewSmoothingInitialized || bFirstPersonView)
	{
		SmoothedCameraArmLengthCm = ArmLength;
		SmoothedCameraTranslationWorld = CameraTranslationWorld;
		SmoothedCameraViewWorldRotation = TargetCameraWorldRotation;
		bCameraViewSmoothingInitialized = true;
	}
	else
	{
		SmoothedCameraArmLengthCm = FMath::FInterpTo(
			SmoothedCameraArmLengthCm,
			ArmLength,
			DeltaSeconds,
			CameraViewPositionLerpSpeed);
		SmoothedCameraTranslationWorld = FMath::VInterpTo(
			SmoothedCameraTranslationWorld,
			CameraTranslationWorld,
			DeltaSeconds,
			CameraViewPositionLerpSpeed);
		SmoothedCameraViewWorldRotation = FMath::RInterpTo(
			SmoothedCameraViewWorldRotation,
			TargetCameraWorldRotation,
			DeltaSeconds,
			CameraViewRotationLerpSpeed);
	}

	ArmLength = SmoothedCameraArmLengthCm;
	CameraTranslationWorld = SmoothedCameraTranslationWorld;
	const FRotator DesiredCameraWorldRotation = SmoothedCameraViewWorldRotation;
	const float DesiredPitchDeg = DesiredCameraWorldRotation.Pitch;
	const float DesiredRelativeYaw = FRotator::NormalizeAxis(
		DesiredCameraWorldRotation.Yaw - ActorYaw);
	const float DesiredRollDeg = DesiredCameraWorldRotation.Roll;
	const FVector UnliftedBoomOrigin = CameraAnchorWorld + CameraTranslationWorld;
	// Ground lift and the obstruction pull-in exist to keep a trailing boom out of the scenery.
	// A cockpit eye has no boom, and letting either ease the eye up or in would move the pilot
	// relative to the airframe - exactly the drift this view must not have.
	const float DesiredGroundLiftCm = bFirstPersonView
		? 0.0f
		: ResolveCameraGroundLift(
			UnliftedBoomOrigin,
			ArmLength,
			DesiredCameraWorldRotation);
	const float PreviousGroundLiftCm = CurrentCameraGroundLiftCm;
	const float PreliminaryGroundLiftCm = bFirstPersonView
		? 0.0f
		: FMath::FInterpTo(
			CurrentCameraGroundLiftCm,
			DesiredGroundLiftCm,
			DeltaSeconds,
			CameraGroundLiftLerpSpeed);
	CameraTranslationWorld.Z += PreliminaryGroundLiftCm;

	const FVector BoomOrigin = CameraAnchorWorld + CameraTranslationWorld;
	const FRotator TargetAvoidanceOffset =
		FindCameraAvoidanceOffset(BoomOrigin, ArmLength, DesiredCameraWorldRotation);
	const bool bAvoiding =
		!TargetAvoidanceOffset.IsNearlyZero(0.01f);
	const float AvoidanceLerpSpeed =
		bAvoiding ? CameraAvoidanceLerpSpeed : CameraAvoidanceReturnLerpSpeed;
	CurrentCameraAvoidanceOffsetDeg.Pitch = FMath::FInterpTo(
		CurrentCameraAvoidanceOffsetDeg.Pitch,
		TargetAvoidanceOffset.Pitch,
		DeltaSeconds,
		AvoidanceLerpSpeed);
	CurrentCameraAvoidanceOffsetDeg.Yaw = FMath::FInterpTo(
		CurrentCameraAvoidanceOffsetDeg.Yaw,
		TargetAvoidanceOffset.Yaw,
		DeltaSeconds,
		AvoidanceLerpSpeed);
	CurrentCameraAvoidanceOffsetDeg.Roll = 0.0f;

	const float AvoidedPitchDeg = FMath::Clamp(
		DesiredPitchDeg + CurrentCameraAvoidanceOffsetDeg.Pitch,
		bFirstPersonView ? -89.0f : -88.0f,
		60.0f);
	const float AvoidedRelativeYaw = FRotator::NormalizeAxis(
		DesiredRelativeYaw + CurrentCameraAvoidanceOffsetDeg.Yaw);
	const FRotator CameraWorldRotation(
		AvoidedPitchDeg,
		ActorYaw + AvoidedRelativeYaw,
		DesiredRollDeg);

	// Re-evaluate the height response at the smoothed avoidance angle. The proximity range
	// starts this correction early enough that both raising and settling can remain smooth.
	const float AvoidedDesiredGroundLiftCm = bFirstPersonView
		? 0.0f
		: ResolveCameraGroundLift(
			UnliftedBoomOrigin,
			ArmLength,
			CameraWorldRotation);
	CurrentCameraGroundLiftCm = bFirstPersonView
		? 0.0f
		: FMath::FInterpTo(
			PreviousGroundLiftCm,
			AvoidedDesiredGroundLiftCm,
			DeltaSeconds,
			CameraGroundLiftLerpSpeed);
	CameraTranslationWorld.Z =
		UnliftedBoomOrigin.Z - CameraAnchorWorld.Z + CurrentCameraGroundLiftCm;

	// Pull in along the same roof-to-camera segment used by avoidance. Scaling the arm and
	// endpoint translation together keeps that segment geometrically exact; the old approach
	// shortened only the arm from a translated origin, which could point below terrain.
	const FVector DesiredCameraLocation =
		CameraAnchorWorld +
		CameraTranslationWorld -
		CameraWorldRotation.Vector() * ArmLength;
	const float TargetPullInAlpha = bFirstPersonView
		? 1.0f
		: ResolveCameraPullInAlpha(CameraAnchorWorld, DesiredCameraLocation);
	const float PullInLerpSpeed =
		TargetPullInAlpha < CurrentCameraPullInAlpha
			? CameraObstructionPullInLerpSpeed
			: CameraObstructionReleaseLerpSpeed;
	CurrentCameraPullInAlpha = bFirstPersonView
		? 1.0f
		: FMath::FInterpTo(
			CurrentCameraPullInAlpha,
			TargetPullInAlpha,
			DeltaSeconds,
			PullInLerpSpeed);

	const FVector PulledInTranslation =
		CameraTranslationWorld * CurrentCameraPullInAlpha;
	CameraBoom->TargetArmLength = ArmLength * CurrentCameraPullInAlpha;
	CameraBoom->TargetOffset = FVector::ZeroVector;
	CameraBoom->SocketOffset =
		CameraWorldRotation.UnrotateVector(PulledInTranslation);
	CameraBoom->SetWorldRotation(CameraWorldRotation);

	// BaselineCameraWorldRotation contains the same damped cockpit attitude as the camera but
	// precedes CameraPitchOffsetDeg. The cannon therefore follows aircraft stabilization while
	// user look pitch sweeps the view past it.
	UpdateCockpitCannonViewModel(BaselineCameraWorldRotation);
}

void ASimCopterHelicopterPawn::UpdateCockpitCannonViewModel(
	const FRotator& MountWorldRotation)
{
	if (CockpitCannonMeshComponent == nullptr)
	{
		return;
	}

	const bool bShowViewModel =
		CameraModeIsFirstPerson(CameraMode) &&
		bUsingOriginalCannonMesh &&
		IsToolAvailable(ESimCopterHelicopterTool::WaterCannon);
	CockpitCannonMeshComponent->SetVisibility(bShowViewModel);
	CockpitCannonMeshComponent->SetHiddenInGame(!bShowViewModel);
	if (!bShowViewModel)
	{
		return;
	}

	// Position is truly camera-local, so spring-arm/component tick order cannot create drift.
	CockpitCannonMeshComponent->SetRelativeLocation(CockpitCannonViewModelOffsetCm);

	// Absolute rotation follows the stabilized cockpit mount rather than reconstructing a
	// pitchless rotator from final camera Euler angles. That removes only dynamic look pitch,
	// retaining the camera's damped airframe pitch and roll without cross-axis coupling.
	CockpitCannonMeshComponent->SetWorldRotation(MountWorldRotation);
}

float ASimCopterHelicopterPawn::ResolveCameraGroundLift(
	const FVector& BoomOrigin,
	float ArmLength,
	const FRotator& WorldRotation) const
{
	if (GetWorld() == nullptr || ArmLength <= UE_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const FVector DesiredCameraLocation = BoomOrigin - WorldRotation.Vector() * ArmLength;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterCameraGroundProbe), false, this);

	// Whatever the camera is standing over counts as its ground, buildings included - an airport
	// apron, a helipad and a rooftop are all surfaces you park on, and the lift should read them
	// exactly like terrain.
	//
	// The previous version skipped building hits hoping to fall through to the terrain beneath,
	// which never worked: a line trace STOPS at its first blocking hit, so there was nothing
	// behind the building to find and the probe simply reported no ground at all. Over the airport
	// that meant the lift never engaged.
	//
	// The one hit still rejected is a non-terrain surface ABOVE the camera - a roof or a bridge
	// deck overhead is not something we are landing on, and treating it as ground would heave the
	// whole view up over it. Terrain above the camera is still accepted, because that means the
	// camera has sunk into the landscape and the hard lift has to push it back out.
	auto TryGetMinimumSafeZ = [this, &QueryParams](const FVector& Location, float& OutMinimumSafeZ)
	{
		const FVector TraceStart = Location + FVector::UpVector * CameraGroundProbeUpCm;
		const FVector TraceEnd = Location - FVector::UpVector * CameraGroundProbeDownCm;
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Camera, QueryParams) ||
			!Hit.bBlockingHit)
		{
			return false;
		}

		if (Hit.ImpactPoint.Z > Location.Z)
		{
			const ASimCity2000CityActor* CityActor = Cast<ASimCity2000CityActor>(Hit.GetActor());
			if (CityActor != nullptr &&
				CityActor->IsBuildingCollisionHit(Hit.GetComponent(), Hit.ImpactPoint))
			{
				return false;
			}
		}

		OutMinimumSafeZ = Hit.ImpactPoint.Z + CameraGroundClearanceCm;
		return true;
	};

	float MinCameraZ = 0.0f;
	if (!TryGetMinimumSafeZ(DesiredCameraLocation, MinCameraZ))
	{
		bLastCameraGroundProbeHit = false;
		return 0.0f;
	}

	const float DistanceAboveGroundCm = DesiredCameraLocation.Z - MinCameraZ;
	bLastCameraGroundProbeHit = true;
	LastCameraGroundProbeDistanceCm = DistanceAboveGroundCm;
	const float RequiredLiftCm = FMath::Max(0.0f, -DistanceAboveGroundCm);

	// Plateau ramp: full lift from FullDistance down, easing off to nothing at ProbeRange. The
	// ramp this replaces was a straight `1 - dist / range`, which only reached full strength at
	// zero clearance - and the camera never gets there, because it stops a clearance above the
	// surface. A landed helicopter therefore received a small fraction of the lift and stayed
	// pinned near the top of the frame.
	const float FullDistanceCm = FMath::Max(0.0f, CameraGroundLiftFullDistanceCm);
	const float RangeCm = FMath::Max(FullDistanceCm + 1.0f, CameraGroundLiftProbeRangeCm);
	const float ProximityAlpha = FMath::Clamp(
		(RangeCm - DistanceAboveGroundCm) / (RangeCm - FullDistanceCm),
		0.0f,
		1.0f);
	return FMath::Max(RequiredLiftCm, CameraGroundLiftHeightCm * ProximityAlpha);
}

float ASimCopterHelicopterPawn::ResolveCameraPullInAlpha(
	const FVector& PathStart,
	const FVector& DesiredCameraLocation) const
{
	if (GetWorld() == nullptr || CameraBoom == nullptr)
	{
		return 1.0f;
	}

	const float PathLength = FVector::Distance(PathStart, DesiredCameraLocation);
	if (PathLength <= UE_SMALL_NUMBER)
	{
		return 1.0f;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterCameraPullInProbe), false, this);
	FHitResult Hit;
	const float ProbeRadius = FMath::Max(1.0f, CameraBoom->ProbeSize);
	float HitDistanceFromPathStart = 0.0f;
	if (GetWorld()->SweepSingleByChannel(
			Hit,
			PathStart,
			DesiredCameraLocation,
			FQuat::Identity,
			ECC_Camera,
			FCollisionShape::MakeSphere(ProbeRadius),
			QueryParams) &&
		Hit.bBlockingHit)
	{
		HitDistanceFromPathStart = Hit.Distance;
	}
	else
	{
		// Start retracting before contact so the pull-in itself can be interpolated smoothly.
		// As with angle avoidance, apply the large padding only over the outer half of the
		// path so a landed helicopter does not overlap the padded sphere at its roof anchor.
		const FVector PaddedStart =
			FMath::Lerp(PathStart, DesiredCameraLocation, 0.5f);
		if (!GetWorld()->SweepSingleByChannel(
				Hit,
				PaddedStart,
				DesiredCameraLocation,
				FQuat::Identity,
				ECC_Camera,
				FCollisionShape::MakeSphere(
					ProbeRadius + FMath::Max(0.0f, CameraObstructionPaddingCm)),
				QueryParams) ||
			!Hit.bBlockingHit)
		{
			return 1.0f;
		}
		HitDistanceFromPathStart = PathLength * 0.5f + Hit.Distance;
	}

	// The sweep shape already represents the camera radius. Preserve only a tiny numerical
	// gap, then express the safe point as a fraction of the exact roof-to-camera segment.
	const float SafeDistance = FMath::Max(
		CameraMinObstructedDistanceCm,
		HitDistanceFromPathStart - 2.0f);
	return FMath::Clamp(SafeDistance / PathLength, 0.0f, 1.0f);
}

bool ASimCopterHelicopterPawn::IsCameraPathClear(
	const FVector& BoomOrigin,
	float ArmLength,
	const FRotator& WorldRotation,
	float ExtraPaddingCm) const
{
	if (GetWorld() == nullptr || CameraBoom == nullptr || ArmLength <= UE_SMALL_NUMBER)
	{
		return true;
	}

	const FVector DesiredCameraLocation = BoomOrigin - WorldRotation.Vector() * ArmLength;
	const FVector PathStart =
		CameraAnchor != nullptr ? CameraAnchor->GetComponentLocation() : BoomOrigin;
	const float ProbeRadius = FMath::Max(1.0f, CameraBoom->ProbeSize);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimCopterCameraAvoidanceProbe), false, this);
	FHitResult Hit;
	if (GetWorld()->SweepSingleByChannel(
			Hit,
			PathStart,
			DesiredCameraLocation,
			FQuat::Identity,
			ECC_Camera,
			FCollisionShape::MakeSphere(ProbeRadius),
			QueryParams))
	{
		return false;
	}

	const float Padding = FMath::Max(0.0f, ExtraPaddingCm);
	if (Padding <= UE_SMALL_NUMBER)
	{
		return true;
	}

	// Ramp the anticipatory padding in over the outer half of the roof-to-camera path so the
	// larger probe protects the camera without rejecting every angle near the helicopter.
	const FVector PaddedStart = FMath::Lerp(PathStart, DesiredCameraLocation, 0.5);
	return !GetWorld()->SweepSingleByChannel(
		Hit,
		PaddedStart,
		DesiredCameraLocation,
		FQuat::Identity,
		ECC_Camera,
		FCollisionShape::MakeSphere(ProbeRadius + Padding),
		QueryParams);
}

FRotator ASimCopterHelicopterPawn::FindCameraAvoidanceOffset(
	const FVector& BoomOrigin,
	float ArmLength,
	const FRotator& DesiredWorldRotation) const
{
	if (IsCameraPathClear(
			BoomOrigin,
			ArmLength,
			DesiredWorldRotation,
			CameraObstructionPaddingCm))
	{
		return FRotator::ZeroRotator;
	}

	// Derive which pitch direction raises the actual camera endpoint instead of relying on
	// rotator sign conventions. Every pitch-bearing candidate below uses that direction, so
	// terrain avoidance can never choose a route that lowers the camera.
	constexpr float PitchProbeAngleDeg = 1.0f;
	FRotator MinusPitchProbe = DesiredWorldRotation;
	MinusPitchProbe.Pitch = FMath::Clamp(
		DesiredWorldRotation.Pitch - PitchProbeAngleDeg,
		-88.0,
		60.0);
	FRotator PlusPitchProbe = DesiredWorldRotation;
	PlusPitchProbe.Pitch = FMath::Clamp(
		DesiredWorldRotation.Pitch + PitchProbeAngleDeg,
		-88.0,
		60.0);
	const double MinusPitchCameraZ =
		(BoomOrigin - MinusPitchProbe.Vector() * ArmLength).Z;
	const double PlusPitchCameraZ =
		(BoomOrigin - PlusPitchProbe.Vector() * ArmLength).Z;
	const double UpPitchSign =
		PlusPitchCameraZ > MinusPitchCameraZ ? 1.0 : -1.0;

	// Search only along the upward pitch arc. Sideways avoidance makes the helicopter slide
	// unpredictably across the frame and is much harder to read than a vertical correction.
	const FVector2D Directions[] = {
		FVector2D(UpPitchSign, 0.0)
	};

	const float MaxAngle = FMath::Clamp(CameraAvoidanceMaxAngleDeg, 0.0f, 85.0f);
	const float Step = FMath::Clamp(CameraAvoidanceSearchStepDeg, 1.0f, 15.0f);
	auto MakeCandidate = [&DesiredWorldRotation](
		const FVector2D& Direction,
		float Angle)
	{
		FRotator Candidate = DesiredWorldRotation;
		Candidate.Pitch = FMath::Clamp(
			DesiredWorldRotation.Pitch + Direction.X * Angle,
			-88.0,
			60.0);
		Candidate.Yaw = FRotator::NormalizeAxis(
			DesiredWorldRotation.Yaw + Direction.Y * Angle);
		return Candidate;
	};
	auto MakeOffset = [&DesiredWorldRotation](const FRotator& Candidate)
	{
		return FRotator(
			FRotator::NormalizeAxis(Candidate.Pitch - DesiredWorldRotation.Pitch),
			FRotator::NormalizeAxis(Candidate.Yaw - DesiredWorldRotation.Yaw),
			0.0);
	};

	for (float Angle = Step; Angle <= MaxAngle + UE_SMALL_NUMBER; Angle += Step)
	{
		for (const FVector2D& Direction : Directions)
		{
			const FRotator Candidate = MakeCandidate(Direction, Angle);
			if (!IsCameraPathClear(
					BoomOrigin,
					ArmLength,
					Candidate,
					CameraObstructionPaddingCm))
			{
				continue;
			}

			// Refine the winning direction within this ring without changing which side was
			// selected. Four iterations put a 5-degree search step well below half a degree.
			float Low = FMath::Max(0.0f, Angle - Step);
			float High = Angle;
			for (int32 Iteration = 0; Iteration < 4; ++Iteration)
			{
				const float Mid = (Low + High) * 0.5f;
				if (IsCameraPathClear(
						BoomOrigin,
						ArmLength,
						MakeCandidate(Direction, Mid),
						CameraObstructionPaddingCm))
				{
					High = Mid;
				}
				else
				{
					Low = Mid;
				}
			}
			return MakeOffset(MakeCandidate(Direction, High));
		}
	}

	// If every sampled route is obstructed, aim as high as the view limits allow. The hard
	// arm sweep still prevents penetration while this is lerped in.
	return MakeOffset(MakeCandidate(FVector2D(UpPitchSign, 0.0), MaxAngle));
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
	// Same rule as the city actor: the authored path is a developer convenience, so it only wins
	// while it still points at a real install.
	const FString ConfiguredPath = OriginalGameRoot.Path.TrimStartAndEnd();
	if (!ConfiguredPath.IsEmpty())
	{
		const FString Absolute = FPaths::IsRelative(ConfiguredPath)
			? FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), ConfiguredPath))
			: FPaths::ConvertRelativePathToFull(ConfiguredPath);
		if (SimCopterOriginalGame::IsOriginalGameRoot(Absolute))
		{
			return Absolute;
		}
	}

	return SimCopterOriginalGame::ResolveRoot();
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

