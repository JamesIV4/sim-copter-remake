// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Game/SimCopterCareerSubsystem.h"
#include "Game/SimCopterSaveSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Missions/SimCopterMissionSystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterSaveNameTest,
	"SimCopter.SaveGame.Names",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterSaveNameTest::RunTest(const FString& Parameters)
{
	FString Error;
	TestFalse(TEXT("Blank names are refused"), USimCopterSaveSubsystem::IsDisplayNameValid(TEXT("  \t "), Error));
	TestFalse(TEXT("Blank-name refusal explains itself"), Error.IsEmpty());

	const FString Normalized = USimCopterSaveSubsystem::NormalizeDisplayName(
		TEXT("  Sea\tCliff\r\nCareer  "));
	TestEqual(TEXT("Whitespace is collapsed and trimmed"), Normalized, FString(TEXT("Sea Cliff Career")));
	TestTrue(TEXT("A real name is accepted"), USimCopterSaveSubsystem::IsDisplayNameValid(Normalized, Error));

	const FString CareerSlot = USimCopterSaveSubsystem::MakeSlotName(
		ESimCopterSessionKind::Career, Normalized);
	const FString CareerSlotAgain = USimCopterSaveSubsystem::MakeSlotName(
		ESimCopterSessionKind::Career, Normalized);
	const FString UserSlot = USimCopterSaveSubsystem::MakeSlotName(
		ESimCopterSessionKind::User, Normalized);
	TestEqual(TEXT("A name maps to a stable slot"), CareerSlot, CareerSlotAgain);
	TestTrue(TEXT("Career slots carry their kind"), CareerSlot.StartsWith(TEXT("SimCopter_C_")));
	TestTrue(TEXT("User slots carry their kind"), UserSlot.StartsWith(TEXT("SimCopter_U_")));
	TestNotEqual(TEXT("Career and user files cannot collide"), CareerSlot, UserSlot);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterSaveArchiveRoundTripTest,
	"SimCopter.SaveGame.ArchiveRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterSaveArchiveRoundTripTest::RunTest(const FString& Parameters)
{
	USimCopterSaveGame* Source = NewObject<USimCopterSaveGame>();
	Source->Kind = ESimCopterSessionKind::Career;
	Source->DisplayName = TEXT("Sea Cliff Career");
	Source->SavedAtUtc = FDateTime(2026, 7, 31, 12, 34, 56);
	Source->CareerCityIndex = 0;
	Source->Cash = 4321;
	Source->Score = 876;
	Source->SessionElapsedSeconds = 123.5f;
	Source->CityDifficulty = 2;
	Source->CityWeights = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f };
	Source->CityDayOrNight = 1;
	Source->CityPointsNeeded = 900;
	Source->CityMoneyEarned = 250;
	Source->OwnedHelicopterMask = 0x12;
	Source->HelicopterDepreciation = { 0, 10, 20, 30 };
	Source->bHasAircraftState = true;
	Source->ActiveHelicopterTypeIndex = 4;
	Source->CareerEquipmentMask = 0x1f;
	Source->CareerTearGasRounds = 7;
	Source->SelectedToolIndex = 3;
	Source->FuelFraction = 0.625f;
	Source->DamageFraction = 0.25f;

	FSimCopterCareerLogEntry Log;
	Log.Text = TEXT("Fire: Ended, Award: 100 Points, 200 Bucks");
	Log.TypeMask = 1;
	Log.SessionSeconds = 90.0f;
	Log.Kind = ESimCopterCareerLogKind::MissionEnded;
	Source->CareerLog.Add(Log);

	FString Error;
	TestTrue(TEXT("The complete payload validates"),
		Source->IsStructurallyValid(ESimCopterSessionKind::Career, Error));
	TestFalse(TEXT("Career payload is rejected by the user-game opener"),
		Source->IsStructurallyValid(ESimCopterSessionKind::User, Error));

	TArray<uint8> Bytes;
	if (!TestTrue(TEXT("Unreal serializes the save to memory"), UGameplayStatics::SaveGameToMemory(Source, Bytes)))
	{
		return false;
	}
	TestTrue(TEXT("The archive is non-empty"), Bytes.Num() > 0);

	USimCopterSaveGame* Loaded = Cast<USimCopterSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("The archive reloads as the right save class"), Loaded))
	{
		return false;
	}

	TestTrue(TEXT("Reloaded payload validates"),
		Loaded->IsStructurallyValid(ESimCopterSessionKind::Career, Error));
	TestEqual(TEXT("Name survives"), Loaded->DisplayName, Source->DisplayName);
	TestEqual(TEXT("Timestamp survives"), Loaded->SavedAtUtc, Source->SavedAtUtc);
	TestEqual(TEXT("Cash survives"), Loaded->Cash, 4321);
	TestEqual(TEXT("Score survives"), Loaded->Score, 876);
	TestEqual(TEXT("All city weights survive"), Loaded->CityWeights.Num(), 7);
	TestEqual(TEXT("Fleet survives"), Loaded->OwnedHelicopterMask, 0x12);
	TestEqual(TEXT("Log survives"), Loaded->CareerLog.Num(), 1);
	TestEqual(TEXT("Log text survives"), Loaded->CareerLog[0].Text, Log.Text);
	TestEqual(TEXT("Active helicopter survives"), Loaded->ActiveHelicopterTypeIndex, 4);
	TestEqual(TEXT("Equipment survives"), Loaded->CareerEquipmentMask, 0x1f);
	TestEqual(TEXT("Ammo survives"), Loaded->CareerTearGasRounds, 7);
	TestEqual(TEXT("Fuel survives"), Loaded->FuelFraction, 0.625f);
	TestEqual(TEXT("Damage survives"), Loaded->DamageFraction, 0.25f);

	Loaded->FormatVersion = USimCopterSaveGame::CurrentFormatVersion + 1;
	TestFalse(TEXT("A future version is refused"),
		Loaded->IsStructurallyValid(ESimCopterSessionKind::Career, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterSaveRestoreStateTest,
	"SimCopter.SaveGame.RestoreState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterSaveRestoreStateTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterMissions;

	FSimCopterMissionSystem Missions;
	Missions.Initialize(nullptr, 1);
	FSimCopterCareerCity City;
	City.Difficulty = 3;
	City.Weights[0] = 12.0f;
	City.DayOrNight = 1;
	City.PointsNeeded = 1400;
	City.MoneyEarned = 300;
	Missions.RestoreSessionState(775, 2345, City);
	TestEqual(TEXT("Mission score restores"), Missions.GetScore(), 775);
	TestEqual(TEXT("Mission cash restores"), Missions.GetCash(), 2345);
	TestEqual(TEXT("Saved city tuning restores"), Missions.GetCareerCity().PointsNeeded, 1400);
	TestEqual(TEXT("Difficulty cache follows restored city"), Missions.GetDifficultyTier(), 4);

	UGameInstance* GameInstance = NewObject<UGameInstance>();
	USimCopterCareerSubsystem* Career = NewObject<USimCopterCareerSubsystem>(GameInstance);
	TArray<int32> Depreciation{ 100, -20, 300 };
	TArray<FSimCopterCareerLogEntry> Log;
	FSimCopterCareerLogEntry Entry;
	Entry.Text = TEXT("Entered City: Sea Cliff");
	Log.Add(Entry);
	Career->RestoreCareerState(0x12, Depreciation, Log);
	TestEqual(TEXT("Fleet mask restores"), Career->GetOwnedHelicopterMask(), 0x12);
	TestEqual(TEXT("Depreciation restores"), Career->GetHelicopterDepreciation(0), 100);
	TestEqual(TEXT("Negative depreciation clamps"), Career->GetHelicopterDepreciation(1), 0);
	TestEqual(TEXT("Career log restores"), Career->GetLogEntries().Num(), 1);
	TestTrue(TEXT("Restored career is open"), Career->IsCareerOpen());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
