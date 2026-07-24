// Copyright Epic Games, Inc. All Rights Reserved.

#include "Missions/SimCopterMissionSystemActor.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Ground/SimCopterFireRenderComponent.h"
#include "Ground/SimCopterParticleFX.h"
#include "Ground/SimCopterEffectFX.h"
#include "Ground/SimCopterGroundAgent.h"
#include "Ground/SimCopterOnFootPawn.h"
#include "Ground/SimCopterTrafficSystemActor.h"
#include "City/SimCity2000CityActor.h"
#include "Audio.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Sound/SoundWave.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"
#include "CollisionQueryParams.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
FString FormatSignedAmount(int32 Value, const TCHAR* Unit)
{
	return FString::Printf(TEXT("%+d %s"), Value, Unit);
}

const TCHAR* GetMissionDeltaLabel(int32 TextId)
{
	switch (TextId)
	{
	case 0x3a2: return TEXT("Flame started");
	case 0x3a3: return TEXT("Flame doused");
	case 0x3a4: return TEXT("Building burned");
	case 0x3a5: return TEXT("Building saved");
	case 0x3a6: return TEXT("Debris doused");
	case 0x3a8: return TEXT("Rescue delivered");
	case 0x3a9: return TEXT("Passenger delivered");
	case 0x3aa: return TEXT("Patient delivered");
	case 0x3ab: return TEXT("Victim picked up");
	case 0x3ac: return TEXT("Rioter dispersed");
	case 0x3b1: return TEXT("Person died");
	case 0x3b6: return TEXT("Car doused");
	case 0x3b7: return TEXT("Car cleared");
	case 0x3b8: return TEXT("Car burned");
	default: return TEXT("Mission update");
	}
}

FString ResolveCareerTweakPath()
{
	TArray<FString, TInlineAllocator<3>> Candidates;
	Candidates.Add(FPaths::ProjectContentDir() / TEXT("OriginalGame/tweak/career.twk"));
	Candidates.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("Reference/SimCopterOriginalGame/tweak/career.twk")));
	Candidates.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame/tweak/career.twk")));

	for (FString Candidate : Candidates)
	{
		Candidate = FPaths::ConvertRelativePathToFull(Candidate);
		FPaths::NormalizeFilename(Candidate);
		if (FPaths::FileExists(Candidate))
		{
			return Candidate;
		}
	}

	return Candidates.Last();
}

bool IsValidMissionTile(int32 TileX, int32 TileY)
{
	return TileX >= 0 && TileX < 128 && TileY >= 0 && TileY < 128;
}

FLinearColor WithAlpha(FLinearColor Color, float Alpha)
{
	Color.A = Alpha;
	return Color;
}
}

ASimCopterMissionSystemActor::ASimCopterMissionSystemActor()
{
	PrimaryActorTick.bCanEverTick = true;

	FireRenderComponent = CreateDefaultSubobject<USimCopterFireRenderComponent>(TEXT("FireRender"));
	SetRootComponent(FireRenderComponent);

	// FIREPTS is rendered through the original palette-selector texture, sampled with nearest
	// filtering so FUN_00496da0's screen-pixel kernels remain hard edged.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlameMaterialFinder(
		TEXT("/Game/Materials/M_SimCopterSpriteTexture.M_SimCopterSpriteTexture"));
	if (FlameMaterialFinder.Succeeded())
	{
		FlameMaterial = FlameMaterialFinder.Object;
	}

	// The typed pool owns the SMOKE and type-0xD ember death follow-up.
	FireSmokeComponent = CreateDefaultSubobject<USimCopterParticleFXComponent>(TEXT("FireSmoke"));
	FireSmokeComponent->SetupAttachment(FireRenderComponent);
}

void ASimCopterMissionSystemActor::BeginPlay()
{
	Super::BeginPlay();
	
	// Assuming 0 for random seed for parity tests if we want, but normally a real seed.
	MissionSystem.Initialize(this, 12345);

	MissionSystem.LoadCareerData(ResolveCareerTweakPath());

	SetupMissionSounds();
	EnsureMessageLogWidget();
	EnsureMissionMarkerWidget();
	EnsureMegaphonePromptWidget();
	EnsureDebugButtonsWidget();

	// Load the FIREPTS flame mesh once (deferred so the traffic/city actors have finished their
	// own BeginPlay asset loads first).
	if (FireRenderComponent != nullptr && !bFireAssetsInitialized)
	{
		FString FireError;
		const FString OriginalRoot = ResolveOriginalGameRootDir();
		bFireAssetsInitialized = FireRenderComponent->InitFireAssets(OriginalRoot, FlameMaterial, FireError);
		if (!bFireAssetsInitialized)
		{
			UE_LOG(LogTemp, Warning, TEXT("SimCopter fire visuals disabled: %s"), *FireError);
		}
		if (FireSmokeComponent != nullptr)
		{
			FString EffectError;
			if (!FireSmokeComponent->InitEffectAssets(OriginalRoot, EffectError))
			{
				UE_LOG(LogTemp, Warning, TEXT("SimCopter fire effect palette unavailable: %s"), *EffectError);
			}
		}
	}
}

void ASimCopterMissionSystemActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (FSimCopterMedevacHandoff& Handoff : MedevacHandoffs)
	{
		EndMedevacHandoff(Handoff);
	}
	MedevacHandoffs.Reset();

	RemoveMegaphonePromptWidget();
	RemoveMissionMarkerWidget();
	RemoveMessageLogWidget();
	RemoveDebugButtonsWidget();
	Super::EndPlay(EndPlayReason);
}

void ASimCopterMissionSystemActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	MissionSystem.Tick(DeltaTime);
	ProcessPassengerTransfers();
	ProcessMedevacHospitalHandoffs(DeltaTime);
	UpdateMegaphonePrompt();
	UpdateFireVisuals(DeltaTime);
	MissionSystem.AdvanceCareerIfComplete();

	bool bChangedLog = false;
	for (int32 Index = MissionMessageLog.Num() - 1; Index >= 0; --Index)
	{
		MissionMessageLog[Index].RemainingSeconds -= DeltaTime;
		if (MissionMessageLog[Index].RemainingSeconds <= 0.0f)
		{
			MissionMessageLog.RemoveAt(Index);
			bChangedLog = true;
		}
	}

	if (bChangedLog)
	{
		RefreshMessageLogWidget();
	}

	RefreshMissionMarkerWidget();
}

int32 ASimCopterMissionSystemActor::GetXbldTileId(int32 TileX, int32 TileY) const
{
	if (const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		return TrafficSystem->GetXbldTileId(TileX, TileY);
	}
	return 0;
}

int32 ASimCopterMissionSystemActor::GetBuildingFootprintSize(int32 TileX, int32 TileY) const
{
	if (const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		return TrafficSystem->GetBuildingFootprintSize(TileX, TileY);
	}
	return 1;
}

int32 ASimCopterMissionSystemActor::GetBuildingTopHeight1616(int32 TileX, int32 TileY) const
{
	// The original reads the burning cell object's own top; the remake's equivalent is the
	// roof of the placed building mesh, measured above the tile floor and converted back to
	// the 16.16 original units the flame offsets use.
	const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem == nullptr)
	{
		return 0;
	}

	FVector TileCenter;
	if (!TrafficSystem->TryGetTileCenterWorldLocation(TileX, TileY, TileCenter))
	{
		return 0;
	}

	float TopZ = TileCenter.Z;
	if (!TraceSurfaceTopZ(TileCenter, TopZ))
	{
		return 0;
	}

	const float HeightCm = TopZ - TileCenter.Z;
	if (HeightCm <= 0.0f)
	{
		return 0;
	}
	return static_cast<int32>(HeightCm / SimCopterEffectFX::Fixed1616ToCm);
}

bool ASimCopterMissionSystemActor::GetCameraTile(int32& OutTileX, int32& OutTileY) const
{
	if (const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		if (const UWorld* World = GetWorld())
		{
			if (const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0))
			{
				if (PlayerController->PlayerCameraManager != nullptr &&
					TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(PlayerController->PlayerCameraManager->GetCameraLocation(), OutTileX, OutTileY))
				{
					return true;
				}
			}
		}
	}

	OutTileX = 64;
	OutTileY = 64;
	return false;
}

bool ASimCopterMissionSystemActor::GetPlayerTile(int32& OutTileX, int32& OutTileY) const
{
	if (const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		if (const UWorld* World = GetWorld())
		{
			if (const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0))
			{
				if (const APawn* Pawn = PlayerController->GetPawn())
				{
					if (TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(Pawn->GetActorLocation(), OutTileX, OutTileY))
					{
						return true;
					}
				}
			}
		}
	}

	OutTileX = 64;
	OutTileY = 64;
	return false;
}

bool ASimCopterMissionSystemActor::IsModalUiActive() const
{
	return false;
}

void ASimCopterMissionSystemActor::OnBuildingFireIgnited(int32 TileX, int32 TileY, int32 EventId)
{
	// The flame visuals are driven by polling MissionSystem.GetFlames() in UpdateFireVisuals, so
	// there is nothing to place here; the ignition just makes the fire object + flames exist.
}

void ASimCopterMissionSystemActor::OnBuildingBurnedDown(int32 TileX, int32 TileY, int32 FootprintSize)
{
	ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem == nullptr)
	{
		return;
	}

	ASimCity2000CityActor* CityActor = TrafficSystem->GetCityActor();
	if (CityActor == nullptr)
	{
		return;
	}

	// Where the rubble burst comes from, captured before the building goes.
	FBox BuildingBounds(ForceInit);
	const bool bHasBounds = CityActor->TryGetBuildingBoundsAtTile(TileX, TileY, BuildingBounds);

	TArray<FIntPoint> ClearedTiles;
	if (!CityActor->DemolishBuildingAtTile(TileX, TileY, ClearedTiles))
	{
		return;
	}

	// FUN_004a5fd0 zeroes the XBLD entry of every tile the building covered, which is what stops
	// the tile reading as a building - including to IsFireSuitableTile, so it cannot re-ignite.
	TrafficSystem->ClearXbldTiles(ClearedTiles);

	FVector TileCenter = FVector::ZeroVector;
	if (!TrafficSystem->TryGetTileCenterWorldLocation(TileX, TileY, TileCenter))
	{
		return;
	}

	const FVector BurstOrigin = bHasBounds
		? FVector(BuildingBounds.GetCenter().X, BuildingBounds.GetCenter().Y, BuildingBounds.Min.Z)
		: TileCenter;

	PlayUiSound(4);

	if (FireSmokeComponent != nullptr)
	{
		// FUN_004af100(cell, 0, 0x200000, 0, 4, eventId): a scale-4 column of debris off the pad.
		FireSmokeComponent->SpawnSplashColumn(BurstOrigin, /*ScaleExponent*/ 4);

		// The original throws 3 + rand % footprint pieces, each on a random heading with a steep
		// upward pitch, drawing its yaw/pitch/speed from the mission LCG in that order.
		SimCopterMissions::FSimCopterMsvcRand& Rand = MissionSystem.GetRand();
		const int32 SafeFootprint = FMath::Max(1, FootprintSize);
		const int32 DebrisCount = (Rand.Rand() % SafeFootprint) + 3;
		// local_54 + 0x300000: the pieces leave from 48 original units above the cell floor.
		const FVector DebrisOrigin = BurstOrigin + FVector::UpVector * (48.0f * SimCopterEffectFX::OriginalUnitToCm);
		for (int32 Piece = 0; Piece < DebrisCount; ++Piece)
		{
			// rand % 0xe10 tenth-degrees of yaw, (rand % 200) + 0x28a tenth-degrees of pitch
			// (65.0 to 84.9 degrees up), and (rand % 100) + 0x32 units per second of speed.
			const float YawDegrees = static_cast<float>(Rand.Rand() % 3600) * 0.1f;
			const float PitchDegrees = static_cast<float>((Rand.Rand() % 200) + 650) * 0.1f;
			const float SpeedUnits = static_cast<float>((Rand.Rand() % 100) + 50);
			const FVector Direction = FRotator(PitchDegrees, YawDegrees, 0.0f).Vector();
			FireSmokeComponent->SpawnEffect(
				ESimCopterEffectType::Debris,
				DebrisOrigin,
				Direction * SpeedUnits * SimCopterEffectFX::OriginalUnitToCm,
				static_cast<float>(SafeFootprint * 2) * SimCopterEffectFX::OriginalUnitToCm);
		}
	}

	UE_LOG(
		LogTemp,
		Display,
		TEXT("Building at tile (%d,%d) burned down: %d tiles cleared."),
		TileX,
		TileY,
		ClearedTiles.Num());
}

void ASimCopterMissionSystemActor::SimForceFire()
{
	// Debug force: bypass the mission-record cap so a fire always spawns even with many missions up.
	const int32 EventId = MissionSystem.DebugForceBuildingFire();
	UE_LOG(LogTemp, Display, TEXT("SimForceFire: created building fire event %d (active flames now %d)"),
		EventId, MissionSystem.GetActiveFlameCount());
}

void ASimCopterMissionSystemActor::SimForceCarFire()
{
	const int32 EventId = MissionSystem.DebugForceCarFire();
	UE_LOG(LogTemp, Display, TEXT("SimForceCarFire: created car fire event %d"), EventId);
}

int32 ASimCopterMissionSystemActor::DumpWaterAt(const FVector& BucketWorldLocation)
{
	ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem == nullptr)
	{
		return 0;
	}

	int32 FlamesHit = 0;

	// Building fires: FUN_004a50c0 compares the water's offset inside the cell against each
	// flame's own offset, so resolve the bucket down to that sub-tile offset rather than
	// soaking the whole cell. One dump frame is one water particle, i.e. one full-strength hit.
	int32 TileX = INDEX_NONE;
	int32 TileY = INDEX_NONE;
	if (TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(BucketWorldLocation, TileX, TileY))
	{
		FVector TileCenter;
		int32 LocalX1616 = 0;
		int32 LocalY1616 = 0;
		int32 LocalZ1616 = 0;
		if (TrafficSystem->TryGetTileCenterWorldLocation(TileX, TileY, TileCenter))
		{
			TrafficSystem->ConvertWorldOffsetToOriginal(
				BucketWorldLocation - TileCenter, LocalX1616, LocalY1616, LocalZ1616);
		}
		FlamesHit += MissionSystem.DouseAtLocalOffset(TileX, TileY, LocalX1616, LocalZ1616, 0x10000);
	}

	// Car fires: put out burning cars under the bucket and pay the douse/clear scoring events.
	TArray<int32> ExtinguishedCarEvents;
	TrafficSystem->DouseBurningVehiclesNear(BucketWorldLocation, CarDouseRadiusCm, ExtinguishedCarEvents);
	for (int32 EventId : ExtinguishedCarEvents)
	{
		MissionSystem.PostEvent(SimCopterMissions::EVT_CarDoused, EventId, 1, false);
		MissionSystem.PostEvent(SimCopterMissions::EVT_CarCleared, EventId, 1, false);
		++FlamesHit;
	}

	return FlamesHit;
}

FString ASimCopterMissionSystemActor::ResolveOriginalGameRootDir() const
{
	TArray<FString, TInlineAllocator<3>> Candidates;
	Candidates.Add(FPaths::ProjectContentDir() / TEXT("OriginalGame"));
	Candidates.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("Reference/SimCopterOriginalGame")));
	Candidates.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame")));

	for (FString Candidate : Candidates)
	{
		Candidate = FPaths::ConvertRelativePathToFull(Candidate);
		FPaths::NormalizeDirectoryName(Candidate);
		if (FPaths::DirectoryExists(Candidate))
		{
			return Candidate;
		}
	}
	return FString();
}

bool ASimCopterMissionSystemActor::TraceSurfaceTopZ(const FVector& WorldXY, float& OutTopZ) const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	const FVector Start(WorldXY.X, WorldXY.Y, WorldXY.Z + 6000.0f);
	const FVector End(WorldXY.X, WorldXY.Y, WorldXY.Z - 6000.0f);

	FCollisionQueryParams Params(FName(TEXT("SimCopterFireSurface")), /*bTraceComplex*/ false, this);

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		OutTopZ = Hit.ImpactPoint.Z;
		return true;
	}
	return false;
}

void ASimCopterMissionSystemActor::UpdateFireVisuals(float DeltaSeconds)
{
	if (FireRenderComponent == nullptr || !FireRenderComponent->IsReady())
	{
		return;
	}

	const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem == nullptr)
	{
		return;
	}

	const TArray<SimCopterMissions::FSimCopterFlame>& Flames = MissionSystem.GetFlames();
	const float TimeSeconds = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0f;

	// Billboard the fire points toward the active camera.
	FVector CameraLocation = GetActorLocation();
	if (const APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (const APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
		{
			CameraLocation = CameraManager->GetCameraLocation();
		}
	}

	TArray<FSimCopterFlameVisual> Visuals;
	Visuals.Reserve(Flames.Num());

	for (int32 Index = 0; Index < Flames.Num(); ++Index)
	{
		const SimCopterMissions::FSimCopterFlame& Flame = Flames[Index];
		if (!Flame.bActive)
		{
			continue;
		}

		FVector TileCenter;
		if (!TrafficSystem->TryGetTileCenterWorldLocation(Flame.TileX, Flame.TileY, TileCenter))
		{
			continue;
		}

		// FUN_004a5340 stores source-runtime X/Y-up/Z offsets. Apply the same verified
		// Maxis-to-Unreal axes and global city yaw as the building mesh; mapping these
		// directly to Unreal XYZ rotates the planar FIREPTS cloud away from its wall.
		FVector FlameXY =
			TileCenter +
			TrafficSystem->ConvertOriginalOffsetToWorld(
				Flame.PosX,
				0,
				Flame.PosZ);

		float TopZ = TileCenter.Z;
		TraceSurfaceTopZ(FlameXY, TopZ);
		// PosY is the flame's own climb up the wall, one storey per FUN_004a4ac0 growth
		// step, so it rides on top of the seated base rather than being traced away.
		FlameXY.Z = TopZ + TrafficSystem->ConvertOriginalOffsetToWorld(0, Flame.PosY, 0).Z;

		// FUN_004a47c0 gives every flame the same 0x100000 render scale; the record's
		// +0x0c is the growth step, not a size.
		constexpr float Scale = 1.0f;

		FSimCopterFlameVisual Visual;
		Visual.Key = Index;
		Visual.World = FlameXY;
		Visual.Scale = Scale;
		Visual.FlickerSeed = static_cast<float>(Index) * 1.7f;
		Visuals.Add(Visual);

		// Rising smoke + embers above this flame. The original draws a dark SMOKE sprite above the
		// fire and throws fire-trajectory embers; reproduce that with palette-coloured particles so
		// the "dark grey smoke near the top" and chaotic sparks read authentically.
		if (FireSmokeComponent != nullptr)
		{
			SpawnFirePlume(FlameXY, Scale, DeltaSeconds);
		}
	}

	// Burning cars use the same runtime fire-point template at a smaller scale. CARFIRET is the
	// authored fire-truck vehicle model, not a flame mesh.
	TArray<FSimCopterBurningVehicle> BurningVehicles;
	TrafficSystem->GetBurningVehicles(BurningVehicles);
	for (const FSimCopterBurningVehicle& Burning : BurningVehicles)
	{
		FSimCopterFlameVisual Visual;
		Visual.Key = Burning.Key;
		Visual.World = Burning.World;
		Visual.Scale = 0.8f;
		Visual.FlickerSeed = static_cast<float>(Burning.Key & 0xFFFF) * 0.013f;
		Visual.bVehicleFire = true;
		Visuals.Add(Visual);
	}

	FireRenderComponent->SyncFlames(Visuals, TimeSeconds, CameraLocation);
}

void ASimCopterMissionSystemActor::SpawnFirePlume(const FVector& FlameBaseWorld, float Scale, float DeltaSeconds)
{
	if (FireSmokeComponent == nullptr ||
		FireSmokeComponent->GetActiveCount(ESimCopterEffectPool::Fire25) != 0)
	{
		return;
	}

	// Type 0xC owns the entire 25-slot fire pool: after 3.5 seconds it emits exactly 24
	// type-0xD four-point cards into the remaining slots. The former free-running random plume
	// exhausted that pool before the decoded burst could ever occur.
	const FVector SmokeOrigin = FlameBaseWorld + FVector::UpVector * (5.0f * SimCopterEffectFX::OriginalUnitToCm);
	FireSmokeComponent->SpawnEffect(
		ESimCopterEffectType::BuildingFireSmoke,
		SmokeOrigin,
		FVector::UpVector * SimCopterEffectFX::OriginalUnitToCm);
}

bool ASimCopterMissionSystemActor::TryActivatePlaneCrash(int32 EventId)
{
	return false;
}

bool ASimCopterMissionSystemActor::TryActivateTrainCrash(int32 EventId)
{
	return false;
}

bool ASimCopterMissionSystemActor::TryActivateBoatRescue(int32 EventId, int32 Timer1616, int32& OutTileX, int32& OutTileY)
{
	return false;
}

bool ASimCopterMissionSystemActor::TryActivateTrainRescue(int32 EventId, int32 Timer1616, int32& OutTileX, int32& OutTileY)
{
	return false;
}

bool ASimCopterMissionSystemActor::TryStartTrafficJam(int32 EventId, int32& OutTileX, int32& OutTileY)
{
	if (ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		return TrafficSystem->TryStartTrafficJam(EventId, OutTileX, OutTileY);
	}
	return false;
}

void ASimCopterMissionSystemActor::EndTrafficJam(int32 EventId)
{
	if (ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		TrafficSystem->EndTrafficJam(EventId);
	}
}



bool ASimCopterMissionSystemActor::TryStartCarFire(int32 EventId, int32& OutTileX, int32& OutTileY)
{
	if (ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		return TrafficSystem->TryStartCarFire(EventId, OutTileX, OutTileY);
	}
	return false;
}

bool ASimCopterMissionSystemActor::TrySpawnMissionPerson(int32 Mode, int32 SubState, int32 TileX, int32 TileY, int32 EventId)
{
	if (ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		return TrafficSystem->TrySpawnMissionPerson(Mode, SubState, TileX, TileY, EventId);
	}
	return false;
}

bool ASimCopterMissionSystemActor::TryResolveTransportSpawnTile(
	int32 OriginX,
	int32 OriginY,
	int32& OutTileX,
	int32& OutTileY)
{
	OutTileX = OriginX;
	OutTileY = OriginY;

	if (ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		if (!TrafficSystem->IsWaterTile(OriginX, OriginY))
		{
			return true;
		}
		return TrafficSystem->TryFindNearestTransportLandTile(OriginX, OriginY, OutTileX, OutTileY);
	}
	return true;
}

bool ASimCopterMissionSystemActor::CreatePlayerCausedMedevacForVictim(ASimCopterGroundAgent* Victim)
{
	if (Victim == nullptr)
	{
		return false;
	}

	ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem == nullptr)
	{
		return false;
	}

	int32 TileX = INDEX_NONE;
	int32 TileY = INDEX_NONE;
	if (!TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(Victim->GetActorLocation(), TileX, TileY))
	{
		return false;
	}

	const int32 EventId = MissionSystem.CreatePlayerCausedMedevacAt(TileX, TileY);
	if (EventId == INDEX_NONE || EventId < 0)
	{
		return false;
	}

	Victim->MissionEventId = EventId;
	Victim->InitialPersonState = 6;
	Victim->SetMissionInjuredPose();
	return true;
}

bool ASimCopterMissionSystemActor::ConvertDroppedTransportPassengerToMedevac(
	ASimCopterGroundAgent* Victim,
	int32 SourceTransportEventId)
{
	if (Victim == nullptr)
	{
		return false;
	}

	if (!CreatePlayerCausedMedevacForVictim(Victim))
	{
		return false;
	}

	if (SourceTransportEventId != INDEX_NONE)
	{
		MissionSystem.PostEvent(SimCopterMissions::EVT_PassengerLost, SourceTransportEventId, 1);
	}

	return true;
}

void ASimCopterMissionSystemActor::PlayRadioVoice(int32 VoiceId, int32 Volume)
{
	if (USoundBase** Sound = RadioVoices.Find(VoiceId))
	{
		UGameplayStatics::PlaySound2D(this, *Sound, Volume / 255.0f);
	}
}

void ASimCopterMissionSystemActor::PlayUiSound(int32 SoundId)
{
	if (USoundBase** Sound = UiSounds.Find(SoundId))
	{
		UGameplayStatics::PlaySound2D(this, *Sound);
	}
}

bool ASimCopterMissionSystemActor::TryActivateSpeederCar(int32 EventId, int32 TileX, int32 TileY)
{
	return false;
}

void ASimCopterMissionSystemActor::OnUiMessage(const SimCopterMissions::FSimCopterMissionUiMessage& Message)
{
	FLinearColor Color = FLinearColor::White;
	const FString Text = FormatMissionUiMessage(Message, Color);
	if (!Text.IsEmpty())
	{
		PushMissionLogMessage(Text, Color);
	}
}

ASimCopterTrafficSystemActor* ASimCopterMissionSystemActor::ResolveTrafficSystem() const
{
	if (SourceTrafficSystem != nullptr && IsValid(SourceTrafficSystem))
	{
		return SourceTrafficSystem;
	}

	if (!bUseActiveTrafficSystem)
	{
		return nullptr;
	}

	if (UWorld* World = GetWorld())
	{
		return Cast<ASimCopterTrafficSystemActor>(UGameplayStatics::GetActorOfClass(World, ASimCopterTrafficSystemActor::StaticClass()));
	}

	return nullptr;
}

void ASimCopterMissionSystemActor::NotifyMedevacVictimBoarded(int32 EventId, int32 Count)
{
	if (EventId == INDEX_NONE || Count <= 0)
	{
		return;
	}

	MissionPassengersOnboard.FindOrAdd(EventId) += Count;
	MissionSystem.PostEvent(SimCopterMissions::EVT_VictimPickedUp, EventId, Count);
}

void ASimCopterMissionSystemActor::NotifyPassengerDroppedFromHelicopter(
	int32 EventId,
	ESimCopterMissionPassengerKind Kind,
	int32 Count)
{
	if (EventId == INDEX_NONE || Count <= 0)
	{
		return;
	}

	if (int32* OnboardCount = MissionPassengersOnboard.Find(EventId))
	{
		*OnboardCount = FMath::Max(0, *OnboardCount - Count);
	}

	if (Kind == ESimCopterMissionPassengerKind::Transport || Kind == ESimCopterMissionPassengerKind::Medevac)
	{
		MissionSystem.AdjustVictimsPickedUp(EventId, -Count);
	}
}

void ASimCopterMissionSystemActor::GetTransferReadyHelicopters(TArray<ASimCopterHelicopterPawn*>& OutHelicopters) const
{
	OutHelicopters.Reset();
	if (GetWorld() == nullptr)
	{
		return;
	}

	TArray<AActor*> HelicopterActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ASimCopterHelicopterPawn::StaticClass(), HelicopterActors);
	for (AActor* Actor : HelicopterActors)
	{
		ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(Actor);
		if (Helicopter != nullptr && Helicopter->CanTransferMissionPassengers())
		{
			OutHelicopters.Add(Helicopter);
		}
	}
}

void ASimCopterMissionSystemActor::ProcessPassengerTransfers()
{
	ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem == nullptr)
	{
		return;
	}

	TArray<ASimCopterHelicopterPawn*> Helicopters;
	GetTransferReadyHelicopters(Helicopters);

	struct FPassengerMissionSnapshot
	{
		int32 EventId = INDEX_NONE;
		int32 PickupX = INDEX_NONE;
		int32 PickupY = INDEX_NONE;
		int32 DropoffX = INDEX_NONE;
		int32 DropoffY = INDEX_NONE;
		int32 WaitingPassengers = 0;
		int32 DeliverablePassengers = 0;
		bool bTransport = false;
		bool bMedevac = false;
	};

	TArray<FPassengerMissionSnapshot, TInlineAllocator<8>> PassengerMissions;
	TSet<int32> ActivePassengerEventIds;
	for (const SimCopterMissions::FSimCopterMissionRecord& Record : MissionSystem.GetRecords())
	{
		if (!Record.bActive)
		{
			continue;
		}

		const bool bTransport = (Record.TypeMask & SimCopterMissions::TYPE_Transport) != 0;
		const bool bMedevac = (Record.TypeMask & SimCopterMissions::TYPE_Medevac) != 0;
		if (!bTransport && !bMedevac)
		{
			continue;
		}

		ActivePassengerEventIds.Add(Record.EventId);

		FPassengerMissionSnapshot Snapshot;
		Snapshot.EventId = Record.EventId;
		Snapshot.PickupX = Record.TileX;
		Snapshot.PickupY = Record.TileY;
		Snapshot.DropoffX = Record.SecondaryX;
		Snapshot.DropoffY = Record.SecondaryY;
		Snapshot.bTransport = bTransport;
		Snapshot.bMedevac = bMedevac;
		if (bTransport)
		{
			Snapshot.WaitingPassengers = FMath::Max(0, Record.TransportPassengers - Record.VictimsPickedUp - Record.PassengersLost);
			Snapshot.DeliverablePassengers = FMath::Max(0, Record.TransportPassengers - Record.TransportDelivered - Record.PassengersLost);
		}
		else
		{
			Snapshot.WaitingPassengers = FMath::Max(0, Record.MedevacVictims - Record.VictimsPickedUp - Record.Casualties);
			Snapshot.DeliverablePassengers = FMath::Max(0, Record.MedevacVictims - Record.MedevacDelivered - Record.Casualties);
		}
		PassengerMissions.Add(Snapshot);
	}

	for (auto It = MissionPassengersOnboard.CreateIterator(); It; ++It)
	{
		if (!ActivePassengerEventIds.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}

	auto IsWorldLocationNearTile = [this, TrafficSystem](const FVector& WorldLocation, int32 TileX, int32 TileY, float RadiusCm) -> bool
	{
		if (!IsValidMissionTile(TileX, TileY))
		{
			return false;
		}

		FVector TileLocation = FVector::ZeroVector;
		if (!TrafficSystem->TryGetTileCenterWorldLocation(TileX, TileY, TileLocation))
		{
			return false;
		}

		return FVector::DistSquared2D(WorldLocation, TileLocation) <= FMath::Square(RadiusCm) &&
			FMath::Abs(WorldLocation.Z - TileLocation.Z) <= PassengerTransferMaxVerticalDeltaCm;
	};

	if (ASimCopterOnFootPawn* OnFootPawn = Cast<ASimCopterOnFootPawn>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0)))
	{
		if (!OnFootPawn->IsCarryingMissionPerson() && OnFootPawn->CanPickUpMissionPersonNow())
		{
			for (const FPassengerMissionSnapshot& Mission : PassengerMissions)
			{
				if (!Mission.bMedevac || Mission.WaitingPassengers <= 0)
				{
					continue;
				}

				if (ASimCopterGroundAgent* Patient = TrafficSystem->FindMissionPersonNear(
					Mission.EventId,
					OnFootPawn->GetActorLocation(),
					MedevacOnFootPickupRadiusCm,
					PassengerTransferMaxVerticalDeltaCm))
				{
					OnFootPawn->PickUpMissionPerson(Patient);
					break;
				}
			}
		}
	}

	for (const FPassengerMissionSnapshot& Mission : PassengerMissions)
	{
		int32& Onboard = MissionPassengersOnboard.FindOrAdd(Mission.EventId);
		FVector DropoffLocation = FVector::ZeroVector;
		const bool bHasDropoffLocation =
			IsValidMissionTile(Mission.DropoffX, Mission.DropoffY) &&
			TrafficSystem->TryGetTileCenterWorldLocation(Mission.DropoffX, Mission.DropoffY, DropoffLocation);

		// Medevac patients are unloaded by the hospital EMT (ProcessMedevacHospitalHandoffs), never
		// by dropping them near the tile, so only transport uses this ground-release path.
		if (bHasDropoffLocation && Mission.bTransport && Mission.DeliverablePassengers > 0)
		{
			const int32 ReleasedOnGround = TrafficSystem->ReleaseMissionPeopleNear(
				Mission.EventId,
				DropoffLocation,
				Mission.DeliverablePassengers,
				PassengerDropoffRadiusCm,
				PassengerTransferMaxVerticalDeltaCm);
			if (ReleasedOnGround > 0)
			{
				Onboard = FMath::Max(0, Onboard - ReleasedOnGround);
				MissionSystem.PostEvent(SimCopterMissions::EVT_TransportDelivered, Mission.EventId, ReleasedOnGround);
			}
		}

		if (Mission.bTransport && bHasDropoffLocation)
		{
			for (ASimCopterHelicopterPawn* Helicopter : Helicopters)
			{
				if (Helicopter == nullptr || !IsWorldLocationNearTile(Helicopter->GetActorLocation(), Mission.DropoffX, Mission.DropoffY, PassengerDropoffRadiusCm))
				{
					continue;
				}

				const int32 OnHelicopter = Helicopter->GetMissionPassengerCount(Mission.EventId, ESimCopterMissionPassengerKind::Transport);
				const int32 Delivered = Helicopter->RemoveMissionPassengersForMission(OnHelicopter, Mission.EventId, ESimCopterMissionPassengerKind::Transport);
				if (Delivered <= 0)
				{
					continue;
				}

				Onboard = FMath::Max(0, Onboard - Delivered);
				TrafficSystem->SpawnMissionPeopleAtWorldLocation(
					Delivered,
					Helicopter->GetPassengerDropWorldLocation(),
					INDEX_NONE,
					0,
					-1,
					185.0f);
				MissionSystem.PostEvent(SimCopterMissions::EVT_TransportDelivered, Mission.EventId, Delivered);
				break;
			}
		}

		if (!Mission.bTransport || Mission.WaitingPassengers <= 0)
		{
			continue;
		}

		for (ASimCopterHelicopterPawn* Helicopter : Helicopters)
		{
			if (Helicopter == nullptr)
			{
				continue;
			}

			if (!IsWorldLocationNearTile(Helicopter->GetActorLocation(), Mission.PickupX, Mission.PickupY, PassengerPickupRadiusCm))
			{
				continue;
			}

			const int32 SeatsAvailable = FMath::Min(Mission.WaitingPassengers, Helicopter->GetAvailablePassengerSeats());
			if (SeatsAvailable <= 0)
			{
				continue;
			}

			TrafficSystem->GuideMissionPeopleToLocation(
				Mission.EventId,
				Helicopter->GetActorLocation(),
				Helicopter->GetActorLocation(),
				SeatsAvailable,
				PassengerPickupRadiusCm,
				PassengerTransferMaxVerticalDeltaCm,
				PassengerBoardGuidanceSeconds);

			int32 NewPickupCreditCount = 0;
			const int32 PickedUp = TrafficSystem->BoardMissionPeopleTouching(
				Mission.EventId,
				Helicopter->GetActorLocation(),
				SeatsAvailable,
				PassengerBoardTouchRadiusCm,
				PassengerTransferMaxVerticalDeltaCm,
				&NewPickupCreditCount);
			if (PickedUp <= 0)
			{
				continue;
			}

			const int32 Boarded = Helicopter->AddMissionPassengersForMission(PickedUp, Mission.EventId, ESimCopterMissionPassengerKind::Transport);
			if (Boarded <= 0)
			{
				continue;
			}

			Onboard += Boarded;
			const int32 NewPickupCredit = FMath::Clamp(NewPickupCreditCount, 0, Boarded);
			const int32 Reboarded = Boarded - NewPickupCredit;
			if (NewPickupCredit > 0)
			{
				MissionSystem.PostEvent(SimCopterMissions::EVT_VictimPickedUp, Mission.EventId, NewPickupCredit);
			}
			if (Reboarded > 0)
			{
				MissionSystem.PostEvent(SimCopterMissions::EVT_VictimPickedUp, Mission.EventId, Reboarded, true);
			}
			break;
		}
	}
}

void ASimCopterMissionSystemActor::ProcessMedevacHospitalHandoffs(float DeltaSeconds)
{
	ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();

	// Collect active medevac missions and their hospital drop-off world locations.
	TMap<int32, FVector> MedevacDropoffs;
	if (TrafficSystem != nullptr)
	{
		for (const SimCopterMissions::FSimCopterMissionRecord& Record : MissionSystem.GetRecords())
		{
			if (!Record.bActive || (Record.TypeMask & SimCopterMissions::TYPE_Medevac) == 0)
			{
				continue;
			}
			if (!IsValidMissionTile(Record.SecondaryX, Record.SecondaryY))
			{
				continue;
			}
			FVector HospitalLocation = FVector::ZeroVector;
			if (TrafficSystem->TryGetTileCenterWorldLocation(Record.SecondaryX, Record.SecondaryY, HospitalLocation))
			{
				MedevacDropoffs.Add(Record.EventId, HospitalLocation);
			}
		}
	}

	// Start a handoff for any landed helicopter that is at a hospital with patients still aboard.
	TArray<ASimCopterHelicopterPawn*> Helicopters;
	GetTransferReadyHelicopters(Helicopters);
	for (const TPair<int32, FVector>& Pair : MedevacDropoffs)
	{
		const int32 EventId = Pair.Key;
		if (FindMedevacHandoff(EventId) != nullptr)
		{
			continue;
		}
		for (ASimCopterHelicopterPawn* Helicopter : Helicopters)
		{
			if (Helicopter == nullptr ||
				Helicopter->GetMissionPassengerCount(EventId, ESimCopterMissionPassengerKind::Medevac) <= 0)
			{
				continue;
			}
			if (FVector::DistSquared2D(Helicopter->GetActorLocation(), Pair.Value) > FMath::Square(MedevacHospitalHandoffRadiusCm))
			{
				continue;
			}
			BeginMedevacHandoff(EventId, Helicopter, Pair.Value);
			break;
		}
	}

	// Advance and clean up in-progress handoffs.
	for (int32 Index = MedevacHandoffs.Num() - 1; Index >= 0; --Index)
	{
		if (!MedevacDropoffs.Contains(MedevacHandoffs[Index].EventId) ||
			!AdvanceMedevacHandoff(MedevacHandoffs[Index], DeltaSeconds))
		{
			EndMedevacHandoff(MedevacHandoffs[Index]);
			MedevacHandoffs.RemoveAt(Index);
		}
	}
}

FSimCopterMedevacHandoff* ASimCopterMissionSystemActor::FindMedevacHandoff(int32 EventId)
{
	for (FSimCopterMedevacHandoff& Handoff : MedevacHandoffs)
	{
		if (Handoff.EventId == EventId)
		{
			return &Handoff;
		}
	}
	return nullptr;
}

void ASimCopterMissionSystemActor::BeginMedevacHandoff(int32 EventId, ASimCopterHelicopterPawn* Helicopter, const FVector& HospitalCenter)
{
	ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem == nullptr || Helicopter == nullptr)
	{
		return;
	}

	// The whole hand-off plays out on the surface the helicopter landed on (hospital roof or the
	// ground beside it): the drop point beside the helicopter gives that surface height.
	const FVector HeliDoor = Helicopter->GetPassengerDropWorldLocation();

	// The doorway sits between the helicopter and the hospital, capped so the EMT's walk stays
	// short and readable.
	FVector ToHospital = HospitalCenter - HeliDoor;
	ToHospital.Z = 0.0f;
	FVector Direction = ToHospital.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		Direction = Helicopter->GetActorForwardVector().GetSafeNormal2D();
		if (Direction.IsNearlyZero())
		{
			Direction = FVector(1.0f, 0.0f, 0.0f);
		}
	}
	const float DoorwayDistance = FMath::Clamp(ToHospital.Size(), 260.0f, MedevacDoorwayDistanceCm);
	FVector DoorwayFeet = HeliDoor + Direction * DoorwayDistance;
	DoorwayFeet.Z = HeliDoor.Z;

	// The EMT waits at the hospital doorway (facing the helicopter), ready to unload it. It is a
	// worker, not a victim, so it carries no mission id (nothing tries to "rescue" the EMT).
	ASimCopterGroundAgent* Emt = TrafficSystem->SpawnScriptedMissionAgent(DoorwayFeet, INDEX_NONE, TEXT("Medik"), false, 1.0f);
	if (Emt == nullptr)
	{
		// Couldn't stage an EMT (e.g. original assets unavailable): don't strand the patients.
		DeliverMedevacDirectly(EventId, Helicopter);
		return;
	}

	FSimCopterMedevacHandoff Handoff;
	Handoff.EventId = EventId;
	Handoff.Helicopter = Helicopter;
	Handoff.Emt = Emt;
	Handoff.HeliDoorLocation = HeliDoor;
	Handoff.DoorwayLocation = DoorwayFeet;
	Handoff.Phase = 0;
	Handoff.Doorway = SpawnHospitalDoorway(DoorwayFeet + FVector(0.0f, 0.0f, 37.5f), (-Direction).Rotation());
	MedevacHandoffs.Add(Handoff);
}

bool ASimCopterMissionSystemActor::AdvanceMedevacHandoff(FSimCopterMedevacHandoff& Handoff, float DeltaSeconds)
{
	ASimCopterHelicopterPawn* Helicopter = Handoff.Helicopter.Get();
	ASimCopterGroundAgent* Emt = Handoff.Emt.Get();
	if (Helicopter == nullptr || Emt == nullptr)
	{
		return false;
	}

	// Refresh the door target (the helicopter can settle a little after touchdown).
	Handoff.HeliDoorLocation = Helicopter->GetPassengerDropWorldLocation();

	const int32 Onboard = Helicopter->GetMissionPassengerCount(Handoff.EventId, ESimCopterMissionPassengerKind::Medevac);

	if (Handoff.Phase == 0)
	{
		// Walk to the helicopter to collect the next patient.
		if (!Helicopter->CanTransferMissionPassengers())
		{
			return false; // helicopter took off / left before we reached it
		}
		if (Onboard <= 0)
		{
			return false; // nothing left to unload - all done
		}

		FVector Target = Handoff.HeliDoorLocation;
		Target.Z = Emt->GetActorLocation().Z;
		Emt->SetMoveTarget(Target);

		if (FVector::DistSquared2D(Emt->GetActorLocation(), Handoff.HeliDoorLocation) <= FMath::Square(MedevacEmtReachRadiusCm))
		{
			const int32 Removed = Helicopter->RemoveMissionPassengersForMission(1, Handoff.EventId, ESimCopterMissionPassengerKind::Medevac);
			if (Removed <= 0)
			{
				return false;
			}
			if (int32* OnboardCount = MissionPassengersOnboard.Find(Handoff.EventId))
			{
				*OnboardCount = FMath::Max(0, *OnboardCount - Removed);
			}

			// Lift a patient out of the helicopter, carried by the EMT just like the player carries
			// them.
			if (ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
			{
				ASimCopterGroundAgent* Patient = TrafficSystem->SpawnScriptedMissionAgent(
					Handoff.HeliDoorLocation, Handoff.EventId, FString(), true, 1.0f);
				if (Patient != nullptr)
				{
					Patient->SetCarriedBy(Emt->GetRootComponent(), FVector(40.0f, 0.0f, -6.0f), FRotator(0.0f, 90.0f, 88.0f));
					Handoff.CarriedPatient = Patient;
				}
			}
			Emt->ClearMoveTarget();
			Handoff.Phase = 1;
		}
		return true;
	}

	// Phase 1: carry the patient to the hospital doorway.
	FVector Target = Handoff.DoorwayLocation;
	Target.Z = Emt->GetActorLocation().Z;
	Emt->SetMoveTarget(Target);

	if (FVector::DistSquared2D(Emt->GetActorLocation(), Handoff.DoorwayLocation) <= FMath::Square(MedevacEmtReachRadiusCm))
	{
		if (ASimCopterGroundAgent* Patient = Handoff.CarriedPatient.Get())
		{
			Patient->Destroy(); // taken inside the hospital
		}
		Handoff.CarriedPatient.Reset();
		MissionSystem.PostEvent(SimCopterMissions::EVT_MedevacDelivered, Handoff.EventId, 1);
		Emt->ClearMoveTarget();

		// Keep going while the helicopter still holds patients for this mission.
		if (Helicopter->GetMissionPassengerCount(Handoff.EventId, ESimCopterMissionPassengerKind::Medevac) > 0)
		{
			Handoff.Phase = 0;
		}
		else
		{
			return false; // finished
		}
	}
	return true;
}

void ASimCopterMissionSystemActor::EndMedevacHandoff(FSimCopterMedevacHandoff& Handoff)
{
	if (ASimCopterGroundAgent* Patient = Handoff.CarriedPatient.Get())
	{
		Patient->Destroy();
	}
	if (ASimCopterGroundAgent* Emt = Handoff.Emt.Get())
	{
		Emt->Destroy();
	}
	if (AActor* Doorway = Handoff.Doorway.Get())
	{
		Doorway->Destroy();
	}
	Handoff.CarriedPatient.Reset();
	Handoff.Emt.Reset();
	Handoff.Doorway.Reset();
}

void ASimCopterMissionSystemActor::DeliverMedevacDirectly(int32 EventId, ASimCopterHelicopterPawn* Helicopter)
{
	if (Helicopter == nullptr)
	{
		return;
	}
	const int32 Onboard = Helicopter->GetMissionPassengerCount(EventId, ESimCopterMissionPassengerKind::Medevac);
	const int32 Removed = Helicopter->RemoveMissionPassengersForMission(Onboard, EventId, ESimCopterMissionPassengerKind::Medevac);
	if (Removed <= 0)
	{
		return;
	}
	if (int32* OnboardCount = MissionPassengersOnboard.Find(EventId))
	{
		*OnboardCount = FMath::Max(0, *OnboardCount - Removed);
	}
	MissionSystem.PostEvent(SimCopterMissions::EVT_MedevacDelivered, EventId, Removed);
}

AActor* ASimCopterMissionSystemActor::SpawnHospitalDoorway(const FVector& CenterLocation, const FRotator& Facing)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Doorway = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
	if (Doorway == nullptr)
	{
		return nullptr;
	}

	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Doorway, TEXT("DoorwayMesh"));
	if (Mesh == nullptr)
	{
		Doorway->Destroy();
		return nullptr;
	}
	Doorway->SetRootComponent(Mesh);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->RegisterComponent();
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		Mesh->SetStaticMesh(Cube);
	}
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	if (HospitalDoorwayMaterial != nullptr)
	{
		Mesh->SetMaterial(0, HospitalDoorwayMaterial);
	}
	Doorway->SetActorLocationAndRotation(CenterLocation, Facing);
	// A tall, thin slab standing on the surface - a stairwell doorway into the hospital. The engine
	// cube is 100 cm, so this is ~12 x 55 x 75 cm.
	Doorway->SetActorScale3D(FVector(0.12f, 0.55f, 0.75f));
	return Doorway;
}

bool ASimCopterMissionSystemActor::FindNearestClearableJam(const FVector& FromWorldLocation, int32& OutEventId, FVector& OutJamWorldLocation) const
{
	OutEventId = INDEX_NONE;
	const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem == nullptr)
	{
		return false;
	}

	float BestDistSq = FMath::Square(MegaphoneRangeCm);
	for (const SimCopterMissions::FSimCopterMissionRecord& Record : MissionSystem.GetRecords())
	{
		if (!Record.bActive || (Record.TypeMask & SimCopterMissions::TYPE_TrafficJam) == 0)
		{
			continue;
		}
		if (!IsValidMissionTile(Record.TileX, Record.TileY))
		{
			continue;
		}
		FVector JamLocation = FVector::ZeroVector;
		if (!TrafficSystem->TryGetTileCenterWorldLocation(Record.TileX, Record.TileY, JamLocation))
		{
			continue;
		}
		const float DistSq = FVector::DistSquared2D(FromWorldLocation, JamLocation);
		if (DistSq <= BestDistSq)
		{
			BestDistSq = DistSq;
			OutEventId = Record.EventId;
			OutJamWorldLocation = JamLocation;
		}
	}
	return OutEventId != INDEX_NONE;
}

bool ASimCopterMissionSystemActor::TryUseMegaphone(const FVector& FromWorldLocation)
{
	int32 EventId = INDEX_NONE;
	FVector JamLocation = FVector::ZeroVector;
	if (!FindNearestClearableJam(FromWorldLocation, EventId, JamLocation))
	{
		return false;
	}

	if (!MissionSystem.ClearTrafficJam(EventId))
	{
		return false;
	}

	// Megaphone bark (auto-loaded original "MG_*" line).
	if (MegaphoneVoices.Num() > 0)
	{
		const int32 Pick = FMath::RandRange(0, MegaphoneVoices.Num() - 1);
		if (USoundBase* Voice = MegaphoneVoices[Pick])
		{
			UGameplayStatics::PlaySound2D(this, Voice);
		}
	}

	UpdateMegaphonePrompt();
	return true;
}

void ASimCopterMissionSystemActor::UpdateMegaphonePrompt()
{
	bool bInRange = false;
	if (const ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0)))
	{
		int32 EventId = INDEX_NONE;
		FVector JamLocation = FVector::ZeroVector;
		bInRange = FindNearestClearableJam(Helicopter->GetActorLocation(), EventId, JamLocation);
	}

	EnsureMegaphonePromptWidget();
	if (!MegaphonePromptWidget.IsValid())
	{
		return;
	}
	if (bInRange != bMegaphonePromptVisible)
	{
		bMegaphonePromptVisible = bInRange;
		MegaphonePromptWidget->SetVisibility(bInRange ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	}
}

void ASimCopterMissionSystemActor::EnsureMegaphonePromptWidget()
{
	if (MegaphonePromptWidget.IsValid() || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	TSharedRef<STextBlock> PromptText =
		SNew(STextBlock)
		.Text(FText::FromString(TEXT("Press  [ M ]  to clear the traffic jam  (Megaphone)")))
		.ColorAndOpacity(FLinearColor(1.0f, 0.95f, 0.5f, 1.0f))
		.ShadowOffset(FVector2D(1.0f, 1.0f))
		.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f))
		.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 18));
	MegaphonePromptText = PromptText;

	MegaphonePromptWidget =
		SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 108.0f))
		.Visibility(EVisibility::Collapsed)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f))
			.Padding(FMargin(16.0f, 8.0f))
			[
				PromptText
			]
		];

	bMegaphonePromptVisible = false;
	GEngine->GameViewport->AddViewportWidgetContent(MegaphonePromptWidget.ToSharedRef(), 25);
}

void ASimCopterMissionSystemActor::RemoveMegaphonePromptWidget()
{
	if (GEngine != nullptr && GEngine->GameViewport != nullptr && MegaphonePromptWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MegaphonePromptWidget.ToSharedRef());
	}
	MegaphonePromptText.Reset();
	MegaphonePromptWidget.Reset();
	bMegaphonePromptVisible = false;
}

void ASimCopterMissionSystemActor::EnsureDebugButtonsWidget()
{
	if (!bShowDebugFireButtons || DebugButtonsWidget.IsValid() || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	auto MakeButton = [](const FString& Label, FOnClicked InOnClicked) -> TSharedRef<SButton>
	{
		return SNew(SButton)
			.OnClicked(InOnClicked)
			.ContentPadding(FMargin(12.0f, 6.0f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label))
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13))
			];
	};

	DebugButtonsWidget =
		SNew(SBox)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(0.0f, 0.0f, 18.0f, 18.0f))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.5f))
			.Padding(FMargin(6.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
				[
					MakeButton(TEXT("Force Fire"), FOnClicked::CreateUObject(this, &ASimCopterMissionSystemActor::OnDebugForceFireClicked))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					MakeButton(TEXT("Force Car Fire"), FOnClicked::CreateUObject(this, &ASimCopterMissionSystemActor::OnDebugForceCarFireClicked))
				]
			]
		];

	GEngine->GameViewport->AddViewportWidgetContent(DebugButtonsWidget.ToSharedRef(), 30);
}

void ASimCopterMissionSystemActor::RemoveDebugButtonsWidget()
{
	if (GEngine != nullptr && GEngine->GameViewport != nullptr && DebugButtonsWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(DebugButtonsWidget.ToSharedRef());
	}
	DebugButtonsWidget.Reset();
}

FReply ASimCopterMissionSystemActor::OnDebugForceFireClicked()
{
	SimForceFire();
	return FReply::Handled();
}

FReply ASimCopterMissionSystemActor::OnDebugForceCarFireClicked()
{
	SimForceCarFire();
	return FReply::Handled();
}

FString ASimCopterMissionSystemActor::ResolveOriginalSoundDir() const
{
	TArray<FString, TInlineAllocator<3>> Candidates;
	Candidates.Add(FPaths::ProjectContentDir() / TEXT("OriginalGame/sound/English"));
	Candidates.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("Reference/SimCopterOriginalGame/sound/English")));
	Candidates.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame/sound/English")));

	for (FString Candidate : Candidates)
	{
		Candidate = FPaths::ConvertRelativePathToFull(Candidate);
		FPaths::NormalizeDirectoryName(Candidate);
		if (FPaths::DirectoryExists(Candidate))
		{
			return Candidate;
		}
	}
	return FString();
}

USoundWave* ASimCopterMissionSystemActor::LoadOriginalVoice(const FString& SoundDir, const FString& BaseName) const
{
	if (SoundDir.IsEmpty() || BaseName.IsEmpty())
	{
		return nullptr;
	}

	// The originals ship with mixed-case extensions.
	FString Path = FPaths::Combine(SoundDir, BaseName + TEXT(".WAV"));
	if (!FPaths::FileExists(Path))
	{
		Path = FPaths::Combine(SoundDir, BaseName + TEXT(".wav"));
		if (!FPaths::FileExists(Path))
		{
			return nullptr;
		}
	}

	TArray<uint8> FileBytes;
	if (!FFileHelper::LoadFileToArray(FileBytes, *Path))
	{
		return nullptr;
	}

	FWaveModInfo WaveInfo;
	if (!WaveInfo.ReadWaveInfo(FileBytes.GetData(), FileBytes.Num()))
	{
		return nullptr;
	}

	const int32 Channels = WaveInfo.pChannels != nullptr ? *WaveInfo.pChannels : 0;
	const int32 SampleRate = WaveInfo.pSamplesPerSec != nullptr ? *WaveInfo.pSamplesPerSec : 0;
	const int32 BitsPerSample = WaveInfo.pBitsPerSample != nullptr ? *WaveInfo.pBitsPerSample : 0;
	if (Channels <= 0 || SampleRate <= 0 || WaveInfo.SampleDataSize == 0)
	{
		return nullptr;
	}

	// USoundWave::RawPCMData expects 16-bit signed PCM. Pass 16-bit through and up-convert 8-bit.
	TArray<uint8> Pcm16;
	if (BitsPerSample == 16)
	{
		Pcm16.Append(WaveInfo.SampleDataStart, WaveInfo.SampleDataSize);
	}
	else if (BitsPerSample == 8)
	{
		const int32 NumSamples = static_cast<int32>(WaveInfo.SampleDataSize);
		Pcm16.SetNumUninitialized(NumSamples * 2);
		int16* Out = reinterpret_cast<int16*>(Pcm16.GetData());
		for (int32 Index = 0; Index < NumSamples; ++Index)
		{
			Out[Index] = static_cast<int16>((static_cast<int32>(WaveInfo.SampleDataStart[Index]) - 128) << 8);
		}
	}
	else
	{
		return nullptr; // unsupported bit depth
	}

	USoundWave* Sound = NewObject<USoundWave>(const_cast<ASimCopterMissionSystemActor*>(this));
	if (Sound == nullptr)
	{
		return nullptr;
	}

	const int32 BytesPerFrame = 2 * Channels;
	const int32 NumFrames = Pcm16.Num() / FMath::Max(1, BytesPerFrame);

	Sound->SetSampleRate(SampleRate);
	Sound->NumChannels = Channels;
	Sound->Duration = static_cast<float>(NumFrames) / static_cast<float>(SampleRate);
	Sound->SoundGroup = SOUNDGROUP_Default;
	Sound->bLooping = false;
	Sound->RawPCMDataSize = Pcm16.Num();
	Sound->RawPCMData = static_cast<uint8*>(FMemory::Malloc(Pcm16.Num()));
	FMemory::Memcpy(Sound->RawPCMData, Pcm16.GetData(), Pcm16.Num());
	return Sound;
}

void ASimCopterMissionSystemActor::SetupMissionSounds()
{
	if (!bAutoLoadOriginalSounds)
	{
		return;
	}

	const FString SoundDir = ResolveOriginalSoundDir();
	if (SoundDir.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Mission] Original sound folder not found; mission audio stays silent."));
		return;
	}

	// Mission completion dispatcher voices used by FSimCopterMissionSystem::CompleteMission
	// (PlayRadioVoice). The exact original id->file table isn't decoded, so the numbered "D2###"
	// dispatcher series is mapped deterministically; any entry already assigned in the editor wins.
	static const int32 RadioVoiceIds[] = { 0x5f, 0x60, 0x61, 0x65, 0x66, 0x67, 0x68, 99, 100 };
	for (int32 VoiceId : RadioVoiceIds)
	{
		if (RadioVoices.Contains(VoiceId))
		{
			continue;
		}
		const FString BaseName = FString::Printf(TEXT("D2%03d"), VoiceId - 94);
		if (USoundWave* Voice = LoadOriginalVoice(SoundDir, BaseName))
		{
			RadioVoices.Add(VoiceId, Voice);
		}
	}

	// Megaphone lines ("MG_*"): one is barked when the megaphone clears a jam.
	MegaphoneVoices.Reset();
	TArray<FString> MegaphoneFiles;
	IFileManager::Get().FindFiles(MegaphoneFiles, *FPaths::Combine(SoundDir, TEXT("MG_*.WAV")), true, false);
	TSet<FString> SeenBaseNames;
	for (const FString& File : MegaphoneFiles)
	{
		const FString BaseName = FPaths::GetBaseFilename(File);
		bool bAlreadySeen = false;
		SeenBaseNames.Add(BaseName.ToUpper(), &bAlreadySeen);
		if (bAlreadySeen)
		{
			continue;
		}
		if (USoundWave* Voice = LoadOriginalVoice(SoundDir, BaseName))
		{
			MegaphoneVoices.Add(Voice);
		}
	}

	UE_LOG(LogTemp, Display, TEXT("[Mission] Auto-loaded %d radio voices and %d megaphone lines from %s"),
		RadioVoices.Num(), MegaphoneVoices.Num(), *SoundDir);
}

void ASimCopterMissionSystemActor::EnsureMessageLogWidget()
{
	if (!bShowMissionMessageLog || MessageLogWidget.IsValid() || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	TSharedRef<SVerticalBox> LogBox = SNew(SVerticalBox);
	MessageLogBox = LogBox;
	MessageLogWidget =
		SNew(SBox)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(FMargin(MessageLogScreenPadding.X, MessageLogScreenPadding.Y, 0.0f, 0.0f))
		.Visibility(EVisibility::Collapsed)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.58f))
			.Padding(FMargin(10.0f, 8.0f))
			[
				LogBox
			]
		];

	GEngine->GameViewport->AddViewportWidgetContent(MessageLogWidget.ToSharedRef(), 20);
}

void ASimCopterMissionSystemActor::RemoveMessageLogWidget()
{
	if (GEngine != nullptr && GEngine->GameViewport != nullptr && MessageLogWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MessageLogWidget.ToSharedRef());
	}

	MessageLogBox.Reset();
	MessageLogWidget.Reset();
}

void ASimCopterMissionSystemActor::RefreshMessageLogWidget()
{
	EnsureMessageLogWidget();
	if (!MessageLogBox.IsValid() || !MessageLogWidget.IsValid())
	{
		return;
	}

	MessageLogBox->ClearChildren();
	for (const FSimCopterMissionLogEntry& Entry : MissionMessageLog)
	{
		MessageLogBox->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 1.5f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(Entry.Text))
			.ColorAndOpacity(Entry.Color)
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14))
		];
	}

	MessageLogWidget->SetVisibility(MissionMessageLog.Num() > 0 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
}

void ASimCopterMissionSystemActor::EnsureMissionMarkerWidget()
{
	if (!bShowMissionWorldMarkers || MissionMarkerWidget.IsValid() || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	TSharedRef<SConstraintCanvas> MarkerCanvas = SNew(SConstraintCanvas);
	MissionMarkerCanvas = MarkerCanvas;
	MissionMarkerWidget =
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			MarkerCanvas
		];

	GEngine->GameViewport->AddViewportWidgetContent(MissionMarkerWidget.ToSharedRef(), 15);
}

void ASimCopterMissionSystemActor::RemoveMissionMarkerWidget()
{
	if (GEngine != nullptr && GEngine->GameViewport != nullptr && MissionMarkerWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MissionMarkerWidget.ToSharedRef());
	}

	MissionMarkerCanvas.Reset();
	MissionMarkerWidget.Reset();
}

void ASimCopterMissionSystemActor::RefreshMissionMarkerWidget()
{
	EnsureMissionMarkerWidget();
	if (!bShowMissionWorldMarkers || !MissionMarkerCanvas.IsValid() || !MissionMarkerWidget.IsValid())
	{
		return;
	}

	MissionMarkerCanvas->ClearChildren();

	TArray<FSimCopterMissionWorldMarkerEntry> Markers;
	BuildMissionWorldMarkers(Markers);

	const FVector2D ClampedMarkerSize(
		FMath::Clamp(MissionMarkerSize.X, 36.0f, 220.0f),
		FMath::Clamp(MissionMarkerSize.Y, 22.0f, 80.0f));

	for (const FSimCopterMissionWorldMarkerEntry& Marker : Markers)
	{
		FVector2D ScreenPosition;
		bool bClamped = false;
		if (!ProjectMissionMarkerToScreen(Marker.WorldLocation, ScreenPosition, bClamped))
		{
			continue;
		}

		const FVector2D DrawPosition(
			ScreenPosition.X - ClampedMarkerSize.X * 0.5f,
			ScreenPosition.Y - ClampedMarkerSize.Y * 0.5f);
		const FLinearColor BackgroundColor = WithAlpha(Marker.Color, bClamped ? 0.72f : 0.86f);

		MissionMarkerCanvas->AddSlot()
		.Offset(FMargin(DrawPosition.X, DrawPosition.Y, ClampedMarkerSize.X, ClampedMarkerSize.Y))
		.Alignment(FVector2D::ZeroVector)
		[
			SNew(SBox)
			.WidthOverride(ClampedMarkerSize.X)
			.HeightOverride(ClampedMarkerSize.Y)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
				.BorderBackgroundColor(BackgroundColor)
				.Padding(FMargin(7.0f, 3.0f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(Marker.Label))
					.Justification(ETextJustify::Center)
					.ColorAndOpacity(FLinearColor::White)
					.ShadowOffset(FVector2D(1.0f, 1.0f))
					.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13))
				]
			]
		];
	}

	MissionMarkerWidget->SetVisibility(Markers.Num() > 0 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
}

void ASimCopterMissionSystemActor::BuildMissionWorldMarkers(TArray<FSimCopterMissionWorldMarkerEntry>& OutMarkers) const
{
	OutMarkers.Reset();

	auto AddTileMarker = [this, &OutMarkers](int32 TileX, int32 TileY, const TCHAR* Label, const FString& Detail, const FLinearColor& Color)
	{
		FVector WorldLocation;
		if (!TryMakeMissionMarkerWorldLocation(TileX, TileY, WorldLocation))
		{
			return;
		}

		FSimCopterMissionWorldMarkerEntry Marker;
		Marker.WorldLocation = WorldLocation;
		Marker.Label = Label;
		Marker.Detail = Detail;
		Marker.Color = Color;
		OutMarkers.Add(Marker);
	};

	for (const SimCopterMissions::FSimCopterMissionRecord& Record : MissionSystem.GetRecords())
	{
		if (!Record.bActive || Record.TypeMask == 0)
		{
			continue;
		}

		const bool bHasDropoff = IsValidMissionTile(Record.SecondaryX, Record.SecondaryY);
		const bool bHasPassengerPickup = (Record.TypeMask & SimCopterMissions::TYPE_Transport) != 0;
		const bool bHasMedicalPickup = (Record.TypeMask & SimCopterMissions::TYPE_Medevac) != 0;
		const bool bHasRescuePickup = (Record.TypeMask & SimCopterMissions::TYPE_RescuePeople) != 0;
		const bool bPickedUpAnyPassenger = Record.VictimsPickedUp > 0 || Record.TransportDelivered > 0 || Record.RescueDelivered > 0 || Record.MedevacDelivered > 0;

		if (bHasPassengerPickup)
		{
			const int32 TransportOnboard = FMath::Max(0, Record.VictimsPickedUp - Record.TransportDelivered - Record.PassengersLost);
			const int32 TransportWaiting = FMath::Max(0, Record.TransportPassengers - Record.VictimsPickedUp - Record.PassengersLost);
			if (TransportOnboard > 0 && bHasDropoff)
			{
				AddTileMarker(Record.SecondaryX, Record.SecondaryY, TEXT("DROP"), Record.Name, FLinearColor(0.05f, 0.72f, 0.32f, 1.0f));
			}
			else if (TransportWaiting > 0)
			{
				AddTileMarker(Record.TileX, Record.TileY, TEXT("PICKUP"), Record.Name, FLinearColor(0.08f, 0.46f, 0.95f, 1.0f));
			}
			continue;
		}

		if (bHasMedicalPickup)
		{
			if (bPickedUpAnyPassenger && bHasDropoff)
			{
				AddTileMarker(Record.SecondaryX, Record.SecondaryY, TEXT("DROP"), Record.Name, FLinearColor(0.05f, 0.72f, 0.32f, 1.0f));
			}
			else
			{
				AddTileMarker(Record.TileX, Record.TileY, TEXT("PATIENT"), Record.Name, FLinearColor(0.8f, 0.12f, 0.55f, 1.0f));
			}
			continue;
		}

		if (bHasRescuePickup)
		{
			if (bPickedUpAnyPassenger && bHasDropoff)
			{
				AddTileMarker(Record.SecondaryX, Record.SecondaryY, TEXT("DROP"), Record.Name, FLinearColor(0.05f, 0.72f, 0.32f, 1.0f));
			}
			else
			{
				AddTileMarker(Record.TileX, Record.TileY, TEXT("RESCUE"), Record.Name, FLinearColor(0.95f, 0.55f, 0.08f, 1.0f));
			}
			continue;
		}

		if ((Record.TypeMask & (SimCopterMissions::TYPE_BuildingFire | SimCopterMissions::TYPE_CarFire)) != 0)
		{
			AddTileMarker(Record.TileX, Record.TileY, TEXT("FIRE"), Record.Name, FLinearColor(0.96f, 0.14f, 0.08f, 1.0f));
			continue;
		}

		if ((Record.TypeMask & SimCopterMissions::TYPE_TrafficJam) != 0)
		{
			AddTileMarker(Record.TileX, Record.TileY, TEXT("JAM"), Record.Name, FLinearColor(1.0f, 0.78f, 0.12f, 1.0f));
			continue;
		}

		if ((Record.TypeMask & SimCopterMissions::TYPE_Riot) != 0)
		{
			AddTileMarker(Record.TileX, Record.TileY, TEXT("RIOT"), Record.Name, FLinearColor(0.68f, 0.22f, 0.82f, 1.0f));
			continue;
		}

		if ((Record.TypeMask & (SimCopterMissions::TYPE_CriminalA | SimCopterMissions::TYPE_CriminalC |
			SimCopterMissions::TYPE_CriminalCar | SimCopterMissions::TYPE_SpeederEvent)) != 0)
		{
			AddTileMarker(Record.TileX, Record.TileY, TEXT("TARGET"), Record.Name, FLinearColor(0.86f, 0.18f, 0.18f, 1.0f));
			continue;
		}

		AddTileMarker(Record.TileX, Record.TileY, TEXT("MISSION"), Record.Name, FLinearColor(0.15f, 0.55f, 1.0f, 1.0f));
	}
}

bool ASimCopterMissionSystemActor::TryMakeMissionMarkerWorldLocation(int32 TileX, int32 TileY, FVector& OutWorldLocation) const
{
	if (!IsValidMissionTile(TileX, TileY))
	{
		return false;
	}

	if (const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		if (TrafficSystem->TryGetTileCenterWorldLocation(TileX, TileY, OutWorldLocation))
		{
			OutWorldLocation.Z += MissionMarkerWorldZOffsetCm;
			return true;
		}
	}

	return false;
}

bool ASimCopterMissionSystemActor::ProjectMissionMarkerToScreen(const FVector& WorldLocation, FVector2D& OutScreenPosition, bool& bOutClamped) const
{
	bOutClamped = false;
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	if (PlayerController == nullptr)
	{
		return false;
	}

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	PlayerController->GetViewportSize(ViewportX, ViewportY);
	if (ViewportX <= 0 || ViewportY <= 0)
	{
		return false;
	}

	FVector CameraLocation;
	FRotator CameraRotation;
	PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
	const FVector CameraLocal = CameraRotation.UnrotateVector(WorldLocation - CameraLocation);

	FVector2D ScreenPosition = FVector2D::ZeroVector;
	const bool bInFront = CameraLocal.X > 1.0f;
	bool bProjected = false;
	if (bInFront)
	{
		bProjected = PlayerController->ProjectWorldLocationToScreen(WorldLocation, ScreenPosition, true);
	}

	if (!bProjected)
	{
		const FVector2D ViewCenter(float(ViewportX) * 0.5f, float(ViewportY) * 0.5f);
		if (bInFront)
		{
			const float Aspect = FMath::Max(float(ViewportX) / FMath::Max(1.0f, float(ViewportY)), 0.01f);
			const float FovDeg = PlayerController->PlayerCameraManager != nullptr ? PlayerController->PlayerCameraManager->GetFOVAngle() : 90.0f;
			const float TanHalfHorizontalFov = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(FovDeg, 5.0f, 170.0f) * 0.5f));
			const float TanHalfVerticalFov = TanHalfHorizontalFov / Aspect;
			const float NormalizedX = CameraLocal.Y / (CameraLocal.X * TanHalfHorizontalFov);
			const float NormalizedY = CameraLocal.Z / (CameraLocal.X * TanHalfVerticalFov);
			ScreenPosition = FVector2D(
				ViewCenter.X + NormalizedX * ViewCenter.X,
				ViewCenter.Y - NormalizedY * ViewCenter.Y);
		}
		else
		{
			FVector2D Direction(CameraLocal.Y, -CameraLocal.Z);
			if (Direction.IsNearlyZero())
			{
				Direction = FVector2D(0.0f, 1.0f);
			}
			Direction.Normalize();
			ScreenPosition = ViewCenter + Direction * FMath::Max(float(ViewportX), float(ViewportY));
		}
	}

	const FVector2D ClampedMarkerSize(
		FMath::Clamp(MissionMarkerSize.X, 36.0f, 220.0f),
		FMath::Clamp(MissionMarkerSize.Y, 22.0f, 80.0f));
	const float MinX = MissionMarkerEdgePadding + ClampedMarkerSize.X * 0.5f;
	const float MaxX = float(ViewportX) - MissionMarkerEdgePadding - ClampedMarkerSize.X * 0.5f;
	const float MinY = MissionMarkerEdgePadding + ClampedMarkerSize.Y * 0.5f;
	const float MaxY = float(ViewportY) - MissionMarkerEdgePadding - ClampedMarkerSize.Y * 0.5f;
	const FVector2D ClampedPosition(
		FMath::Clamp(ScreenPosition.X, MinX, MaxX),
		FMath::Clamp(ScreenPosition.Y, MinY, MaxY));

	bOutClamped = !bInFront || FVector2D::Distance(ScreenPosition, ClampedPosition) > 0.5f;
	OutScreenPosition = ClampedPosition;
	return true;
}

void ASimCopterMissionSystemActor::PushMissionLogMessage(const FString& Text, const FLinearColor& Color)
{
	if (!bShowMissionMessageLog)
	{
		return;
	}

	FSimCopterMissionLogEntry Entry;
	Entry.Text = Text;
	Entry.Color = Color;
	Entry.RemainingSeconds = MessageLogDurationSeconds;
	MissionMessageLog.Insert(Entry, 0);

	while (MissionMessageLog.Num() > MaxMessageLogEntries)
	{
		MissionMessageLog.Pop(EAllowShrinking::No);
	}

	UE_LOG(LogTemp, Display, TEXT("[Mission] %s"), *Text);
	RefreshMessageLogWidget();
}

FString ASimCopterMissionSystemActor::FormatMissionUiMessage(const SimCopterMissions::FSimCopterMissionUiMessage& Message, FLinearColor& OutColor) const
{
	const FString MissionName = !Message.MissionName.IsEmpty()
		? Message.MissionName
		: FString::Printf(TEXT("Mission #%d"), Message.EventId);

	switch (Message.Kind)
	{
	case 5:
		OutColor = FLinearColor(0.65f, 0.9f, 1.0f, 1.0f);
		return FString::Printf(TEXT("Mission started: %s"), *MissionName);
	case 6:
		OutColor = Message.ValueA < 0 ? FLinearColor(1.0f, 0.38f, 0.32f, 1.0f) : FLinearColor(0.55f, 1.0f, 0.55f, 1.0f);
		return FString::Printf(
			TEXT("Mission ended: %s (%s, %s)"),
			*MissionName,
			*FormatSignedAmount(Message.ValueA, TEXT("points")),
			*FormatSignedAmount(Message.ValueB, TEXT("cash")));
	case 8:
		OutColor = Message.ValueA < 0 ? FLinearColor(1.0f, 0.38f, 0.32f, 1.0f) : FLinearColor(0.55f, 1.0f, 0.55f, 1.0f);
		return FString::Printf(
			TEXT("%s: %s (%s)"),
			GetMissionDeltaLabel(Message.TextId),
			*FormatSignedAmount(Message.ValueA, TEXT("points")),
			*MissionName);
	case 9:
		OutColor = Message.ValueA < 0 ? FLinearColor(1.0f, 0.38f, 0.32f, 1.0f) : FLinearColor(1.0f, 0.86f, 0.34f, 1.0f);
		return FString::Printf(
			TEXT("%s: %s (%s)"),
			GetMissionDeltaLabel(Message.TextId),
			*FormatSignedAmount(Message.ValueA, TEXT("cash")),
			*MissionName);
	default:
		OutColor = FLinearColor::White;
		return FString();
	}
}
