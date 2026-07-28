// Copyright Epic Games, Inc. All Rights Reserved.

#include "Missions/SimCopterMissionSystemActor.h"
#include "Sound/SoundWaveProcedural.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Ground/SimCopterAmbientVehicles.h"
#include "Ground/SimCopterFireRenderComponent.h"
#include "Ground/SimCopterParticleFX.h"
#include "Ground/SimCopterEffectFX.h"
#include "Ground/SimCopterGroundAgent.h"
#include "Ground/SimCopterOnFootPawn.h"
#include "Ground/SimCopterTrafficSystemActor.h"
#include "City/SimCity2000CityActor.h"
#include "City/SimCopterHangar.h"
#include "Game/SimCopterCareerSubsystem.h"
#include "UI/SimCopterHangarShop.h"
#include "Audio.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
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
	// Planes, boats and the train are ambient traffic, not mission props: the original ticks all
	// three pools from FUN_0047a760 whether or not a mission wants them.
	ResolveAmbientVehicles();
	EnsureMessageLogWidget();
	EnsureMissionMarkerWidget();
	EnsureMegaphonePromptWidget();

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
		EndMedevacHandoff(Handoff, /*bResolvePatients*/ false);
	}
	MedevacHandoffs.Reset();

	RemoveMegaphonePromptWidget();
	RemoveMissionMarkerWidget();
	RemoveMessageLogWidget();
	Super::EndPlay(EndPlayReason);
}

void ASimCopterMissionSystemActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (SessionMode == ESimCopterMissionSessionMode::Pending)
	{
		if (bSessionSelectionHeld)
		{
			// The session is still being set up: nothing simulates until it opens.
			return;
		}

		// The city level was entered directly (no main menu), so open the default session.
		StartCityJobsSession(0);
	}

	SessionElapsedSeconds += DeltaTime;

	MissionSystem.Tick(DeltaTime);
	ProcessPassengerTransfers();
	ProcessRescueTransfers();
	ProcessMedevacHospitalHandoffs(DeltaTime);
	UpdateMegaphonePrompt();
	UpdateFireVisuals(DeltaTime);

	// Only a scheduled-jobs session walks the career city list; the original's user-city mode
	// (DAT_00518d50 = 1) has no city to advance to.
	if (SessionMode == ESimCopterMissionSessionMode::CityJobs)
	{
		MissionSystem.AdvanceCareerIfComplete();
	}

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
	// FUN_004a6e60 never rolls a new job while the shell is up, and the hangar shell is the only
	// modal screen the remake has in-city.
	return ASimCopterHangar::IsAnyShellOpen(GetWorld());
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

void ASimCopterMissionSystemActor::HoldSessionForMenu()
{
	bSessionSelectionHeld = true;
	SessionMode = ESimCopterMissionSessionMode::Pending;
}

void ASimCopterMissionSystemActor::BeginSession(
	ESimCopterMissionSessionMode Mode,
	int32 CareerCityIndex,
	bool bAllowScheduledMissions)
{
	using namespace SimCopterMissions;

	// FUN_00408210: entering a city adopts its record. The career table is only present when
	// career.twk loaded; without it the system keeps whatever city it already has.
	if (MissionSystem.GetCareerCityCount() > 0)
	{
		const int32 ClampedIndex = FMath::Clamp(CareerCityIndex, 0, MissionSystem.GetCareerCityCount() - 1);
		MissionSystem.SelectCareerCity(ClampedIndex);
	}

	if (!bAllowScheduledMissions)
	{
		// The zero-weight city: FUN_004a6d20 sees a weight sum below 1.0 and writes an all-zero
		// cumulative table, so FUN_004a6e60's bucket comparisons never fire. Difficulty tier and
		// day/night still come from the selected city.
		FSimCopterCareerCity City = MissionSystem.GetCareerCity();
		for (float& Weight : City.Weights)
		{
			Weight = 0.0f;
		}
		MissionSystem.SetCareerCity(City);
	}

	// FUN_004080c0 / FUN_00407f30: $1000 and no points.
	MissionSystem.BeginSession();

	SessionMode = Mode;
	bSessionSelectionHeld = false;
	SessionElapsedSeconds = 0.0f;

	// The career record opens with the session: an empty log and the starter airframe on the
	// books. The prices the catalog quotes come from the same heli.twk the flight model reads.
	if (USimCopterCareerSubsystem* Career = GetGameInstance() != nullptr
			? GetGameInstance()->GetSubsystem<USimCopterCareerSubsystem>()
			: nullptr)
	{
		Career->EnsurePricesLoaded(ResolveOriginalGameRootDir());
		Career->BeginCareer();

		// 534 "Entered City: %s, %s" - the original prints the city's name and the date it was
		// entered. The remake has neither: the hardcoded per-city map names in FUN_00408370 are
		// not ported and there is no calendar, so the record's own index stands in.
		Career->AddLogEntry(
			ESimCopterCareerLogKind::EnteredCity,
			FString::Printf(TEXT("Entered City: City%d, tier %d"),
				MissionSystem.GetCareerCityIndex(),
				MissionSystem.GetDifficultyTier()),
			0,
			0.0f);
	}
}

void ASimCopterMissionSystemActor::StartFreeRoamSession(int32 CareerCityIndex)
{
	BeginSession(ESimCopterMissionSessionMode::FreeRoam, CareerCityIndex, /*bAllowScheduledMissions=*/false);
	UE_LOG(LogTemp, Display, TEXT("SimCopter session: free roam (city %d, tier %d, no scheduled jobs)"),
		MissionSystem.GetCareerCityIndex(), MissionSystem.GetDifficultyTier());
}

void ASimCopterMissionSystemActor::StartCityJobsSession(int32 CareerCityIndex, bool bFirstJobImmediately)
{
	BeginSession(ESimCopterMissionSessionMode::CityJobs, CareerCityIndex, /*bAllowScheduledMissions=*/true);
	UE_LOG(LogTemp, Display, TEXT("SimCopter session: city %d jobs (tier %d, %d points needed)"),
		MissionSystem.GetCareerCityIndex(),
		MissionSystem.GetDifficultyTier(),
		MissionSystem.GetCareerCity().PointsNeeded);

	if (bFirstJobImmediately)
	{
		const bool bCreated = MissionSystem.RollScheduledMissionNow();
		UE_LOG(LogTemp, Display, TEXT("SimCopter session: opening job rolled immediately -> %s"),
			bCreated ? TEXT("created") : TEXT("nothing placed (the scheduler will try again)"));
	}
}

int32 ASimCopterMissionSystemActor::StartSingleMissionSession(int32 CareerCityIndex, int32 TypeMask)
{
	BeginSession(ESimCopterMissionSessionMode::SingleMission, CareerCityIndex, /*bAllowScheduledMissions=*/false);
	return StartMissionNow(TypeMask);
}

int32 ASimCopterMissionSystemActor::StartMissionNow(int32 TypeMask)
{
	const int32 EventId = MissionSystem.CreateEventOfType(TypeMask);
	UE_LOG(LogTemp, Display, TEXT("SimCopter session: mission %s (mask 0x%x) at city %d tier %d -> event %d"),
		SimCopterMissions::FSimCopterMissionSystem::GetTypeDisplayName(TypeMask),
		TypeMask,
		MissionSystem.GetCareerCityIndex(),
		MissionSystem.GetDifficultyTier(),
		EventId);

	return EventId == -1 ? INDEX_NONE : EventId;
}

bool ASimCopterMissionSystemActor::GetCareerCityInfo(int32 Index, SimCopterMissions::FSimCopterCareerCity& OutCity) const
{
	if (const SimCopterMissions::FSimCopterCareerCity* City = MissionSystem.GetCareerCityByIndex(Index))
	{
		OutCity = *City;
		return true;
	}
	return false;
}

// SCHOOK: DouseWaterParticle 0x004a50c0
int32 ASimCopterMissionSystemActor::ApplyWaterParticleImpact(
	const FVector& ImpactWorldLocation,
	const int32 Strength1616)
{
	ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem == nullptr || Strength1616 <= 0)
	{
		return 0;
	}

	int32 FlamesHit = 0;

	// FUN_004a50c0 compares the trajectory impact offset inside the cell against each flame's
	// own offset. Do not douse at the bucket/emitter location.
	int32 TileX = INDEX_NONE;
	int32 TileY = INDEX_NONE;
	if (TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(ImpactWorldLocation, TileX, TileY))
	{
		FVector TileCenter;
		int32 LocalX1616 = 0;
		int32 LocalY1616 = 0;
		int32 LocalZ1616 = 0;
		if (TrafficSystem->TryGetTileCenterWorldLocation(TileX, TileY, TileCenter))
		{
			TrafficSystem->ConvertWorldOffsetToOriginal(
				ImpactWorldLocation - TileCenter, LocalX1616, LocalY1616, LocalZ1616);
		}
		FlamesHit += MissionSystem.DouseAtLocalOffset(
			TileX,
			TileY,
			LocalX1616,
			LocalZ1616,
			Strength1616);
	}

	// Car fires are likewise resolved where the trajectory landed.
	TArray<int32> ExtinguishedCarEvents;
	TrafficSystem->DouseBurningVehiclesNear(ImpactWorldLocation, CarDouseRadiusCm, ExtinguishedCarEvents);
	for (int32 EventId : ExtinguishedCarEvents)
	{
		MissionSystem.PostEvent(SimCopterMissions::EVT_CarDoused, EventId, 1, false);
		MissionSystem.PostEvent(SimCopterMissions::EVT_CarCleared, EventId, 1, false);
		++FlamesHit;
	}

	// So is burning crash wreckage - the plane's airframe and the derailed carriages. They count
	// as doused, not cleared: there is no jam to break up, just a fire to put out.
	if (ASimCopterAmbientVehiclesActor* Vehicles = ResolveAmbientVehicles())
	{
		TArray<int32> ExtinguishedWreckEvents;
		Vehicles->DouseBurningWrecksNear(ImpactWorldLocation, CarDouseRadiusCm, ExtinguishedWreckEvents);
		for (int32 EventId : ExtinguishedWreckEvents)
		{
			MissionSystem.PostEvent(SimCopterMissions::EVT_CarDoused, EventId, 1, false);
			++FlamesHit;
		}
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
	const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	const ASimCity2000CityActor* City =
		TrafficSystem != nullptr ? TrafficSystem->GetCityActor() : nullptr;
	if (World == nullptr || City == nullptr)
	{
		return false;
	}

	const FVector Start(WorldXY.X, WorldXY.Y, WorldXY.Z + 6000.0f);
	const FVector End(WorldXY.X, WorldXY.Y, WorldXY.Z - 6000.0f);

	FCollisionQueryParams Params(FName(TEXT("SimCopterFireSurface")), /*bTraceComplex*/ false, this);

	// Fire is seated on the authored city surface. A single visibility hit could select the
	// helicopter, a pedestrian, or another transient actor flying over the flame, which made the
	// fire jump onto that actor. Walk the trace and accept only city terrain/building collision.
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	TArray<FHitResult> Hits;
	if (!World->LineTraceMultiByObjectType(Hits, Start, End, ObjectParams, Params))
	{
		return false;
	}

	for (const FHitResult& Hit : Hits)
	{
		const UPrimitiveComponent* HitComponent = Hit.GetComponent();
		if (City->IsTerrainCollisionComponent(HitComponent) ||
			City->IsBuildingCollisionHit(HitComponent, Hit.ImpactPoint))
		{
			OutTopZ = Hit.ImpactPoint.Z;
			return true;
		}
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
	// Crashed planes and derailed carriages burn through the same path.
	if (ASimCopterAmbientVehiclesActor* Vehicles = ResolveAmbientVehicles())
	{
		Vehicles->GetBurningWrecks(BurningVehicles);
	}
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

ASimCopterAmbientVehiclesActor* ASimCopterMissionSystemActor::ResolveAmbientVehicles()
{
	if (ASimCopterAmbientVehiclesActor* Cached = CachedAmbientVehicles.Get())
	{
		return Cached;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	ASimCopterAmbientVehiclesActor* Found = Cast<ASimCopterAmbientVehiclesActor>(
		UGameplayStatics::GetActorOfClass(World, ASimCopterAmbientVehiclesActor::StaticClass()));
	if (Found == nullptr)
	{
		// The pools are pure runtime state, so a level that has not been re-saved with the actor
		// still gets planes, boats and a train.
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Found = World->SpawnActor<ASimCopterAmbientVehiclesActor>(
			ASimCopterAmbientVehiclesActor::StaticClass(), FTransform::Identity, SpawnParams);
	}

	CachedAmbientVehicles = Found;
	return Found;
}

int32 ASimCopterMissionSystemActor::CreateMissionAt(int32 TileX, int32 TileY, int32 TypeMask)
{
	return MissionSystem.CreateEventAt(TileX, TileY, TypeMask);
}

void ASimCopterMissionSystemActor::PostMissionEvent(int32 Code, int32 EventId, int32 Value, bool bSilent)
{
	MissionSystem.PostEvent(Code, EventId, Value, bSilent);
}

void ASimCopterMissionSystemActor::PostMissionEventAt(int32 Code, int32 EventId, int32 X, int32 Y, int32 Value, bool bSilent)
{
	SimCopterMissions::FSimCopterMissionEvent Event;
	Event.Code = Code;
	Event.EventId = EventId;
	Event.X = X;
	Event.Y = Y;
	Event.Value = Value;
	Event.bSilent = bSilent;
	MissionSystem.PostEvent(Event);
}

void ASimCopterMissionSystemActor::RemoveMissionPeople(int32 EventId)
{
	if (EventId == INDEX_NONE)
	{
		return;
	}
	if (ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		TrafficSystem->RemoveMissionPeople(EventId);
	}
}

bool ASimCopterMissionSystemActor::TryActivatePlaneCrash(int32 EventId)
{
	if (ASimCopterAmbientVehiclesActor* Vehicles = ResolveAmbientVehicles())
	{
		return Vehicles->TryActivatePlaneCrash(EventId);
	}
	return false;
}

bool ASimCopterMissionSystemActor::TryActivateTrainCrash(int32 EventId)
{
	if (ASimCopterAmbientVehiclesActor* Vehicles = ResolveAmbientVehicles())
	{
		return Vehicles->TryActivateTrainCrash(EventId);
	}
	return false;
}

bool ASimCopterMissionSystemActor::TryActivateBoatRescue(
	int32 EventId,
	int32 Timer1616,
	int32 TileX,
	int32 TileY,
	int32& OutTileX,
	int32& OutTileY)
{
	if (ASimCopterAmbientVehiclesActor* Vehicles = ResolveAmbientVehicles())
	{
		return Vehicles->TryActivateBoatRescue(
			EventId, static_cast<float>(Timer1616) / 65536.0f, TileX, TileY, OutTileX, OutTileY);
	}
	return false;
}

bool ASimCopterMissionSystemActor::TryActivateTrainRescue(int32 EventId, int32 Timer1616, int32& OutTileX, int32& OutTileY)
{
	if (ASimCopterAmbientVehiclesActor* Vehicles = ResolveAmbientVehicles())
	{
		return Vehicles->TryActivateTrainRescue(
			EventId, static_cast<float>(Timer1616) / 65536.0f, OutTileX, OutTileY);
	}
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
	Victim->ResetMissionActionTracking();
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
		PlayOriginalClip(*Sound, Volume / 255.0f);
	}
}

void ASimCopterMissionSystemActor::PlayUiSound(int32 SoundId)
{
	if (USoundBase** Sound = UiSounds.Find(SoundId))
	{
		PlayOriginalClip(*Sound);
	}
}

bool ASimCopterMissionSystemActor::TryActivateSpeederCar(int32 EventId, int32 TileX, int32 TileY)
{
	// FUN_004b84f0 -> FUN_004b8540: the traffic system owns the pool and the road placement.
	if (ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		return TrafficSystem->TryActivateSpeederCar(EventId, TileX, TileY);
	}
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

	WriteCareerLogEntry(Message);
}

// The hangar's Mission Log page prints the original's own lines (strings 536, 537, 540 and 541),
// so the same four message kinds that drive the HUD ticker are re-formatted into the career log
// with the type name string 570 + n gives them.
void ASimCopterMissionSystemActor::WriteCareerLogEntry(const SimCopterMissions::FSimCopterMissionUiMessage& Message)
{
	USimCopterCareerSubsystem* Career = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<USimCopterCareerSubsystem>()
		: nullptr;
	if (Career == nullptr)
	{
		return;
	}

	const FString TypeName = SimCopterHangarShop::GetMissionTypeLogName(Message.TypeMask);
	const FString MissionName = Message.MissionName.IsEmpty()
		? TypeName
		: Message.MissionName;

	FString Line;
	ESimCopterCareerLogKind Kind = ESimCopterCareerLogKind::MissionStarted;
	switch (Message.Kind)
	{
	case 5:
		// 536 "%s: Started %s%s" - the trailing pair is the original's "Directly "/"Delayed"
		// (strings 548/549), which describes how the job was scheduled; the remake's scheduler
		// always places on the delayed path.
		Kind = ESimCopterCareerLogKind::MissionStarted;
		Line = FString::Printf(TEXT("%s: Started %s"), *MissionName, TEXT("Delayed"));
		break;
	case 6:
		// 537 "%s: Ended, Award: %ld Points, %ld Bucks"
		Kind = ESimCopterCareerLogKind::MissionEnded;
		Line = FString::Printf(TEXT("%s: Ended, Award: %d Points, %d Bucks"), *MissionName, Message.ValueA, Message.ValueB);
		break;
	case 8:
		// 541 "%s: %s %ld Points"
		Kind = ESimCopterCareerLogKind::PointsAward;
		Line = FString::Printf(TEXT("%s: %s %d Points"), *MissionName, GetMissionDeltaLabel(Message.TextId), Message.ValueA);
		break;
	case 9:
		// 540 "%s: %s %ld Bucks"
		Kind = ESimCopterCareerLogKind::CashAward;
		Line = FString::Printf(TEXT("%s: %s %d Bucks"), *MissionName, GetMissionDeltaLabel(Message.TextId), Message.ValueA);
		break;
	default:
		return;
	}

	Career->AddLogEntry(Kind, Line, Message.TypeMask, SessionElapsedSeconds);
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

bool ASimCopterMissionSystemActor::TryGetMissionDestinationTile(
	const int32 EventId,
	int32& OutTileX,
	int32& OutTileY) const
{
	OutTileX = INDEX_NONE;
	OutTileY = INDEX_NONE;
	const SimCopterMissions::FSimCopterMissionRecord* Record = MissionSystem.FindRecord(EventId);
	if (Record == nullptr ||
		!Record->bActive ||
		!IsValidMissionTile(Record->SecondaryX, Record->SecondaryY))
	{
		return false;
	}

	OutTileX = Record->SecondaryX;
	OutTileY = Record->SecondaryY;
	return true;
}

bool ASimCopterMissionSystemActor::NotifyMissionPersonBoarded(ASimCopterGroundAgent* Person)
{
	if (Person == nullptr ||
		Person->MissionEventId == INDEX_NONE ||
		Person->HasMissionResolutionReported() ||
		Person->IsMissionPickupCounted() ||
		!Person->HasClaimedPassengerSeat())
	{
		return false;
	}

	const ASimCopterHelicopterPawn* Helicopter =
		Cast<ASimCopterHelicopterPawn>(Person->GetBehaviorCarrier());
	if (Helicopter == nullptr ||
		Helicopter->GetMissionPassengerCount(
			Person->MissionEventId,
			Person->GetMissionPassengerKind()) <= 0)
	{
		// Opcode 13 can only acknowledge an action that already happened. A decoded or partial
		// behavior path saying "picked up" is not permission to synthesize a passenger.
		return false;
	}

	const SimCopterMissions::FSimCopterMissionRecord* Record =
		MissionSystem.FindRecord(Person->MissionEventId);
	if (Record == nullptr || !Record->bActive)
	{
		return false;
	}

	// Reboarding after a seat-window drop restores the counter silently: the first pickup already
	// paid and announced. A single real person can therefore cross the VM, on-foot, and recovery
	// paths without any of them double-crediting the mission.
	const bool bSilent = Person->HasMissionPickupCreditAwarded();
	MissionSystem.PostEvent(
		SimCopterMissions::EVT_VictimPickedUp,
		Person->MissionEventId,
		1,
		bSilent);
	Person->SetMissionPickupCreditAwarded(true);
	Person->SetMissionPickupCounted(true);
	return true;
}

int32 ASimCopterMissionSystemActor::PostPassengerDelivery(
	const int32 EventId,
	const ESimCopterMissionPassengerKind Kind,
	const int32 RequestedCount,
	const bool bSilent)
{
	const SimCopterMissions::FSimCopterMissionRecord* Record = MissionSystem.FindRecord(EventId);
	if (Record == nullptr || !Record->bActive || RequestedCount <= 0)
	{
		return 0;
	}

	int32 EventCode = INDEX_NONE;
	int32 Remaining = 0;
	switch (Kind)
	{
	case ESimCopterMissionPassengerKind::Transport:
		if ((Record->TypeMask & SimCopterMissions::TYPE_Transport) != 0)
		{
			EventCode = SimCopterMissions::EVT_TransportDelivered;
			Remaining = Record->TransportPassengers - Record->TransportDelivered -
				Record->PassengersLost - Record->Casualties;
		}
		break;
	case ESimCopterMissionPassengerKind::Medevac:
		if ((Record->TypeMask & SimCopterMissions::TYPE_Medevac) != 0)
		{
			EventCode = SimCopterMissions::EVT_MedevacDelivered;
			Remaining = Record->MedevacVictims - Record->MedevacDelivered - Record->Casualties;
		}
		break;
	case ESimCopterMissionPassengerKind::Rescue:
		if ((Record->TypeMask & SimCopterMissions::TYPE_RescuePeople) != 0)
		{
			EventCode = SimCopterMissions::EVT_RescueDelivered;
			Remaining = Record->RescueVictims - Record->RescueDelivered - Record->Casualties;
		}
		break;
	default:
		break;
	}

	const int32 Delivered = FMath::Clamp(RequestedCount, 0, FMath::Max(0, Remaining));
	if (EventCode != INDEX_NONE && Delivered > 0)
	{
		MissionSystem.PostEvent(EventCode, EventId, Delivered, bSilent);
	}
	return Delivered;
}

bool ASimCopterMissionSystemActor::NotifyMissionPersonDelivered(ASimCopterGroundAgent* Person)
{
	if (Person == nullptr ||
		Person->MissionEventId == INDEX_NONE ||
		Person->HasMissionResolutionReported() ||
		!Person->IsMissionPickupCounted() ||
		Person->HasClaimedPassengerSeat() ||
		Cast<ASimCopterHelicopterPawn>(Person->GetBehaviorCarrier()) != nullptr)
	{
		return false;
	}

	const int32 Delivered = PostPassengerDelivery(
		Person->MissionEventId,
		Person->GetMissionPassengerKind(),
		1);
	if (Delivered <= 0)
	{
		return false;
	}

	Person->SetMissionResolutionReported(true);
	Person->SetMissionPickupCounted(false);
	return true;
}

bool ASimCopterMissionSystemActor::NotifyMissionPersonDied(ASimCopterGroundAgent* Person)
{
	if (Person == nullptr ||
		Person->MissionEventId == INDEX_NONE ||
		Person->HasMissionResolutionReported())
	{
		return false;
	}

	const SimCopterMissions::FSimCopterMissionRecord* Record =
		MissionSystem.FindRecord(Person->MissionEventId);
	if (Record == nullptr || !Record->bActive)
	{
		return false;
	}

	const ESimCopterMissionPassengerKind Kind = Person->GetMissionPassengerKind();
	int32 Remaining = 0;
	switch (Kind)
	{
	case ESimCopterMissionPassengerKind::Transport:
		Remaining = Record->TransportPassengers - Record->TransportDelivered -
			Record->PassengersLost - Record->Casualties;
		break;
	case ESimCopterMissionPassengerKind::Medevac:
		Remaining = Record->MedevacVictims - Record->MedevacDelivered - Record->Casualties;
		break;
	case ESimCopterMissionPassengerKind::Rescue:
		Remaining = Record->RescueVictims - Record->RescueDelivered - Record->Casualties;
		break;
	default:
		break;
	}
	if (Remaining <= 0)
	{
		return false;
	}

	MissionSystem.PostEvent(SimCopterMissions::EVT_PersonDied, Person->MissionEventId, 1);
	Person->SetMissionResolutionReported(true);
	Person->SetMissionPickupCounted(false);
	return true;
}

void ASimCopterMissionSystemActor::NotifyPassengerDroppedFromHelicopter(
	int32 EventId,
	ESimCopterMissionPassengerKind Kind,
	int32 Count)
{
	(void)Kind;
	if (EventId == INDEX_NONE || Count <= 0)
	{
		return;
	}

	// Every kind gives back its pickup credit when it leaves the cabin without being delivered -
	// otherwise a rescue whose survivors were dumped mid-air could never be finished.
	MissionSystem.AdjustVictimsPickedUp(EventId, -Count);
}

bool ASimCopterMissionSystemActor::CanHospitalParamedicBoardPlayerHelicopter(
	const ASimCopterHelicopterPawn* Helicopter) const
{
	if (Helicopter == nullptr)
	{
		return false;
	}

	bool bHasWaitingMedevacPatient = false;
	for (const SimCopterMissions::FSimCopterMissionRecord& Record : MissionSystem.GetRecords())
	{
		if (!Record.bActive || (Record.TypeMask & SimCopterMissions::TYPE_Medevac) == 0)
		{
			continue;
		}

		if (Helicopter->GetMissionPassengerCount(
				Record.EventId,
				ESimCopterMissionPassengerKind::Medevac) > 0)
		{
			return false;
		}

		const int32 Waiting = Record.MedevacVictims -
			Record.VictimsPickedUp -
			Record.Casualties;
		bHasWaitingMedevacPatient |= Waiting > 0;
	}
	return bHasWaitingMedevacPatient;
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

int32 ASimCopterMissionSystemActor::ReleaseMissionPassengersFromHelicopter(
	ASimCopterHelicopterPawn* Helicopter,
	const int32 EventId,
	const ESimCopterMissionPassengerKind Kind,
	const int32 MaxCount,
	const FVector& DropLocation,
	const bool bRemoveAfterDelivery)
{
	ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (Helicopter == nullptr || TrafficSystem == nullptr || MaxCount <= 0)
	{
		return 0;
	}

	int32 Processed = 0;
	int32 Delivered = 0;
	while (Processed < MaxCount)
	{
		ASimCopterGroundAgent* Person =
			TrafficSystem->FindPersonAboardForEvent(Helicopter, EventId, Kind);
		if (Person == nullptr)
		{
			break;
		}
		if (Kind == ESimCopterMissionPassengerKind::Rescue &&
			Person->GetBehaviorAttribute(EBhavAttr::State) == 2 &&
			Person->IsAtBehaviorHomeTile())
		{
			// BHAV 303 explicitly refuses to deliver a building/roof rescue on its placement
			// tile. Preserve that rule in the recovery path so landing at the pickup cannot both
			// board and instantly complete the mission.
			break;
		}

		Person->AlightFromCarrier(); // atomically returns this real person's seat
		const float Side = (Processed & 1) == 0 ? 1.0f : -1.0f;
		Person->SetActorLocation(
			DropLocation + FVector(0.0f, Side * (35.0f + 28.0f * float(Processed)), 0.0f),
			false);
		Person->SnapToGroundImmediate();
		if (NotifyMissionPersonDelivered(Person))
		{
			Delivered++;
		}

		if (bRemoveAfterDelivery)
		{
			Person->Destroy();
		}
		else
		{
			Person->MissionEventId = INDEX_NONE;
			Person->InitialPersonState = 0;
			Person->ClearMissionPose();
			Person->ResumeNormalPedestrianBehavior();
		}
		Processed++;
	}

	// Compatibility for seats created before real-person ownership was introduced (or by a test
	// fixture). Only the unmatched slots use stand-ins; the normal path never replaces a person.
	const int32 LegacyRequested = MaxCount - Processed;
	if (LegacyRequested > 0)
	{
		const int32 LegacySlots = FMath::Min(
			LegacyRequested,
			Helicopter->GetMissionPassengerCount(EventId, Kind));
		const int32 Removed = Helicopter->RemoveMissionPassengersForMission(
			LegacySlots, EventId, Kind);
		const int32 LegacyDelivered = PostPassengerDelivery(EventId, Kind, Removed);
		Delivered += LegacyDelivered;
		if (!bRemoveAfterDelivery && LegacyDelivered > 0)
		{
			TrafficSystem->SpawnMissionPeopleAtWorldLocation(
				LegacyDelivered,
				DropLocation,
				INDEX_NONE,
				0,
				-1,
				185.0f);
		}
	}

	return Delivered;
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
		FVector DropoffLocation = FVector::ZeroVector;
		const bool bHasDropoffLocation =
			IsValidMissionTile(Mission.DropoffX, Mission.DropoffY) &&
			TrafficSystem->TryGetTileCenterWorldLocation(Mission.DropoffX, Mission.DropoffY, DropoffLocation);

		if (Mission.bTransport && bHasDropoffLocation)
		{
			for (ASimCopterHelicopterPawn* Helicopter : Helicopters)
			{
				if (Helicopter == nullptr || !IsWorldLocationNearTile(Helicopter->GetActorLocation(), Mission.DropoffX, Mission.DropoffY, PassengerDropoffRadiusCm))
				{
					continue;
				}

				const int32 OnHelicopter = Helicopter->GetMissionPassengerCount(
					Mission.EventId,
					ESimCopterMissionPassengerKind::Transport);
				if (OnHelicopter <= 0)
				{
					continue;
				}

				ReleaseMissionPassengersFromHelicopter(
					Helicopter,
					Mission.EventId,
					ESimCopterMissionPassengerKind::Transport,
					FMath::Min(OnHelicopter, Mission.DeliverablePassengers),
					Helicopter->GetPassengerDropWorldLocation(),
					/*bRemoveAfterDelivery*/ false);
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

			const int32 SeatsAvailable = FMath::Min(
				Mission.WaitingPassengers,
				Helicopter->GetAvailablePassengerSeats());
			if (SeatsAvailable <= 0)
			{
				continue;
			}

			// The passenger's exact event id and physical proximity are authoritative. BHAV 291
			// follows the player from four tiles away, so by the time they reach a helicopter it
			// is wrong to reject them merely because the pilot touched down just outside a
			// radius around the mission record's original tile.
			const int32 NextSeatIndex = Helicopter->GetMissionPassengerSlots().Num();
			const FVector CabinDoor = Helicopter->GetPassengerDropWorldLocation(NextSeatIndex);
			TrafficSystem->GuideMissionPeopleToLocation(
				Mission.EventId,
				CabinDoor,
				CabinDoor,
				SeatsAvailable,
				PassengerPickupRadiusCm,
				PassengerTransferMaxVerticalDeltaCm,
				PassengerBoardGuidanceSeconds);

			// Boarding attaches the passenger to the helicopter and claims their seat as part of
			// the pickup. BoardCarrier also invokes the idempotent mission action service, so this
			// loop neither books a second seat nor posts a second pickup event.
			const int32 PickedUp = TrafficSystem->BoardMissionPeopleTouching(
				Mission.EventId,
				CabinDoor,
				SeatsAvailable,
				PassengerBoardTouchRadiusCm,
				PassengerTransferMaxVerticalDeltaCm,
				nullptr,
				Helicopter);
			if (PickedUp <= 0)
			{
				continue;
			}

			break;
		}
	}
}

void ASimCopterMissionSystemActor::ProcessRescueTransfers()
{
	// The decoded program still decides how survivors approach the harness and when they want to
	// leave. This loop is the stability backstop around the same authoritative BoardCarrier /
	// AlightFromCarrier actions: swimmers and train riders cannot be allowed to strand a mission
	// forever because one movement opcode misses a moving target.
	ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem == nullptr || GetWorld() == nullptr)
	{
		return;
	}

	struct FRescueSnapshot
	{
		int32 EventId = INDEX_NONE;
		int32 Waiting = 0;
		int32 Deliverable = 0;
	};
	TArray<FRescueSnapshot, TInlineAllocator<8>> Missions;
	for (const SimCopterMissions::FSimCopterMissionRecord& Record : MissionSystem.GetRecords())
	{
		if (!Record.bActive || (Record.TypeMask & SimCopterMissions::TYPE_RescuePeople) == 0)
		{
			continue;
		}
		FRescueSnapshot& Snapshot = Missions.AddDefaulted_GetRef();
		Snapshot.EventId = Record.EventId;
		Snapshot.Waiting = FMath::Max(
			0,
			Record.RescueVictims - Record.VictimsPickedUp - Record.Casualties);
		Snapshot.Deliverable = FMath::Max(
			0,
			Record.RescueVictims - Record.RescueDelivered - Record.Casualties);
	}

	TArray<AActor*> HelicopterActors;
	UGameplayStatics::GetAllActorsOfClass(
		GetWorld(), ASimCopterHelicopterPawn::StaticClass(), HelicopterActors);
	for (const FRescueSnapshot& Mission : Missions)
	{
		int32 Waiting = Mission.Waiting;
		for (AActor* Actor : HelicopterActors)
		{
			ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(Actor);
			if (Helicopter == nullptr)
			{
				continue;
			}

			if (Waiting > 0 && Helicopter->GetAvailablePassengerSeats() > 0)
			{
				FVector PickupPoint = Helicopter->GetActorLocation();
				float PickupRadius = Helicopter->GetSimpleCollisionRadius() + RescueBoardTouchMarginCm;
				float PickupVertical = Helicopter->GetSimpleCollisionHalfHeight() + RescueBoardTouchMarginCm;
				bool bUseHarness = false;
				FVector RopeEnd = FVector::ZeroVector;
				if (Helicopter->IsHarnessRopeEndSelected() &&
					Helicopter->TryGetRopeEndWorldLocation(RopeEnd))
				{
					bUseHarness = true;
					PickupPoint = RopeEnd;
					PickupRadius = RescueHarnessReachCm;
					PickupVertical = RescueHarnessReachCm;
				}

				// Direct cabin entry is a landed-helicopter action. Harness boarding remains valid
				// in flight and claims its cabin seat only when op 58 winds the rider in.
				if (bUseHarness || Helicopter->CanTransferMissionPassengers())
				{
					const int32 Boarded = TrafficSystem->BoardMissionPeopleTouching(
						Mission.EventId,
						PickupPoint,
						bUseHarness ? 1 : FMath::Min(
							Waiting,
							Helicopter->GetAvailablePassengerSeats()),
						PickupRadius,
						PickupVertical,
						nullptr,
						Helicopter,
						bUseHarness);
					Waiting = FMath::Max(0, Waiting - Boarded);
				}
			}

			if (Mission.Deliverable <= 0 || !Helicopter->CanTransferMissionPassengers())
			{
				continue;
			}

			int32 DropTileX = INDEX_NONE;
			int32 DropTileY = INDEX_NONE;
			if (!TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(
					Helicopter->GetActorLocation(), DropTileX, DropTileY) ||
				TrafficSystem->IsWaterTile(DropTileX, DropTileY))
			{
				continue;
			}

			const int32 Delivered = ReleaseMissionPassengersFromHelicopter(
				Helicopter,
				Mission.EventId,
				ESimCopterMissionPassengerKind::Rescue,
				Mission.Deliverable,
				Helicopter->GetPassengerDropWorldLocation(),
				/*bRemoveAfterDelivery*/ false);
			if (Delivered > 0)
			{
				break;
			}
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

	// FUN_004c25b0 explicitly spawns class 0x0c / state 5 on XBLD D1, whose BHAV 801 is the
	// hospital paramedic. Use that visible roof worker for the deterministic handoff when one is
	// available; only create a temporary EMT when ambient population did not provide one.
	ASimCopterGroundAgent* Emt = TrafficSystem->FindNearestAvailablePersonInState(
		HospitalCenter,
		5,
		MedevacHospitalHandoffRadiusCm);
	const bool bOwnsEmt = Emt == nullptr;
	if (Emt != nullptr)
	{
		Emt->SetMissionScriptedMover();
	}
	else
	{
		Emt = TrafficSystem->SpawnScriptedMissionAgent(
			DoorwayFeet, INDEX_NONE, TEXT("Medik"), false, 1.0f);
	}
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
	Handoff.PhaseSeconds = 0.0f;
	Handoff.bOwnsEmt = bOwnsEmt;
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
	Handoff.PhaseSeconds += DeltaSeconds;

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

		if (FVector::DistSquared2D(Emt->GetActorLocation(), Handoff.HeliDoorLocation) <= FMath::Square(MedevacEmtReachRadiusCm) ||
			Handoff.PhaseSeconds >= 12.0f)
		{
			ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
			ASimCopterGroundAgent* Patient = TrafficSystem != nullptr
				? TrafficSystem->FindPersonAboardForEvent(
					Helicopter,
					Handoff.EventId,
					ESimCopterMissionPassengerKind::Medevac)
				: nullptr;
			if (Patient != nullptr)
			{
				Patient->AlightFromCarrier(); // removes this real person's own slot
				Patient->SetActorLocation(Handoff.HeliDoorLocation, false);
			}
			else
			{
				const int32 Removed = Helicopter->RemoveMissionPassengersForMission(
					1,
					Handoff.EventId,
					ESimCopterMissionPassengerKind::Medevac);
				if (Removed <= 0)
				{
					return false;
				}
				Patient = TrafficSystem != nullptr
					? TrafficSystem->SpawnScriptedMissionAgent(
						Handoff.HeliDoorLocation, Handoff.EventId, FString(), true, 1.0f)
					: nullptr;
			}

			if (Patient == nullptr)
			{
				// The seat was real even if an old save/test supplied no actor. Resolve that one
				// unit rather than leave an invisible mission dependency.
				PostPassengerDelivery(
					Handoff.EventId,
					ESimCopterMissionPassengerKind::Medevac,
					1);
				Handoff.PhaseSeconds = 0.0f;
				return Helicopter->GetMissionPassengerCount(
					Handoff.EventId,
					ESimCopterMissionPassengerKind::Medevac) > 0;
			}

			// The scripted carry temporarily pauses the patient's own VM. The behavior requested
			// the action; the stable carrier service owns the transform and the eventual outcome.
			Patient->SetCarriedBy(
				Emt->GetRootComponent(),
				FVector(40.0f, 0.0f, -6.0f),
				FRotator(0.0f, 90.0f, 88.0f));
			Handoff.CarriedPatient = Patient;
			Emt->ClearMoveTarget();
			Handoff.Phase = 1;
			Handoff.PhaseSeconds = 0.0f;
		}
		return true;
	}

	// Phase 1: carry the patient to the hospital doorway.
	FVector Target = Handoff.DoorwayLocation;
	Target.Z = Emt->GetActorLocation().Z;
	Emt->SetMoveTarget(Target);

	if (FVector::DistSquared2D(Emt->GetActorLocation(), Handoff.DoorwayLocation) <= FMath::Square(MedevacEmtReachRadiusCm) ||
		Handoff.PhaseSeconds >= 12.0f)
	{
		if (ASimCopterGroundAgent* Patient = Handoff.CarriedPatient.Get())
		{
			NotifyMissionPersonDelivered(Patient);
			Patient->Destroy(); // taken inside the hospital
		}
		Handoff.CarriedPatient.Reset();
		Emt->ClearMoveTarget();

		// Keep going while the helicopter still holds patients for this mission.
		if (Helicopter->GetMissionPassengerCount(Handoff.EventId, ESimCopterMissionPassengerKind::Medevac) > 0)
		{
			Handoff.Phase = 0;
			Handoff.PhaseSeconds = 0.0f;
		}
		else
		{
			return false; // finished
		}
	}
	return true;
}

void ASimCopterMissionSystemActor::EndMedevacHandoff(
	FSimCopterMedevacHandoff& Handoff,
	const bool bResolvePatients)
{
	if (ASimCopterGroundAgent* Patient = Handoff.CarriedPatient.Get())
	{
		// A failed animation/path must never erase the real patient. This handoff only exists at
		// the hospital, so resolve the action directly before removing the carried actor.
		if (bResolvePatients)
		{
			NotifyMissionPersonDelivered(Patient);
		}
		Patient->Destroy();
	}
	if (ASimCopterGroundAgent* Emt = Handoff.Emt.Get())
	{
		if (Handoff.bOwnsEmt)
		{
			Emt->Destroy();
		}
		else
		{
			Emt->ClearMoveTarget();
			Emt->SetBehaviorGroundSnap(true);
			Emt->ResumeSuspendedPedestrianBehavior();
		}
	}
	if (bResolvePatients)
	{
		if (ASimCopterHelicopterPawn* Helicopter = Handoff.Helicopter.Get())
		{
			if (Helicopter->CanTransferMissionPassengers() &&
				Helicopter->GetMissionPassengerCount(
					Handoff.EventId,
					ESimCopterMissionPassengerKind::Medevac) > 0)
			{
				DeliverMedevacDirectly(Handoff.EventId, Helicopter);
			}
		}
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
	if (Onboard <= 0)
	{
		return;
	}
	ReleaseMissionPassengersFromHelicopter(
		Helicopter,
		EventId,
		ESimCopterMissionPassengerKind::Medevac,
		Onboard,
		Helicopter->GetPassengerDropWorldLocation(),
		/*bRemoveAfterDelivery*/ true);
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

namespace
{
// Chebyshev reach of a square spiral of N rings: the same tiles FUN_004beda0(N) visits.
bool IsWithinSpiralRings(const FIntPoint& A, const FIntPoint& B, int32 Rings)
{
	return FMath::Max(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y)) <= Rings;
}
}

// SCHOOK: FireTruckAcquireTarget 0x004b9890 0x004b9b10
bool ASimCopterMissionSystemActor::TryAcquireServiceFireTarget(
	const FIntPoint& FromTile,
	const int32 RadiusTiles,
	FServiceFireTarget& OutTarget) const
{
	const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem == nullptr)
	{
		return false;
	}

	// FUN_004b9890 walks FUN_004beda0's square spiral outward and stops at the first thing it
	// finds, so nearer fires win by construction rather than by comparing distances.
	bool bFound = false;
	int32 BestCost = TNumericLimits<int32>::Max();
	const SimCopterMissions::FSimCopterFlame* Chosen = nullptr;
	int32 SeenInCell = 0;

	for (const SimCopterMissions::FSimCopterFlame& Flame : MissionSystem.GetFlames())
	{
		if (!Flame.bActive)
		{
			continue;
		}
		const FIntPoint FlameTile(Flame.TileX, Flame.TileY);
		if (!IsWithinSpiralRings(FromTile, FlameTile, RadiusTiles))
		{
			continue;
		}
		const int32 Cost = SimCopterDispatch::TileCost(FromTile, FlameTile);
		if (Cost < BestCost)
		{
			BestCost = Cost;
			Chosen = &Flame;
			SeenInCell = 1;
			bFound = true;
		}
		else if (Cost == BestCost)
		{
			// Reservoir sample among the flames the spiral reaches at the same distance, so a
			// multi-flame building gets hosed all over instead of on one corner.
			++SeenInCell;
			if (FMath::RandRange(0, SeenInCell - 1) == 0)
			{
				Chosen = &Flame;
			}
		}
	}

	// A burning vehicle on a tile the spiral reaches is taken outright: FUN_004b9890 returns on
	// the first tile object flagged 0x1000, ahead of any flame it was still sampling.
	TArray<FSimCopterBurningVehicle> BurningVehicles;
	TrafficSystem->GetBurningVehicles(BurningVehicles);
	for (const FSimCopterBurningVehicle& Vehicle : BurningVehicles)
	{
		int32 VehicleTileX = INDEX_NONE;
		int32 VehicleTileY = INDEX_NONE;
		if (!TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(Vehicle.World, VehicleTileX, VehicleTileY))
		{
			continue;
		}
		const FIntPoint VehicleTile(VehicleTileX, VehicleTileY);
		if (!IsWithinSpiralRings(FromTile, VehicleTile, RadiusTiles))
		{
			continue;
		}
		const int32 Cost = SimCopterDispatch::TileCost(FromTile, VehicleTile);
		if (!bFound || Cost <= BestCost)
		{
			OutTarget.World = Vehicle.World;
			OutTarget.Tile = VehicleTile;
			OutTarget.EventId = Vehicle.EventId;
			return true;
		}
	}

	if (!bFound || Chosen == nullptr)
	{
		return false;
	}

	FVector TileCenter = FVector::ZeroVector;
	if (!TrafficSystem->TryGetTileCenterWorldLocation(Chosen->TileX, Chosen->TileY, TileCenter))
	{
		return false;
	}

	// FUN_004b9b10 aims at the cell origin plus the flame's own offset, height included.
	OutTarget.World =
		TileCenter + TrafficSystem->ConvertOriginalOffsetToWorld(Chosen->PosX, Chosen->PosY, Chosen->PosZ);
	OutTarget.Tile = FIntPoint(Chosen->TileX, Chosen->TileY);
	OutTarget.EventId = Chosen->EventId;
	return true;
}

bool ASimCopterMissionSystemActor::TryFindNearestJamTile(
	const FIntPoint& FromTile,
	int32 RadiusTiles,
	FIntPoint& OutTile,
	int32& OutEventId) const
{
	OutEventId = INDEX_NONE;
	int32 BestCost = TNumericLimits<int32>::Max();
	for (const SimCopterMissions::FSimCopterMissionRecord& Record : MissionSystem.GetRecords())
	{
		if (!Record.bActive || (Record.TypeMask & SimCopterMissions::TYPE_TrafficJam) == 0)
		{
			continue;
		}
		const FIntPoint JamTile(Record.TileX, Record.TileY);
		if (!IsValidMissionTile(Record.TileX, Record.TileY) || !IsWithinSpiralRings(FromTile, JamTile, RadiusTiles))
		{
			continue;
		}
		const int32 Cost = SimCopterDispatch::TileCost(FromTile, JamTile);
		if (Cost < BestCost)
		{
			BestCost = Cost;
			OutTile = JamTile;
			OutEventId = Record.EventId;
		}
	}
	return BestCost != TNumericLimits<int32>::Max();
}

bool ASimCopterMissionSystemActor::TryFindNearestMedicalTile(
	const FIntPoint& FromTile,
	int32 RadiusTiles,
	FIntPoint& OutTile,
	int32& OutEventId) const
{
	constexpr int32 MedicalMask =
		SimCopterMissions::TYPE_Medevac | SimCopterMissions::TYPE_RescuePeople | SimCopterMissions::TYPE_FireRescue;

	OutEventId = INDEX_NONE;
	int32 BestCost = TNumericLimits<int32>::Max();
	for (const SimCopterMissions::FSimCopterMissionRecord& Record : MissionSystem.GetRecords())
	{
		if (!Record.bActive || (Record.TypeMask & MedicalMask) == 0)
		{
			continue;
		}
		const FIntPoint Tile(Record.TileX, Record.TileY);
		if (!IsValidMissionTile(Record.TileX, Record.TileY) || !IsWithinSpiralRings(FromTile, Tile, RadiusTiles))
		{
			continue;
		}
		const int32 Cost = SimCopterDispatch::TileCost(FromTile, Tile);
		if (Cost < BestCost)
		{
			BestCost = Cost;
			OutTile = Tile;
			OutEventId = Record.EventId;
		}
	}
	return BestCost != TNumericLimits<int32>::Max();
}

void ASimCopterMissionSystemActor::GetActiveFlameTiles(TArray<TPair<FIntPoint, int32>>& OutTiles) const
{
	OutTiles.Reset();
	for (const SimCopterMissions::FSimCopterFlame& Flame : MissionSystem.GetFlames())
	{
		if (!Flame.bActive)
		{
			continue;
		}
		const FIntPoint Tile(Flame.TileX, Flame.TileY);
		if (TPair<FIntPoint, int32>* Existing = OutTiles.FindByPredicate(
				[&Tile](const TPair<FIntPoint, int32>& Entry) { return Entry.Key == Tile; }))
		{
			Existing->Value++;
		}
		else
		{
			OutTiles.Emplace(Tile, 1);
		}
	}
}

// SCHOOK: FireTruckSpray 0x004a5ca0
void ASimCopterMissionSystemActor::SpawnServiceWaterJet(const FVector& TruckWorld, const FVector& TargetWorld)
{
	if (FireSmokeComponent == nullptr)
	{
		return;
	}

	const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	const float CmPerUnit = TrafficSystem != nullptr ? TrafficSystem->GetPeopleWorldCmPerOriginalUnit() : 6.25f;
	if (CmPerUnit <= 0.0f)
	{
		return;
	}

	// FUN_004b9b10 leaves a unit vector aiming at the flame and the range alongside it.
	const FVector Delta = TargetWorld - TruckWorld;
	const float DistanceUnits = Delta.Size() / CmPerUnit;
	const FVector UnitAim = Delta.GetSafeNormal();
	if (UnitAim.IsNearlyZero())
	{
		return;
	}

	// FUN_004a5ca0 substitutes the swept elevation for that aim's vertical component. The
	// horizontal pair it keeps belongs to a unit vector, so an elevation of up to 4.0 throws the
	// shot as steep as ~76 degrees: the sweep is what makes the stream arc over the fire.
	const SimCopterWaterGameplay::FFireTruckJetLaunch Launch =
		SimCopterWaterGameplay::AdvanceFireTruckJet(
			ServiceJetSweep,
			SimCopterWaterGameplay::FireTruckBuildingElevationMax1616,
			SimCopterWaterGameplay::DirectionToFixed(UnitAim),
			FMath::RoundToInt(DistanceUnits * 65536.0f),
			FMath::RandRange(0, 99));

	const FVector Direction = SimCopterWaterGameplay::DirectionToFloat(Launch.Direction1616);
	if (Direction.IsNearlyZero())
	{
		return;
	}
	const float SpeedUnitsPerSecond = static_cast<float>(Launch.Speed1616) / 65536.0f;

	// The nozzle sits 30 units above the truck (FUN_004a5ca0 lifts only the spawn point; the
	// aim is measured from the truck itself).
	const FVector NozzleWorld =
		TruckWorld +
		FVector::UpVector *
			(static_cast<float>(SimCopterWaterGameplay::FireTruckNozzleLift1616) / 65536.0f * CmPerUnit);

	const bool bSpawned = FireSmokeComponent->SpawnEffect(
		ESimCopterEffectType::BucketDrip,
		NozzleWorld,
		Direction * SpeedUnitsPerSecond * CmPerUnit);

	// The trajectory pool is shared with the helicopter's water, so a busy scene can refuse
	// the droplet. Report the first refusal rather than letting the jet silently not exist.
	if (!bSpawned && !bLoggedServiceJetSpawnFailure)
	{
		bLoggedServiceJetSpawnFailure = true;
		UE_LOG(LogTemp, Warning, TEXT("[Mission] Fire-truck water jet could not spawn a droplet (effect pool full or uninitialised)."));
	}
}

bool ASimCopterMissionSystemActor::ClearTrafficJamEvent(int32 EventId)
{
	return MissionSystem.ClearTrafficJam(EventId);
}

void ASimCopterMissionSystemActor::ReportSpeederCarCaught(int32 EventId)
{
	// FUN_004b8c90: {0x25, eventId, ., ., 1}. This is the one that closes the mission properly -
	// CriminalsCaught reaches TargetCount and the shared crime completion test passes.
	MissionSystem.PostEvent(SimCopterMissions::EVT_CriminalCaught, EventId, 1);
}

void ASimCopterMissionSystemActor::ReportSpeederCarUnresolved(int32 EventId)
{
	// FUN_004b8b60 only posts this when FUN_0049bd00 returned 0, i.e. nowhere to put the driver.
	// CAT_ExpireSilently makes the update loop skip the completion test, so the record just runs
	// out - no fanfare and no payout, which is the point.
	MissionSystem.PostEvent(
		SimCopterMissions::EVT_SetCategory,
		EventId,
		SimCopterMissions::CAT_ExpireSilently);
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
			PlayOriginalClip(Voice);
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

USoundWaveProcedural* ASimCopterMissionSystemActor::LoadOriginalVoice(const FString& SoundDir, const FString& BaseName) const
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

	// A USoundWaveProcedural, not a bare USoundWave. A runtime USoundWave whose only audio is
	// RawPCMData is not playable by the UE5 mixer - RawPCMData is a legacy/editor field, and the
	// mixer wants compressed inline data or streamed chunks. Both ways such a wave can end up
	// configured crash on its first playback:
	//
	//   * left streaming (the default), it enters the audio streaming cache with zero chunks and
	//     a worker thread eventually calls USoundWave::CacheInheritedLoadingBehavior(), which
	//     asserts IsInGameThread() - a runtime object never ran PostLoad to resolve it;
	//   * forced inline, it takes the non-streaming decode path, which wants BINKA data that was
	//     never built, and the Bink decoder asserts on the empty buffer
	//     (InSrcBufferDataSize >= sizeof(BinkAudioFileHeader)).
	//
	// USoundWaveProcedural avoids both: it sets bProcedural (PostLoad early-outs before the
	// loading-behaviour cache, and IsStreaming() is false) and overrides HasCompressedData /
	// GetCompressedData / InitAudioResource / GeneratePCMData to serve a queued PCM FIFO.
	USoundWaveProcedural* Sound = NewObject<USoundWaveProcedural>(const_cast<ASimCopterMissionSystemActor*>(this));
	if (Sound == nullptr)
	{
		return nullptr;
	}

	const int32 BytesPerFrame = 2 * Channels;
	const int32 NumFrames = Pcm16.Num() / FMath::Max(1, BytesPerFrame);

	FOriginalClipAudio Clip;
	Clip.SampleRate = SampleRate;
	Clip.Channels = Channels;
	Clip.Duration = static_cast<float>(NumFrames) / static_cast<float>(SampleRate);
	Clip.Pcm16 = MoveTemp(Pcm16);

	Sound->SetSampleRate(Clip.SampleRate);
	Sound->NumChannels = Clip.Channels;
	Sound->Duration = Clip.Duration;
	Sound->SoundGroup = SOUNDGROUP_Default;
	Sound->bLooping = false;
	// The FIFO reads in whole samples; ours are 16-bit.
	Sound->SampleByteSize = 2;

	// This object is only ever a handle: PlayOriginalClip builds a fresh wave per play from
	// the stored samples, so this one's FIFO is deliberately left empty.
	const_cast<ASimCopterMissionSystemActor*>(this)->VoicePcmByWave.Add(
		TObjectKey<USoundWaveProcedural>(Sound),
		MoveTemp(Clip));

	return Sound;
}

USoundWaveProcedural* ASimCopterMissionSystemActor::MakeOneShotVoice(const FOriginalClipAudio& Clip)
{
	if (Clip.Pcm16.Num() == 0 || Clip.SampleRate <= 0 || Clip.Channels <= 0)
	{
		return nullptr;
	}

	USoundWaveProcedural* OneShot = NewObject<USoundWaveProcedural>(this);
	if (OneShot == nullptr)
	{
		return nullptr;
	}

	OneShot->SetSampleRate(Clip.SampleRate);
	OneShot->NumChannels = Clip.Channels;
	OneShot->Duration = Clip.Duration;
	OneShot->SoundGroup = SOUNDGROUP_Default;
	OneShot->bLooping = false;
	OneShot->SampleByteSize = 2;
	OneShot->QueueAudio(Clip.Pcm16.GetData(), Clip.Pcm16.Num());
	return OneShot;
}

void ASimCopterMissionSystemActor::PlayOriginalClip(USoundBase* Sound, float VolumeMultiplier)
{
	if (Sound == nullptr)
	{
		return;
	}

	// Runtime clips get a private wave per play. A procedural wave's FIFO is drained by the
	// audio render thread, so re-queueing one shared wave races that reader whenever two plays
	// overlap - and mission completions routinely land within a second of each other. The
	// throwaway wave is queued once, never reset, and is kept alive by the audio component
	// PlaySound2D creates for it.
	if (USoundWaveProcedural* Handle = Cast<USoundWaveProcedural>(Sound))
	{
		if (const FOriginalClipAudio* Clip = VoicePcmByWave.Find(TObjectKey<USoundWaveProcedural>(Handle)))
		{
			if (USoundWaveProcedural* OneShot = MakeOneShotVoice(*Clip))
			{
				UGameplayStatics::PlaySound2D(this, OneShot, VolumeMultiplier);
			}
			return;
		}
	}

	// Anything assigned in the editor is a normal cooked asset and plays directly.
	UGameplayStatics::PlaySound2D(this, Sound, VolumeMultiplier);
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

	// Speeder tags track the live car, so this needs the traffic system rather than just tiles.
	ASimCopterTrafficSystemActor* TrafficSystem = const_cast<ASimCopterMissionSystemActor*>(this)->ResolveTrafficSystem();

	// The hangar's tag is permanent: it is the one marker that is not a job, and it is there so
	// the player can always find their way back to base. "Base Location" is the mission log's own
	// name for it (string 586).
	if (const UWorld* World = GetWorld())
	{
		for (TActorIterator<ASimCopterHangar> It(const_cast<UWorld*>(World)); It; ++It)
		{
			FSimCopterMissionWorldMarkerEntry HangarMarker;
			HangarMarker.WorldLocation = It->GetTagWorldLocation();
			HangarMarker.Label = ASimCopterHangar::GetTagLabel();
			HangarMarker.Detail = ASimCopterHangar::GetTagDetail();
			HangarMarker.Color = FLinearColor(0.16f, 0.52f, 0.72f, 1.0f);
			OutMarkers.Add(HangarMarker);
			break;
		}
	}

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
			int32 TransportOnboard = 0;
			if (const UWorld* World = GetWorld())
			{
				for (TActorIterator<ASimCopterHelicopterPawn> It(const_cast<UWorld*>(World)); It; ++It)
				{
					TransportOnboard += It->GetMissionPassengerCount(
						Record.EventId,
						ESimCopterMissionPassengerKind::Transport);
				}
			}
			const int32 TransportWaiting = FMath::Max(
				0,
				Record.TransportPassengers -
				Record.TransportDelivered -
				Record.PassengersLost -
				TransportOnboard);
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

		if ((Record.TypeMask & SimCopterMissions::TYPE_CriminalCar) != 0)
		{
			// The speeder drives away from its spawn tile immediately, so its tag has to follow
			// the car rather than sit on the tile the mission was created at. The label is the
			// player's instruction: light it, then send police, then it is done.
			FVector CarWorld = FVector::ZeroVector;
			int32 SpotlightMark = 0;
			bool bStopped = false;
			if (TrafficSystem != nullptr &&
				TrafficSystem->TryGetSpeederCarState(Record.EventId, CarWorld, SpotlightMark, bStopped))
			{
				// A stopped car's tag is emitted by the linger pass below instead, so that it
				// survives the record being retired the instant the mission pays out. Emitting
				// it here as well would double it up for the frame in between.
				if (!bStopped)
				{
					// The marker box is a fixed width, so the label stays as short as every other
					// one here ("FIRE", "JAM", "RIOT"). Progress is carried by colour instead:
					// red = not lit yet, amber = marked and police can now stop it.
					FSimCopterMissionWorldMarkerEntry Marker;
					Marker.WorldLocation = CarWorld + FVector(0.0f, 0.0f, MissionMarkerWorldZOffsetCm);
					Marker.Label = TEXT("SPEEDER");
					Marker.Detail = Record.Name;
					Marker.Color = SpotlightMark > 0
						? FLinearColor(1.0f, 0.78f, 0.12f, 1.0f)
						: FLinearColor(0.86f, 0.18f, 0.18f, 1.0f);
					OutMarkers.Add(Marker);
				}
			}
			else
			{
				// The car has not been placed yet, or has already been taken away.
				AddTileMarker(Record.TileX, Record.TileY, TEXT("SPEEDER"), Record.Name, FLinearColor(0.86f, 0.18f, 0.18f, 1.0f));
			}
			continue;
		}

		if ((Record.TypeMask & (SimCopterMissions::TYPE_CriminalA | SimCopterMissions::TYPE_CriminalC |
			SimCopterMissions::TYPE_SpeederEvent)) != 0)
		{
			AddTileMarker(Record.TileX, Record.TileY, TEXT("TARGET"), Record.Name, FLinearColor(0.86f, 0.18f, 0.18f, 1.0f));
			continue;
		}

		AddTileMarker(Record.TileX, Record.TileY, TEXT("MISSION"), Record.Name, FLinearColor(0.15f, 0.55f, 1.0f, 1.0f));
	}

	// A speeder pays out the moment it stops, which retires its record in the same frame and
	// would take the tag with it. Hold the green one up for a few seconds afterwards so the stop
	// reads as a result rather than the marker just vanishing. Driven off the car, not a record,
	// precisely because the record is already gone by then.
	if (TrafficSystem != nullptr)
	{
		TArray<FVector> StoppedSpeeders;
		TrafficSystem->GetRecentlyStoppedSpeederLocations(StoppedSpeeders);
		for (const FVector& CarWorld : StoppedSpeeders)
		{
			FSimCopterMissionWorldMarkerEntry Marker;
			Marker.WorldLocation = CarWorld + FVector(0.0f, 0.0f, MissionMarkerWorldZOffsetCm);
			Marker.Label = TEXT("SPEEDER");
			Marker.Detail = TEXT("Stopped");
			Marker.Color = FLinearColor(0.05f, 0.72f, 0.32f, 1.0f);
			OutMarkers.Add(Marker);
		}
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
