// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/SimCopterSaveSubsystem.h"

#include "Flight/SimCopterHelicopterPawn.h"
#include "Flight/SimCopterHelicopterRegistry.h"
#include "Game/SimCopterCareerProgression.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"
#include "Missions/SimCopterMissionSystemActor.h"

namespace
{
constexpr int32 SaveUserIndex = 0;
constexpr int32 CityWeightCount = 7;
constexpr int32 MaxDisplayNameLength = 48;
const TCHAR* const ManagedSlotPrefix = TEXT("SimCopter_");

ASimCopterHelicopterPawn* ResolveCareerHelicopter(const UObject* WorldContextObject)
{
	if (WorldContextObject == nullptr)
	{
		return nullptr;
	}

	if (const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(WorldContextObject, 0))
	{
		if (ASimCopterHelicopterPawn* Possessed = Cast<ASimCopterHelicopterPawn>(PlayerController->GetPawn()))
		{
			return Possessed;
		}
	}

	TArray<AActor*> Helicopters;
	UGameplayStatics::GetAllActorsOfClass(
		WorldContextObject, ASimCopterHelicopterPawn::StaticClass(), Helicopters);
	Helicopters.Sort([](const AActor& Left, const AActor& Right)
	{
		return Left.GetName() < Right.GetName();
	});
	return Helicopters.Num() > 0 ? Cast<ASimCopterHelicopterPawn>(Helicopters[0]) : nullptr;
}
}

bool USimCopterSaveGame::IsStructurallyValid(
	const ESimCopterSessionKind ExpectedKind,
	FString& OutError) const
{
	OutError.Reset();
	if (FormatMagic != GetFormatMagic())
	{
		OutError = TEXT("This is not a SimCopter Remake save.");
		return false;
	}
	if (FormatVersion <= 0 || FormatVersion > CurrentFormatVersion)
	{
		OutError = FString::Printf(
			TEXT("Save version %d is not supported by this build (latest: %d)."),
			FormatVersion,
			CurrentFormatVersion);
		return false;
	}
	if (Kind == ESimCopterSessionKind::None || (ExpectedKind != ESimCopterSessionKind::None && Kind != ExpectedKind))
	{
		OutError = ExpectedKind == ESimCopterSessionKind::Career
			? TEXT("That file is not a career save.")
			: TEXT("That file is not a user-game save.");
		return false;
	}
	if (DisplayName.IsEmpty())
	{
		OutError = TEXT("The save has no name.");
		return false;
	}
	if (CareerCityIndex < 0 || CareerCityIndex >= SimCopterCareerProgression::CityCount)
	{
		OutError = TEXT("The save contains an invalid career city.");
		return false;
	}
	if (Kind == ESimCopterSessionKind::User && UserCityFileName.IsEmpty())
	{
		OutError = TEXT("The user-game save does not identify its SimCity file.");
		return false;
	}
	if (CityWeights.Num() != CityWeightCount)
	{
		OutError = TEXT("The save contains an invalid city-settings record.");
		return false;
	}
	for (const float Weight : CityWeights)
	{
		if (!FMath::IsFinite(Weight))
		{
			OutError = TEXT("The save contains an invalid city-settings record.");
			return false;
		}
	}
	if (Cash < 0 || Score < 0 || !FMath::IsFinite(SessionElapsedSeconds))
	{
		OutError = TEXT("The save contains an invalid career balance or score.");
		return false;
	}
	if (bHasAircraftState &&
		(ActiveHelicopterTypeIndex < 0 ||
		 ActiveHelicopterTypeIndex >= SimCopterHelicopterRegistry::GetDefinitionCount() ||
		 SelectedToolIndex < 0 ||
		 SelectedToolIndex >= static_cast<int32>(ESimCopterHelicopterTool::Count) ||
		 !FMath::IsFinite(FuelFraction) || FuelFraction < 0.0f || FuelFraction > 1.0f ||
		 !FMath::IsFinite(DamageFraction) || DamageFraction < 0.0f || DamageFraction > 1.0f))
	{
		OutError = TEXT("The save contains an invalid aircraft record.");
		return false;
	}
	return true;
}

FString USimCopterSaveGame::GetCityDisplayName() const
{
	return Kind == ESimCopterSessionKind::Career
		? FString(SimCopterCareerProgression::GetCityName(CareerCityIndex))
		: FPaths::GetBaseFilename(UserCityFileName);
}

USimCopterSaveSubsystem* USimCopterSaveSubsystem::Get(const UObject* WorldContextObject)
{
	const UGameInstance* GameInstance = WorldContextObject != nullptr
		? UGameplayStatics::GetGameInstance(WorldContextObject)
		: nullptr;
	return GameInstance != nullptr
		? GameInstance->GetSubsystem<USimCopterSaveSubsystem>()
		: nullptr;
}

void USimCopterSaveSubsystem::BeginNewGame()
{
	CurrentSlotName.Reset();
	CurrentDisplayName.Reset();
	PendingLoadedGame = nullptr;
	bPendingMissionStateApplied = false;
}

FString USimCopterSaveSubsystem::NormalizeDisplayName(const FString& DisplayName)
{
	FString Result = DisplayName;
	Result.TrimStartAndEndInline();

	FString Clean;
	Clean.Reserve(FMath::Min(Result.Len(), MaxDisplayNameLength));
	bool bPreviousWasSpace = false;
	for (const TCHAR Character : Result)
	{
		if (Clean.Len() >= MaxDisplayNameLength)
		{
			break;
		}
		if (FChar::IsWhitespace(Character))
		{
			if (!bPreviousWasSpace && !Clean.IsEmpty())
			{
				Clean.AppendChar(TEXT(' '));
			}
			bPreviousWasSpace = true;
			continue;
		}
		if (FChar::IsControl(Character))
		{
			continue;
		}

		Clean.AppendChar(Character);
		bPreviousWasSpace = false;
	}
	Clean.TrimEndInline();
	return Clean;
}

bool USimCopterSaveSubsystem::IsDisplayNameValid(const FString& DisplayName, FString& OutError)
{
	OutError.Reset();
	const FString Normalized = NormalizeDisplayName(DisplayName);
	if (Normalized.IsEmpty())
	{
		OutError = TEXT("Enter a name for the saved game.");
		return false;
	}
	return true;
}

FString USimCopterSaveSubsystem::MakeSlotName(
	const ESimCopterSessionKind Kind,
	const FString& DisplayName)
{
	const FString Normalized = NormalizeDisplayName(DisplayName);
	FString Safe;
	Safe.Reserve(Normalized.Len());
	for (const TCHAR Character : Normalized)
	{
		Safe.AppendChar(FChar::IsAlnum(Character) ? Character : TEXT('_'));
	}
	while (Safe.Contains(TEXT("__")))
	{
		Safe.ReplaceInline(TEXT("__"), TEXT("_"));
	}
	bool bTrimmedUnderscore = false;
	do
	{
		bTrimmedUnderscore = false;
		Safe.TrimCharInline(TEXT('_'), &bTrimmedUnderscore);
	}
	while (bTrimmedUnderscore);
	if (Safe.IsEmpty())
	{
		Safe = TEXT("Saved_Game");
	}

	const FString LowerName = Normalized.ToLower();
	const uint32 NameCrc = FCrc::StrCrc32(*LowerName);
	const TCHAR KindCode = Kind == ESimCopterSessionKind::Career ? TEXT('C') : TEXT('U');
	return FString::Printf(TEXT("%s%c_%s_%08x"), ManagedSlotPrefix, KindCode, *Safe.Left(32), NameCrc);
}

bool USimCopterSaveSubsystem::IsManagedSlotName(const FString& SlotName)
{
	if (!SlotName.StartsWith(ManagedSlotPrefix, ESearchCase::CaseSensitive))
	{
		return false;
	}
	for (const TCHAR Character : SlotName)
	{
		if (!FChar::IsAlnum(Character) && Character != TEXT('_'))
		{
			return false;
		}
	}
	return true;
}

USimCopterSaveGame* USimCopterSaveSubsystem::ReadSaveSlot(const FString& SlotName)
{
	if (!IsManagedSlotName(SlotName) || !UGameplayStatics::DoesSaveGameExist(SlotName, SaveUserIndex))
	{
		return nullptr;
	}
	return Cast<USimCopterSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, SaveUserIndex));
}

USimCopterSaveGame* USimCopterSaveSubsystem::CaptureCurrentGame(
	const UObject* WorldContextObject,
	const FString& DisplayName,
	FString& OutError) const
{
	OutError.Reset();
	if (WorldContextObject == nullptr)
	{
		OutError = TEXT("There is no active game to save.");
		return nullptr;
	}

	const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObject);
	const USimCopterSessionSubsystem* Session = GameInstance != nullptr
		? GameInstance->GetSubsystem<USimCopterSessionSubsystem>()
		: nullptr;
	if (Session == nullptr || Session->GetSessionKind() == ESimCopterSessionKind::None)
	{
		OutError = TEXT("There is no career or user-game session to save.");
		return nullptr;
	}

	const ASimCopterMissionSystemActor* Missions = Cast<ASimCopterMissionSystemActor>(
		UGameplayStatics::GetActorOfClass(WorldContextObject, ASimCopterMissionSystemActor::StaticClass()));
	if (Missions == nullptr)
	{
		OutError = TEXT("The mission session is not ready yet.");
		return nullptr;
	}

	USimCopterSaveGame* Save = NewObject<USimCopterSaveGame>(GetTransientPackage());
	Save->Kind = Session->GetSessionKind();
	Save->DisplayName = NormalizeDisplayName(DisplayName);
	Save->SavedAtUtc = FDateTime::UtcNow();
	Save->CareerCityIndex = Missions->GetSessionCareerCityIndex();
	Save->UserCityFileName = Save->Kind == ESimCopterSessionKind::User
		? FPaths::GetCleanFilename(Session->GetCityFilePath())
		: FString();
	Save->Cash = Missions->GetSessionCash();
	Save->Score = Missions->GetSessionScore();
	Save->SessionElapsedSeconds = Missions->GetSessionElapsedSeconds();

	const SimCopterMissions::FSimCopterCareerCity& City = Missions->GetSessionCareerCity();
	Save->CityDifficulty = City.Difficulty;
	Save->CityWeights.Append(City.Weights, CityWeightCount);
	Save->CityDayOrNight = City.DayOrNight;
	Save->CityPointsNeeded = City.PointsNeeded;
	Save->CityMoneyEarned = City.MoneyEarned;

	if (const USimCopterCareerSubsystem* Career = GameInstance->GetSubsystem<USimCopterCareerSubsystem>())
	{
		Save->OwnedHelicopterMask = Career->GetOwnedHelicopterMask();
		Save->HelicopterDepreciation = Career->GetHelicopterDepreciationValues();
		Save->CareerLog = Career->GetLogEntries();
	}

	if (const ASimCopterHelicopterPawn* Helicopter = ResolveCareerHelicopter(WorldContextObject))
	{
		const FSimCopterEquipmentState& Equipment = Helicopter->GetEquipmentState();
		Save->bHasAircraftState = true;
		Save->ActiveHelicopterTypeIndex = Helicopter->GetHelicopterTypeIndex();
		Save->CareerEquipmentMask = Equipment.CareerEquipmentMask;
		Save->CareerTearGasRounds = Equipment.CareerTearGasRounds;
		Save->SelectedToolIndex = static_cast<int32>(Helicopter->GetSelectedTool());
		Save->FuelFraction = Helicopter->GetFuelFraction();
		Save->DamageFraction = Helicopter->GetDamageFraction();
	}

	if (!Save->IsStructurallyValid(Save->Kind, OutError))
	{
		return nullptr;
	}
	return Save;
}

bool USimCopterSaveSubsystem::SaveCurrentGame(
	const UObject* WorldContextObject,
	FString& OutError)
{
	if (CurrentSlotName.IsEmpty() || CurrentDisplayName.IsEmpty())
	{
		OutError = TEXT("This game has not been named yet.");
		return false;
	}

	USimCopterSaveGame* Save = CaptureCurrentGame(WorldContextObject, CurrentDisplayName, OutError);
	if (Save == nullptr)
	{
		return false;
	}
	if (!UGameplayStatics::SaveGameToSlot(Save, CurrentSlotName, SaveUserIndex))
	{
		OutError = TEXT("The saved-game file could not be written.");
		return false;
	}
	return true;
}

bool USimCopterSaveSubsystem::SaveCurrentGameAs(
	const UObject* WorldContextObject,
	const FString& DisplayName,
	FString& OutError)
{
	if (!IsDisplayNameValid(DisplayName, OutError))
	{
		return false;
	}

	const FString Normalized = NormalizeDisplayName(DisplayName);
	USimCopterSaveGame* Save = CaptureCurrentGame(WorldContextObject, Normalized, OutError);
	if (Save == nullptr)
	{
		return false;
	}

	const FString SlotName = MakeSlotName(Save->Kind, Normalized);
	if (!UGameplayStatics::SaveGameToSlot(Save, SlotName, SaveUserIndex))
	{
		OutError = TEXT("The saved-game file could not be written.");
		return false;
	}

	CurrentSlotName = SlotName;
	CurrentDisplayName = Normalized;
	return true;
}

bool USimCopterSaveSubsystem::LoadGame(
	const FString& SlotName,
	const ESimCopterSessionKind ExpectedKind,
	FString& OutError)
{
	OutError.Reset();
	USimCopterSaveGame* Save = ReadSaveSlot(SlotName);
	if (Save == nullptr)
	{
		OutError = TEXT("The saved-game file could not be read.");
		return false;
	}
	if (!Save->IsStructurallyValid(ExpectedKind, OutError))
	{
		return false;
	}

	USimCopterSessionSubsystem* Session = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<USimCopterSessionSubsystem>()
		: nullptr;
	if (Session == nullptr)
	{
		OutError = TEXT("The game session service is unavailable.");
		return false;
	}

	if (Save->Kind == ESimCopterSessionKind::Career)
	{
		Session->RequestCareerCity(Save->CareerCityIndex);
	}
	else
	{
		const FString CitiesDir = USimCopterSessionSubsystem::ResolveCitiesDir();
		const FString CityPath = FPaths::Combine(CitiesDir, FPaths::GetCleanFilename(Save->UserCityFileName));
		if (CitiesDir.IsEmpty() || !FPaths::FileExists(CityPath))
		{
			OutError = FString::Printf(
				TEXT("Cannot find the saved SimCity file '%s' in the original game's cities folder."),
				*Save->UserCityFileName);
			return false;
		}
		Session->RequestUserCity(CityPath);
	}

	if (Session->GetCityFilePath().IsEmpty() || !FPaths::FileExists(Session->GetCityFilePath()))
	{
		Session->ClearPendingSession();
		OutError = TEXT("The saved city file is not available in the configured original-game folder.");
		return false;
	}

	PendingLoadedGame = Save;
	bPendingMissionStateApplied = false;
	CurrentSlotName = SlotName;
	CurrentDisplayName = Save->DisplayName;
	return true;
}

void USimCopterSaveSubsystem::GetSaveSummaries(
	const ESimCopterSessionKind Kind,
	TArray<FSimCopterSaveSummary>& OutSaves) const
{
	OutSaves.Reset();

	TArray<FString> Files;
	const FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"));
	IFileManager::Get().FindFiles(Files, *(SaveDir / TEXT("SimCopter_*.sav")), true, false);
	for (const FString& File : Files)
	{
		const FString SlotName = FPaths::GetBaseFilename(File);
		USimCopterSaveGame* Save = ReadSaveSlot(SlotName);
		FString Error;
		if (Save == nullptr || !Save->IsStructurallyValid(Kind, Error))
		{
			continue;
		}

		FSimCopterSaveSummary& Summary = OutSaves.AddDefaulted_GetRef();
		Summary.SlotName = SlotName;
		Summary.DisplayName = Save->DisplayName;
		Summary.Kind = Save->Kind;
		Summary.CityName = Save->GetCityDisplayName();
		Summary.SavedAtUtc = Save->SavedAtUtc;
		Summary.Cash = Save->Cash;
		Summary.Score = Save->Score;
	}

	OutSaves.Sort([](const FSimCopterSaveSummary& Left, const FSimCopterSaveSummary& Right)
	{
		if (Left.SavedAtUtc != Right.SavedAtUtc)
		{
			return Left.SavedAtUtc > Right.SavedAtUtc;
		}
		return Left.DisplayName < Right.DisplayName;
	});
}

FString USimCopterSaveSubsystem::GetSuggestedSaveName(const UObject* WorldContextObject) const
{
	const UGameInstance* GameInstance = WorldContextObject != nullptr
		? UGameplayStatics::GetGameInstance(WorldContextObject)
		: nullptr;
	const USimCopterSessionSubsystem* Session = GameInstance != nullptr
		? GameInstance->GetSubsystem<USimCopterSessionSubsystem>()
		: nullptr;
	if (Session == nullptr)
	{
		return TEXT("Saved Game");
	}
	if (Session->GetSessionKind() == ESimCopterSessionKind::Career)
	{
		return FString::Printf(
			TEXT("Career - %s"),
			SimCopterCareerProgression::GetCityName(Session->GetCareerCityIndex()));
	}
	return FString::Printf(TEXT("User - %s"), *FPaths::GetBaseFilename(Session->GetCityFilePath()));
}

bool USimCopterSaveSubsystem::ApplyPendingMissionAndCareerState(
	ASimCopterMissionSystemActor* MissionActor)
{
	if (PendingLoadedGame == nullptr || MissionActor == nullptr)
	{
		return false;
	}

	SimCopterMissions::FSimCopterCareerCity City = MissionActor->GetSessionCareerCity();
	City.Difficulty = PendingLoadedGame->CityDifficulty;
	for (int32 Index = 0; Index < CityWeightCount; ++Index)
	{
		City.Weights[Index] = PendingLoadedGame->CityWeights[Index];
	}
	City.DayOrNight = PendingLoadedGame->CityDayOrNight;
	City.PointsNeeded = PendingLoadedGame->CityPointsNeeded;
	City.MoneyEarned = PendingLoadedGame->CityMoneyEarned;

	MissionActor->RestoreSavedSessionState(
		PendingLoadedGame->Score,
		PendingLoadedGame->Cash,
		City,
		PendingLoadedGame->SessionElapsedSeconds);

	if (USimCopterCareerSubsystem* Career = GetGameInstance() != nullptr
			? GetGameInstance()->GetSubsystem<USimCopterCareerSubsystem>()
			: nullptr)
	{
		Career->RestoreCareerState(
			PendingLoadedGame->OwnedHelicopterMask,
			PendingLoadedGame->HelicopterDepreciation,
			PendingLoadedGame->CareerLog);
	}

	bPendingMissionStateApplied = true;
	return true;
}

bool USimCopterSaveSubsystem::ApplyPendingAircraftState(UWorld* World)
{
	if (PendingLoadedGame == nullptr || !bPendingMissionStateApplied)
	{
		return false;
	}
	if (!PendingLoadedGame->bHasAircraftState)
	{
		PendingLoadedGame = nullptr;
		bPendingMissionStateApplied = false;
		return true;
	}

	ASimCopterHelicopterPawn* Helicopter = ResolveCareerHelicopter(World);
	if (Helicopter == nullptr)
	{
		return false;
	}

	Helicopter->RestoreSavedCareerState(
		PendingLoadedGame->ActiveHelicopterTypeIndex,
		PendingLoadedGame->CareerEquipmentMask,
		PendingLoadedGame->CareerTearGasRounds,
		PendingLoadedGame->FuelFraction,
		PendingLoadedGame->DamageFraction,
		PendingLoadedGame->SelectedToolIndex);

	PendingLoadedGame = nullptr;
	bPendingMissionStateApplied = false;
	return true;
}
