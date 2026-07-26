// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimCopterHangar.generated.h"

class APlayerController;
class ASimCopterTrafficSystemActor;
class UBoxComponent;
class UMaterialInterface;
class UProceduralMeshComponent;
class USimCopterHangarArt;
class SWidget;

// Placement maths for the player's hangar, kept free of the actor so it can be tested without a
// world.
//
// **The hangar stands on the airport's terminal plot** - the middle 2x2 of the 4x4 block
// FUN_004829f0 stamps, ringed by its twelve helipads (see SimCopterAirport). That is the plot the
// original puts its own airport building on (XBLD 0xf6 -> GEO object 0x096 on base 0x165), and
// the one the airport port already resolves through SimCopterAirport::GetTerminalTile. Standing
// the hangar anywhere else would put a second building beside an airport that already has one.
//
// The building itself is not the original's. `dhangar.bmp` is a rendered still and the original's
// shell is reached from a menu, so there is no hangar mesh to port - only the plot, the height
// FUN_004829f0 flattens the block to, and the textures object 0x096 is skinned with.
namespace SimCopterHangarPlacement
{
// The footprint is the terminal's own 2x2 plot, inset a little so the walls never sit on the
// helipads that ring it. 760 x 760 cm at the shipped 400 cm tile.
constexpr float WidthTiles = 1.9f;
constexpr float DepthTiles = 1.9f;
constexpr float EavesHeightTiles = 0.5f;
constexpr float ApexHeightTiles = 0.8f;
constexpr float DoorWidthTiles = 1.25f;
constexpr float DoorHeightTiles = 0.42f;

// The floor slab is lifted clear of the flattened apron under it. The city actor offsets its own
// meshes off the terrain by the same 2 cm for the same reason - two coplanar surfaces z-fight.
constexpr float FloorLiftCm = 2.0f;

// Atlas cells the original's airport building is skinned with, all on page 40 of BMP/SIM3D.BMP:
// object 0x096's fourteen faces use cells 20..23 for its walls (cell 23 on five of them, so that
// is the one the hangar takes), and pad object 0x08b uses cell 61 for its slab. Cell 52 is the
// airport's gravel, which the terminal's flat-shaded roof faces have no texture for.
constexpr int32 TexturePage = 40;
constexpr int32 WallTextureCell = 23;
constexpr int32 RoofTextureCell = 52;
constexpr int32 FloorTextureCell = 61;

// One repeat of a 32x32 cell across this many centimetres of wall. The terminal's own UVs run to
// 3.0 over its 478 cm side, i.e. about 160 cm a repeat; this keeps that density.
constexpr float TextureRepeatCm = 170.0f;

// The terminal plot's top-left tile - SimCopterAirport::GetTerminalTile, restated here so the
// placement maths reads on its own.
SIMCOPTERREMAKE_API FIntPoint GetPlotOriginTile(const FIntPoint& AirportOrigin);

// The plot's centre in tile-centre coordinates: the airport origin plus (1.5, 1.5), because the
// 2x2 spans tile offsets 1 and 2 on both axes.
SIMCOPTERREMAKE_API FVector2D GetPlotCentreTile(const FIntPoint& AirportOrigin);

// The four tiles of the plot. These are the ones whose building has to come off before the
// hangar goes on, and they are what the pad ring is measured against.
SIMCOPTERREMAKE_API void GetPlotTiles(const FIntPoint& AirportOrigin, TArray<FIntPoint>& OutTiles);

// Yaw in degrees that turns the actor's +X (the doorway) toward Target, snapped to the nearest
// quarter turn so the building stays square with the city grid and its doors open onto a pad row
// rather than across a corner.
SIMCOPTERREMAKE_API float GetSnappedFacingYawDegrees(const FVector& From, const FVector& Target);
}

// The player's home base: a hangar beside the airport that opens the original's hangar shell
// when they walk into it.
UCLASS()
class SIMCOPTERREMAKE_API ASimCopterHangar : public AActor
{
	GENERATED_BODY()

public:
	ASimCopterHangar();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	// True when WorldLocation is inside the doorway volume. The overlap event opens the shell the
	// instant the player walks in; this is what the actor's slow tick re-checks with, so a pawn
	// that arrives by teleport (a respawn, a console jump) is not missed.
	bool IsInsideEntryVolume(const FVector& WorldLocation) const;

	// Stands the hangar beside the airport with its doorway turned to face FacingTarget (the
	// place the session puts the player). Returns false when the city has no usable ground for
	// it. Safe to call once the traffic system has built its grid.
	bool PlaceAtAirport(ASimCopterTrafficSystemActor* Traffic, const FVector& FacingTarget);

	// Where the world tag hangs - above the ridge.
	FVector GetTagWorldLocation() const;

	// True while the shell is up. The mission layer holds new jobs back while it is
	// (ISimCopterMissionWorld::IsModalUiActive).
	bool IsShellOpen() const { return bShellOpen; }

	// True for any hangar in the world.
	static bool IsAnyShellOpen(const UWorld* World);

	void OpenShell(APlayerController* PlayerController);

	// Closes the shell and pushes the player back out through the doors so the trigger they are
	// standing in does not immediately re-open it.
	void CloseShell();

	// Label and sub-label the world tag shows. "Base Location" is the mission log's own name for
	// this place (string 586).
	static const TCHAR* GetTagLabel() { return TEXT("HANGAR"); }
	static const TCHAR* GetTagDetail() { return TEXT("Base Location"); }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UProceduralMeshComponent> ShellMesh;

	// Sits inside the building, clear of the walls, so only walking through the doorway trips it.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCopter|Components")
	TObjectPtr<UBoxComponent> EntryTrigger;

	// One city tile in world centimetres. Overwritten by PlaceAtAirport from the city actor.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Hangar", meta = (ClampMin = "10.0"))
	float TileSizeCm = 400.0f;

	// How far past the doorway Done leaves the player, in tiles. Measured from the front wall, so
	// it only has to clear the entry trigger's own inset - a bigger shove would fling them across
	// the apron and into the pads.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Hangar", meta = (ClampMin = "0.0"))
	float ExitPushTiles = 0.45f;

	// Seconds after closing during which the trigger cannot re-open the shell. Belt and braces
	// behind the push-back, for the case where the player is walking into the doors as they
	// press Done.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Hangar", meta = (ClampMin = "0.0"))
	float ReopenCooldownSeconds = 1.25f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Original Assets")
	FDirectoryPath OriginalGameRoot;

	UFUNCTION()
	void HandleTriggerBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	// Skin the shell with the original airport building's own atlas cells. Off falls back to the
	// flat vertex colours, which is also what happens when the original assets are missing.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Hangar")
	bool bUseOriginalTextures = true;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ShellMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> TexturedMaterial;

	// One dynamic instance per skinned surface (walls / roof / floor), keyed by atlas cell.
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UMaterialInterface>> CellMaterials;

	UPROPERTY(Transient)
	TObjectPtr<USimCopterHangarArt> Art;

	TSharedPtr<SWidget> ShellWidget;
	TWeakObjectPtr<APlayerController> ShellController;

	bool bShellOpen = false;
	double ReopenAllowedTimeSeconds = 0.0;

	void BuildShellMesh();
	FString ResolveOriginalGameRoot() const;

	// Cuts one 32x32 cell out of SIM3D.BMP page 40 and wraps it in a lit-texture instance.
	// Returns null (and the caller falls back to vertex colour) when the originals are absent.
	UMaterialInterface* ResolveCellMaterial(int32 AtlasCell);

	// Shared by the overlap event and the tick: open the shell if this really is the player on
	// foot and the shell is not already up or on its re-open cooldown.
	void TryOpenForPawn(AActor* Candidate);

	// Puts the player one push-length back along the actor's forward axis - out through the
	// doorway, since the doorway is what forward points at.
	void PushPlayerOutOfDoorway();
};
