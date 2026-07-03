// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Missions/SimCopterMissionSystem.h"
#include "SimCopterMissionSystemActor.generated.h"

class ASimCopterTrafficSystemActor;
class ASimCopterHelicopterPawn;
class SConstraintCanvas;
class SVerticalBox;
class SWidget;

struct FSimCopterMissionLogEntry
{
	FString Text;
	FLinearColor Color = FLinearColor::White;
	float RemainingSeconds = 0.0f;
};

struct FSimCopterMissionWorldMarkerEntry
{
	FVector WorldLocation = FVector::ZeroVector;
	FString Label;
	FString Detail;
	FLinearColor Color = FLinearColor::White;
};

UCLASS()
class SIMCOPTERREMAKE_API ASimCopterMissionSystemActor : public AActor, public SimCopterMissions::ISimCopterMissionWorld
{
	GENERATED_BODY()
	
public:	
	ASimCopterMissionSystemActor();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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
	virtual void OnUiMessage(const SimCopterMissions::FSimCopterMissionUiMessage& Message) override;

	virtual bool TryActivatePlaneCrash(int32 EventId) override;
	virtual bool TryActivateTrainCrash(int32 EventId) override;
	virtual bool TryActivateBoatRescue(int32 EventId, int32 Timer1616, int32& OutTileX, int32& OutTileY) override;
	virtual bool TryActivateTrainRescue(int32 EventId, int32 Timer1616, int32& OutTileX, int32& OutTileY) override;
	
	virtual bool TryStartTrafficJam(int32 EventId, int32& OutTileX, int32& OutTileY) override;
	virtual void EndTrafficJam(int32 EventId) override;
	virtual bool TryStartCarFire(int32 EventId, int32& OutTileX, int32& OutTileY) override;
	virtual bool TryActivateSpeederCar(int32 EventId, int32 TileX, int32 TileY) override;
	virtual bool TrySpawnMissionPerson(int32 Mode, int32 SubState, int32 TileX, int32 TileY, int32 EventId) override;
	void NotifyMedevacVictimBoarded(int32 EventId, int32 Count);
	
	// ~End ISimCopterMissionWorld Interface

private:
	UPROPERTY(EditInstanceOnly, Category = "SimCopter|Traffic")
	TObjectPtr<ASimCopterTrafficSystemActor> SourceTrafficSystem;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic")
	bool bUseActiveTrafficSystem = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|UI")
	bool bShowMissionMessageLog = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|UI", meta = (ClampMin = "1", ClampMax = "12"))
	int32 MaxMessageLogEntries = 6;

	UPROPERTY(EditAnywhere, Category = "SimCopter|UI", meta = (ClampMin = "0.5", ClampMax = "30.0"))
	float MessageLogDurationSeconds = 8.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|UI")
	FVector2D MessageLogScreenPadding = FVector2D(18.0f, 18.0f);

	UPROPERTY(EditAnywhere, Category = "SimCopter|UI")
	bool bShowMissionWorldMarkers = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|UI")
	FVector2D MissionMarkerSize = FVector2D(88.0f, 32.0f);

	UPROPERTY(EditAnywhere, Category = "SimCopter|UI", meta = (ClampMin = "0.0", ClampMax = "2000.0"))
	float MissionMarkerWorldZOffsetCm = 950.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|UI", meta = (ClampMin = "0.0", ClampMax = "128.0"))
	float MissionMarkerEdgePadding = 18.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "50.0"))
	float PassengerPickupRadiusCm = 780.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "50.0"))
	float PassengerDropoffRadiusCm = 820.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "50.0"))
	float PassengerTransferMaxVerticalDeltaCm = 420.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "50.0"))
	float PassengerBoardTouchRadiusCm = 130.0f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "0.05"))
	float PassengerBoardGuidanceSeconds = 0.45f;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "25.0"))
	float MedevacOnFootPickupRadiusCm = 95.0f;

	SimCopterMissions::FSimCopterMissionSystem MissionSystem;
	TArray<FSimCopterMissionLogEntry> MissionMessageLog;
	TSharedPtr<SWidget> MessageLogWidget;
	TSharedPtr<SVerticalBox> MessageLogBox;
	TSharedPtr<SWidget> MissionMarkerWidget;
	TSharedPtr<SConstraintCanvas> MissionMarkerCanvas;
	TMap<int32, int32> MissionPassengersOnboard;

	ASimCopterTrafficSystemActor* ResolveTrafficSystem() const;
	void ProcessPassengerTransfers();
	void GetTransferReadyHelicopters(TArray<ASimCopterHelicopterPawn*>& OutHelicopters) const;
	void EnsureMessageLogWidget();
	void RemoveMessageLogWidget();
	void RefreshMessageLogWidget();
	void PushMissionLogMessage(const FString& Text, const FLinearColor& Color);
	FString FormatMissionUiMessage(const SimCopterMissions::FSimCopterMissionUiMessage& Message, FLinearColor& OutColor) const;
	void EnsureMissionMarkerWidget();
	void RemoveMissionMarkerWidget();
	void RefreshMissionMarkerWidget();
	void BuildMissionWorldMarkers(TArray<FSimCopterMissionWorldMarkerEntry>& OutMarkers) const;
	bool TryMakeMissionMarkerWorldLocation(int32 TileX, int32 TileY, FVector& OutWorldLocation) const;
	bool ProjectMissionMarkerToScreen(const FVector& WorldLocation, FVector2D& OutScreenPosition, bool& bOutClamped) const;
};
