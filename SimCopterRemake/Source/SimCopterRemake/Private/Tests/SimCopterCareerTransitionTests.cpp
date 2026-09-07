// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Flight/SimCopterHelicopterRegistry.h"
#include "Game/SimCopterCareerProgression.h"
#include "Game/SimCopterCareerSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Missions/SimCopterMissionSystem.h"
#include "UObject/Package.h"

// SCHOOK: CareerEnterCity 0x00408210 / CareerNewGame 0x00407f30.
//
// FUN_0044bf70's career-select OK is the whole rule:
//
//   if (app[0xb0] == 0) FUN_00408210(city);   // advancing: adopt the record, zero the score
//   else                FUN_00407f30(city);   // new career: mode 2, $1000, 0 points
//
// FUN_00407f30 writes career + 0x40 = 1000, +0x44 = 0x10 (the Schweizer, runtime type 4),
// +0x48 = 3 (bucket + megaphone), +0x50 = 0 and +0x54 = 0. FUN_00408210 writes exactly one of
// them - `*(careerBase + 0x50) = 0` - so money, fleet, fittings and ammunition all survive the
// move between two cities of one career.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterCareerCityTransitionTest,
	"SimCopter.Career.CityTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCareerCityTransitionTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterMissions;

	// The subsystem insists on a GameInstance outer; none of the paths under test touch it.
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	USimCopterCareerSubsystem* Career = NewObject<USimCopterCareerSubsystem>(GameInstance);

	const int32 SchweizerType = USimCopterCareerSubsystem::StartingHelicopterTypeIndex;
	const int32 SecondType = SchweizerType == 0 ? 1 : 0;

	// --- FUN_00407f30: a new career resets the whole block ---
	{
		FSimCopterMissionSystem Missions;
		Missions.BeginSession();
		TestEqual(TEXT("A new career opens on $1000"),
			Missions.GetCash(), FSimCopterMissionSystem::SessionStartingCash);
		TestEqual(TEXT("A new career opens on no points"), Missions.GetScore(), 0);

		Career->SetHelicopterOwned(SecondType, true);
		Career->AddLogEntry(ESimCopterCareerLogKind::Purchase, TEXT("stale"), 0, 0.0f);
		Career->BeginCareer();

		TestEqual(TEXT("A new career owns only the Schweizer"),
			Career->GetOwnedHelicopterMask(), 1 << SchweizerType);
		TestEqual(TEXT("A new career starts with an empty log"), Career->GetLogEntries().Num(), 0);
		TestTrue(TEXT("A new career is open"), Career->IsCareerOpen());
		TestFalse(TEXT("A new career carries nothing over"), Career->HasPendingCityTransfer());
	}

	// --- FUN_00408210: an advancement keeps everything but the score ---
	{
		FSimCopterMissionSystem Missions;
		Missions.BeginSession();
		Missions.AddCash(6500);   // $7500 in the bank when the level finished
		Missions.AddScore(4000);  // the points that completed the city

		Career->BeginCareer();
		Career->SetHelicopterOwned(SecondType, true);
		Career->AddHelicopterDepreciation(SecondType, 900);
		Career->AddLogEntry(ESimCopterCareerLogKind::MissionEnded, TEXT("Fire: Ended"), 0, 12.0f);

		FSimCopterCareerCityTransfer Transfer;
		Transfer.Cash = Missions.GetCash();
		Transfer.ActiveHelicopterTypeIndex = SecondType;
		Transfer.CareerEquipmentMask = SimCopterHelicopterRegistry::AllCareerEquipmentBits;
		Transfer.CareerTearGasRounds = 7;
		Career->SetPendingCityTransfer(Transfer);

		TestTrue(TEXT("A completed city hands an advancement over"), Career->HasPendingCityTransfer());

		// What the next city's BeginSession does with it.
		FSimCopterMissionSystem NextCity;
		NextCity.ContinueSession(Career->GetPendingCityTransfer().Cash);
		Career->ContinueCareerIntoNextCity();

		TestEqual(TEXT("The money crosses the city boundary"), NextCity.GetCash(), 7500);
		TestEqual(TEXT("Only the score is cleared"), NextCity.GetScore(), 0);
		TestEqual(TEXT("The fleet crosses the city boundary"),
			Career->GetOwnedHelicopterMask(), (1 << SchweizerType) | (1 << SecondType));
		TestEqual(TEXT("The log crosses the city boundary"), Career->GetLogEntries().Num(), 1);

		// FUN_00484790 re-places every owned airframe and writes heli[0xcd] = 0, so the accrual
		// FUN_0048b070 subtracts from the trade-in is gone by the time the new city is playable.
		TestEqual(TEXT("Depreciation is written off by the re-placement"),
			Career->GetHelicopterDepreciation(SecondType), 0);

		// And the aircraft half, as the game mode reads it back off the pad.
		const FSimCopterCareerCityTransfer& Pending = Career->GetPendingCityTransfer();
		TestEqual(TEXT("The airframe being flown crosses"), Pending.ActiveHelicopterTypeIndex, SecondType);
		TestEqual(TEXT("The fittings cross"),
			Pending.CareerEquipmentMask, SimCopterHelicopterRegistry::AllCareerEquipmentBits);
		TestEqual(TEXT("The tear gas magazine crosses"), Pending.CareerTearGasRounds, 7);

		Career->ClearPendingCityTransfer();
		TestFalse(TEXT("Applying the advancement consumes it"), Career->HasPendingCityTransfer());
	}

	// A career cannot advance into a negative balance: FUN_00407a90 clamps the money at zero and
	// so does the transfer, so a city finished in debt still opens the next one on nothing.
	{
		FSimCopterCareerCityTransfer Broke;
		Broke.Cash = -250;
		Career->SetPendingCityTransfer(Broke);
		TestEqual(TEXT("Carried money is clamped at zero"),
			Career->GetPendingCityTransfer().Cash, 0);

		FSimCopterMissionSystem NextCity;
		NextCity.ContinueSession(Career->GetPendingCityTransfer().Cash);
		TestEqual(TEXT("The next city opens on nothing, not $1000"), NextCity.GetCash(), 0);
		Career->ClearPendingCityTransfer();
	}

	return true;
}

// The end of the ladder is not an advancement. City 29 (Metropolis) is the only record whose
// successor trio is all -1, so finishing it leaves nothing to continue into and the front end
// falls back to FUN_00457c90's new-career trio {0, 1, 2}.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterCareerFinalCityTest,
	"SimCopter.Career.FinalCityEndsTheLadder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterCareerFinalCityTest::RunTest(const FString& Parameters)
{
	TArray<int32> Successors;
	SimCopterCareerProgression::GetSuccessors(29, Successors);
	TestEqual(TEXT("The final city offers nothing to advance into"), Successors.Num(), 0);
	TestEqual(TEXT("The final city is the Final Level"), SimCopterCareerProgression::GetLevel(29), 11);

	// Every other city has at least one, so the empty case is the ladder ending and not a hole in
	// the transcribed table.
	for (int32 CityIndex = 0; CityIndex < SimCopterCareerProgression::CityCount - 1; ++CityIndex)
	{
		TArray<int32> Choices;
		SimCopterCareerProgression::GetSuccessors(CityIndex, Choices);
		TestTrue(
			*FString::Printf(TEXT("City %d can be advanced out of"), CityIndex),
			Choices.Num() > 0);
	}

	TArray<int32> NewCareer;
	SimCopterCareerProgression::GetNewCareerChoices(NewCareer);
	TestEqual(TEXT("A new career is offered three cities"), NewCareer.Num(), 3);

	return true;
}
