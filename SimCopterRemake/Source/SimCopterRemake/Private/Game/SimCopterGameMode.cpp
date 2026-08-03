// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/SimCopterGameMode.h"

#include "City/SimCity2000CityActor.h"
#include "City/SimCopterAirport.h"
#include "City/SimCopterDayNightCycleActor.h"
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
	DayNightCycleClass = ASimCopterDayNightCycleActor::StaticClass();
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

	// Day/night cycle - spawn before the session is applied so the starting time can be set.
	if (bSpawnDayNightCycle && DayNightCycleClass != nullptr)
	{
		TArray<AActor*> ExistingCycles;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), DayNightCycleClass, ExistingCycles);
		if (ExistingCycles.Num() == 0)
		{
			DayNightCycleActor = GetWorld()->SpawnActor<ASimCopterDayNightCycleActor>(DayNightCycleClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		}
		else
		{
			DayNightCycleActor = Cast<ASimCopterDayNightCycleActor>(ExistingCycles[0]);
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

	Actor->StartCityJobsSession(
		Session->GetCareerCityIndex(),
		Session->ShouldStartFirstMissionImmediately());

	if (USimCopterSaveSubsystem* Saves = USimCopterSaveSubsystem::Get(this); Saves != nullptr)
	{
		Saves->ApplyPendingMissionAndCareerState(Actor);
	}

	if (const int32 TypeMask = Session->GetPendingMissionTypeMask())
	{
		Actor->StartMissionNow(TypeMask);
	}

	// Set the day/night starting time from the career city's DayOrNight flag.
	ApplyDayNightStartingTime();
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

ASimCopterDayNightCycleActor* ASimCopterGameMode::ResolveDayNightCycleActor()
{
	if (!DayNightCycleActor.IsValid())
	{
		DayNightCycleActor = Cast<ASimCopterDayNightCycleActor>(
			UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterDayNightCycleActor::StaticClass()));
	}

	return DayNightCycleActor.Get();
}

void ASimCopterGameMode::ApplyDayNightStartingTime()
{
	ASimCopterDayNightCycleActor* Cycle = ResolveDayNightCycleActor();
	if (Cycle == nullptr)
	{
		return;
	}

	// Read the career city's DayOrNight flag from the mission system.
	ASimCopterMissionSystemActor* MissionActor = ResolveMissionSystemActor();
	if (MissionActor == nullptr)
	{
		return;
	}

	const int32 DayOrNight = MissionActor->GetSessionCareerCity().DayOrNight;

	// DayOrNight: 1 = day, 0 = night (from career.twk Ctrl8, verified in FUN_0043c540).
	// Map to a starting time on the 0..1 clock:
	//   Day  (1) → 0.35  (mid-morning, ~8:30 AM feel: pleasant daylight)
	//   Night(0) → 0.85  (early night, ~9 PM feel: dark, city lights visible)
	const float StartTime = (DayOrNight != 0) ? 0.35f : 0.85f;
	Cycle->SetTimeOfDay(StartTime);

	UE_LOG(LogTemp, Display, TEXT("SimCopter day/night: starting at TimeOfDay=%.2f (%s)"),
		StartTime, (DayOrNight != 0) ? TEXT("day") : TEXT("night"));
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

void ASimCopterGameMode::SimSetTime(float NormalizedTime)
{
	if (ASimCopterDayNightCycleActor* Cycle = ResolveDayNightCycleActor())
	{
		Cycle->SetTimeOfDay(FMath::Clamp(NormalizedTime, 0.0f, 0.9999f));
		UE_LOG(LogTemp, Display, TEXT("SimSetTime: TimeOfDay set to %.3f (pitch %.1f°)"),
			Cycle->GetTimeOfDay(), Cycle->GetSunPitchDegrees());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("SimSetTime: no day/night cycle actor in this map."));
	}
}

void ASimCopterGameMode::SimPauseTime()
{
	if (ASimCopterDayNightCycleActor* Cycle = ResolveDayNightCycleActor())
	{
		const bool bNewState = !Cycle->IsCycleEnabled();
		Cycle->SetCycleEnabled(bNewState);
		UE_LOG(LogTemp, Display, TEXT("SimPauseTime: cycle %s (TimeOfDay=%.3f)"),
			bNewState ? TEXT("resumed") : TEXT("paused"), Cycle->GetTimeOfDay());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("SimPauseTime: no day/night cycle actor in this map."));
	}
}
