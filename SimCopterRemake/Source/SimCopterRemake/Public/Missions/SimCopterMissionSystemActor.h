// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Missions/SimCopterMissionSystem.h"
#include "SimCopterMissionSystemActor.generated.h"

UCLASS()
class SIMCOPTERREMAKE_API ASimCopterMissionSystemActor : public AActor, public SimCopterMissions::ISimCopterMissionWorld
{
	GENERATED_BODY()
	
public:	
	ASimCopterMissionSystemActor();

protected:
	virtual void BeginPlay() override;

public:	
	virtual void Tick(float DeltaTime) override;

	// ~Begin ISimCopterMissionWorld Interface
	virtual int32 GetXbldTileId(int32 TileX, int32 TileY) const override;
	virtual int32 GetBuildingFootprintSize(int32 TileX, int32 TileY) const override;
	virtual bool GetCameraTile(int32& OutTileX, int32& OutTileY) const override;
	virtual bool GetPlayerTile(int32& OutTileX, int32& OutTileY) const override;
	virtual bool IsModalUiActive() const override;

	virtual void OnBuildingFireIgnited(int32 TileX, int32 TileY, int32 EventId) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Audio")
	TMap<int32, USoundBase*> UiSounds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCopter|Audio")
	TMap<int32, USoundBase*> RadioVoices;

	virtual void PlayRadioVoice(int32 VoiceId, int32 Volume) override;
	virtual void PlayUiSound(int32 SoundId) override;

	virtual bool TryActivatePlaneCrash(int32 EventId) override;
	virtual bool TryActivateTrainCrash(int32 EventId) override;
	virtual bool TryActivateBoatRescue(int32 EventId, int32 Timer1616, int32& OutTileX, int32& OutTileY) override;
	virtual bool TryActivateTrainRescue(int32 EventId, int32 Timer1616, int32& OutTileX, int32& OutTileY) override;
	
	virtual bool TryStartTrafficJam(int32 EventId, int32& OutTileX, int32& OutTileY) override;
	virtual void EndTrafficJam(int32 EventId) override;
	virtual bool TryStartCarFire(int32 EventId, int32& OutTileX, int32& OutTileY) override;
	virtual bool TryActivateSpeederCar(int32 EventId, int32 TileX, int32 TileY) override;
	virtual bool TrySpawnMissionPerson(int32 Mode, int32 SubState, int32 TileX, int32 TileY, int32 EventId) override;
	
	// ~End ISimCopterMissionWorld Interface

private:
	SimCopterMissions::FSimCopterMissionSystem MissionSystem;
};
