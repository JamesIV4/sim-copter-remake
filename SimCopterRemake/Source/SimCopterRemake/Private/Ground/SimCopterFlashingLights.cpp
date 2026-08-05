// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterFlashingLights.h"

#include "City/SimCopterEffectExposure.h"
#include "Components/PointLightComponent.h"
#include "Formats/MaxisMeshReader.h"
#include "Game/SimCopterLowPowerMode.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Ground/SimCopterEffectFX.h"
#include "Ground/SimCopterEffectRasterizer.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

float FSimCopterFlashingLightSchedule::GetLightWorldSizeCm()
{
	// The original's 4-pixel block, put through its own perspective divide at the reference depth.
	return LightSizeViewportPixels *
		FSimCopterEffectRasterizer::GetWorldSizePerViewportPixel(LightSizeReferenceDepthCm);
}

float FSimCopterFlashingLightSchedule::GetWorldSizeForScreenPixels(
	const float CameraDepthCm,
	const float ViewportWidthPixels,
	const float HorizontalFovDegrees,
	const float ScreenPixels)
{
	const float Width = FMath::Max(ViewportWidthPixels, 1.0f);
	const float HalfFovRadians = FMath::DegreesToRadians(
		FMath::Clamp(HorizontalFovDegrees, 1.0f, 179.0f) * 0.5f);
	return 2.0f * FMath::Max(CameraDepthCm, 0.0f) * FMath::Tan(HalfFovRadians) *
		FMath::Max(ScreenPixels, 0.0f) / Width;
}

int32 FSimCopterFlashingLightSchedule::GetPhaseForPaletteIndex(uint8 PaletteIndex)
{
	// The switch in FUN_00496c00, in its own order.
	switch (PaletteIndex)
	{
	case WhitePaletteIndex: return 0;
	case RedPaletteIndex: return 1;
	case GreenPaletteIndex: return 2;
	case YellowPaletteIndex: return 3;
	case BluePaletteIndex: return 4;
	default: return UnlistedColorPhase;
	}
}

int32 FSimCopterFlashingLightSchedule::GetPhaseAtTime(double GameTimeSeconds)
{
	// DAT_005039c8 is unsigned and only ever incremented, so negative time has no original
	// counterpart; clamp rather than let the modulo go negative.
	const double Ticks = FMath::Max(GameTimeSeconds, 0.0) / PhaseSeconds;
	return static_cast<int32>(static_cast<uint64>(Ticks) % static_cast<uint64>(PhaseCount));
}

int32 FSimCopterFlashingLightSchedule::ExtractLightPoints(
	const FMaxisMeshObject& Object,
	const TArray<FColor>* Palette,
	float ModelUnitsPerCentimeter,
	float ModelScale,
	bool bApplyCityMeshOrientation,
	TArray<FSimCopterFlashingLightPoint>& OutPoints)
{
	int32 AddedCount = 0;
	for (const FMaxisMeshFace& Face : Object.Faces)
	{
		if (Face.FaceType != LightFaceType || Face.VertexIndices.Num() < 1)
		{
			continue;
		}

		const int32 VertexIndex = Face.VertexIndices[0];
		if (!Object.Vertices.IsValidIndex(VertexIndex))
		{
			continue;
		}

		FSimCopterFlashingLightPoint Point;
		const FVector Converted = FMaxisMeshReader::ConvertMaxisVertexToUnreal(
			Object.Vertices[VertexIndex],
			ModelUnitsPerCentimeter) * ModelScale;
		Point.LocalOffset = bApplyCityMeshOrientation
			? SimCopterEffectFX::ApplyCityMeshOrientation(Converted)
			: Converted;
		Point.PaletteIndex = Face.MaterialIndex;
		// FLinearColor(FColor) sRGB-decodes, so the unlit card shows the colour the 8-bit
		// framebuffer showed - the same treatment the other palette-coloured effect cards get.
		Point.Color = (Palette != nullptr && Palette->IsValidIndex(Face.MaterialIndex))
			? FLinearColor((*Palette)[Face.MaterialIndex])
			: FLinearColor::White;
		OutPoints.Add(Point);
		++AddedCount;
	}

	return AddedCount;
}

USimCopterFlashingLightsComponent::USimCopterFlashingLightsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Same flat palette-coloured card material the other original effects use.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CardMaterialFinder(
		TEXT("/Game/Materials/M_SimCopterParticleFX.M_SimCopterParticleFX"));
	if (CardMaterialFinder.Succeeded())
	{
		CardMaterial = CardMaterialFinder.Object;
	}
}

void USimCopterFlashingLightsComponent::BeginPlay()
{
	Super::BeginPlay();

	// Each component loads the persisted scale itself. The alternative - having the pawn push it
	// into the city's component - loses the value whenever the city actor is not up yet at the
	// moment the pawn starts, which is most of the time.
	if (GConfig != nullptr && !GGameUserSettingsIni.IsEmpty())
	{
		double Value = 0.0;
		if (GConfig->GetDouble(GetConfigSection(), GetIntensityScaleConfigKey(), Value, GGameUserSettingsIni))
		{
			PointLightIntensityScale = FMath::Max(static_cast<float>(Value), 0.0f);
		}
	}
}

void USimCopterFlashingLightsComponent::SaveIntensityScaleToConfig(const float Scale)
{
	if (GConfig == nullptr || GGameUserSettingsIni.IsEmpty())
	{
		return;
	}

	GConfig->SetDouble(
		GetConfigSection(),
		GetIntensityScaleConfigKey(),
		FMath::Max(Scale, 0.0f),
		GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void USimCopterFlashingLightsComponent::SetLightPoints(TArray<FSimCopterFlashingLightPoint> InPoints)
{
	LightPoints = MoveTemp(InPoints);
	LastPhase = INDEX_NONE;
	bHasDrawnOnce = false;
}

void USimCopterFlashingLightsComponent::AppendLightPoints(const TArray<FSimCopterFlashingLightPoint>& InPoints)
{
	LightPoints.Append(InPoints);
	LastPhase = INDEX_NONE;
	bHasDrawnOnce = false;
}

void USimCopterFlashingLightsComponent::ClearLightPoints()
{
	LightPoints.Reset();
	LastPhase = INDEX_NONE;
	bHasDrawnOnce = false;
	if (MeshComponent != nullptr)
	{
		MeshComponent->ClearAllMeshSections();
	}
	ReleasePointLights(0);
}

void USimCopterFlashingLightsComponent::SyncLightsFromPlayerCamera(double GameTimeSeconds)
{
	const APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	const APlayerCameraManager* CameraManager = PC != nullptr ? PC->PlayerCameraManager : nullptr;
	if (CameraManager == nullptr)
	{
		return;
	}
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	PC->GetViewportSize(ViewportWidth, ViewportHeight);
	ActiveViewportWidthPixels = FMath::Max(float(ViewportWidth), 1.0f);
	ActiveHorizontalFovDegrees = CameraManager->GetFOVAngle();

	SyncLights(GameTimeSeconds, CameraManager->GetCameraLocation(), CameraManager->GetCameraRotation());
}

void USimCopterFlashingLightsComponent::SyncLights(
	double GameTimeSeconds,
	const FVector& CameraLocation,
	const FRotator& CameraRotation)
{
	if (LightPoints.IsEmpty())
	{
		return;
	}

	const int32 Phase = FSimCopterFlashingLightSchedule::GetPhaseAtTime(GameTimeSeconds);

	// The cards are camera-facing, so they need rebuilding when the view or the owner moves as
	// well as when the phase flips. The thresholds are loose - a light is a few pixels across.
	const FTransform ComponentTransform = GetComponentTransform();
	const bool bViewMoved =
		!CameraLocation.Equals(LastCameraLocation, 25.0f) ||
		!CameraRotation.Equals(LastCameraRotation, 0.5f) ||
		!ComponentTransform.Equals(LastComponentTransform, 1.0f);
	if (bHasDrawnOnce && Phase == LastPhase && !bViewMoved)
	{
		return;
	}

	LastPhase = Phase;
	LastCameraLocation = CameraLocation;
	LastCameraRotation = CameraRotation;
	LastComponentTransform = ComponentTransform;
	bHasDrawnOnce = true;

	RebuildCards(Phase, CameraLocation, CameraRotation);
}

void USimCopterFlashingLightsComponent::RebuildCards(
	int32 Phase,
	const FVector& CameraLocation,
	const FRotator& CameraRotation)
{
	if (MeshComponent == nullptr)
	{
		MeshComponent = NewObject<UProceduralMeshComponent>(this, TEXT("FlashingLightCards"));
		MeshComponent->SetupAttachment(this);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComponent->SetCanEverAffectNavigation(false);
		MeshComponent->bUseAsyncCooking = false;
		MeshComponent->SetCastShadow(false);
		MeshComponent->RegisterComponent();
	}

	const FTransform LocalToWorld = GetComponentTransform();
	const FTransform WorldToLocal = LocalToWorld.Inverse();
	const FVector CameraForward = CameraRotation.Vector();
	const FVector CameraRight = FRotationMatrix(CameraRotation).GetScaledAxis(EAxis::Y);
	const FVector CameraUp = FRotationMatrix(CameraRotation).GetScaledAxis(EAxis::Z);

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	TArray<FLitLight> LitLights;

	// Fixed world size, so perspective shrinks the card with distance instead of the original's
	// fixed-pixel stamp blowing it up. See the schedule header for why this diverges.
	const float CardWorldSize = FSimCopterFlashingLightSchedule::GetLightWorldSizeCm() *
		FMath::Max(CardSizeScale, KINDA_SMALL_NUMBER);

	for (const FSimCopterFlashingLightPoint& Point : LightPoints)
	{
		if (!FSimCopterFlashingLightSchedule::IsLitAtPhase(Point.PaletteIndex, Phase))
		{
			continue;
		}

		const FVector Center = LocalToWorld.TransformPosition(Point.LocalOffset);
		const float CameraDepth = FVector::DotProduct(Center - CameraLocation, CameraForward);
		if (CameraDepth <= 1.0f)
		{
			continue;
		}

		FLitLight& Lit = LitLights.AddDefaulted_GetRef();
		Lit.World = Center;
		Lit.Color = Point.Color;
		Lit.CameraDepthCm = CameraDepth;
		Lit.CameraDistanceSquared = FVector::DistSquared(Center, CameraLocation);

		// Never let the card fall below one physical output pixel, or a distant skyline stops
		// twinkling. Use the live camera projection rather than the original 560x400 raster unit.
		const float MinWorldSize =
			FSimCopterFlashingLightSchedule::GetWorldSizeForScreenPixels(
				CameraDepth,
				ActiveViewportWidthPixels,
				ActiveHorizontalFovDegrees,
				FSimCopterFlashingLightSchedule::MinLightSizeViewportPixels);
		const float HalfSize = FMath::Max(CardWorldSize, MinWorldSize) * 0.5f;

		const int32 Base = Vertices.Num();
		Vertices.Add(WorldToLocal.TransformPosition(Center - CameraRight * HalfSize - CameraUp * HalfSize));
		Vertices.Add(WorldToLocal.TransformPosition(Center + CameraRight * HalfSize - CameraUp * HalfSize));
		Vertices.Add(WorldToLocal.TransformPosition(Center + CameraRight * HalfSize + CameraUp * HalfSize));
		Vertices.Add(WorldToLocal.TransformPosition(Center - CameraRight * HalfSize + CameraUp * HalfSize));

		UVs.Append({ FVector2D(0.0f, 1.0f), FVector2D(1.0f, 1.0f), FVector2D(1.0f, 0.0f), FVector2D(0.0f, 0.0f) });

		const FVector LocalNormal = WorldToLocal.TransformVectorNoScale(-CameraForward);
		const FProcMeshTangent Tangent(WorldToLocal.TransformVectorNoScale(CameraRight), false);
		for (int32 Vertex = 0; Vertex < 4; ++Vertex)
		{
			Normals.Add(LocalNormal);
			Colors.Add(Point.Color);
			Tangents.Add(Tangent);
		}

		Triangles.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
	}

	UpdatePointLights(LitLights);

	if (Vertices.IsEmpty())
	{
		// Phases 6 and 7 light nothing, and so does any phase whose colour this model lacks.
		MeshComponent->ClearAllMeshSections();
		return;
	}

	MeshComponent->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
	// The card is unlit, so its colour is an absolute number of nits and has to be put on the same
	// scale as the sun or the marker draws as a black square. See USimCopterEffectExposureSubsystem.
	if (CardMaterialInstance == nullptr && CardMaterial != nullptr)
	{
		CardMaterialInstance = UMaterialInstanceDynamic::Create(CardMaterial, this);
	}
	if (CardMaterialInstance != nullptr)
	{
		CardMaterialInstance->SetScalarParameterValue(
			USimCopterEffectExposureSubsystem::GetEmissiveNitsParameterName(),
			USimCopterEffectExposureSubsystem::GetEffectEmissiveNitsForWorld(GetWorld()));
		MeshComponent->SetMaterial(0, CardMaterialInstance);
	}
	else if (CardMaterial != nullptr)
	{
		MeshComponent->SetMaterial(0, CardMaterial);
	}
}

void USimCopterFlashingLightsComponent::UpdatePointLights(const TArray<FLitLight>& LitLights)
{
	// A city puts hundreds of these on screen at once and the mode has taken MegaLights away, so in
	// low power the beacons go back to being what the original drew: a coloured card and nothing
	// else. The cards themselves stay - they are the gameplay-visible part, and they are free.
	//
	// No subscription needed: the cards rebuild on every phase step (~2 Hz) and this runs with them,
	// so a mid-session toggle releases the pool within a blink.
	if (!bCastPointLights || SimCopterLowPower::IsEnabled() || LitLights.IsEmpty())
	{
		ReleasePointLights(0);
		return;
	}

	// Nothing is ever culled for distance, and by default nothing is culled for budget either -
	// every lit marker in the phase gets a light. MegaLights makes that affordable.
	TArray<const FLitLight*> Selected;
	Selected.Reserve(LitLights.Num());
	for (const FLitLight& Lit : LitLights)
	{
		Selected.Add(&Lit);
	}

	// Only when a cap has been asked for (a no-MegaLights configuration) does it cost a sort, and
	// then the nearest markers win, because those are the ones whose spill lands on visible
	// geometry. Only the lit subset of one colour phase is ever in this array.
	if (MaxPointLights > 0 && Selected.Num() > MaxPointLights)
	{
		Selected.Sort([](const FLitLight& A, const FLitLight& B)
		{
			return A.CameraDistanceSquared < B.CameraDistanceSquared;
		});
		Selected.SetNum(MaxPointLights);
	}

	const int32 UsedCount = Selected.Num();
	if (PointLights.Num() < UsedCount)
	{
		PointLights.SetNum(UsedCount);
	}

	for (int32 Index = 0; Index < UsedCount; ++Index)
	{
		if (PointLights[Index] == nullptr)
		{
			UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
			// Movable: these follow a flying helicopter, and the phase rotation re-points them at
			// different markers every step.
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetupAttachment(this);
			// Explicit rather than relying on the default, because the whole no-cap policy above
			// rests on MegaLights being allowed to solve these.
			Light->bAllowMegaLights = true;
			Light->bUseInverseSquaredFalloff = false;
			Light->SetIntensityUnits(ELightUnits::Unitless);
			// Divide the exposure back out, so a marker keeps the same presence on screen at noon
			// and at midnight. The level's day sequence runs a physically scaled sun at 120,000 lux
			// where the day/night actor it replaced ran it at 4, and auto exposure follows the sun:
			// a beacon tuned against the old scale simply has no visible contribution left under
			// the new one. These are gameplay markers - the original stamped them straight into the
			// frame buffer with no lighting model at all - so constant screen presence IS the
			// authentic behaviour, and it does not need retuning the next time the sky changes.
			// The effect cards get the identical treatment in the material (EyeAdaptationInverse,
			// see CreateSimCopterMaterials.py).
			Light->SetInverseExposureBlend(1.0f);
			Light->RegisterComponent();
			PointLights[Index] = Light;
		}

		UPointLightComponent* Light = PointLights[Index];
		const FLitLight& Lit = *Selected[Index];
		Light->SetWorldLocation(Lit.World);
		// The marker's own palette colour, which is the whole point - a red beacon has to throw
		// red light. Intensity is flat: the original has no lighting model to take a falloff from.
		Light->SetLightColor(Lit.Color);
		Light->SetAttenuationRadius(PointLightAttenuationRadiusCm);
		Light->SetIntensity(GetEffectivePointLightIntensity());
		Light->SetCastShadows(bPointLightsCastShadows);
		Light->SetVisibility(true);
	}

	ReleasePointLights(UsedCount);
}

void USimCopterFlashingLightsComponent::ReleasePointLights(int32 FirstUnusedIndex)
{
	// Kept alive but hidden rather than destroyed: the pool churns every phase flip, and
	// re-registering components at 20 Hz is far more expensive than a visibility toggle.
	for (int32 Index = FirstUnusedIndex; Index < PointLights.Num(); ++Index)
	{
		if (PointLights[Index] != nullptr)
		{
			PointLights[Index]->SetVisibility(false);
		}
	}
}
