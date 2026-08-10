// Copyright Epic Games, Inc. All Rights Reserved.

#include "Missions/SimCopterMissionSystemActor.h"
#include "Audio/SimCopterAudioSubsystem.h"
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
#include "Formats/SimCopterOriginalGamePaths.h"
#include "Game/SimCopterCareerSubsystem.h"
#include "Game/SimCopterSessionSubsystem.h"
#include "UI/SimCopterHangarShop.h"
#include "UI/SimCopterMissionMarkerLayout.h"
#include "Audio.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Brushes/SlateImageBrush.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Sound/SoundWave.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"
#include "CollisionQueryParams.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
constexpr uint32 MissionRuntimeSaveMagic = 0x4d534352; // 'MSCR'
constexpr int32 MissionRuntimeSaveVersion = 1;
FString FormatSignedAmount(int32 Value, const TCHAR* Unit)
{
	return FString::Printf(TEXT("%+d %s"), Value, Unit);
}

FString ResolveCareerTweakPath()
{
	const FString Resolved = SimCopterOriginalGame::ResolveFile(TEXT("tweak/career.twk"));
	if (!Resolved.IsEmpty())
	{
		return Resolved;
	}

	// Nothing found: still hand back the path the player is told to create, so the failure that
	// follows names somewhere they can act on.
	return FPaths::Combine(SimCopterOriginalGame::GetPlayerRootDir(), TEXT("tweak/career.twk"));
}

bool IsValidMissionTile(int32 TileX, int32 TileY)
{
	return TileX >= 0 && TileX < 128 && TileY >= 0 && TileY < 128;
}

// FUN_004a5c10's horizontal gate: `(1 - DAT_004f9740) * 0x80000 + DAT_00505f54`, where
// DAT_00505f54 ships as 0x180000 = 24.0 units. DAT_004f9740 is a global whose shipped value is
// 2 and whose meaning is not pinned, so the port takes the base 24.0 and leaves the +/-8.0 term
// out. It is a tight box on purpose - the damage inside it kills a healthy airframe in seconds,
// so it must only fire when the helicopter is genuinely down in the flames.
constexpr float FireProximityRadiusCm = 24.0f * SimCopterEffectFX::OriginalUnitToCm;

// The original measures the delta from the flame's *top*, subtracting a height term as well as
// the flame's own Y. FUN_004a47c0 draws every flame at a 0x100000 scale, so the port uses that
// as the height; the damage band is 109 units wide, which swamps any small error here.
constexpr float FlameHeightCm = 16.0f * SimCopterEffectFX::OriginalUnitToCm;

FString FormatMissionMarkerDistance(float DistanceCm)
{
	const float DistanceMeters = FMath::Max(0.0f, DistanceCm) * 0.01f;
	if (DistanceMeters < 1000.0f)
	{
		return FString::Printf(TEXT("%d M"), FMath::RoundToInt(DistanceMeters));
	}

	const float DistanceKilometers = DistanceMeters * 0.001f;
	return DistanceKilometers < 10.0f
		? FString::Printf(TEXT("%.1f KM"), DistanceKilometers)
		: FString::Printf(TEXT("%.0f KM"), DistanceKilometers);
}

FName ResolveMissionMarkerIconName(const FString& Label)
{
	if (Label == TEXT("HANGAR")) return TEXT("warehouse");
	if (Label == TEXT("TRANSPORT")) return TEXT("local_shipping");
	if (Label == TEXT("DROPOFF")) return TEXT("flag");
	if (Label == TEXT("PATIENT")) return TEXT("personal_injury");
	if (Label == TEXT("HOSPITAL")) return TEXT("local_hospital");
	if (Label == TEXT("RESCUE")) return TEXT("hail");
	if (Label == TEXT("FIRE")) return TEXT("local_fire_department");
	if (Label == TEXT("JAM")) return TEXT("traffic");
	if (Label == TEXT("RIOT")) return TEXT("campaign");
	if (Label == TEXT("BURGLAR")) return TEXT("directions_car");
	if (Label == TEXT("ROBBER") || Label == TEXT("ARSONIST") || Label == TEXT("MUGGER")) return TEXT("my_location");
	if (Label == TEXT("ROOFTOP")) return TEXT("hail");
	return TEXT("location_on");
}

const FSlateBrush* GetMissionMarkerIconBrush(const FString& Label)
{
	static TMap<FName, TSharedPtr<FSlateVectorImageBrush>> IconBrushes;

	const FName IconName = ResolveMissionMarkerIconName(Label);
	TSharedPtr<FSlateVectorImageBrush>& Brush = IconBrushes.FindOrAdd(IconName);
	if (!Brush.IsValid())
	{
		const FString IconPath = FPaths::Combine(
			FPaths::ProjectContentDir(),
			TEXT("Slate/MissionIcons"),
			IconName.ToString() + TEXT(".svg"));
		Brush = MakeShared<FSlateVectorImageBrush>(
			IconPath,
			FVector2D(39.0f, 39.0f),
			FLinearColor::White);
	}
	return Brush.Get();
}

// A loose .png on disk has to go through FSlateDynamicImageBrush. FSlateImageBrush is a *static*
// brush: FSlateRHIResourceManager only ever resolves those against ResourceMap, which holds the
// textures registered by a style set, so a raw file path finds nothing and the brush paints the
// default white quad instead - the dark box that showed up behind the marker icons rather than a
// shadow. Only vector brushes (the .svg icons) load straight off disk while static, because they
// are rasterised through the vector graphics cache. Returns null when the file is missing; SImage
// treats a null brush as "draw nothing", which is the right failure for a decoration.
TSharedPtr<FSlateDynamicImageBrush> MakeMissionMarkerFileBrush(const FString& FilePath, const FVector2D& ImageSize)
{
	if (!FPaths::FileExists(FilePath))
	{
		return nullptr;
	}
	return MakeShared<FSlateDynamicImageBrush>(FName(*FilePath), ImageSize, FLinearColor::White);
}

const FSlateBrush* GetMissionMarkerIconShadowBrush(const FString& Label)
{
	static TMap<FName, TSharedPtr<FSlateDynamicImageBrush>> ShadowBrushes;

	const FName IconName = ResolveMissionMarkerIconName(Label);
	if (const TSharedPtr<FSlateDynamicImageBrush>* Cached = ShadowBrushes.Find(IconName))
	{
		return Cached->Get();
	}

	const FString ShadowPath = FPaths::Combine(
		FPaths::ProjectContentDir(),
		TEXT("Slate/MissionIcons"),
		IconName.ToString() + TEXT("_shadow.png"));
	const TSharedPtr<FSlateDynamicImageBrush> Brush = MakeMissionMarkerFileBrush(ShadowPath, FVector2D(43.0f, 43.0f));
	ShadowBrushes.Add(IconName, Brush);
	return Brush.Get();
}

const FSlateBrush* GetMissionMarkerAccentGlowBrush()
{
	static bool bResolved = false;
	static TSharedPtr<FSlateDynamicImageBrush> Brush;
	if (!bResolved)
	{
		bResolved = true;
		const FString GlowPath = FPaths::Combine(
			FPaths::ProjectContentDir(),
			TEXT("Slate/MissionIcons/label_accent_glow.png"));
		Brush = MakeMissionMarkerFileBrush(GlowPath, FVector2D(128.0f, 2.0f));
	}
	return Brush.Get();
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

	// Planes, boats and the train are ambient traffic, not mission props: the original ticks all
	// three pools from FUN_0047a760 whether or not a mission wants them.
	ResolveAmbientVehicles();
	EnsureMessageLogWidget();
	EnsureMissionMarkerWidget();

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

void ASimCopterMissionSystemActor::StopMarchingBandAudio()
{
	if (MarchingBandVoiceSlot != INDEX_NONE)
	{
		if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
		{
			Audio->ReleaseVoiceSlot(MarchingBandVoiceSlot);
		}
		MarchingBandVoiceSlot = INDEX_NONE;
	}
}

void ASimCopterMissionSystemActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopMarchingBandAudio();
	for (FSimCopterMedevacHandoff& Handoff : MedevacHandoffs)
	{
		EndMedevacHandoff(Handoff, /*bResolvePatients*/ false);
	}
	MedevacHandoffs.Reset();
	MedevacHospitalTiles.Reset();

	RemoveMissionMarkerWidget();
	RemoveMessageLogWidget();
	Super::EndPlay(EndPlayReason);
}

bool ASimCopterMissionSystemActor::CaptureRuntimeSaveState(TArray<uint8>& OutData)
{
	OutData.Reset();
	FMemoryWriter Writer(OutData, true);
	uint32 Magic = MissionRuntimeSaveMagic;
	int32 Version = MissionRuntimeSaveVersion;
	Writer << Magic << Version;
	if (!MissionSystem.SerializeRuntimeState(Writer))
	{
		OutData.Reset();
		return false;
	}

	uint8 Mode = static_cast<uint8>(SessionMode);
	uint8 SelectionHeld = bSessionSelectionHeld ? 1 : 0;
	Writer << Mode << SelectionHeld << SessionElapsedSeconds;
	Writer << ServiceJetSweep.Elevation1616 << ServiceJetSweep.Step1616;

	int32 HospitalCount = MedevacHospitalTiles.Num();
	Writer << HospitalCount;
	for (const TPair<int32, FIntPoint>& Pair : MedevacHospitalTiles)
	{
		int32 EventId = Pair.Key;
		FIntPoint Tile = Pair.Value;
		Writer << EventId << Tile;
	}

	int32 LogCount = MissionMessageLog.Num();
	Writer << LogCount;
	for (FSimCopterMissionLogEntry& Entry : MissionMessageLog)
	{
		Writer << Entry.Text << Entry.Color << Entry.RemainingSeconds;
	}

	TArray<uint8> FireEffectState;
	if (FireSmokeComponent == nullptr ||
		!FireSmokeComponent->CaptureRuntimeSaveState(FireEffectState))
	{
		OutData.Reset();
		return false;
	}
	Writer << FireEffectState;

	int32 HandoffCount = MedevacHandoffs.Num();
	Writer << HandoffCount;
	for (FSimCopterMedevacHandoff& Handoff : MedevacHandoffs)
	{
		FName HelicopterName = Handoff.Helicopter.IsValid()
			? Handoff.Helicopter->GetFName()
			: NAME_None;
		FName EmtName = Handoff.Emt.IsValid()
			? Handoff.Emt->GetRuntimeSaveIdentityName()
			: NAME_None;
		Writer << Handoff.EventId << HelicopterName << EmtName;
		Writer << Handoff.LastOnboardCount << Handoff.SecondsWithoutProgress;
	}
	if (Writer.IsError())
	{
		OutData.Reset();
		return false;
	}
	return true;
}

bool ASimCopterMissionSystemActor::RestoreRuntimeSaveState(const TArray<uint8>& Data)
{
	if (Data.IsEmpty())
	{
		return false;
	}
	// Saves written before UserCityJobs existed serialized user sessions as CityJobs. The outer
	// session selection is authoritative and has already opened the user sandbox before this live
	// world payload is applied, so remember it across the legacy mode byte below.
	const bool bRestoreAsUserCitySandbox =
		SessionMode == ESimCopterMissionSessionMode::UserCityJobs;

	FMemoryReader Reader(Data, true);
	uint32 Magic = 0;
	int32 Version = 0;
	Reader << Magic << Version;
	if (Magic != MissionRuntimeSaveMagic || Version != MissionRuntimeSaveVersion ||
		!MissionSystem.SerializeRuntimeState(Reader))
	{
		return false;
	}

	uint8 Mode = 0;
	uint8 SelectionHeld = 0;
	Reader << Mode << SelectionHeld << SessionElapsedSeconds;
	Reader << ServiceJetSweep.Elevation1616 << ServiceJetSweep.Step1616;
	if (Mode > static_cast<uint8>(ESimCopterMissionSessionMode::UserCityJobs))
	{
		return false;
	}
	SessionMode = static_cast<ESimCopterMissionSessionMode>(Mode);
	if (bRestoreAsUserCitySandbox)
	{
		SessionMode = ESimCopterMissionSessionMode::UserCityJobs;
	}
	bSessionSelectionHeld = SelectionHeld != 0;

	int32 HospitalCount = 0;
	Reader << HospitalCount;
	if (HospitalCount < 0 || HospitalCount > SimCopterMissions::FSimCopterMissionSystem::MaxRecords)
	{
		return false;
	}
	MedevacHospitalTiles.Reset();
	for (int32 Index = 0; Index < HospitalCount; ++Index)
	{
		int32 EventId = INDEX_NONE;
		FIntPoint Tile = FIntPoint::ZeroValue;
		Reader << EventId << Tile;
		MedevacHospitalTiles.Add(EventId, Tile);
	}

	int32 LogCount = 0;
	Reader << LogCount;
	if (LogCount < 0 || LogCount > 64)
	{
		return false;
	}
	MissionMessageLog.SetNum(LogCount);
	for (FSimCopterMissionLogEntry& Entry : MissionMessageLog)
	{
		Reader << Entry.Text << Entry.Color << Entry.RemainingSeconds;
	}

	TArray<uint8> FireEffectState;
	Reader << FireEffectState;
	if (FireEffectState.IsEmpty())
	{
		return false;
	}

	int32 HandoffCount = 0;
	Reader << HandoffCount;
	if (HandoffCount < 0 || HandoffCount > SimCopterMissions::FSimCopterMissionSystem::MaxRecords)
	{
		return false;
	}
	MedevacHandoffs.Reset(HandoffCount);
	for (int32 Index = 0; Index < HandoffCount; ++Index)
	{
		FName HelicopterName;
		FName EmtName;
		FSimCopterMedevacHandoff& Handoff = MedevacHandoffs.AddDefaulted_GetRef();
		Reader << Handoff.EventId << HelicopterName << EmtName;
		Reader << Handoff.LastOnboardCount << Handoff.SecondsWithoutProgress;

		for (TActorIterator<ASimCopterHelicopterPawn> It(GetWorld()); It; ++It)
		{
			if (It->GetFName() == HelicopterName)
			{
				Handoff.Helicopter = *It;
				break;
			}
		}
		for (TActorIterator<ASimCopterGroundAgent> It(GetWorld()); It; ++It)
		{
			if (It->GetRuntimeSaveIdentityName() == EmtName)
			{
				Handoff.Emt = *It;
				break;
			}
		}
		if ((!HelicopterName.IsNone() && !Handoff.Helicopter.IsValid()) ||
			(!EmtName.IsNone() && !Handoff.Emt.IsValid()))
		{
			return false;
		}
	}
	if (Reader.IsError() || Reader.Tell() != Reader.TotalSize())
	{
		return false;
	}
	if (FireSmokeComponent == nullptr ||
		!FireSmokeComponent->RestoreRuntimeSaveState(FireEffectState))
	{
		return false;
	}

	SessionElapsedSeconds = FMath::Max(0.0f, SessionElapsedSeconds);
	StopMarchingBandAudio();
	bMarchingBandSpawned = false;
	bMarchingBandApproaching = false;
	MarchingBandTargetUpdateTimer = 2.0f;
	LastMarchingBandPlayerLocation = FVector::ZeroVector;
	MarchingBandAgents.Reset();
	ActiveFireworkRockets.Reset();
	FireworksTimer = 0.0f;
	bLevelCompletePromptDisplayed = false;
	PromptRefreshTimer = 0.0f;
	UpdateFireVisuals(0.0f);
	RefreshMessageLogWidget();
	return true;
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
	ProcessPassengerTransfers(DeltaTime);
	ProcessRescueTransfers();
	ProcessMedevacHospitalHandoffs(DeltaTime);
	UpdateBurningDebris(DeltaTime);
	UpdateFireVisuals(DeltaTime);
	UpdateFireAudio();
	UpdateEmergencySirenAudio();

	// Only a scheduled-jobs session walks the career city list; the original's user-city mode
	// (DAT_00518d50 = 1) has no city to advance to.
	if (SessionMode == ESimCopterMissionSessionMode::CityJobs)
	{
		MissionSystem.CheckLevelCompletion();
		ProcessLevelCompleteLanding(DeltaTime);
	}

	bool bChangedLog = false;
	for (int32 Index = MissionMessageLog.Num() - 1; Index >= 0; --Index)
	{
		if (MissionMessageLog[Index].bDestroyOnTimeout)
		{
			MissionMessageLog[Index].RemainingSeconds -= DeltaTime;
			if (MissionMessageLog[Index].RemainingSeconds <= 0.0f)
			{
				MissionMessageLog.RemoveAt(Index);
				bChangedLog = true;
			}
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

bool ASimCopterMissionSystemActor::IsPlayerOnFoot() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PlayerController =
		World != nullptr ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	return PlayerController != nullptr && Cast<ASimCopterOnFootPawn>(PlayerController->GetPawn()) != nullptr;
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

	// SCHOOK: BuildingBurnedDownSound 0x004a5fd0 - Play3D(4 BLDEXPL) at the building, not 2D.
	if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
	{
		Audio->Play3D(SimCopterSound::SND_BLDEXPL, BurstOrigin);
	}

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

	if (Mode == ESimCopterMissionSessionMode::UserCityJobs)
	{
		// FUN_004080c0 seeds mode 1's editable DAT_00518cd0 record separately from the career
		// table. Starting directly from Career City0 made a new user city's panel - and therefore
		// its actual scheduler mix - disagree with the original game.
		MissionSystem.SetCareerCity(FSimCopterMissionSystem::MakeUserCityDefaults(
			MissionSystem.GetCareerCity()));
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

void ASimCopterMissionSystemActor::StartUserCitySession(int32 TuningCityIndex, bool bFirstJobImmediately)
{
	// FUN_004080c0 opens original mode 1 with its own editable defaults and the ordinary mission
	// scheduler. The user-requested sandbox rule deliberately omits the original's rolling score
	// threshold: jobs and points remain live, but there is no goal, filled meter, or level finish.
	BeginSession(ESimCopterMissionSessionMode::UserCityJobs, TuningCityIndex, /*bAllowScheduledMissions=*/true);
	UE_LOG(LogTemp, Display, TEXT("SimCopter session: user city sandbox (tier %d, no points goal)"),
		MissionSystem.GetDifficultyTier());

	if (bFirstJobImmediately)
	{
		const bool bCreated = MissionSystem.RollScheduledMissionNow();
		UE_LOG(LogTemp, Display, TEXT("SimCopter session: opening user-city job rolled immediately -> %s"),
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

namespace
{
	// FUN_0048ed00's class-0x10 arm, the only class it treats this way: the moment the slot's speed
	// drops under 0x40001 it grounds and takes a fresh life of 0x3c0000 seconds of burning, puffing
	// smoke every 0x3333 of its sub-timer. Everything else is given a life of 0 and simply stops.
	constexpr float ArsonBurnSeconds = 60.0f;      // 0x3c0000
	constexpr float ArsonPuffIntervalSeconds = 0.2f; // 0x3333
	// The pool the firebomb comes out of (DAT_005d6880), shared with the rioters' thrown rocks.
	constexpr int32 ArsonDebrisSlots = 30;
	// The size the burning pool is drawn at, in original units - the debris card's own 0x30000.
	constexpr float ArsonDebrisSizeUnits = 3.0f;
}

// SCHOOK: ArsonistFirebomb 0x004cbfd0 -> FUN_0048e0b0 type 4 -> FUN_0048ed00 class 0x10
void ASimCopterMissionSystemActor::ThrowArsonistFirebomb(const FVector& ThrowerWorldLocation)
{
	ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem == nullptr)
	{
		return;
	}
	// A fixed pool, and a full one simply refuses the throw (FUN_0048e0b0 returns null after
	// walking all thirty slots) - the arsonist's animation still plays either way.
	if (BurningDebris.Num() >= ArsonDebrisSlots)
	{
		return;
	}

	int32 TileX = INDEX_NONE;
	int32 TileY = INDEX_NONE;
	if (!TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(ThrowerWorldLocation, TileX, TileY))
	{
		return;
	}

	// Retail's walker and building cells are one occupancy model, so the person can throw while
	// still standing in a cell that FUN_004a5f60 accepts. The remake correctly keeps a capsule out
	// of rendered walls, which usually moves that same state-11 person onto a road/sidewalk cell and
	// made every burnout fail before its difficulty roll. The generic mission-person placer searches
	// at most footprint + 2 rings (six at the largest footprint), so the intended building is still
	// within this bounded adaptation.
	int32 IgnitionTileX = INDEX_NONE;
	int32 IgnitionTileY = INDEX_NONE;
	constexpr int32 RenderedBuildingIgnitionSearchRadius = 6;
	if (!MissionSystem.FindNearestFireSuitableTile(
		TileX,
		TileY,
		RenderedBuildingIgnitionSearchRadius,
		IgnitionTileX,
		IgnitionTileY))
	{
		// The projectile still exists and burns out when there is no eligible structure; it simply
		// fails FUN_004a5f60 later, as the retail slot does.
		IgnitionTileX = TileX;
		IgnitionTileY = TileY;
	}

	// The lob is 75 to 95 degrees above horizontal, so the landing point is the thrower's own tile
	// and the flight is short. The remake skips the ballistic slot and grounds it where it lands,
	// which is the only part of the trajectory the 60-second burn and the ignition roll depend on.
	FVector Landing = ThrowerWorldLocation;
	float SurfaceZ = 0.0f;
	if (TraceSurfaceTopZ(ThrowerWorldLocation, SurfaceZ))
	{
		Landing.Z = SurfaceZ;
	}

	FSimCopterBurningDebris& Slot = BurningDebris.AddDefaulted_GetRef();
	Slot.World = Landing;
	Slot.Tile = FIntPoint(IgnitionTileX, IgnitionTileY);
	Slot.BurnSecondsRemaining = ArsonBurnSeconds;
	Slot.PuffSecondsRemaining = 0.0f;

	// FUN_0048e0b0's type-4 arm posts event 7 at spawn with no owning record (the arsonist passes
	// -1), so the debris is reported to the scoring layer even though nothing owns it yet.
	MissionSystem.PostEvent(SimCopterMissions::EVT_DebrisCreated, INDEX_NONE, 1, true);
	UE_LOG(LogTemp, Verbose,
		TEXT("Arsonist firebomb burning at person tile (%d,%d), ignition tile (%d,%d)."),
		TileX, TileY, IgnitionTileX, IgnitionTileY);
}

void ASimCopterMissionSystemActor::UpdateBurningDebris(const float DeltaSeconds)
{
	if (BurningDebris.Num() == 0 || DeltaSeconds <= 0.0f)
	{
		return;
	}

	for (int32 Index = BurningDebris.Num() - 1; Index >= 0; --Index)
	{
		FSimCopterBurningDebris& Slot = BurningDebris[Index];

		Slot.PuffSecondsRemaining -= DeltaSeconds;
		if (Slot.PuffSecondsRemaining <= 0.0f)
		{
			Slot.PuffSecondsRemaining = ArsonPuffIntervalSeconds;
			if (FireSmokeComponent != nullptr)
			{
				FireSmokeComponent->SpawnEffect(
					ESimCopterEffectType::Smoke,
					Slot.World,
					FVector::UpVector * (20.0f * SimCopterEffectFX::OriginalUnitToCm),
					ArsonDebrisSizeUnits * SimCopterEffectFX::OriginalUnitToCm);
			}
		}

		Slot.BurnSecondsRemaining -= DeltaSeconds;
		if (Slot.BurnSecondsRemaining > 0.0f)
		{
			continue;
		}

		// Burned out. The ignition test is FUN_0048ed00's, in its order: the tile must take a fire
		// and have none already burning nearby (CanIgniteCrashSite is FUN_004a5f60 + FUN_004a6860's
		// spiral, the same pair the plane crash uses), and then the roll is one in (8 - difficulty),
		// so an easy city misses far more often than a hard one.
		const int32 Divisor = FMath::Max(1, 8 - MissionSystem.GetDifficultyTier());
		if (MissionSystem.CanIgniteCrashSite(Slot.Tile.X, Slot.Tile.Y) &&
			(MissionSystem.GetRand().Rand() % Divisor) == 0)
		{
			CreateMissionAt(Slot.Tile.X, Slot.Tile.Y, SimCopterMissions::TYPE_BuildingFire);
		}

		// Event 8 either way - the debris is gone whether or not it took the building with it.
		MissionSystem.PostEvent(SimCopterMissions::EVT_DebrisExpired, INDEX_NONE, 1, true);
		BurningDebris.RemoveAtSwap(Index);
	}
}

FString ASimCopterMissionSystemActor::ResolveOriginalGameRootDir() const
{
	return SimCopterOriginalGame::ResolveRoot();
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

bool ASimCopterMissionSystemActor::TryGetFlameWorldLocation(
	const SimCopterMissions::FSimCopterFlame& Flame,
	FVector& OutWorld) const
{
	const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	FVector TileCenter;
	if (TrafficSystem == nullptr ||
		!TrafficSystem->TryGetTileCenterWorldLocation(Flame.TileX, Flame.TileY, TileCenter))
	{
		return false;
	}

	// FUN_004a5340 stores source-runtime X/Y-up/Z offsets. Apply the same verified
	// Maxis-to-Unreal axes and global city yaw as the building mesh; mapping these
	// directly to Unreal XYZ rotates the planar FIREPTS cloud away from its wall.
	OutWorld = TileCenter + TrafficSystem->ConvertOriginalOffsetToWorld(Flame.PosX, 0, Flame.PosZ);

	float TopZ = TileCenter.Z;
	TraceSurfaceTopZ(OutWorld, TopZ);
	// PosY is the flame's own climb up the wall, one storey per FUN_004a4ac0 growth
	// step, so it rides on top of the seated base rather than being traced away.
	OutWorld.Z = TopZ + TrafficSystem->ConvertOriginalOffsetToWorld(0, Flame.PosY, 0).Z;
	return true;
}

// SCHOOK: FireProximityProbe 0x004a5c10
int32 ASimCopterMissionSystemActor::GetFireHeightDelta1616(const FVector& WorldLocation) const
{
	const float Unit = FMath::Max(SimCopterEffectFX::OriginalUnitToCm, 0.01f);

	int32 Nearest = 0;
	for (const SimCopterMissions::FSimCopterFlame& Flame : MissionSystem.GetFlames())
	{
		if (!Flame.bActive)
		{
			continue;
		}

		FVector FlameWorld;
		if (!TryGetFlameWorldLocation(Flame, FlameWorld))
		{
			continue;
		}

		// The original's horizontal gate is a box, not a circle: |dx| and |dz| each under the
		// radius. It is deliberately tight - the damage inside it is severe - so this only fires
		// when the helicopter is genuinely down among the flames.
		if (FMath::Abs(WorldLocation.X - FlameWorld.X) >= FireProximityRadiusCm ||
			FMath::Abs(WorldLocation.Y - FlameWorld.Y) >= FireProximityRadiusCm)
		{
			continue;
		}

		// heliY - flameY - flameHeight: how far above the *top* of the flame the helicopter is.
		const float TopZ = FlameWorld.Z + FlameHeightCm;
		int32 Delta = FMath::RoundToInt((WorldLocation.Z - TopZ) / Unit * 65536.0f);
		if (Delta == 0)
		{
			// The original returns 1 for an exact zero, because 0 is its "no fire here" answer.
			Delta = 1;
		}

		// Several flames can cover one point; the closest one owns the damage.
		if (Nearest == 0 || FMath::Abs(Delta) < FMath::Abs(Nearest))
		{
			Nearest = Delta;
		}
	}

	return Nearest;
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

		FVector FlameXY;
		if (!TryGetFlameWorldLocation(Flame, FlameXY))
		{
			continue;
		}

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

bool ASimCopterMissionSystemActor::TrySpawnMissionPerson(
	int32 PersonState,
	int32 BehaviorClass,
	int32 TileX,
	int32 TileY,
	int32 EventId)
{
	if (ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		ASimCopterGroundAgent* Person = nullptr;
		if (!TrafficSystem->TrySpawnMissionPerson(
			PersonState, BehaviorClass, TileX, TileY, EventId, FString(), &Person))
		{
			return false;
		}

		// FUN_004c4190 returns the placed person and its first opcode-13 outcome soon publishes
		// the same coordinates. With rendered buildings the collision-aware spawn may be several
		// cells from the requested building; publish the actual first position immediately so the
		// ARSONIST/ROBBER/MUGGER marker cannot point at an apparently empty roof or wall until the
		// BHAV reaches its first loop.
		if (Person != nullptr && PersonState >= 10 && PersonState <= 13)
		{
			int32 ActualTileX = INDEX_NONE;
			int32 ActualTileY = INDEX_NONE;
			if (Person->TryGetTileCoordinate(ActualTileX, ActualTileY))
			{
				SimCopterMissions::FSimCopterMissionEvent Move;
				Move.Code = SimCopterMissions::EVT_SetPrimaryCoords;
				Move.EventId = EventId;
				Move.X = ActualTileX;
				Move.Y = ActualTileY;
				Move.bSilent = true;
				MissionSystem.PostEvent(Move);
				UE_LOG(LogTemp, Display,
					TEXT("Mission criminal spawned: event %d, state %d, class %d, actor %s, tile (%d,%d)."),
					EventId, PersonState, BehaviorClass, *Person->GetName(), ActualTileX, ActualTileY);
			}
		}
		return true;
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

bool ASimCopterMissionSystemActor::CreatePlayerCausedCarFireForVehicle(
	ASimCopterGroundAgent* Vehicle)
{
	if (Vehicle == nullptr || Vehicle->GetAgentKind() != ESimCopterGroundAgentKind::Vehicle)
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
	if (!TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(
		Vehicle->GetActorLocation(), TileX, TileY))
	{
		return false;
	}

	// TYPE_CarFireEvent normally asks FUN_0049fd00 to choose a car. A missile impact already
	// resolved the class-0x10 object, so bind that exact car for the synchronous world callback
	// made by CreateEventAt. Clear the one-shot even when record allocation fails.
	TrafficSystem->ArmNextCarFireTarget(Vehicle);
	const int32 EventId = MissionSystem.CreateEventAt(
		TileX, TileY, SimCopterMissions::TYPE_CarFireEvent);
	TrafficSystem->ClearNextCarFireTarget();
	return EventId != INDEX_NONE && EventId >= 0;
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

// SCHOOK: DispatchVoicePlay 0x0042a3b0
// The dispatcher lines are ordinary table slots - 0x2f..0x6e are D1000..D1018, L001..L009,
// D2001..D2020 and DIS053..DIS068 - so the id the mission system already computes is the id the
// mixer wants, and nothing here has to map anything.
//
// The second argument is NOT a volume. FUN_0042a3b0 sets the buffer to the full master volume
// before playing, whatever that argument is, and passes it instead to the node it appends to the
// per-id completion list at 0x519a18. The port plays at full volume to match and ignores it.
void ASimCopterMissionSystemActor::PlayRadioVoice(int32 VoiceId, int32 /*QueueTag*/)
{
	if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
	{
		Audio->QueueRadioVoice(VoiceId);
	}
}

void ASimCopterMissionSystemActor::PlayUiSound(int32 SoundId)
{
	if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
	{
		Audio->Play2D(SoundId);
	}
}

// SCHOOK: FireLoopSound 0x004a4ac0
void ASimCopterMissionSystemActor::UpdateFireAudio()
{
	USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this);
	if (Audio == nullptr)
	{
		return;
	}

	const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem == nullptr)
	{
		return;
	}

	// Nearest burning flame to the listener. One slot, one voice: the original does exactly this
	// - it walks the flame list every tick, keeps the closest, and drives slot 0x0d with it.
	// The tile centre is close enough for a sound; the roof trace UpdateFireVisuals does is for
	// seating the sprite, and this must keep working when the render component is not ready.
	const FVector Listener = Audio->GetListenerLocation();
	bool bFound = false;
	FVector NearestWorld = FVector::ZeroVector;
	double NearestDistSq = TNumericLimits<double>::Max();

	for (const SimCopterMissions::FSimCopterFlame& Flame : MissionSystem.GetFlames())
	{
		FVector FlameWorld;
		if (!Flame.bActive ||
			!TrafficSystem->TryGetTileCenterWorldLocation(Flame.TileX, Flame.TileY, FlameWorld))
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(FlameWorld, Listener);
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			NearestWorld = FlameWorld;
			bFound = true;
		}
	}

	if (!bFound)
	{
		Audio->Stop(SimCopterSound::SND_FIRELP11);
		return;
	}

	// Play3D culls past 1920 units on its own, so a distant fire simply never starts; keeping
	// the position fresh is what makes an already-running one fade as you fly away.
	if (Audio->IsPlaying(SimCopterSound::SND_FIRELP11))
	{
		Audio->SetPosition(SimCopterSound::SND_FIRELP11, NearestWorld);
	}
	else
	{
		Audio->Play3D(SimCopterSound::SND_FIRELP11, NearestWorld, SimCopterSoundFlags::Loop);
	}
}

// SCHOOK: EmergencySirenMixer 0x004a1d50
void ASimCopterMissionSystemActor::UpdateEmergencySirenAudio()
{
	USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this);
	const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (Audio == nullptr || TrafficSystem == nullptr)
	{
		return;
	}

	const FVector Listener = Audio->GetListenerLocation();
	const float RangeCm =
		USimCopterAudioSubsystem::AudibleRangeUnits * USimCopterAudioSubsystem::OriginalUnitToCm;

	// The original does NOT position these. It plays each one at `listener + (0, 0, distance)`,
	// a synthetic point straight along one axis whose only purpose is to be the right distance
	// away, and then overrides the volume with the same distance anyway. There is no direction
	// in an original siren - only "how close is the nearest one" - so the port plays them 2D and
	// applies the identical volume law, which is what that synthetic point amounted to.
	auto DriveSiren = [Audio, RangeCm](int32 SoundId, bool bHasSource, double DistanceCm)
	{
		if (!bHasSource || DistanceCm >= RangeCm)
		{
			if (Audio->IsPlaying(SoundId))
			{
				Audio->Stop(SoundId);
			}
			return;
		}
		Audio->Play2D(SoundId, SimCopterSoundFlags::Loop);
		const float DistanceUnits =
			static_cast<float>(DistanceCm) / USimCopterAudioSubsystem::OriginalUnitToCm;
		Audio->SetVolumeAdjust(
			SoundId,
			USimCopterAudioSubsystem::DistanceVolumeIndex(DistanceUnits) - 10000);
	};

	// Services 0/1/2 are fire/police/ambulance (FUN_0049b060's argument).
	struct FSirenRoute { int32 Service; int32 SoundId; };
	static const FSirenRoute Routes[] = {
		{ 0, SimCopterSound::SND_FIRESIRE },
		{ 1, SimCopterSound::SND_POLICESI },
		{ 2, SimCopterSound::SND_AMBSRN11 },
	};
	for (const FSirenRoute& Route : Routes)
	{
		const ASimCopterGroundAgent* Agent =
			TrafficSystem->FindNearestServiceVehicleAgent(Listener, Route.Service);
		DriveSiren(
			Route.SoundId,
			Agent != nullptr,
			Agent != nullptr ? FVector::Dist(Agent->GetActorLocation(), Listener) : 0.0);
	}

	// The hose loop follows whichever truck last fired its monitor. A truck sprays on a timer,
	// not every frame, so hold the loop across the gap rather than stuttering it.
	const double Now = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
	constexpr double HoseHoldSeconds = 1.0;
	const bool bHosing = (Now - ServiceJetLastSeconds) < HoseHoldSeconds;
	DriveSiren(
		SimCopterSound::SND_FIRHOSLP,
		bHosing,
		bHosing ? FVector::Dist(ServiceJetWorld, Listener) : 0.0);
}

bool ASimCopterMissionSystemActor::TryActivateBurglarCar(
	int32 EventId,
	int32 TileX,
	int32 TileY,
	int32 CruiseDelay1616)
{
	// FUN_004b84f0 -> FUN_004b8540: the traffic system owns the pool and the road placement.
	if (ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		return TrafficSystem->TryActivateBurglarCar(EventId, TileX, TileY, CruiseDelay1616);
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
		Line = FString::Printf(TEXT("%s: %s %d Points"), *MissionName, SimCopterMissions::GetMissionUpdateText(Message.TextId), Message.ValueA);
		break;
	case 9:
		// 540 "%s: %s %ld Bucks"
		Kind = ESimCopterCareerLogKind::CashAward;
		Line = FString::Printf(TEXT("%s: %s %d Bucks"), *MissionName, SimCopterMissions::GetMissionUpdateText(Message.TextId), Message.ValueA);
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

// SCHOOK: FindActiveRecordOfType 0x004a9230
int32 ASimCopterMissionSystemActor::FindActiveMissionOfType(const int32 TypeMask) const
{
	// The original walks the 30 slots and returns the first whose flag bit 0 is set and whose type
	// mask contains every requested bit ((mask & param) == param, not a plain AND).
	for (const SimCopterMissions::FSimCopterMissionRecord& Record : MissionSystem.GetRecords())
	{
		if (Record.bActive && (Record.TypeMask & TypeMask) == TypeMask)
		{
			return Record.EventId;
		}
	}
	return INDEX_NONE;
}

// SCHOOK: CountActiveRecordsOfType 0x004abb00
int32 ASimCopterMissionSystemActor::CountActiveMissionsOfType(const int32 TypeMask) const
{
	int32 Count = 0;
	for (const SimCopterMissions::FSimCopterMissionRecord& Record : MissionSystem.GetRecords())
	{
		if (Record.bActive && (Record.TypeMask & TypeMask) == TypeMask)
		{
			++Count;
		}
	}
	return Count;
}

bool ASimCopterMissionSystemActor::NotifyMissionPersonBoarded(ASimCopterGroundAgent* Person)
{
	const bool bAmbulanceHandoff =
		Person != nullptr && Person->IsAmbulanceHandoffPending();
	if (Person == nullptr ||
		Person->MissionEventId == INDEX_NONE ||
		Person->HasMissionResolutionReported() ||
		Person->IsMissionPickupCounted() ||
		(!Person->HasClaimedPassengerSeat() && !bAmbulanceHandoff))
	{
		return false;
	}

	if (!bAmbulanceHandoff)
	{
		const ASimCopterHelicopterPawn* Helicopter =
			Cast<ASimCopterHelicopterPawn>(Person->GetBehaviorCarrier());
		if (Helicopter == nullptr ||
			Helicopter->GetMissionPassengerCount(
				Person->MissionEventId,
				Person->GetMissionPassengerKind()) <= 0)
		{
			// Opcode 13 can only acknowledge an action that already happened. A decoded or
			// partial behavior path saying "picked up" is not permission to synthesize a
			// passenger. BHAV 275's ambulance handoff is the other concrete action boundary.
			return false;
		}
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

bool ASimCopterMissionSystemActor::IsPassengerDeliverySurfaceAllowed(
	const ESimCopterMissionPassengerKind Kind,
	const bool bIsWater,
	const float HeightAboveTerrainCm,
	const float GroundToleranceCm)
{
	if (bIsWater)
	{
		return false;
	}

	// BHAV 263 deliberately unloads state-6 patients on the D1 hospital roof. Every other passenger
	// follows FUN_004c9bc0's strict "less than six units above ground" result. Do not ask the scene
	// trace for "ground" here: in the remake it quite correctly returns a rendered building roof,
	// which is precisely the surface that must not complete an ordinary rescue or transport.
	return Kind == ESimCopterMissionPassengerKind::Medevac ||
		HeightAboveTerrainCm < FMath::Max(0.0f, GroundToleranceCm);
}

bool ASimCopterMissionSystemActor::IsRescuePickupAvailable(
	const bool bHarnessDeployed,
	const bool bCanBoardThroughAirframe)
{
	// BHAV 305 -> opcode 48 attaches to the rope end while the aircraft is airborne. Opcode 58
	// moves that same rider into a cabin seat only after the harness has been wound back in. Do not
	// apply the landed-airframe boarding gate to the first half of that decoded sequence.
	return bHarnessDeployed || bCanBoardThroughAirframe;
}

bool ASimCopterMissionSystemActor::IsPassengerDeliveryLocationAllowed(
	const ESimCopterMissionPassengerKind Kind,
	const FVector& FeetWorldLocation) const
{
	const ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem == nullptr)
	{
		return false;
	}

	int32 TileX = INDEX_NONE;
	int32 TileY = INDEX_NONE;
	float TerrainWorldZ = 0.0f;
	if (!TrafficSystem->TryGetPeopleTileCoordinateAtWorldLocation(FeetWorldLocation, TileX, TileY) ||
		!TrafficSystem->TryGetTerrainWorldZAtWorldLocation(FeetWorldLocation, TerrainWorldZ))
	{
		return false;
	}

	float DropHeightOffsetCm = 0.0f;
	if (Kind != ESimCopterMissionPassengerKind::Medevac)
	{
		if (const ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterHelicopterPawn::StaticClass())))
		{
			DropHeightOffsetCm = Helicopter->GetPassengerDropHeightOffsetCm();
		}
	}

	return IsPassengerDeliverySurfaceAllowed(
		Kind,
		TrafficSystem->IsWaterTile(TileX, TileY),
		FeetWorldLocation.Z - TerrainWorldZ,
		TrafficSystem->GetPeopleWorldCmPerOriginalUnit() * 6.0f + DropHeightOffsetCm);
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
	Person->SetAmbulanceHandoffPending(false);
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

	// A living patient or a body already in the cabin always belongs to the hospital unload
	// service first. Do not let a helper medic claim another seat just because the casualty made
	// their mission record inactive.
	for (const FSimCopterMissionPassengerSlot& Slot : Helicopter->GetMissionPassengerSlots())
	{
		if (Slot.Kind == ESimCopterMissionPassengerKind::Medevac)
		{
			return false;
		}
	}

	bool bHasWaitingMedevacPatient = false;
	for (const SimCopterMissions::FSimCopterMissionRecord& Record : MissionSystem.GetRecords())
	{
		if (!Record.bActive || (Record.TypeMask & SimCopterMissions::TYPE_Medevac) == 0)
		{
			continue;
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
		// The looser of the two height bands, because this list serves boarding as well as the
		// drop-off; the release below adds the alight gate and its settle time on top.
		ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(Actor);
		if (Helicopter != nullptr && Helicopter->CanBoardMissionPassengers())
		{
			OutHelicopters.Add(Helicopter);
		}
	}
}

bool ASimCopterMissionSystemActor::IsHelicopterSettledForAlight(const ASimCopterHelicopterPawn& Helicopter)
{
	return Helicopter.CanTransferMissionPassengers() &&
		Helicopter.GetSecondsWithinAlightClearance() >= Helicopter.GetPassengerAlightSettleSeconds();
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
	if (!IsPassengerDeliveryLocationAllowed(Kind, DropLocation))
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

		// Atomically returns this real person's seat and places them at that seat row's door point.
		// AlightFromCarrier owns the shared VM/recovery transform; adding another mission-side spread
		// here used to fan later survivors progressively farther away from the aircraft.
		Person->AlightFromCarrier();
		// No ground snap: they were let out of the cabin, so they fall whatever is left of the
		// cabin's height onto what is underneath rather than appearing already stood on it.
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

void ASimCopterMissionSystemActor::ProcessPassengerTransfers(const float DeltaSeconds)
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

				// Getting out is BHAV 292's business, and it only probes about every thirteenth
				// tick. This loop runs every mission tick, so without the same beat it emptied the
				// cabin the frame the skids came into range - "they get out before I can land".
				if (!IsHelicopterSettledForAlight(*Helicopter))
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
			// Restore the proven pre-5ff1eaa boarding point. People enter at the helicopter's
			// midpoint; the side/row cabin-door locations remain correct for exiting passengers.
			const FVector CabinMidpoint = Helicopter->GetActorLocation();

			// Guidance is a BACKSTOP, not the approach. BHAV 750 -> 291 already owns this: a
			// 40-tick (2.67 s) wait at spawn, then a loop that commits to boarding only inside
			// ONE TILE, walks at `movespeed := 16` (125 cm/s) in eight octant directions on a
			// re-rolled rand(40)+30 step budget, and otherwise walks toward the player, waves
			// ('WvNo'), idles five ticks and accrues boredom (BHAV 290).
			//
			// Steering every tick ran straight over all of it - a live guidance target skips the
			// VM's own movement branch in UpdateMovement entirely - so passengers beelined in at
			// the generic 230 cm/s from nearly two tiles out and were aboard before the pilot
			// could set down. Let the shipped program do the walking, and only step in when it
			// demonstrably has not, the same way the medevac handoff watchdog was demoted.
			// The clock only runs while there is somebody in range who could be walking in. It is
			// not "how long has this mission existed" - a passenger three tiles away has not
			// stalled, they simply have not been collected yet.
			const bool bPassengerInRange = TrafficSystem->FindMissionPersonNear(
				Mission.EventId,
				CabinMidpoint,
				PassengerPickupRadiusCm,
				PassengerTransferMaxVerticalDeltaCm) != nullptr;

			float& StallSeconds = PassengerBoardStallSeconds.FindOrAdd(Mission.EventId);
			StallSeconds = bPassengerInRange ? StallSeconds + DeltaSeconds : 0.0f;
			if (bPassengerInRange && StallSeconds >= PassengerBoardRecoverySeconds)
			{
				TrafficSystem->GuideMissionPeopleToLocation(
					Mission.EventId,
					CabinMidpoint,
					CabinMidpoint,
					SeatsAvailable,
					PassengerPickupRadiusCm,
					PassengerTransferMaxVerticalDeltaCm,
					PassengerBoardGuidanceSeconds,
					ASimCopterGroundAgent::ShippedPassengerWalkSpeedCmPerSec);
			}

			// Boarding attaches the passenger to the helicopter and claims their seat as part of
			// the pickup. BoardCarrier also invokes the idempotent mission action service, so this
			// loop neither books a second seat nor posts a second pickup event.
			const int32 PickedUp = TrafficSystem->BoardMissionPeopleTouching(
				Mission.EventId,
				CabinMidpoint,
				SeatsAvailable,
				PassengerBoardTouchRadiusCm,
				PassengerTransferMaxVerticalDeltaCm,
				nullptr,
				Helicopter);
			if (PickedUp <= 0)
			{
				continue;
			}

			// Somebody got in, so the approach is working: the next passenger starts from zero
			// rather than inheriting a countdown the queue ahead of them ran down.
			StallSeconds = 0.0f;
			break;
		}
	}

	// Drop entries for missions that are over, so the map cannot grow across a long session.
	for (auto It = PassengerBoardStallSeconds.CreateIterator(); It; ++It)
	{
		const SimCopterMissions::FSimCopterMissionRecord* Record = MissionSystem.FindRecord(It.Key());
		if (Record == nullptr || !Record->bActive)
		{
			It.RemoveCurrent();
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
				if (IsRescuePickupAvailable(bUseHarness, Helicopter->CanBoardMissionPassengers()))
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

			// BHAV 700's loop reaches 303 'Rescue try get off heli or bucket if appropriate' between
			// idles, so a survivor never leaves the cabin the instant the aircraft comes into range
			// either. Same beat as the transport drop-off above.
			if (Mission.Deliverable <= 0 || !IsHelicopterSettledForAlight(*Helicopter))
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

	// Cache every active medevac's hospital before lifecycle completion clears its record type.
	// A casualty can complete the scoring record while their real body is still occupying a seat.
	TSet<int32> ActiveMedevacEvents;
	if (TrafficSystem != nullptr)
	{
		for (const SimCopterMissions::FSimCopterMissionRecord& Record : MissionSystem.GetRecords())
		{
			if (!Record.bActive || (Record.TypeMask & SimCopterMissions::TYPE_Medevac) == 0)
			{
				continue;
			}
			ActiveMedevacEvents.Add(Record.EventId);
			if (!IsValidMissionTile(Record.SecondaryX, Record.SecondaryY))
			{
				continue;
			}
			MedevacHospitalTiles.Add(Record.EventId, FIntPoint(Record.SecondaryX, Record.SecondaryY));
		}
	}

	// See every seat, not only landed helicopters. An inactive casualty record must retain its
	// hospital service while the body is still being flown there.
	TArray<AActor*> HelicopterActors;
	if (GetWorld() != nullptr)
	{
		UGameplayStatics::GetAllActorsOfClass(
			GetWorld(),
			ASimCopterHelicopterPawn::StaticClass(),
			HelicopterActors);
	}
	TArray<ASimCopterHelicopterPawn*> AllHelicopters;
	TArray<ASimCopterHelicopterPawn*> Helicopters;
	for (AActor* Actor : HelicopterActors)
	{
		ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(Actor);
		if (Helicopter == nullptr)
		{
			continue;
		}
		AllHelicopters.Add(Helicopter);
		if (Helicopter->CanTransferMissionPassengers())
		{
			Helicopters.Add(Helicopter);
		}
	}

	// Keep a mission-required state-5 worker physically posted on every relevant hospital roof.
	// The post remains required for an active mission anywhere in the city, and after a casualty
	// until the last body has actually left the cabin.
	TMap<int32, FVector> MedevacDropoffs;
	if (TrafficSystem != nullptr)
	{
		for (auto It = MedevacHospitalTiles.CreateIterator(); It; ++It)
		{
			const int32 EventId = It.Key();
			bool bHasPatientAboard = false;
			for (const ASimCopterHelicopterPawn* Helicopter : AllHelicopters)
			{
				if (Helicopter != nullptr &&
					Helicopter->GetMissionPassengerCount(
						EventId,
						ESimCopterMissionPassengerKind::Medevac) > 0)
				{
					bHasPatientAboard = true;
					break;
				}
			}

			if (!ActiveMedevacEvents.Contains(EventId) &&
				!bHasPatientAboard &&
				FindMedevacHandoff(EventId) == nullptr)
			{
				It.RemoveCurrent();
				continue;
			}

			const FIntPoint HospitalTile = It.Value();
			FVector HospitalLocation = FVector::ZeroVector;
			if (TrafficSystem->TryGetTileCenterWorldLocation(
					HospitalTile.X,
					HospitalTile.Y,
					HospitalLocation))
			{
				MedevacDropoffs.Add(EventId, HospitalLocation);
				TrafficSystem->EnsureHospitalParamedicAtTile(HospitalTile.X, HospitalTile.Y);
			}
		}
	}

	// Start a handoff for any landed helicopter that is at a hospital with patients still aboard.
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

	// FUN_004c25b0 explicitly spawns class 0x0c / state 5 on XBLD D1. Do not pause that worker:
	// BHAV 801 -> 263 is the original unload implementation. It selects the real patient aboard
	// the player (op 84), walks over, alights them through the carrier service (op 47), totes
	// them (op 44), and sets them down laterally (op 51). BHAV 282 then recognizes XBLD D1,
	// posts the delivery, and leaves the map. This record only watches that interaction for
	// progress so a malformed legacy seat cannot strand a mission forever.
	ASimCopterGroundAgent* Emt = TrafficSystem->FindNearestAvailablePersonInState(
		HospitalCenter,
		5,
		MedevacHospitalHandoffRadiusCm,
		/*bRequirePersistentHospitalCrew*/ true);
	if (Emt == nullptr)
	{
		// EnsureHospitalParamedicAtTile retries every mission tick. Do not invent a temporary
		// worker or visual prop while the original-data actor is still being staged.
		return;
	}

	FSimCopterMedevacHandoff Handoff;
	Handoff.EventId = EventId;
	Handoff.Helicopter = Helicopter;
	Handoff.Emt = Emt;
	Handoff.LastOnboardCount = Helicopter->GetMissionPassengerCount(
		EventId,
		ESimCopterMissionPassengerKind::Medevac);
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
	if (!Helicopter->CanTransferMissionPassengers())
	{
		return false; // the VM keeps its own stack and can try again after a later landing
	}

	const int32 Onboard = Helicopter->GetMissionPassengerCount(
		Handoff.EventId,
		ESimCopterMissionPassengerKind::Medevac);
	if (Onboard <= 0)
	{
		return false;
	}

	if (Onboard < Handoff.LastOnboardCount)
	{
		// Opcode 47 has released a real patient from the cabin. Their own BHAV now owns
		// completion on the hospital tile, while the medic loops for the next seat.
		Handoff.LastOnboardCount = Onboard;
		Handoff.SecondsWithoutProgress = 0.0f;
	}
	else if (ResolveTrafficSystem() != nullptr &&
		ResolveTrafficSystem()->FindPersonAboardForEvent(
			Helicopter, Handoff.EventId, ESimCopterMissionPassengerKind::Medevac) != nullptr)
	{
		// A real casualty is in the cabin, so there is a real handoff to wait for and
		// DeliverMedevacDirectly would refuse anyway. Do not run the clock down toward a recovery
		// that cannot happen - the medic walking over is not a failure.
		Handoff.SecondsWithoutProgress = 0.0f;
	}
	else
	{
		Handoff.SecondsWithoutProgress += DeltaSeconds;
	}

	if (Handoff.SecondsWithoutProgress < MedevacBehaviorRecoverySeconds)
	{
		return true;
	}

	// Recovery for an abstract legacy seat or a behavior asset that failed to load. This is not
	// the normal animation path: the executable's BHAV has had a generous window to act first.
	UE_LOG(LogTemp, Warning,
		TEXT("Medevac %d made no BHAV-263 unload progress for %.1fs; resolving %d remaining seat(s) through the mission service."),
		Handoff.EventId,
		Handoff.SecondsWithoutProgress,
		Onboard);
	DeliverMedevacDirectly(Handoff.EventId, Helicopter);
	return false;
}

void ASimCopterMissionSystemActor::EndMedevacHandoff(
	FSimCopterMedevacHandoff& Handoff,
	const bool bResolvePatients)
{
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
	Handoff.Emt.Reset();
	Handoff.Helicopter.Reset();
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

	// ABSTRACT SEATS ONLY. This exists for a seat with no person behind it - a legacy save, a test
	// fixture, a behaviour asset that failed to load - where nothing can ever animate the handoff
	// and the mission would otherwise never close.
	//
	// It must never touch a real casualty. Teleporting one out of the cabin, crediting the delivery
	// and destroying the actor is precisely what the player sees as "the paramedic snatched my
	// injured Sim out of the helicopter from several tiles away": the medic was simply still on its
	// way, or could not reach an aircraft parked off its roof, and this fired at
	// MedevacBehaviorRecoverySeconds regardless. A real patient waits for a real handoff, however
	// long that takes - the pilot can always fly somewhere the medic can reach.
	if (ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem())
	{
		if (TrafficSystem->FindPersonAboardForEvent(
				Helicopter, EventId, ESimCopterMissionPassengerKind::Medevac) != nullptr)
		{
			return;
		}
	}
	ReleaseMissionPassengersFromHelicopter(
		Helicopter,
		EventId,
		ESimCopterMissionPassengerKind::Medevac,
		Onboard,
		Helicopter->GetPassengerDropWorldLocation(),
		/*bRemoveAfterDelivery*/ true);
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
		SimCopterMissions::TYPE_Medevac | SimCopterMissions::TYPE_RescuePeople | SimCopterMissions::TYPE_RooftopRescue;

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

	// Feeds the FIRHOSLP loop in UpdateEmergencySirenAudio (id 0x14).
	ServiceJetWorld = TruckWorld;
	ServiceJetLastSeconds = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;

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

bool ASimCopterMissionSystemActor::ReportTrafficJamCarCleared(int32 EventId)
{
	const SimCopterMissions::FSimCopterMissionRecord* Record = MissionSystem.FindRecord(EventId);
	if (Record == nullptr || !Record->bActive ||
		(Record->TypeMask & SimCopterMissions::TYPE_TrafficJam) == 0)
	{
		return false;
	}

	// SCHOOK: FUN_0049d7e0 posts {0x1b, car+0x113, ..., 1} after message 0 removes
	// the car's 0x200 jam flag. UpdateLifecycle closes the record when every counted car clears.
	MissionSystem.PostEvent(SimCopterMissions::EVT_CarCleared, EventId, 1);
	return true;
}

void ASimCopterMissionSystemActor::ReportBurglarCaught(int32 EventId)
{
	// FUN_004b8c90: {0x25, eventId, ., ., 1}. This is the one that closes the mission properly -
	// the burglar-specific lifecycle test sees a caught criminal instead of a continuing cycle.
	MissionSystem.PostEvent(SimCopterMissions::EVT_CriminalCaught, EventId, 1);
}

void ASimCopterMissionSystemActor::ReportBurglarSpawnFailed(int32 EventId)
{
	// FUN_004b8b60 only posts this when FUN_0049bd00 returned 0, i.e. nowhere to put the driver.
	// CAT_ExpireSilently makes the update loop skip the completion test, so the record just runs
	// out - no fanfare and no payout, which is the point.
	MissionSystem.PostEvent(
		SimCopterMissions::EVT_SetCategory,
		EventId,
		SimCopterMissions::CAT_ExpireSilently);
}

void ASimCopterMissionSystemActor::EnsureMessageLogWidget()
{
	if (!bShowMissionMessageLog || MessageLogWidget.IsValid() || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	TSharedRef<SVerticalBox> LogBox = SNew(SVerticalBox);
	MessageLogBox = LogBox;
	TSharedRef<SBorder> LogPanel =
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.58f))
		.Padding(FMargin(10.0f, 8.0f))
		[
			LogBox
		];
	MessageLogPanel = LogPanel;
	MessageLogWidget =
		SNew(SBox)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(FMargin(MessageLogScreenPadding.X, MessageLogScreenPadding.Y, 0.0f, 0.0f))
		.Visibility(EVisibility::Collapsed)
		[
			LogPanel
		];

	GEngine->GameViewport->AddViewportWidgetContent(MessageLogWidget.ToSharedRef(), 20);
}

void ASimCopterMissionSystemActor::RemoveMessageLogWidget()
{
	if (GEngine != nullptr && GEngine->GameViewport != nullptr && MessageLogWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MessageLogWidget.ToSharedRef());
	}

	MessageLogPanel.Reset();
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
	TArray<SimCopterMissionMarkerLayout::FUiObstacle> UiObstacles;
	BuildMissionMarkerUiObstacles(UiObstacles);
	TArray<SimCopterMissionMarkerLayout::FPlacedMarker> PlacedMarkers;

	const FVector2D ClampedMarkerSize(
		FMath::Clamp(MissionMarkerSize.X, 104.0f, 160.0f),
		FMath::Clamp(MissionMarkerSize.Y, 63.0f, 88.0f));
	const UWorld* MarkerWorld = GetWorld();
	const FVector2D MarkerViewportSize = MarkerWorld != nullptr
		? UWidgetLayoutLibrary::GetViewportWidgetGeometry(MarkerWorld).GetLocalSize()
		: FVector2D::ZeroVector;

	FVector DistanceOrigin = FVector::ZeroVector;
	bool bHasDistanceOrigin = false;
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0))
		{
			if (const APawn* PlayerPawn = PlayerController->GetPawn())
			{
				DistanceOrigin = PlayerPawn->GetActorLocation();
				bHasDistanceOrigin = true;
			}
			else
			{
				FRotator UnusedRotation;
				PlayerController->GetPlayerViewPoint(DistanceOrigin, UnusedRotation);
				bHasDistanceOrigin = true;
			}
		}
	}

	for (const FSimCopterMissionWorldMarkerEntry& Marker : Markers)
	{
		FVector2D ScreenPosition;
		bool bClamped = false;
		if (!ProjectMissionMarkerToScreen(Marker.WorldLocation, ScreenPosition, bClamped))
		{
			continue;
		}

		bool bOverlapAdjusted = false;
		ScreenPosition = SimCopterMissionMarkerLayout::ResolveMarkerCenter(
			ScreenPosition,
			ClampedMarkerSize,
			MarkerViewportSize,
			MissionMarkerEdgePadding,
			UiObstacles,
			PlacedMarkers,
			MissionMarkerAllowedOverlap,
			bOverlapAdjusted);
		bClamped = bClamped || bOverlapAdjusted;

		// Deconflict only against earlier mission markers. A half-panel overlap is intentional: it
		// keeps clustered objectives compact while leaving every icon visibly distinct.
		PlacedMarkers.Emplace(
			FBox2D(ScreenPosition - ClampedMarkerSize * 0.5f, ScreenPosition + ClampedMarkerSize * 0.5f));

		const FVector2D DrawPosition(
			ScreenPosition.X - ClampedMarkerSize.X * 0.5f,
			ScreenPosition.Y - ClampedMarkerSize.Y * 0.5f);
		const FString DistanceText = bHasDistanceOrigin
			? FormatMissionMarkerDistance(FVector::Distance(DistanceOrigin, Marker.WorldLocation))
			: TEXT("-- M");
		const FString PlateText = FString::Printf(TEXT("%s / %s"), *Marker.Label, *DistanceText);
		const FSlateBrush* IconBrush = GetMissionMarkerIconBrush(Marker.Label);
		const FSlateBrush* IconShadowBrush = GetMissionMarkerIconShadowBrush(Marker.Label);
		const FSlateBrush* AccentGlowBrush = GetMissionMarkerAccentGlowBrush();
		const float ForegroundOpacity = bClamped ? 0.70f : 1.0f;
		FLinearColor AccentGlowColor = Marker.Color;
		AccentGlowColor.A *= ForegroundOpacity;

		MissionMarkerCanvas->AddSlot()
		.Offset(FMargin(DrawPosition.X, DrawPosition.Y, ClampedMarkerSize.X, ClampedMarkerSize.Y))
		.Alignment(FVector2D::ZeroVector)
		[
			SNew(SBox)
			.WidthOverride(ClampedMarkerSize.X)
			.HeightOverride(ClampedMarkerSize.Y)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(56.0f)
					.HeightOverride(43.0f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(SImage)
							.Image(IconShadowBrush)
							.ColorAndOpacity(FLinearColor(0.01f, 0.018f, 0.022f, 0.34f * ForegroundOpacity))
						]
						+ SOverlay::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(SImage)
							.Image(IconBrush)
							.ColorAndOpacity(FLinearColor(0.78f, 0.84f, 0.85f, ForegroundOpacity))
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.0f, 2.0f, 0.0f, 0.0f))
				[
					SNew(SBox)
					.HeightOverride(18.0f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()
						[
							SNew(SBorder)
							.BorderImage(FCoreStyle::Get().GetBrush(TEXT("BoxShadow")))
							.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.30f * ForegroundOpacity))
						]
						+ SOverlay::Slot()
						.Padding(FMargin(1.0f, 1.0f, 1.0f, 2.0f))
						[
							SNew(SBorder)
							.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
							.BorderBackgroundColor(FLinearColor(0.24f, 0.29f, 0.30f, 0.82f * ForegroundOpacity))
							.Padding(FMargin(1.0f))
							[
								SNew(SOverlay)
								+ SOverlay::Slot()
								[
									SNew(SBorder)
									.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
									.BorderBackgroundColor(FLinearColor(0.035f, 0.075f, 0.085f, 0.90f * ForegroundOpacity))
									.Padding(FMargin(4.0f, 0.0f))
									[
										SNew(SBox)
										.HAlign(HAlign_Fill)
										.VAlign(VAlign_Center)
										[
											SNew(STextBlock)
											.Text(FText::FromString(PlateText))
											.Justification(ETextJustify::Center)
											.ColorAndOpacity(FLinearColor(0.95f, 0.975f, 0.98f, ForegroundOpacity))
											.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))
										]
									]
								]
								+ SOverlay::Slot()
								.HAlign(HAlign_Fill)
								.VAlign(VAlign_Top)
								[
									SNew(SBox)
									.HeightOverride(1.0f)
									[
										SNew(SImage)
										.Image(AccentGlowBrush)
										.ColorAndOpacity(AccentGlowColor)
									]
								]
								+ SOverlay::Slot()
								.HAlign(HAlign_Fill)
								.VAlign(VAlign_Bottom)
								[
									SNew(SBox)
									.HeightOverride(1.0f)
									[
										SNew(SImage)
										.Image(AccentGlowBrush)
										.ColorAndOpacity(AccentGlowColor)
									]
								]
							]
						]
					]
				]
			]
		];
	}

	MissionMarkerWidget->SetVisibility(Markers.Num() > 0 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
}

bool ASimCopterMissionSystemActor::IsMissionBegun(const SimCopterMissions::FSimCopterMissionRecord& Record) const
{
	if (!Record.bActive || Record.TypeMask == 0)
	{
		return false;
	}

	const bool bHasPassengerPickup = (Record.TypeMask & SimCopterMissions::TYPE_Transport) != 0;
	const bool bHasMedicalPickup = (Record.TypeMask & SimCopterMissions::TYPE_Medevac) != 0;
	const bool bHasRescuePickup = (Record.TypeMask & SimCopterMissions::TYPE_RescuePeople) != 0;

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
		return (TransportOnboard > 0 || Record.TransportDelivered > 0 || Record.VictimsPickedUp > 0);
	}

	if (bHasMedicalPickup)
	{
		int32 MedevacOnboard = 0;
		if (const UWorld* World = GetWorld())
		{
			for (TActorIterator<ASimCopterHelicopterPawn> It(const_cast<UWorld*>(World)); It; ++It)
			{
				MedevacOnboard += It->GetMissionPassengerCount(
					Record.EventId,
					ESimCopterMissionPassengerKind::Medevac);
			}
		}
		return (MedevacOnboard > 0 || Record.MedevacDelivered > 0 || Record.VictimsPickedUp > 0);
	}

	if (bHasRescuePickup)
	{
		int32 RescueOnboard = 0;
		if (const UWorld* World = GetWorld())
		{
			for (TActorIterator<ASimCopterHelicopterPawn> It(const_cast<UWorld*>(World)); It; ++It)
			{
				RescueOnboard += It->GetMissionPassengerCount(
					Record.EventId,
					ESimCopterMissionPassengerKind::Rescue);
			}
		}
		return (RescueOnboard > 0 || Record.RescueDelivered > 0 || Record.VictimsPickedUp > 0);
	}

	return Record.VictimsPickedUp > 0;
}

void ASimCopterMissionSystemActor::BuildMissionWorldMarkers(TArray<FSimCopterMissionWorldMarkerEntry>& OutMarkers) const
{
	OutMarkers.Reset();

	// Burglar tags track the live getaway car, so this needs the traffic system rather than tiles.
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
		const bool bBegun = IsMissionBegun(Record);

		if (bHasPassengerPickup)
		{
			if (bBegun && bHasDropoff)
			{
				AddTileMarker(Record.SecondaryX, Record.SecondaryY, TEXT("DROPOFF"), Record.Name, FLinearColor(0.05f, 0.72f, 0.32f, 1.0f));
			}
			else
			{
				AddTileMarker(Record.TileX, Record.TileY, TEXT("TRANSPORT"), Record.Name, FLinearColor(0.08f, 0.46f, 0.95f, 1.0f));
			}
			continue;
		}

		if (bHasMedicalPickup)
		{
			if (bBegun && bHasDropoff)
			{
				AddTileMarker(Record.SecondaryX, Record.SecondaryY, TEXT("HOSPITAL"), Record.Name, FLinearColor(0.05f, 0.72f, 0.32f, 1.0f));
			}
			else
			{
				AddTileMarker(Record.TileX, Record.TileY, TEXT("PATIENT"), Record.Name, FLinearColor(0.8f, 0.12f, 0.55f, 1.0f));
			}
			continue;
		}

		if (bHasRescuePickup)
		{
			if (bBegun && bHasDropoff)
			{
				AddTileMarker(Record.SecondaryX, Record.SecondaryY, TEXT("DROP"), Record.Name, FLinearColor(0.05f, 0.72f, 0.32f, 1.0f));
			}
			else
			{
				const TCHAR* RescueLabel =
					(Record.TypeMask & SimCopterMissions::TYPE_RooftopRescue) == SimCopterMissions::TYPE_RooftopRescue
						? TEXT("ROOFTOP")
						: TEXT("RESCUE");
				AddTileMarker(Record.TileX, Record.TileY, RescueLabel, Record.Name, FLinearColor(0.95f, 0.55f, 0.08f, 1.0f));
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

		if ((Record.TypeMask & SimCopterMissions::TYPE_Burglar) != 0)
		{
			// FUN_004b8630 republishes the moving car tile. While the burglar is outside, their BHAV
			// republishes the person's tile instead, so use the record rather than pinning the tag to
			// the parked car.
			FVector CarWorld = FVector::ZeroVector;
			int32 SpotlightMark = 0;
			bool bStopped = false;
			if (TrafficSystem != nullptr &&
				TrafficSystem->TryGetBurglarCarState(Record.EventId, CarWorld, SpotlightMark, bStopped))
			{
				if (bStopped)
				{
					AddTileMarker(Record.TileX, Record.TileY, TEXT("BURGLAR"), Record.Name,
						FLinearColor(0.86f, 0.18f, 0.18f, 1.0f));
				}
				else
				{
					FSimCopterMissionWorldMarkerEntry Marker;
					Marker.WorldLocation = CarWorld;
					Marker.Label = TEXT("BURGLAR");
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
				AddTileMarker(Record.TileX, Record.TileY, TEXT("BURGLAR"), Record.Name, FLinearColor(0.86f, 0.18f, 0.18f, 1.0f));
			}
			continue;
		}

		if ((Record.TypeMask & SimCopterMissions::TYPE_Robber) != 0)
		{
			AddTileMarker(Record.TileX, Record.TileY, TEXT("ROBBER"), Record.Name, FLinearColor(0.86f, 0.18f, 0.18f, 1.0f));
			continue;
		}
		if ((Record.TypeMask & SimCopterMissions::TYPE_Arsonist) != 0)
		{
			AddTileMarker(Record.TileX, Record.TileY, TEXT("ARSONIST"), Record.Name, FLinearColor(0.86f, 0.18f, 0.18f, 1.0f));
			continue;
		}
		if ((Record.TypeMask & SimCopterMissions::TYPE_Mugger) != 0)
		{
			AddTileMarker(Record.TileX, Record.TileY, TEXT("MUGGER"), Record.Name, FLinearColor(0.86f, 0.18f, 0.18f, 1.0f));
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
			return true;
		}
	}

	return false;
}

void ASimCopterMissionSystemActor::BuildMissionMarkerUiObstacles(
	TArray<SimCopterMissionMarkerLayout::FUiObstacle>& OutObstacles) const
{
	OutObstacles.Reset();
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const FGeometry ViewportGeometry = UWidgetLayoutLibrary::GetViewportWidgetGeometry(World);
	const FVector2D ViewportSize = ViewportGeometry.GetLocalSize();
	if (ViewportSize.X <= 0.0f || ViewportSize.Y <= 0.0f)
	{
		return;
	}

	TArray<TSharedPtr<SWidget>> Widgets;
	if (MessageLogWidget.IsValid() && MessageLogWidget->GetVisibility().IsVisible() && MessageLogPanel.IsValid())
	{
		Widgets.Add(MessageLogPanel);
	}
	if (const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0))
	{
		if (const ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(PlayerController->GetPawn()))
		{
			Helicopter->AppendMissionMarkerAvoidanceWidgets(Widgets);
		}
	}

	for (const TSharedPtr<SWidget>& Widget : Widgets)
	{
		if (!Widget.IsValid() || !Widget->GetVisibility().IsVisible())
		{
			continue;
		}

		const FGeometry& WidgetGeometry = Widget->GetCachedGeometry();
		const FVector2D AbsoluteMin = WidgetGeometry.GetAbsolutePosition();
		const FVector2D AbsoluteMax = AbsoluteMin + FVector2D(WidgetGeometry.GetAbsoluteSize());
		const FVector2D LocalA = ViewportGeometry.AbsoluteToLocal(AbsoluteMin);
		const FVector2D LocalB = ViewportGeometry.AbsoluteToLocal(AbsoluteMax);
		const FVector2D LocalMin(
			FMath::Clamp(FMath::Min(LocalA.X, LocalB.X), 0.0f, ViewportSize.X),
			FMath::Clamp(FMath::Min(LocalA.Y, LocalB.Y), 0.0f, ViewportSize.Y));
		const FVector2D LocalMax(
			FMath::Clamp(FMath::Max(LocalA.X, LocalB.X), 0.0f, ViewportSize.X),
			FMath::Clamp(FMath::Max(LocalA.Y, LocalB.Y), 0.0f, ViewportSize.Y));
		if (LocalMax.X - LocalMin.X > 0.5f && LocalMax.Y - LocalMin.Y > 0.5f)
		{
			OutObstacles.Emplace(FBox2D(LocalMin, LocalMax), MissionMarkerUiPadding);
		}
	}
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

	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportWidgetGeometry(World).GetLocalSize();
	if (ViewportSize.X <= 0.0f || ViewportSize.Y <= 0.0f)
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
		// MissionMarkerCanvas is a viewport Slate widget, so it needs DPI- and quality-scale-free
		// widget coordinates. Feeding raw projected pixels into it makes the error grow farther
		// from the upper-left corner.
		bProjected = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
			PlayerController,
			WorldLocation,
			ScreenPosition,
			false);
	}

	if (!bProjected)
	{
		const FVector2D ViewCenter = ViewportSize * 0.5f;
		if (bInFront)
		{
			const float Aspect = FMath::Max(ViewportSize.X / FMath::Max(1.0f, ViewportSize.Y), 0.01f);
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
			ScreenPosition = ViewCenter + Direction * FMath::Max(ViewportSize.X, ViewportSize.Y);
		}
	}

	const FVector2D ClampedMarkerSize(
		FMath::Clamp(MissionMarkerSize.X, 104.0f, 160.0f),
		FMath::Clamp(MissionMarkerSize.Y, 63.0f, 88.0f));
	// Project the true objective/car location, then lift only the panel. This keeps its horizontal
	// center locked to the area regardless of distance, camera pitch, or helicopter bank.
	const FVector2D DesiredMarkerCenter(
		ScreenPosition.X,
		ScreenPosition.Y - ClampedMarkerSize.Y * 0.5f - MissionMarkerScreenOffset);
	const float MinX = MissionMarkerEdgePadding + ClampedMarkerSize.X * 0.5f;
	const float MaxX = ViewportSize.X - MissionMarkerEdgePadding - ClampedMarkerSize.X * 0.5f;
	const float MinY = MissionMarkerEdgePadding + ClampedMarkerSize.Y * 0.5f;
	const float MaxY = ViewportSize.Y - MissionMarkerEdgePadding - ClampedMarkerSize.Y * 0.5f;
	const FVector2D ClampedPosition(
		FMath::Clamp(DesiredMarkerCenter.X, MinX, MaxX),
		FMath::Clamp(DesiredMarkerCenter.Y, MinY, MaxY));

	bOutClamped = !bInFront || FVector2D::Distance(DesiredMarkerCenter, ClampedPosition) > 0.5f;
	OutScreenPosition = ClampedPosition;
	return true;
}

void ASimCopterMissionSystemActor::PushMissionLogMessage(const FString& Text, const FLinearColor& Color, bool bDestroyOnTimeout)
{
	if (!bShowMissionMessageLog)
	{
		return;
	}

	for (FSimCopterMissionLogEntry& Existing : MissionMessageLog)
	{
		if (Existing.Text == Text)
		{
			Existing.Color = Color;
			Existing.RemainingSeconds = MessageLogDurationSeconds;
			Existing.bDestroyOnTimeout = bDestroyOnTimeout;
			RefreshMessageLogWidget();
			return;
		}
	}

	FSimCopterMissionLogEntry Entry;
	Entry.Text = Text;
	Entry.Color = Color;
	Entry.RemainingSeconds = MessageLogDurationSeconds;
	Entry.bDestroyOnTimeout = bDestroyOnTimeout;
	MissionMessageLog.Insert(Entry, 0);

	while (MissionMessageLog.Num() > MaxMessageLogEntries)
	{
		MissionMessageLog.Pop(EAllowShrinking::No);
	}

	UE_LOG(LogTemp, Display, TEXT("[Mission] %s"), *Text);
	RefreshMessageLogWidget();
}

void ASimCopterMissionSystemActor::ClearMissionLogMessage(const FString& Text)
{
	bool bRemoved = false;
	for (int32 Index = MissionMessageLog.Num() - 1; Index >= 0; --Index)
	{
		if (MissionMessageLog[Index].Text == Text)
		{
			MissionMessageLog.RemoveAt(Index);
			bRemoved = true;
		}
	}
	if (bRemoved)
	{
		RefreshMessageLogWidget();
	}
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
			SimCopterMissions::GetMissionUpdateText(Message.TextId),
			*FormatSignedAmount(Message.ValueA, TEXT("points")),
			*MissionName);
	case 9:
		OutColor = Message.ValueA < 0 ? FLinearColor(1.0f, 0.38f, 0.32f, 1.0f) : FLinearColor(1.0f, 0.86f, 0.34f, 1.0f);
		return FString::Printf(
			TEXT("%s: %s (%s)"),
			SimCopterMissions::GetMissionUpdateText(Message.TextId),
			*FormatSignedAmount(Message.ValueA, TEXT("cash")),
			*MissionName);
	default:
		OutColor = FLinearColor::White;
		return FString();
	}
}

// SCHOOK: TubaLeader 443 / TubaInit 444 (person states 17 and 18, DAT_0058de80)
//
// The band is not a scripted prop - it is eight ordinary people running the shipped programs, and
// every part of what it does is in BHAV 444:
//
//   rec[2]  bind 'Play'                     the instrument animation (NOT a wave)
//   rec[24] movespeed := 8                  62.5 cm/s at 15 Hz - a march, not a jog
//   rec[3]  select class 9 within 4 tiles   the player, wherever they are (incl. the cockpit)
//   rec[4]  op 38 walk to them, autoturn    through MoveStep, so tile classes and the climb gate
//                                           apply and they cannot cross a building
//   rec[25]/[27] attr29 == 444 ?            member -> random turns + 'Move rand speed rand time'
//                                           and a tick-gated 1-in-3 sound 37 (one trombone /
//                                           trumpet / tuba note out of nine)
//                                           leader -> sound 38, march.wav, and keep following
//
// The old implementation was none of this: it drove the agents with SetMoveTarget on the generic
// seek mover (which does not sweep or check tiles - hence walking through walls), bound the wave
// clip instead of 'Play', configured them with an EMPTY original-game root so no behaviour model
// or figure could load, and played march.wav itself from one looping voice slot at the airport.
void ASimCopterMissionSystemActor::SpawnMarchingBandAtAirport()
{
	if (bMarchingBandSpawned || GetWorld() == nullptr)
	{
		return;
	}

	ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem == nullptr)
	{
		return;
	}

	const FIntPoint AirportOrigin = TrafficSystem->GetAirportOriginTile();
	if (AirportOrigin.X < 0 || AirportOrigin.Y < 0)
	{
		return;
	}

	// One leader on state 17 (BHAV 443) and the rest on state 18 (BHAV 444). Behaviour class 18 is
	// TubaExpert, the band figure, and TrySpawnMissionPerson resolves the original-game root and
	// finds a spawn point that is not inside a building - both of which the old path skipped.
	constexpr int32 BandCount = 8;
	constexpr int32 TubaLeaderPersonState = 17;
	constexpr int32 TubaMemberPersonState = 18;
	constexpr int32 TubaBehaviorClass = 18;

	// On the pads, where the player lands. The airport is tile class 1 - both stamped ids fall
	// through GetTileClassForBuildingId to its catch-all `return 1`, 0xde being past 0xD2..0xDC and
	// 0xf6 past 0xE8..0xF5 - and class 1 is in no behaviour row, so a walker there would refuse
	// every direction and spin. SetIgnoresTileClassRules waives that one rule for the band; the
	// climb gate still keeps them out of the terminal.
	for (int32 Index = 0; Index < BandCount; ++Index)
	{
		ASimCopterGroundAgent* Agent = nullptr;
		const bool bSpawned = TrafficSystem->TrySpawnMissionPerson(
			Index == 0 ? TubaLeaderPersonState : TubaMemberPersonState,
			TubaBehaviorClass,
			AirportOrigin.X + (Index % 4),
			AirportOrigin.Y + (Index / 4),
			INDEX_NONE,
			TEXT("TubaExpert"),
			&Agent);

		if (bSpawned && Agent != nullptr)
		{
			Agent->SetIgnoresTileClassRules(true);
			// Uniform colours are a remake flourish and stay - the original band is all one figure.
			// The offset indexes a 14-entry clothes table, so it has to stay inside it: the old
			// (i % 5) * 8 + 4 ran to 36 and was clamped, giving five members the same outfit.
			Agent->SetPedestrianFigureClothesOffset((Index % 5) * 3 + 1);
			MarchingBandAgents.Add(Agent);
		}
	}

	bMarchingBandSpawned = MarchingBandAgents.Num() > 0;
}

// SCHOOK: FireworksInit 0x004916e0 / FUN_0048e0b0 / FUN_0048ed00
// Faithfully launches fireworks rockets with ascent trails, apex burst rings, and falling ember sparkles.
// - Launch Whistle: Sound 0x17 (TGSHWH.WAV) 3D whistle played AT LAUNCH (t = 0s)
// - Apex Boom: Sound 0x07 (BOOM1.WAV) 3D explosion played AT APEX DETONATION (t = 1.8s)
// - Randomized Interval (FUN_0048ed00): 0.5s to 1.5s delay (0x8000 to 0x18000 fixed point)
// - Spread Formula (FUN_0048e0b0): (_rand() & 0x1f) angular dispersion multiplier
// - Rocket Ascent (FUN_0048e0b0 param 7): lifespan 0x1cccc (1.8s), upward velocity ~2500 cm/s, trail puffs (FUN_004af220 class 1)
// - Explosion Ring (FUN_0048e0b0 param 9): lifespan 0xe666 (0.9s), radial burst speed 1200 cm/s
// - Falling Sparkles (FUN_0048e0b0 param 8): lifespan 0x60000 (6.0s), gravity drift -250 cm/s^2 (-0x280000)
void ASimCopterMissionSystemActor::UpdateFireworksFX(float DeltaSeconds)
{
	USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this);

	// 1. Advance active rockets in flight. Trigger detonation explosion ring, sparkles, and boom sound AT APEX (t = 1.8s).
	for (int32 Index = ActiveFireworkRockets.Num() - 1; Index >= 0; --Index)
	{
		FSimCopterActiveFireworkRocket& Rocket = ActiveFireworkRockets[Index];
		Rocket.TimeRemaining -= DeltaSeconds;
		if (Rocket.TimeRemaining <= 0.0f)
		{
			if (FireSmokeComponent != nullptr)
			{
				// Apex Burst Ring (FUN_0048e0b0 param 9: 32 particles, 1.2s lifespan, 1500 cm/s radial speed)
				FireSmokeComponent->SpawnRing(
					Rocket.ApexLocation,
					32,
					1500.0f,
					250.0f,
					25.0f,
					Rocket.BurstColor,
					1.2f,
					-200.0f);

				// Apex Falling Embers / Sparkles (FUN_0048e0b0 param 8: 6.0s lifespan, -250 cm/s^2 gravity drift)
				//
				// DIVERGENCE, by request: the embers are not all one colour. Each burst carries a
				// second palette colour and every ember mixes the two by its own amount, with a
				// little brightness jitter on top - so a shell reads as a shower of sparks rather
				// than a solid-coloured ring. The original's fireworks are much plainer; this is a
				// deliberate flourish, not a port.
				for (int32 Spark = 0; Spark < 24; ++Spark)
				{
					const FVector SparkVel(
						FMath::RandRange(-500.0f, 500.0f),
						FMath::RandRange(-500.0f, 500.0f),
						FMath::RandRange(-100.0f, 400.0f));

					FLinearColor SparkColor = FMath::Lerp(
						Rocket.BurstColor,
						Rocket.AccentColor,
						FMath::FRand());
					const float Brightness = FMath::FRandRange(0.75f, 1.35f);
					SparkColor.R *= Brightness;
					SparkColor.G *= Brightness;
					SparkColor.B *= Brightness;

					FireSmokeComponent->SpawnParticle(
						Rocket.ApexLocation,
						SparkVel,
						FMath::FRandRange(13.0f, 23.0f),
						SparkColor,
						FMath::FRandRange(4.5f, 6.5f),
						-250.0f); // SCHOOK: DAT_005d62e0 gravity drift
				}
			}

			// Play 3D explosion / boom sound AT DETONATION (Sound 0x07: BOOM1.WAV)
			if (Audio != nullptr)
			{
				Audio->Play3D(0x07, Rocket.ApexLocation); // SCHOOK: BOOM1 0x07 detonation boom
			}

			ActiveFireworkRockets.RemoveAt(Index);
		}
	}

	// 2. Check launch timer interval
	FireworksTimer += DeltaSeconds;
	if (FireworksTimer < NextFireworksInterval)
	{
		return;
	}
	FireworksTimer = 0.0f;
	NextFireworksInterval = FMath::RandRange(0.5f, 1.5f); // SCHOOK: FUN_0048ed00 timer 0x8000 - 0x18000

	ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
	if (TrafficSystem == nullptr || GetWorld() == nullptr)
	{
		return;
	}

	const FIntPoint AirportOrigin = TrafficSystem->GetAirportOriginTile();
	if (AirportOrigin.X < 0 || AirportOrigin.Y < 0)
	{
		return;
	}

	const int32 OffsetX = FMath::RandRange(0, 3);
	const int32 OffsetY = FMath::RandRange(0, 3);

	FVector GroundLocation = FVector::ZeroVector;
	if (TrafficSystem->TryGetTileCenterWorldLocation(AirportOrigin.X + OffsetX, AirportOrigin.Y + OffsetY, GroundLocation))
	{
		GroundLocation.Z += 20.0f; // Helipad ground level

		const FLinearColor PaletteColors[] = {
			FLinearColor(1.0f, 0.2f, 0.2f, 1.0f),  // Bright Red
			FLinearColor(1.0f, 0.85f, 0.1f, 1.0f), // Gold / Yellow
			FLinearColor(0.2f, 0.6f, 1.0f, 1.0f),  // Electric Blue
			FLinearColor(0.2f, 1.0f, 0.4f, 1.0f),  // Emerald Green
			FLinearColor(1.0f, 0.4f, 0.9f, 1.0f),  // Magenta
			FLinearColor(1.0f, 1.0f, 1.0f, 1.0f)   // Silver White
		};
		// Two colours per shell: the dominant one and an accent the embers mix toward. Picking the
		// accent from the remaining five guarantees they differ, so no burst comes out flat.
		constexpr int32 PaletteCount = UE_ARRAY_COUNT(PaletteColors);
		const int32 BurstIndex = FMath::RandRange(0, PaletteCount - 1);
		const int32 AccentIndex = (BurstIndex + FMath::RandRange(1, PaletteCount - 1)) % PaletteCount;
		const FLinearColor BurstColor = PaletteColors[BurstIndex];
		const FLinearColor AccentColor = PaletteColors[AccentIndex];

		// Rocket launch apex location & Ghidra (_rand() & 0x1f) angular dispersion spread
		const float AscentHeightCm = FMath::RandRange(2500.0f, 4500.0f);
		const float SpreadX = ((FMath::Rand() & 0x1F) - 15.5f) * 14.0f; // SCHOOK: FUN_0048e0b0 lines 107-113
		const float SpreadY = ((FMath::Rand() & 0x1F) - 15.5f) * 14.0f;
		const FVector RocketVelocity(SpreadX, SpreadY, AscentHeightCm / 1.8f);
		const FVector ApexLocation = GroundLocation + FVector(SpreadX * 1.8f, SpreadY * 1.8f, AscentHeightCm);

		// Play launch whistling sound AT LAUNCH (Sound 0x17: TGSHWH.WAV).
		if (Audio != nullptr)
		{
			Audio->Play3D(SimCopterSound::FireworkMortarSound, GroundLocation);
		}

		if (FireSmokeComponent != nullptr)
		{
			// 1. Rocket Tracer Particle (FUN_0048e0b0 param 7: 1.8s ascent tracer)
			FireSmokeComponent->SpawnParticle(
				GroundLocation,
				RocketVelocity,
				24.0f,
				FLinearColor(1.0f, 0.9f, 0.6f, 1.0f),
				1.8f,
				0.0f);

			// 2. Ascent Trail Puffs (FUN_004af220 effect class 1 along ascent vector)
			for (int32 Step = 1; Step <= 4; ++Step)
			{
				const FVector TrailPos = FMath::Lerp(GroundLocation, ApexLocation, Step / 4.0f);
				FireSmokeComponent->SpawnTilePuff(TrailPos, 1);
			}
		}

		// Register in-flight rocket for apex detonation explosion 1.8s later
		FSimCopterActiveFireworkRocket NewRocket;
		NewRocket.ApexLocation = ApexLocation;
		NewRocket.BurstColor = BurstColor;
		NewRocket.AccentColor = AccentColor;
		NewRocket.TimeRemaining = 1.8f;
		ActiveFireworkRockets.Add(NewRocket);
	}
}

// The band's approach, the following, the facing, the animation and the music are all BHAV 444's
// job (see SpawnMarchingBandAtAirport). Nothing steers them from here: rec[3] selects the player at
// four tiles and rec[4] walks to them through MoveStep, which is what applies the tile-class and
// climb rules the old scripted mover bypassed. This only drops references to band members the
// world has reclaimed.
void ASimCopterMissionSystemActor::UpdateMarchingBandApproach(const FVector& PlayerLocation, float DeltaSeconds)
{
	bMarchingBandApproaching = true;
	MarchingBandAgents.RemoveAll([](const TWeakObjectPtr<ASimCopterGroundAgent>& Agent)
	{
		return !Agent.IsValid();
	});
}

void ASimCopterMissionSystemActor::ProcessLevelCompleteLanding(float DeltaTime)
{
	if (SessionMode != ESimCopterMissionSessionMode::CityJobs || !MissionSystem.IsLevelComplete())
	{
		if (bLevelCompletePromptDisplayed)
		{
			bLevelCompletePromptDisplayed = false;
			ClearMissionLogMessage(TEXT("Level Complete! Press Enter to advance to level select."));
		}
		return;
	}

	// 1. SCHOOK: TubaLeader 443 / TubaInit 444 - spawn airport celebration marching band
	SpawnMarchingBandAtAirport();

	// 2. SCHOOK: FireworksInit 0x004916e0 - launch fireworks bursts over airport
	UpdateFireworksFX(DeltaTime);

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (PlayerPawn == nullptr)
	{
		if (bLevelCompletePromptDisplayed)
		{
			bLevelCompletePromptDisplayed = false;
			ClearMissionLogMessage(TEXT("Level Complete! Press Enter to advance to level select."));
		}
		return;
	}

	bool bLandedAtAirport = false;
	ASimCopterHelicopterPawn* PlayerHelicopter = Cast<ASimCopterHelicopterPawn>(PlayerPawn);
	if (PlayerHelicopter != nullptr)
	{
		bLandedAtAirport = PlayerHelicopter->IsStandingOnAirport();
	}
	else
	{
		// Player is on foot or controlling ground pawn: test position against airport 4x4 tile footprint
		ASimCopterTrafficSystemActor* TrafficSystem = ResolveTrafficSystem();
		ASimCity2000CityActor* City = Cast<ASimCity2000CityActor>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ASimCity2000CityActor::StaticClass()));

		if (TrafficSystem != nullptr && City != nullptr)
		{
			const FIntPoint AirportOrigin = TrafficSystem->GetAirportOriginTile();
			float SurfaceZ = 0.0f;
			uint8 TerrainClass = 0xff;
			FIntPoint Tile = FIntPoint::ZeroValue;
			if (AirportOrigin.X >= 0 && AirportOrigin.Y >= 0 &&
				City->TryGetWaterGameplaySurface(PlayerPawn->GetActorLocation(), SurfaceZ, TerrainClass, &Tile))
			{
				bLandedAtAirport = (Tile.X >= AirportOrigin.X && Tile.X <= AirportOrigin.X + 3 &&
					Tile.Y >= AirportOrigin.Y && Tile.Y <= AirportOrigin.Y + 3);
			}
		}
	}

	if (!bLandedAtAirport)
	{
		if (bLevelCompletePromptDisplayed)
		{
			bLevelCompletePromptDisplayed = false;
			ClearMissionLogMessage(TEXT("Level Complete! Press Enter to advance to level select."));
		}
		return;
	}

	// 3. Marching band approaches player
	UpdateMarchingBandApproach(PlayerPawn->GetActorLocation(), DeltaTime);

	// 4. Prompt: "Level Complete! Press Enter to advance to level select."
	// Persistent message with bDestroyOnTimeout = false (stays on HUD until player leaves helipad).
	if (!bLevelCompletePromptDisplayed)
	{
		bLevelCompletePromptDisplayed = true;
		PushMissionLogMessage(
			TEXT("Level Complete! Press Enter to advance to level select."),
			FLinearColor::Green,
			/*bDestroyOnTimeout=*/false);
	}

	// 5. User input check: Enter key ONLY for M&K (or gamepad accept button)
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	const bool bAdvancing = (PC != nullptr && (
		PC->WasInputKeyJustPressed(EKeys::Enter) ||
		PC->WasInputKeyJustPressed(EKeys::Virtual_Gamepad_Accept) ||
		PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom)));

	if (bAdvancing)
	{
		const SimCopterMissions::FSimCopterCareerCity& CurCity = MissionSystem.GetCareerCity();
		MissionSystem.AddCash(CurCity.MoneyEarned);

		const int32 CompletedIndex = MissionSystem.GetCareerCityIndex();

		if (USimCopterSessionSubsystem* Session = GetGameInstance() != nullptr
			? GetGameInstance()->GetSubsystem<USimCopterSessionSubsystem>()
			: nullptr)
		{
			Session->SetCompletedCareerCityIndex(CompletedIndex);
		}

		UGameplayStatics::OpenLevel(this, FName(USimCopterSessionSubsystem::GetMainMenuLevelName()));
	}
}

