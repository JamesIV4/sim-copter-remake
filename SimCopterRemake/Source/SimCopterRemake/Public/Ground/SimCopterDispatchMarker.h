// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Ground/SimCopterDispatch.h"
#include "ProceduralMeshComponent.h"

#include "SimCopterDispatchMarker.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;

// The in-world waypoint marker a dispatched emergency vehicle hangs over its destination -
// what the shipped help calls the "Police / Fire Truck / Ambulance Dispatch Pylon".
//
// Decoded from SimCopter.exe:
//
//   FUN_004b8e10 / FUN_004b9350 / FUN_004b9ce0
//                 the three service constructors. Each loads one GEO object onto a *second*
//                 render node at veh + 0x13b, separate from the vehicle body, and leaves it
//                 hidden: 0x121 AICON "MedicPoint" (ambulance), 0x122 PICON "CopPointer"
//                 (police), 0x123 FICON "FirePointe" (fire truck).
//   FUN_004be890  links the marker into the destination tile's object list and sets the
//                 "marker placed" flag veh + 0x2b1 & 0x20.
//   FUN_004be820  unlinks it again and clears that flag - which is how a chase unit's marker
//                 follows the spotlight as the destination moves.
//   FUN_004b9e40  the vehicle tick. While the flag is set it re-anchors the marker on the
//                 destination tile's cell and positions it at (cell.worldX, cell.altitude +
//                 0xa0000, cell.worldZ) - the tile centre, ten original units off the ground.
//   FUN_004be750  rebuilds the node transform every tick, yawing the matrix by
//                 ((rand() % 10) * 5 + 0x19) * 0x20000 through FUN_0046cafc. FUN_0046c594
//                 wraps its angle at 0xe100000, so the unit is tenth-degrees and the step is a
//                 fresh random 5.0..14.0 degrees per tick - the spin that makes it read as a
//                 beacon rather than a prop.
//   FUN_004b9c00 / FUN_004babe0
//                 the save-load paths, which re-link the marker on exactly `2 < state < 5` -
//                 i.e. it is live while the unit is chasing (3) or responding (4), and gone
//                 once it arrives, is recalled, or parks.
//
// The separate 2D map overlay (FUN_004a42f0 / FUN_004a4370, the pylon table at DAT_005d3eb0
// that draws an icon and a line to the destination in the map window) is not ported: the
// remake has no map view to blit into.
UCLASS(ClassGroup = (SimCopter), meta = (BlueprintSpawnableComponent))
class SIMCOPTERREMAKE_API USimCopterDispatchMarkerComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	USimCopterDispatchMarkerComponent(const FObjectInitializer& ObjectInitializer);

	// GEO object id for a service's marker, or INDEX_NONE for one that has none.
	static int32 GetMarkerObjectId(SimCopterDispatch::EService Service);

	// Human-readable GEO table name, for logs.
	static const TCHAR* GetMarkerObjectName(SimCopterDispatch::EService Service);

	// Original units the marker floats above the destination tile's ground (0xa0000 in 16.16).
	static constexpr float MarkerHeightOriginalUnits = 10.0f;

	// Where the GEO tables live. The owner resolves this; the component only reads it.
	void SetOriginalGameRoot(const FString& InRootPath) { OriginalGameRoot = InRootPath; }

	// Builds the service's GEO object if it is not already built, parks the marker at
	// WorldLocation and shows it. Safe to call every tick; the mesh is only rebuilt when the
	// service changes. Returns false when the object could not be loaded.
	bool ShowAt(SimCopterDispatch::EService Service, const FVector& WorldLocation);

	// Leaves the mesh built but hidden - the original keeps the node and just unlinks it.
	void Hide();

	// One FUN_004be750 step: a fresh random yaw of 5.0..14.0 degrees. Call once per tick while
	// the marker is shown.
	void StepSpin();

	// Not IsShown: UPrimitiveComponent already has one taking show flags.
	bool IsMarkerShown() const { return bShown; }
	const FString& GetLastLoadError() const { return LastLoadError; }

	/**
	 * How much of an effect card's emissive a pylon carries - "very low", and a fraction rather than
	 * a number of nits on purpose.
	 *
	 * The scene runs a physically scaled sun (120,000 lux at noon), so any constant emissive is
	 * either invisible by day or a lamp at night; every other authored brightness in the project is
	 * therefore derived from the light in the scene, and this is the same. Live on
	 * `SimCopter.Dispatch.MarkerEmissive`.
	 */
	static float GetMarkerEmissiveFraction();

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> VertexColorMaterial;

	// The pylon's own instance of the shared lit vertex-colour material. It has to be an instance:
	// that material is also the untextured city and every vehicle, and EmissiveNits defaults to 0
	// there precisely so raising it here lights the pylon and nothing else.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MarkerMaterial;

	// Which service's object is currently built, so a redispatch of the same kind is free.
	SimCopterDispatch::EService BuiltService = SimCopterDispatch::EService::Count;
	bool bBuildFailed = false;
	bool bShown = false;
	float SpinYawDegrees = 0.0f;
	FString OriginalGameRoot;
	FString LastLoadError;

	bool EnsureMeshBuilt(SimCopterDispatch::EService Service);
};
