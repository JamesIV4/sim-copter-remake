// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SimCopterCareerSubsystem.generated.h"

// The half of the original's career record the hangar shell reads and writes, plus the mission
// log it prints.
//
// Decoded from SimCopter.exe (Docs/scratchpad/ghidra/heli_tools_models_decode_20260724.md):
//
//   career + 0x40  money        FUN_00407a70 read / FUN_00407a90 add (clamped at 0)
//   career + 0x44  helicopter ownership bitmask, bit = runtime type index
//   career + 0x48  equipment ownership bitmask
//   career + 0x50  score
//   career + 0x54  tear gas rounds
//
//   FUN_0042d840   the shop's purchase path: catalog row -> index permutation, then set the bit
//   FUN_0048b050   helicopter price = *(int*)(&DAT_00504128 + type * 0x5c), i.e. the per-type
//                  block's "+0x44 New Cost", which heli.twk overwrites (FUN_00489e20)
//   FUN_0048b070   trade-in value = price - depreciation, floored at price / 2
//   FUN_0048b1a0   "take delivery": sets the ownership bit and parks the new aircraft on a pad
//
// Money and score already live on the mission system (its session record is the same career
// block), so they are deliberately not duplicated here; this subsystem owns what nothing else
// did - which helicopters are on the books, what they cost, and the log.
//
// It hangs off the game instance rather than an actor so a career survives the travel between
// the front end and the city level, exactly as USimCopterSessionSubsystem does.

// Which original log line an entry was printed from. The values are the resource string ids the
// original formats with (SimCopter.exe STRINGTABLE, English block).
UENUM()
enum class ESimCopterCareerLogKind : uint8
{
	// 534 "Entered City: %s, %s"
	EnteredCity,
	// 536 "%s: Started %s%s"
	MissionStarted,
	// 537 "%s: Ended, Award: %ld Points, %ld Bucks"
	MissionEnded,
	// 540 "%s: %s %ld Bucks"
	CashAward,
	// 541 "%s: %s %ld Points"
	PointsAward,
	// No original line: the shop writing itself into the log so a purchase is accounted for.
	Purchase,
};

USTRUCT()
struct SIMCOPTERREMAKE_API FSimCopterCareerLogEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FString Text;

	// Mission type mask the line belongs to, so the log's "By Type" sort (string 531) has
	// something to sort on. 0 for lines that are not about a mission.
	UPROPERTY()
	int32 TypeMask = 0;

	// Seconds since the session opened. The original stamps each line with the in-game date
	// (strings 500-521, the weekday and month names); the remake has no clock to date them
	// with yet, so the log prints elapsed session time instead.
	UPROPERTY()
	float SessionSeconds = 0.0f;

	UPROPERTY()
	ESimCopterCareerLogKind Kind = ESimCopterCareerLogKind::MissionStarted;
};

UCLASS()
class SIMCOPTERREMAKE_API USimCopterCareerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// The runtime type index of the aircraft a new career starts with. FUN_0042d420 opens the
	// catalog on the row the player owns, and every shipped career starts on the cheapest
	// airframe - catalog row 0, runtime type 4, the Schweizer 300.
	static constexpr int32 StartingHelicopterTypeIndex = 4;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// --- career + 0x44: which airframes are on the books ---

	int32 GetOwnedHelicopterMask() const { return OwnedHelicopterMask; }
	bool OwnsHelicopter(int32 TypeIndex) const;
	void SetHelicopterOwned(int32 TypeIndex, bool bOwned);
	int32 GetOwnedHelicopterCount() const;
	// Lowest owned runtime type index, or INDEX_NONE when the books are empty - what selling the
	// aircraft you are flying has to fall back to.
	int32 FindFirstOwnedHelicopterTypeIndex(int32 ExcludeTypeIndex = INDEX_NONE) const;

	// --- prices ---

	// Reads every heli.twk section's "New Cost" control once and caches it. Safe to call
	// repeatedly; only the first call with a usable root does the work.
	void EnsurePricesLoaded(const FString& OriginalGameRoot);

	// FUN_0048b050. 0 when the tables have not been loaded or the type has no catalog row.
	int32 GetHelicopterPrice(int32 TypeIndex) const;

	// FUN_0048b070: price - depreciation, floored at price / 2. Nothing accrues depreciation in
	// the remake yet, so this currently returns the full price for an undamaged airframe; the
	// floor and the subtraction are reproduced so a depreciation source can be added without
	// touching the shop.
	int32 GetHelicopterTradeInValue(int32 TypeIndex) const;

	int32 GetHelicopterDepreciation(int32 TypeIndex) const;
	void AddHelicopterDepreciation(int32 TypeIndex, int32 Dollars);

	// --- mission log ---

	void AddLogEntry(ESimCopterCareerLogKind Kind, const FString& Text, int32 TypeMask, float SessionSeconds);
	const TArray<FSimCopterCareerLogEntry>& GetLogEntries() const { return LogEntries; }

	// Log capacity. The original's log is a fixed record array; this one just stops the oldest
	// lines growing without bound over a long session.
	static constexpr int32 MaxLogEntries = 256;

	// Clears the log and puts the books back to a new career. Called when a session opens.
	void BeginCareer();

	// True once BeginCareer has run, so a city entered directly (PIE) can seed itself.
	bool IsCareerOpen() const { return bCareerOpen; }

private:
	UPROPERTY()
	int32 OwnedHelicopterMask = 0;

	UPROPERTY()
	TArray<FSimCopterCareerLogEntry> LogEntries;

	UPROPERTY()
	bool bCareerOpen = false;

	// Indexed by runtime type index; empty until EnsurePricesLoaded finds heli.twk.
	UPROPERTY()
	TArray<int32> HelicopterPrices;

	UPROPERTY()
	TArray<int32> HelicopterDepreciation;
};
