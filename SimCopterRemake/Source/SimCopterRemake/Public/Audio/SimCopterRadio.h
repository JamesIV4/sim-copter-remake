// SimCopter's in-cockpit radio.
//
// Ported from the station player at 0x0042f160..0x00431950. Decode notes in
// Docs/memory/simcopter-sound.md; the three things that shape this file:
//
//   * Playlists are DISCOVERED, not tabulated. `FUN_004302b0` globs `*.id*` under
//     sound\radio\stations\ to find the stations - the .id files are empty and it is their
//     *filename* that is the call sign - then each station's music\ and dj\<lang>\, plus the
//     shared commercl\<lang>\, are globbed for `*.wav`.
//
//   * It is a SHUFFLE BAG, not a per-pick random draw. `FUN_0042ff00` Fisher-Yates each file
//     list once at load; `FUN_00430070` re-shuffles the slot-type pattern at the end of every
//     cycle and then swaps first with last if the new first repeats the old last. Playback
//     walks each list in order and wraps, so every track plays once per cycle before any
//     repeat. Both shuffles use `FUN_00455d70`, a subtractive lagged-Fibonacci generator -
//     neither MSVC rand nor the people LFSR.
//
//   * The probability gates in `FUN_0042f160` sit ON TOP of that pattern and DO use MSVC rand.
//
// The dial ordering, the tuner geometry and click-to-tune are the remake's; see the .cpp.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SimCopterRadio.generated.h"

class USimCopterAudioSubsystem;

/** The four slot types of the pattern array, in the order the loader fills their lists. */
UENUM()
enum class ESimCopterRadioSlot : uint8
{
	Music      = 0, // always plays
	Dj         = 1, // 20%
	Commercial = 2, // 90%
	Jingle     = 3, // 20% - the folders ship empty, so this never fires
};

/** One discovered station. */
USTRUCT()
struct FSimCopterRadioStation
{
	GENERATED_BODY()

	/** The `.id` file's base name: KMIX, kcla, Kjaz, kroc, ktec. */
	UPROPERTY()
	FString CallSign;

	/** Absolute path of the station folder. */
	UPROPERTY()
	FString Directory;

	UPROPERTY()
	TArray<FString> Music;

	UPROPERTY()
	TArray<FString> Dj;

	UPROPERTY()
	TArray<FString> Jingle;

	bool HasContent() const { return Music.Num() > 0 || Dj.Num() > 0 || Jingle.Num() > 0; }
};

/**
 * SCHOOK: RadioTick 0x0042f160
 *
 * One radio for the world. Get it with USimCopterRadioSubsystem::Get(WorldContext).
 */
UCLASS()
class SIMCOPTERREMAKE_API USimCopterRadioSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static USimCopterRadioSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return Stations.Num() > 0; }

	// --- controls ---

	void SetPowered(bool bInPowered);
	bool IsPowered() const { return bPowered; }

	/** Dial order, ascending. Empty when no station folder was found. */
	const TArray<FSimCopterRadioStation>& GetStations() const { return Stations; }

	int32 GetStationCount() const { return Stations.Num(); }
	int32 GetStationIndex() const { return StationIndex; }
	FString GetStationCallSign() const;

	/** Clamped, and re-arms the schedule so the new station is heard immediately. */
	void SetStationIndex(int32 Index);
	void NextStation() { StepStation(+1); }
	void PreviousStation() { StepStation(-1); }
	void StepStation(int32 Delta);

	/**
	 * Where the needle sits, 0 at the left end of the printed scale and 1 at the right.
	 * With N stations the dial is divided into N evenly spaced detents, each sitting at the
	 * centre of its share of the scale - so the outermost stations are inset rather than
	 * jammed against the ends.
	 */
	float GetDialAlpha() const;

	/** Inverse of GetDialAlpha: the station nearest a point on the scale. Used by click-to-tune. */
	int32 GetStationForDialAlpha(float Alpha) const;

	/** What is sounding right now, for the HUD/debug readout. */
	ESimCopterRadioSlot GetCurrentSlot() const { return CurrentSlot; }
	const FString& GetCurrentTitle() const { return CurrentTitle; }

	/** 0..1, applied on top of the mixer's master volume. */
	void SetVolume(float InVolume);
	float GetVolume() const { return Volume; }

	// --- the ported RNG, exposed for tests ---

	/**
	 * SCHOOK: RadioRandom 0x00455d70
	 * Subtractive lagged-Fibonacci: two cursors advance mod 55, the entry at the first is
	 * decremented by the entry at the second, and the result is taken mod n. The original's
	 * 55-word table is seeded elsewhere in the exe; the seeding here is the port's own, which
	 * changes which order you get but not the generator's behaviour.
	 */
	struct SIMCOPTERREMAKE_API FLaggedFibonacci
	{
		int32 State[55] = {};
		int32 CursorA = 0;
		int32 CursorB = 0;

		void Seed(uint32 SeedValue);
		uint32 Next(uint32 Modulo);
	};

	/**
	 * SCHOOK: RadioShuffle 0x0042ff00 / 0x00430070
	 * Fisher-Yates over Rng. `PreviousLast` is the element that ended the last cycle; when it is
	 * given and the shuffle puts an equal value first, first and last are swapped so the same
	 * value cannot straddle the seam. Pass INDEX_NONE to skip that.
	 */
	static void ShuffleWithAntiRepeat(TArray<int32>& InOut, FLaggedFibonacci& Rng, int32 PreviousLast);

	/** The scheduler's per-slot roll thresholds, out of 100. Music is unconditional. */
	static int32 GetSlotChancePercent(ESimCopterRadioSlot Slot);

	/** FUN_0042f160's gap between items, in the original's timer units (4000 = 4 s). */
	static constexpr float GapSeconds = 4.0f;

	/** The chance FUN_0042f160 replays the music slot without advancing the cursor. */
	static constexpr int32 BackToBackMusicPercent = 10;

private:
	UPROPERTY(Transient)
	TArray<FSimCopterRadioStation> Stations;

	/** Shared across every station - commercl\ sits above stations\, not inside one. */
	TArray<FString> Commercials;

	int32 StationIndex = 0;
	bool bPowered = true;
	float Volume = 1.0f;

	/** The slot-type pattern, reshuffled at the end of each cycle, and its cursor. */
	TArray<int32> Pattern;
	int32 PatternCursor = 0;
	int32 PatternPreviousLast = INDEX_NONE;

	/** Shuffled play order per category, plus the cursor that walks it. */
	TArray<int32> Order[4];
	int32 OrderCursor[4] = {};

	ESimCopterRadioSlot CurrentSlot = ESimCopterRadioSlot::Music;
	FString CurrentTitle;

	/** True once the current item has ended and the inter-item gap is running. */
	bool bWaiting = true;
	double GapStartTime = 0.0;

	FLaggedFibonacci Rng;

	void ScanStations(const FString& SoundRoot);
	void RebuildPlaylists();
	void ReshufflePattern();
	const TArray<FString>& GetList(ESimCopterRadioSlot Slot) const;

	/** Advances the cursor for a category, reshuffling and wrapping at the end. */
	int32 TakeNext(ESimCopterRadioSlot Slot);

	/** Resolves a list entry to an absolute path and starts it. */
	bool PlaySlot(ESimCopterRadioSlot Slot, USimCopterAudioSubsystem* Audio);

	/** Drops everything queued and starts the gap, so a station change is heard at once. */
	void RearmSchedule(bool bStopCurrent);
};
