// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Game/SimCopterCareerSubsystem.h"
#include "Game/SimCopterSessionSubsystem.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SimCopterSaveSubsystem.generated.h"

class ASimCopterMissionSystemActor;

// Versioned remake save payload.
//
// SCHOOK: SaveGame 0x004200e0 writes CPTR plus a CRER/USER header, CFILE, CINF/UINF, CSET,
// BOMB and CSUM chunks. The currently ported state represented by those chunks is captured here:
// session/city, career or user record, city settings and the career-owned aircraft. The original
// BOMB chunk also owns live city objects and missions; those transient systems restart when this
// payload is loaded until they have their own deterministic serializer.
UCLASS()
class SIMCOPTERREMAKE_API USimCopterSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr int32 CurrentFormatVersion = 2;
	static const TCHAR* GetFormatMagic() { return TEXT("SimCopterRemakeSave"); }

	UPROPERTY()
	FString FormatMagic = GetFormatMagic();

	UPROPERTY()
	int32 FormatVersion = CurrentFormatVersion;

	UPROPERTY()
	ESimCopterSessionKind Kind = ESimCopterSessionKind::None;

	UPROPERTY()
	FString DisplayName;

	UPROPERTY()
	FDateTime SavedAtUtc;

	// CFILE + the mode-specific city selection.
	UPROPERTY()
	int32 CareerCityIndex = 0;

	// A clean file name, not an absolute installation path, so moving the repo does not break a
	// user-game save. Career games derive city<N>.sc2 from CareerCityIndex instead.
	UPROPERTY()
	FString UserCityFileName;

	// CINF/UINF career record.
	UPROPERTY()
	int32 Cash = 0;

	UPROPERTY()
	int32 Score = 0;

	UPROPERTY()
	float SessionElapsedSeconds = 0.0f;

	// CSET live city tuning.
	UPROPERTY()
	int32 CityDifficulty = 0;

	UPROPERTY()
	TArray<float> CityWeights;

	UPROPERTY()
	int32 CityDayOrNight = 0;

	UPROPERTY()
	int32 CityPointsNeeded = 0;

	UPROPERTY()
	int32 CityMoneyEarned = 0;

	// Fleet, inventory and log fields from the career block.
	UPROPERTY()
	int32 OwnedHelicopterMask = 0;

	UPROPERTY()
	TArray<int32> HelicopterDepreciation;

	UPROPERTY()
	TArray<FSimCopterCareerLogEntry> CareerLog;

	UPROPERTY()
	bool bHasAircraftState = false;

	UPROPERTY()
	int32 ActiveHelicopterTypeIndex = 0;

	UPROPERTY()
	int32 CareerEquipmentMask = 0;

	UPROPERTY()
	int32 CareerTearGasRounds = 0;

	UPROPERTY()
	int32 SelectedToolIndex = 0;

	UPROPERTY()
	float FuelFraction = 1.0f;

	UPROPERTY()
	float DamageFraction = 0.0f;

	// Version 2: pointer-free BOMB runtime-owner payloads. Each owner versions its own internal
	// byte stream so malformed or future state is rejected instead of half-applied.
	UPROPERTY()
	bool bHasRuntimeWorldState = false;

	UPROPERTY()
	TArray<uint8> MissionRuntimeState;

	UPROPERTY()
	TArray<uint8> TrafficRuntimeState;

	UPROPERTY()
	TArray<uint8> AmbientVehicleRuntimeState;

	UPROPERTY()
	TArray<uint8> AircraftRuntimeState;

	UPROPERTY()
	TArray<FIntPoint> DemolishedBuildingOrigins;

	UPROPERTY()
	bool bPlayerWasInHelicopter = false;

	UPROPERTY()
	bool bHasOnFootTransform = false;

	UPROPERTY()
	FTransform OnFootTransform = FTransform::Identity;

	UPROPERTY()
	TArray<uint8> OnFootRuntimeState;

	bool IsStructurallyValid(ESimCopterSessionKind ExpectedKind, FString& OutError) const;
	FString GetCityDisplayName() const;
};

struct SIMCOPTERREMAKE_API FSimCopterSaveSummary
{
	FString SlotName;
	FString DisplayName;
	ESimCopterSessionKind Kind = ESimCopterSessionKind::None;
	FString CityName;
	FDateTime SavedAtUtc;
	int32 Cash = 0;
	int32 Score = 0;
};

// Owns named save slots across the front-end/city level travel. The active slot is the original's
// DAT_00519740 named-save path: Save overwrites it; a new session with no name routes Save to
// Save As. Files use Unreal's versioned SaveGame archive under Saved/SaveGames and are deliberately
// not presented as byte-compatible .scc/.scu files.
UCLASS()
class SIMCOPTERREMAKE_API USimCopterSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static USimCopterSaveSubsystem* Get(const UObject* WorldContextObject);

	bool HasCurrentSave() const { return !CurrentSlotName.IsEmpty(); }
	const FString& GetCurrentSlotName() const { return CurrentSlotName; }
	const FString& GetCurrentDisplayName() const { return CurrentDisplayName; }

	// Starts a genuinely new career/user game rather than a loaded slot.
	void BeginNewGame();

	bool SaveCurrentGame(const UObject* WorldContextObject, FString& OutError);
	bool SaveCurrentGameAs(const UObject* WorldContextObject, const FString& DisplayName, FString& OutError);
	bool LoadGame(const FString& SlotName, ESimCopterSessionKind ExpectedKind, FString& OutError);

	void GetSaveSummaries(ESimCopterSessionKind Kind, TArray<FSimCopterSaveSummary>& OutSaves) const;
	FString GetSuggestedSaveName(const UObject* WorldContextObject) const;

	// Called by ASimCopterGameMode on the two sides of airport placement.
	bool ApplyPendingMissionAndCareerState(ASimCopterMissionSystemActor* MissionActor);
	bool ApplyPendingAircraftState(UWorld* World);
	bool HasPendingLoad() const { return PendingLoadedGame != nullptr; }

	// Pure helpers are public so save-name/version behavior has headless automation coverage.
	static FString NormalizeDisplayName(const FString& DisplayName);
	static bool IsDisplayNameValid(const FString& DisplayName, FString& OutError);
	static FString MakeSlotName(ESimCopterSessionKind Kind, const FString& DisplayName);

private:
	UPROPERTY()
	TObjectPtr<USimCopterSaveGame> PendingLoadedGame;

	FString CurrentSlotName;
	FString CurrentDisplayName;
	bool bPendingMissionStateApplied = false;

	USimCopterSaveGame* CaptureCurrentGame(const UObject* WorldContextObject, const FString& DisplayName, FString& OutError) const;
	static USimCopterSaveGame* ReadSaveSlot(const FString& SlotName);
	static bool IsManagedSlotName(const FString& SlotName);
};
