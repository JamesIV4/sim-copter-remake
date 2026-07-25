// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SimCopterSessionSubsystem.generated.h"

// Which kind of game the player started from the main menu. These are the original's two session
// kinds, selected by `DAT_00518d50`: a career (FUN_00407f30, mode 2) plays the shipped
// cities\career\city<N>.sc2 with that city's career.twk record, and a user game (FUN_004080c0,
// mode 1) plays any SimCity 2000 city with career City0's tuning as its default.
// See Docs/scratchpad/ghidra/session_modes_and_menu_20260724.md.
UENUM()
enum class ESimCopterSessionKind : uint8
{
	None,
	Career,
	User,
};

// Carries the main menu's choice across the level load into the city level.
//
// The menu runs in its own front-end map, so the choice cannot live on an actor: this subsystem
// belongs to the game instance and therefore outlives the travel. The city actor reads
// CityFilePath when it builds, and the gameplay game mode reads the rest to open the session on
// the mission system.
UCLASS()
class SIMCOPTERREMAKE_API USimCopterSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// --- what the menu requests ---

	// Career city 0..29: plays cities/career/city<N>.sc2 with that city's difficulty and weights.
	void RequestCareerCity(int32 CareerCityIndex);

	// Any SimCity 2000 city file, the original's "New User Game". Tuning defaults to career City0,
	// as FUN_004080c0 does.
	void RequestUserCity(const FString& CityFilePath);

	// Optional extra (no original equivalent): create one mission of this type as soon as the city
	// is up, instead of waiting out the scheduler's countdown. 0 = none.
	void SetPendingMissionTypeMask(int32 TypeMask) { PendingMissionTypeMask = TypeMask; }

	// Optional extra: roll the city's first scheduled mission immediately rather than after the
	// original's 180 second opening countdown (DAT_00505fb4).
	void SetStartFirstMissionImmediately(bool bValue) { bStartFirstMissionImmediately = bValue; }

	void ClearPendingSession();

	// --- what the city level reads ---

	bool HasPendingSession() const { return Kind != ESimCopterSessionKind::None; }
	ESimCopterSessionKind GetSessionKind() const { return Kind; }
	int32 GetCareerCityIndex() const { return CareerCityIndex; }
	const FString& GetCityFilePath() const { return CityFilePath; }
	int32 GetPendingMissionTypeMask() const { return PendingMissionTypeMask; }
	bool ShouldStartFirstMissionImmediately() const { return bStartFirstMissionImmediately; }

	// --- city file discovery ---

	// `Reference/SimCopterOriginalGame/cities`, or an empty string when the original game folder is
	// not present.
	static FString ResolveCitiesDir();

	// cities/career/city<N>.sc2 - the binding the original builds from career record +0x40 plus
	// ".sc2" resolved against search-path class 7 ("cities\career\").
	static FString ResolveCareerCityFilePath(int32 CareerCityIndex);

	// Every .sc2 directly under cities/ (the shipped user cities), sorted by name.
	static void GetUserCityFilePaths(TArray<FString>& OutPaths);

	// The front-end map the main menu lives in, and the city level a session travels to.
	static const TCHAR* GetMainMenuLevelName() { return TEXT("/Game/MainMenu"); }
	static const TCHAR* GetCityLevelName() { return TEXT("/Game/CityRender"); }

private:
	ESimCopterSessionKind Kind = ESimCopterSessionKind::None;
	int32 CareerCityIndex = 0;
	FString CityFilePath;
	int32 PendingMissionTypeMask = 0;
	bool bStartFirstMissionImmediately = false;
};
