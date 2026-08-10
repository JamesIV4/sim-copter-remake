// Copyright Epic Games, Inc. All Rights Reserved.

#include "City/SimCopterSmokeStacks.h"

#include "City/SimCopterDayNight.h"
#include "City/SimCopterEffectExposure.h"
#include "Components/PointLightComponent.h"
#include "Formats/MaxisMeshReader.h"
#include "GameFramework/PlayerController.h"
#include "Game/SimCopterLowPowerMode.h"
#include "Ground/SimCopterEffectFX.h"
#include "Ground/SimCopterEffectRasterizer.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

USimCopterSmokeStacksComponent::USimCopterSmokeStacksComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

int32 USimCopterSmokeStacksComponent::ExtractSmokeMarkers(
	const FMaxisMeshObject& Object,
	const float ModelUnitsPerCentimeter,
	const float ModelScale,
	const bool bApplyCityMeshOrientation,
	TArray<FSmokeMarker>& OutMarkers)
{
	const int32 FirstIndex = OutMarkers.Num();
	for (const FMaxisMeshFace& Face : Object.Faces)
	{
		if (Face.FaceType != EffectMarkerFaceType || Face.VertexIndices.Num() < 1)
		{
			continue;
		}
		const int32 VertexIndex = Face.VertexIndices[0];
		if (!Object.Vertices.IsValidIndex(VertexIndex))
		{
			continue;
		}

		FSmokeMarker Marker;
		Marker.LocalOffset = FMaxisMeshReader::ConvertMaxisVertexToUnreal(
			Object.Vertices[VertexIndex], ModelUnitsPerCentimeter) * ModelScale;
		if (bApplyCityMeshOrientation)
		{
			Marker.LocalOffset = SimCopterEffectFX::ApplyCityMeshOrientation(Marker.LocalOffset);
		}
		Marker.EffectClass = Face.MaterialIndex;
		OutMarkers.Add(Marker);
	}

	// The trail runs up the stack, so the last puff is the top of the plume - that is where the one
	// light for this chimney belongs.
	int32 TopIndex = INDEX_NONE;
	float TopZ = -TNumericLimits<float>::Max();
	for (int32 Index = FirstIndex; Index < OutMarkers.Num(); ++Index)
	{
		if (OutMarkers[Index].LocalOffset.Z > TopZ)
		{
			TopZ = OutMarkers[Index].LocalOffset.Z;
			TopIndex = Index;
		}
	}
	if (TopIndex != INDEX_NONE)
	{
		OutMarkers[TopIndex].bPointLightAnchor = true;
	}

	return OutMarkers.Num() - FirstIndex;
}

bool USimCopterSmokeStacksComponent::InitSmokeAssets(
	const TArray<FColor>& Palette,
	UMaterialInterface* InCardMaterial,
	FString& OutError)
{
	if (bAssetsReady)
	{
		return true;
	}
	if (Palette.Num() < 256)
	{
		OutError = TEXT("Shared SIM3D palette is missing or incomplete.");
		return false;
	}

	SharedPalette = Palette;
	CardMaterial = InCardMaterial;
	SelectorAtlas = FSimCopterEffectRasterizer::CreateSelectorAtlas(this, SharedPalette);
	if (SelectorAtlas == nullptr)
	{
		OutError = TEXT("Could not build the original effect selector atlas.");
		return false;
	}
	if (CardMaterial != nullptr)
	{
		CardMaterialInstance = UMaterialInstanceDynamic::Create(CardMaterial, this);
		if (CardMaterialInstance != nullptr)
		{
			CardMaterialInstance->SetTextureParameterValue(TEXT("Texture"), SelectorAtlas);
		}
	}

	bAssetsReady = true;
	return true;
}

void USimCopterSmokeStacksComponent::SetSmokeMarkers(TArray<FSmokeMarker> InMarkers)
{
	Markers = MoveTemp(InMarkers);
	RebuildPointLights();
}

void USimCopterSmokeStacksComponent::ClearSmokeMarkers()
{
	Markers.Reset();
	RebuildPointLights();
	if (MeshComponent != nullptr)
	{
		MeshComponent->ClearAllMeshSections();
	}
}

void USimCopterSmokeStacksComponent::RebuildPointLights()
{
	for (UPointLightComponent* Light : PointLights)
	{
		if (Light != nullptr)
		{
			Light->DestroyComponent();
		}
	}
	PointLights.Reset();
	AppliedLightScale = -1.0f;

	if (!bCastPointLights)
	{
		return;
	}

	for (const FSmokeMarker& Marker : Markers)
	{
		if (!Marker.bPointLightAnchor)
		{
			continue;
		}
		UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
		if (Light == nullptr)
		{
			continue;
		}
		Light->SetupAttachment(this);
		Light->SetRelativeLocation(Marker.LocalOffset);

		FLinearColor LightColor(PointLightColor);
		if (bUsePaletteLightColor && SharedPalette.Num() >= 256)
		{
			// Average the effect class's own eight selector entries. That is the colour the plume
			// is actually drawn in, so the light it throws matches it instead of being guessed.
			FLinearColor Accumulated = FLinearColor::Black;
			for (int32 Phase = 0; Phase < FSimCopterEffectRasterizer::SelectorPhaseCount; ++Phase)
			{
				const uint8 PaletteIndex =
					FSimCopterEffectRasterizer::GetSelectorPaletteIndex(Marker.EffectClass, Phase);
				Accumulated += FLinearColor(SharedPalette[PaletteIndex]);
			}
			LightColor = Accumulated / static_cast<float>(FSimCopterEffectRasterizer::SelectorPhaseCount);
		}
		Light->SetLightColor(LightColor);
		Light->SetAttenuationRadius(FMath::Max(100.0f, PointLightAttenuationRadiusCm));
		Light->SetIntensity(0.0f);
		// Exposure independent, like the beacons and the headlights - see
		// Docs/memory/simcopter-exposure-scale.md.
		Light->SetInverseExposureBlend(1.0f);
		Light->SetCastShadows(bPointLightsCastShadows);
		Light->SetVisibility(false);
		Light->RegisterComponent();
		PointLights.Add(Light);
	}

	RefreshPointLightIntensity();
}

void USimCopterSmokeStacksComponent::RefreshPointLightIntensity()
{
	if (PointLights.IsEmpty())
	{
		return;
	}

	float Scale = 0.0f;
	if (bEnabled && bCastPointLights && !SimCopterLowPower::IsEnabled())
	{
		float NightAlpha = 1.0f;
		if (const USimCopterDayNightSubsystem* DayNight = USimCopterDayNightSubsystem::Get(this))
		{
			NightAlpha = DayNight->GetNightAlpha();
		}
		Scale = FMath::Lerp(
			FMath::Clamp(DaytimeIntensityScale, 0.0f, 1.0f),
			1.0f,
			FMath::Clamp(NightAlpha, 0.0f, 1.0f));
	}

	if (FMath::IsNearlyEqual(Scale, AppliedLightScale, 1.0f / 512.0f))
	{
		return;
	}
	AppliedLightScale = Scale;

	const bool bLightsVisible = Scale > 0.0f;
	const float LightIntensity = FMath::Max(0.0f, PointLightIntensity) * Scale;
	for (UPointLightComponent* Light : PointLights)
	{
		if (Light == nullptr)
		{
			continue;
		}
		Light->SetVisibility(bLightsVisible);
		if (bLightsVisible)
		{
			Light->SetIntensity(LightIntensity);
		}
	}
}

void USimCopterSmokeStacksComponent::SyncSmokeFromPlayerCamera(const float TimeSeconds)
{
	FVector CameraLocation = GetComponentLocation();
	if (const APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (const APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
		{
			CameraLocation = CameraManager->GetCameraLocation();
		}
	}
	SyncSmoke(TimeSeconds, CameraLocation);
}

void USimCopterSmokeStacksComponent::SyncSmoke(const float TimeSeconds, const FVector& CameraLocation)
{
	RefreshPointLightIntensity();

	if (!bAssetsReady)
	{
		return;
	}

	if (MeshComponent == nullptr)
	{
		MeshComponent = NewObject<UProceduralMeshComponent>(this, TEXT("SmokeCards"));
		MeshComponent->SetupAttachment(this);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComponent->SetCanEverAffectNavigation(false);
		MeshComponent->bUseAsyncCooking = false;
		MeshComponent->SetCastShadow(false);
		MeshComponent->RegisterComponent();
	}

	if (Markers.IsEmpty() || !bEnabled)
	{
		if (MeshComponent->GetNumSections() > 0)
		{
			MeshComponent->ClearAllMeshSections();
		}
		return;
	}

	// Same construction as USimCopterFireRenderComponent::SyncFlames - the two draw the same
	// face-type-26 markers through the same FUN_00496da0 kernel, one against a cloned FIREPTS
	// template and one against the city's static chimneys.
	const FTransform LocalToWorld = GetComponentTransform();
	const FTransform WorldToLocal = LocalToWorld.Inverse();

	FRotator CameraRotation = (GetComponentLocation() - CameraLocation).Rotation();
	if (const APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (const APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
		{
			CameraRotation = CameraManager->GetCameraRotation();
		}
	}
	const FVector CameraForward = CameraRotation.Vector();
	const FVector CameraRight = FRotationMatrix(CameraRotation).GetScaledAxis(EAxis::Y);
	const FVector CameraUp = FRotationMatrix(CameraRotation).GetScaledAxis(EAxis::Z);
	const int32 RasterFrame = FMath::FloorToInt(TimeSeconds * 20.0f);

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	for (int32 MarkerIndex = 0; MarkerIndex < Markers.Num(); ++MarkerIndex)
	{
		const FSmokeMarker& Marker = Markers[MarkerIndex];
		const FVector Center = LocalToWorld.TransformPosition(Marker.LocalOffset);
		const float CameraDepth = FVector::DotProduct(Center - CameraLocation, CameraForward);
		if (CameraDepth <= 1.0f)
		{
			continue;
		}

		const int32 DepthScale = FSimCopterEffectRasterizer::ComputeDepthScale1616(CameraDepth);
		const FSimCopterEffectKernelMetrics Metrics =
			FSimCopterEffectRasterizer::ComputeKernelMetrics(Marker.EffectClass, DepthScale);
		if (Metrics.Iterations <= 0)
		{
			// Past FarDepth1616 the original's kernel collapses to nothing, which is the distance
			// cull the smoke gets - the same one the fire gets.
			continue;
		}

		const float PixelSizeCm = FSimCopterEffectRasterizer::GetWorldSizePerViewportPixel(CameraDepth);
		uint32 RandomState =
			static_cast<uint32>(MarkerIndex) * 0x9e3779b9u ^
			static_cast<uint32>(RasterFrame) * 0xc2b2ae35u;
		// FUN_00496da0 consumes rand()%7 for a background-remap row before entering the 0x10
		// renderer; the row is unused by the selector writes but the sample keeps the ordering.
		FSimCopterEffectRasterizer::AdvanceRandom(RandomState);

		for (int32 Iteration = 0; Iteration < Metrics.Iterations; ++Iteration)
		{
			const int32 JitterX =
				static_cast<int32>(FSimCopterEffectRasterizer::AdvanceRandom(RandomState) %
					static_cast<uint32>(Metrics.JitterSpanPixels)) -
				Metrics.JitterHalfExtentPixels;
			const int32 JitterY =
				static_cast<int32>(FSimCopterEffectRasterizer::AdvanceRandom(RandomState) %
					static_cast<uint32>(Metrics.JitterSpanPixels)) -
				Metrics.JitterHalfExtentPixels;
			const int32 Radius =
				Metrics.MinRadius +
				static_cast<int32>(FSimCopterEffectRasterizer::AdvanceRandom(RandomState) %
					static_cast<uint32>(Metrics.RadiusChoiceCount));
			const FSimCopterEffectStencilMetrics Stencil =
				FSimCopterEffectRasterizer::GetStencilMetrics(Radius);
			const float RasterWidth = static_cast<float>(Stencil.Width);
			const float RasterHeight = static_cast<float>(Stencil.Height);
			const FVector KernelCenter =
				Center +
				CameraRight * ((static_cast<float>(JitterX) + (RasterWidth - 1.0f) * 0.5f) * PixelSizeCm) -
				CameraUp * ((static_cast<float>(JitterY) + (RasterHeight - 1.0f) * 0.5f) * PixelSizeCm);
			const float HalfWidth = RasterWidth * PixelSizeCm * 0.5f;
			const float HalfHeight = RasterHeight * PixelSizeCm * 0.5f;

			const int32 Base = Vertices.Num();
			Vertices.Add(WorldToLocal.TransformPosition(
				KernelCenter - CameraRight * HalfWidth - CameraUp * HalfHeight));
			Vertices.Add(WorldToLocal.TransformPosition(
				KernelCenter + CameraRight * HalfWidth - CameraUp * HalfHeight));
			Vertices.Add(WorldToLocal.TransformPosition(
				KernelCenter + CameraRight * HalfWidth + CameraUp * HalfHeight));
			Vertices.Add(WorldToLocal.TransformPosition(
				KernelCenter - CameraRight * HalfWidth + CameraUp * HalfHeight));

			const int32 SelectorPhase = FSimCopterEffectRasterizer::ConsumeSelectorPhase(Radius);
			const FVector2D UV0 = FSimCopterEffectRasterizer::GetAtlasUV(
				Marker.EffectClass, SelectorPhase, Radius, 0, 0);
			const FVector2D UV1 = FSimCopterEffectRasterizer::GetAtlasUV(
				Marker.EffectClass, SelectorPhase, Radius, Stencil.Width, Stencil.Height);
			UVs.Append({
				FVector2D(UV0.X, UV1.Y),
				UV1,
				FVector2D(UV1.X, UV0.Y),
				UV0
			});

			const FVector LocalNormal = WorldToLocal.TransformVectorNoScale(-CameraForward);
			const FProcMeshTangent Tangent(WorldToLocal.TransformVectorNoScale(CameraRight), false);
			for (int32 Vertex = 0; Vertex < 4; ++Vertex)
			{
				Normals.Add(LocalNormal);
				Colors.Add(FLinearColor::White);
				Tangents.Add(Tangent);
			}
			Triangles.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
		}
	}

	if (Vertices.IsEmpty())
	{
		MeshComponent->ClearAllMeshSections();
		return;
	}

	MeshComponent->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
	if (CardMaterialInstance != nullptr)
	{
		// Smoke is a SURFACE, not a light source: it has to go dark with the sun rather than hold a
		// minimum emissive. M_SimCopterSpriteTexture's baked 26000-nit default would otherwise have
		// every chimney glowing at midnight - the trap the exposure note calls out.
		USimCopterEffectExposureSubsystem::ApplyEmissiveNits(
			CardMaterialInstance, GetWorld(), /*bIsLightSource=*/false);
		MeshComponent->SetMaterial(0, CardMaterialInstance);
	}
}
