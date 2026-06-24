// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/SimCopterGameMode.h"

#include "Flight/SimCopterHelicopterPawn.h"
#include "Ground/SimCopterOnFootPawn.h"
#include "Ground/SimCopterTrafficSystemActor.h"
#include "Kismet/GameplayStatics.h"

ASimCopterGameMode::ASimCopterGameMode()
{
	DefaultPawnClass = ASimCopterOnFootPawn::StaticClass();
	TrafficSystemClass = ASimCopterTrafficSystemActor::StaticClass();
}

void ASimCopterGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (!bSpawnTrafficSystem || TrafficSystemClass == nullptr || GetWorld() == nullptr)
	{
		return;
	}

	TArray<AActor*> ExistingTrafficSystems;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), TrafficSystemClass, ExistingTrafficSystems);
	if (ExistingTrafficSystems.Num() > 0)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GetWorld()->SpawnActor<ASimCopterTrafficSystemActor>(TrafficSystemClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
}
