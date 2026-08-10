#include "Audio/SimCopterRadio.h"

#include "Audio/SimCopterAudioSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterRadio, Log, All);

namespace
{
	/** The language folder the DJ and commercial globs go through (FUN_00433b20's dir 3). */
	const TCHAR* GLanguageDir = TEXT("English");
}

USimCopterRadioSubsystem* USimCopterRadioSubsystem::Get(const UObject* WorldContextObject)
{
	if (WorldContextObject == nullptr)
	{
		return nullptr;
	}
	const UWorld* World = GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World != nullptr ? World->GetSubsystem<USimCopterRadioSubsystem>() : nullptr;
}

bool USimCopterRadioSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USimCopterRadioSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USimCopterRadioSubsystem, STATGROUP_Tickables);
}

void USimCopterRadioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Seeded from the clock so two sessions do not open on the same track. The original's
	// 55-word table is initialised elsewhere in the exe and is not ported; what matters for
	// parity is the generator's shape, which is.
	Rng.Seed(static_cast<uint32>(FPlatformTime::Cycles()));

	USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this);
	if (Audio == nullptr || Audio->GetSoundRoot().IsEmpty())
	{
		UE_LOG(LogSimCopterRadio, Warning, TEXT("[Radio] No sound root; the radio stays silent."));
		return;
	}

	ScanStations(Audio->GetSoundRoot());
	if (Stations.Num() == 0)
	{
		UE_LOG(LogSimCopterRadio, Warning, TEXT("[Radio] No stations found under %s/radio."), *Audio->GetSoundRoot());
		return;
	}

	// FUN_00430890 falls back to PTR_DAT_004f8f8c - the literal "KMIX" - when the saved state
	// carries no station, so that is what the radio opens on.
	StationIndex = 0;
	for (int32 Index = 0; Index < Stations.Num(); ++Index)
	{
		if (Stations[Index].CallSign.Equals(TEXT("KMIX"), ESearchCase::IgnoreCase))
		{
			StationIndex = Index;
			break;
		}
	}

	RebuildPlaylists();
	RearmSchedule(/*bStopCurrent=*/false);

	UE_LOG(LogSimCopterRadio, Display, TEXT("[Radio] %d stations, %d commercials; opening on %s."),
		Stations.Num(), Commercials.Num(), *GetStationCallSign());
}

void USimCopterRadioSubsystem::Deinitialize()
{
	if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
	{
		Audio->StopRadio();
	}
	Stations.Reset();
	Commercials.Reset();
	Super::Deinitialize();
}

// ---------------------------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------------------------

void USimCopterRadioSubsystem::ScanStations(const FString& SoundRoot)
{
	const FString RadioRoot = FPaths::Combine(SoundRoot, TEXT("radio"));
	const FString StationsRoot = FPaths::Combine(RadioRoot, TEXT("stations"));
	if (!FPaths::DirectoryExists(StationsRoot))
	{
		return;
	}

	// commercl\<lang>\*.wav - shared by every station, which is why it lives above stations\.
	IFileManager& Files = IFileManager::Get();
	const FString CommercialDir = FPaths::Combine(RadioRoot, TEXT("commercl"), GLanguageDir);
	TArray<FString> Found;
	Files.FindFiles(Found, *FPaths::Combine(CommercialDir, TEXT("*.wav")), true, false);
	for (const FString& File : Found)
	{
		Commercials.Add(FPaths::Combine(CommercialDir, File));
	}

	TArray<FString> StationDirs;
	Files.FindFiles(StationDirs, *(StationsRoot / TEXT("*")), false, true);
	for (const FString& DirName : StationDirs)
	{
		const FString Dir = FPaths::Combine(StationsRoot, DirName);

		// The station is named by its `*.id*` marker, not by the folder: the files are empty and
		// exist only to carry the call sign. No marker, no station.
		TArray<FString> Markers;
		Files.FindFiles(Markers, *FPaths::Combine(Dir, TEXT("*.id*")), true, false);
		if (Markers.Num() == 0)
		{
			continue;
		}

		FSimCopterRadioStation Station;
		Station.CallSign = FPaths::GetBaseFilename(Markers[0]);
		Station.Directory = Dir;

		auto Glob = [&Files](const FString& Folder, TArray<FString>& Out)
		{
			TArray<FString> Names;
			Files.FindFiles(Names, *FPaths::Combine(Folder, TEXT("*.wav")), true, false);
			for (const FString& Name : Names)
			{
				Out.Add(FPaths::Combine(Folder, Name));
			}
		};
		Glob(FPaths::Combine(Dir, TEXT("music")), Station.Music);
		Glob(FPaths::Combine(Dir, TEXT("dj"), GLanguageDir), Station.Dj);
		Glob(FPaths::Combine(Dir, TEXT("jingle")), Station.Jingle);

		if (Station.HasContent())
		{
			Stations.Add(MoveTemp(Station));
		}
	}

	// Dial order. The original has no explicit ordering - it takes whatever the directory
	// enumeration hands back - so the port sorts by call sign to make the needle's travel
	// stable across machines and file systems. This is a remake choice.
	Stations.Sort([](const FSimCopterRadioStation& A, const FSimCopterRadioStation& B)
	{
		return A.CallSign.Compare(B.CallSign, ESearchCase::IgnoreCase) < 0;
	});
}

const TArray<FString>& USimCopterRadioSubsystem::GetList(ESimCopterRadioSlot Slot) const
{
	static const TArray<FString> Empty;
	if (!Stations.IsValidIndex(StationIndex))
	{
		return Empty;
	}
	switch (Slot)
	{
	case ESimCopterRadioSlot::Music:      return Stations[StationIndex].Music;
	case ESimCopterRadioSlot::Dj:         return Stations[StationIndex].Dj;
	case ESimCopterRadioSlot::Commercial: return Commercials;
	case ESimCopterRadioSlot::Jingle:     return Stations[StationIndex].Jingle;
	default:                              return Empty;
	}
}

// ---------------------------------------------------------------------------------------------
// The ported RNG and shuffle
// ---------------------------------------------------------------------------------------------

void USimCopterRadioSubsystem::FLaggedFibonacci::Seed(uint32 SeedValue)
{
	// The original's table is filled elsewhere; any spread-out fill gives the same generator.
	//
	// Do NOT fold the seed (an earlier `SeedValue | 1` here mapped 1000 and 1001 onto the same
	// state, so two launches a millisecond apart replayed the same playlist). The LCG below has
	// a non-zero increment, so every seed including 0 is fine as-is.
	uint32 Value = SeedValue;
	for (int32 Index = 0; Index < 55; ++Index)
	{
		Value = Value * 1664525u + 1013904223u;
		State[Index] = static_cast<int32>(Value >> 1);
	}
	// The two cursors are the 55/24 lag pair the update rule implies.
	CursorA = 0;
	CursorB = 31;
}

uint32 USimCopterRadioSubsystem::FLaggedFibonacci::Next(uint32 Modulo)
{
	// SCHOOK: RadioRandom 0x00455d70
	//   DAT_0055afe4 = (DAT_0055afe4 + 1) % 0x37;
	//   DAT_0055afe8 = (DAT_0055afe8 + 1) % 0x37;
	//   tbl[a] -= tbl[b];
	//   return (uint)tbl[a] % n;
	if (Modulo == 0)
	{
		return 0;
	}
	CursorA = (CursorA + 1) % 55;
	CursorB = (CursorB + 1) % 55;
	State[CursorA] = State[CursorA] - State[CursorB];
	return static_cast<uint32>(State[CursorA]) % Modulo;
}

void USimCopterRadioSubsystem::ShuffleWithAntiRepeat(TArray<int32>& InOut, FLaggedFibonacci& Rng, int32 PreviousLast)
{
	// SCHOOK: RadioShuffle 0x00430070
	// The loop starts at the SECOND element and swaps it with a slot in [0, i] - the standard
	// forward Fisher-Yates the original writes as `swap(*p, base[rand(index+1)])`.
	for (int32 Index = 1; Index < InOut.Num(); ++Index)
	{
		const int32 Pick = static_cast<int32>(Rng.Next(static_cast<uint32>(Index + 1)));
		InOut.Swap(Index, Pick);
	}

	// ...then: if the new first element repeats whatever ended the previous cycle, send it to
	// the back. Without this the seam between cycles can play the same category twice running.
	if (PreviousLast != INDEX_NONE && InOut.Num() > 1 && InOut[0] == PreviousLast)
	{
		InOut.Swap(0, InOut.Num() - 1);
	}
}

int32 USimCopterRadioSubsystem::GetSlotChancePercent(ESimCopterRadioSlot Slot)
{
	// FUN_0042f160's four branches: music is unconditional, the rest roll rand() % 100.
	switch (Slot)
	{
	case ESimCopterRadioSlot::Music:      return 100;
	case ESimCopterRadioSlot::Dj:         return 20;   // < 0x14
	case ESimCopterRadioSlot::Commercial: return 90;   // < 0x5a
	case ESimCopterRadioSlot::Jingle:     return 20;   // < 0x14
	default:                              return 0;
	}
}

void USimCopterRadioSubsystem::SetSlotEnabled(const ESimCopterRadioSlot Slot, const bool bEnabled)
{
	const int32 Index = static_cast<int32>(Slot);
	if (Index >= 0 && Index < UE_ARRAY_COUNT(bSlotEnabled))
	{
		bSlotEnabled[Index] = bEnabled;
	}
}

bool USimCopterRadioSubsystem::IsSlotEnabled(const ESimCopterRadioSlot Slot) const
{
	// The Sound dialog has no control for the music slot and the original never gates it, so it
	// stays on whatever the flags say.
	if (Slot == ESimCopterRadioSlot::Music)
	{
		return true;
	}

	const int32 Index = static_cast<int32>(Slot);
	return Index >= 0 && Index < UE_ARRAY_COUNT(bSlotEnabled) ? bSlotEnabled[Index] : true;
}

// ---------------------------------------------------------------------------------------------
// Playlists
// ---------------------------------------------------------------------------------------------

void USimCopterRadioSubsystem::RebuildPlaylists()
{
	// One shuffled order per category, walked in sequence and wrapped - so every track is heard
	// once per cycle before any repeats. That is the audible difference from drawing at random.
	for (int32 SlotIndex = 0; SlotIndex < 4; ++SlotIndex)
	{
		const ESimCopterRadioSlot Slot = static_cast<ESimCopterRadioSlot>(SlotIndex);
		const int32 Count = GetList(Slot).Num();
		Order[SlotIndex].Reset(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Order[SlotIndex].Add(Index);
		}
		ShuffleWithAntiRepeat(Order[SlotIndex], Rng, INDEX_NONE);
		OrderCursor[SlotIndex] = 0;
	}

	// The pattern the scheduler walks. The original loads its own; the composition here - music
	// carrying the cycle with one of each interruption woven in - is the port's, and the
	// probability gates then thin the interruptions out exactly as FUN_0042f160 does.
	Pattern.Reset();
	const int32 MusicBeats = 4;
	for (int32 Beat = 0; Beat < MusicBeats; ++Beat)
	{
		Pattern.Add(static_cast<int32>(ESimCopterRadioSlot::Music));
	}
	Pattern.Add(static_cast<int32>(ESimCopterRadioSlot::Dj));
	Pattern.Add(static_cast<int32>(ESimCopterRadioSlot::Commercial));
	Pattern.Add(static_cast<int32>(ESimCopterRadioSlot::Jingle));

	PatternPreviousLast = INDEX_NONE;
	ReshufflePattern();
}

void USimCopterRadioSubsystem::ReshufflePattern()
{
	const int32 OldLast = Pattern.Num() > 0 ? Pattern.Last() : INDEX_NONE;
	ShuffleWithAntiRepeat(Pattern, Rng, PatternPreviousLast);
	PatternPreviousLast = OldLast;
	PatternCursor = 0;
}

int32 USimCopterRadioSubsystem::TakeNext(ESimCopterRadioSlot Slot)
{
	const int32 SlotIndex = static_cast<int32>(Slot);
	const TArray<FString>& List = GetList(Slot);
	if (List.Num() == 0 || Order[SlotIndex].Num() == 0)
	{
		return INDEX_NONE;
	}

	if (OrderCursor[SlotIndex] >= Order[SlotIndex].Num())
	{
		// End of the bag: reshuffle and start again, which is where the cursor wrap in
		// FUN_0042f3d0 lands after resetting to the list head.
		ShuffleWithAntiRepeat(Order[SlotIndex], Rng, Order[SlotIndex].Last());
		OrderCursor[SlotIndex] = 0;
	}

	const int32 Pick = Order[SlotIndex][OrderCursor[SlotIndex]++];
	return List.IsValidIndex(Pick) ? Pick : INDEX_NONE;
}

bool USimCopterRadioSubsystem::PlaySlot(ESimCopterRadioSlot Slot, USimCopterAudioSubsystem* Audio)
{
	const int32 Pick = TakeNext(Slot);
	if (Pick == INDEX_NONE || Audio == nullptr)
	{
		return false;
	}
	const FString& Path = GetList(Slot)[Pick];
	if (!Audio->PlayRadioFile(Path, Volume))
	{
		return false;
	}
	CurrentSlot = Slot;
	CurrentTitle = FPaths::GetBaseFilename(Path);
	return true;
}

// ---------------------------------------------------------------------------------------------
// The scheduler
// ---------------------------------------------------------------------------------------------

void USimCopterRadioSubsystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this);
	if (Audio == nullptr || Stations.Num() == 0)
	{
		return;
	}

	// This subsystem exists in every game world, including the front end and the city while the
	// player is walking. The radio belongs to the possessed helicopter, so power alone must not
	// make it audible in either of those states.
	if (!bPlayerInHelicopter)
	{
		if (Audio->IsRadioPlaying())
		{
			Audio->StopRadio();
		}
		return;
	}

	if (!bPowered)
	{
		if (Audio->IsRadioPlaying())
		{
			Audio->StopRadio();
		}
		return;
	}

	// SCHOOK: RadioTick 0x0042f160
	// Nothing happens until the current item ends; then a fixed gap runs before the next pick.
	if (!bWaiting)
	{
		if (Audio->IsRadioPlaying())
		{
			return;
		}
		bWaiting = true;
		GapStartTime = FPlatformTime::Seconds();
		return;
	}

	if (FPlatformTime::Seconds() - GapStartTime < static_cast<double>(GapSeconds))
	{
		return;
	}

	// The back-to-back case, before the cursor moves: a one-in-ten chance to follow music with
	// more music. FUN_0042f160 rolls MSVC rand for this, not the shuffle's generator.
	const bool bOnMusic = Pattern.IsValidIndex(PatternCursor) &&
		Pattern[PatternCursor] == static_cast<int32>(ESimCopterRadioSlot::Music);
	if (bOnMusic && FMath::RandRange(0, 99) < BackToBackMusicPercent)
	{
		if (PlaySlot(ESimCopterRadioSlot::Music, Audio))
		{
			bWaiting = false;
		}
		return;
	}

	// Advance, reshuffling the pattern when the cycle closes.
	++PatternCursor;
	if (!Pattern.IsValidIndex(PatternCursor))
	{
		ReshufflePattern();
	}
	if (!Pattern.IsValidIndex(PatternCursor))
	{
		return;
	}

	const ESimCopterRadioSlot Slot = static_cast<ESimCopterRadioSlot>(Pattern[PatternCursor]);

	// Music plays unconditionally; the other three have to make their roll or the beat passes
	// in silence with the cursor already moved on. A slot the Sound dialog switched off behaves
	// exactly like one that lost its roll.
	const int32 Chance = GetSlotChancePercent(Slot);
	if (!IsSlotEnabled(Slot) || (Chance < 100 && FMath::RandRange(0, 99) >= Chance))
	{
		GapStartTime = FPlatformTime::Seconds();
		return;
	}

	if (PlaySlot(Slot, Audio))
	{
		bWaiting = false;
	}
	else
	{
		// Empty category (jingle, in every shipped install): re-arm the gap so the scheduler
		// does not spin on it.
		GapStartTime = FPlatformTime::Seconds();
	}
}

void USimCopterRadioSubsystem::RearmSchedule(bool bStopCurrent)
{
	if (bStopCurrent)
	{
		if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
		{
			Audio->StopRadio();
		}
	}
	bWaiting = true;
	// Back-date the gap so a deliberate station change is heard at once rather than after four
	// seconds of silence.
	GapStartTime = FPlatformTime::Seconds() - static_cast<double>(GapSeconds);
	CurrentTitle.Reset();
}

// ---------------------------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------------------------

void USimCopterRadioSubsystem::SetPowered(bool bInPowered)
{
	if (bPowered == bInPowered)
	{
		return;
	}
	bPowered = bInPowered;
	if (bPowered)
	{
		RearmSchedule(/*bStopCurrent=*/false);
	}
	else if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
	{
		Audio->StopRadio();
	}
}

void USimCopterRadioSubsystem::SetPlayerInHelicopter(const bool bInPlayerHelicopter)
{
	if (bPlayerInHelicopter == bInPlayerHelicopter)
	{
		return;
	}

	bPlayerInHelicopter = bInPlayerHelicopter;
	// Stop the abandoned item on exit and back-date the normal four-second gap on both edges.
	// Re-entering therefore starts the next item immediately while preserving the station and
	// the player's explicit radio power choice.
	RearmSchedule(/*bStopCurrent=*/true);
}

FString USimCopterRadioSubsystem::GetStationCallSign() const
{
	return Stations.IsValidIndex(StationIndex) ? Stations[StationIndex].CallSign : FString();
}

void USimCopterRadioSubsystem::SetStationIndex(int32 Index)
{
	if (Stations.Num() == 0)
	{
		return;
	}
	const int32 Clamped = FMath::Clamp(Index, 0, Stations.Num() - 1);
	if (Clamped == StationIndex)
	{
		return;
	}
	StationIndex = Clamped;
	// The dashboard radio's channel selector returns its volume rocker to the top. Keep this in
	// the subsystem too so console and Settings-screen channel changes follow the same rule.
	SetVolume(1.0f);
	// The playlists are per station, so switching rebuilds them - a fresh shuffle bag, which is
	// also what the original does when it loads a station's lists.
	RebuildPlaylists();
	RearmSchedule(/*bStopCurrent=*/true);
}

void USimCopterRadioSubsystem::StepStation(int32 Delta)
{
	if (Stations.Num() == 0 || Delta == 0)
	{
		return;
	}
	const int32 Count = Stations.Num();
	SetStationIndex(((StationIndex + Delta) % Count + Count) % Count);
}

void USimCopterRadioSubsystem::SetVolume(float InVolume)
{
	Volume = FMath::Clamp(InVolume, 0.0f, 1.0f);
	if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
	{
		Audio->SetRadioVolumeMultiplier(Volume);
	}
}

float USimCopterRadioSubsystem::GetDialAlpha() const
{
	// 0 puts the needle on the first detent and 1 on the last; the widget owns where those two
	// sit on the artwork, so nothing here knows about pixels.
	const int32 Count = Stations.Num();
	if (Count <= 0)
	{
		return 0.0f;
	}
	if (Count == 1)
	{
		return 0.5f;
	}
	return static_cast<float>(StationIndex) / static_cast<float>(Count - 1);
}

int32 USimCopterRadioSubsystem::GetStationForDialAlpha(float Alpha) const
{
	const int32 Count = Stations.Num();
	if (Count <= 0)
	{
		return INDEX_NONE;
	}
	if (Count == 1)
	{
		return 0;
	}
	const float Position = FMath::Clamp(Alpha, 0.0f, 1.0f) * static_cast<float>(Count - 1);
	return FMath::Clamp(FMath::RoundToInt(Position), 0, Count - 1);
}
