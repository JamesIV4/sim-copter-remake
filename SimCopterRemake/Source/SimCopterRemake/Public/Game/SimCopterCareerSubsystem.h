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

// The career block as it crosses the travel between two cities of the same career.
//
// SCHOOK: CareerEnterCity 0x00408210 vs CareerNewGame 0x00407f30. The career-select page's OK
// (FUN_0044bf70, control 0x7d7) branches on the "new career" flag `app+0xb0`: a brand new career
// runs FUN_00407f30, which writes $1000, mask 0x10 (the Schweizer), equipment 3 and 0 tear gas
// rounds; an *advancement* runs FUN_00408210, which adopts the next city's record and clears
// exactly one field - the score at career + 0x50. Money (+0x40), the fleet (+0x44), the fittings
// (+0x48) and the tear-gas magazine (+0x54) are untouched.
//
// The original can leave those in static memory. The remake travels through /Game/MainMenu and
// back, which destroys the mission system actor that holds the cash and the helicopter pawn that
// holds the fittings, so the fields ride across on the game instance in here.
USTRUCT()
struct SIMCOPTERREMAKE_API FSimCopterCareerCityTransfer
{
	GENERATED_BODY()

	// False until a completed career city hands one over; a new game never sets it.
	UPROPERTY()
	bool bValid = false;

	// career + 0x40. The end-of-level award has already been paid in.
	UPROPERTY()
	int32 Cash = 0;

	// Which of the owned airframes the player was flying. The original parks every owned
	// helicopter on its own pad (FUN_0047a240); the remake models one, so the active one comes
	// across and the rest stay on the books through the owned mask. Defaults to the Schweizer -
	// FUN_00407f30's mask 0x10 - which is USimCopterCareerSubsystem::StartingHelicopterTypeIndex,
	// declared below and asserted equal in the .cpp.
	UPROPERTY()
	int32 ActiveHelicopterTypeIndex = 4;

	// career + 0x48 and career + 0x54.
	UPROPERTY()
	int32 CareerEquipmentMask = 0;

	UPROPERTY()
	int32 CareerTearGasRounds = 0;
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
	const TArray<int32>& GetHelicopterDepreciationValues() const { return HelicopterDepreciation; }

	// --- mission log ---

	void AddLogEntry(ESimCopterCareerLogKind Kind, const FString& Text, int32 TypeMask, float SessionSeconds);
	const TArray<FSimCopterCareerLogEntry>& GetLogEntries() const { return LogEntries; }

	// Log capacity. The original's log is a fixed record array; this one just stops the oldest
	// lines growing without bound over a long session.
	static constexpr int32 MaxLogEntries = 256;

	// FUN_00407f30. Clears the log and puts the books back to a new career. Called when a session
	// opens that is NOT a career advancing into its next city.
	void BeginCareer();

	// FUN_00408210. Entering the next city of a career keeps the fleet and the log; only the
	// depreciation goes, because FUN_0047a240 re-places every owned airframe through
	// FUN_00484790, which writes heli[0xcd] = 0 (and with it a full tank and full hit points).
	void ContinueCareerIntoNextCity();

	// --- the half of the career block that lives on actors the level travel destroys ---

	// Handed over by the mission actor when a completed career city advances; consumed by the
	// next city's session open (cash, fleet) and by the game mode once the aircraft is on its pad
	// (airframe, fittings, ammunition).
	void SetPendingCityTransfer(const FSimCopterCareerCityTransfer& Transfer);
	bool HasPendingCityTransfer() const { return PendingCityTransfer.bValid; }
	const FSimCopterCareerCityTransfer& GetPendingCityTransfer() const { return PendingCityTransfer; }

	// Starting a new game or loading a save abandons an unconsumed advancement.
	void ClearPendingCityTransfer();

	// Restores the part of the original CINF career block owned here. Save loading happens after
	// BeginCareer because the mission actor opens a normal city session first; keeping the restore
	// on this subsystem preserves the existing single owner for fleet and log state.
	void RestoreCareerState(
		int32 InOwnedHelicopterMask,
		const TArray<int32>& InHelicopterDepreciation,
		const TArray<FSimCopterCareerLogEntry>& InLogEntries);

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

	UPROPERTY()
	FSimCopterCareerCityTransfer PendingCityTransfer;
};
