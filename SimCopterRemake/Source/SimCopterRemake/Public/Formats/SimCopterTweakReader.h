// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct SIMCOPTERREMAKE_API FSimCopterTweakSection
{
	FString Name;
	TMap<FString, FString> Values;

	bool TryGetValue(const FString& Key, FString& OutValue) const;
	FString GetString(const FString& Key, const FString& DefaultValue = FString()) const;
	bool TryGetFloat(const FString& Key, float& OutValue) const;
	float GetFloat(const FString& Key, float DefaultValue = 0.0f) const;
	bool TryGetInt(const FString& Key, int32& OutValue) const;
	int32 GetInt(const FString& Key, int32 DefaultValue = 0) const;
};

struct SIMCOPTERREMAKE_API FSimCopterTweakFile
{
	TArray<FSimCopterTweakSection> Sections;

	const FSimCopterTweakSection* FindSection(const FString& SectionName) const;
	FSimCopterTweakSection* FindSection(const FString& SectionName);
};

class SIMCOPTERREMAKE_API FSimCopterTweakReader
{
public:
	static bool ParseTweakText(const FString& Text, FSimCopterTweakFile& OutTweakFile, FString& OutError);
	static bool LoadTweakFileFromFile(const FString& FilePath, FSimCopterTweakFile& OutTweakFile, FString& OutError);
};
