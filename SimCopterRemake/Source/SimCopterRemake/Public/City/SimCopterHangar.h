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
// The original has no hangar building: `dhangar.bmp` is a rendered still, and the shell is
// reached from the game's menu rather than by walking into anything. This remake gives the
// player somewhere to walk to, so the building and where it stands are a remake decision - but
// it is anchored to the one thing the original does place, the 4x4 airport block
// FUN_004829f0 stamps (see SimCopterAirport).
namespace SimCopterHangarPlacement
{
// The four tiles just outside the airport block, one per side, in the order they are tested.
enum class ESide : uint8
{
	// -Y in tile space: the row above the block.
	North,
	// +Y: the row below it, which is where pad 0 and the session's first helicopter sit.
	South,
	// -X: the column to its left.
	West,
	// +X: the column to its right.
	East,
	Count,
};

// The building's footprint, in city tiles. Roughly 38 m x 42 m at the shipped 400 cm tile.
constexpr float WidthTiles = 2.4f;
constexpr float DepthTiles = 2.6f;
constexpr float EavesHeightTiles = 0.55f;
constexpr float ApexHeightTiles = 0.85f;
constexpr float DoorWidthTiles = 1.5f;
constexpr float DoorHeightTiles = 0.5f;

// How far the hangar's centre stands off the block edge. DepthTiles/2 of that is the building
// itself, so the doors end up about a fifth of a tile clear of the outermost pads.
constexpr float StandoffTiles = 1.5f;

// Centre tile (fractional) of the hangar on one side of the block.
SIMCOPTERREMAKE_API FVector2D GetSideAnchorTile(const FIntPoint& AirportOrigin, ESide Side);

// Which side of the block a point in tile space is nearest to. Used to put the hangar on the
// side the player starts on so its doors are the first thing they see.
SIMCOPTERREMAKE_API ESide GetNearestSide(const FIntPoint& AirportOrigin, const FVector2D& TargetTile);

// The four sides ordered by how close each one's anchor is to TargetTile. The first entry is
// GetNearestSide; the rest are the fallbacks to try when that side is built on or under water.
SIMCOPTERREMAKE_API void GetSidesByDistance(
	const FIntPoint& AirportOrigin,
	const FVector2D& TargetTile,
	TArray<ESide>& OutSides);

// The integer tiles the building covers when anchored at AnchorTile. Three by three: the
// footprint is 2.4 x 2.6 tiles centred on a half-tile boundary.
SIMCOPTERREMAKE_API void GetFootprintTiles(const FVector2D& AnchorTile, TArray<FIntPoint>& OutTiles);

// Yaw in degrees that turns the actor's +X (the doorway) toward Target, snapped to the nearest
// quarter turn so the building stays square with the city grid.
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

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ShellMaterial;

	UPROPERTY(Transient)
	TObjectPtr<USimCopterHangarArt> Art;

	TSharedPtr<SWidget> ShellWidget;
	TWeakObjectPtr<APlayerController> ShellController;

	bool bShellOpen = false;
	double ReopenAllowedTimeSeconds = 0.0;

	void BuildShellMesh();
	FString ResolveOriginalGameRoot() const;

	// Shared by the overlap event and the tick: open the shell if this really is the player on
	// foot and the shell is not already up or on its re-open cooldown.
	void TryOpenForPawn(AActor* Candidate);

	// Puts the player one push-length back along the actor's forward axis - out through the
	// doorway, since the doorway is what forward points at.
	void PushPlayerOutOfDoorway();
};
