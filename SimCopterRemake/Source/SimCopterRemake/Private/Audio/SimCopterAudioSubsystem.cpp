#include "Audio/SimCopterAudioSubsystem.h"

#include "Audio.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Formats/SimCopterOriginalGamePaths.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWaveProcedural.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCopterAudio, Log, All);

namespace
{
	/**
	 * A procedural wave never reports "finished" on its own, so a looping slot needs a duration
	 * the mixer will not treat as an end. AudioDefines.h spells this 10000 s; it is repeated
	 * here so this file does not depend on AudioMixerCore's header layout.
	 */
	constexpr float GLoopingDurationSeconds = 10000.0f;

	/**
	 * Both original clamps on a buffer frequency, from FUN_0041dd90:
	 *     cmp eax, 0x64     -> floor 100 Hz
	 *     cmp eax, 0x186a0  -> ceiling 100000 Hz
	 */
	constexpr int32 GMinFrequencyHz = 100;
	constexpr int32 GMaxFrequencyHz = 100000;

	/**
	 * UE's own pitch clamp (FAudioDevice::ClampPitch, MIN_PITCH/MAX_PITCH). The original's
	 * range is far wider - a rotor below about 250 rpm asks for a pitch the mixer will not
	 * produce - so the port clamps here explicitly rather than letting the value be silently
	 * altered somewhere downstream. This is a deliberate divergence; see the memory note.
	 */
	constexpr float GMinPitchMultiplier = 0.4f;
	constexpr float GMaxPitchMultiplier = 2.0f;
}

USimCopterAudioSubsystem* USimCopterAudioSubsystem::Get(const UObject* WorldContextObject)
{
	if (WorldContextObject == nullptr)
	{
		return nullptr;
	}
	const UWorld* World = GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World != nullptr ? World->GetSubsystem<USimCopterAudioSubsystem>() : nullptr;
}

bool USimCopterAudioSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void USimCopterAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	SoundRoot = ResolveSoundRoot();
	if (SoundRoot.IsEmpty())
	{
		UE_LOG(LogSimCopterAudio, Warning,
			TEXT("[Audio] Original 'sound' folder not found; the game will run silent."));
		return;
	}

	// bAttenuate off: the distance law is FUN_004247c0's, applied by us as a volume multiplier.
	// Spatialisation stays on so the listener still hears direction - the original only panned
	// by +/-12.5%% (FUN_004247c0's `(10000 * localX) / 8`), which is a deliberate divergence.
	SpatialAttenuation = NewObject<USoundAttenuation>(this, TEXT("SimCopterSpatialAttenuation"));
	if (SpatialAttenuation != nullptr)
	{
		FSoundAttenuationSettings& Settings = SpatialAttenuation->Attenuation;
		Settings.bAttenuate = false;
		Settings.bSpatialize = true;
		Settings.bAttenuateWithLPF = false;
		Settings.bEnableOcclusion = false;
		Settings.bEnableReverbSend = false;
		// Nothing culls on this radius - Play3D already applied the original's 1920-unit test -
		// but the mixer wants a sane value, so use the same distance.
		Settings.FalloffDistance = AudibleRangeUnits * OriginalUnitToCm;
	}

	bSoundsAvailable = true;
	UE_LOG(LogSimCopterAudio, Display, TEXT("[Audio] Sound root: %s"), *SoundRoot);
}

void USimCopterAudioSubsystem::Deinitialize()
{
	for (int32 Id = 0; Id < SimCopterSound::NumSlots; ++Id)
	{
		if (UAudioComponent* Component = SlotComponents.IsValidIndex(Id) ? SlotComponents[Id].Get() : nullptr)
		{
			Component->Stop();
			Component->DestroyComponent();
		}
	}
	SlotComponents.Reset();
	for (const TObjectPtr<UAudioComponent>& Component : AttachedVoiceLoopComponents)
	{
		if (Component != nullptr)
		{
			Component->Stop();
			Component->DestroyComponent();
		}
	}
	AttachedVoiceLoopComponents.Reset();

	StopStandaloneSounds();

	if (RadioComponent != nullptr)
	{
		RadioComponent->Stop();
		RadioComponent->DestroyComponent();
		RadioComponent = nullptr;
	}
	if (MusicComponent != nullptr)
	{
		MusicComponent->Stop();
		MusicComponent->DestroyComponent();
		MusicComponent = nullptr;
	}
	RadioEndTime = 0.0;
	ClearRadioVoiceQueue();
	ClipCache.Reset();
	bSoundsAvailable = false;

	Super::Deinitialize();
}

TStatId USimCopterAudioSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USimCopterAudioSubsystem, STATGROUP_Tickables);
}

void USimCopterAudioSubsystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const double Now = FPlatformTime::Seconds();

	// One-shots: a procedural wave keeps its source alive after the queue drains, so the port
	// retires the slot on its own deadline. IsPlaying() reads the same field, which is what the
	// original's DirectSound status query gave every `if (!IsPlaying(id))` call site.
	for (int32 Id = 0; Id < SimCopterSound::NumSlots; ++Id)
	{
		FSlot& Slot = Slots[Id];
		if (Slot.bLooping || Slot.OneShotEndTime <= 0.0 || Now < Slot.OneShotEndTime)
		{
			continue;
		}
		Slot.OneShotEndTime = 0.0;
		if (UAudioComponent* Component = SlotComponents.IsValidIndex(Id) ? SlotComponents[Id].Get() : nullptr)
		{
			Component->Stop();
		}
	}

	for (int32 Index = LooseComponents.Num() - 1; Index >= 0; --Index)
	{
		UAudioComponent* Component = LooseComponents[Index].Get();
		if (Component == nullptr || !Component->IsPlaying())
		{
			if (Component != nullptr)
			{
				Component->DestroyComponent();
			}
			LooseComponents.RemoveAtSwap(Index);
		}
	}
	for (int32 Index = AttachedVoiceLoopComponents.Num() - 1; Index >= 0; --Index)
	{
		UAudioComponent* Component = AttachedVoiceLoopComponents[Index].Get();
		if (Component == nullptr || !Component->IsPlaying())
		{
			if (Component != nullptr)
			{
				Component->DestroyComponent();
			}
			AttachedVoiceLoopComponents.RemoveAtSwap(Index);
		}
	}

	// SCHOOK: DispatchVoicePlay 0x0042a3b0: advance queued radio voice announcements sequentially
	if (CurrentDispatchVoiceId != INDEX_NONE)
	{
		if (!IsPlaying(CurrentDispatchVoiceId) || (CurrentDispatchVoiceEndTime > 0.0 && Now >= CurrentDispatchVoiceEndTime))
		{
			CurrentDispatchVoiceId = INDEX_NONE;
			CurrentDispatchVoiceEndTime = 0.0;
		}
	}

	if (CurrentDispatchVoiceId == INDEX_NONE && DispatchVoiceQueue.Num() > 0)
	{
		int32 NextId = DispatchVoiceQueue[0];
		DispatchVoiceQueue.RemoveAt(0);
		if (Play2D(NextId))
		{
			CurrentDispatchVoiceId = NextId;
			const FSlot& Slot = Slots[NextId];
			CurrentDispatchVoiceEndTime = Now + static_cast<double>(Slot.Clip.Duration);
		}
	}
}

void USimCopterAudioSubsystem::QueueRadioVoice(int32 SoundId)
{
	if (SoundId < 0 || SoundId >= SimCopterSound::NumSlots)
	{
		return;
	}
	DispatchVoiceQueue.Add(SoundId);
}

void USimCopterAudioSubsystem::ClearRadioVoiceQueue()
{
	if (CurrentDispatchVoiceId != INDEX_NONE)
	{
		Stop(CurrentDispatchVoiceId);
		CurrentDispatchVoiceId = INDEX_NONE;
		CurrentDispatchVoiceEndTime = 0.0;
	}
	DispatchVoiceQueue.Reset();
}

bool USimCopterAudioSubsystem::IsRadioVoicePlayingOrQueued() const
{
	return CurrentDispatchVoiceId != INDEX_NONE || DispatchVoiceQueue.Num() > 0;
}

// ---------------------------------------------------------------------------------------------
// Asset resolution
// ---------------------------------------------------------------------------------------------

FString USimCopterAudioSubsystem::ResolveSoundRoot() const
{
	return SimCopterOriginalGame::ResolveDirectory(TEXT("sound"));
}

FString USimCopterAudioSubsystem::ResolveWavPath(const FString& WavName, SimCopterSound::ESoundDir Dir) const
{
	if (SoundRoot.IsEmpty() || WavName.IsEmpty())
	{
		return FString();
	}

	// Ghidra spells a '#' in a symbol as '_', so the people table carries trbnc_ / trptf_ /
	// tubaf_ for files that are trbnc#.WAV / trptf#.WAV / tubaf#.WAV on disk.
	FString Base = WavName;
	if (Base.EndsWith(TEXT("_")))
	{
		Base.LeftChopInline(1);
		Base.AppendChar(TEXT('#'));
	}

	// Search the language folder first, then the root, then people/, whatever the slot claims.
	// Two reasons the claim is not enough on its own: id 0x1d registers HELP1 against the
	// language folder although the retail install ships help1.wav in sound\, and the people
	// clips are named by FUN_004c5210 with no directory at all.
	TArray<FString, TInlineAllocator<3>> Dirs;
	if (Dir == SimCopterSound::ESoundDir::Language)
	{
		Dirs.Add(FPaths::Combine(SoundRoot, LanguageDir));
		Dirs.Add(SoundRoot);
	}
	else
	{
		Dirs.Add(SoundRoot);
		Dirs.Add(FPaths::Combine(SoundRoot, LanguageDir));
	}
	Dirs.Add(FPaths::Combine(SoundRoot, TEXT("people")));

	// The install mixes .WAV and .wav, and NTFS is case-insensitive, but a packaged build on a
	// case-sensitive mount is not - try both.
	static const TCHAR* Extensions[] = { TEXT(".WAV"), TEXT(".wav") };
	for (const FString& Folder : Dirs)
	{
		for (const TCHAR* Extension : Extensions)
		{
			const FString Path = FPaths::Combine(Folder, Base + Extension);
			if (FPaths::FileExists(Path))
			{
				return Path;
			}
		}
	}
	return FString();
}

bool USimCopterAudioSubsystem::DecodeWav(const FString& AbsolutePath, FSimCopterPcmClip& OutClip)
{
	OutClip = FSimCopterPcmClip();

	TArray<uint8> FileBytes;
	if (!FFileHelper::LoadFileToArray(FileBytes, *AbsolutePath))
	{
		return false;
	}

	FWaveModInfo WaveInfo;
	if (!WaveInfo.ReadWaveInfo(FileBytes.GetData(), FileBytes.Num()))
	{
		UE_LOG(LogSimCopterAudio, Warning, TEXT("[Audio] '%s' is not a readable RIFF/WAVE."), *AbsolutePath);
		return false;
	}

	const int32 Channels = WaveInfo.pChannels != nullptr ? *WaveInfo.pChannels : 0;
	const int32 SampleRate = WaveInfo.pSamplesPerSec != nullptr ? static_cast<int32>(*WaveInfo.pSamplesPerSec) : 0;
	const int32 BitsPerSample = WaveInfo.pBitsPerSample != nullptr ? *WaveInfo.pBitsPerSample : 0;
	if (Channels <= 0 || SampleRate <= 0 || WaveInfo.SampleDataSize == 0)
	{
		return false;
	}

	// The retail install mixes two quality sets - 11025 Hz 8-bit and 22050 Hz 16-bit - and the
	// procedural FIFO wants signed 16-bit either way, so 8-bit is widened here.
	if (BitsPerSample == 16)
	{
		OutClip.Pcm16.Append(WaveInfo.SampleDataStart, WaveInfo.SampleDataSize);
	}
	else if (BitsPerSample == 8)
	{
		const int32 NumSamples = static_cast<int32>(WaveInfo.SampleDataSize);
		OutClip.Pcm16.SetNumUninitialized(NumSamples * 2);
		int16* Out = reinterpret_cast<int16*>(OutClip.Pcm16.GetData());
		for (int32 Index = 0; Index < NumSamples; ++Index)
		{
			Out[Index] = static_cast<int16>((static_cast<int32>(WaveInfo.SampleDataStart[Index]) - 128) << 8);
		}
	}
	else
	{
		UE_LOG(LogSimCopterAudio, Warning, TEXT("[Audio] '%s' has unsupported depth %d."), *AbsolutePath, BitsPerSample);
		return false;
	}

	OutClip.SampleRate = SampleRate;
	OutClip.Channels = Channels;
	OutClip.Duration = static_cast<float>(OutClip.Pcm16.Num() / (2 * Channels)) / static_cast<float>(SampleRate);
	return true;
}

const FSimCopterPcmClip* USimCopterAudioSubsystem::LoadClip(const FString& WavName, SimCopterSound::ESoundDir Dir)
{
	const FString Key = WavName.ToLower();
	if (const FSimCopterPcmClip* Cached = ClipCache.Find(Key))
	{
		return Cached->IsValid() ? Cached : nullptr;
	}

	// Cache the failure too, so a missing clip is not re-probed on every frame that asks: an
	// empty entry is the "already tried, nothing here" marker the lookup above reads.
	FSimCopterPcmClip& Clip = ClipCache.Add(Key);

	const FString Path = ResolveWavPath(WavName, Dir);
	if (Path.IsEmpty())
	{
		UE_LOG(LogSimCopterAudio, Verbose, TEXT("[Audio] '%s' not found under %s"), *WavName, *SoundRoot);
		return nullptr;
	}

	return DecodeWav(Path, Clip) ? &Clip : nullptr;
}

// ---------------------------------------------------------------------------------------------
// The radio's channel
// ---------------------------------------------------------------------------------------------

bool USimCopterAudioSubsystem::PlayRadioFile(const FString& AbsolutePath, float VolumeMultiplier)
{
	UWorld* World = GetWorld();
	if (World == nullptr || !bSoundsAvailable || AbsolutePath.IsEmpty())
	{
		return false;
	}

	// Deliberately uncached: a five-minute music track is about 7 MB of 16-bit PCM and the mix
	// station alone has 25 of them. The samples live only as long as the wave that plays them.
	FSimCopterPcmClip Clip;
	if (!DecodeWav(AbsolutePath, Clip))
	{
		return false;
	}

	USoundWaveProcedural* Wave = MakeWave(Clip, /*bLoop=*/false, this);
	if (Wave == nullptr)
	{
		return false;
	}

	if (RadioComponent == nullptr)
	{
		RadioComponent = NewObject<UAudioComponent>(this);
		if (RadioComponent == nullptr)
		{
			return false;
		}
		RadioComponent->bAutoActivate = false;
		RadioComponent->bAutoDestroy = false;
		RadioComponent->bAllowSpatialization = false;
		RadioComponent->bIsUISound = true;
		RadioComponent->RegisterComponentWithWorld(World);
	}

	RadioComponent->Stop();
	RadioComponent->SetSound(Wave);
	SetRadioVolumeMultiplier(VolumeMultiplier);
	RadioEndTime = FPlatformTime::Seconds() + static_cast<double>(Clip.Duration);
	RadioComponent->Play();
	return true;
}

void USimCopterAudioSubsystem::SetRadioVolumeMultiplier(const float VolumeMultiplier)
{
	RadioVolumeMultiplier = FMath::Clamp(VolumeMultiplier, 0.0f, 1.0f);
	ApplyRadioVolume();
}

void USimCopterAudioSubsystem::ApplyRadioVolume()
{
	if (RadioComponent != nullptr)
	{
		RadioComponent->SetVolumeMultiplier(
			RadioVolumeMultiplier * VolumeIndexToGain(MasterVolume));
	}
}

void USimCopterAudioSubsystem::StopRadio()
{
	RadioEndTime = 0.0;
	if (RadioComponent != nullptr)
	{
		RadioComponent->Stop();
	}
}

bool USimCopterAudioSubsystem::IsRadioPlaying() const
{
	return RadioEndTime > 0.0 && FPlatformTime::Seconds() < RadioEndTime;
}

float USimCopterAudioSubsystem::GetRadioRemainingSeconds() const
{
	if (!IsRadioPlaying())
	{
		return 0.0f;
	}
	return static_cast<float>(RadioEndTime - FPlatformTime::Seconds());
}

bool USimCopterAudioSubsystem::EnsureSlotLoaded(int32 Id)
{
	const SimCopterSound::FSoundSlot* Registered = SimCopterSound::GetSlot(Id);
	if (Registered == nullptr || !bSoundsAvailable)
	{
		return false;
	}

	FSlot& Slot = Slots[Id];
	if (Slot.bLoadAttempted)
	{
		return Slot.Clip.IsValid();
	}
	Slot.bLoadAttempted = true;

	if (const FSimCopterPcmClip* Clip = LoadClip(Registered->Wav, Registered->Dir))
	{
		Slot.Clip = *Clip;
		Slot.LoadedWav = Registered->Wav;
	}
	return Slot.Clip.IsValid();
}

// ---------------------------------------------------------------------------------------------
// Components and waves
// ---------------------------------------------------------------------------------------------

USoundWaveProcedural* USimCopterAudioSubsystem::MakeWave(const FSimCopterPcmClip& Clip, bool bLoop, UObject* Outer)
{
	// A fresh wave per play, deliberately. A procedural wave's FIFO is drained by the audio
	// render thread, so re-queueing one shared wave races that reader; the mission-voice port
	// hit exactly this and the throwaway wave is the fix that stuck.
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(Outer);
	if (Wave == nullptr)
	{
		return nullptr;
	}

	Wave->SetSampleRate(Clip.SampleRate);
	Wave->NumChannels = Clip.Channels;
	Wave->Duration = bLoop ? GLoopingDurationSeconds : Clip.Duration;
	Wave->SoundGroup = SOUNDGROUP_Default;
	Wave->bLooping = false; // procedural waves ignore this; looping is the underflow refill below
	Wave->SampleByteSize = 2;
	Wave->QueueAudio(Clip.Pcm16.GetData(), Clip.Pcm16.Num());

	if (bLoop)
	{
		// The refill runs on the audio render thread, so it must not touch subsystem state.
		// A shared immutable copy of the samples is captured instead.
		TSharedRef<TArray<uint8>, ESPMode::ThreadSafe> Samples =
			MakeShared<TArray<uint8>, ESPMode::ThreadSafe>(Clip.Pcm16);
		Wave->OnSoundWaveProceduralUnderflow.BindLambda(
			[Samples](USoundWaveProcedural* InWave, int32 /*SamplesRequired*/)
			{
				if (InWave != nullptr)
				{
					InWave->QueueAudio(Samples->GetData(), Samples->Num());
				}
			});
	}

	return Wave;
}

UAudioComponent* USimCopterAudioSubsystem::EnsureSlotComponent(int32 Id)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	if (SlotComponents.Num() < SimCopterSound::NumSlots)
	{
		SlotComponents.SetNum(SimCopterSound::NumSlots);
	}
	if (UAudioComponent* Existing = SlotComponents[Id].Get())
	{
		return Existing;
	}

	UAudioComponent* Component = NewObject<UAudioComponent>(this);
	if (Component == nullptr)
	{
		return nullptr;
	}
	Component->bAutoActivate = false;
	Component->bAutoDestroy = false;
	Component->bAllowSpatialization = false;
	Component->bStopWhenOwnerDestroyed = true;
	Component->SetVolumeMultiplier(1.0f);
	Component->RegisterComponentWithWorld(World);

	SlotComponents[Id] = Component;
	return Component;
}

bool USimCopterAudioSubsystem::StartSlot(int32 Id, bool bLoop)
{
	UAudioComponent* Component = EnsureSlotComponent(Id);
	if (Component == nullptr)
	{
		return false;
	}

	FSlot& Slot = Slots[Id];
	USoundWaveProcedural* Wave = MakeWave(Slot.Clip, bLoop, this);
	if (Wave == nullptr)
	{
		return false;
	}

	Component->Stop();
	Component->SetSound(Wave);
	Component->SetPitchMultiplier(Slot.PitchMultiplier);
	ApplySlotVolume(Id);

	Slot.bLooping = bLoop;
	Slot.OneShotEndTime = bLoop
		? 0.0
		: FPlatformTime::Seconds()
			+ static_cast<double>(Slot.Clip.Duration / FMath::Max(Slot.PitchMultiplier, KINDA_SMALL_NUMBER));

	Component->Play();
	return true;
}

void USimCopterAudioSubsystem::ApplySlotVolume(int32 Id)
{
	UAudioComponent* Component = SlotComponents.IsValidIndex(Id) ? SlotComponents[Id].Get() : nullptr;
	if (Component == nullptr)
	{
		return;
	}

	// FUN_0042a360 / FUN_004247c0 both drive the buffer at (master * index) / 10000, and
	// FUN_0041de20 then clamps that into [0, 10000] before handing it to DirectSound.
	const int32 Scaled = (MasterVolume * Slots[Id].VolumeIndex) / 10000;
	Component->SetVolumeMultiplier(VolumeIndexToGain(Scaled));
}

// ---------------------------------------------------------------------------------------------
// The ported arithmetic
// ---------------------------------------------------------------------------------------------

int32 USimCopterAudioSubsystem::OctagonalNorm1616(int32 X, int32 Y, int32 Z)
{
	// SCHOOK: VecOctagonalNorm 0x00468220
	int32 A = FMath::Abs(X);
	int32 B = FMath::Abs(Y);
	const int32 C = FMath::Abs(Z);

	int32 Low = B;
	if (A < B)
	{
		Low = A;
		A = B;
	}
	// The original returns `largest + ((sum of the other two) >> 2)` either way; which two are
	// "the other two" depends on whether |z| beat the winner of |x| vs |y|.
	return (A < C) ? (C + ((A + Low) >> 2)) : (((C + Low) >> 2) + A);
}

int32 USimCopterAudioSubsystem::DistanceVolumeIndex(float DistanceUnits)
{
	// SCHOOK: SoundDistanceVolume 0x004247c0
	//   ratio = FixedDiv(distance, 0x07800000)   ; / 1920.0
	//   index = (FixedMul(ratio, 0xf0600000) >> 16) + 10000
	// 0xf0600000 is -4000.0 in 16.16, so the index falls linearly from 10000 at the listener
	// to 6000 at the cull edge - only -40 dB across the whole audible range.
	const float Ratio = DistanceUnits / AudibleRangeUnits;
	return 10000 - FMath::FloorToInt(Ratio * static_cast<float>(DistanceAttenuationSpan));
}

float USimCopterAudioSubsystem::VolumeIndexToGain(int32 Index)
{
	// FUN_0041de20 clamps to [0, 10000] and passes `index - 10000` to IDirectSoundBuffer::
	// SetVolume, whose unit is hundredths of a decibel of attenuation.
	const int32 Clamped = FMath::Clamp(Index, 0, 10000);
	if (Clamped <= 0)
	{
		return 0.0f;
	}
	return FMath::Pow(10.0f, static_cast<float>(Clamped - 10000) / 2000.0f);
}

// ---------------------------------------------------------------------------------------------
// The original's API
// ---------------------------------------------------------------------------------------------

bool USimCopterAudioSubsystem::Play2D(int32 Id, int32 Flags)
{
	if (!EnsureSlotLoaded(Id))
	{
		return false;
	}

	// FUN_0042a2a0 turns the flag word into the buffer's play mode: bit1 set means mode 1
	// (rewind if already playing), clear means mode 2 (leave it alone and return). Every
	// shipped call site leaves it clear, which is why the original never stacks a sound.
	const bool bRestart = (Flags & SimCopterSoundFlags::Restart) != 0;
	if (!bRestart && IsPlaying(Id))
	{
		return true;
	}

	FSlot& Slot = Slots[Id];
	Slot.bPositional = false;
	Slot.VolumeIndex = 10000; // Play2D sets the buffer to the master volume outright.

	if (UAudioComponent* Component = EnsureSlotComponent(Id))
	{
		Component->bAllowSpatialization = false;
		Component->AttenuationSettings = nullptr;
		Component->bOverrideAttenuation = false;
	}
	return StartSlot(Id, (Flags & SimCopterSoundFlags::Loop) != 0);
}

bool USimCopterAudioSubsystem::Play3D(int32 Id, const FVector& WorldLocation, int32 Flags)
{
	if (!EnsureSlotLoaded(Id))
	{
		return false;
	}

	// SCHOOK: SoundPlay3D 0x0042a1f0 - the audibility test comes first and is a hard reject,
	// not a fade: `if (0x7800000 <= FUN_00468220(pos - listener)) return 0;`.
	const FVector DeltaCm = WorldLocation - ListenerLocation;
	const float Unit = OriginalUnitToCm;
	const int32 Norm = OctagonalNorm1616(
		static_cast<int32>(DeltaCm.X / Unit * 65536.0f),
		static_cast<int32>(DeltaCm.Y / Unit * 65536.0f),
		static_cast<int32>(DeltaCm.Z / Unit * 65536.0f));
	if (Norm >= static_cast<int32>(AudibleRangeUnits) * 65536)
	{
		return false;
	}

	const bool bRestart = (Flags & SimCopterSoundFlags::Restart) != 0;
	if (!bRestart && IsPlaying(Id))
	{
		// Still true to the original: FUN_0042a1f0 calls SetPosition after Play unconditionally,
		// so an already-playing looper is re-aimed at the new emitter.
		SetPosition(Id, WorldLocation);
		return true;
	}

	if (!StartSlot(Id, (Flags & SimCopterSoundFlags::Loop) != 0))
	{
		return false;
	}
	SetPosition(Id, WorldLocation);
	return true;
}

void USimCopterAudioSubsystem::Stop(int32 Id)
{
	if (!SimCopterSound::GetSlot(Id))
	{
		return;
	}
	FSlot& Slot = Slots[Id];
	Slot.bLooping = false;
	Slot.OneShotEndTime = 0.0;
	if (UAudioComponent* Component = SlotComponents.IsValidIndex(Id) ? SlotComponents[Id].Get() : nullptr)
	{
		Component->Stop();
	}
}

bool USimCopterAudioSubsystem::IsPlaying(int32 Id) const
{
	if (!SimCopterSound::GetSlot(Id))
	{
		return false;
	}
	const FSlot& Slot = Slots[Id];
	if (Slot.bLooping)
	{
		return true;
	}
	return Slot.OneShotEndTime > 0.0 && FPlatformTime::Seconds() < Slot.OneShotEndTime;
}

void USimCopterAudioSubsystem::SetVolumeAdjust(int32 Id, int32 Adjust)
{
	if (!SimCopterSound::GetSlot(Id))
	{
		return;
	}
	// SCHOOK: SoundSetVolume 0x0042a360 - `DAT_00519a70[id] = adj + 10000`.
	Slots[Id].VolumeIndex = Adjust + 10000;
	ApplySlotVolume(Id);
}

void USimCopterAudioSubsystem::SetPosition(int32 Id, const FVector& WorldLocation)
{
	if (!SimCopterSound::GetSlot(Id))
	{
		return;
	}

	FSlot& Slot = Slots[Id];
	Slot.bPositional = true;
	Slot.Location = WorldLocation;

	const float DistanceUnits = static_cast<float>(FVector::Dist(WorldLocation, ListenerLocation)) / OriginalUnitToCm;
	Slot.VolumeIndex = DistanceVolumeIndex(DistanceUnits);

	if (UAudioComponent* Component = EnsureSlotComponent(Id))
	{
		Component->bAllowSpatialization = true;
		Component->bOverrideAttenuation = false;
		Component->AttenuationSettings = SpatialAttenuation;
		Component->SetWorldLocation(WorldLocation);
	}
	ApplySlotVolume(Id);
}

void USimCopterAudioSubsystem::AddFrequency(int32 Id, int32 DeltaHz)
{
	if (!EnsureSlotLoaded(Id))
	{
		return;
	}

	// SCHOOK: SoundAddFrequency 0x0042a330 - `SetFreq(GetFreq() + delta)`, where GetFreq
	// (vtable +0x60) is `return this[0x5c]`, the clip's OWN rate. The delta is therefore
	// absolute, not cumulative: calling it every frame with the same argument is a no-op, which
	// is exactly how the rotor loop drives it.
	SetFrequencyHz(Id, Slots[Id].Clip.SampleRate + DeltaHz);
}

void USimCopterAudioSubsystem::SetFrequencyHz(int32 Id, int32 Hz)
{
	if (!EnsureSlotLoaded(Id))
	{
		return;
	}

	FSlot& Slot = Slots[Id];
	const int32 Base = FMath::Max(Slot.Clip.SampleRate, 1);
	const int32 Target = FMath::Clamp(Hz, GMinFrequencyHz, GMaxFrequencyHz);

	Slot.PitchMultiplier = FMath::Clamp(
		static_cast<float>(Target) / static_cast<float>(Base),
		GMinPitchMultiplier,
		GMaxPitchMultiplier);

	if (UAudioComponent* Component = SlotComponents.IsValidIndex(Id) ? SlotComponents[Id].Get() : nullptr)
	{
		Component->SetPitchMultiplier(Slot.PitchMultiplier);
	}
}

void USimCopterAudioSubsystem::ResetFrequency(int32 Id)
{
	AddFrequency(Id, 0);
}

bool USimCopterAudioSubsystem::SetFile(int32 Id, const FString& WavName, SimCopterSound::ESoundDir Dir)
{
	if (!SimCopterSound::GetSlot(Id) || !bSoundsAvailable)
	{
		return false;
	}

	FSlot& Slot = Slots[Id];
	if (Slot.LoadedWav.Equals(WavName, ESearchCase::IgnoreCase) && Slot.Clip.IsValid())
	{
		return true;
	}

	const FSimCopterPcmClip* Clip = LoadClip(WavName, Dir);
	if (Clip == nullptr)
	{
		return false;
	}

	// SCHOOK: SoundSetFile 0x0042a100 - the original destroys and rebuilds the buffer, so
	// anything playing in the slot ends.
	Stop(Id);
	Slot.Clip = *Clip;
	Slot.LoadedWav = WavName;
	Slot.bLoadAttempted = true;
	Slot.PitchMultiplier = 1.0f;
	return true;
}

bool USimCopterAudioSubsystem::ResetFile(int32 Id)
{
	const SimCopterSound::FSoundSlot* Registered = SimCopterSound::GetSlot(Id);
	return Registered != nullptr && SetFile(Id, Registered->Wav, Registered->Dir);
}

void USimCopterAudioSubsystem::SetListener(const FVector& WorldLocation, const FRotator& WorldRotation)
{
	ListenerLocation = WorldLocation;
	ListenerRotation = WorldRotation;
}

void USimCopterAudioSubsystem::SetMasterVolume(int32 Volume)
{
	MasterVolume = FMath::Clamp(Volume, 0, 10000);
	for (int32 Id = 0; Id < SimCopterSound::NumSlots; ++Id)
	{
		ApplySlotVolume(Id);
	}
	ApplyRadioVolume();
}

// ---------------------------------------------------------------------------------------------
// People voice bank
// ---------------------------------------------------------------------------------------------

int32 USimCopterAudioSubsystem::AcquireVoiceSlot()
{
	for (int32 Id = SimCopterSound::VoiceBankFirst; Id <= SimCopterSound::VoiceBankLast; ++Id)
	{
		if (!Slots[Id].bVoiceBankInUse)
		{
			Slots[Id].bVoiceBankInUse = true;
			return Id;
		}
	}
	// All fourteen are speaking. The original drops the request the same way.
	return INDEX_NONE;
}

void USimCopterAudioSubsystem::ReleaseVoiceSlot(int32 Id)
{
	if (!SimCopterSound::IsVoiceBankSlot(Id))
	{
		return;
	}
	Stop(Id);
	Slots[Id].bVoiceBankInUse = false;
}

bool USimCopterAudioSubsystem::PlayVoiceEvent(
	int32 Slot,
	int32 VoiceEvent,
	const FVector& WorldLocation,
	int32 PitchDeltaHz,
	bool bNonPositional,
	int32 Flags)
{
	if (!SimCopterSound::IsVoiceBankSlot(Slot) || !bSoundsAvailable)
	{
		return false;
	}
	const SimCopterSound::FVoiceEvent* Event = SimCopterSound::GetVoiceEvent(VoiceEvent);
	if (Event == nullptr || Event->Clips.Num() == 0)
	{
		return false;
	}

	// FUN_004c5210 picks with FUN_004cea00(N), its own small-range RNG. The people simulation's
	// LFSR is not shared with it, so an engine random here is not a parity loss.
	const int32 Pick = FMath::RandRange(0, Event->Clips.Num() - 1);
	if (!SetFile(Slot, Event->Clips[Pick], SimCopterSound::ESoundDir::Root))
	{
		return false;
	}
	AddFrequency(Slot, PitchDeltaHz);
	// FUN_004c5210's tail: param_4 chooses between FUN_0042a1f0 (3D, culled by distance) and
	// FUN_0042a2a0 (2D, always at full volume).
	return bNonPositional ? Play2D(Slot, Flags) : Play3D(Slot, WorldLocation, Flags);
}

UAudioComponent* USimCopterAudioSubsystem::PlayAttachedVoiceLoop(
	const int32 VoiceEvent,
	USceneComponent* AttachParent,
	const int32 FrequencyHz,
	const float MaxRangeCm,
	const float VolumeMultiplier)
{
	UWorld* World = GetWorld();
	const SimCopterSound::FVoiceEvent* Event = SimCopterSound::GetVoiceEvent(VoiceEvent);
	if (World == nullptr || !bSoundsAvailable || AttachParent == nullptr || Event == nullptr || Event->Clips.Num() == 0 ||
		MaxRangeCm <= 0.0f || FVector::DistSquared(AttachParent->GetComponentLocation(), ListenerLocation) >= FMath::Square(MaxRangeCm))
	{
		return nullptr;
	}

	const int32 Pick = FMath::RandRange(0, Event->Clips.Num() - 1);
	const FSimCopterPcmClip* Clip = LoadClip(Event->Clips[Pick], SimCopterSound::ESoundDir::Root);
	if (Clip == nullptr)
	{
		return nullptr;
	}
	USoundWaveProcedural* Wave = MakeWave(*Clip, /*bLoop=*/true, this);
	if (Wave == nullptr)
	{
		return nullptr;
	}

	UAudioComponent* Component = NewObject<UAudioComponent>(this);
	if (Component == nullptr)
	{
		return nullptr;
	}
	Component->bAutoActivate = false;
	Component->bAutoDestroy = false;
	Component->bAllowSpatialization = true;
	Component->bOverrideAttenuation = true;
	Component->bStopWhenOwnerDestroyed = true;
	FSoundAttenuationSettings& Settings = Component->AttenuationOverrides;
	Settings.bAttenuate = true;
	Settings.bSpatialize = true;
	Settings.bAttenuateWithLPF = false;
	Settings.bEnableOcclusion = false;
	Settings.bEnableReverbSend = false;
	Settings.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
	Settings.AttenuationShape = EAttenuationShape::Sphere;
	Settings.AttenuationShapeExtents = FVector::ZeroVector;
	Settings.FalloffDistance = MaxRangeCm;
	Component->SetSound(Wave);
	Component->SetPitchMultiplier(FMath::Clamp(
		static_cast<float>(FMath::Clamp(FrequencyHz, GMinFrequencyHz, GMaxFrequencyHz)) /
		static_cast<float>(FMath::Max(Clip->SampleRate, 1)),
		GMinPitchMultiplier,
		GMaxPitchMultiplier));
	Component->SetVolumeMultiplier(FMath::Max(0.0f, VolumeMultiplier) * VolumeIndexToGain(MasterVolume));
	Component->RegisterComponentWithWorld(World);
	Component->AttachToComponent(AttachParent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	Component->Play();
	AttachedVoiceLoopComponents.Add(Component);
	return Component;
}

void USimCopterAudioSubsystem::SetAttachedVoiceLoopFrequencyHz(
	UAudioComponent* Component,
	const int32 VoiceEvent,
	const int32 FrequencyHz)
{
	const SimCopterSound::FVoiceEvent* Event = SimCopterSound::GetVoiceEvent(VoiceEvent);
	if (Component == nullptr || Event == nullptr || Event->Clips.Num() == 0)
	{
		return;
	}
	const FSimCopterPcmClip* Clip = LoadClip(Event->Clips[0], SimCopterSound::ESoundDir::Root);
	if (Clip == nullptr)
	{
		return;
	}
	Component->SetPitchMultiplier(FMath::Clamp(
		static_cast<float>(FMath::Clamp(FrequencyHz, GMinFrequencyHz, GMaxFrequencyHz)) /
		static_cast<float>(FMath::Max(Clip->SampleRate, 1)),
		GMinPitchMultiplier,
		GMaxPitchMultiplier));
}

void USimCopterAudioSubsystem::StopAttachedVoiceLoop(UAudioComponent* Component)
{
	if (Component == nullptr)
	{
		return;
	}
	AttachedVoiceLoopComponents.Remove(Component);
	Component->Stop();
	Component->DestroyComponent();
}

// ---------------------------------------------------------------------------------------------
// Standalone files (the front end's own sound objects)
// ---------------------------------------------------------------------------------------------

bool USimCopterAudioSubsystem::PlayFile2D(const FString& WavName, SimCopterSound::ESoundDir Dir, float VolumeMultiplier)
{
	UWorld* World = GetWorld();
	if (World == nullptr || !bSoundsAvailable)
	{
		return false;
	}

	const FSimCopterPcmClip* Clip = LoadClip(WavName, Dir);
	if (Clip == nullptr)
	{
		return false;
	}

	USoundWaveProcedural* Wave = MakeWave(*Clip, false, this);
	if (Wave == nullptr)
	{
		return false;
	}

	UAudioComponent* Component = NewObject<UAudioComponent>(this);
	if (Component == nullptr)
	{
		return false;
	}
	Component->bAutoActivate = false;
	Component->bAutoDestroy = false;
	Component->bAllowSpatialization = false;
	Component->bIsUISound = true;
	Component->SetSound(Wave);
	Component->SetVolumeMultiplier(VolumeMultiplier * VolumeIndexToGain(MasterVolume));
	Component->RegisterComponentWithWorld(World);
	Component->Play();

	LooseComponents.Add(Component);
	return true;
}

void USimCopterAudioSubsystem::StopStandaloneSounds()
{
	for (const TObjectPtr<UAudioComponent>& Component : LooseComponents)
	{
		if (Component != nullptr)
		{
			Component->Stop();
			Component->DestroyComponent();
		}
	}
	LooseComponents.Reset();
}

bool USimCopterAudioSubsystem::PlayMusicFile2D(
	const FString& WavName,
	SimCopterSound::ESoundDir Dir,
	float VolumeMultiplier)
{
	UWorld* World = GetWorld();
	if (World == nullptr || !bSoundsAvailable)
	{
		return false;
	}

	const FSimCopterPcmClip* Clip = LoadClip(WavName, Dir);
	if (Clip == nullptr)
	{
		return false;
	}

	USoundWaveProcedural* Wave = MakeWave(*Clip, /*bLoop=*/true, this);
	if (Wave == nullptr)
	{
		return false;
	}

	if (MusicComponent == nullptr)
	{
		MusicComponent = NewObject<UAudioComponent>(this);
		if (MusicComponent == nullptr)
		{
			return false;
		}
		MusicComponent->bAutoActivate = false;
		MusicComponent->bAutoDestroy = false;
		MusicComponent->bAllowSpatialization = false;
		MusicComponent->bIsUISound = true;
		MusicComponent->RegisterComponentWithWorld(World);
	}

	MusicComponent->Stop();
	MusicComponent->SetSound(Wave);
	MusicComponent->SetVolumeMultiplier(VolumeMultiplier * VolumeIndexToGain(MasterVolume));
	MusicComponent->Play();
	return true;
}

void USimCopterAudioSubsystem::StopMusic()
{
	if (MusicComponent != nullptr)
	{
		MusicComponent->Stop();
	}
}

bool USimCopterAudioSubsystem::IsMusicPlaying() const
{
	return MusicComponent != nullptr && MusicComponent->IsPlaying();
}
