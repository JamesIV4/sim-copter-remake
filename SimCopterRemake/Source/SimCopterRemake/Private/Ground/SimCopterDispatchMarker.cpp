// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterDispatchMarker.h"

#include "Formats/MaxisMeshLibrary.h"
#include "Formats/MaxisProceduralMeshBuilder.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterDispatchMarker, Log, All);

namespace
{
// The vehicle bodies are built at this scale by the ground agent (VehicleModelScale), and the
// marker has to read at the same size beside them.
constexpr float MarkerModelUnitsPerCentimeter = 2621.44f;
constexpr float MarkerModelScale = 0.25f;
constexpr bool bMarkerRenderBackfaces = true;
}

USimCopterDispatchMarkerComponent::USimCopterDispatchMarkerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;

	// The marker IS the mesh: a child procedural mesh created as a default subobject would not
	// be registered when the owner spawns this component at runtime, and would never render.
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCastShadow(false);
	SetVisibility(false);
	bUseAsyncCooking = false;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ModelMaterialFinder(
		TEXT("/Game/Materials/M_SimCopterLitVertexColor.M_SimCopterLitVertexColor"));
	if (ModelMaterialFinder.Succeeded())
	{
		VertexColorMaterial = ModelMaterialFinder.Object;
	}
}

int32 USimCopterDispatchMarkerComponent::GetMarkerObjectId(const SimCopterDispatch::EService Service)
{
	// The object each service constructor loads onto the vehicle's second render node.
	switch (Service)
	{
	case SimCopterDispatch::EService::Ambulance: return 0x121; // FUN_004b8e10
	case SimCopterDispatch::EService::FireTruck: return 0x123; // FUN_004b9350
	case SimCopterDispatch::EService::Police:    return 0x122; // FUN_004b9ce0
	default: return INDEX_NONE;
	}
}

const TCHAR* USimCopterDispatchMarkerComponent::GetMarkerObjectName(const SimCopterDispatch::EService Service)
{
	switch (Service)
	{
	case SimCopterDispatch::EService::Ambulance: return TEXT("AICON");
	case SimCopterDispatch::EService::FireTruck: return TEXT("FICON");
	case SimCopterDispatch::EService::Police:    return TEXT("PICON");
	default: return TEXT("");
	}
}

bool USimCopterDispatchMarkerComponent::EnsureMeshBuilt(const SimCopterDispatch::EService Service)
{
	if (BuiltService == Service)
	{
		return !bBuildFailed;
	}

	// A redispatch of a different service reuses the component, so reset the failure latch too.
	BuiltService = Service;
	bBuildFailed = true;
	LastLoadError.Reset();

	const int32 ObjectId = GetMarkerObjectId(Service);
	if (ObjectId == INDEX_NONE)
	{
		LastLoadError = TEXT("No marker object for this service.");
		return false;
	}

	if (OriginalGameRoot.IsEmpty())
	{
		LastLoadError = TEXT("Original game root is empty.");
		return false;
	}

	FMaxisMeshLibrary MeshLibrary;
	FString Error;
	if (!MeshLibrary.LoadFromOriginalGameRoot(OriginalGameRoot, Error))
	{
		LastLoadError = Error;
		return false;
	}

	const TArray<FColor>* ColorMap = nullptr;
	const FMaxisMeshObject* Object = MeshLibrary.FindObjectByObjectId(ObjectId, &ColorMap);
	if (Object == nullptr)
	{
		LastLoadError = FString::Printf(
			TEXT("Could not find dispatch marker '%s' (GEO id 0x%03x) in '%s'."),
			GetMarkerObjectName(Service),
			ObjectId,
			*OriginalGameRoot);
		return false;
	}

	// The markers are flat-shaded palette art like the vehicle bodies, so they go through the
	// same builder. They carry no face-type-11 cards, so there is no second section to split off.
	FMaxisMeshSection Section;
	FMaxisProceduralMeshBuilder::BuildPaletteColoredSection(
		*Object,
		ColorMap,
		MarkerModelUnitsPerCentimeter,
		MarkerModelScale,
		bMarkerRenderBackfaces,
		FLinearColor(0.85f, 0.85f, 0.88f),
		Section);

	if (Section.IsEmpty())
	{
		LastLoadError = FString::Printf(
			TEXT("Dispatch marker '%s' (GEO id 0x%03x) built no triangles."),
			GetMarkerObjectName(Service),
			ObjectId);
		return false;
	}

	ClearAllMeshSections();
	CreateMeshSection_LinearColor(
		0,
		Section.Vertices,
		Section.Triangles,
		Section.Normals,
		Section.UVs,
		Section.VertexColors,
		Section.Tangents,
		false);
	if (VertexColorMaterial != nullptr)
	{
		SetMaterial(0, VertexColorMaterial);
	}

	bBuildFailed = false;
	return true;
}

bool USimCopterDispatchMarkerComponent::ShowAt(
	const SimCopterDispatch::EService Service,
	const FVector& WorldLocation)
{
	if (!EnsureMeshBuilt(Service))
	{
		if (bShown)
		{
			Hide();
		}
		return false;
	}

	SetWorldLocation(WorldLocation);
	if (!bShown)
	{
		bShown = true;
		SetVisibility(true);
	}
	return true;
}

void USimCopterDispatchMarkerComponent::Hide()
{
	if (!bShown)
	{
		return;
	}
	bShown = false;
	SetVisibility(false);
}

void USimCopterDispatchMarkerComponent::StepSpin()
{
	if (!bShown)
	{
		return;
	}

	// FUN_004be750: FUN_0046cafc(((rand() % 10) * 5 + 0x19) * 0x20000, matrix). FUN_0046c594
	// wraps at 0xe100000, so a full turn is 3600 units - tenth-degrees - and the 16.16 factor
	// 0x20000 doubles the step. That is a fresh random 5.0 to 14.0 degrees every tick, applied
	// to the matrix the node already carries rather than to an absolute angle.
	const int32 Step = ((FMath::Rand() % 10) * 5 + 0x19) * 2;
	SpinYawDegrees = FMath::Fmod(SpinYawDegrees + static_cast<float>(Step) * 0.1f, 360.0f);
	SetRelativeRotation(FRotator(0.0f, SpinYawDegrees, 0.0f));
}
