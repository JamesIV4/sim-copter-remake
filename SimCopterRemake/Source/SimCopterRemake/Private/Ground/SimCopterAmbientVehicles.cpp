// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ground/SimCopterAmbientVehicles.h"

#include "Audio/SimCopterAudioSubsystem.h"
#include "City/SimCity2000CityActor.h"
#include "Formats/MaxisMeshLibrary.h"
#include "Formats/MaxisProceduralMeshBuilder.h"
#include "Formats/SimCity2000Reader.h"
#include "Game/SimCopterVehicleMaterialSubsystem.h"
#include "Ground/SimCopterGroundAgent.h"
#include "Ground/SimCopterParticleFX.h"
#include "Ground/SimCopterTrafficSystemActor.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Missions/SimCopterMissionSystem.h"
#include "Missions/SimCopterMissionSystemActor.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterAmbientVehicles, Log, All);

namespace
{
constexpr uint32 AmbientRuntimeSaveMagic = 0x414d4249; // 'AMBI'
constexpr int32 AmbientRuntimeSaveVersion = 1;

void SerializeAmbientBool(FArchive& Archive, bool& Value)
{
	uint8 Byte = Value ? 1 : 0;
	Archive << Byte;
	if (Archive.IsLoading()) Value = Byte != 0;
}
// The city builds its GEO models at TileSize / OriginalMeshSourceTileSize; the ground agents use
// the same 0.25 at the default 400 cm tile. Planes, boats and trains are the same art, so they go
// through the same conversion.
constexpr float AmbientModelUnitsPerCentimeter = 2621.44f;
constexpr float AmbientModelSourceTileSize = 1600.0f;
constexpr bool bAmbientRenderBackfaces = true;

// FUN_004b37e0: the plane's cruise speed +0x1c and the 60-second respawn delay +0x30.
constexpr float PlaneCruiseUnitsPerSec = 120.0f;
constexpr float PlaneRespawnDelaySeconds = 60.0f;
// FUN_004b3420: one flight segment is 0x200000 (32.0) units long, and the plane holds
// max(buildingTop, 0x15e0000) + 0x1e0000 above the cell - or 0x17c0000 over open ground.
constexpr float PlaneSegmentUnits = 32.0f;
constexpr float PlaneCruiseAltitudeUnits = 380.0f;
// FUN_004b2630's UFO effect cadence (+0x48 = 0xb40000).
constexpr float UfoEffectIntervalSeconds = 180.0f;

// FUN_004b14f0: ambient boats respawn 10 s after they leave; the capsized boat never does.
constexpr float BoatRespawnDelaySeconds = 10.0f;
// FUN_004af770: the wake/effect timer resets to 0xe666.
constexpr float BoatWakeIntervalSeconds = 0.9f;
// FUN_004afb60: a helicopter within 70.0 units of the ground on the boat's tile pushes its
// speed up by as much as 30.0 units/s.
constexpr float BoatHelicopterScareAltitudeUnits = 70.0f;
constexpr float BoatHelicopterScareBoostUnits = 30.0f;
// Spiral limits. "Placement" is the mission path (FUN_004b10a0 / FUN_004b7890, which search hard
// for a spot near the tile the placer chose); "respawn" is the ambient path (FUN_004b0cf0 /
// FUN_004b74a0), which only walks seven rings out from the view edge so the vehicle lands inside
// the keep-alive radius instead of being dropped again on the next frame.
constexpr int32 BoatPlacementRings = 30;
constexpr int32 TrainPlacementRings = 20;
constexpr int32 AmbientRespawnRings = 7;

// FUN_004b7bf0: the train respawns 30 s after it leaves.
constexpr float TrainRespawnDelaySeconds = 30.0f;
// FUN_004b49b0: the derail runs for 0x20000 (2.0 s) before the cars blow up.
constexpr float TrainDerailSeconds = 2.0f;
// FUN_004b49b0 spins the loco by 10.0 degrees per frame while it slides.
constexpr float TrainDerailSpinDegPerFrame = 10.0f;

// FUN_004b1950 / FUN_004b7fd0 victim counts.
constexpr int32 BoatRescueMinVictims = 3;   // 3 + rand % 3
constexpr int32 BoatRescueVictimSpread = 3;
constexpr int32 TrainRescueMinVictims = 1;  // 1 + rand % 3
constexpr int32 TrainRescueVictimSpread = 3;

// FUN_004c3eb0 spawn modes. 1 = in the water beside the capsized boat, 0x13 = trapped by the train.
constexpr int32 WaterRescueSpawnMode = 1;
constexpr int32 TrainRescueSpawnMode = 0x13;
}

bool SimCopterAmbientVehicles::IsRailTile(const int32 XbldId, const int32 ZoneId)
{
	// FUN_004b7890 builds `xbldId | ((xzon & 2) << 14)` and accepts these ranges:
	//   0x2c..0x2d, 0x32..0x3a, 0x45..0x48, 0x4d..0x4e, 0x5a..0x5b and the 0x8000 raised variants.
	const uint32 Id = static_cast<uint32>(static_cast<uint8>(XbldId)) |
		((static_cast<uint32>(ZoneId) & 2u) << 14);

	if (Id < 0x3b)
	{
		return Id > 0x31 || (Id > 0x2b && Id < 0x2e);
	}
	if (Id < 0x4f)
	{
		return Id > 0x4c || (Id > 0x44 && Id < 0x49);
	}
	if (Id < 0x805c)
	{
		return Id > 0x8059 || (Id > 0x59 && Id < 0x5c);
	}
	return false;
}

bool SimCopterAmbientVehicles::IsTraversableRailTile(const int32 XbldId, const int32 ZoneId)
{
	// FUN_004b5290's "does the track continue" test, run on the tile it has just stepped to:
	//   0x2c..0x3e, 0x45..0x48, 0x4d..0x4e, 0x5a..0x5b and the 0x8000 raised variants.
	const uint32 Id = static_cast<uint32>(static_cast<uint8>(XbldId)) |
		((static_cast<uint32>(ZoneId) & 2u) << 14);

	if (Id < 0x49)
	{
		return Id > 0x44 || (Id > 0x2b && Id < 0x3f);
	}
	if (Id < 0x5c)
	{
		return Id > 0x59 || (Id > 0x4c && Id < 0x4f);
	}
	return Id > 0x8059 && Id < 0x805c;
}

float SimCopterAmbientVehicles::GetRailBridgeDeckHeightTileFraction(
	const int32 XbldId,
	const int32 ZoneId)
{
	// SCHOOK: FUN_004b7020 adds 0x1f0000 to the target's up coordinate for 0x5a/0x5b
	// and 0x805a/0x805b. One tile is 0x40 original units, so the RL90/RL90F running
	// plane is exactly 31/64 of a tile above the scene-cell terrain origin.
	const uint32 Id = static_cast<uint32>(static_cast<uint8>(XbldId)) |
		((static_cast<uint32>(ZoneId) & 2u) << 14);
	return Id == 0x5a || Id == 0x5b || Id == 0x805a || Id == 0x805b
		? 31.0f / OriginalUnitsPerTile
		: 0.0f;
}

float SimCopterAmbientVehicles::GetRailTileCenterHeightTileFraction(
	const int32 XbldId,
	const int32 ZoneId)
{
	const float BridgeHeight = GetRailBridgeDeckHeightTileFraction(XbldId, ZoneId);
	if (BridgeHeight > 0.0f)
	{
		return BridgeHeight;
	}

	const uint8 Id = static_cast<uint8>(XbldId);
	return Id >= 0x2e && Id <= 0x31
		? 15.5f / OriginalUnitsPerTile
		: 0.0f;
}

float SimCopterAmbientVehicles::GetRailEdgeHeightTileFraction(
	const int32 XbldId,
	const int32 ZoneId,
	const int32 NeighborDeltaX,
	const int32 NeighborDeltaY)
{
	const float BridgeHeight = GetRailBridgeDeckHeightTileFraction(XbldId, ZoneId);
	if (BridgeHeight > 0.0f)
	{
		return BridgeHeight;
	}

	// SCHOOK: FUN_004b7020 cases 0x2e..0x31 apply the same 0x1f0000 lift as
	// RL90/RL90F when the train target is approached from each piece's high edge. The
	// directions below are also visible directly in RL46..RL49's authored rail planes.
	const uint8 Id = static_cast<uint8>(XbldId);
	const bool bHighEdge =
		(Id == 0x2e && NeighborDeltaX == 0 && NeighborDeltaY < 0) ||
		(Id == 0x2f && NeighborDeltaX < 0 && NeighborDeltaY == 0) ||
		(Id == 0x30 && NeighborDeltaX == 0 && NeighborDeltaY > 0) ||
		(Id == 0x31 && NeighborDeltaX > 0 && NeighborDeltaY == 0);
	return bHighEdge ? 31.0f / OriginalUnitsPerTile : 0.0f;
}

ASimCopterAmbientVehiclesActor::ASimCopterAmbientVehiclesActor()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	EffectComponent = CreateDefaultSubobject<USimCopterParticleFXComponent>(TEXT("CrashEffects"));
	EffectComponent->SetupAttachment(Root);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ModelMaterialFinder(
		TEXT("/Game/Materials/M_SimCopterLitVertexColor.M_SimCopterLitVertexColor"));
	if (ModelMaterialFinder.Succeeded())
	{
		VertexColorMaterial = ModelMaterialFinder.Object;
	}
}

void ASimCopterAmbientVehiclesActor::BeginPlay()
{
	Super::BeginPlay();

	// Share the fleet's material instance so the metallic slider reaches the planes/trains/boats.
	if (USimCopterVehicleMaterialSubsystem* VehicleMaterials = USimCopterVehicleMaterialSubsystem::Get(this))
	{
		if (UMaterialInstanceDynamic* Shared = VehicleMaterials->GetVehicleMaterial(VertexColorMaterial))
		{
			VertexColorMaterial = Shared;
		}
	}

	RandomStream.Initialize(1996);

	if (EffectComponent != nullptr)
	{
		FString EffectError;
		if (!EffectComponent->InitEffectAssets(ResolveOriginalGameRoot(), EffectError))
		{
			UE_LOG(LogSimCopterAmbientVehicles, Warning,
				TEXT("Crash debris unavailable: %s"), *EffectError);
		}
	}
}

bool ASimCopterAmbientVehiclesActor::CaptureRuntimeSaveState(TArray<uint8>& OutData)
{
	EnsurePools();
	if (!bPoolsInitialized) return false;
	OutData.Reset();
	FMemoryWriter Writer(OutData, true);
	uint32 Magic = AmbientRuntimeSaveMagic;
	int32 Version = AmbientRuntimeSaveVersion;
	Writer << Magic << Version;
	int32 RandomCurrent = RandomStream.GetCurrentSeed();
	Writer << RandomCurrent << UfoBeamTickAccumSeconds << NextWreckKey;

	for (FSimCopterAmbientPlane& Plane : Planes)
	{
		Writer << Plane.ObjectId;
		SerializeAmbientBool(Writer, Plane.bVisible);
		SerializeAmbientBool(Writer, Plane.bCrashRequested);
		SerializeAmbientBool(Writer, Plane.bCrashing);
		Writer << Plane.Direction << Plane.SegmentRemainingCm << Plane.SpeedCmPerSec;
		Writer << Plane.Tile << Plane.RespawnAccumSeconds << Plane.VerticalSpeedCmPerSec;
		Writer << Plane.EventId << Plane.EffectTimerSeconds << Plane.HitCount << Plane.World;
	}
	for (FSimCopterAmbientBoat& Boat : Boats)
	{
		Writer << Boat.ObjectId;
		SerializeAmbientBool(Writer, Boat.bVisible);
		Writer << Boat.Direction << Boat.DistanceToTargetCm << Boat.SpeedCmPerSec << Boat.BaseSpeedCmPerSec;
		Writer << Boat.Tile << Boat.TargetTile << Boat.PreviousTile << Boat.RespawnAccumSeconds;
		Writer << Boat.EventId << Boat.MissionTimerSeconds << Boat.WakeTimerSeconds << Boat.World;
	}
	SerializeAmbientBool(Writer, Train.bVisible);
	SerializeAmbientBool(Writer, Train.bCrashRequested);
	SerializeAmbientBool(Writer, Train.bDerailing);
	SerializeAmbientBool(Writer, Train.bRescueActive);
	Writer << Train.Tile << Train.NextTile << Train.PreviousTile;
	Writer << Train.SpeedCmPerSec << Train.BaseSpeedCmPerSec << Train.RespawnAccumSeconds;
	Writer << Train.EventId << Train.MissionTimerSeconds << Train.DerailTimerSeconds;
	Writer << Train.DerailSpinDegrees << Train.World << Train.Direction << Train.PitchDegrees;
	Writer << Train.PathHistory;

	int32 WreckCount = Wrecks.Num();
	Writer << WreckCount;
	for (FSimCopterVehicleWreck& Wreck : Wrecks)
	{
		Writer << Wreck.ObjectId << Wreck.EventId << Wreck.Key;
		SerializeAmbientBool(Writer, Wreck.bBurning);
		Writer << Wreck.BurnTimeoutSeconds << Wreck.World << Wreck.Direction << Wreck.ExtraYawDegrees;
	}
	TArray<uint8> EffectState;
	if (EffectComponent == nullptr || !EffectComponent->CaptureRuntimeSaveState(EffectState))
	{
		OutData.Reset();
		return false;
	}
	Writer << EffectState;
	return !Writer.IsError();
}

bool ASimCopterAmbientVehiclesActor::RestoreRuntimeSaveState(const TArray<uint8>& Data)
{
	EnsurePools();
	if (!bPoolsInitialized || Data.IsEmpty()) return false;
	FMemoryReader Reader(Data, true);
	uint32 Magic = 0;
	int32 Version = 0;
	int32 RandomCurrent = 0;
	Reader << Magic << Version << RandomCurrent << UfoBeamTickAccumSeconds << NextWreckKey;
	if (Magic != AmbientRuntimeSaveMagic || Version != AmbientRuntimeSaveVersion) return false;
	RandomStream.Initialize(RandomCurrent);

	for (FSimCopterAmbientPlane& Plane : Planes)
	{
		UProceduralMeshComponent* Mesh = Plane.Mesh;
		Reader << Plane.ObjectId;
		SerializeAmbientBool(Reader, Plane.bVisible);
		SerializeAmbientBool(Reader, Plane.bCrashRequested);
		SerializeAmbientBool(Reader, Plane.bCrashing);
		Reader << Plane.Direction << Plane.SegmentRemainingCm << Plane.SpeedCmPerSec;
		Reader << Plane.Tile << Plane.RespawnAccumSeconds << Plane.VerticalSpeedCmPerSec;
		Reader << Plane.EventId << Plane.EffectTimerSeconds << Plane.HitCount << Plane.World;
		Plane.Mesh = Mesh;
	}
	for (FSimCopterAmbientBoat& Boat : Boats)
	{
		UProceduralMeshComponent* Mesh = Boat.Mesh;
		Reader << Boat.ObjectId;
		SerializeAmbientBool(Reader, Boat.bVisible);
		Reader << Boat.Direction << Boat.DistanceToTargetCm << Boat.SpeedCmPerSec << Boat.BaseSpeedCmPerSec;
		Reader << Boat.Tile << Boat.TargetTile << Boat.PreviousTile << Boat.RespawnAccumSeconds;
		Reader << Boat.EventId << Boat.MissionTimerSeconds << Boat.WakeTimerSeconds << Boat.World;
		Boat.Mesh = Mesh;
	}
	UProceduralMeshComponent* LocoMesh = Train.LocoMesh;
	UProceduralMeshComponent* CarMeshes[SimCopterAmbientVehicles::TrainCarCount] = {Train.CarMeshes[0], Train.CarMeshes[1]};
	SerializeAmbientBool(Reader, Train.bVisible);
	SerializeAmbientBool(Reader, Train.bCrashRequested);
	SerializeAmbientBool(Reader, Train.bDerailing);
	SerializeAmbientBool(Reader, Train.bRescueActive);
	Reader << Train.Tile << Train.NextTile << Train.PreviousTile;
	Reader << Train.SpeedCmPerSec << Train.BaseSpeedCmPerSec << Train.RespawnAccumSeconds;
	Reader << Train.EventId << Train.MissionTimerSeconds << Train.DerailTimerSeconds;
	Reader << Train.DerailSpinDegrees << Train.World << Train.Direction << Train.PitchDegrees;
	Reader << Train.PathHistory;
	Train.LocoMesh = LocoMesh;
	for (int32 Index = 0; Index < SimCopterAmbientVehicles::TrainCarCount; ++Index) Train.CarMeshes[Index] = CarMeshes[Index];

	struct FSavedWreck
	{
		int32 ObjectId = INDEX_NONE;
		int32 EventId = INDEX_NONE;
		int32 Key = 0;
		bool bBurning = false;
		float Timeout = 0.0f;
		FVector World = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
		float Yaw = 0.0f;
	};
	int32 WreckCount = 0;
	Reader << WreckCount;
	if (WreckCount < 0 || WreckCount > 32) return false;
	TArray<FSavedWreck> SavedWrecks;
	SavedWrecks.SetNum(WreckCount);
	for (FSavedWreck& Wreck : SavedWrecks)
	{
		Reader << Wreck.ObjectId << Wreck.EventId << Wreck.Key;
		SerializeAmbientBool(Reader, Wreck.bBurning);
		Reader << Wreck.Timeout << Wreck.World << Wreck.Direction << Wreck.Yaw;
	}
	TArray<uint8> EffectState;
	Reader << EffectState;
	if (EffectState.IsEmpty()) return false;
	if (Reader.IsError() || Reader.Tell() != Reader.TotalSize()) return false;

	for (FSimCopterVehicleWreck& Wreck : Wrecks) DestroyWreck(Wreck);
	Wrecks.Reset();
	const int32 SavedNextWreckKey = NextWreckKey;
	for (const FSavedWreck& Saved : SavedWrecks)
	{
		if (FSimCopterVehicleWreck* Wreck = AddWreck(
			Saved.ObjectId, Saved.World, Saved.Direction, Saved.Yaw,
			Saved.EventId, Saved.bBurning, Saved.Timeout))
		{
			Wreck->Key = Saved.Key;
		}
	}
	NextWreckKey = SavedNextWreckKey;
	if (EffectComponent == nullptr || !EffectComponent->RestoreRuntimeSaveState(EffectState))
	{
		return false;
	}

	for (FSimCopterAmbientPlane& Plane : Planes)
	{
		if (Plane.Mesh != nullptr)
		{
			Plane.Mesh->SetVisibility(Plane.bVisible);
			if (Plane.bVisible) SetMeshTransform(Plane.Mesh, Plane.World, Plane.Direction);
		}
	}
	for (FSimCopterAmbientBoat& Boat : Boats)
	{
		if (Boat.Mesh != nullptr)
		{
			Boat.Mesh->SetVisibility(Boat.bVisible);
			if (Boat.bVisible) SetMeshTransform(Boat.Mesh, Boat.World, Boat.Direction);
		}
	}
	if (Train.LocoMesh != nullptr) Train.LocoMesh->SetVisibility(Train.bVisible);
	for (UProceduralMeshComponent* Mesh : Train.CarMeshes) if (Mesh != nullptr) Mesh->SetVisibility(Train.bVisible);
	if (Train.bVisible) UpdateTrainCarTransforms();

	TrainRoofRiders.Reset();
	if (Train.EventId != INDEX_NONE && GetWorld() != nullptr)
	{
		TArray<AActor*> People;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ASimCopterGroundAgent::StaticClass(), People);
		for (AActor* Actor : People)
		{
			ASimCopterGroundAgent* Person = Cast<ASimCopterGroundAgent>(Actor);
			if (Person != nullptr && Person->GetAgentKind() == ESimCopterGroundAgentKind::Pedestrian &&
				Person->MissionEventId == Train.EventId)
			{
				TrainRoofRiders.Add(Person);
			}
		}
		UpdateTrainRoofRiders();
	}
	return true;
}

USceneComponent* ASimCopterAmbientVehiclesActor::GetUfoBeamTargetComponent() const
{
	for (const FSimCopterAmbientPlane& Plane : Planes)
	{
		if (Plane.ObjectId == SimCopterAmbientVehicles::UfoObjectId && Plane.Mesh != nullptr)
		{
			return Plane.Mesh;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Resolution helpers
// ---------------------------------------------------------------------------------------------

ASimCopterTrafficSystemActor* ASimCopterAmbientVehiclesActor::ResolveTrafficSystem() const
{
	if (UWorld* World = GetWorld())
	{
		return Cast<ASimCopterTrafficSystemActor>(
			UGameplayStatics::GetActorOfClass(World, ASimCopterTrafficSystemActor::StaticClass()));
	}
	return nullptr;
}

ASimCity2000CityActor* ASimCopterAmbientVehiclesActor::ResolveCityActor() const
{
	if (const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		if (ASimCity2000CityActor* City = TrafficSystem->GetCityActor())
		{
			return City;
		}
	}
	if (UWorld* World = GetWorld())
	{
		return Cast<ASimCity2000CityActor>(
			UGameplayStatics::GetActorOfClass(World, ASimCity2000CityActor::StaticClass()));
	}
	return nullptr;
}

ASimCopterMissionSystemActor* ASimCopterAmbientVehiclesActor::ResolveMissionSystem() const
{
	if (UWorld* World = GetWorld())
	{
		return Cast<ASimCopterMissionSystemActor>(
			UGameplayStatics::GetActorOfClass(World, ASimCopterMissionSystemActor::StaticClass()));
	}
	return nullptr;
}

FString ASimCopterAmbientVehiclesActor::ResolveOriginalGameRoot() const
{
	if (const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		return TrafficSystem->ResolveOriginalGameRoot();
	}
	if (const ASimCity2000CityActor* City = ResolveCityActor())
	{
		return City->GetResolvedOriginalGameRoot();
	}
	return FString();
}

float ASimCopterAmbientVehiclesActor::GetTileSizeCm() const
{
	if (const ASimCity2000CityActor* City = ResolveCityActor())
	{
		const float TileSize = City->GetTileSize();
		if (TileSize > KINDA_SMALL_NUMBER)
		{
			return TileSize;
		}
	}
	return 400.0f;
}

float ASimCopterAmbientVehiclesActor::GetCmPerOriginalUnit() const
{
	return GetTileSizeCm() / SimCopterAmbientVehicles::OriginalUnitsPerTile;
}

bool ASimCopterAmbientVehiclesActor::TryGetCameraTile(FIntPoint& OutTile) const
{
	if (const ASimCopterMissionSystemActor* Mission = ResolveMissionSystem())
	{
		int32 X = INDEX_NONE;
		int32 Y = INDEX_NONE;
		if (Mission->GetCameraTile(X, Y))
		{
			OutTile = FIntPoint(X, Y);
			return true;
		}
	}
	return false;
}

bool ASimCopterAmbientVehiclesActor::TryGetTileCenter(const FIntPoint& Tile, FVector& OutWorld) const
{
	if (const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		return TrafficSystem->TryGetTileCenterWorldLocation(Tile.X, Tile.Y, OutWorld);
	}
	return false;
}

bool ASimCopterAmbientVehiclesActor::TryGetTrainTileCenter(
	const FIntPoint& Tile,
	FVector& OutWorld) const
{
	const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem == nullptr || !TryGetTileCenter(Tile, OutWorld))
	{
		return false;
	}

	const float HeightFraction = SimCopterAmbientVehicles::GetRailTileCenterHeightTileFraction(
		TrafficSystem->GetXbldTileId(Tile.X, Tile.Y),
		TrafficSystem->GetZoneTileId(Tile.X, Tile.Y));
	if (HeightFraction <= 0.0f)
	{
		return true;
	}

	const FVector LocalDeckOffset(0.0f, 0.0f, GetTileSizeCm() * HeightFraction);
	OutWorld += ResolveCityActor() != nullptr
		? ResolveCityActor()->GetActorTransform().TransformVector(LocalDeckOffset)
		: LocalDeckOffset;
	return true;
}

bool ASimCopterAmbientVehiclesActor::IsWaterTile(const FIntPoint& Tile) const
{
	if (const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		return TrafficSystem->IsWaterTile(Tile.X, Tile.Y);
	}
	return false;
}

bool ASimCopterAmbientVehiclesActor::IsOpenWaterTile(const FIntPoint& Tile) const
{
	// FUN_004b10a0's CAPBOAT1 branch: every tile of the 3x3 has to be water, which is what keeps
	// the capsized boat off the shoreline.
	for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
	{
		for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
		{
			if (!IsWaterTile(FIntPoint(Tile.X + OffsetX, Tile.Y + OffsetY)))
			{
				return false;
			}
		}
	}
	return true;
}

bool ASimCopterAmbientVehiclesActor::TryGetWaterSurfaceZ(const FVector& WorldXY, float& OutZ) const
{
	if (const ASimCity2000CityActor* City = ResolveCityActor())
	{
		uint8 TerrainClass = 0;
		if (City->TryGetWaterGameplaySurface(WorldXY, OutZ, TerrainClass))
		{
			return true;
		}
		return City->TryGetOceanSurfaceWorldZ(OutZ);
	}
	return false;
}

bool ASimCopterAmbientVehiclesActor::IsRailTileAt(const FIntPoint& Tile, const bool bForTravel) const
{
	const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem == nullptr ||
		Tile.X < 0 || Tile.X >= FSimCity2000City::MapSize ||
		Tile.Y < 0 || Tile.Y >= FSimCity2000City::MapSize)
	{
		return false;
	}

	const int32 XbldId = TrafficSystem->GetXbldTileId(Tile.X, Tile.Y);
	const int32 ZoneId = TrafficSystem->GetZoneTileId(Tile.X, Tile.Y);
	return bForTravel
		? SimCopterAmbientVehicles::IsTraversableRailTile(XbldId, ZoneId)
		: SimCopterAmbientVehicles::IsRailTile(XbldId, ZoneId);
}

void ASimCopterAmbientVehiclesActor::EnsureRailScan()
{
	if (bRailScanned)
	{
		return;
	}

	const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem == nullptr)
	{
		return;
	}

	RailTiles.Reset();
	RailTileSet.Reset();
	WaterTiles.Reset();
	for (int32 Y = 0; Y < FSimCity2000City::MapSize; ++Y)
	{
		for (int32 X = 0; X < FSimCity2000City::MapSize; ++X)
		{
			if (SimCopterAmbientVehicles::IsRailTile(
					TrafficSystem->GetXbldTileId(X, Y),
					TrafficSystem->GetZoneTileId(X, Y)))
			{
				RailTiles.Add(FIntPoint(X, Y));
				RailTileSet.Add(FIntPoint(X, Y));
			}
			else if (TrafficSystem->IsWaterTile(X, Y))
			{
				WaterTiles.Add(FIntPoint(X, Y));
			}
		}
	}

	bRailScanned = true;
	UE_LOG(LogSimCopterAmbientVehicles, Display,
		TEXT("Rail network: %d tiles. Water: %d tiles."), RailTiles.Num(), WaterTiles.Num());
}

bool ASimCopterAmbientVehiclesActor::TryGetTrainWaypoint(
	const FIntPoint& FromTile,
	const FIntPoint& ToTile,
	FVector& OutWorld) const
{
	// The train runs on the midpoints of the tile edges it crosses, not on tile centres.
	//
	// On a straight run the two are the same line (edge midpoints and tile centres are colinear),
	// but SimCity's diagonal rail - XBLD 0x32..0x35, the RL50..RL53 corner pieces - is a corner
	// tile joining one vertical neighbour to one horizontal one. Aiming at its centre makes the
	// train dogleg through a right angle in the middle of a tile whose art is a straight diagonal.
	// Aiming at the shared edge instead cuts the corner as a single straight chord, which is what
	// the rendered track shows and what FUN_004b5290's two-leg direction codes (3/6/9/0xc) mean.
	FVector FromCenter = FVector::ZeroVector;
	FVector ToCenter = FVector::ZeroVector;
	if (!TryGetTileCenter(FromTile, FromCenter) || !TryGetTileCenter(ToTile, ToCenter))
	{
		return false;
	}

	OutWorld = (FromCenter + ToCenter) * 0.5f;

	// FUN_004b7020 uses the same exact +31-unit height for an RL46..RL49 high edge and an
	// RL90/RL90F bridge deck. Evaluate both tiles at their shared edge and keep the larger value:
	// each grade then runs continuously from 0 -> 31 -> bridge (and back down) instead of changing
	// height at tile centres or briefly sinking under the rendered track.
	const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem != nullptr)
	{
		const FIntPoint FromToDelta = ToTile - FromTile;
		const FIntPoint ToFromDelta = FromTile - ToTile;
		const float FromFraction = SimCopterAmbientVehicles::GetRailEdgeHeightTileFraction(
			TrafficSystem->GetXbldTileId(FromTile.X, FromTile.Y),
			TrafficSystem->GetZoneTileId(FromTile.X, FromTile.Y),
			FromToDelta.X,
			FromToDelta.Y);
		const float ToFraction = SimCopterAmbientVehicles::GetRailEdgeHeightTileFraction(
			TrafficSystem->GetXbldTileId(ToTile.X, ToTile.Y),
			TrafficSystem->GetZoneTileId(ToTile.X, ToTile.Y),
			ToFromDelta.X,
			ToFromDelta.Y);
		const FVector LocalDeckOffset(
			0.0f,
			0.0f,
			GetTileSizeCm() * FMath::Max(FromFraction, ToFraction));
		OutWorld += ResolveCityActor() != nullptr
			? ResolveCityActor()->GetActorTransform().TransformVector(LocalDeckOffset)
			: LocalDeckOffset;
	}
	return true;
}

int32 ASimCopterAmbientVehiclesActor::TileDistanceToCamera(const FIntPoint& Tile) const
{
	// FUN_004b2630 / FUN_004b4660 wrap the tile delta into [-128, 128] before taking the
	// Chebyshev distance.
	FIntPoint Camera(INDEX_NONE, INDEX_NONE);
	if (!TryGetCameraTile(Camera))
	{
		return 0;
	}

	auto Wrap = [](int32 Delta)
	{
		if (Delta > 0x80) Delta -= ((Delta + 0x7f) & ~0xff);
		if (Delta < -0x80) Delta += ((0x7f - Delta) & ~0xff);
		return FMath::Abs(Delta);
	};

	return FMath::Max(Wrap(Camera.X - Tile.X), Wrap(Camera.Y - Tile.Y));
}

int32 ASimCopterAmbientVehiclesActor::GetDifficultyTier() const
{
	if (const ASimCopterMissionSystemActor* Mission = ResolveMissionSystem())
	{
		return Mission->GetMissionDifficultyTier();
	}
	return 1;
}

// ---------------------------------------------------------------------------------------------
// Mesh plumbing
// ---------------------------------------------------------------------------------------------

UProceduralMeshComponent* ASimCopterAmbientVehiclesActor::CreateVehicleMesh(const int32 ObjectId, const TCHAR* Name)
{
	const FString RootPath = ResolveOriginalGameRoot();
	if (RootPath.IsEmpty())
	{
		return nullptr;
	}

	if (!MeshLibrary.IsValid())
	{
		TSharedPtr<FMaxisMeshLibrary> Library = MakeShared<FMaxisMeshLibrary>();
		FString Error;
		if (!Library->LoadFromOriginalGameRoot(RootPath, Error))
		{
			if (!bLoggedMeshError)
			{
				bLoggedMeshError = true;
				UE_LOG(LogSimCopterAmbientVehicles, Warning, TEXT("%s"), *Error);
			}
			return nullptr;
		}
		MeshLibrary = Library;
	}

	const TArray<FColor>* ColorMap = nullptr;
	const FMaxisMeshObject* Object = MeshLibrary->FindObjectByObjectId(ObjectId, &ColorMap);
	if (Object == nullptr)
	{
		if (!bLoggedMeshError)
		{
			bLoggedMeshError = true;
			UE_LOG(LogSimCopterAmbientVehicles, Warning,
				TEXT("GEO object 0x%03x not found in '%s'; that ambient vehicle stays hidden."),
				ObjectId, *RootPath);
		}
		return nullptr;
	}

	FMaxisMeshSection Section;
	FMaxisProceduralMeshBuilder::BuildPaletteColoredSection(
		*Object,
		ColorMap,
		AmbientModelUnitsPerCentimeter,
		GetTileSizeCm() / AmbientModelSourceTileSize,
		bAmbientRenderBackfaces,
		FLinearColor(0.72f, 0.72f, 0.75f),
		Section);
	if (Section.IsEmpty())
	{
		return nullptr;
	}

	// Remember how tall the model is so a rider can be put on its roof rather than at its pivot.
	ModelTopHeightCm.Add(ObjectId, static_cast<float>(Section.LocalBounds.Max.Z));

	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this, FName(Name));
	if (Mesh == nullptr)
	{
		return nullptr;
	}

	Mesh->SetupAttachment(GetRootComponent());
	Mesh->bUseAsyncCooking = false;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->RegisterComponent();
	Mesh->CreateMeshSection_LinearColor(
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
		Mesh->SetMaterial(0, VertexColorMaterial);
	}
	Mesh->SetVisibility(false);

	OwnedMeshes.Add(Mesh);
	return Mesh;
}

void ASimCopterAmbientVehiclesActor::EnsurePools()
{
	if (bPoolsInitialized)
	{
		return;
	}

	// Nothing can be placed until the city grid is up.
	if (ResolveTrafficSystem() == nullptr || ResolveCityActor() == nullptr)
	{
		return;
	}

	// FUN_004b3a10: the first plane gets PLANE1, every later one the UFO.
	Planes[0].ObjectId = SimCopterAmbientVehicles::PlaneObjectId;
	Planes[1].ObjectId = SimCopterAmbientVehicles::UfoObjectId;
	for (FSimCopterAmbientPlane& Plane : Planes)
	{
		Plane.Mesh = CreateVehicleMesh(Plane.ObjectId,
			Plane.ObjectId == SimCopterAmbientVehicles::UfoObjectId ? TEXT("Ufo") : TEXT("Plane"));
		Plane.SpeedCmPerSec = PlaneCruiseUnitsPerSec * GetCmPerOriginalUnit();
		// FUN_004b37e0 arms +0x34 at the full delay, so the first respawn happens immediately.
		Plane.RespawnAccumSeconds = PlaneRespawnDelaySeconds;
		Plane.EffectTimerSeconds = UfoEffectIntervalSeconds;
	}

	// FUN_004af6f0: slot 0 is CAPBOAT1, slots 1..2 are the ambient BOAT1s.
	for (int32 Index = 0; Index < SimCopterAmbientVehicles::BoatSlots; ++Index)
	{
		FSimCopterAmbientBoat& Boat = Boats[Index];
		Boat.ObjectId = (Index == 0)
			? SimCopterAmbientVehicles::CapsizedBoatObjectId
			: SimCopterAmbientVehicles::BoatObjectId;
		Boat.Mesh = CreateVehicleMesh(Boat.ObjectId, *FString::Printf(TEXT("Boat%d"), Index));
		DrawBoatSpeed(Boat);
		Boat.RespawnAccumSeconds = BoatRespawnDelaySeconds;
	}

	Train.LocoMesh = CreateVehicleMesh(SimCopterAmbientVehicles::TrainLocoObjectId, TEXT("TrainLoco"));
	for (int32 Index = 0; Index < SimCopterAmbientVehicles::TrainCarCount; ++Index)
	{
		Train.CarMeshes[Index] = CreateVehicleMesh(
			SimCopterAmbientVehicles::TrainCarObjectIds[Index],
			*FString::Printf(TEXT("TrainCar%d"), Index));
	}
	// FUN_004b7bf0: (((rand & 7) + 0x56) << 16) * 4 / 5 units per second.
	Train.BaseSpeedCmPerSec =
		((RandomStream.RandRange(0, 7) + 0x56) * 4.0f / 5.0f) * GetCmPerOriginalUnit();
	Train.SpeedCmPerSec = Train.BaseSpeedCmPerSec;
	Train.RespawnAccumSeconds = TrainRespawnDelaySeconds;

	bPoolsInitialized = true;

	UE_LOG(LogSimCopterAmbientVehicles, Display,
		TEXT("Pools ready: PLANE1=%s UFO=%s CAPBOAT1=%s BOAT1=%s/%s TRAIN1=%s TRAIN2=%s TRAIN3=%s"),
		Planes[0].Mesh != nullptr ? TEXT("ok") : TEXT("MISSING"),
		Planes[1].Mesh != nullptr ? TEXT("ok") : TEXT("MISSING"),
		Boats[0].Mesh != nullptr ? TEXT("ok") : TEXT("MISSING"),
		Boats[1].Mesh != nullptr ? TEXT("ok") : TEXT("MISSING"),
		Boats[2].Mesh != nullptr ? TEXT("ok") : TEXT("MISSING"),
		Train.LocoMesh != nullptr ? TEXT("ok") : TEXT("MISSING"),
		Train.CarMeshes[0] != nullptr ? TEXT("ok") : TEXT("MISSING"),
		Train.CarMeshes[1] != nullptr ? TEXT("ok") : TEXT("MISSING"));
}

float ASimCopterAmbientVehiclesActor::GetModelTopHeightCm(const int32 ObjectId) const
{
	const float* Found = ModelTopHeightCm.Find(ObjectId);
	return Found != nullptr ? *Found : 0.0f;
}

bool ASimCopterAmbientVehiclesActor::IsMissionEventActive(const int32 EventId) const
{
	if (EventId == INDEX_NONE)
	{
		return false;
	}
	if (const ASimCopterMissionSystemActor* Mission = ResolveMissionSystem())
	{
		return Mission->IsMissionEventActive(EventId);
	}
	return false;
}

FSimCopterVehicleWreck* ASimCopterAmbientVehiclesActor::AddWreck(
	const int32 ObjectId,
	const FVector& World,
	const FVector& Direction,
	const float ExtraYawDegrees,
	const int32 EventId,
	const bool bBurning,
	const float BurnTimeoutSeconds)
{
	UProceduralMeshComponent* Mesh = CreateVehicleMesh(ObjectId, *FString::Printf(TEXT("Wreck%d"), NextWreckKey));
	if (Mesh == nullptr)
	{
		return nullptr;
	}

	FSimCopterVehicleWreck Wreck;
	Wreck.Mesh = Mesh;
	Wreck.ObjectId = ObjectId;
	Wreck.EventId = EventId;
	// A key space of its own, above the flame slots and the burning-car ids.
	Wreck.Key = 0x50000000 | (NextWreckKey++ & 0x0FFFFFFF);
	Wreck.bBurning = bBurning;
	Wreck.BurnTimeoutSeconds = BurnTimeoutSeconds;
	Wreck.World = World;
	Wreck.Direction = Direction;
	Wreck.ExtraYawDegrees = ExtraYawDegrees;

	SetMeshTransform(Mesh, World, Direction, ExtraYawDegrees);
	Wrecks.Add(Wreck);
	return &Wrecks.Last();
}

void ASimCopterAmbientVehiclesActor::DestroyWreck(FSimCopterVehicleWreck& Wreck)
{
	if (Wreck.Mesh != nullptr)
	{
		OwnedMeshes.Remove(Wreck.Mesh);
		Wreck.Mesh->DestroyComponent();
		Wreck.Mesh = nullptr;
	}
}

void ASimCopterAmbientVehiclesActor::ClearWrecksForEvent(const int32 EventId)
{
	for (int32 Index = Wrecks.Num() - 1; Index >= 0; --Index)
	{
		if (Wrecks[Index].EventId == EventId)
		{
			DestroyWreck(Wrecks[Index]);
			Wrecks.RemoveAt(Index);
		}
	}
}

void ASimCopterAmbientVehiclesActor::UpdateWrecks(const float DeltaSeconds)
{
	ASimCopterMissionSystemActor* Mission = ResolveMissionSystem();

	for (int32 Index = Wrecks.Num() - 1; Index >= 0; --Index)
	{
		FSimCopterVehicleWreck& Wreck = Wrecks[Index];

		// A wreck that is still alight when its mission's clock runs out counts as burned, which
		// is the same accounting a car that burns up gets - it docks points and lets the record
		// close instead of pinning it open forever.
		if (Wreck.bBurning && Wreck.BurnTimeoutSeconds > 0.0f)
		{
			Wreck.BurnTimeoutSeconds -= DeltaSeconds;
			if (Wreck.BurnTimeoutSeconds <= 0.0f)
			{
				Wreck.bBurning = false;
				if (Mission != nullptr)
				{
					Mission->PostMissionEvent(SimCopterMissions::EVT_CarBurned, Wreck.EventId, 1, false);
				}
			}
		}

		// Once the mission that owns it is finished - completed, failed or expired - the wreckage
		// is cleared away. Anything still burning is settled first.
		if (!IsMissionEventActive(Wreck.EventId))
		{
			DestroyWreck(Wreck);
			Wrecks.RemoveAt(Index);
		}
	}
}

void ASimCopterAmbientVehiclesActor::GetBurningWrecks(TArray<FSimCopterBurningVehicle>& Out) const
{
	for (const FSimCopterVehicleWreck& Wreck : Wrecks)
	{
		if (!Wreck.bBurning)
		{
			continue;
		}
		FSimCopterBurningVehicle Burning;
		Burning.Key = Wreck.Key;
		Burning.EventId = Wreck.EventId;
		Burning.World = Wreck.World;
		Out.Add(Burning);
	}
}

void ASimCopterAmbientVehiclesActor::DouseBurningWrecksNear(
	const FVector& WorldLocation,
	const float RadiusCm,
	TArray<int32>& OutExtinguishedEventIds)
{
	const float RadiusSq = RadiusCm * RadiusCm;
	for (FSimCopterVehicleWreck& Wreck : Wrecks)
	{
		if (!Wreck.bBurning || FVector::DistSquared(Wreck.World, WorldLocation) > RadiusSq)
		{
			continue;
		}
		Wreck.bBurning = false;
		Wreck.BurnTimeoutSeconds = 0.0f;
		OutExtinguishedEventIds.Add(Wreck.EventId);
	}
}

void ASimCopterAmbientVehiclesActor::SetMeshTransform(
	UProceduralMeshComponent* Mesh,
	const FVector& World,
	const FVector& Direction,
	const float ExtraYawDegrees,
	const float ExtraPitchDegrees)
{
	if (Mesh == nullptr)
	{
		return;
	}

	FRotator Rotation = Direction.IsNearlyZero() ? FRotator::ZeroRotator : Direction.Rotation();
	Rotation.Yaw += ExtraYawDegrees;
	Rotation.Pitch += ExtraPitchDegrees;
	Mesh->SetWorldLocationAndRotation(World, Rotation);
	if (!Mesh->IsVisible())
	{
		Mesh->SetVisibility(true);
	}
}

// ---------------------------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------------------------

void ASimCopterAmbientVehiclesActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	EnsurePools();
	if (!bPoolsInitialized)
	{
		return;
	}
	EnsureRailScan();

	// FUN_0047a760's order: boats (FUN_004b1800), planes (FUN_004b3b80), train (FUN_004b7f40).
	if (bEnableBoats)
	{
		for (FSimCopterAmbientBoat& Boat : Boats)
		{
			UpdateBoat(Boat, DeltaSeconds);
		}
	}
	if (bEnablePlanes)
	{
		for (FSimCopterAmbientPlane& Plane : Planes)
		{
			UpdatePlane(Plane, DeltaSeconds);
		}
	}
	if (bEnableTrain)
	{
		UpdateTrain(DeltaSeconds);
		UpdateTrainRoofRiders();
	}

	UpdateWrecks(DeltaSeconds);
	UpdateAmbientVehicleAudio();
}

// SCHOOK: AmbientVehicleSound 0x004b23e0 (planes/UFO) and 0x004b4570 (train)
//
// Both are the same shape as the siren mixer: one voice per vehicle kind, started as a 3D loop
// when the vehicle is inside the 1920-unit radius and stopped when it leaves, with the volume
// re-derived from distance every tick.
//
// The plane pair is worth spelling out, because the names mislead. CESSLP1 (0x1c) is the plane's
// ENGINE and plays whenever it is in range; DIVE1 (0x1b) is layered on top only while +0x07 - the
// crashing flag - is set. DIVE1 is the dive, not "the other aircraft".
void ASimCopterAmbientVehiclesActor::UpdateAmbientVehicleAudio()
{
	USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this);
	if (Audio == nullptr)
	{
		return;
	}

	const FVector Listener = Audio->GetListenerLocation();
	const float RangeCm =
		USimCopterAudioSubsystem::AudibleRangeUnits * USimCopterAudioSubsystem::OriginalUnitToCm;

	auto DriveLoop = [Audio, Listener, RangeCm](int32 SoundId, bool bLive, const FVector& World)
	{
		if (!bLive || FVector::Dist(World, Listener) >= RangeCm)
		{
			if (Audio->IsPlaying(SoundId))
			{
				Audio->Stop(SoundId);
			}
			return;
		}
		Audio->Play3D(SoundId, World, SimCopterSoundFlags::Loop);
		const float DistanceUnits =
			static_cast<float>(FVector::Dist(World, Listener)) / USimCopterAudioSubsystem::OriginalUnitToCm;
		Audio->SetVolumeAdjust(
			SoundId,
			USimCopterAudioSubsystem::DistanceVolumeIndex(DistanceUnits) - 10000);
	};

	bool bPlaneLive = false;
	bool bPlaneDiving = false;
	FVector PlaneWorld = FVector::ZeroVector;
	bool bUfoLive = false;
	FVector UfoWorld = FVector::ZeroVector;

	for (const FSimCopterAmbientPlane& Plane : Planes)
	{
		if (!Plane.bVisible)
		{
			continue;
		}
		// Slot 1 is the UFO, not a second aircraft - see the header note.
		if (Plane.ObjectId == SimCopterAmbientVehicles::UfoObjectId)
		{
			bUfoLive = true;
			UfoWorld = Plane.World;
		}
		else
		{
			bPlaneLive = true;
			PlaneWorld = Plane.World;
			bPlaneDiving = Plane.bCrashing;
		}
	}

	DriveLoop(SimCopterSound::SND_CESSLP1, bPlaneLive, PlaneWorld);
	DriveLoop(SimCopterSound::SND_DIVE1, bPlaneLive && bPlaneDiving, PlaneWorld);
	DriveLoop(SimCopterSound::SND_UFO, bUfoLive, UfoWorld);
	DriveLoop(SimCopterSound::SND_TRAIN1, Train.bVisible, Train.World);
}

// ---------------------------------------------------------------------------------------------
// Planes - FUN_004b2330 / FUN_004b2630 / FUN_004b3420 / FUN_004b3530 / FUN_004b2910 / FUN_004b2ab0
// ---------------------------------------------------------------------------------------------

void ASimCopterAmbientVehiclesActor::UpdatePlane(FSimCopterAmbientPlane& Plane, const float DeltaSeconds)
{
	const bool bIsUfo = Plane.ObjectId == SimCopterAmbientVehicles::UfoObjectId;

	if (!Plane.bVisible)
	{
		// FUN_004b2330: the UFO also needs its enable flag (DAT_00504084) and to be under its
		// ten-hit retirement count.
		if (bIsUfo && (!bEnableUfo || Plane.HitCount > 9))
		{
			return;
		}

		Plane.RespawnAccumSeconds += DeltaSeconds;
		if (!Plane.bCrashing && Plane.RespawnAccumSeconds > PlaneRespawnDelaySeconds)
		{
			RespawnPlaneAtViewEdge(Plane);
		}
		if (!Plane.bVisible)
		{
			return;
		}
	}

	// FUN_004b2630: drop out of the world once it wanders past (viewRange >> 1) + 4 tiles.
	if (!Plane.bCrashing && !Plane.bCrashRequested &&
		TileDistanceToCamera(Plane.Tile) > (ViewRangeTiles >> 1) + 4)
	{
		HidePlane(Plane);
		return;
	}

	if (bIsUfo)
	{
		// FUN_004b2630's first act on a non-PLANE1 aircraft is FUN_004c0d10(itself): the abduction
		// roll. It runs once per original simulation tick, so the remake meters it to OriginalSimHz
		// rather than per frame - at 1-in-16250 a frame the frame rate would set how often the UFO
		// takes anyone.
		UfoBeamTickAccumSeconds += DeltaSeconds;
		const float BeamTickSeconds = 1.0f / SimCopterAmbientVehicles::OriginalSimHz;
		int32 BeamTicks = FMath::FloorToInt(UfoBeamTickAccumSeconds / BeamTickSeconds);
		UfoBeamTickAccumSeconds -= BeamTicks * BeamTickSeconds;
		BeamTicks = FMath::Min(BeamTicks, 4); // don't burst the roll after a hitch
		if (ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
		{
			for (int32 BeamTick = 0; BeamTick < BeamTicks; ++BeamTick)
			{
				// The saucer itself, not the pool actor: an abductee flies to where the mesh is.
				const int32 Taken = TrafficSystem->TryBeamPeopleUp(Plane.Mesh);
				if (Taken > 0)
				{
					UE_LOG(LogSimCopterAmbientVehicles, Log,
						TEXT("UFO beamed up %d %s."), Taken, Taken == 1 ? TEXT("person") : TEXT("people"));
				}
			}
		}

		Plane.EffectTimerSeconds -= DeltaSeconds;
		if (Plane.EffectTimerSeconds < 0.0f && !Plane.bCrashing && !Plane.bCrashRequested)
		{
			Plane.EffectTimerSeconds = UfoEffectIntervalSeconds;
			if (EffectComponent != nullptr)
			{
				// FUN_0048e0b0 type 0xb, the UFO's periodic beam puff.
				EffectComponent->SpawnEffect(
					ESimCopterEffectType::GeoSmoke,
					Plane.World,
					FVector(0.0f, 0.0f, -60.0f));
			}
		}
	}

	if (Plane.bCrashRequested)
	{
		BeginPlaneCrash(Plane);
	}

	if (Plane.bCrashing)
	{
		UpdatePlaneCrash(Plane, DeltaSeconds);
		return;
	}

	// FUN_004b2ab0's cruise branch: advance along the segment, then FUN_004b3420 picks the next.
	const float StepCm = Plane.SpeedCmPerSec * DeltaSeconds;
	const float Applied = FMath::Min(StepCm, Plane.SegmentRemainingCm);
	Plane.SegmentRemainingCm -= Applied;
	Plane.World += Plane.Direction * Applied;
	Plane.World.Z += Plane.VerticalSpeedCmPerSec * DeltaSeconds;

	if (Plane.SegmentRemainingCm <= 0.0f)
	{
		const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
		int32 TileX = INDEX_NONE;
		int32 TileY = INDEX_NONE;
		if (TrafficSystem != nullptr &&
			TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(Plane.World, TileX, TileY))
		{
			Plane.Tile = FIntPoint(TileX, TileY);
		}
		StartPlaneSegment(Plane);
	}

	SetMeshTransform(Plane.Mesh, Plane.World, Plane.Direction);
}

bool ASimCopterAmbientVehiclesActor::RespawnPlaneAtViewEdge(FSimCopterAmbientPlane& Plane)
{
	// FUN_004b3530: pick one of four quadrants off the camera and place the plane half the draw
	// distance away, pointed back across the camera tile with a random yaw offset.
	Plane.RespawnAccumSeconds = 0.0f;

	FIntPoint Camera(INDEX_NONE, INDEX_NONE);
	if (!TryGetCameraTile(Camera))
	{
		return false;
	}

	const int32 Radius = FMath::Max(4, ViewRangeTiles >> 1);
	const int32 Quadrant = RandomStream.RandRange(0, 3);
	static const FIntPoint Offsets[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
	const FIntPoint Candidate(
		FMath::Clamp(Camera.X + Offsets[Quadrant].X * Radius, 3, FSimCity2000City::MapSize - 4),
		FMath::Clamp(Camera.Y + Offsets[Quadrant].Y * Radius, 3, FSimCity2000City::MapSize - 4));

	FVector TileCenter = FVector::ZeroVector;
	FVector CameraCenter = FVector::ZeroVector;
	if (!TryGetTileCenter(Candidate, TileCenter) || !TryGetTileCenter(Camera, CameraCenter))
	{
		return false;
	}

	Plane.Tile = Candidate;
	Plane.World = TileCenter;
	Plane.World.Z += PlaneCruiseAltitudeUnits * GetCmPerOriginalUnit();

	// FUN_004b3530 heads for the camera tile and then rotates by (300 - rand % 600) tenth-degrees.
	FVector Toward = CameraCenter - Plane.World;
	Toward.Z = 0.0f;
	Plane.Direction = Toward.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	const float YawOffsetDegrees = (300.0f - static_cast<float>(RandomStream.RandRange(0, 599))) * 0.1f;
	Plane.Direction = Plane.Direction.RotateAngleAxis(YawOffsetDegrees, FVector::UpVector);

	Plane.bVisible = true;
	Plane.bCrashing = false;
	Plane.RespawnAccumSeconds = 0.0f;
	Plane.HitCount = 0;
	StartPlaneSegment(Plane);
	SetMeshTransform(Plane.Mesh, Plane.World, Plane.Direction);
	return true;
}

void ASimCopterAmbientVehiclesActor::StartPlaneSegment(FSimCopterAmbientPlane& Plane)
{
	// FUN_004b3420 only wanders the non-PLANE1 object - the airliner holds its heading and the
	// UFO turns by (800 - rand % 1600) tenth-degrees once in every eight segments.
	if (Plane.ObjectId != SimCopterAmbientVehicles::PlaneObjectId && RandomStream.RandRange(0, 7) == 0)
	{
		const float YawDegrees = (800.0f - static_cast<float>(RandomStream.RandRange(0, 1599))) * 0.1f;
		Plane.Direction = Plane.Direction.RotateAngleAxis(YawDegrees, FVector::UpVector);
	}

	Plane.SegmentRemainingCm = PlaneSegmentUnits * GetCmPerOriginalUnit();

	// FUN_004b3420's altitude term: hold the cruise height above the ground below, closing the
	// error by 1/16 of it per original simulation frame. Off the 128x128 grid there is no cell to
	// read a height from, so the plane holds its altitude rather than chasing a target measured
	// from its own position - which would make it climb away forever.
	Plane.VerticalSpeedCmPerSec = 0.0f;
	if (const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		float TerrainZ = 0.0f;
		if (TrafficSystem->TryGetTerrainWorldZAtWorldLocation(Plane.World, TerrainZ))
		{
			const float TargetZ = TerrainZ + PlaneCruiseAltitudeUnits * GetCmPerOriginalUnit();
			Plane.VerticalSpeedCmPerSec =
				((TargetZ - Plane.World.Z) / 16.0f) * SimCopterAmbientVehicles::OriginalSimHz;
		}
	}
}

void ASimCopterAmbientVehiclesActor::BeginPlaneCrash(FSimCopterAmbientPlane& Plane)
{
	// FUN_004b2910. The UFO settles its own award the moment it starts down.
	if (Plane.ObjectId == SimCopterAmbientVehicles::UfoObjectId)
	{
		if (ASimCopterMissionSystemActor* Mission = ResolveMissionSystem())
		{
			Mission->PostMissionEvent(SimCopterMissions::EVT_UfoResolved, INDEX_NONE, 1, false);
		}
	}

	if (!Plane.bVisible)
	{
		// Nothing under it yet - the original waits for FUN_0049ad30 to find a valid cell.
		return;
	}

	FVector Below = Plane.World;
	if (const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		float TerrainZ = 0.0f;
		if (TrafficSystem->TryGetTerrainWorldZAtWorldLocation(Plane.World, TerrainZ))
		{
			Below.Z = TerrainZ;
		}
	}

	if (Plane.ObjectId == SimCopterAmbientVehicles::UfoObjectId)
	{
		// The UFO takes a random (1 - rand%3, -1, 1 - rand%3) dive.
		Plane.Direction = FVector(
			static_cast<float>(1 - RandomStream.RandRange(0, 2)),
			static_cast<float>(1 - RandomStream.RandRange(0, 2)),
			-1.0f).GetSafeNormal(UE_SMALL_NUMBER, FVector::DownVector);
	}
	else
	{
		// The airliner aims for the cell centre underneath it.
		Plane.Direction = (Below - Plane.World).GetSafeNormal(UE_SMALL_NUMBER, FVector::DownVector);
	}

	Plane.bCrashing = true;
	Plane.bCrashRequested = false;
}

void ASimCopterAmbientVehiclesActor::UpdatePlaneCrash(FSimCopterAmbientPlane& Plane, const float DeltaSeconds)
{
	const FVector Next = Plane.World + Plane.Direction * (Plane.SpeedCmPerSec * DeltaSeconds);

	const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	int32 TileX = INDEX_NONE;
	int32 TileY = INDEX_NONE;
	float SurfaceZ = Next.Z;
	if (TrafficSystem != nullptr &&
		TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(Next, TileX, TileY))
	{
		Plane.Tile = FIntPoint(TileX, TileY);
		float TerrainZ = 0.0f;
		if (TrafficSystem->TryGetTerrainWorldZAtWorldLocation(Next, TerrainZ))
		{
			SurfaceZ = TerrainZ;
		}
	}

	// FUN_004b2cd0 ray-tests the step against the cell's contents first; the remake settles for
	// the surface below the plane, which is where a dive at this angle lands anyway.
	if (Next.Z <= SurfaceZ)
	{
		FVector Impact = Next;
		Impact.Z = SurfaceZ;
		Plane.World = Impact;
		ResolvePlaneImpact(Plane, Plane.Tile);
		return;
	}

	Plane.World = Next;
	SetMeshTransform(Plane.Mesh, Plane.World, Plane.Direction);
}

void ASimCopterAmbientVehiclesActor::ResolvePlaneImpact(FSimCopterAmbientPlane& Plane, const FIntPoint& ImpactTile)
{
	ASimCopterMissionSystemActor* Mission = ResolveMissionSystem();
	const int32 CrashEventId = Plane.EventId;
	const FVector ImpactWorld = Plane.World;
	// Nose-down and slewed: it did not land, it arrived.
	FVector WreckDirection = Plane.Direction;
	WreckDirection.Z = 0.0f;
	WreckDirection = WreckDirection.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	const int32 WreckObjectId = Plane.ObjectId;

	SpawnCrashDebris(ImpactWorld, CrashEventId, 3 + RandomStream.RandRange(0, 2));
	HidePlane(Plane);
	Plane.bCrashing = false;
	Plane.bCrashRequested = false;
	Plane.EventId = INDEX_NONE;
	Plane.RespawnAccumSeconds = 0.0f;

	if (Mission == nullptr)
	{
		return;
	}

	// FUN_004b2cd0's three endings. In all of them the airframe stays where it hit - the wreck is
	// the thing the player flies to - and is cleared with whichever mission ends up owning it.
	if (IsWaterTile(ImpactTile))
	{
		// Terrain class below 10 - the airliner ditched, so the survivors become a boat rescue and
		// the plane's own background record retires without scoring. The hull floats at the crash
		// site until they are all out of the water.
		const int32 RescueEventId =
			Mission->CreateMissionAt(ImpactTile.X, ImpactTile.Y, SimCopterMissions::TYPE_BoatRescue);
		AddWreck(WreckObjectId, ImpactWorld, WreckDirection, 0.0f, RescueEventId, false, 0.0f);
		Mission->PostMissionEvent(SimCopterMissions::EVT_SetCategory, CrashEventId, SimCopterMissions::CAT_ExpireSilently, false);
		return;
	}

	if (Mission->CanIgniteCrashSite(ImpactTile.X, ImpactTile.Y))
	{
		const int32 FireEventId =
			Mission->CreateMissionAt(ImpactTile.X, ImpactTile.Y, SimCopterMissions::TYPE_BuildingFire);
		if (FireEventId != INDEX_NONE)
		{
			// The building fire it started owns the scene now; the burning airframe is part of it
			// and goes when the fire is out.
			AddWreck(WreckObjectId, ImpactWorld, WreckDirection, 0.0f, FireEventId, true, 0.0f);
			Mission->PostMissionEvent(SimCopterMissions::EVT_SetCategory, CrashEventId, SimCopterMissions::CAT_ExpireSilently, false);
			return;
		}
	}

	// Nothing to set alight - open ground. The background plane-crash record becomes the live
	// mission at the impact tile, and the burning airframe is what the player has to put out.
	Mission->PostMissionEvent(SimCopterMissions::EVT_SetCategory, CrashEventId, SimCopterMissions::CAT_Active, false);
	Mission->PostMissionEventAt(SimCopterMissions::EVT_SetPrimaryCoords, CrashEventId, ImpactTile.X, ImpactTile.Y, 0, false);
	BeginWreckFire(CrashEventId, 1);
	AddWreck(WreckObjectId, ImpactWorld, WreckDirection, 0.0f, CrashEventId, true, WreckBurnTimeoutSeconds);
}

void ASimCopterAmbientVehiclesActor::BeginWreckFire(const int32 EventId, const int32 WreckCount)
{
	// Burning wreckage is accounted for with the mission layer's own burning-vehicle counters:
	// bit 0x400 plus one EVT_CarCrashed per wreck. FUN_004a73e0 will not complete a record while
	// CarsDoused + CarsBurned is short of CarsCrashed, which is exactly the "the fires stay lit
	// until the player deals with them" rule we want, and the douse pays through the decoded
	// [Fire Miss] car controls rather than an invented reward.
	ASimCopterMissionSystemActor* Mission = ResolveMissionSystem();
	if (Mission == nullptr || EventId == INDEX_NONE || WreckCount <= 0)
	{
		return;
	}

	Mission->PromoteMissionType(EventId, SimCopterMissions::TYPE_CarFire);
	Mission->PostMissionEvent(SimCopterMissions::EVT_CarCrashed, EventId, WreckCount, true);
}

void ASimCopterAmbientVehiclesActor::HidePlane(FSimCopterAmbientPlane& Plane)
{
	// FUN_004b3530 zeroes +0x34 at the start of the respawn, not when the plane is unlinked.
	Plane.bVisible = false;
	if (Plane.Mesh != nullptr)
	{
		Plane.Mesh->SetVisibility(false);
	}
}

bool ASimCopterAmbientVehiclesActor::TryActivatePlaneCrash(const int32 EventId)
{
	EnsurePools();

	// FUN_004b3aa0: only PLANE1, and only when it is not already going down.
	for (FSimCopterAmbientPlane& Plane : Planes)
	{
		if (Plane.ObjectId != SimCopterAmbientVehicles::PlaneObjectId)
		{
			continue;
		}
		if (Plane.bCrashRequested || Plane.bCrashing)
		{
			return false;
		}

		// Divergence, for the same reason as the train crash: the original arms the flag and lets
		// the dive wait for the next time the plane is in the world, which in the remake can be a
		// minute of nothing. A plane can come down anywhere, so if it is not up, put it up.
		if (!Plane.bVisible && !RespawnPlaneAtViewEdge(Plane))
		{
			return false;
		}

		Plane.bCrashRequested = true;
		Plane.EventId = EventId;
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Boats - FUN_004af770 / FUN_004b10a0 / FUN_004b0cf0 / FUN_004b1950 / FUN_004b2150
// ---------------------------------------------------------------------------------------------

void ASimCopterAmbientVehiclesActor::DrawBoatSpeed(FSimCopterAmbientBoat& Boat)
{
	// FUN_004b14f0: ((rand & 7) + 10) units/s, and CAPBOAT1 divides that by (5 - difficulty).
	float Units = static_cast<float>(RandomStream.RandRange(0, 7) + 10);
	if (Boat.ObjectId == SimCopterAmbientVehicles::CapsizedBoatObjectId)
	{
		Units /= static_cast<float>(FMath::Max(1, 5 - GetDifficultyTier()));
	}
	Boat.BaseSpeedCmPerSec = Units * GetCmPerOriginalUnit();
	Boat.SpeedCmPerSec = Boat.BaseSpeedCmPerSec;
}

void ASimCopterAmbientVehiclesActor::UpdateBoat(FSimCopterAmbientBoat& Boat, const float DeltaSeconds)
{
	const bool bCapsized = Boat.ObjectId == SimCopterAmbientVehicles::CapsizedBoatObjectId;

	if (!Boat.bVisible)
	{
		// FUN_004b1800: only the ambient boats come back on their own.
		if (bCapsized)
		{
			return;
		}
		Boat.RespawnAccumSeconds += DeltaSeconds;
		if (Boat.RespawnAccumSeconds > BoatRespawnDelaySeconds)
		{
			RespawnBoatAtViewEdge(Boat);
		}
		if (!Boat.bVisible)
		{
			return;
		}
	}

	if (bCapsized)
	{
		// The rescue is over - everyone delivered, or the record failed and was retired. Nothing
		// left to circle: the hull goes with it.
		if (Boat.EventId != INDEX_NONE && !IsMissionEventActive(Boat.EventId))
		{
			HideBoat(Boat);
			Boat.EventId = INDEX_NONE;
			Boat.MissionTimerSeconds = 0.0f;
			return;
		}

		// FUN_004af770's CAPBOAT1 branch: float on the water and burn down the mission timer.
		float SurfaceZ = Boat.World.Z;
		if (TryGetWaterSurfaceZ(Boat.World, SurfaceZ))
		{
			Boat.World.Z = SurfaceZ;
		}
		Boat.MissionTimerSeconds -= DeltaSeconds;
		if (Boat.MissionTimerSeconds < 0.0f)
		{
			SinkBoat(Boat);
			return;
		}
	}
	else if (TileDistanceToCamera(Boat.Tile) > (ViewRangeTiles >> 1) + 4)
	{
		HideBoat(Boat);
		return;
	}

	// FUN_004afb60: a low helicopter over the boat's tile makes it run.
	Boat.SpeedCmPerSec = Boat.BaseSpeedCmPerSec;
	if (const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
	{
		const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
		int32 PlayerX = INDEX_NONE;
		int32 PlayerY = INDEX_NONE;
		if (TrafficSystem != nullptr &&
			TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(Player->GetActorLocation(), PlayerX, PlayerY) &&
			FIntPoint(PlayerX, PlayerY) == Boat.Tile)
		{
			const float ScareCeilingCm = BoatHelicopterScareAltitudeUnits * GetCmPerOriginalUnit();
			const float AltitudeCm = Player->GetActorLocation().Z - Boat.World.Z;
			if (AltitudeCm >= 0.0f && AltitudeCm <= ScareCeilingCm)
			{
				const float Alpha = 1.0f - (AltitudeCm / ScareCeilingCm);
				Boat.SpeedCmPerSec += BoatHelicopterScareBoostUnits * GetCmPerOriginalUnit() * Alpha;
			}
		}
	}

	// FUN_004af770's wake: an ambient boat drops a small spray behind it, the capsized one puffs.
	Boat.WakeTimerSeconds -= DeltaSeconds;
	if (Boat.WakeTimerSeconds < 0.0f)
	{
		Boat.WakeTimerSeconds = BoatWakeIntervalSeconds;
		if (EffectComponent != nullptr)
		{
			EffectComponent->SpawnEffect(
				bCapsized ? ESimCopterEffectType::Smoke : ESimCopterEffectType::SmallSpray,
				Boat.World,
				-Boat.Direction * Boat.SpeedCmPerSec * 0.5f,
				20.0f * GetCmPerOriginalUnit());
		}
	}

	if (Boat.TargetTile.X == INDEX_NONE)
	{
		ChooseBoatTarget(Boat);
	}

	const float StepCm = Boat.SpeedCmPerSec * DeltaSeconds;
	Boat.DistanceToTargetCm -= StepCm;
	Boat.World += Boat.Direction * StepCm;
	float SurfaceZ = Boat.World.Z;
	if (TryGetWaterSurfaceZ(Boat.World, SurfaceZ))
	{
		Boat.World.Z = SurfaceZ;
	}

	// FUN_004b00a0 promotes the boat into the tile it has crossed into.
	if (Boat.DistanceToTargetCm <= 0.0f)
	{
		Boat.PreviousTile = Boat.Tile;
		Boat.Tile = Boat.TargetTile;
		ChooseBoatTarget(Boat);
	}

	SetMeshTransform(Boat.Mesh, Boat.World, Boat.Direction);
}

void ASimCopterAmbientVehiclesActor::ChooseBoatTarget(FSimCopterAmbientBoat& Boat)
{
	// FUN_004b0150 collects the water neighbours and FUN_004b06c0 picks one; the boat only turns
	// back when there is nowhere else to go.
	static const FIntPoint Steps[4] = { FIntPoint(0, -1), FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(-1, 0) };

	TArray<FIntPoint, TInlineAllocator<4>> Candidates;
	TArray<FIntPoint, TInlineAllocator<4>> Fallback;
	for (const FIntPoint& Step : Steps)
	{
		const FIntPoint Candidate(Boat.Tile.X + Step.X, Boat.Tile.Y + Step.Y);
		if (!IsWaterTile(Candidate))
		{
			continue;
		}
		Fallback.Add(Candidate);
		if (Candidate != Boat.PreviousTile)
		{
			Candidates.Add(Candidate);
		}
	}

	const TArray<FIntPoint, TInlineAllocator<4>>& Pool = Candidates.Num() > 0 ? Candidates : Fallback;
	if (Pool.Num() == 0)
	{
		// Landlocked (it was placed in a puddle): drop it and let the respawn find open water.
		HideBoat(Boat);
		return;
	}

	Boat.TargetTile = Pool[RandomStream.RandRange(0, Pool.Num() - 1)];

	FVector TargetCenter = FVector::ZeroVector;
	if (!TryGetTileCenter(Boat.TargetTile, TargetCenter))
	{
		HideBoat(Boat);
		return;
	}

	FVector Delta = TargetCenter - Boat.World;
	Delta.Z = 0.0f;
	Boat.DistanceToTargetCm = Delta.Size();
	Boat.Direction = Delta.GetSafeNormal(UE_SMALL_NUMBER, Boat.Direction);
}

bool ASimCopterAmbientVehiclesActor::PlaceBoatNearTile(
	FSimCopterAmbientBoat& Boat,
	const FIntPoint& Origin,
	const int32 MaxRings)
{
	// FUN_004b10a0: outward rings until a water tile turns up. CAPBOAT1 additionally needs the
	// whole 3x3 to be water, so it always lands in open water.
	const bool bCapsized = Boat.ObjectId == SimCopterAmbientVehicles::CapsizedBoatObjectId;

	for (int32 Ring = 0; Ring < MaxRings; ++Ring)
	{
		for (int32 OffsetY = -Ring; OffsetY <= Ring; ++OffsetY)
		{
			for (int32 OffsetX = -Ring; OffsetX <= Ring; ++OffsetX)
			{
				if (Ring > 0 && FMath::Abs(OffsetX) != Ring && FMath::Abs(OffsetY) != Ring)
				{
					continue;
				}

				const FIntPoint Candidate(Origin.X + OffsetX, Origin.Y + OffsetY);
				if (Candidate.X < 1 || Candidate.X >= FSimCity2000City::MapSize - 1 ||
					Candidate.Y < 1 || Candidate.Y >= FSimCity2000City::MapSize - 1)
				{
					continue;
				}
				if (bCapsized ? !IsOpenWaterTile(Candidate) : !IsWaterTile(Candidate))
				{
					continue;
				}

				FVector Center = FVector::ZeroVector;
				if (!TryGetTileCenter(Candidate, Center))
				{
					continue;
				}

				float SurfaceZ = Center.Z;
				if (TryGetWaterSurfaceZ(Center, SurfaceZ))
				{
					Center.Z = SurfaceZ;
				}

				Boat.Tile = Candidate;
				Boat.PreviousTile = FIntPoint(INDEX_NONE, INDEX_NONE);
				Boat.TargetTile = FIntPoint(INDEX_NONE, INDEX_NONE);
				Boat.World = Center;
				Boat.bVisible = true;
				Boat.RespawnAccumSeconds = 0.0f;
				Boat.WakeTimerSeconds = BoatWakeIntervalSeconds;
				DrawBoatSpeed(Boat);
				ChooseBoatTarget(Boat);
				if (Boat.bVisible)
				{
					SetMeshTransform(Boat.Mesh, Boat.World, Boat.Direction);
					return true;
				}
			}
		}
	}

	return false;
}

bool ASimCopterAmbientVehiclesActor::RespawnBoatAtViewEdge(FSimCopterAmbientBoat& Boat)
{
	// FUN_004b0cf0 walks out from a quadrant off the camera until it finds free water. It zeroes
	// the respawn accumulator on every attempt, so a failed one retries a frame later while a
	// successful-but-too-far one still waits out the full delay.
	Boat.RespawnAccumSeconds = 0.0f;

	FIntPoint Camera(INDEX_NONE, INDEX_NONE);
	if (!TryGetCameraTile(Camera))
	{
		return false;
	}

	const int32 Radius = FMath::Max(2, ViewRangeTiles >> 1);
	static const FIntPoint Offsets[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
	const FIntPoint Offset = Offsets[RandomStream.RandRange(0, 3)];
	const FIntPoint Origin(
		FMath::Clamp(Camera.X + Offset.X * Radius, 1, FSimCity2000City::MapSize - 2),
		FMath::Clamp(Camera.Y + Offset.Y * Radius, 1, FSimCity2000City::MapSize - 2));

	if (PlaceBoatNearTile(Boat, Origin, AmbientRespawnRings) &&
		TileDistanceToCamera(Boat.Tile) <= (ViewRangeTiles >> 1) + 4)
	{
		return true;
	}

	// Same divergence as the train respawn: fall back to the nearest water inside the keep-alive
	// radius rather than letting the probe strand the boat where it is dropped again next frame.
	EnsureRailScan();
	int32 BestDistance = MAX_int32;
	FIntPoint BestTile(INDEX_NONE, INDEX_NONE);
	for (const FIntPoint& Tile : WaterTiles)
	{
		const int32 Distance = TileDistanceToCamera(Tile);
		if (Distance < BestDistance && Distance <= (ViewRangeTiles >> 1) + 4)
		{
			BestDistance = Distance;
			BestTile = Tile;
		}
	}

	if (BestTile.X == INDEX_NONE)
	{
		HideBoat(Boat);
		return false;
	}
	return PlaceBoatNearTile(Boat, BestTile, 2);
}

void ASimCopterAmbientVehiclesActor::SinkBoat(FSimCopterAmbientBoat& Boat)
{
	// FUN_004b2150: the splash and puff, then the split - an ambient boat leaves a rescue behind,
	// the capsized one takes its people with it (FUN_004c3f00).
	if (EffectComponent != nullptr)
	{
		EffectComponent->SpawnSplashColumn(Boat.World);
	}

	const int32 EventId = Boat.EventId;
	const FIntPoint Tile = Boat.Tile;
	const bool bCapsized = Boat.ObjectId == SimCopterAmbientVehicles::CapsizedBoatObjectId;
	HideBoat(Boat);
	Boat.EventId = INDEX_NONE;
	Boat.MissionTimerSeconds = 0.0f;

	ASimCopterMissionSystemActor* Mission = ResolveMissionSystem();
	if (Mission == nullptr)
	{
		return;
	}

	if (bCapsized)
	{
		Mission->RemoveMissionPeople(EventId);
	}
	else
	{
		Mission->CreateMissionAt(Tile.X, Tile.Y, SimCopterMissions::TYPE_BoatRescue);
	}
}

void ASimCopterAmbientVehiclesActor::HideBoat(FSimCopterAmbientBoat& Boat)
{
	// The respawn accumulator is deliberately NOT reset here: FUN_004af770's unlink leaves +0x4f
	// alone and only the respawn attempt (FUN_004b0cf0) zeroes it.
	Boat.bVisible = false;
	Boat.TargetTile = FIntPoint(INDEX_NONE, INDEX_NONE);
	if (Boat.Mesh != nullptr)
	{
		Boat.Mesh->SetVisibility(false);
	}
}

bool ASimCopterAmbientVehiclesActor::TryActivateBoatRescue(
	const int32 EventId,
	const float TimerSeconds,
	const int32 TileX,
	const int32 TileY,
	int32& OutTileX,
	int32& OutTileY)
{
	EnsurePools();

	// FUN_004b1950 uses boat slot 0 (CAPBOAT1) and fails outright when it is already out.
	FSimCopterAmbientBoat& Boat = Boats[0];
	if (Boat.bVisible)
	{
		return false;
	}

	if (!PlaceBoatNearTile(Boat, FIntPoint(TileX, TileY), BoatPlacementRings))
	{
		return false;
	}

	Boat.EventId = EventId;
	Boat.MissionTimerSeconds = TimerSeconds;

	// 3 + rand % 3 survivors in the water beside the boat (spawn mode 1).
	const int32 Count = BoatRescueMinVictims + RandomStream.RandRange(0, BoatRescueVictimSpread - 1);
	int32 Spawned = 0;
	if (ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		Spawned = TrafficSystem->SpawnMissionSwimmersAtWorldLocation(
			Count,
			Boat.World,
			EventId,
			WaterRescueSpawnMode,
			GetTileSizeCm() * 0.30f);
	}

	if (Spawned <= 0)
	{
		HideBoat(Boat);
		Boat.EventId = INDEX_NONE;
		return false;
	}

	// The original posts EVT_SetPrimaryCoords with the boat's own tile; the core does that with
	// the tile we report back, and then EVT_RescueVictimAdded for the count.
	OutTileX = Boat.Tile.X;
	OutTileY = Boat.Tile.Y;
	if (ASimCopterMissionSystemActor* Mission = ResolveMissionSystem())
	{
		Mission->PostMissionEvent(SimCopterMissions::EVT_RescueVictimAdded, EventId, Spawned, true);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------
// Train - FUN_004b4440 / FUN_004b4660 / FUN_004b7890 / FUN_004b7f60 / FUN_004b7fd0 / FUN_004b49b0
// ---------------------------------------------------------------------------------------------

void ASimCopterAmbientVehiclesActor::UpdateTrain(const float DeltaSeconds)
{
	if (!Train.bVisible)
	{
		// FUN_004b4440: nothing comes back while a derail or a rescue is running.
		if (Train.bDerailing || Train.bRescueActive)
		{
			return;
		}
		Train.RespawnAccumSeconds += DeltaSeconds;
		if (Train.RespawnAccumSeconds > TrainRespawnDelaySeconds)
		{
			RespawnTrainNearCamera();
		}
		if (!Train.bVisible)
		{
			return;
		}
	}

	// FUN_004b4660: the rescue timer running out forces the derail.
	if (Train.bRescueActive)
	{
		Train.MissionTimerSeconds -= DeltaSeconds;
		if (Train.MissionTimerSeconds < 0.0f)
		{
			Train.bRescueActive = false;
			Train.bDerailing = true;
			Train.DerailTimerSeconds = TrainDerailSeconds;
		}
	}

	if (Train.bCrashRequested)
	{
		Train.bCrashRequested = false;
		Train.bDerailing = true;
		Train.DerailTimerSeconds = TrainDerailSeconds;
	}

	if (Train.bDerailing)
	{
		UpdateTrainDerail(DeltaSeconds);
		return;
	}

	if (!Train.bRescueActive && TileDistanceToCamera(Train.Tile) > (ViewRangeTiles >> 1) + 4)
	{
		HideTrain();
		return;
	}

	FVector NextWaypoint = FVector::ZeroVector;
	if (Train.NextTile.X == INDEX_NONE || !TryGetTrainWaypoint(Train.Tile, Train.NextTile, NextWaypoint))
	{
		if (!AdvanceTrainTile())
		{
			HideTrain();
			return;
		}
		if (!TryGetTrainWaypoint(Train.Tile, Train.NextTile, NextWaypoint))
		{
			HideTrain();
			return;
		}
	}

	// The leg the loco is on, and the grade it climbs. Each waypoint sits at its own terrain
	// height, so the leg between two of them is the ramp: hold the entry height across the whole
	// tile and the train stair-steps, and probing the rendered surface per frame instead just
	// makes it jitter over the sleepers and the joints between track pieces.
	const FVector LegStart = Train.PathHistory.Num() > 0 ? Train.PathHistory[0] : Train.World;
	const FVector LegVector = NextWaypoint - LegStart;
	const float LegRunCm = FVector(LegVector.X, LegVector.Y, 0.0f).Size();
	Train.PitchDegrees = LegRunCm > KINDA_SMALL_NUMBER
		? FMath::RadiansToDegrees(FMath::Atan2(LegVector.Z, LegRunCm))
		: 0.0f;

	FVector Delta = NextWaypoint - Train.World;
	Delta.Z = 0.0f;
	const float StepCm = Train.SpeedCmPerSec * DeltaSeconds;
	if (Delta.SizeSquared() <= FMath::Square(StepCm))
	{
		Train.World = NextWaypoint;
		Train.PathHistory.Insert(Train.World, 0);
		const int32 MaxHistory = 6 * SimCopterAmbientVehicles::TrainCarCount + 4;
		if (Train.PathHistory.Num() > MaxHistory)
		{
			Train.PathHistory.SetNum(MaxHistory);
		}
		Train.PreviousTile = Train.Tile;
		Train.Tile = Train.NextTile;
		if (!AdvanceTrainTile())
		{
			HideTrain();
			return;
		}

		// FUN_004b4660 re-posts the mission's coordinates every tile so a rescue marker follows
		// the train down the line.
		if (Train.bRescueActive && Train.EventId != INDEX_NONE)
		{
			if (ASimCopterMissionSystemActor* Mission = ResolveMissionSystem())
			{
				Mission->PostMissionEventAt(
					SimCopterMissions::EVT_SetPrimaryCoords, Train.EventId, Train.Tile.X, Train.Tile.Y, 0, false);
			}
		}
	}
	else
	{
		// Direction stays flat - the step is measured along the ground - and the height comes from
		// how far along the leg the loco now is.
		Train.Direction = Delta.GetSafeNormal(UE_SMALL_NUMBER, Train.Direction);
		Train.World += Train.Direction * StepCm;
		Train.World.Z = LegRunCm > KINDA_SMALL_NUMBER
			? FMath::Lerp(LegStart.Z, NextWaypoint.Z, FMath::Clamp(FVector::Dist2D(LegStart, Train.World) / LegRunCm, 0.0f, 1.0f))
			: NextWaypoint.Z;
	}

	UpdateTrainCarTransforms();
}

bool ASimCopterAmbientVehiclesActor::AdvanceTrainTile()
{
	// FUN_004b5290 / FUN_004b6030 read the rail shape out of the tile id to choose the leg the
	// train leaves by. The remake follows the rail graph instead (see the port note in
	// Docs/scratchpad/ghidra/planes_trains_boats_decode_20260727.md): never reverse, prefer the
	// straight continuation, otherwise take a random branch.
	static const FIntPoint Steps[4] = { FIntPoint(0, -1), FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(-1, 0) };

	const FIntPoint Straight(
		Train.Tile.X + (Train.Tile.X - Train.PreviousTile.X),
		Train.Tile.Y + (Train.Tile.Y - Train.PreviousTile.Y));

	TArray<FIntPoint, TInlineAllocator<4>> Candidates;
	for (const FIntPoint& Step : Steps)
	{
		const FIntPoint Candidate(Train.Tile.X + Step.X, Train.Tile.Y + Step.Y);
		if (!IsRailTileAt(Candidate, /*bForTravel*/ true) || Candidate == Train.PreviousTile)
		{
			continue;
		}
		Candidates.Add(Candidate);
	}

	if (Candidates.Num() == 0)
	{
		// A dead end: back down the way it came, which is what the original's fallback leg does.
		if (IsRailTileAt(Train.PreviousTile, /*bForTravel*/ true))
		{
			Train.NextTile = Train.PreviousTile;
			return true;
		}
		return false;
	}

	if (Train.PreviousTile.X != INDEX_NONE && Candidates.Contains(Straight))
	{
		Train.NextTile = Straight;
		return true;
	}

	Train.NextTile = Candidates[RandomStream.RandRange(0, Candidates.Num() - 1)];
	return true;
}

bool ASimCopterAmbientVehiclesActor::PlaceTrainNearTile(const FIntPoint& Origin, const int32 MaxRings)
{
	EnsureRailScan();
	if (RailTiles.Num() == 0)
	{
		return false;
	}

	// FUN_004b7890's spiral (20 rings for a mission placement, 7 for FUN_004b74a0's respawn).
	for (int32 Ring = 0; Ring < MaxRings; ++Ring)
	{
		for (int32 OffsetY = -Ring; OffsetY <= Ring; ++OffsetY)
		{
			for (int32 OffsetX = -Ring; OffsetX <= Ring; ++OffsetX)
			{
				if (Ring > 0 && FMath::Abs(OffsetX) != Ring && FMath::Abs(OffsetY) != Ring)
				{
					continue;
				}

				const FIntPoint Candidate(Origin.X + OffsetX, Origin.Y + OffsetY);
				if (!IsRailTileAt(Candidate, /*bForTravel*/ false))
				{
					continue;
				}

				FVector Center = FVector::ZeroVector;
				if (!TryGetTrainTileCenter(Candidate, Center))
				{
					continue;
				}

				Train.Tile = Candidate;
				Train.PreviousTile = FIntPoint(INDEX_NONE, INDEX_NONE);
				Train.NextTile = FIntPoint(INDEX_NONE, INDEX_NONE);
				Train.World = Center;
				Train.PathHistory.Reset();
				Train.PathHistory.Add(Center);
				Train.bVisible = true;
				Train.bDerailing = false;
				Train.DerailSpinDegrees = 0.0f;
				Train.PitchDegrees = 0.0f;
				Train.RespawnAccumSeconds = 0.0f;
				Train.SpeedCmPerSec = Train.BaseSpeedCmPerSec;
				if (!AdvanceTrainTile())
				{
					Train.bVisible = false;
					continue;
				}
				UpdateTrainCarTransforms();
				return true;
			}
		}
	}

	return false;
}

bool ASimCopterAmbientVehiclesActor::RespawnTrainNearCamera()
{
	// FUN_004b74a0 zeroes the accumulator on every attempt (see the boat note above).
	Train.RespawnAccumSeconds = 0.0f;

	FIntPoint Camera(INDEX_NONE, INDEX_NONE);
	if (!TryGetCameraTile(Camera))
	{
		return false;
	}

	const int32 Radius = FMath::Max(2, ViewRangeTiles >> 1);
	static const FIntPoint Offsets[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
	const FIntPoint Offset = Offsets[RandomStream.RandRange(0, 3)];
	const FIntPoint Origin(
		FMath::Clamp(Camera.X + Offset.X * Radius, 0, FSimCity2000City::MapSize - 1),
		FMath::Clamp(Camera.Y + Offset.Y * Radius, 0, FSimCity2000City::MapSize - 1));

	if (PlaceTrainNearTile(Origin, AmbientRespawnRings) &&
		TileDistanceToCamera(Train.Tile) <= (ViewRangeTiles >> 1) + 4)
	{
		return true;
	}

	// Remake divergence: the original's seven-ring probe assumes its own (much shorter) draw
	// distance, so on a sparse rail network it can place the train outside the keep-alive radius
	// and drop it again on the very next frame - thirty seconds of nothing, repeatedly. Fall back
	// to the rail tile nearest the camera that is inside that radius, which is what the probe was
	// reaching for anyway.
	int32 BestDistance = MAX_int32;
	FIntPoint BestTile(INDEX_NONE, INDEX_NONE);
	for (const FIntPoint& Tile : RailTiles)
	{
		const int32 Distance = TileDistanceToCamera(Tile);
		if (Distance < BestDistance && Distance <= (ViewRangeTiles >> 1) + 4)
		{
			BestDistance = Distance;
			BestTile = Tile;
		}
	}

	if (BestTile.X == INDEX_NONE)
	{
		HideTrain();
		return false;
	}
	return PlaceTrainNearTile(BestTile, 1);
}

void ASimCopterAmbientVehiclesActor::UpdateTrainDerail(const float DeltaSeconds)
{
	// FUN_004b49b0: the cars slide on at half speed, spinning, for two seconds.
	Train.DerailTimerSeconds -= DeltaSeconds;
	Train.DerailSpinDegrees += TrainDerailSpinDegPerFrame * SimCopterAmbientVehicles::OriginalSimHz * DeltaSeconds;
	Train.World += Train.Direction * (Train.SpeedCmPerSec * 0.5f * DeltaSeconds);
	UpdateTrainCarTransforms();

	if (Train.DerailTimerSeconds > 0.0f)
	{
		return;
	}

	// Then it stops dead. Each car explodes and is left lying where it slid to, burning: the
	// wreckage is the mission, and it stays until the player puts it out or the clock beats them.
	const int32 EventId = Train.EventId;
	const FIntPoint Tile = Train.Tile;

	struct FCarPlacement { int32 ObjectId; FVector World; FVector Direction; float Yaw; };
	TArray<FCarPlacement, TInlineAllocator<3>> Placements;

	auto CaptureCar = [this, &Placements](UProceduralMeshComponent* Mesh, int32 ObjectId, float Yaw)
	{
		if (Mesh == nullptr)
		{
			return;
		}
		FCarPlacement Placement;
		Placement.ObjectId = ObjectId;
		Placement.World = Mesh->GetComponentLocation();
		Placement.Direction = Mesh->GetForwardVector();
		Placement.Yaw = Yaw;
		Placements.Add(Placement);
	};

	// Freeze them exactly where the slide left them, spin and all.
	CaptureCar(Train.LocoMesh, SimCopterAmbientVehicles::TrainLocoObjectId, 0.0f);
	for (int32 Index = 0; Index < SimCopterAmbientVehicles::TrainCarCount; ++Index)
	{
		CaptureCar(Train.CarMeshes[Index], SimCopterAmbientVehicles::TrainCarObjectIds[Index], 0.0f);
	}

	for (const FCarPlacement& Placement : Placements)
	{
		SpawnCrashDebris(Placement.World, EventId, 3);
	}

	Train.bDerailing = false;
	Train.bRescueActive = false;
	Train.EventId = INDEX_NONE;
	Train.MissionTimerSeconds = 0.0f;
	ClearTrainRoofRiders();
	HideTrain();

	if (ASimCopterMissionSystemActor* Mission = ResolveMissionSystem())
	{
		Mission->RemoveMissionPeople(EventId);
		Mission->PostMissionEventAt(
			SimCopterMissions::EVT_SetPrimaryCoords, EventId, Tile.X, Tile.Y, 0, false);
		Mission->PostMissionEvent(
			SimCopterMissions::EVT_SetCategory, EventId, SimCopterMissions::CAT_Active, false);
	}

	BeginWreckFire(EventId, Placements.Num());
	for (const FCarPlacement& Placement : Placements)
	{
		AddWreck(
			Placement.ObjectId,
			Placement.World,
			Placement.Direction,
			Placement.Yaw,
			EventId,
			true,
			WreckBurnTimeoutSeconds);
	}
}

void ASimCopterAmbientVehiclesActor::UpdateTrainCarTransforms()
{
	// The loco rides the grade of the leg it is on; the cars each take their own from the stretch
	// of path they are standing on, so a train crossing a crest bends over it a car at a time
	// instead of moving as one rigid block.
	SetMeshTransform(Train.LocoMesh, Train.World, Train.Direction, Train.DerailSpinDegrees, Train.PitchDegrees);

	// The original keeps a car per tile behind the loco; the remake trails them along the same
	// path at one tile of spacing.
	const float SpacingCm = GetTileSizeCm();
	for (int32 Index = 0; Index < SimCopterAmbientVehicles::TrainCarCount; ++Index)
	{
		const float TrailCm = SpacingCm * static_cast<float>(Index + 1);
		FVector CarWorld = Train.World - Train.Direction * TrailCm;
		FVector CarDirection = Train.Direction;

		// Walk the recorded waypoints so a car on a bend follows the rails rather than cutting
		// the corner.
		float Remaining = TrailCm;
		FVector Cursor = Train.World;
		for (const FVector& Point : Train.PathHistory)
		{
			const float Leg = FVector::Dist(Cursor, Point);
			if (Leg <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			if (Leg >= Remaining)
			{
				const FVector Step = (Point - Cursor) / Leg;
				CarWorld = Cursor + Step * Remaining;
				CarDirection = -Step;
				Remaining = 0.0f;
				break;
			}
			Remaining -= Leg;
			Cursor = Point;
		}
		if (Remaining > 0.0f)
		{
			CarWorld = Cursor - Train.Direction * Remaining;
		}

		// CarDirection came off the path polyline, so it already carries that stretch's grade in
		// its Z and Rotation() turns it into the car's pitch.
		SetMeshTransform(
			Train.CarMeshes[Index],
			CarWorld,
			CarDirection,
			Train.DerailSpinDegrees * (Index == 0 ? 0.8f : 0.5f));
	}
}

void ASimCopterAmbientVehiclesActor::UpdateTrainRoofRiders()
{
	if (TrainRoofRiders.Num() == 0)
	{
		return;
	}

	// They ride the first carriage: TRAIN2 is the long flat-roofed one, which is why it is the
	// car worth stranding people on.
	UProceduralMeshComponent* Car = Train.CarMeshes[0];
	if (Car == nullptr || !Train.bVisible)
	{
		return;
	}

	const FVector CarWorld = Car->GetComponentLocation();
	const FRotator CarRotation = Car->GetComponentRotation();
	const float RoofCm = GetModelTopHeightCm(SimCopterAmbientVehicles::TrainCarObjectIds[0]);
	const float SpacingCm = GetTileSizeCm() * 0.18f;

	int32 Slot = 0;
	for (int32 Index = TrainRoofRiders.Num() - 1; Index >= 0; --Index)
	{
		ASimCopterGroundAgent* Rider = TrainRoofRiders[Index].Get();
		if (Rider == nullptr || Rider->IsActorBeingDestroyed())
		{
			TrainRoofRiders.RemoveAt(Index);
			continue;
		}
		if (Rider->IsMissionCarried() || Rider->GetBehaviorCarrier() != nullptr)
		{
			// On the harness or in the cabin: whatever picked them up owns their transform, and
			// putting them back on the roof would drag them off it.
			continue;
		}

		// Spread them along the carriage's long axis so a group is reachable one at a time.
		const float Along = (static_cast<float>(Slot) - 0.5f * static_cast<float>(TrainRoofRiders.Num() - 1)) * SpacingCm;
		const FVector RiderWorld =
			CarWorld +
			CarRotation.RotateVector(FVector(Along, 0.0f, 0.0f)) +
			FVector(0.0f, 0.0f, RoofCm + Rider->GetCapsuleHalfHeightCm());
		Rider->SetActorLocation(RiderWorld, false);
		Rider->SetActorRotation(FRotator(0.0f, CarRotation.Yaw + 90.0f, 0.0f));
		++Slot;
	}
}

void ASimCopterAmbientVehiclesActor::ClearTrainRoofRiders()
{
	TrainRoofRiders.Reset();
}

void ASimCopterAmbientVehiclesActor::HideTrain()
{
	// As with the boat: only the respawn attempt zeroes the accumulator (FUN_004b74a0).
	Train.bVisible = false;
	Train.DerailSpinDegrees = 0.0f;
	Train.NextTile = FIntPoint(INDEX_NONE, INDEX_NONE);
	if (Train.LocoMesh != nullptr)
	{
		Train.LocoMesh->SetVisibility(false);
	}
	for (UProceduralMeshComponent* Car : Train.CarMeshes)
	{
		if (Car != nullptr)
		{
			Car->SetVisibility(false);
		}
	}
}

bool ASimCopterAmbientVehiclesActor::TryActivateTrainCrash(const int32 EventId)
{
	EnsurePools();

	// FUN_004b7f60: refuses while a crash is already pending or running.
	if (Train.bCrashRequested || Train.bDerailing)
	{
		return false;
	}

	// Divergence: the original just arms the flag and lets the crash wait for the next time the
	// train happens to be in the world. Because the remake's train spends much more of its life
	// out of range (its keep-alive radius is tied to a far longer draw distance), an armed-but-
	// invisible crash would read as a mission that never happens, so the train is put on the rails
	// now - the same placement the rescue uses.
	if (!Train.bVisible && !PlaceTrainNearTile(Train.Tile.X == INDEX_NONE
		? FIntPoint(RandomStream.RandRange(0, FSimCity2000City::MapSize - 1), RandomStream.RandRange(0, FSimCity2000City::MapSize - 1))
		: Train.Tile, TrainPlacementRings))
	{
		return false;
	}

	Train.bCrashRequested = true;
	Train.EventId = EventId;
	return true;
}

bool ASimCopterAmbientVehiclesActor::TryActivateTrainRescue(
	const int32 EventId,
	const float TimerSeconds,
	int32& OutTileX,
	int32& OutTileY)
{
	EnsurePools();

	// FUN_004b7fd0: nothing doing while the train is crashing or already carrying a rescue.
	if (Train.bCrashRequested || Train.bDerailing || Train.bRescueActive)
	{
		return false;
	}

	if (!Train.bVisible)
	{
		// The original seeds the spiral from a random map tile rather than the placer's tile.
		const FIntPoint Seed(
			RandomStream.RandRange(0, FSimCity2000City::MapSize - 1),
			RandomStream.RandRange(0, FSimCity2000City::MapSize - 1));
		if (!PlaceTrainNearTile(Seed, TrainPlacementRings))
		{
			return false;
		}
	}

	Train.EventId = EventId;
	Train.MissionTimerSeconds = TimerSeconds;

	// 1 + rand % 3 passengers (spawn mode 0x13), stranded on the roof of the first carriage - the
	// flat-topped TRAIN2 - of a train that does not stop. Getting them off a moving train is the
	// mission; the train only stops when it derails, which is the failure.
	const int32 Count = TrainRescueMinVictims + RandomStream.RandRange(0, TrainRescueVictimSpread - 1);
	int32 Spawned = 0;
	ClearTrainRoofRiders();
	if (ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		TArray<ASimCopterGroundAgent*> Riders;
		Spawned = TrafficSystem->SpawnMissionSwimmersAtWorldLocation(
			Count,
			Train.World,
			EventId,
			TrainRescueSpawnMode,
			GetTileSizeCm() * 0.16f,
			/*bFloatOnWaterSurface*/ false,
			&Riders);
		for (ASimCopterGroundAgent* Rider : Riders)
		{
			TrainRoofRiders.Add(Rider);
		}
	}

	if (Spawned <= 0)
	{
		Train.EventId = INDEX_NONE;
		ClearTrainRoofRiders();
		return false;
	}

	// Put them on the roof straight away rather than a frame later at the train's old position.
	UpdateTrainRoofRiders();

	Train.bRescueActive = true;
	OutTileX = Train.Tile.X;
	OutTileY = Train.Tile.Y;
	if (ASimCopterMissionSystemActor* Mission = ResolveMissionSystem())
	{
		Mission->PostMissionEvent(SimCopterMissions::EVT_RescueVictimAdded, EventId, Spawned, true);
	}
	return true;
}

// ---------------------------------------------------------------------------------------------
// Shared
// ---------------------------------------------------------------------------------------------

void ASimCopterAmbientVehiclesActor::SpawnCrashDebris(const FVector& World, const int32 EventId, const int32 Count)
{
	if (EffectComponent == nullptr)
	{
		return;
	}

	// Both crash sites also play CRSH2 at the impact point, and the plane's stops the dive.
	if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
	{
		Audio->Play3D(SimCopterSound::SND_CRSH2, World);
		Audio->Stop(SimCopterSound::SND_DIVE1);
	}

	// FUN_004b2cd0 / FUN_004b49b0 both throw type-4 debris with a random yaw over 3600
	// tenth-degrees and a pitch of 750 + rand % 120 tenth-degrees.
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float YawDegrees = static_cast<float>(RandomStream.RandRange(0, 3599)) * 0.1f;
		const float PitchDegrees = static_cast<float>(750 + RandomStream.RandRange(0, 119)) * 0.1f;
		const float SpeedCmPerSec = static_cast<float>(25 + RandomStream.RandRange(0, 29)) * GetCmPerOriginalUnit();
		const FVector Velocity = FRotator(PitchDegrees, YawDegrees, 0.0f).Vector() * SpeedCmPerSec;
		EffectComponent->SpawnEffect(ESimCopterEffectType::Debris, World, Velocity);
	}
	EffectComponent->SpawnTilePuff(World, 1);
}

bool ASimCopterAmbientVehiclesActor::TryGetDebugViewTarget(const int32 Which, FVector& OutWorld) const
{
	switch (Which)
	{
	case 0:
		if (Train.bVisible)
		{
			OutWorld = Train.World;
			return true;
		}
		return false;
	case 1:
		for (const FSimCopterAmbientBoat& Boat : Boats)
		{
			if (Boat.bVisible && Boat.ObjectId == SimCopterAmbientVehicles::CapsizedBoatObjectId)
			{
				OutWorld = Boat.World;
				return true;
			}
		}
		return false;
	case 2:
		for (const FSimCopterAmbientPlane& Plane : Planes)
		{
			if (Plane.bVisible)
			{
				OutWorld = Plane.World;
				return true;
			}
		}
		return false;
	default:
		for (const FSimCopterVehicleWreck& Wreck : Wrecks)
		{
			OutWorld = Wreck.World;
			return true;
		}
		return false;
	}
}

FString ASimCopterAmbientVehiclesActor::GetStatusLine() const
{
	int32 VisiblePlanes = 0;
	for (const FSimCopterAmbientPlane& Plane : Planes)
	{
		VisiblePlanes += Plane.bVisible ? 1 : 0;
	}
	int32 VisibleBoats = 0;
	for (const FSimCopterAmbientBoat& Boat : Boats)
	{
		VisibleBoats += Boat.bVisible ? 1 : 0;
	}

	FString Detail;
	for (const FSimCopterAmbientPlane& Plane : Planes)
	{
		if (Plane.bVisible)
		{
			Detail += FString::Printf(TEXT("  [%s tile %d,%d world %.0f %.0f %.0f%s]"),
				Plane.ObjectId == SimCopterAmbientVehicles::UfoObjectId ? TEXT("UFO") : TEXT("PLANE1"),
				Plane.Tile.X, Plane.Tile.Y,
				Plane.World.X, Plane.World.Y, Plane.World.Z,
				Plane.bCrashing ? TEXT(" CRASHING") : TEXT(""));
		}
	}
	for (const FSimCopterAmbientBoat& Boat : Boats)
	{
		if (Boat.bVisible)
		{
			Detail += FString::Printf(TEXT("  [%s tile %d,%d world %.0f %.0f %.0f]"),
				Boat.ObjectId == SimCopterAmbientVehicles::CapsizedBoatObjectId ? TEXT("CAPBOAT1") : TEXT("BOAT1"),
				Boat.Tile.X, Boat.Tile.Y,
				Boat.World.X, Boat.World.Y, Boat.World.Z);
		}
	}
	if (Train.bVisible)
	{
		Detail += FString::Printf(TEXT("  [TRAIN tile %d,%d -> %d,%d world %.0f %.0f %.0f%s]"),
			Train.Tile.X, Train.Tile.Y, Train.NextTile.X, Train.NextTile.Y,
			Train.World.X, Train.World.Y, Train.World.Z,
			Train.bRescueActive ? TEXT(" RESCUE") : TEXT(""));
	}

	return FString::Printf(
		TEXT("Planes %d/2  Boats %d/3  Train %s  Rail %d tiles%s"),
		VisiblePlanes,
		VisibleBoats,
		Train.bDerailing ? TEXT("derailing") : (Train.bVisible ? TEXT("running") : TEXT("off map")),
		RailTiles.Num(),
		*Detail);
}
