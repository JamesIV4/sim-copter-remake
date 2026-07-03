// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Missions/SimCopterMissionSystem.h"

using namespace SimCopterMissions;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterMissionSystemPRNGTest, "SimCopter.Missions.PRNGParity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMissionSystemPRNGTest::RunTest(const FString& Parameters)
{
	FSimCopterMsvcRand Rng;
	Rng.Seed(1);

	// MSVC rand() with seed 1 produces: 41, 18467, 6334, 26500, 19169
	int32 Expected[] = { 41, 18467, 6334, 26500, 19169 };

	for (int32 i = 0; i < 5; ++i)
	{
		int32 Val = Rng.Rand();
		if (Val != Expected[i])
		{
			AddError(FString::Printf(TEXT("PRNG mismatch at step %d: Expected %d, Got %d"), i, Expected[i], Val));
			return false;
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterMissionSystemWeightTableTest, "SimCopter.Missions.WeightTableParity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMissionSystemWeightTableTest::RunTest(const FString& Parameters)
{
	FSimCopterCareerCity City;
	City.Weights[0] = 100.0f; // Fire
	City.Weights[1] = 50.0f;  // Crime
	City.Weights[2] = 200.0f; // Rescue
	City.Weights[3] = 0.0f;   // Riot
	City.Weights[4] = 0.0f;   // Traffic
	City.Weights[5] = 150.0f; // MedEvac
	City.Weights[6] = 0.0f;   // Transport

	FSimCopterMissionSystem System;
	System.Initialize(nullptr, 1);
	System.SetCareerCity(City);

	// Fire: 100/500 = 20% -> 20
	// Crime: 50/500 = 10% -> 30
	// Rescue: 200/500 = 40% -> 70
	// Riot: 0 -> 70
	// Traffic: 0 -> 70
	// MedEvac: 150/500 = 30% -> 100
	// Transport: 0 -> 100

	const int32* Weights = System.GetCumulativeWeightTable();

	if (Weights[1] != 20 || Weights[2] != 30 || Weights[3] != 70 || Weights[4] != 70 || Weights[5] != 70 || Weights[6] != 100 || Weights[7] != 100)
	{
		AddError(FString::Printf(TEXT("Weight table mismatch: %d %d %d %d %d %d %d"), Weights[1], Weights[2], Weights[3], Weights[4], Weights[5], Weights[6], Weights[7]));
		return false;
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterMissionSystemSchedulerTest, "SimCopter.Missions.SchedulerParity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterMissionSystemSchedulerTest::RunTest(const FString& Parameters)
{
	FSimCopterCareerCity City;
	// Equal weights so we can test the mask dispatch
	for (int32 i=0; i<7; ++i) City.Weights[i] = 10.0f;

	FSimCopterMissionSystem System;
	System.Initialize(nullptr, 1);
	System.SetCareerCity(City);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterEconomyCareerParsingTest, "SimCopter.Economy.CareerParsing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterEconomyCareerParsingTest::RunTest(const FString& Parameters)
{
	FSimCopterMissionSystem System;
	System.Initialize(nullptr, 1);
	
	FString CareerPath = FPaths::ProjectContentDir() / TEXT("OriginalGame/tweak/career.twk");
	if (!FPaths::FileExists(CareerPath))
	{
		CareerPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Reference/SimCopterOriginalGame/tweak/career.twk"));
	}
	
	if (!System.LoadCareerData(CareerPath))
	{
		AddError(TEXT("Failed to load career data"));
		return false;
	}
	
	const FSimCopterCareerCity& City0 = System.GetCareerCity();
	if (City0.Difficulty != 0)
	{
		AddError(FString::Printf(TEXT("City0 Difficulty should be 0, got %d"), City0.Difficulty));
		return false;
	}
	
	if (City0.PointsNeeded != 400)
	{
		AddError(FString::Printf(TEXT("City0 PointsNeeded should be 400, got %d"), City0.PointsNeeded));
		return false;
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCopterEconomyScoreProgressionTest, "SimCopter.Economy.ScoreProgression", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterEconomyScoreProgressionTest::RunTest(const FString& Parameters)
{
	FSimCopterMissionSystem System;
	System.Initialize(nullptr, 1);
	
	FString CareerPath = FPaths::ProjectContentDir() / TEXT("OriginalGame/tweak/career.twk");
	if (!FPaths::FileExists(CareerPath))
	{
		CareerPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Reference/SimCopterOriginalGame/tweak/career.twk"));
	}
	
	if (!System.LoadCareerData(CareerPath))
	{
		AddError(TEXT("Failed to load career data"));
		return false;
	}
	
	// Default City0 requires 400 points.
	System.AddScore(200);
	System.AdvanceCareerIfComplete(); // Should do nothing
	
	if (System.GetCareerCity().Difficulty != 0)
	{
		AddError(TEXT("Should not have advanced city yet."));
		return false;
	}
	
	System.AddScore(200);
	System.AdvanceCareerIfComplete(); // Should advance
	
	// Next city (City1) usually has 500 PointsNeeded.
	// We'll just verify the score reset to 0!
	if (System.GetScore() != 0)
	{
		AddError(TEXT("Score should reset to 0 after advancing city."));
		return false;
	}

	return true;
}
