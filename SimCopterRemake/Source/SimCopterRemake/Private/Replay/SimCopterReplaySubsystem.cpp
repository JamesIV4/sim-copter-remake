// Copyright Epic Games, Inc. All Rights Reserved.

#include "Replay/SimCopterReplaySubsystem.h"

#include "Audio/SimCopterAudioSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Flight/SimCopterHelicopterPawn.h"
#include "Formats/SimCopterOriginalGamePaths.h"
#include "Game/SimCopterPlayerController.h"
#include "Game/SimCopterSessionSubsystem.h"
#include "Ground/SimCopterAmbientVehicles.h"
#include "Ground/SimCopterGroundAgent.h"
#include "Ground/SimCopterTrafficSystemActor.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Crc.h"
#include "Misc/Paths.h"
#include "Missions/SimCopterMissionSystemActor.h"
#include "Stats/Stats.h"
#include "Replay/SimCopterReplayFreeCamera.h"
#include "Replay/SimCopterReplayRecordable.h"
#include "Serialization/Archive.h"

USimCopterReplaySubsystem* USimCopterReplaySubsystem::ActiveRecorder = nullptr;

namespace
{
/**
 * How much a single take may hold before it stops itself. A busy city records at roughly 25 MB a
 * minute, so the default is about twenty minutes - well past any shot anyone frames, and far short
 * of what would start swapping.
 */
TAutoConsoleVariable<int32> CVarReplayMemoryBudgetMB(
	TEXT("SimCopter.Replay.MemoryBudgetMB"),
	512,
	TEXT("Memory a single replay take may use before recording stops itself, in megabytes."),
	ECVF_Default);

/**
 * Off by default: the behaviour VM executes thousands of records a second across the city, and the
 * opcode track is only useful when you are chasing one person's program.
 */
TAutoConsoleVariable<int32> CVarReplayRecordOpcodes(
	TEXT("SimCopter.Replay.RecordOpcodes"),
	0,
	TEXT("Record every behaviour-VM opcode into the replay event track. Very chatty."),
	ECVF_Default);

/**
 * A clip records everything in the world, but reviewing one has to spawn a stand-in per track and
 * a privanim figure is a procedural mesh build apiece. The cap is what stops a pathological clip
 * from hitching for ten seconds when the operator presses Play.
 */
TAutoConsoleVariable<int32> CVarReplayMaxPuppets(
	TEXT("SimCopter.Replay.MaxPuppets"),
	600,
	TEXT("Most stand-in actors playback will spawn for a clip."),
	ECVF_Default);

/** Stops one frame's worth of trace spam from swallowing the whole memory budget. */
constexpr int32 MaxEventsPerFrame = 96;

/**
 * What a review sets global time dilation to. Not zero, because the engine clamps against
 * `AWorldSettings::MinGlobalTimeDilation` (0.0001 by default) and would silently correct it; at
 * this rate a ten-minute review advances the simulation by well under one frame.
 */
constexpr float ReviewTimeDilation = 0.0001f;

// The first four replay views ARE the helicopter's four camera modes, and they are converted by
// value so the panel's camera button and the C key stay one control. Free is the fifth and has no
// counterpart on the pawn.
static_assert(
	static_cast<uint8>(ESimCopterReplayCameraView::Chase) == static_cast<uint8>(ESimCopterCameraMode::Chase)
	&& static_cast<uint8>(ESimCopterReplayCameraView::Orbit) == static_cast<uint8>(ESimCopterCameraMode::Orbit)
	&& static_cast<uint8>(ESimCopterReplayCameraView::Rescue) == static_cast<uint8>(ESimCopterCameraMode::Rescue)
	&& static_cast<uint8>(ESimCopterReplayCameraView::Cockpit) == static_cast<uint8>(ESimCopterCameraMode::Cockpit),
	"ESimCopterReplayCameraView's first four entries must line up with ESimCopterCameraMode.");

FString SanitizeForFileSystem(const FString& In)
{
	FString Out;
	Out.Reserve(In.Len());
	for (const TCHAR Character : In)
	{
		Out.AppendChar(FChar::IsAlnum(Character) ? Character : TEXT('_'));
	}
	while (Out.Contains(TEXT("__")))
	{
		Out.ReplaceInline(TEXT("__"), TEXT("_"));
	}
	bool bTrimmed = false;
	do
	{
		bTrimmed = false;
		Out.TrimCharInline(TEXT('_'), &bTrimmed);
	}
	while (bTrimmed);
	return Out;
}
}

// ---------------------------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------------------------

USimCopterReplaySubsystem* USimCopterReplaySubsystem::Get(const UObject* WorldContextObject)
{
	if (WorldContextObject == nullptr || !IsInGameThread())
	{
		return nullptr;
	}
	const UWorld* World = GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World != nullptr ? World->GetSubsystem<USimCopterReplaySubsystem>() : nullptr;
}

void USimCopterReplaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Deliberately NOT resolved here. A `UWorldSubsystem::Initialize` runs before the city actor
	// has placed anything and, for the game-instance subsystems it would need, before
	// `OnPostEngineInit` - the trap `Docs/memory/simcopter-dlss-resolution-scale.md` documents at
	// length. The level id is resolved lazily, the first time the panel is opened.
	LevelId.Reset();
	LevelDisplayName.Reset();
}

void USimCopterReplaySubsystem::Deinitialize()
{
	// The event sink is a raw function pointer into a global. Leaving it pointing at a subsystem
	// that is going away would crash the next world's first traced decision.
	if (ActiveRecorder == this)
	{
		SimCopterReplay::SetEventSink(nullptr);
		SimCopterReplay::GRecordingEvents = false;
		SimCopterReplay::GRecordingOpcodeEvents = false;
		ActiveRecorder = nullptr;
	}

	// Level teardown destroys the actors for us, but the time dilation belongs to the world settings
	// and would be inherited by whatever runs next.
	ThawWorldAfterReview();
	DestroyPuppets();
	DestroyFreeCamera();
	SuspendedActors.Reset();
	State = ESimCopterReplayState::Idle;
	bPanelOpen = false;

	Super::Deinitialize();
}

bool USimCopterReplaySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USimCopterReplaySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USimCopterReplaySubsystem, STATGROUP_Tickables);
}

bool USimCopterReplaySubsystem::IsTickable() const
{
	return Super::IsTickable() && (bPanelOpen || State != ESimCopterReplayState::Idle);
}

void USimCopterReplaySubsystem::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// REAL time, because a review freezes the world with near-zero global time dilation and every
	// delta the engine hands out during one is ~0. Recording still uses the world's own delta: a
	// take should follow game time, so opening the Settings screen mid-take does not record frames
	// of a stopped world.
	const double NowRealSeconds = FPlatformTime::Seconds();
	const float RealDeltaSeconds = LastPlaybackRealTimeSeconds > 0.0
		? static_cast<float>(FMath::Clamp(NowRealSeconds - LastPlaybackRealTimeSeconds, 0.0, 0.25))
		: 0.0f;
	LastPlaybackRealTimeSeconds = NowRealSeconds;

	if (State == ESimCopterReplayState::Recording)
	{
		// Fixed-step, so a 30 fps machine and a 240 fps one produce the same clip. The accumulator
		// is capped rather than drained: a two-second hitch should drop frames, not record eight
		// identical ones back to back.
		RecordAccumulator += FMath::Min(DeltaSeconds, 0.5f);
		while (RecordAccumulator >= Clip.FrameIntervalSeconds)
		{
			RecordAccumulator -= Clip.FrameIntervalSeconds;
			CaptureFrame();
			if (State != ESimCopterReplayState::Recording)
			{
				// The budget ran out inside CaptureFrame.
				break;
			}
		}
	}
	else if (State == ESimCopterReplayState::Reviewing && bPlaying)
	{
		const float Duration = Clip.GetDurationSeconds();
		PlayheadSeconds += RealDeltaSeconds * PlaybackSpeed;
		if (PlayheadSeconds >= Duration)
		{
			// Stop at the end rather than looping: a loop makes it impossible to tell a clip that
			// ended from one that is still running, and the operator can press play again.
			PlayheadSeconds = Duration;
			bPlaying = false;
			BroadcastChanged();
		}
		ApplyPlayhead();
	}

	// The helicopter's own camera rig (the boom, the ground lift, the zoom framing) is driven from
	// its Tick, and the review has that switched off so the flight model cannot fight the clip. It
	// gets REAL delta: every interpolation in there (the smoothed view rotation especially) is an
	// FMath::RInterpTo, which returns its input unchanged at delta zero.
	if (State == ESimCopterReplayState::Reviewing && CameraView != ESimCopterReplayCameraView::Free)
	{
		if (ASimCopterHelicopterPawn* Helicopter = ResolvePlayerHelicopter())
		{
			Helicopter->UpdateCameraForReplay(RealDeltaSeconds);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// The panel
// ---------------------------------------------------------------------------------------------

void USimCopterReplaySubsystem::OpenPanel()
{
	if (bPanelOpen)
	{
		return;
	}
	// Whether replay mode was ALREADY running - a take in progress, or a review being watched with
	// the panel down. Tab is show/hide for the controls, so re-opening the panel over one of those
	// must not disturb anything, least of all the camera the operator has just framed.
	const bool bResumingExistingSession = IsReplayModeActive();
	bPanelOpen = true;

	if (LevelId.IsEmpty())
	{
		LevelId = ResolveLevelId(this, LevelDisplayName);
	}

	if (!bResumingExistingSession)
	{
		// Fresh entry into replay mode: start on whatever view the player was already flying, and
		// remember it so leaving hands the same one back.
		if (const ASimCopterHelicopterPawn* Helicopter = ResolvePlayerHelicopter())
		{
			CameraView = static_cast<ESimCopterReplayCameraView>(Helicopter->GetCameraMode());
		}
		CameraViewBeforePanel = CameraView;
	}
	EnsureFreeCamera();

	UE_LOG(LogSimCopterReplay, Log, TEXT("Panel opened. %s"), *DescribeState());
	BroadcastChanged();
}

void USimCopterReplaySubsystem::ClosePanel()
{
	if (!bPanelOpen)
	{
		return;
	}

	// TAB IS SHOW/HIDE FOR THE CONTROLS. It never changes what the system is doing: a take keeps
	// recording and a review keeps playing, because the point of getting the panel out of the way
	// is to WATCH - full screen, HUD off, flying the free camera round the shot. Ending a review is
	// the CLOSE button's job, and only CLOSE hands the world back.
	bPanelOpen = false;

	if (IsReplayModeActive())
	{
		// Still recording or still reviewing, so the camera - including a free camera the operator
		// has just framed a shot with - stays exactly as it is. An on-screen indicator takes over
		// from the panel so it is clear the clip is still running and Tab brings the controls back.
		UE_LOG(
			LogSimCopterReplay,
			Log,
			TEXT("Panel hidden, replay mode continues. %s"),
			*DescribeState());
		BroadcastChanged();
		return;
	}

	// Not reviewing: the panel going away really is the end of replay mode.
	//
	// The HUD is the player's, not the panel's: it must never be left hidden by a mode that is gone.
	SetHudHidden(false);

	// Order matters, and getting it wrong is what left the view stuck on a detached camera: the
	// view target has to move OFF the free camera before the actor is destroyed, or the camera
	// manager spends a frame pointing at a destroyed actor and never re-resolves.
	CameraView = CameraViewBeforePanel;
	RestoreCameraAfterPanel();
	DestroyFreeCamera();

	UE_LOG(LogSimCopterReplay, Log, TEXT("Panel closed. %s"), *DescribeState());
	BroadcastChanged();
}

void USimCopterReplaySubsystem::TogglePanel()
{
	if (bPanelOpen)
	{
		ClosePanel();
	}
	else
	{
		OpenPanel();
	}
}

// ---------------------------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------------------------

bool USimCopterReplaySubsystem::CanStartRecording() const
{
	return bPanelOpen
		&& State != ESimCopterReplayState::Recording
		&& State != ESimCopterReplayState::Reviewing
		&& GetWorld() != nullptr;
}

void USimCopterReplaySubsystem::StartRecording()
{
	if (!CanStartRecording())
	{
		return;
	}

	ResetClip();

	Clip.FrameIntervalSeconds = SimCopterReplay::FrameIntervalSeconds;
	Clip.LevelId = LevelId;
	Clip.LevelDisplayName = LevelDisplayName;
	Clip.RecordedAtUtc = FDateTime::UtcNow();
	RecordAccumulator = 0.0f;
	RecordFrameIndex = 0;
	ClipMemoryBytes = 0;
	bRecordingHitBudget = false;
	State = ESimCopterReplayState::Recording;

	// Open the event funnel. `GRecordingOpcodeEvents` is read once here rather than per event so
	// flipping the CVar mid-take cannot produce a clip that is half one thing and half another.
	ActiveRecorder = this;
	SimCopterReplay::SetEventSink(&USimCopterReplaySubsystem::ReplayEventSink);
	SimCopterReplay::GRecordingEvents = true;
	SimCopterReplay::GRecordingOpcodeEvents = CVarReplayRecordOpcodes.GetValueOnGameThread() != 0;

	// Frame zero right away, so the very first thing the clip shows is the world as it was when
	// Record was pressed rather than 50 ms later.
	CaptureFrame();
	UE_LOG(LogSimCopterReplay, Log, TEXT("Recording started. %s"), *DescribeState());
	BroadcastChanged();
}

void USimCopterReplaySubsystem::StopRecording()
{
	FinishRecording(/*bEnterReview=*/true);
}

void USimCopterReplaySubsystem::FinishRecording(const bool bEnterReview)
{
	if (State != ESimCopterReplayState::Recording)
	{
		return;
	}

	SimCopterReplay::GRecordingEvents = false;
	SimCopterReplay::GRecordingOpcodeEvents = false;
	SimCopterReplay::SetEventSink(nullptr);
	ActiveRecorder = nullptr;

	CloseOpenTracks();
	Clip.FrameCount = RecordFrameIndex;
	Clip.ClipMnemonics = Mnemonics.GetNames();

	// Tracks with no keys at all are actors that existed but never came into the take (an agent
	// spawned and despawned inside one frame). They would spawn a puppet that is never shown.
	Clip.Tracks.RemoveAll([](const SimCopterReplay::FReplayActorTrack& Track)
	{
		return Track.Keys.Num() == 0;
	});

	TrackIndexByActor.Reset();
	LastStoredState.Reset();
	TouchedTracks.Reset();

	State = ESimCopterReplayState::Idle;
	UE_LOG(
		LogSimCopterReplay,
		Log,
		TEXT("Recording stopped after %d frames (%.2fs), %d tracks, %d events, %.1f MB. enterReview=%d"),
		Clip.FrameCount,
		Clip.GetDurationSeconds(),
		Clip.Tracks.Num(),
		Clip.Events.Num(),
		static_cast<double>(ClipMemoryBytes) / (1024.0 * 1024.0),
		bEnterReview ? 1 : 0);

	if (bEnterReview && Clip.FrameCount > 0)
	{
		EnterReview();
	}
	BroadcastChanged();
}

void USimCopterReplaySubsystem::ResetClip()
{
	if (State == ESimCopterReplayState::Recording)
	{
		SimCopterReplay::GRecordingEvents = false;
		SimCopterReplay::GRecordingOpcodeEvents = false;
		SimCopterReplay::SetEventSink(nullptr);
		ActiveRecorder = nullptr;
	}
	if (State == ESimCopterReplayState::Reviewing)
	{
		LeaveReview();
	}

	Clip.Reset();
	Mnemonics.Reset();
	TrackIndexByActor.Reset();
	LastStoredState.Reset();
	TouchedTracks.Reset();
	ClipMemoryBytes = 0;
	bRecordingHitBudget = false;
	RecordAccumulator = 0.0f;
	RecordFrameIndex = 0;
	PlayheadSeconds = 0.0f;
	bPlaying = false;
	State = ESimCopterReplayState::Idle;
	BroadcastChanged();
}

float USimCopterReplaySubsystem::GetRecordedSeconds() const
{
	return static_cast<float>(FMath::Max(RecordFrameIndex - 1, 0)) * Clip.FrameIntervalSeconds;
}

void USimCopterReplaySubsystem::CaptureFrame()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const int32 FrameIndex = RecordFrameIndex++;
	TouchedTracks.Reset();

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor == nullptr || Actor->IsActorBeingDestroyed())
		{
			continue;
		}
		ISimCopterReplayRecordable* Recordable = Cast<ISimCopterReplayRecordable>(Actor);
		if (Recordable == nullptr)
		{
			continue;
		}
		RecordActorState(*Actor, *Recordable, FrameIndex);
	}

	// Anything that had a track and was not seen this frame has despawned. Its track ends on the
	// previous frame, and the next sighting of the same actor pointer opens a new one - the engine
	// reuses actors out of a pool, and a recycled body is a different person.
	for (auto TrackIt = TrackIndexByActor.CreateIterator(); TrackIt; ++TrackIt)
	{
		if (!TouchedTracks.Contains(TrackIt.Value()))
		{
			TrackIt.RemoveCurrent();
		}
	}

	// Where every positional loop is THIS frame. A loop is started once and then re-aimed every
	// tick by whoever owns it, so recording only the start would pin the rotor to wherever the
	// aircraft was when the take began - which is exactly what made the helicopter sound like it
	// was next to the listener for the whole replay.
	if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
	{
		TArray<TPair<int32, FVector>> ActiveSounds;
		Audio->GetActivePositionalSounds(ActiveSounds);
		for (const TPair<int32, FVector>& Sound : ActiveSounds)
		{
			SimCopterReplay::FReplayEvent& Move = Clip.Events.AddDefaulted_GetRef();
			Move.FrameIndex = FrameIndex;
			Move.Kind = SimCopterReplay::EReplayEventKind::SoundMove;
			Move.PayloadA = Sound.Key;
			Move.WorldLocationCm = FVector3f(Sound.Value);
		}
	}

	ClipMemoryBytes = Clip.GetApproximateMemoryBytes();
	const int64 BudgetBytes = static_cast<int64>(FMath::Max(CVarReplayMemoryBudgetMB.GetValueOnGameThread(), 1))
		* 1024 * 1024;
	if (ClipMemoryBytes >= BudgetBytes)
	{
		UE_LOG(
			LogSimCopterReplay,
			Warning,
			TEXT("Replay take stopped: it reached the %lld MB budget (SimCopter.Replay.MemoryBudgetMB) after %.1f seconds."),
			BudgetBytes / (1024 * 1024),
			GetRecordedSeconds());
		bRecordingHitBudget = true;
		StopRecording();
	}
}

void USimCopterReplaySubsystem::RecordActorState(
	AActor& Actor,
	ISimCopterReplayRecordable& Recordable,
	const int32 FrameIndex)
{
	int32 TrackIndex = INDEX_NONE;
	if (const int32* Existing = TrackIndexByActor.Find(TObjectKey<AActor>(&Actor)))
	{
		TrackIndex = *Existing;
	}
	else
	{
		TrackIndex = Clip.Tracks.AddDefaulted();
		SimCopterReplay::FReplayActorTrack& NewTrack = Clip.Tracks[TrackIndex];
		// Ids are handed out in track order and never reused, so a clip is self-describing: nothing
		// in the file refers to an engine name or pointer that stops meaning anything once the take
		// is over.
		NewTrack.ActorId = static_cast<uint32>(TrackIndex) + 1;
		NewTrack.Kind = Recordable.GetReplayActorKind();
		NewTrack.Label = Recordable.GetReplayLabel();
		NewTrack.PersonState = Recordable.GetReplayPersonState();
		Recordable.GetReplaySpawnDescriptor(NewTrack.Spawn);
		NewTrack.FirstFrame = FrameIndex;
		NewTrack.LastFrame = FrameIndex;

		TrackIndexByActor.Add(TObjectKey<AActor>(&Actor), TrackIndex);
		LastStoredState.SetNum(Clip.Tracks.Num());
	}

	TouchedTracks.Add(TrackIndex);

	SimCopterReplay::FReplayActorTrack& Track = Clip.Tracks[TrackIndex];
	SimCopterReplay::FReplayActorState NewState;
	Recordable.CaptureReplayState(Mnemonics, NewState);
	Track.LastFrame = FrameIndex;

	// The sparse rule: a key only exists where something changed. A parked car costs one key for
	// the whole take, and playback's interpolation reconstructs every frame between two keys
	// exactly - the state did not move, so neither does the reconstruction.
	if (Track.Keys.Num() > 0 && !NewState.DiffersFrom(LastStoredState[TrackIndex]))
	{
		return;
	}

	// One holding key at the previous frame before a change, so the actor sits still until the
	// instant it moves instead of drifting across the whole gap. Without this a car that is parked
	// for ten seconds and then pulls away appears to have been creeping the entire time.
	if (Track.Keys.Num() > 0 && Track.Keys.Last().FrameIndex < FrameIndex - 1)
	{
		SimCopterReplay::FReplayKey& Hold = Track.Keys.AddDefaulted_GetRef();
		Hold.FrameIndex = FrameIndex - 1;
		Hold.State = LastStoredState[TrackIndex];
	}

	SimCopterReplay::FReplayKey& Key = Track.Keys.AddDefaulted_GetRef();
	Key.FrameIndex = FrameIndex;
	Key.State = NewState;
	LastStoredState[TrackIndex] = NewState;
}

void USimCopterReplaySubsystem::CloseOpenTracks()
{
	// A track whose last key predates the end of the take needs one more key so the actor holds its
	// final pose to the end rather than being interpolated from wherever it last moved.
	const int32 LastFrame = FMath::Max(RecordFrameIndex - 1, 0);
	for (const TPair<TObjectKey<AActor>, int32>& Pair : TrackIndexByActor)
	{
		if (!Clip.Tracks.IsValidIndex(Pair.Value))
		{
			continue;
		}
		SimCopterReplay::FReplayActorTrack& Track = Clip.Tracks[Pair.Value];
		if (Track.Keys.Num() > 0 && Track.Keys.Last().FrameIndex < LastFrame)
		{
			SimCopterReplay::FReplayKey& Key = Track.Keys.AddDefaulted_GetRef();
			Key.FrameIndex = LastFrame;
			Key.State = Track.Keys.Last().State;
		}
		Track.LastFrame = LastFrame;
	}
}

// ---------------------------------------------------------------------------------------------
// The event track
// ---------------------------------------------------------------------------------------------

void USimCopterReplaySubsystem::ReplayEventSink(const SimCopterReplay::FRecordedEvent& Event)
{
	if (ActiveRecorder != nullptr)
	{
		ActiveRecorder->HandleEvent(Event);
	}
}

void USimCopterReplaySubsystem::HandleEvent(const SimCopterReplay::FRecordedEvent& Event)
{
	const FString EventText = Event.Text != nullptr ? *Event.Text : FString();
	if (State != ESimCopterReplayState::Recording)
	{
		return;
	}
	// A sound carries its meaning in its payload, so it is the one kind allowed to have no text.
	if (EventText.IsEmpty() && !Event.IsSound())
	{
		return;
	}

	const int32 FrameIndex = FMath::Max(RecordFrameIndex - 1, 0);

	// One riot can emit hundreds of lines in a frame. Dropping the overflow keeps the clip usable;
	// silently blowing the memory budget on trace text would not. Sounds are exempt: there are only
	// ever a handful a frame, and a clip that drops them plays back silent.
	if (!Event.IsSound())
	{
		int32 ThisFrameCount = 0;
		for (int32 Index = Clip.Events.Num() - 1; Index >= 0; --Index)
		{
			if (Clip.Events[Index].FrameIndex != FrameIndex)
			{
				break;
			}
			if (++ThisFrameCount >= MaxEventsPerFrame)
			{
				return;
			}
		}
	}

	SimCopterReplay::FReplayEvent& Recorded = Clip.Events.AddDefaulted_GetRef();
	Recorded.FrameIndex = FrameIndex;
	Recorded.Kind = Event.Kind;
	Recorded.PersonState = Event.PersonState;
	Recorded.PayloadA = Event.PayloadA;
	Recorded.PayloadB = Event.PayloadB;
	Recorded.Text = EventText;

	if (Event.bHasWorldLocation)
	{
		// A sound knows exactly where it was played; nothing else should override that.
		Recorded.WorldLocationCm = FVector3f(Event.WorldLocation);
	}
	else if (Event.Source != nullptr)
	{
		Recorded.WorldLocationCm = FVector3f(Event.Source->GetActorLocation());
		if (const int32* TrackIndex =
				TrackIndexByActor.Find(TObjectKey<AActor>(const_cast<AActor*>(Event.Source))))
		{
			Recorded.ActorId = Clip.Tracks.IsValidIndex(*TrackIndex) ? Clip.Tracks[*TrackIndex].ActorId : 0;
		}
	}
}

void USimCopterReplaySubsystem::AddBookmark(const FString& Text)
{
	if (!HasClip() && State != ESimCopterReplayState::Recording)
	{
		return;
	}

	SimCopterReplay::FReplayEvent& Event = Clip.Events.AddDefaulted_GetRef();
	Event.Kind = SimCopterReplay::EReplayEventKind::Bookmark;
	Event.Text = Text.IsEmpty() ? TEXT("Mark") : Text;
	Event.FrameIndex = State == ESimCopterReplayState::Recording
		? FMath::Max(RecordFrameIndex - 1, 0)
		: Clip.TimeToFrame(PlayheadSeconds);

	// A bookmark dropped during review lands out of order, and everything that reads the track
	// (the timeline's markers, GetEventsAroundPlayhead) assumes chronological order.
	Clip.Events.StableSort([](const SimCopterReplay::FReplayEvent& A, const SimCopterReplay::FReplayEvent& B)
	{
		return A.FrameIndex < B.FrameIndex;
	});
	BroadcastChanged();
}

void USimCopterReplaySubsystem::GetEventsAroundPlayhead(
	const int32 MaxEvents,
	TArray<const SimCopterReplay::FReplayEvent*>& OutEvents) const
{
	OutEvents.Reset();
	if (MaxEvents <= 0 || Clip.Events.Num() == 0)
	{
		return;
	}

	const int32 PlayheadFrame = State == ESimCopterReplayState::Recording
		? FMath::Max(RecordFrameIndex - 1, 0)
		: Clip.TimeToFrame(PlayheadSeconds);

	// Newest first, walking back from the playhead: the list reads like a log, with what just
	// happened at the top.
	for (int32 Index = Clip.Events.Num() - 1; Index >= 0 && OutEvents.Num() < MaxEvents; --Index)
	{
		// Sounds are machinery, not decisions - and a positional loop records a move every single
		// frame, which would bury the list this exists to show.
		if (Clip.Events[Index].IsSound())
		{
			continue;
		}
		if (Clip.Events[Index].FrameIndex <= PlayheadFrame)
		{
			OutEvents.Add(&Clip.Events[Index]);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------------------------

void USimCopterReplaySubsystem::EnterReview()
{
	if (State == ESimCopterReplayState::Reviewing || !HasClip())
	{
		return;
	}

	Mnemonics.SetNames(TArray<FString>(Clip.ClipMnemonics));

	// Order matters. The live world is suspended (and its poses remembered) BEFORE the puppets go
	// in, so a puppet spawned into the middle of the crowd cannot be mistaken for a live agent and
	// hidden along with it.
	FreezeWorldForReview();

	// The live mixer has nothing left to say once the world is frozen, and every loop it was
	// holding - the rotor, the sirens, the radio, a walking voice per person - would hang there
	// underneath the replay. The clip plays its own recorded sound events instead.
	if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
	{
		Audio->SilenceForReplayReview();
	}
	LastSoundFrame = 0.0f;

	SuspendLiveWorld();
	BuildPuppets();
	// The review may be watched with the panel hidden, and the free camera has to survive that, so
	// it is created here rather than only when the panel opens.
	EnsureFreeCamera();

	State = ESimCopterReplayState::Reviewing;
	PlayheadSeconds = 0.0f;
	bPlaying = false;
	RefreshMissionHudVisibility();
	ApplyPlayhead();
	ApplyCameraView();

	UE_LOG(LogSimCopterReplay, Log, TEXT("Review entered. %s"), *DescribeState());
}

void USimCopterReplaySubsystem::LeaveReview()
{
	if (State != ESimCopterReplayState::Reviewing)
	{
		return;
	}

	bPlaying = false;
	DestroyPuppets();
	RestoreLiveWorld();

	// Whatever the clip was playing stops with it; the live systems restart their own loops on
	// their next tick, which the thaw below lets happen.
	if (USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this))
	{
		Audio->SilenceForReplayReview();
	}

	ThawWorldAfterReview();
	State = ESimCopterReplayState::Idle;
	// After the state change, so the review's unconditional hide is lifted.
	RefreshMissionHudVisibility();

	UE_LOG(LogSimCopterReplay, Log, TEXT("Review left. %s"), *DescribeState());
}

void USimCopterReplaySubsystem::ExitReview()
{
	if (State != ESimCopterReplayState::Reviewing)
	{
		return;
	}

	// Ends the review WITHOUT throwing the clip away: the world comes back, the pause is released,
	// and the clip is still in memory to be saved or reviewed again. ResetClip is the destructive
	// one. The camera goes back to the player's pawn here too, because a review may have left it on
	// the free camera and the aircraft is about to start flying again.
	LeaveReview();
	CameraView = CameraViewBeforePanel;
	if (bPanelOpen)
	{
		ApplyCameraView();
	}
	else
	{
		// Exiting a review that was being watched with the panel hidden ends replay mode outright,
		// so everything it was holding goes back: the camera, the free camera actor and the HUD.
		RestoreCameraAfterPanel();
		DestroyFreeCamera();
		SetHudHidden(false);
	}

	UE_LOG(LogSimCopterReplay, Log, TEXT("Review ended, clip kept. %s"), *DescribeState());
	BroadcastChanged();
}

bool USimCopterReplaySubsystem::CanReviewLoadedClip() const
{
	return HasClip() && State == ESimCopterReplayState::Idle;
}

void USimCopterReplaySubsystem::ReviewLoadedClip()
{
	if (!CanReviewLoadedClip())
	{
		return;
	}
	EnterReview();
	UE_LOG(LogSimCopterReplay, Log, TEXT("Review re-entered. %s"), *DescribeState());
	BroadcastChanged();
}

void USimCopterReplaySubsystem::Play()
{
	if (State != ESimCopterReplayState::Reviewing || !HasClip())
	{
		return;
	}
	// Pressing play with the head parked on the last frame should start the clip again rather than
	// doing nothing, which is what "play" means everywhere else.
	if (PlayheadSeconds >= Clip.GetDurationSeconds() - KINDA_SMALL_NUMBER)
	{
		PlayheadSeconds = 0.0f;
	}
	bPlaying = true;
	// Restart the real-time clock, or the first frame after a long pause would jump the playhead by
	// however long the operator sat there.
	LastPlaybackRealTimeSeconds = FPlatformTime::Seconds();
	BroadcastChanged();
}

void USimCopterReplaySubsystem::Pause()
{
	if (!bPlaying)
	{
		return;
	}
	bPlaying = false;
	BroadcastChanged();
}

void USimCopterReplaySubsystem::TogglePlayPause()
{
	if (bPlaying)
	{
		Pause();
	}
	else
	{
		Play();
	}
}

void USimCopterReplaySubsystem::GoToStart()
{
	SetPlayheadSeconds(0.0f);
}

void USimCopterReplaySubsystem::SetPlayheadSeconds(const float Seconds)
{
	if (State != ESimCopterReplayState::Reviewing)
	{
		return;
	}
	PlayheadSeconds = FMath::Clamp(Seconds, 0.0f, Clip.GetDurationSeconds());
	ApplyPlayhead();
	BroadcastChanged();
}

void USimCopterReplaySubsystem::SetPlaybackSpeed(const float Speed)
{
	PlaybackSpeed = FMath::Clamp(Speed, MinPlaybackSpeed, MaxPlaybackSpeed);
	BroadcastChanged();
}

void USimCopterReplaySubsystem::ApplyPlayhead()
{
	if (Clip.FrameIntervalSeconds <= 0.0f)
	{
		return;
	}

	// Fractional, not rounded: this is what turns a 20 Hz recording into smooth motion at any frame
	// rate and at 0.1x speed.
	const float Frame = PlayheadSeconds / Clip.FrameIntervalSeconds;
	FireSoundEventsForPlayhead(LastSoundFrame, Frame);
	LastSoundFrame = Frame;

	for (int32 TrackIndex = 0; TrackIndex < Clip.Tracks.Num(); ++TrackIndex)
	{
		if (!Puppets.IsValidIndex(TrackIndex))
		{
			break;
		}
		AActor* Puppet = Puppets[TrackIndex];
		if (Puppet == nullptr || Puppet->IsActorBeingDestroyed())
		{
			continue;
		}
		ISimCopterReplayRecordable* Recordable = Cast<ISimCopterReplayRecordable>(Puppet);
		if (Recordable == nullptr)
		{
			continue;
		}

		SimCopterReplay::FReplayActorState SampledState;
		if (!Clip.Tracks[TrackIndex].Sample(Frame, SampledState))
		{
			// Before the actor spawned or after it despawned.
			Puppet->SetActorHiddenInGame(true);
			continue;
		}
		Recordable->ApplyReplayState(Mnemonics, SampledState);
	}
}

void USimCopterReplaySubsystem::FireSoundEventsForPlayhead(
	const float PreviousFrame,
	const float CurrentFrame)
{
	USimCopterAudioSubsystem* Audio = USimCopterAudioSubsystem::Get(this);
	if (Audio == nullptr || Clip.Events.Num() == 0)
	{
		return;
	}

	// Forward, continuous motion only. Scrubbing across two seconds of a busy city would fire
	// dozens of effects in one frame - a noise, not a replay - and running backwards would play
	// them in reverse order, which is worse. Either way the sound track just re-anchors silently.
	const float Advance = CurrentFrame - PreviousFrame;
	const float MaxContinuousAdvanceFrames = 1.0f / (Clip.FrameIntervalSeconds * 4.0f);
	if (Advance <= 0.0f || Advance > MaxContinuousAdvanceFrames)
	{
		return;
	}

	for (const SimCopterReplay::FReplayEvent& Event : Clip.Events)
	{
		if (!Event.IsSound())
		{
			continue;
		}
		const float EventFrame = static_cast<float>(Event.FrameIndex);
		if (EventFrame <= PreviousFrame || EventFrame > CurrentFrame)
		{
			continue;
		}

		if (Event.Kind == SimCopterReplay::EReplayEventKind::SoundStop)
		{
			Audio->Stop(Event.PayloadA);
			continue;
		}

		if (Event.Kind == SimCopterReplay::EReplayEventKind::SoundMove)
		{
			// Only re-aims something already playing. A move for a slot the replay has not started
			// must not start it - that is what keeps a clip from waking sounds it never recorded.
			if (Audio->IsPlaying(Event.PayloadA))
			{
				Audio->SetPosition(Event.PayloadA, FVector(Event.WorldLocationCm));
			}
			continue;
		}

		// A zero location is what RecordSoundEvent stores for a 2D play; anything else was a
		// positioned effect and has to stay positioned, or the whole city's audio collapses onto
		// the listener.
		if (Event.WorldLocationCm.IsNearlyZero())
		{
			Audio->Play2D(Event.PayloadA, Event.PayloadB);
		}
		else
		{
			Audio->Play3D(Event.PayloadA, FVector(Event.WorldLocationCm), Event.PayloadB);
		}
	}
}

void USimCopterReplaySubsystem::BuildPuppets()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	DestroyPuppets();
	Puppets.SetNum(Clip.Tracks.Num());

	// The population's own agent class, when there is a traffic system to ask: the project may use
	// a Blueprint subclass with authored defaults, and a puppet built from the C++ class would not
	// match the crowd it is standing in for.
	TSubclassOf<ASimCopterGroundAgent> AgentClass = ASimCopterGroundAgent::StaticClass();
	ASimCopterTrafficSystemActor* TrafficSystem = Cast<ASimCopterTrafficSystemActor>(
		UGameplayStatics::GetActorOfClass(World, ASimCopterTrafficSystemActor::StaticClass()));
	if (TrafficSystem != nullptr && TrafficSystem->GetGroundAgentClass() != nullptr)
	{
		AgentClass = TrafficSystem->GetGroundAgentClass();
	}

	const FString OriginalGameRoot = SimCopterOriginalGame::ResolveRoot();
	const int32 MaxPuppets = FMath::Max(CVarReplayMaxPuppets.GetValueOnGameThread(), 1);
	int32 SpawnedCount = 0;

	for (int32 TrackIndex = 0; TrackIndex < Clip.Tracks.Num(); ++TrackIndex)
	{
		const SimCopterReplay::FReplayActorTrack& Track = Clip.Tracks[TrackIndex];
		if (Track.Keys.Num() == 0)
		{
			continue;
		}

		switch (Track.Kind)
		{
		case SimCopterReplay::EReplayActorKind::Helicopter:
		case SimCopterReplay::EReplayActorKind::OnFootPlayer:
		{
			// The player's aircraft and the on-foot pawn are the two actors a city always has
			// exactly one of, and both are already suspended and repositionable. Reusing them
			// avoids spawning a second helicopter with its own audio, rotor wash and collision -
			// and means the recorded flight is flown by the same airframe the player owns, with
			// the model they were flying.
			AActor* Live = Track.Kind == SimCopterReplay::EReplayActorKind::Helicopter
				? static_cast<AActor*>(ResolvePlayerHelicopter())
				: nullptr;
			if (Live == nullptr)
			{
				for (const FSuspendedActor& Suspended : SuspendedActors)
				{
					AActor* Candidate = Suspended.Actor.Get();
					if (Candidate == nullptr)
					{
						continue;
					}
					const ISimCopterReplayRecordable* Recordable = Cast<ISimCopterReplayRecordable>(Candidate);
					if (Recordable != nullptr && Recordable->GetReplayActorKind() == Track.Kind)
					{
						Live = Candidate;
						break;
					}
				}
			}
			if (Live != nullptr)
			{
				Puppets[TrackIndex] = Live;
			}
			break;
		}

		case SimCopterReplay::EReplayActorKind::Pedestrian:
		case SimCopterReplay::EReplayActorKind::Vehicle:
		{
			if (SpawnedCount >= MaxPuppets)
			{
				break;
			}

			// Deferred, because `ASimCopterGroundAgent::BeginPlay` starts the behaviour VM for a
			// pedestrian and there is no unwinding that afterwards - the puppet flag has to be set
			// before the actor begins play, not after.
			const SimCopterReplay::FReplayActorState& FirstState = Track.Keys[0].State;
			const FTransform SpawnTransform(
				FRotator(FirstState.RotationDeg.X, FirstState.RotationDeg.Y, FirstState.RotationDeg.Z),
				FVector(FirstState.LocationCm));
			ASimCopterGroundAgent* Agent = World->SpawnActorDeferred<ASimCopterGroundAgent>(
				AgentClass,
				SpawnTransform,
				nullptr,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Agent == nullptr)
			{
				break;
			}

			Agent->BecomeReplayPuppet();
			if (!Track.Spawn.FigureName.IsEmpty())
			{
				// Before ConfigureAgent: that is where the figure is built, and the binding cannot
				// be changed once it has been.
				Agent->SetPedestrianFigureName(Track.Spawn.FigureName);
			}
			Agent->SetInitialBehaviorClass(Track.Spawn.BehaviorClass);
			Agent->SetPedestrianFigureClothesOffset(Track.Spawn.ClothesOffset);
			Agent->FinishSpawning(SpawnTransform);

			Agent->ConfigureAgent(
				Track.Kind == SimCopterReplay::EReplayActorKind::Vehicle
					? ESimCopterGroundAgentKind::Vehicle
					: ESimCopterGroundAgentKind::Pedestrian,
				Track.Spawn.MeshTableName,
				OriginalGameRoot,
				/*NewMovementSpeedCmPerSec=*/0.0f);

			Puppets[TrackIndex] = Agent;
			SpawnedPuppets.Add(Agent);
			++SpawnedCount;
			break;
		}

		default:
			break;
		}
	}

	if (SpawnedCount >= MaxPuppets)
	{
		UE_LOG(
			LogSimCopterReplay,
			Warning,
			TEXT("Replay clip has more tracks than SimCopter.Replay.MaxPuppets (%d); the rest will not be shown."),
			MaxPuppets);
	}
}

void USimCopterReplaySubsystem::DestroyPuppets()
{
	// Only the stand-ins playback created are destroyed. `Puppets` also points at the player's own
	// helicopter and on-foot pawn, which were borrowed; `RestoreLiveWorld` puts those back.
	for (AActor* Puppet : SpawnedPuppets)
	{
		if (Puppet == nullptr || Puppet->IsActorBeingDestroyed())
		{
			continue;
		}
		if (ISimCopterReplayRecordable* Recordable = Cast<ISimCopterReplayRecordable>(Puppet))
		{
			Recordable->EndReplayPlayback();
		}
		Puppet->Destroy();
	}
	SpawnedPuppets.Reset();
	Puppets.Reset();
}

void USimCopterReplaySubsystem::SuspendLiveWorld()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	SuspendedActors.Reset();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor == nullptr || Actor->IsActorBeingDestroyed())
		{
			continue;
		}

		// The world-owning systems are not recordable, but they must not run either: the time
		// dilation already reduces their delta to nothing, and this makes it certain. Without it a
		// mission callout, a dispatch or a scheduler roll can still land in the middle of a review
		// and announce itself over the clip.
		if (Actor->IsA<ASimCopterMissionSystemActor>()
			|| Actor->IsA<ASimCopterTrafficSystemActor>()
			|| Actor->IsA<ASimCopterAmbientVehiclesActor>())
		{
			FSuspendedActor& SuspendedSystem = SuspendedActors.AddDefaulted_GetRef();
			SuspendedSystem.Actor = Actor;
			SuspendedSystem.bWasHidden = Actor->IsHidden();
			SuspendedSystem.bWasTickEnabled = Actor->IsActorTickEnabled();
			SuspendedSystem.bIsWorldSystem = true;
			Actor->SetActorTickEnabled(false);
			continue;
		}

		ISimCopterReplayRecordable* Recordable = Cast<ISimCopterReplayRecordable>(Actor);
		if (Recordable == nullptr)
		{
			continue;
		}

		FSuspendedActor& Suspended = SuspendedActors.AddDefaulted_GetRef();
		Suspended.Actor = Actor;
		Suspended.bWasHidden = Actor->IsHidden();
		Suspended.bWasTickEnabled = Actor->IsActorTickEnabled();
		Recordable->CaptureReplayState(Mnemonics, Suspended.State);

		// The live crowd is frozen where it stood when the clip was opened. Leaving it on screen
		// would show the recorded population walking through a second, motionless one.
		Actor->SetActorHiddenInGame(true);
		Actor->SetActorTickEnabled(false);
	}
}

void USimCopterReplaySubsystem::RestoreLiveWorld()
{
	for (const FSuspendedActor& Suspended : SuspendedActors)
	{
		AActor* Actor = Suspended.Actor.Get();
		if (Actor == nullptr || Actor->IsActorBeingDestroyed())
		{
			continue;
		}
		// A world system was only tick-suspended; it has no recorded transform, and applying one
		// would move the whole system actor.
		if (!Suspended.bIsWorldSystem)
		{
			if (ISimCopterReplayRecordable* Recordable = Cast<ISimCopterReplayRecordable>(Actor))
			{
				// Puts the helicopter (and anything else playback borrowed) back exactly where the
				// sim left it, before the world is thawed and the flight model starts integrating
				// again from wherever the actor happens to be.
				Recordable->ApplyReplayState(Mnemonics, Suspended.State);
				Recordable->EndReplayPlayback();
			}
			Actor->SetActorHiddenInGame(Suspended.bWasHidden);
		}
		Actor->SetActorTickEnabled(Suspended.bWasTickEnabled);
	}
	SuspendedActors.Reset();
}

// ---------------------------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------------------------

void USimCopterReplaySubsystem::EnsureFreeCamera()
{
	UWorld* World = GetWorld();
	if (World == nullptr || FreeCamera != nullptr)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FreeCamera = World->SpawnActor<ASimCopterReplayFreeCamera>(
		ASimCopterReplayFreeCamera::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (FreeCamera == nullptr)
	{
		return;
	}

	FreeCamera->SetSmoothingEnabled(bSmoothCamera);

	// Start it wherever the player is looking now, so switching to free cam is a continuation of
	// the shot rather than a cut to the world origin.
	if (const APlayerController* Controller = World->GetFirstPlayerController())
	{
		FVector ViewLocation = FVector::ZeroVector;
		FRotator ViewRotation = FRotator::ZeroRotator;
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
		FreeCamera->SnapTo(ViewLocation, ViewRotation);
	}
}

void USimCopterReplaySubsystem::DestroyFreeCamera()
{
	if (FreeCamera != nullptr && !FreeCamera->IsActorBeingDestroyed())
	{
		FreeCamera->Destroy();
	}
	FreeCamera = nullptr;
}

void USimCopterReplaySubsystem::SetCameraView(const ESimCopterReplayCameraView View)
{
	if (CameraView == View)
	{
		return;
	}
	CameraView = View;
	ApplyCameraView();
	UE_LOG(LogSimCopterReplay, Log, TEXT("Camera view -> %d. %s"), static_cast<int32>(View), *DescribeState());
	BroadcastChanged();
}

void USimCopterReplaySubsystem::CycleCameraView()
{
	// Free is on the end of the existing four rather than replacing one of them, so the C key keeps
	// doing what it always did and the extra view is simply one more press away.
	switch (CameraView)
	{
	case ESimCopterReplayCameraView::Chase:   SetCameraView(ESimCopterReplayCameraView::Orbit); break;
	case ESimCopterReplayCameraView::Orbit:   SetCameraView(ESimCopterReplayCameraView::Rescue); break;
	case ESimCopterReplayCameraView::Rescue:  SetCameraView(ESimCopterReplayCameraView::Cockpit); break;
	case ESimCopterReplayCameraView::Cockpit: SetCameraView(ESimCopterReplayCameraView::Free); break;
	default:                                  SetCameraView(ESimCopterReplayCameraView::Chase); break;
	}
}

void USimCopterReplaySubsystem::RestoreCameraAfterPanel()
{
	UWorld* World = GetWorld();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Controller == nullptr)
	{
		return;
	}

	// The helicopter's view mode goes back to whatever it was before the panel took it over, but
	// only the mode - the view TARGET is the pawn the player is actually possessing. Routing this
	// through ApplyCameraView instead was a bug: that function always targets the helicopter, so
	// closing the panel while on foot left the player watching their parked aircraft.
	if (ASimCopterHelicopterPawn* Helicopter = ResolvePlayerHelicopter())
	{
		Helicopter->SetCameraMode(static_cast<ESimCopterCameraMode>(CameraViewBeforePanel));
	}

	APawn* PossessedPawn = Controller->GetPawn();
	AActor* Target = PossessedPawn != nullptr ? static_cast<AActor*>(PossessedPawn) : nullptr;
	if (Target == nullptr)
	{
		Target = ResolvePlayerHelicopter();
	}
	if (Target != nullptr)
	{
		Controller->SetViewTargetWithBlend(Target, 0.0f);
	}

	UE_LOG(
		LogSimCopterReplay,
		Verbose,
		TEXT("Camera restored to '%s' in view %d."),
		Target != nullptr ? *Target->GetName() : TEXT("<none>"),
		static_cast<int32>(CameraViewBeforePanel));
}

void USimCopterReplaySubsystem::ApplyCameraView()
{
	UWorld* World = GetWorld();
	APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Controller == nullptr)
	{
		return;
	}

	// Whichever way the view just went, the crosshair has to be re-evaluated: it is hidden in free
	// camera and restored on the way back.
	if (ASimCopterHelicopterPawn* CrosshairOwner = ResolvePlayerHelicopter())
	{
		CrosshairOwner->RefreshCrosshairVisibility();
	}

	if (IsFreeCameraActive())
	{
		EnsureFreeCamera();
		if (FreeCamera != nullptr)
		{
			// A hard cut. A blend would swing the camera across the city every time the operator
			// pressed C, which is unusable while framing a shot.
			Controller->SetViewTargetWithBlend(FreeCamera, 0.0f);
			UE_LOG(LogSimCopterReplay, Verbose, TEXT("View target -> free camera."));
		}
		return;
	}

	if (ASimCopterHelicopterPawn* Helicopter = ResolvePlayerHelicopter())
	{
		Helicopter->SetCameraMode(static_cast<ESimCopterCameraMode>(CameraView));
		Controller->SetViewTargetWithBlend(Helicopter, 0.0f);
		return;
	}

	// No helicopter (the player is on foot): fall back to whatever pawn they are in.
	if (APawn* Pawn = Controller->GetPawn())
	{
		Controller->SetViewTargetWithBlend(Pawn, 0.0f);
	}
}

void USimCopterReplaySubsystem::SetSmoothCameraEnabled(const bool bEnabled)
{
	bSmoothCamera = bEnabled;
	if (FreeCamera != nullptr)
	{
		FreeCamera->SetSmoothingEnabled(bEnabled);
	}
	BroadcastChanged();
}

float USimCopterReplaySubsystem::GetFreeCameraFov() const
{
	return FreeCamera != nullptr ? FreeCamera->GetFieldOfView() : 90.0f;
}

ASimCopterHelicopterPawn* USimCopterReplaySubsystem::ResolvePlayerHelicopter() const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}
	if (const APlayerController* Controller = World->GetFirstPlayerController())
	{
		if (ASimCopterHelicopterPawn* Possessed = Cast<ASimCopterHelicopterPawn>(Controller->GetPawn()))
		{
			return Possessed;
		}
	}
	// On foot the aircraft is still in the world, just unpossessed - and it is still the thing the
	// chase and cockpit views are bolted to.
	return Cast<ASimCopterHelicopterPawn>(
		UGameplayStatics::GetActorOfClass(World, ASimCopterHelicopterPawn::StaticClass()));
}

// ---------------------------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------------------------

void USimCopterReplaySubsystem::SetHudHidden(const bool bHidden)
{
	if (bHudHidden == bHidden)
	{
		return;
	}
	bHudHidden = bHidden;

	// The cockpit overlays belong to the helicopter pawn and the marker/message layers to the
	// mission actor, so each owner hides its own - there is no viewport-wide enumeration of added
	// Slate content to do it centrally.
	if (ASimCopterHelicopterPawn* Helicopter = ResolvePlayerHelicopter())
	{
		Helicopter->SetHudHiddenForReplay(bHudHidden);
	}
	RefreshMissionHudVisibility();
	BroadcastChanged();
}

void USimCopterReplaySubsystem::RefreshMissionHudVisibility()
{
	ASimCopterMissionSystemActor* Missions = Cast<ASimCopterMissionSystemActor>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ASimCopterMissionSystemActor::StaticClass()));
	if (Missions == nullptr)
	{
		return;
	}

	// Always hidden during a review, whatever Hide HUD says. The mission layer is suspended, so its
	// markers and its message log are frozen leftovers from the live game - a countdown that is not
	// counting and a callout for a job that is not happening, sitting over somebody else's replay.
	Missions->SetHudHiddenForReplay(bHudHidden || State == ESimCopterReplayState::Reviewing);
}

// ---------------------------------------------------------------------------------------------
// Pause
// ---------------------------------------------------------------------------------------------

void USimCopterReplaySubsystem::FreezeWorldForReview()
{
	if (bWorldFrozen)
	{
		return;
	}
	UWorld* World = GetWorld();
	AWorldSettings* WorldSettings = World != nullptr ? World->GetWorldSettings() : nullptr;
	if (WorldSettings == nullptr)
	{
		UE_LOG(LogSimCopterReplay, Warning, TEXT("Replay could not freeze the world: no world settings."));
		return;
	}

	TimeDilationBeforeReview = WorldSettings->TimeDilation;
	// Presentation-only systems (the particle pools, the tracers, the gas) read this and switch to
	// real time, so the clip's fire, water and rotor wash keep running while the sim does not.
	SimCopterReplay::GReviewFreezeActive = true;
	// Not zero: the engine clamps global dilation to MinGlobalTimeDilation, and a value it refuses
	// would be silently corrected. This is small enough that a ten-minute review advances the sim
	// by well under a frame.
	WorldSettings->SetTimeDilation(ReviewTimeDilation);
	bWorldFrozen = true;

	UE_LOG(
		LogSimCopterReplay,
		Log,
		TEXT("World frozen for review at dilation %g (was %g). NOT paused - a paused world stops ")
		TEXT("component ticks, which freezes the camera boom and the free camera."),
		WorldSettings->TimeDilation,
		TimeDilationBeforeReview);
}

void USimCopterReplaySubsystem::ThawWorldAfterReview()
{
	SimCopterReplay::GReviewFreezeActive = false;
	if (!bWorldFrozen)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (AWorldSettings* WorldSettings = World != nullptr ? World->GetWorldSettings() : nullptr)
	{
		WorldSettings->SetTimeDilation(TimeDilationBeforeReview);
	}
	bWorldFrozen = false;
	UE_LOG(LogSimCopterReplay, Log, TEXT("World thawed, dilation restored to %g."), TimeDilationBeforeReview);
}

void USimCopterReplaySubsystem::BroadcastChanged()
{
	StateChanged.Broadcast();
}

void USimCopterReplaySubsystem::DumpTracks(TArray<FString>& OutLines) const
{
	OutLines.Reset();
	OutLines.Add(DescribeState());

	if (Clip.Tracks.Num() == 0)
	{
		// The first thing to rule out: a clip whose frame counter ran but which captured nobody.
		// The timeline would still play, because its length comes from the frame count.
		OutLines.Add(TEXT("NO TRACKS - the recorder captured no actors at all."));
		return;
	}

	const float Frame = Clip.FrameIntervalSeconds > 0.0f
		? PlayheadSeconds / Clip.FrameIntervalSeconds
		: 0.0f;
	OutLines.Add(FString::Printf(
		TEXT("%d tracks, %d events, playhead frame %.2f of %d, %d puppet slots"),
		Clip.Tracks.Num(),
		Clip.Events.Num(),
		Frame,
		Clip.FrameCount,
		Puppets.Num()));

	// Only the first handful in full, then a summary: a city clip has hundreds of tracks and the
	// answer is always visible in the first few.
	constexpr int32 DetailedTracks = 12;
	int32 BoundCount = 0;
	int32 SampledCount = 0;

	for (int32 Index = 0; Index < Clip.Tracks.Num(); ++Index)
	{
		const SimCopterReplay::FReplayActorTrack& Track = Clip.Tracks[Index];
		const AActor* Puppet = Puppets.IsValidIndex(Index) ? Puppets[Index] : nullptr;
		SimCopterReplay::FReplayActorState Sampled;
		const bool bSampled = Track.Sample(Frame, Sampled);

		BoundCount += (Puppet != nullptr) ? 1 : 0;
		SampledCount += bSampled ? 1 : 0;

		if (Index >= DetailedTracks)
		{
			continue;
		}

		// The two positions side by side are the whole point: if they differ, the apply is not
		// landing; if they match but nothing looks like it is moving, the clip itself is static.
		const FVector ActualLocation = Puppet != nullptr ? Puppet->GetActorLocation() : FVector::ZeroVector;
		OutLines.Add(FString::Printf(
			TEXT("  [%3d] %-10s %-14s keys=%-4d frames=%d..%d puppet=%-28s hidden=%d clipPos=(%.0f,%.0f,%.0f) actualPos=(%.0f,%.0f,%.0f)%s"),
			Index,
			SimCopterReplay::GetActorKindName(Track.Kind),
			*Track.Label,
			Track.Keys.Num(),
			Track.FirstFrame,
			Track.LastFrame,
			Puppet != nullptr ? *Puppet->GetName() : TEXT("<NONE BOUND>"),
			Puppet != nullptr && Puppet->IsHidden() ? 1 : 0,
			Sampled.LocationCm.X, Sampled.LocationCm.Y, Sampled.LocationCm.Z,
			ActualLocation.X, ActualLocation.Y, ActualLocation.Z,
			bSampled ? TEXT("") : TEXT("  <-- SAMPLE FAILED (playhead outside the track's life)")));
	}

	OutLines.Add(FString::Printf(
		TEXT("%d/%d tracks have a puppet bound, %d/%d sample at the current playhead."),
		BoundCount,
		Clip.Tracks.Num(),
		SampledCount,
		Clip.Tracks.Num()));
}

FString USimCopterReplaySubsystem::DescribeState() const
{
	// Everything a "the panel did something odd" report needs in one line: which mode, whether the
	// world is paused and who pushed it, where the camera is, and what is on the view target.
	const TCHAR* StateName = TEXT("Idle");
	switch (State)
	{
	case ESimCopterReplayState::Recording: StateName = TEXT("Recording"); break;
	case ESimCopterReplayState::Reviewing: StateName = TEXT("Reviewing"); break;
	default: break;
	}

	const UWorld* World = GetWorld();
	const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	const AActor* ViewTarget = Controller != nullptr ? Controller->GetViewTarget() : nullptr;
	const APawn* PossessedPawn = Controller != nullptr ? Controller->GetPawn() : nullptr;

	return FString::Printf(
		TEXT("state=%s panel=%d recorded=%.2fs clipFrames=%d tracks=%d events=%d playhead=%.2fs playing=%d ")
		TEXT("view=%d freeCamAlive=%d hudHidden=%d frozen=%d worldPaused=%d ")
		TEXT("puppets=%d spawned=%d suspended=%d pawn='%s' viewTarget='%s'"),
		StateName,
		bPanelOpen ? 1 : 0,
		GetRecordedSeconds(),
		Clip.FrameCount,
		// tracks=0 with clipFrames>0 is the signature of "the timeline runs but nothing moves":
		// the frame counter advanced while the recorder captured nobody.
		Clip.Tracks.Num(),
		Clip.Events.Num(),
		PlayheadSeconds,
		bPlaying ? 1 : 0,
		static_cast<int32>(CameraView),
		FreeCamera != nullptr ? 1 : 0,
		bHudHidden ? 1 : 0,
		bWorldFrozen ? 1 : 0,
		(World != nullptr && World->IsPaused()) ? 1 : 0,
		Puppets.Num(),
		SpawnedPuppets.Num(),
		SuspendedActors.Num(),
		PossessedPawn != nullptr ? *PossessedPawn->GetName() : TEXT("<none>"),
		ViewTarget != nullptr ? *ViewTarget->GetName() : TEXT("<none>"));
}

// ---------------------------------------------------------------------------------------------
// Clips on disk
// ---------------------------------------------------------------------------------------------

FString USimCopterReplaySubsystem::ResolveLevelId(const UObject* WorldContextObject, FString& OutDisplayName)
{
	OutDisplayName.Reset();

	const UWorld* World = WorldContextObject != nullptr && GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	const USimCopterSessionSubsystem* Session = GameInstance != nullptr
		? GameInstance->GetSubsystem<USimCopterSessionSubsystem>()
		: nullptr;

	if (Session != nullptr && Session->HasPendingSession())
	{
		if (Session->GetSessionKind() == ESimCopterSessionKind::Career)
		{
			const int32 Index = Session->GetCareerCityIndex();
			OutDisplayName = FString::Printf(TEXT("Career City %d"), Index);
			return FString::Printf(TEXT("career:%d"), Index);
		}

		const FString CityFile = FPaths::GetCleanFilename(Session->GetCityFilePath());
		if (!CityFile.IsEmpty())
		{
			OutDisplayName = CityFile.ToUpper();
			return FString::Printf(TEXT("user:%s"), *CityFile.ToLower());
		}
	}

	// A session entered directly (PIE straight into the city level) has no kind at all. The map
	// name is still a real, stable identity - it just is not a city choice.
	const FString MapName = World != nullptr ? World->GetMapName() : FString(TEXT("Unknown"));
	OutDisplayName = MapName;
	return FString::Printf(TEXT("map:%s"), *MapName.ToLower());
}

const FString& USimCopterReplaySubsystem::GetLevelDisplayName()
{
	if (LevelId.IsEmpty())
	{
		LevelId = ResolveLevelId(this, LevelDisplayName);
	}
	return LevelDisplayName;
}

FString USimCopterReplaySubsystem::GetClipDirectory(const FString& LevelId)
{
	// A folder per level, so the rule "you can only load clips from the level you are in" is a
	// property of where the files are rather than a filter someone has to remember to apply.
	return FPaths::Combine(
		FPaths::ProjectSavedDir(),
		SimCopterReplay::ClipDirectoryName,
		SanitizeForFileSystem(LevelId));
}

FString USimCopterReplaySubsystem::NormalizeClipName(const FString& DisplayName)
{
	FString Result = DisplayName;
	Result.TrimStartAndEndInline();

	FString Clean;
	Clean.Reserve(FMath::Min(Result.Len(), SimCopterReplay::MaxClipNameLength));
	bool bPreviousWasSpace = false;
	for (const TCHAR Character : Result)
	{
		if (Clean.Len() >= SimCopterReplay::MaxClipNameLength)
		{
			break;
		}
		if (FChar::IsWhitespace(Character))
		{
			if (!bPreviousWasSpace && !Clean.IsEmpty())
			{
				Clean.AppendChar(TEXT(' '));
			}
			bPreviousWasSpace = true;
			continue;
		}
		if (FChar::IsControl(Character))
		{
			continue;
		}
		Clean.AppendChar(Character);
		bPreviousWasSpace = false;
	}
	Clean.TrimEndInline();
	return Clean;
}

bool USimCopterReplaySubsystem::IsClipNameValid(const FString& DisplayName, FString& OutError)
{
	OutError.Reset();
	if (NormalizeClipName(DisplayName).IsEmpty())
	{
		OutError = TEXT("Enter a name for the clip.");
		return false;
	}
	return true;
}

FString USimCopterReplaySubsystem::MakeClipFileName(const FString& DisplayName)
{
	const FString Normalized = NormalizeClipName(DisplayName);
	FString Safe = SanitizeForFileSystem(Normalized);
	if (Safe.IsEmpty())
	{
		Safe = TEXT("Clip");
	}

	// The name hash keeps two clips whose names differ only in punctuation ("Take 1" and "Take-1")
	// in separate files, the same way the saved-game slot names do.
	const uint32 NameCrc = FCrc::StrCrc32(*Normalized.ToLower());
	return FString::Printf(TEXT("%s_%08x"), *Safe.Left(32), NameCrc);
}

bool USimCopterReplaySubsystem::SaveClip(const FString& DisplayName, FString& OutError)
{
	OutError.Reset();

	if (!HasClip())
	{
		OutError = TEXT("There is nothing recorded to save.");
		return false;
	}
	if (!IsClipNameValid(DisplayName, OutError))
	{
		return false;
	}
	if (LevelId.IsEmpty())
	{
		LevelId = ResolveLevelId(this, LevelDisplayName);
	}

	Clip.Name = NormalizeClipName(DisplayName);
	Clip.LevelId = LevelId;
	Clip.LevelDisplayName = LevelDisplayName;
	Clip.ClipMnemonics = Mnemonics.GetNames();

	const FString Directory = GetClipDirectory(LevelId);
	const FString FilePath = FPaths::Combine(
		Directory,
		MakeClipFileName(Clip.Name) + SimCopterReplay::ClipFileExtension);

	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.MakeDirectory(*Directory, /*Tree=*/true))
	{
		OutError = TEXT("The clip folder could not be created.");
		return false;
	}

	// Written to a temporary name and moved into place, so a crash or a full disk part way through
	// leaves the previous version of the clip intact rather than a truncated file that looks valid
	// until it is opened.
	const FString TempPath = FilePath + TEXT(".tmp");
	{
		TUniquePtr<FArchive> Writer(FileManager.CreateFileWriter(*TempPath));
		if (!Writer.IsValid())
		{
			OutError = TEXT("The clip file could not be created.");
			return false;
		}
		if (!Clip.Serialize(*Writer, OutError))
		{
			Writer->Close();
			FileManager.Delete(*TempPath, false, true, true);
			return false;
		}
		Writer->Close();
		if (Writer->IsError())
		{
			FileManager.Delete(*TempPath, false, true, true);
			OutError = TEXT("The clip could not be written to disk.");
			return false;
		}
	}

	if (!FileManager.Move(*FilePath, *TempPath, /*bReplace=*/true))
	{
		FileManager.Delete(*TempPath, false, true, true);
		OutError = TEXT("The clip could not be saved.");
		return false;
	}

	BroadcastChanged();
	return true;
}

void USimCopterReplaySubsystem::GetClipSummaries(TArray<FSimCopterReplayClipSummary>& OutSummaries) const
{
	OutSummaries.Reset();

	FString ResolvedLevelId = LevelId;
	if (ResolvedLevelId.IsEmpty())
	{
		FString UnusedDisplayName;
		ResolvedLevelId = ResolveLevelId(this, UnusedDisplayName);
	}

	const FString Directory = GetClipDirectory(ResolvedLevelId);
	TArray<FString> FileNames;
	IFileManager::Get().FindFiles(
		FileNames,
		*FPaths::Combine(Directory, FString(TEXT("*")) + SimCopterReplay::ClipFileExtension),
		/*Files=*/true,
		/*Directories=*/false);

	for (const FString& FileName : FileNames)
	{
		const FString FilePath = FPaths::Combine(Directory, FileName);
		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*FilePath));
		if (!Reader.IsValid())
		{
			continue;
		}

		// The whole clip is read to build one list row, which is wasteful but correct, and the list
		// is only rebuilt when the operator opens it. A separate header block would be faster and
		// would also be a second thing that can disagree with the file it describes.
		SimCopterReplay::FReplayClip Candidate;
		FString Error;
		const bool bRead = Candidate.Serialize(*Reader, Error);
		Reader->Close();
		if (!bRead || Candidate.LevelId != ResolvedLevelId)
		{
			continue;
		}

		FSimCopterReplayClipSummary& Summary = OutSummaries.AddDefaulted_GetRef();
		Summary.FileName = FPaths::GetBaseFilename(FileName);
		Summary.DisplayName = Candidate.Name;
		Summary.RecordedAtUtc = Candidate.RecordedAtUtc;
		Summary.DurationSeconds = Candidate.GetDurationSeconds();
		Summary.TrackCount = Candidate.Tracks.Num();
		Summary.EventCount = Candidate.Events.Num();
	}

	OutSummaries.Sort([](const FSimCopterReplayClipSummary& A, const FSimCopterReplayClipSummary& B)
	{
		return A.RecordedAtUtc > B.RecordedAtUtc;
	});
}

bool USimCopterReplaySubsystem::LoadClip(const FString& FileName, FString& OutError)
{
	OutError.Reset();

	if (LevelId.IsEmpty())
	{
		LevelId = ResolveLevelId(this, LevelDisplayName);
	}

	const FString FilePath = FPaths::Combine(
		GetClipDirectory(LevelId),
		FileName + SimCopterReplay::ClipFileExtension);

	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*FilePath));
	if (!Reader.IsValid())
	{
		OutError = TEXT("That clip could not be opened.");
		return false;
	}

	SimCopterReplay::FReplayClip Loaded;
	const bool bRead = Loaded.Serialize(*Reader, OutError);
	Reader->Close();
	if (!bRead)
	{
		return false;
	}

	// The folder already enforces this, but a clip copied in by hand would not have gone through
	// it - and a clip from another city plays back as a population walking through solid buildings.
	if (Loaded.LevelId != LevelId)
	{
		OutError = FString::Printf(
			TEXT("That clip was recorded in %s and can only be played there."),
			Loaded.LevelDisplayName.IsEmpty() ? TEXT("another city") : *Loaded.LevelDisplayName);
		return false;
	}
	if (Loaded.FrameCount <= 0)
	{
		OutError = TEXT("That clip is empty.");
		return false;
	}

	// Anything in view now goes before the new clip arrives, so a failed load cannot leave the
	// world half-driven by the previous one.
	ResetClip();

	Clip = MoveTemp(Loaded);
	Mnemonics.SetNames(TArray<FString>(Clip.ClipMnemonics));
	ClipMemoryBytes = Clip.GetApproximateMemoryBytes();
	EnterReview();
	BroadcastChanged();
	return true;
}

bool USimCopterReplaySubsystem::DeleteClip(const FString& FileName, FString& OutError)
{
	OutError.Reset();

	const FString FilePath = FPaths::Combine(
		GetClipDirectory(LevelId),
		FileName + SimCopterReplay::ClipFileExtension);
	if (!IFileManager::Get().Delete(*FilePath, /*RequireExists=*/true, /*EvenReadOnly=*/true, /*Quiet=*/true))
	{
		OutError = TEXT("That clip could not be deleted.");
		return false;
	}
	BroadcastChanged();
	return true;
}
