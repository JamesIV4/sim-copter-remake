// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Input/Reply.h"
#include "Missions/SimCopterMissionSystem.h"
#include "SimCopterMissionSystemActor.generated.h"

class ASimCopterTrafficSystemActor;
class ASimCopterHelicopterPawn;
class ASimCopterGroundAgent;
class USimCopterFireRenderComponent;
class USimCopterParticleFXComponent;
enum class ESimCopterMissionPassengerKind : uint8;
class UMaterialInterface;
class USoundBase;
class USoundWave;
class SConstraintCanvas;
class STextBlock;
class SVerticalBox;
class SWidget;

// One in-progress medevac unload at a hospital: an EMT ferries patients out of a landed helicopter
// to a doorway "into" the hospital, one at a time, until the helicopter is empty.
struct FSimCopterMedevacHandoff
{
	int32 EventId = INDEX_NONE;
	TWeakObjectPtr<ASimCopterHelicopterPawn> Helicopter;
	TWeakObjectPtr<ASimCopterGroundAgent> Emt;
	TWeakObjectPtr<ASimCopterGroundAgent> CarriedPatient;
	TWeakObjectPtr<AActor> Doorway;
	FVector HeliDoorLocation = FVector::ZeroVector;
	FVector DoorwayLocation = FVector::ZeroVector;
	// 0 = EMT walking to the helicopter to collect a patient; 1 = carrying one to the doorway.
	uint8 Phase = 0;
};

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
	virtual bool TryResolveTransportSpawnTile(int32 OriginX, int32 OriginY, int32& OutTileX, int32& OutTileY) override;
	virtual bool TrySpawnMissionPerson(int32 Mode, int32 SubState, int32 TileX, int32 TileY, int32 EventId) override;
	void NotifyMedevacVictimBoarded(int32 EventId, int32 Count);
	void NotifyPassengerDroppedFromHelicopter(int32 EventId, ESimCopterMissionPassengerKind Kind, int32 Count);
	bool CreatePlayerCausedMedevacForVictim(ASimCopterGroundAgent* Victim);
	bool ConvertDroppedTransportPassengerToMedevac(ASimCopterGroundAgent* Victim, int32 SourceTransportEventId);

	// Megaphone: clear the nearest in-range traffic jam from the given (helicopter) location.
	// Returns true if a jam was cleared. Called by the helicopter pawn's megaphone key.
	bool TryUseMegaphone(const FVector& FromWorldLocation);

	// Debug: force-spawn a building fire (or car fire) near the camera so fire visuals and the
	// bucket douse can be exercised without waiting for the scheduler. Invoked via the helicopter
	// pawn's `SimForceFire` / `SimForceCarFire` console commands (the player pawn routes Exec).
	void SimForceFire();
	void SimForceCarFire();

	// Water bucket: dump water at the given world location - extinguishes nearby fires (via the
	// mission system's DouseAt) and spawns the visible water splash/spray. Called by the
	// helicopter pawn while the bucket is dumping. Returns the number of tiles that had fire in
	// range (0 if the dump hit no fire).
	int32 DumpWaterAt(const FVector& BucketWorldLocation);

	// ~End ISimCopterMissionWorld Interface

private:
	UPROPERTY(EditInstanceOnly, Category = "SimCopter|Traffic")
	TObjectPtr<ASimCopterTrafficSystemActor> SourceTrafficSystem;

	UPROPERTY(EditAnywhere, Category = "SimCopter|Traffic")
	bool bUseActiveTrafficSystem = true;

	UPROPERTY(EditAnywhere, Category = "SimCopter|UI")
	bool bShowMissionMessageLog = true;

	// Debug: show on-screen buttons that force-spawn a fire / car-fire mission even when the mission
	// pool is full (bypasses the cap). Handy for testing the fire visuals.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Debug")
	bool bShowDebugFireButtons = true;

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

	// How close the helicopter (with medevac patients aboard) must be to the hospital drop-off tile
	// before the EMT comes out to unload it.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "100.0"))
	float MedevacHospitalHandoffRadiusCm = 1500.0f;

	// How close the EMT must get to the helicopter door / the doorway to act on it.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "25.0"))
	float MedevacEmtReachRadiusCm = 130.0f;

	// Distance from the helicopter to place the hospital doorway the EMT delivers patients to.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "100.0"))
	float MedevacDoorwayDistanceCm = 420.0f;

	// Optional dark material for the hospital doorway box; a plain box is used when unset.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions")
	TObjectPtr<UMaterialInterface> HospitalDoorwayMaterial;

	// Megaphone range (horizontal): the helicopter must be within this of a traffic jam for the
	// megaphone prompt to show and the jam to be clearable. (Original flavor: ~300 m.)
	UPROPERTY(EditAnywhere, Category = "SimCopter|Missions", meta = (ClampMin = "100.0"))
	float MegaphoneRangeCm = 7500.0f;

	// Auto-detect the original game's sound folder on BeginPlay and load the mission/megaphone
	// voice lines from it (so the sound maps below don't have to be filled in by hand).
	UPROPERTY(EditAnywhere, Category = "SimCopter|Audio")
	bool bAutoLoadOriginalSounds = true;

	// Megaphone voice lines (auto-loaded from the original "MG_*" files); one is played at random
	// when the megaphone is used.
	UPROPERTY(VisibleInstanceOnly, Category = "SimCopter|Audio")
	TArray<TObjectPtr<USoundBase>> MegaphoneVoices;

	SimCopterMissions::FSimCopterMissionSystem MissionSystem;

	// Renders the cloned FIREPTS fire/smoke marker effects for every active building flame; driven
	// each tick from MissionSystem.GetFlames().
	UPROPERTY(Transient)
	TObjectPtr<USimCopterFireRenderComponent> FireRenderComponent;

	// Material for fire/smoke point sprites (defaults to M_SimCopterParticleFXSoft).
	UPROPERTY(EditAnywhere, Category = "SimCopter|Fire")
	TObjectPtr<UMaterialInterface> FlameMaterial;

	// Rising smoke + embers above each fire from the typed effect pool.
	UPROPERTY(Transient)
	TObjectPtr<USimCopterParticleFXComponent> FireSmokeComponent;

	// How close the bucket water must be to a burning car to put it out.
	UPROPERTY(EditAnywhere, Category = "SimCopter|Fire", meta = (ClampMin = "50.0"))
	float CarDouseRadiusCm = 600.0f;

	// Cached resolved city actor (for the rendered-surface trace that seats flames on rooftops)
	// and the original game root (for loading the flame GEO meshes once).
	TWeakObjectPtr<AActor> ResolvedCityActor;
	bool bFireAssetsInitialized = false;

	// Build the per-frame flame draw list from the mission system and push it to the fire
	// component. Converts each flame's tile to a rooftop-traced world point + growth/flicker.
	void UpdateFireVisuals(float DeltaSeconds);
	// Emit rising smoke + fire embers above a burning point (origin = flame base, Scale = flame size).
	void SpawnFirePlume(const FVector& FlameBaseWorld, float Scale, float DeltaSeconds);
	FString ResolveOriginalGameRootDir() const;
	// Trace the rendered surface (building roof / terrain) at a world XY; returns the top Z.
	bool TraceSurfaceTopZ(const FVector& WorldXY, float& OutTopZ) const;

	TArray<FSimCopterMissionLogEntry> MissionMessageLog;
	TSharedPtr<SWidget> MessageLogWidget;
	TSharedPtr<SVerticalBox> MessageLogBox;
	TSharedPtr<SWidget> MissionMarkerWidget;
	TSharedPtr<SConstraintCanvas> MissionMarkerCanvas;
	TMap<int32, int32> MissionPassengersOnboard;
	TArray<FSimCopterMedevacHandoff> MedevacHandoffs;

	TSharedPtr<SWidget> MegaphonePromptWidget;
	TSharedPtr<STextBlock> MegaphonePromptText;
	bool bMegaphonePromptVisible = false;

	// On-screen debug buttons (force fire / force car fire).
	TSharedPtr<SWidget> DebugButtonsWidget;
	void EnsureDebugButtonsWidget();
	void RemoveDebugButtonsWidget();
	FReply OnDebugForceFireClicked();
	FReply OnDebugForceCarFireClicked();

	// Megaphone / jam clearing.
	bool FindNearestClearableJam(const FVector& FromWorldLocation, int32& OutEventId, FVector& OutJamWorldLocation) const;
	void UpdateMegaphonePrompt();
	void EnsureMegaphonePromptWidget();
	void RemoveMegaphonePromptWidget();

	// Sound auto-setup.
	void SetupMissionSounds();
	FString ResolveOriginalSoundDir() const;
	USoundWave* LoadOriginalVoice(const FString& SoundDir, const FString& BaseName) const;

	ASimCopterTrafficSystemActor* ResolveTrafficSystem() const;
	void ProcessPassengerTransfers();
	// Runs the EMT patient-unload sequence at hospitals for landed helicopters carrying medevac
	// patients (called each Tick, after ProcessPassengerTransfers).
	void ProcessMedevacHospitalHandoffs(float DeltaSeconds);
	FSimCopterMedevacHandoff* FindMedevacHandoff(int32 EventId);
	void BeginMedevacHandoff(int32 EventId, ASimCopterHelicopterPawn* Helicopter, const FVector& HospitalCenter);
	// Returns false when the handoff is finished/aborted and should be cleaned up.
	bool AdvanceMedevacHandoff(FSimCopterMedevacHandoff& Handoff, float DeltaSeconds);
	void EndMedevacHandoff(FSimCopterMedevacHandoff& Handoff);
	void DeliverMedevacDirectly(int32 EventId, ASimCopterHelicopterPawn* Helicopter);
	AActor* SpawnHospitalDoorway(const FVector& CenterLocation, const FRotator& Facing);
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
