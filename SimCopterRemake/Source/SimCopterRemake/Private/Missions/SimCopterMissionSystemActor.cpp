// Copyright Epic Games, Inc. All Rights Reserved.

#include "Missions/SimCopterMissionSystemActor.h"
#include "Ground/SimCopterTrafficSystemActor.h"
#include "Kismet/GameplayStatics.h"

ASimCopterMissionSystemActor::ASimCopterMissionSystemActor()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ASimCopterMissionSystemActor::BeginPlay()
{
	Super::BeginPlay();
	
	// Assuming 0 for random seed for parity tests if we want, but normally a real seed.
	MissionSystem.Initialize(this, 12345);
	
	FString CareerPath = FPaths::ProjectContentDir() / TEXT("OriginalGame/tweak/career.twk");
	if (!FPaths::FileExists(CareerPath))
	{
		CareerPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Reference/SimCopterOriginalGame/tweak/career.twk"));
	}
	MissionSystem.LoadCareerData(CareerPath);
}

void ASimCopterMissionSystemActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	MissionSystem.Tick(DeltaTime);
}

int32 ASimCopterMissionSystemActor::GetXbldTileId(int32 TileX, int32 TileY) const
{
	return 0;
}

int32 ASimCopterMissionSystemActor::GetBuildingFootprintSize(int32 TileX, int32 TileY) const
{
	return 1;
}

bool ASimCopterMissionSystemActor::GetCameraTile(int32& OutTileX, int32& OutTileY) const
{
	OutTileX = 64;
	OutTileY = 64;
	return true;
}

bool ASimCopterMissionSystemActor::GetPlayerTile(int32& OutTileX, int32& OutTileY) const
{
	OutTileX = 64;
	OutTileY = 64;
	return true;
}

bool ASimCopterMissionSystemActor::IsModalUiActive() const
{
	return false;
}

void ASimCopterMissionSystemActor::OnBuildingFireIgnited(int32 TileX, int32 TileY, int32 EventId)
{
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
	AActor* TrafficActor = UGameplayStatics::GetActorOfClass(this, ASimCopterTrafficSystemActor::StaticClass());
	if (ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(TrafficActor))
	{
		return TrafficSystem->TryStartTrafficJam(EventId, OutTileX, OutTileY);
	}
	return false;
}

void ASimCopterMissionSystemActor::EndTrafficJam(int32 EventId)
{
	AActor* TrafficActor = UGameplayStatics::GetActorOfClass(this, ASimCopterTrafficSystemActor::StaticClass());
	if (ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(TrafficActor))
	{
		TrafficSystem->EndTrafficJam(EventId);
	}
}



bool ASimCopterMissionSystemActor::TryStartCarFire(int32 EventId, int32& OutTileX, int32& OutTileY)
{
	AActor* TrafficActor = UGameplayStatics::GetActorOfClass(this, ASimCopterTrafficSystemActor::StaticClass());
	if (ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(TrafficActor))
	{
		return TrafficSystem->TryStartCarFire(EventId, OutTileX, OutTileY);
	}
	return false;
}

bool ASimCopterMissionSystemActor::TrySpawnMissionPerson(int32 Mode, int32 SubState, int32 TileX, int32 TileY, int32 EventId)
{
	AActor* TrafficActor = UGameplayStatics::GetActorOfClass(this, ASimCopterTrafficSystemActor::StaticClass());
	if (ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(TrafficActor))
	{
		return TrafficSystem->TrySpawnMissionPerson(Mode, SubState, TileX, TileY, EventId);
	}
	return false;
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
