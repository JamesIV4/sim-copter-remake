// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/SimCopterGameMode.h"

#include "Flight/SimCopterHelicopterPawn.h"
#include "Ground/SimCopterOnFootPawn.h"
#include "Ground/SimCopterTrafficSystemActor.h"
#include "Kismet/GameplayStatics.h"

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
			GetWorld()->SpawnActor<ASimCopterMissionSystemActor>(MissionSystemClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		}
	}
}
