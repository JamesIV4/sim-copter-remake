// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterFireRenderComponent.h"
#include "City/SimCopterEffectExposure.h"
#include "Formats/MaxisMeshLibrary.h"
#include "Formats/MaxisMeshReader.h"
#include "GameFramework/PlayerController.h"
#include "Ground/SimCopterEffectFX.h"
#include "Ground/SimCopterEffectRasterizer.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

// FUN_004a47c0 clones FIREPTS into every flame slot. FIREPTS is a template made from face type
// 0x1a/light type 1 markers; its raw material bytes are effect classes and must not be rendered as
// literal VGA palette indices (that mistake produced the red/green square towers).
namespace
{
	constexpr int32 FirePtsObjectId = 0x120;
}

USimCopterFireRenderComponent::USimCopterFireRenderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool USimCopterFireRenderComponent::InitFireAssets(const FString& OriginalGameRoot, UMaterialInterface* InFlameMaterial, FString& OutError)
{
	if (bAssetsReady)
	{
		return true;
	}

	FlameMaterial = InFlameMaterial;

	if (OriginalGameRoot.IsEmpty())
	{
		OutError = TEXT("Original game root is empty.");
		return false;
	}

	FMaxisMeshLibrary MeshLibrary;
	if (!MeshLibrary.LoadFromOriginalGameRoot(OriginalGameRoot, OutError))
	{
		return false;
	}

	const TArray<FColor>* ColorMap = nullptr;
	const FMaxisMeshObject* FlameObject = MeshLibrary.FindObjectByObjectId(FirePtsObjectId, &ColorMap);
	if (FlameObject == nullptr)
	{
		OutError = FString::Printf(TEXT("FIREPTS flame object (id 0x%x) not found in '%s'."), FirePtsObjectId, *OriginalGameRoot);
		return false;
	}
	if (ColorMap == nullptr || ColorMap->Num() < 256)
	{
		OutError = TEXT("FIREPTS shared palette is missing or incomplete.");
		return false;
	}
	SharedPalette = *ColorMap;
	SelectorAtlas = FSimCopterEffectRasterizer::CreateSelectorAtlas(this, SharedPalette);
	if (SelectorAtlas == nullptr)
	{
		OutError = TEXT("Could not build the original effect selector atlas.");
		return false;
	}
	if (FlameMaterial != nullptr)
	{
		FlameMaterialInstance = UMaterialInstanceDynamic::Create(FlameMaterial, this);
		if (FlameMaterialInstance != nullptr)
		{
			FlameMaterialInstance->SetTextureParameterValue(TEXT("Texture"), SelectorAtlas);
		}
	}

	// Extract one runtime point per marker. Face material 2 is the lower fire body and material 1
	// is the tall smoke trail; neither is a literal on-screen red/green color.
	FirePoints.Reset();
	FBox PointBounds(ForceInit);
	for (const FMaxisMeshFace& Face : FlameObject->Faces)
	{
		if (Face.VertexIndices.Num() < 1)
		{
			continue;
		}
		const int32 VertexIndex = Face.VertexIndices[0];
		if (!FlameObject->Vertices.IsValidIndex(VertexIndex))
		{
			continue;
		}

		FFirePoint Point;
		Point.LocalOffset = SimCopterEffectFX::ApplyCityMeshOrientation(
			FMaxisMeshReader::ConvertMaxisVertexToUnreal(
				FlameObject->Vertices[VertexIndex],
				ModelUnitsPerCentimeter) *
			FlameModelScale);
		Point.EffectClass = Face.MaterialIndex;
		FirePoints.Add(Point);
		PointBounds += Point.LocalOffset;
	}

	if (FirePoints.Num() == 0)
	{
		OutError = TEXT("FIREPTS produced no fire points.");
		return false;
	}

	// Seat the base of the authored point cloud at Z = 0. The point's screen footprint is computed
	// later by FUN_00496da0's class/depth rules; it is not inferred from these object bounds.
	const float BaseZ = PointBounds.Min.Z;
	for (FFirePoint& Point : FirePoints)
	{
		Point.LocalOffset.Z -= BaseZ;
	}

	bAssetsReady = true;
	return true;
}

void USimCopterFireRenderComponent::SyncFlames(const TArray<FSimCopterFlameVisual>& Visuals, float TimeSeconds, const FVector& CameraLocation)
{
	if (!bAssetsReady)
	{
		return;
	}

	if (MeshComponent == nullptr)
	{
		MeshComponent = NewObject<UProceduralMeshComponent>(this, TEXT("FireCards"));
		MeshComponent->SetupAttachment(this);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComponent->SetCanEverAffectNavigation(false);
		MeshComponent->bUseAsyncCooking = false;
		MeshComponent->SetCastShadow(false);
		MeshComponent->RegisterComponent();
	}

	if (Visuals.Num() == 0)
	{
		if (MeshComponent->GetNumSections() > 0)
		{
			MeshComponent->ClearAllMeshSections();
		}
		return;
	}

	const FTransform WorldToLocal = GetComponentTransform().Inverse();

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;

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

	for (const FSimCopterFlameVisual& Visual : Visuals)
	{
		for (int32 PointIndex = 0; PointIndex < FirePoints.Num(); ++PointIndex)
		{
			const FFirePoint& Point = FirePoints[PointIndex];
			const FVector Center = Visual.World + Point.LocalOffset * Visual.Scale;
			const float CameraDepth = FVector::DotProduct(Center - CameraLocation, CameraForward);
			if (CameraDepth <= 1.0f)
			{
				continue;
			}

			const int32 DepthScale =
				FSimCopterEffectRasterizer::ComputeDepthScale1616(CameraDepth);
			const FSimCopterEffectKernelMetrics Metrics =
				FSimCopterEffectRasterizer::ComputeKernelMetrics(Point.EffectClass, DepthScale);
			if (Metrics.Iterations <= 0)
			{
				continue;
			}

			// One original 560x400 viewport pixel, in centimetres at this depth. The
			// original kernels are square, so both axes share it.
			const float PixelSizeCm =
				FSimCopterEffectRasterizer::GetWorldSizePerViewportPixel(CameraDepth);
			uint32 RandomState =
				static_cast<uint32>(Visual.Key) * 0x9e3779b9u ^
				static_cast<uint32>(PointIndex) * 0x85ebca6bu ^
				static_cast<uint32>(RasterFrame) * 0xc2b2ae35u ^
				GetTypeHash(Visual.FlickerSeed);
			// FUN_00496da0 consumes rand()%7 to choose a background-remap row
			// before entering the 0x10 renderer. That row is unused by its direct
			// selector writes, but consuming the sample preserves random ordering.
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
				// The original writes the kernel's first pixel at the jittered projected
				// point and grows right/down, so the covered span is centred half a
				// pixel short of the geometric middle.
				const FVector KernelCenter =
					Center +
					CameraRight *
						((static_cast<float>(JitterX) + (RasterWidth - 1.0f) * 0.5f) * PixelSizeCm) -
					CameraUp *
						((static_cast<float>(JitterY) + (RasterHeight - 1.0f) * 0.5f) * PixelSizeCm);
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

				const int32 SelectorPhase =
					FSimCopterEffectRasterizer::ConsumeSelectorPhase(Radius);
				const FVector2D UV0 = FSimCopterEffectRasterizer::GetAtlasUV(
					Point.EffectClass, SelectorPhase, Radius, 0, 0);
				const FVector2D UV1 = FSimCopterEffectRasterizer::GetAtlasUV(
					Point.EffectClass, SelectorPhase, Radius, Stencil.Width, Stencil.Height);
				UVs.Append({
					FVector2D(UV0.X, UV1.Y),
					UV1,
					FVector2D(UV1.X, UV0.Y),
					UV0
				});
				const FVector LocalNormal = WorldToLocal.TransformVectorNoScale(-CameraForward);
				const FProcMeshTangent Tangent(
					WorldToLocal.TransformVectorNoScale(CameraRight),
					false);
				for (int32 Vertex = 0; Vertex < 4; ++Vertex)
				{
					Normals.Add(LocalNormal);
					Colors.Add(FLinearColor::White);
					Tangents.Add(Tangent);
				}
				Triangles.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
			}
		}
	}

	if (Vertices.IsEmpty())
	{
		MeshComponent->ClearAllMeshSections();
		return;
	}
	MeshComponent->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
	if (FlameMaterialInstance != nullptr)
	{
		// The flames ride the shared unlit M_SimCopterSpriteTexture, whose baked EmissiveNits default
		// is a fixed daylight 26000. Without this the fire burned at that value at every hour - four
		// orders of magnitude over the night exposure - while the smoke and embers beside it, which
		// come from USimCopterParticleFXComponent, tracked the sun correctly. Same derivation, same
		// numbers, so the two halves of one fire finally agree.
		USimCopterEffectExposureSubsystem::ApplyEmissiveNits(
			FlameMaterialInstance, GetWorld(), /*bIsLightSource=*/true);
		MeshComponent->SetMaterial(0, FlameMaterialInstance);
	}
}
