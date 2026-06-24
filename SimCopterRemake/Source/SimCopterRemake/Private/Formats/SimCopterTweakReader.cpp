// Copyright Epic Games, Inc. All Rights Reserved.

#include "Formats/SimCopterTweakReader.h"

#include "Misc/DefaultValueHelper.h"
#include "Misc/FileHelper.h"

namespace
{
FString NormalizeTweakKey(const FString& Key)
{
	return Key.TrimStartAndEnd().ToLower();
}

bool IsCommentOrBlank(const FString& Line)
{
	const FString Trimmed = Line.TrimStartAndEnd();
	return Trimmed.IsEmpty() || Trimmed.StartsWith(TEXT("#")) || Trimmed.StartsWith(TEXT(";")) || Trimmed.StartsWith(TEXT("%"));
}
}

bool FSimCopterTweakSection::TryGetValue(const FString& Key, FString& OutValue) const
{
	if (const FString* Value = Values.Find(NormalizeTweakKey(Key)))
	{
		OutValue = *Value;
		return true;
	}

	return false;
}

FString FSimCopterTweakSection::GetString(const FString& Key, const FString& DefaultValue) const
{
	FString Value;
	return TryGetValue(Key, Value) ? Value : DefaultValue;
}

bool FSimCopterTweakSection::TryGetFloat(const FString& Key, float& OutValue) const
{
	FString Value;
	if (!TryGetValue(Key, Value))
	{
		return false;
	}

	return FDefaultValueHelper::ParseFloat(Value, OutValue);
}

float FSimCopterTweakSection::GetFloat(const FString& Key, float DefaultValue) const
{
	float Value = DefaultValue;
	TryGetFloat(Key, Value);
	return Value;
}

bool FSimCopterTweakSection::TryGetInt(const FString& Key, int32& OutValue) const
{
	FString Value;
	if (!TryGetValue(Key, Value))
	{
		return false;
	}

	return FDefaultValueHelper::ParseInt(Value, OutValue);
}

int32 FSimCopterTweakSection::GetInt(const FString& Key, int32 DefaultValue) const
{
	int32 Value = DefaultValue;
	TryGetInt(Key, Value);
	return Value;
}

const FSimCopterTweakSection* FSimCopterTweakFile::FindSection(const FString& SectionName) const
{
	const FString WantedName = SectionName.TrimStartAndEnd();
	return Sections.FindByPredicate([&WantedName](const FSimCopterTweakSection& Section)
	{
		return Section.Name.Equals(WantedName, ESearchCase::IgnoreCase);
	});
}

FSimCopterTweakSection* FSimCopterTweakFile::FindSection(const FString& SectionName)
{
	const FString WantedName = SectionName.TrimStartAndEnd();
	return Sections.FindByPredicate([&WantedName](const FSimCopterTweakSection& Section)
	{
		return Section.Name.Equals(WantedName, ESearchCase::IgnoreCase);
	});
}

bool FSimCopterTweakReader::ParseTweakText(const FString& Text, FSimCopterTweakFile& OutTweakFile, FString& OutError)
{
	OutTweakFile.Sections.Reset();
	OutError.Reset();

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);

	FSimCopterTweakSection* CurrentSection = nullptr;
	for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
	{
		FString Line = Lines[LineIndex].TrimStartAndEnd();
		Line.RemoveFromStart(TEXT("\ufeff"));
		if (IsCommentOrBlank(Line))
		{
			continue;
		}

		if (Line.StartsWith(TEXT("[")) && Line.EndsWith(TEXT("]")))
		{
			FSimCopterTweakSection& NewSection = OutTweakFile.Sections.AddDefaulted_GetRef();
			NewSection.Name = Line.Mid(1, Line.Len() - 2).TrimStartAndEnd();
			CurrentSection = &NewSection;
			continue;
		}

		FString Key;
		FString Value;
		if (!Line.Split(TEXT("="), &Key, &Value))
		{
			OutError = FString::Printf(TEXT("Invalid tweak line %d: '%s'."), LineIndex + 1, *Lines[LineIndex]);
			return false;
		}

		if (CurrentSection == nullptr)
		{
			FSimCopterTweakSection& NewSection = OutTweakFile.Sections.AddDefaulted_GetRef();
			NewSection.Name = TEXT("");
			CurrentSection = &NewSection;
		}

		CurrentSection->Values.Add(NormalizeTweakKey(Key), Value.TrimStartAndEnd());
	}

	return true;
}

bool FSimCopterTweakReader::LoadTweakFileFromFile(const FString& FilePath, FSimCopterTweakFile& OutTweakFile, FString& OutError)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FilePath))
	{
		OutError = FString::Printf(TEXT("Could not read tweak file '%s'."), *FilePath);
		return false;
	}

	if (!ParseTweakText(Text, OutTweakFile, OutError))
	{
		OutError = FString::Printf(TEXT("%s (%s)"), *OutError, *FilePath);
		return false;
	}

	return true;
}
