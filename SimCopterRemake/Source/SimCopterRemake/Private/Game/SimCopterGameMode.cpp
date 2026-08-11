// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/SimCopterGameMode.h"

#include "City/SimCity2000CityActor.h"
#include "City/SimCopterAirport.h"
#include "City/SimCopterHangar.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Game/SimCopterPlayerController.h"
#include "Game/SimCopterSaveSubsystem.h"
#include "Game/SimCopterSessionSubsystem.h"
#include "Ground/SimCopterOnFootPawn.h"
#include "Ground/SimCopterTrafficSystemActor.h"
#include "Kismet/GameplayStatics.h"
#include "Missions/SimCopterMissionSystem.h"
#include "UI/SimCopterMissionCatalog.h"

#include "Missions/SimCopterMissionSystemActor.h"

ASimCopterGameMode::ASimCopterGameMode()
{
	DefaultPawnClass = ASimCopterOnFootPawn::StaticClass();
	// Owns the in-game Settings screen (playmenu.bmp, control 0x7d3) and the pause that goes with it.
	PlayerControllerClass = ASimCopterPlayerController::StaticClass();
	TrafficSystemClass = ASimCopterTrafficSystemActor::StaticClass();
	MissionSystemClass = ASimCopterMissionSystemActor::StaticClass();
	HangarClass = ASimCopterHangar::StaticClass();
}

void ASimCopterGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (GetWorld() == nullptr)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (bSpawnTrafficSystem && TrafficSystemClass != nullptr)
	{
		TArray<AActor*> ExistingTrafficSystems;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), TrafficSystemClass, ExistingTrafficSystems);
		if (ExistingTrafficSystems.Num() == 0)
		{
			GetWorld()->SpawnActor<ASimCopterTrafficSystemActor>(TrafficSystemClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		}
	}

	if (bSpawnMissionSystem && MissionSystemClass != nullptr)
	{
		TArray<AActor*> ExistingMissionSystems;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), MissionSystemClass, ExistingMissionSystems);
		if (ExistingMissionSystems.Num() == 0)
		{
			MissionSystemActor = GetWorld()->SpawnActor<ASimCopterMissionSystemActor>(MissionSystemClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		}
		else
		{
			MissionSystemActor = Cast<ASimCopterMissionSystemActor>(ExistingMissionSystems[0]);
		}
	}

	ApplyPendingSession();

	GetWorldTimerManager().SetTimerForNextTick(this, &ASimCopterGameMode::PlaceSessionOnAirportPads);
}

void ASimCopterGameMode::PlaceSessionOnAirportPads()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	ASimCopterTrafficSystemActor* Traffic =
		Cast<ASimCopterTrafficSystemActor>(UGameplayStatics::GetActorOfClass(World, ASimCopterTrafficSystemActor::StaticClass()));
	if (Traffic == nullptr)
	{
		return;
	}

	TArray<AActor*> HelicopterActors;
	UGameplayStatics::GetAllActorsOfClass(World, ASimCopterHelicopterPawn::StaticClass(), HelicopterActors);
	if (HelicopterActors.Num() == 0)
	{
		return;
	}
	// GetAllActorsOfClass has no defined order; the original walks its helicopter table in a
	// fixed one, so sort to keep the assignment stable between runs of the same map.
	HelicopterActors.Sort([](const AActor& Left, const AActor& Right)
	{
		return Left.GetName() < Right.GetName();
	});

	// FUN_0047a240 asks FUN_0048b000 for a free pad once per helicopter, and each placement
	// occupies the pad it lands on, so the next helicopter sees one fewer. Nothing else is
	// parked on the pads at city entry, so the first helicopter gets pad 0.
	TBitArray<> PadTaken(false, SimCopterAirport::PadCount);
	auto IsPadTaken = [&PadTaken](int32 PadIndex) { return PadTaken[PadIndex]; };

	ASimCopterHelicopterPawn* PlayerHelicopter = nullptr;
	FVector PlayerHelicopterPad = FVector::ZeroVector;

	for (AActor* Actor : HelicopterActors)
	{
		ASimCopterHelicopterPawn* Helicopter = Cast<ASimCopterHelicopterPawn>(Actor);
		if (Helicopter == nullptr)
		{
			continue;
		}

		const int32 PadIndex = SimCopterAirport::FindFreePadIndex(IsPadTaken, IsPadTaken);
		FVector PadWorld = FVector::ZeroVector;
		if (PadIndex == INDEX_NONE || !Traffic->TryGetAirportPadWorldLocation(PadIndex, PadWorld))
		{
			UE_LOG(LogTemp, Warning, TEXT("SimCopter airport: no free helipad for %s."), *Helicopter->GetName());
			continue;
		}
		PadTaken[PadIndex] = true;

		// FUN_00484790 clears the helicopter's orientation outright; the pads have no facing.
		Helicopter->PlaceOnHelipad(PadWorld, 0.0f);

		if (PlayerHelicopter == nullptr)
		{
			PlayerHelicopter = Helicopter;
			PlayerHelicopterPad = PadWorld;
		}

		UE_LOG(LogTemp, Display, TEXT("SimCopter airport: %s parked on pad %d at %s."),
			*Helicopter->GetName(), PadIndex, *PadWorld.ToCompactString());
	}

	if (PlayerHelicopter == nullptr)
	{
		return;
	}

	// The original starts the session in the helicopter. This port keeps its on-foot start, so
	// the player is stood on the pad beside the aircraft instead - close enough to walk to it,
	// far enough not to trip the auto-board radius before they have taken a look around.
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	APawn* PlayerPawn = PlayerController != nullptr ? PlayerController->GetPawn() : nullptr;
	if (PlayerPawn == nullptr || PlayerPawn == PlayerHelicopter)
	{
		// No on-foot pawn to stand anywhere, but the hangar still belongs beside the airport;
		// face it at the helicopter's pad.
		PlaceHangar(Traffic, PlayerHelicopterPad);
		if (USimCopterSaveSubsystem* Saves = USimCopterSaveSubsystem::Get(this); Saves != nullptr)
		{
			Saves->ApplyPendingAircraftState(World);
		}
		return;
	}

	const float TileSizeCm = Traffic->GetCityActor() != nullptr ? Traffic->GetCityActor()->GetTileSize() : 400.0f;
	const FVector StandLocation =
		PlayerHelicopterPad +
		FVector(-TileSizeCm * 0.75f, 0.0f, PlayerPawn->GetSimpleCollisionHalfHeight() + 4.0f);
	PlayerPawn->TeleportTo(StandLocation, FRotator(0.0f, 0.0f, 0.0f), /*bIsATest=*/false, /*bNoCheck=*/true);

	PlaceHangar(Traffic, StandLocation);

	// This is deliberately last. The city-entry pad pass, default on-foot placement and hangar
	// demolition all happen first; only then may version-2 BOMB state put the aircraft/player,
	// mission people, fires and mutable city objects back on their saved frame.
	if (USimCopterSaveSubsystem* Saves = USimCopterSaveSubsystem::Get(this); Saves != nullptr)
	{
		Saves->ApplyPendingAircraftState(World);
	}
}

void ASimCopterGameMode::PlaceHangar(ASimCopterTrafficSystemActor* Traffic, const FVector& PlayerStandLocation)
{
	UWorld* World = GetWorld();
	if (!bSpawnHangar || World == nullptr || HangarClass == nullptr || Traffic == nullptr)
	{
		return;
	}

	// A hangar placed in the level by hand wins; otherwise one is spawned here.
	ASimCopterHangar* Hangar = Cast<ASimCopterHangar>(UGameplayStatics::GetActorOfClass(World, HangarClass));
	if (Hangar == nullptr)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Hangar = World->SpawnActor<ASimCopterHangar>(HangarClass, PlayerStandLocation, FRotator::ZeroRotator, SpawnParams);
	}

	if (Hangar == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("SimCopter hangar: could not be spawned."));
		return;
	}

	if (!Hangar->PlaceAtAirport(Traffic, PlayerStandLocation))
	{
		UE_LOG(LogTemp, Warning, TEXT("SimCopter hangar: could not be placed at the airport; leaving it where it spawned."));
	}
}

void ASimCopterGameMode::ApplyPendingSession()
{
	USimCopterSessionSubsystem* Session = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<USimCopterSessionSubsystem>()
		: nullptr;

	if (Session == nullptr || !Session->HasPendingSession())
	{
		// Entered without the main menu: the mission actor opens its own default session on its
		// first tick, exactly as it did before the front end existed.
		return;
	}

	ASimCopterMissionSystemActor* Actor = ResolveMissionSystemActor();
	if (Actor == nullptr)
	{
		return;
	}

	// Hold first so nothing can spawn between here and the session opening below.
	Actor->HoldSessionForMenu();

	UE_LOG(LogTemp, Display, TEXT("SimCopter game mode: %s session, city file '%s'"),
		Session->GetSessionKind() == ESimCopterSessionKind::Career ? TEXT("career") : TEXT("user"),
		*Session->GetCityFilePath());

	if (Session->GetSessionKind() == ESimCopterSessionKind::Career)
	{
		Actor->StartCityJobsSession(
			Session->GetCareerCityIndex(),
			Session->ShouldStartFirstMissionImmediately());
	}
	else
	{
		Actor->StartUserCitySession(
			/*TuningCityIndex=*/0,
			Session->ShouldStartFirstMissionImmediately());
	}

	if (USimCopterSaveSubsystem* Saves = USimCopterSaveSubsystem::Get(this); Saves != nullptr)
	{
		Saves->ApplyPendingMissionAndCareerState(Actor);
	}

	if (const int32 TypeMask = Session->GetPendingMissionTypeMask())
	{
		Actor->StartMissionNow(TypeMask);
	}
}

ASimCopterMissionSystemActor* ASimCopterGameMode::ResolveMissionSystemActor()
{
	if (!MissionSystemActor.IsValid())
	{
		MissionSystemActor = Cast<ASimCopterMissionSystemActor>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass()));
	}

	if (!MissionSystemActor.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("SimCopter session: no mission system actor in this map."));
		return nullptr;
	}

	return MissionSystemActor.Get();
}

void ASimCopterGameMode::SimMainMenu()
{
	if (USimCopterSessionSubsystem* Session = GetGameInstance() != nullptr
			? GetGameInstance()->GetSubsystem<USimCopterSessionSubsystem>()
			: nullptr)
	{
		Session->ClearPendingSession();
	}

	UGameplayStatics::OpenLevel(this, FName(USimCopterSessionSubsystem::GetMainMenuLevelName()));
}

void ASimCopterGameMode::SimFreeRoam(int32 CareerCityIndex)
{
	if (ASimCopterMissionSystemActor* Actor = ResolveMissionSystemActor())
	{
		Actor->StartFreeRoamSession(CareerCityIndex);
	}
}

void ASimCopterGameMode::SimCityJobs(int32 CareerCityIndex)
{
	if (ASimCopterMissionSystemActor* Actor = ResolveMissionSystemActor())
	{
		Actor->StartCityJobsSession(CareerCityIndex, /*bFirstJobImmediately=*/true);
	}
}

void ASimCopterGameMode::SimLoadMission(int32 MissionIndex, int32 CareerCityIndex)
{
	const TArrayView<const FSimCopterMissionCatalogEntry> Missions = GetSimCopterMissionCatalog();

	if (!Missions.IsValidIndex(MissionIndex))
	{
		UE_LOG(LogTemp, Display, TEXT("SimLoadMission <index> [cityIndex] - loadable missions:"));
		for (int32 Index = 0; Index < Missions.Num(); ++Index)
		{
			UE_LOG(LogTemp, Display, TEXT("  %2d  %-14s mask 0x%-6x %s bucket%s"),
				Index,
				SimCopterMissions::FSimCopterMissionSystem::GetTypeDisplayName(Missions[Index].TypeMask),
				Missions[Index].TypeMask,
				Missions[Index].Bucket,
				Missions[Index].bWorldHookPorted ? TEXT("") : TEXT(" (world hook not ported)"));
		}
		return;
	}

	if (ASimCopterMissionSystemActor* Actor = ResolveMissionSystemActor())
	{
		if (Actor->StartSingleMissionSession(CareerCityIndex, Missions[MissionIndex].TypeMask) == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning, TEXT("SimLoadMission: %s could not be placed near the camera."),
				SimCopterMissions::FSimCopterMissionSystem::GetTypeDisplayName(Missions[MissionIndex].TypeMask));
		}
	}
}

void ASimCopterGameMode::SimArsonFirebomb(const int32 Count, const bool bBurnOutNow)
{
	ASimCopterMissionSystemActor* Actor = ResolveMissionSystemActor();
	if (Actor == nullptr)
	{
		return;
	}

	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (PlayerPawn == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("SimArsonFirebomb: no player pawn to throw from."));
		return;
	}

	// The pool is thirty slots (DAT_005d6880) and a full one refuses the throw, exactly as
	// FUN_0048e0b0 does, so asking for more than that is not an error - it just stops early.
	const int32 Requested = FMath::Max(1, Count);
	const int32 Before = Actor->GetBurningDebrisCount();
	for (int32 Index = 0; Index < Requested; ++Index)
	{
		Actor->ThrowArsonistFirebomb(PlayerPawn->GetActorLocation());
		if (bBurnOutNow)
		{
			// One step past the 60-second life takes every live slot straight to its ignition roll.
			Actor->DebugAdvanceBurningDebris(61.0f);
		}
	}

	UE_LOG(LogTemp, Display,
		TEXT("SimArsonFirebomb: threw %d, %d still burning (was %d). Burn-out results are logged as "
			 "they happen."),
		Requested,
		Actor->GetBurningDebrisCount(),
		Before);
}
