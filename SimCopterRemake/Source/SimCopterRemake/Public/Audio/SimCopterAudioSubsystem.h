// The remake's sound mixer, ported from SimCopter's own.
//
// The original owns exactly one DirectSound buffer per registered id (the array at 0x0055b1ac)
// and every gameplay system talks to it through nine free functions that index by id:
//
//   FUN_0042a2a0(id, flags)          Play2D          volume = master, no attenuation
//   FUN_0042a1f0(id, pos, flags)     Play3D          culled past 1920 units, then SetPosition
//   FUN_0042a310(id)                 Stop
//   FUN_0042a3a0(id)                 IsPlaying
//   FUN_0042a360(id, adj)            SetVolumeAdjust
//   FUN_0042a2f0(id, pos)            SetPosition     -> FUN_004247c0, the attenuation law
//   FUN_0042a330(id, delta)          AddFrequency    SetFreq(base + delta), NOT cumulative
//   FUN_0042a100(id, name)           SetFile         swap the WAV in a slot
//   FUN_0042a3b0(_, id, _)           queued play with a completion list
//
// Three consequences of "one buffer per id" that the port has to keep, because gameplay code
// leans on all of them:
//
//   * Playing an id that is already playing is a NO-OP. Every shipped call site passes flags
//     0 or 1, and FUN_0042a2a0 turns that into mode 2 ("if playing, return"). That is why the
//     original never stacks a second BOOM1 over the first, and why so many call sites are
//     written `if (!IsPlaying(id)) Play(id)` - the guard is redundant, not load-bearing.
//   * Stop(id) is global for that sound. There is no per-emitter handle to stop.
//   * A looping 3D sound is kept in place by the owner calling SetPosition every frame.
//
// See Docs/memory/simcopter-sound.md for the decode notes.

#pragma once

#include "CoreMinimal.h"
#include "Audio/SimCopterSoundTable.h"
#include "Subsystems/WorldSubsystem.h"
#include "SimCopterAudioSubsystem.generated.h"

class UAudioComponent;
class USceneComponent;
class USoundAttenuation;
class USoundWaveProcedural;

/** Flag bits of the `flags` argument shared by FUN_0042a2a0 and FUN_0042a1f0. */
namespace SimCopterSoundFlags
{
	/** bit0 -> the `loop` argument of the buffer's Play. */
	inline constexpr int32 Loop = 1;

	/**
	 * bit1 -> play mode 1 instead of 2, i.e. rewind to the start when the sound is already
	 * playing rather than leaving it alone. No shipped call site sets it; it is here so the
	 * flag word means the same thing it does in the original.
	 */
	inline constexpr int32 Restart = 2;
}

/** Decoded PCM for one original WAV, kept so a slot can be re-armed without touching disk. */
struct FSimCopterPcmClip
{
	TArray<uint8> Pcm16;
	int32 SampleRate = 0;
	int32 Channels = 0;
	float Duration = 0.0f;

	bool IsValid() const { return Pcm16.Num() > 0 && SampleRate > 0 && Channels > 0; }
};

/**
 * World-scoped mixer holding the 130 original sound slots.
 *
 * Get it with USimCopterAudioSubsystem::Get(WorldContext) - that returns null off the game
 * thread or before the world exists, and every call site is expected to null-check rather than
 * assume, because the checkup menu and the main menu run without one.
 */
UCLASS()
class SIMCOPTERREMAKE_API USimCopterAudioSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Convenience accessor; null when there is no world (tests, cook, CDO construction). */
	static USimCopterAudioSubsystem* Get(const UObject* WorldContextObject);

	// --- UWorldSubsystem ---
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	// --- FTickableGameObject ---
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bSoundsAvailable; }

	// --- the original's API, id for id ---

	/** SCHOOK: SoundPlay2D 0x0042a2a0. Returns false when the slot has no audio. */
	bool Play2D(int32 Id, int32 Flags = 0);

	/** SCHOOK: SoundPlay3D 0x0042a1f0. Culled past AudibleRangeUnits; returns false if culled. */
	bool Play3D(int32 Id, const FVector& WorldLocation, int32 Flags = 0);

	/** SCHOOK: SoundStop 0x0042a310. */
	void Stop(int32 Id);

	/** SCHOOK: SoundIsPlaying 0x0042a3a0. */
	bool IsPlaying(int32 Id) const;

	/**
	 * SCHOOK: SoundSetVolume 0x0042a360.
	 * Adjust is the original's signed offset from 10000; the stored index is Adjust + 10000 and
	 * the buffer is driven at (master * index) / 10000, in hundredths of a decibel below unity.
	 */
	void SetVolumeAdjust(int32 Id, int32 Adjust);

	/** SCHOOK: SoundSetPosition 0x0042a2f0 -> 0x004247c0. Recomputes this slot's volume. */
	void SetPosition(int32 Id, const FVector& WorldLocation);

	/** SCHOOK: SoundAddFrequency 0x0042a330. DeltaHz is added to the clip's own sample rate. */
	void AddFrequency(int32 Id, int32 DeltaHz);

	/**
	 * The buffer's own SetFrequency, vtable +0x68 - the absolute rate AddFrequency computes and
	 * hands on. FUN_004c5210 calls it directly to re-tune a voice that is already looping, which
	 * is how a medevac patient's EKG slows as their health drains without restarting the clip.
	 */
	void SetFrequencyHz(int32 Id, int32 Hz);

	/** Restore a slot to its registered sample rate (AddFrequency(id, 0) in the original). */
	void ResetFrequency(int32 Id);

	/** SCHOOK: SoundSetFile 0x0042a100. Loads WavName into the slot, stopping it first. */
	bool SetFile(int32 Id, const FString& WavName, SimCopterSound::ESoundDir Dir);

	/** Restore a slot's registered WAV after a SetFile. */
	bool ResetFile(int32 Id);

	// --- listener ---

	/**
	 * The listener the attenuation law measures against: DAT_0061a748..750 for the volume and
	 * DAT_0061a6ac..6b4 for Play3D's audibility cull. Those are two different globals in the
	 * original but track the same eye, so the port keeps one.
	 */
	void SetListener(const FVector& WorldLocation, const FRotator& WorldRotation);
	FVector GetListenerLocation() const { return ListenerLocation; }

	// --- people voice bank (0x71..0x7e) ---

	/**
	 * Claim a bank slot, or INDEX_NONE when all fourteen are speaking. Mirrors the allocator
	 * behind FUN_004c5210: the original keeps the in-use flags in DAT_0058dc42[] and stores the
	 * winner in person[0x172].
	 */
	int32 AcquireVoiceSlot();
	void ReleaseVoiceSlot(int32 Id);

	/**
	 * Play one people-voice event (the `switch (param_2)` of FUN_004c5210) out of Slot. Picks
	 * uniformly from the event's clips and SetFile()s the winner in. PitchDeltaHz is the
	 * original's per-person AddFrequency argument.
	 *
	 * bNonPositional is the handler's param_4: zero plays 3D at WorldLocation (FUN_0042a1f0),
	 * non-zero plays 2D (FUN_0042a2a0) so the sound is heard wherever the listener is - that is
	 * how an injured passenger's EKG reaches the cockpit. Flags carries the loop bit.
	 */
	bool PlayVoiceEvent(
		int32 Slot,
		int32 VoiceEvent,
		const FVector& WorldLocation,
		int32 PitchDeltaHz = 0,
		bool bNonPositional = false,
		int32 Flags = 0);

	/**
	 * A polyphonic attached loop for movement sounds. Unlike PlayVoiceEvent, this does not borrow
	 * one of the fourteen dialogue slots, so a person's footsteps and voice cannot replace each
	 * other. MaxRangeCm is a hard start/cutoff radius and the component's attenuation falloff.
	 */
	UAudioComponent* PlayAttachedVoiceLoop(
		int32 VoiceEvent,
		USceneComponent* AttachParent,
		int32 FrequencyHz,
		float MaxRangeCm,
		float VolumeMultiplier = 1.0f);
	void SetAttachedVoiceLoopFrequencyHz(UAudioComponent* Component, int32 VoiceEvent, int32 FrequencyHz);
	void StopAttachedVoiceLoop(UAudioComponent* Component);

	// --- dispatcher / radio voice queue (SCHOOK: DispatchVoicePlay 0x0042a3b0) ---

	/** Enqueues a dispatcher/radio voice sound ID to play sequentially. */
	void QueueRadioVoice(int32 SoundId);

	/** Clear any pending queued dispatcher voice announcements. */
	void ClearRadioVoiceQueue();

	/** True if a dispatcher voice clip is currently playing or queued. */
	bool IsRadioVoicePlayingOrQueued() const;

	// --- one-off files outside the table ---

	/**
	 * Play a WAV that the original owns as a standalone sound object rather than a table slot:
	 * the front-end screens each construct their own (menu.wav, career.wav, carsel.wav,
	 * hangar.wav, button.wav, MBoxCht.wav, blast.wav). Unlike a slot, these are polyphonic -
	 * there is no shared buffer to collide over.
	 */
	bool PlayFile2D(const FString& WavName, SimCopterSound::ESoundDir Dir, float VolumeMultiplier = 1.0f);

	/** Stop and discard all one-shot standalone sounds owned by the current front-end screen. */
	void StopStandaloneSounds();

	/**
	 * The looping half of the same idea, for the one standalone sound object the original plays
	 * with `Play(1, 1)` instead of `Play(0, 1)`: the main menu's menuback.wav, started by
	 * FUN_0045f3d0 and stopped by the page's [vt+0xfc] (FUN_0045f630) when the menu goes away.
	 * One voice, like the original's - starting a second track replaces the first.
	 */
	bool PlayMusicFile2D(const FString& WavName, SimCopterSound::ESoundDir Dir, float VolumeMultiplier = 1.0f);
	void StopMusic();
	bool IsMusicPlaying() const;

	// --- mixer state ---

	// --- the radio's own channel ---
	//
	// The radio is not a table slot. In the original it is a separate object with its own buffer
	// (`FUN_004306e0`'s construction, volume defaulting to 10000), which is why a station can
	// keep playing underneath every effect. It also streams things two orders of magnitude
	// larger than any effect - a music track is five minutes - so these deliberately do NOT go
	// through the clip cache; each play decodes once and drops the samples when it ends.

	/** Absolute path, because the radio tree is nested well below the slot search roots. */
	bool PlayRadioFile(const FString& AbsolutePath, float VolumeMultiplier = 1.0f);
	/** Updates both the stored radio gain and the item that is already playing. */
	void SetRadioVolumeMultiplier(float VolumeMultiplier);
	float GetRadioVolumeMultiplier() const { return RadioVolumeMultiplier; }
	void StopRadio();
	bool IsRadioPlaying() const;

	/** Seconds left of the current radio item, 0 when nothing is playing. */
	float GetRadioRemainingSeconds() const;

	/** DAT_00519cc0, the master effects volume index in [0, 10000]. */
	void SetMasterVolume(int32 Volume);
	int32 GetMasterVolume() const { return MasterVolume; }

	/**
	 * Silences everything the live game is playing, for a replay review.
	 *
	 * A review freezes the world, so nothing is going to stop these normally: the rotor loop, the
	 * sirens, a fire, the radio and every attached walking voice would all hang at whatever they
	 * were doing when the clip opened and drone under the replay. The clip plays its own recorded
	 * sound events instead. Live audio comes back on its own when the world thaws and the systems
	 * that own these loops tick again.
	 */
	void SilenceForReplayReview();

	/**
	 * Every positional slot that is currently making a sound, with where it is. The replay recorder
	 * samples this once per recorded frame: a loop is started once and then re-aimed every tick, so
	 * without the moves a clip would pin the rotor to wherever the aircraft was when it started.
	 */
	void GetActivePositionalSounds(TArray<TPair<int32, FVector>>& OutSounds) const;

	/** True once the original sound folder was found and at least one clip loaded. */
	bool AreSoundsAvailable() const { return bSoundsAvailable; }

	/** Absolute path of the resolved `sound` folder, or empty. */
	const FString& GetSoundRoot() const { return SoundRoot; }

	// --- the ported arithmetic, exposed for tests ---

	/**
	 * SCHOOK: VecOctagonalNorm 0x00468220 - largest component plus a quarter of the other two.
	 * Play3D's audibility test uses this, not a true length, so the audible region is an
	 * octagon rather than a sphere. Inputs and result are 16.16 original units.
	 */
	static int32 OctagonalNorm1616(int32 X, int32 Y, int32 Z);

	/**
	 * SCHOOK: SoundDistanceVolume 0x004247c0 - the volume index for a distance in original
	 * units: 10000 at the listener falling linearly to 6000 at the 1920-unit cull edge.
	 * Not clamped below; SetVolumeAdjust clamps into [0, 10000] as the original does.
	 */
	static int32 DistanceVolumeIndex(float DistanceUnits);

	/** Volume index (hundredths of a dB below unity) -> a linear gain for UAudioComponent. */
	static float VolumeIndexToGain(int32 Index);

	/** FUN_0042a1f0's cull radius: `if (0x7800000 <= dist) return 0`, i.e. 1920.0 units. */
	static constexpr float AudibleRangeUnits = 1920.0f;

	/** FUN_004247c0's slope: -4000 volume index over the full range = -40 dB. */
	static constexpr int32 DistanceAttenuationSpan = 4000;

	/** One original unit in Unreal centimetres, matching FSimCopterEffectFX::OriginalUnitToCm. */
	static constexpr float OriginalUnitToCm = 6.25f;

private:
	/** Runtime state of one of the 130 slots. The UAudioComponent lives in SlotComponents. */
	struct FSlot
	{
		FSimCopterPcmClip Clip;

		/** Name currently loaded, so ResetFile / a repeat SetFile can skip the work. */
		FString LoadedWav;

		/** true once we have tried to load this slot, so a missing file is not retried forever. */
		bool bLoadAttempted = false;

		/** The original's obj[9]: the volume index, 10000 = unity. */
		int32 VolumeIndex = 10000;

		bool bLooping = false;
		bool bPositional = false;
		FVector Location = FVector::ZeroVector;

		/**
		 * When a one-shot is expected to end. The original asks DirectSound; a procedural wave
		 * plays silence forever instead, so the port tracks the deadline itself.
		 */
		double OneShotEndTime = 0.0;

		float PitchMultiplier = 1.0f;
		bool bVoiceBankInUse = false;
	};

	FSlot Slots[SimCopterSound::NumSlots];

	/**
	 * One component per slot, mirroring the original's one DirectSound buffer per id. Held in a
	 * UPROPERTY rather than inside FSlot because FSlot is a plain struct the collector does not
	 * walk; indexed by sound id and sized lazily.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> SlotComponents;

	/** Resolved `.../sound`, or empty when the original install could not be found. */
	FString SoundRoot;

	/** Language subfolder under SoundRoot; "English" in every shipped build. */
	FString LanguageDir = TEXT("English");

	bool bSoundsAvailable = false;

	/** DAT_00519cc0. */
	int32 MasterVolume = 10000;

	FVector ListenerLocation = FVector::ZeroVector;
	FRotator ListenerRotation = FRotator::ZeroRotator;

	/**
	 * Distance attenuation is ours, not the mixer's: the component only ever gets a volume
	 * multiplier we computed from FUN_004247c0. This asset therefore has bAttenuate off and
	 * exists purely to turn spatialisation on so the listener still hears direction.
	 */
	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> SpatialAttenuation = nullptr;

	/** Components spawned by PlayFile2D, reaped when they finish. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> LooseComponents;

	/** Polyphonic movement loops; separate so front-end standalone cleanup cannot stop them. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> AttachedVoiceLoopComponents;

	/** The radio's single voice. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> RadioComponent = nullptr;
	float RadioVolumeMultiplier = 1.0f;

	/** The front end's single looping music voice (menuback.wav). */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MusicComponent = nullptr;

	double RadioEndTime = 0.0;

	/** Dispatcher voice queue (SCHOOK: DispatchVoicePlay 0x0042a3b0). */
	TArray<int32> DispatchVoiceQueue;
	int32 CurrentDispatchVoiceId = INDEX_NONE;
	double CurrentDispatchVoiceEndTime = 0.0;

	/** Clips loaded by PlayFile2D / SetFile, keyed by lowercase relative path. */
	TMap<FString, FSimCopterPcmClip> ClipCache;

	FString ResolveSoundRoot() const;
	FString ResolveWavPath(const FString& WavName, SimCopterSound::ESoundDir Dir) const;
	const FSimCopterPcmClip* LoadClip(const FString& WavName, SimCopterSound::ESoundDir Dir);

	/** Decode one RIFF/WAVE file. Shared by the cached slot loader and the uncached radio one. */
	static bool DecodeWav(const FString& AbsolutePath, FSimCopterPcmClip& OutClip);
	bool EnsureSlotLoaded(int32 Id);
	UAudioComponent* EnsureSlotComponent(int32 Id);

	/** Arms a fresh procedural wave from Clip and starts Component. */
	bool StartSlot(int32 Id, bool bLoop);

	/** Applies VolumeIndex * master to the component. */
	void ApplySlotVolume(int32 Id);
	void ApplyRadioVolume();

	static USoundWaveProcedural* MakeWave(const FSimCopterPcmClip& Clip, bool bLoop, UObject* Outer);
};
