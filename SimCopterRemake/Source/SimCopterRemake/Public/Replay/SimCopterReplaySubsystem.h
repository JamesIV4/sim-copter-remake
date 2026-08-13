// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Replay/SimCopterReplayTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "SimCopterReplaySubsystem.generated.h"

class AActor;
class ASimCopterGroundAgent;
class ASimCopterHelicopterPawn;
class ASimCopterReplayFreeCamera;
class ISimCopterReplayRecordable;

/** What the replay system is doing right now. */
UENUM()
enum class ESimCopterReplayState : uint8
{
	/** The panel is closed, or open with nothing recorded. The game plays normally. */
	Idle,
	/** Sampling the live world. The game still plays normally - you record by playing. */
	Recording,
	/** A clip is loaded and owns the world. The sim is paused and playback drives the puppets. */
	Reviewing,
};

/** Which camera the operator is looking through while the panel is open. */
UENUM()
enum class ESimCopterReplayCameraView : uint8
{
	Chase,
	Orbit,
	Rescue,
	Cockpit,
	/** The detached camera. Only reachable while the panel is open. */
	Free,
};

/** One clip on disk, as the load list shows it. */
struct SIMCOPTERREMAKE_API FSimCopterReplayClipSummary
{
	/** Base file name without the extension - what `LoadClip` takes. */
	FString FileName;
	FString DisplayName;
	FDateTime RecordedAtUtc = FDateTime(0);
	float DurationSeconds = 0.0f;
	int32 TrackCount = 0;
	int32 EventCount = 0;
};

/**
 * Records and replays what every actor in the city did.
 *
 * WHOLE-CLOTH ADDITION, not a port: SimCopter has no replay, no free camera and no clip files, so
 * nothing in here cites a `FUN_004xxxxx` and none of it uses the sim's 16.16 fixed point.
 *
 * It is a *snapshot* recorder, deliberately, not a deterministic re-simulation. Every 0.05 s it
 * asks each `ISimCopterReplayRecordable` in the world where it is and what pose it is in, and
 * stores a key only when that changed (see `FReplayActorState::DiffersFrom`). Playback pauses the
 * sim and pushes interpolated states back onto a set of puppets. The consequence worth knowing is
 * that a clip is a *recording*, not a save: scrubbing, reverse and 0.1x playback are all free and
 * cannot desync, but playing a clip does not re-run the simulation and nothing in it can be
 * changed after the fact.
 *
 * Alongside the motion, the clip carries an event track: the behaviour VM's own decisions, teed off
 * the `SIMCOPTER_PEOPLE_TRACE` macro that every shipped decision site already calls, plus the
 * mission layer's player-facing log. That is the "what did this actor decide" half of a replay, and
 * it comes for free because those call sites already exist.
 *
 * Clips are per level. `ResolveLevelId` derives an id from the session (career index or user city
 * file name) and clips are stored in a folder named for it, so the load list physically cannot
 * offer a clip from another city.
 */
UCLASS()
class SIMCOPTERREMAKE_API USimCopterReplaySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Null off the game thread, before the world exists, and in the front-end map. */
	static USimCopterReplaySubsystem* Get(const UObject* WorldContextObject);

	// --- UWorldSubsystem ---
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	// --- FTickableGameObject ---
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	/** Playback runs under the pause it installs itself, so this has to be true. */
	virtual bool IsTickableWhenPaused() const override { return true; }

	// --- the panel ---

	bool IsPanelOpen() const { return bPanelOpen; }
	void OpenPanel();
	/**
	 * Lowers the panel and hands the world back.
	 *
	 * A take that is still running KEEPS RUNNING. Recording is something you do by playing, so
	 * closing the panel to go and fly is the normal way to use it, not a reason to end the take -
	 * press Tab again and STOP when the shot is over. A review, by contrast, always ends here,
	 * because a review owns the world and holds the pause.
	 */
	void ClosePanel();
	void TogglePanel();

	ESimCopterReplayState GetState() const { return State; }

	// --- recording ---

	/** False in the front-end map, and while a clip is being reviewed. */
	bool CanStartRecording() const;
	void StartRecording();
	/** Ends the take and moves to Reviewing with the clip loaded. Harmless if not recording. */
	void StopRecording();
	/** Throws away the in-progress or in-view clip and returns to Idle. */
	void ResetClip();

	float GetRecordedSeconds() const;
	int64 GetClipMemoryBytes() const { return ClipMemoryBytes; }
	/** True once the take hit `SimCopter.Replay.MemoryBudgetMB` and stopped itself. */
	bool DidRecordingHitBudget() const { return bRecordingHitBudget; }

	// --- playback ---

	bool HasClip() const { return Clip.FrameCount > 0; }
	const SimCopterReplay::FReplayClip& GetClip() const { return Clip; }

	/**
	 * Ends the review and hands the world back - the pause is released, the stand-ins go, and the
	 * live population is put back exactly where it was. The clip stays in memory, so it can still
	 * be saved or reviewed again. This is the panel's CLOSE button and what Tab does on the way
	 * out; `ResetClip` is the one that throws the clip away.
	 */
	void ExitReview();

	/** Re-enters a review of the clip already in memory. The panel's REVIEW button. */
	void ReviewLoadedClip();
	bool CanReviewLoadedClip() const;

	bool IsPlaying() const { return bPlaying; }
	void Play();
	void Pause();
	void TogglePlayPause();
	void GoToStart();

	float GetPlayheadSeconds() const { return PlayheadSeconds; }
	/** Scrubbing. Clamped into the clip, and applied immediately whether playing or paused. */
	void SetPlayheadSeconds(float Seconds);
	float GetClipDurationSeconds() const { return Clip.GetDurationSeconds(); }

	/** 0.1x to 4x. 1 is the rate it was recorded at. */
	void SetPlaybackSpeed(float Speed);
	float GetPlaybackSpeed() const { return PlaybackSpeed; }
	static constexpr float MinPlaybackSpeed = 0.1f;
	static constexpr float MaxPlaybackSpeed = 4.0f;

	/** Events at or near the playhead, newest first, for the panel's event list. */
	void GetEventsAroundPlayhead(int32 MaxEvents, TArray<const SimCopterReplay::FReplayEvent*>& OutEvents) const;

	// --- clips on disk ---

	/** The id a clip must carry to be loadable here: "career:7", "user:islandtown.sc2". */
	static FString ResolveLevelId(const UObject* WorldContextObject, FString& OutDisplayName);

	/** The same identity spelled for a human ("Career City 7"), resolving it on first use. */
	const FString& GetLevelDisplayName();

	/** Writes the in-memory clip out under `DisplayName`. False with a player-facing reason. */
	bool SaveClip(const FString& DisplayName, FString& OutError);
	/** Every clip recorded in *this* level, newest first. */
	void GetClipSummaries(TArray<FSimCopterReplayClipSummary>& OutSummaries) const;
	bool LoadClip(const FString& FileName, FString& OutError);
	bool DeleteClip(const FString& FileName, FString& OutError);

	static FString NormalizeClipName(const FString& DisplayName);
	static bool IsClipNameValid(const FString& DisplayName, FString& OutError);
	static FString MakeClipFileName(const FString& DisplayName);

	// --- camera ---

	ESimCopterReplayCameraView GetCameraView() const { return CameraView; }
	void SetCameraView(ESimCopterReplayCameraView View);
	/** Chase -> Orbit -> Rescue -> Cockpit -> Free -> Chase. The panel's camera button, and C. */
	void CycleCameraView();
	bool IsFreeCameraActive() const
	{
		return IsReplayModeActive() && CameraView == ESimCopterReplayCameraView::Free;
	}
	ASimCopterReplayFreeCamera* GetFreeCamera() const { return FreeCamera; }

	/**
	 * Replay mode owns the camera whenever the panel is up OR a clip is being reviewed. Tab only
	 * shows and hides the controls, so a review watched full screen is still replay mode - and the
	 * free camera has to stay live through it, because framing the shot with the panel out of the
	 * way is the entire point.
	 */
	bool IsReplayModeActive() const
	{
		return bPanelOpen || State == ESimCopterReplayState::Reviewing;
	}

	bool IsSmoothCameraEnabled() const { return bSmoothCamera; }
	void SetSmoothCameraEnabled(bool bEnabled);
	void ToggleSmoothCamera() { SetSmoothCameraEnabled(!bSmoothCamera); }

	float GetFreeCameraFov() const;

	// --- HUD ---

	bool IsHudHidden() const { return bHudHidden; }
	void SetHudHidden(bool bHidden);
	void ToggleHudHidden() { SetHudHidden(!bHudHidden); }

	// --- panel plumbing ---

	/** Raised whenever anything the panel displays changes, so it can refresh without polling. */
	DECLARE_MULTICAST_DELEGATE(FOnReplayStateChanged);
	FOnReplayStateChanged& OnStateChanged() { return StateChanged; }

	/** Drops a named marker on the timeline at the playhead (or at the live recording head). */
	void AddBookmark(const FString& Text);

	/**
	 * One line describing everything the replay system currently believes, for the `SimReplayStatus`
	 * console command and for a bug report. Every transition is also logged to `LogSimCopterReplay`
	 * as it happens.
	 */
	FString DescribeState() const;

	/**
	 * Per-track detail for `SimReplayDump`: what the clip holds, whether a stand-in is bound to it,
	 * what the clip says the actor should be doing at the playhead, and where that actor actually
	 * is. Written for "the timeline runs but nothing moves", which can only be one of four things -
	 * no tracks, no keys, no puppet bound, or the apply not landing - and this separates them in one
	 * command.
	 */
	void DumpTracks(TArray<FString>& OutLines) const;

private:
	// --- recording ---

	void CaptureFrame();
	/** Interns the mnemonic table entry and appends a key when the state actually moved. */
	void RecordActorState(
		AActor& Actor,
		ISimCopterReplayRecordable& Recordable,
		int32 FrameIndex);
	void CloseOpenTracks();
	/**
	 * `bEnterReview` is false only when the panel is closing: stopping a take there should keep the
	 * clip but must not spawn a world of puppets on the way out just to tear them down again.
	 */
	void FinishRecording(bool bEnterReview);
	static void ReplayEventSink(const SimCopterReplay::FRecordedEvent& Event);
	void HandleEvent(const SimCopterReplay::FRecordedEvent& Event);

	/**
	 * Plays the clip's own recorded audio as the playhead crosses it.
	 *
	 * A review freezes the world, so the live mixer has nothing left to say - and whatever it was
	 * holding when the clip opened would drone underneath the replay at the wrong pitch. Review
	 * entry silences the game; this is what puts the clip's sound back.
	 *
	 * Only forward, continuous motion fires anything. A scrub or a jump re-anchors without playing,
	 * because replaying two seconds of a busy city's sound effects in one frame is a noise, not a
	 * replay.
	 */
	void FireSoundEventsForPlayhead(float PreviousFrame, float CurrentFrame);
	/** Frame the sound track was last serviced at, so crossings can be detected. */
	float LastSoundFrame = 0.0f;

	// --- playback ---

	void EnterReview();
	void LeaveReview();
	/** Pushes the clip's state at `PlayheadSeconds` onto the puppets. */
	void ApplyPlayhead();
	void BuildPuppets();
	void DestroyPuppets();
	/** Hides the live population so the puppets are the only thing on screen, and remembers it. */
	void SuspendLiveWorld();
	void RestoreLiveWorld();

	// --- camera ---

	void EnsureFreeCamera();
	void DestroyFreeCamera();
	void ApplyCameraView();
	/**
	 * Puts the view back on whatever the player is actually possessing and restores the camera mode
	 * the panel found. Deliberately NOT `ApplyCameraView` with the panel flag cleared: that route
	 * always targets the helicopter, which is wrong when the player is on foot.
	 */
	void RestoreCameraAfterPanel();
	ASimCopterHelicopterPawn* ResolvePlayerHelicopter() const;

	/**
	 * Stops the simulation for a review WITHOUT using `SetPause`.
	 *
	 * The pause looked like the obvious tool and was the wrong one, in two ways that both made the
	 * feature useless:
	 *
	 *  - A paused world stops COMPONENT ticks, so `USpringArmComponent` never recomputes its
	 *    socket. The helicopter was being moved along the clip correctly the whole time, but the
	 *    camera boom stayed frozen at the transform it held when the pause landed - which reads as
	 *    "the clip does not play". The free camera was dead for the same reason.
	 *  - It raises the engine's own PAUSED overlay in the middle of every shot.
	 *
	 * Near-zero global time dilation freezes gameplay just as hard while leaving the world
	 * unpaused, so component ticks, the camera manager and input all keep working normally. The
	 * two things that must NOT freeze - the playhead and the free camera - run on real time
	 * instead of world time.
	 */
	void FreezeWorldForReview();
	void ThawWorldAfterReview();

	void BroadcastChanged();

	static FString GetClipDirectory(const FString& LevelId);

	// --- state ---

	ESimCopterReplayState State = ESimCopterReplayState::Idle;
	bool bPanelOpen = false;

	SimCopterReplay::FReplayClip Clip;
	SimCopterReplay::FReplayMnemonicTable Mnemonics;
	int64 ClipMemoryBytes = 0;
	bool bRecordingHitBudget = false;

	/** Time owed to the recorder, so a 144 Hz frame rate still samples at exactly 20 Hz. */
	float RecordAccumulator = 0.0f;
	int32 RecordFrameIndex = 0;

	/** Live actor -> its track index, for the take in progress. */
	TMap<TObjectKey<AActor>, int32> TrackIndexByActor;
	/** The last state stored for each track, so `DiffersFrom` has something to compare against. */
	TArray<SimCopterReplay::FReplayActorState> LastStoredState;
	/** Tracks still open this frame; anything not touched has despawned and gets closed out. */
	TSet<int32> TouchedTracks;

	bool bPlaying = false;
	float PlayheadSeconds = 0.0f;
	float PlaybackSpeed = 1.0f;

	/**
	 * Track index -> the actor standing in for it. Parallel to `Clip.Tracks`, and entries may be
	 * null where a track has no stand-in (past the puppet cap, or a helicopter track in a world
	 * that has no helicopter).
	 */
	UPROPERTY()
	TArray<TObjectPtr<AActor>> Puppets;

	/**
	 * Only the actors playback actually spawned. `Puppets` also points at the player's own
	 * helicopter and on-foot pawn, which are borrowed and must never be destroyed.
	 */
	UPROPERTY()
	TArray<TObjectPtr<AActor>> SpawnedPuppets;

	/** What the live world looked like when review started, so leaving it puts everything back. */
	struct FSuspendedActor
	{
		TWeakObjectPtr<AActor> Actor;
		SimCopterReplay::FReplayActorState State;
		bool bWasHidden = false;
		bool bWasTickEnabled = true;
	};
	TArray<FSuspendedActor> SuspendedActors;

	UPROPERTY()
	TObjectPtr<ASimCopterReplayFreeCamera> FreeCamera;

	ESimCopterReplayCameraView CameraView = ESimCopterReplayCameraView::Chase;
	/** What the helicopter's view was when the panel opened, so closing it puts that back. */
	ESimCopterReplayCameraView CameraViewBeforePanel = ESimCopterReplayCameraView::Chase;
	bool bSmoothCamera = false;
	bool bHudHidden = false;

	/** Set while the review is holding the world frozen, so thawing is balanced. */
	bool bWorldFrozen = false;
	/** The dilation to put back when the review ends. */
	float TimeDilationBeforeReview = 1.0f;
	/** Real (undilated) clock the playhead advances on, because world time is frozen. */
	double LastPlaybackRealTimeSeconds = 0.0;

	FString LevelId;
	FString LevelDisplayName;

	FOnReplayStateChanged StateChanged;

	/** The instance the free-function event sink forwards to. One world, one recorder. */
	static USimCopterReplaySubsystem* ActiveRecorder;
};
