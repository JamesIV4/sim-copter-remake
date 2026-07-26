// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/SimCopterCareerSubsystem.h"

#include "Flight/SimCopterHelicopterRegistry.h"
#include "Formats/SimCopterTweakReader.h"
#include "Misc/Paths.h"

namespace
{
// heli.twk's controls are label/value pairs; "New Cost ($)" is Ctrl11 in every shipped section,
// but the label is matched rather than the index because the tweak editor can reorder them.
constexpr int32 MaxTweakControls = 16;

bool ReadNewCostControl(const FSimCopterTweakSection& Section, int32& OutDollars)
{
	for (int32 ControlIndex = 0; ControlIndex < MaxTweakControls; ++ControlIndex)
	{
		const FString Label = Section.GetString(FString::Printf(TEXT("Ctrl%d_Label"), ControlIndex));
		if (!Label.StartsWith(TEXT("New Cost"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		float Value = 0.0f;
		if (Section.TryGetFloat(FString::Printf(TEXT("Ctrl%d_Value"), ControlIndex), Value))
		{
			OutDollars = FMath::RoundToInt(Value);
			return true;
		}
	}

	return false;
}
}

void USimCopterCareerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const int32 TypeCount = SimCopterHelicopterRegistry::GetDefinitionCount();
	HelicopterPrices.Init(0, TypeCount);
	HelicopterDepreciation.Init(0, TypeCount);
}

bool USimCopterCareerSubsystem::OwnsHelicopter(const int32 TypeIndex) const
{
	if (TypeIndex < 0 || TypeIndex >= 32)
	{
		return false;
	}
	return (OwnedHelicopterMask & (1 << TypeIndex)) != 0;
}

void USimCopterCareerSubsystem::SetHelicopterOwned(const int32 TypeIndex, const bool bOwned)
{
	if (TypeIndex < 0 || TypeIndex >= 32)
	{
		return;
	}

	// FUN_0048b1a0 sets the bit; FUN_0042d9f0's sale clears it. Selling also throws away whatever
	// depreciation the airframe had accrued, because the next one off the lot is new.
	if (bOwned)
	{
		OwnedHelicopterMask |= (1 << TypeIndex);
	}
	else
	{
		OwnedHelicopterMask &= ~(1 << TypeIndex);
		if (HelicopterDepreciation.IsValidIndex(TypeIndex))
		{
			HelicopterDepreciation[TypeIndex] = 0;
		}
	}
}

int32 USimCopterCareerSubsystem::GetOwnedHelicopterCount() const
{
	return FMath::CountBits(static_cast<uint32>(OwnedHelicopterMask));
}

int32 USimCopterCareerSubsystem::FindFirstOwnedHelicopterTypeIndex(const int32 ExcludeTypeIndex) const
{
	const int32 TypeCount = SimCopterHelicopterRegistry::GetDefinitionCount();
	for (int32 TypeIndex = 0; TypeIndex < TypeCount; ++TypeIndex)
	{
		if (TypeIndex != ExcludeTypeIndex && OwnsHelicopter(TypeIndex))
		{
			return TypeIndex;
		}
	}
	return INDEX_NONE;
}

void USimCopterCareerSubsystem::EnsurePricesLoaded(const FString& OriginalGameRoot)
{
	const int32 TypeCount = SimCopterHelicopterRegistry::GetDefinitionCount();
	if (HelicopterPrices.Num() != TypeCount)
	{
		HelicopterPrices.Init(0, TypeCount);
	}
	if (HelicopterDepreciation.Num() != TypeCount)
	{
		HelicopterDepreciation.Init(0, TypeCount);
	}

	// Every entry non-zero means a previous call already did this.
	const bool bAlreadyLoaded = HelicopterPrices.Num() > 0 &&
		!HelicopterPrices.ContainsByPredicate([](const int32 Price) { return Price <= 0; });
	if (bAlreadyLoaded || OriginalGameRoot.IsEmpty())
	{
		return;
	}

	FSimCopterTweakFile TweakFile;
	FString Error;
	const FString HeliTweakPath = FPaths::Combine(OriginalGameRoot, TEXT("tweak/heli.twk"));
	if (!FSimCopterTweakReader::LoadTweakFileFromFile(HeliTweakPath, TweakFile, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("SimCopter career: could not read helicopter prices - %s"), *Error);
		return;
	}

	for (const FSimCopterHelicopterDefinition& Definition : SimCopterHelicopterRegistry::GetDefinitions())
	{
		const FSimCopterTweakSection* Section = TweakFile.FindSection(Definition.TweakSection);
		int32 Dollars = 0;
		if (Section != nullptr && ReadNewCostControl(*Section, Dollars))
		{
			HelicopterPrices[Definition.InternalTypeIndex] = Dollars;
		}
	}
}

int32 USimCopterCareerSubsystem::GetHelicopterPrice(const int32 TypeIndex) const
{
	return HelicopterPrices.IsValidIndex(TypeIndex) ? HelicopterPrices[TypeIndex] : 0;
}

int32 USimCopterCareerSubsystem::GetHelicopterDepreciation(const int32 TypeIndex) const
{
	return HelicopterDepreciation.IsValidIndex(TypeIndex) ? HelicopterDepreciation[TypeIndex] : 0;
}

void USimCopterCareerSubsystem::AddHelicopterDepreciation(const int32 TypeIndex, const int32 Dollars)
{
	if (HelicopterDepreciation.IsValidIndex(TypeIndex) && Dollars > 0)
	{
		HelicopterDepreciation[TypeIndex] += Dollars;
	}
}

int32 USimCopterCareerSubsystem::GetHelicopterTradeInValue(const int32 TypeIndex) const
{
	// FUN_0048b070.
	const int32 Price = GetHelicopterPrice(TypeIndex);
	return FMath::Max(Price - GetHelicopterDepreciation(TypeIndex), Price / 2);
}

void USimCopterCareerSubsystem::AddLogEntry(
	const ESimCopterCareerLogKind Kind,
	const FString& Text,
	const int32 TypeMask,
	const float SessionSeconds)
{
	if (Text.IsEmpty())
	{
		return;
	}

	FSimCopterCareerLogEntry Entry;
	Entry.Kind = Kind;
	Entry.Text = Text;
	Entry.TypeMask = TypeMask;
	Entry.SessionSeconds = SessionSeconds;
	LogEntries.Add(MoveTemp(Entry));

	if (LogEntries.Num() > MaxLogEntries)
	{
		LogEntries.RemoveAt(0, LogEntries.Num() - MaxLogEntries, EAllowShrinking::No);
	}
}

void USimCopterCareerSubsystem::BeginCareer()
{
	LogEntries.Reset();
	OwnedHelicopterMask = 0;
	SetHelicopterOwned(StartingHelicopterTypeIndex, true);
	for (int32& Value : HelicopterDepreciation)
	{
		Value = 0;
	}
	bCareerOpen = true;
}
