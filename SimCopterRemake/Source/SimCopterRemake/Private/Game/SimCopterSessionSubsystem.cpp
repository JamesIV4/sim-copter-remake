// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/SimCopterSessionSubsystem.h"

#include "Formats/SimCopterOriginalGamePaths.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

void USimCopterSessionSubsystem::RequestCareerCity(int32 InCareerCityIndex)
{
	Kind = ESimCopterSessionKind::Career;
	CareerCityIndex = FMath::Clamp(InCareerCityIndex, 0, 29);
	CityFilePath = ResolveCareerCityFilePath(CareerCityIndex);
}

void USimCopterSessionSubsystem::RequestUserCity(const FString& InCityFilePath)
{
	Kind = ESimCopterSessionKind::User;
	// Career City0 supplies only the shared base record; BeginSession replaces its settings fields
	// with FUN_004080c0's separate mode-1 defaults.
	CareerCityIndex = 0;
	CityFilePath = InCityFilePath;
}

void USimCopterSessionSubsystem::ClearPendingSession()
{
	Kind = ESimCopterSessionKind::None;
	CareerCityIndex = 0;
	CityFilePath.Reset();
	PendingMissionTypeMask = 0;
	bStartFirstMissionImmediately = false;
}

FString USimCopterSessionSubsystem::ResolveCitiesDir()
{
	return SimCopterOriginalGame::ResolveDirectory(TEXT("cities"));
}

FString USimCopterSessionSubsystem::ResolveCareerCityFilePath(int32 CareerCityIndex)
{
	const FString CitiesDir = ResolveCitiesDir();
	if (CitiesDir.IsEmpty())
	{
		return FString();
	}

	return FPaths::Combine(CitiesDir, TEXT("career"), FString::Printf(TEXT("city%d.sc2"), FMath::Clamp(CareerCityIndex, 0, 29)));
}

void USimCopterSessionSubsystem::GetUserCityFilePaths(TArray<FString>& OutPaths)
{
	OutPaths.Reset();

	const FString CitiesDir = ResolveCitiesDir();
	if (CitiesDir.IsEmpty())
	{
		return;
	}

	TArray<FString> FileNames;
	IFileManager::Get().FindFiles(FileNames, *(CitiesDir / TEXT("*.sc2")), true, false);
	FileNames.Sort();

	for (const FString& FileName : FileNames)
	{
		OutPaths.Add(FPaths::Combine(CitiesDir, FileName));
	}
}
