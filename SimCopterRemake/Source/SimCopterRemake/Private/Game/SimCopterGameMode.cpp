// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/SimCopterGameMode.h"

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
	TrafficSystemClass = ASimCopterTrafficSystemActor::StaticClass();
	MissionSystemClass = ASimCopterMissionSystemActor::StaticClass();
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
