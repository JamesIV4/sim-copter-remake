// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

class AActor;
class FArchive;

/**
 * Every replay state transition is logged here at `Log`: the panel opening and closing, recording
 * starting and stopping, review entered and left, the pause pushed and popped, and the camera view
 * changing. `Verbose` adds the view-target and input-suppression detail.
 *
 *     log LogSimCopterReplay Verbose
 *     SimReplayStatus                    one line of everything the system currently believes
 *
 * It is deliberately on at `Log` by default rather than behind a CVar: these are a handful of lines
 * per session, and "the panel did something odd" is exactly the report that arrives without anyone
 * having thought to switch a trace on first.
 */
SIMCOPTERREMAKE_API DECLARE_LOG_CATEGORY_EXTERN(LogSimCopterReplay, Log, All);

/**
 * The replay clip data model.
 *
 * NOT a port. The original SimCopter has no replay of any kind - there is no `FUN_004xxxxx` behind
 * anything in this file - so it is written to the remake's own conventions rather than the
 * executable's, and it deliberately does not use the 16.16 fixed point the sim runs on: a clip is
 * presentation data, and quantising it twice only loses precision the camera can see.
 *
 * A clip is a set of sparse per-actor tracks plus a flat event list, sampled at
 * `SimCopterReplay::FrameIntervalSeconds`. The recorder only appends a key when an actor's state
 * actually moved (`FReplayActorState::DiffersFrom`), so the several hundred parked cars and idle
 * pedestrians a city always has cost one key each for the whole clip rather than one per frame.
 * Playback interpolates between the two keys bracketing the requested frame, which is what makes
 * scrubbing, reverse and sub-1x playback speeds fall out for free.
 *
 * The event list is the "actor decisions" half. It is fed by the trace macro every behaviour-VM
 * site already calls (`SIMCOPTER_PEOPLE_TRACE`) and by the mission layer's own message funnel, so
 * it covers the shipped decision points without a new call site per decision.
 */
namespace SimCopterReplay
{
/** 'SCRP', little endian. */
inline constexpr uint32 ClipFileMagic = 0x50524353u;

/**
 * Bumped whenever the on-disk layout changes. A clip written by a different version is refused
 * with a message rather than read hopefully - a half-understood clip plays back as a city full of
 * actors teleporting into the ground, which reads as a physics bug rather than a format one.
 */
inline constexpr int32 ClipFileVersion = 1;

/**
 * 20 Hz. This is the original's own simulation period (`OriginalFrameSeconds`, 0.05 s - see
 * Docs/memory/simcopter-heli-flight-model.md), and it is above both the behaviour VM's 12.5 Hz
 * ceiling and the remake's 15 Hz people tick, so no decision the sim makes can fall between two
 * recorded frames.
 */
inline constexpr float FrameIntervalSeconds = 0.05f;

/** File extension for a saved clip, and the folder clips live under `Saved/`. */
inline const TCHAR* const ClipFileExtension = TEXT(".screplay");
inline const TCHAR* const ClipDirectoryName = TEXT("SimCopterReplays");

/** Longest clip name the save dialog accepts, matching the saved-game name limit. */
inline constexpr int32 MaxClipNameLength = 48;

enum class EReplayActorKind : uint8
{
	Unknown = 0,
	/** A person: ambient crowd, criminals, medics, riot, mission casualties. */
	Pedestrian,
	/** Anything on wheels - `ASimCopterGroundAgent` with `ESimCopterGroundAgentKind::Vehicle`. */
	Vehicle,
	/** The player's aircraft, and any other helicopter in the world. */
	Helicopter,
	/** The player when they are out of the aircraft on foot. */
	OnFootPlayer,
};

enum class EReplayEventKind : uint8
{
	Generic = 0,
	/** A behaviour-VM decision: a selection, a goto-object step, a refusal, an outcome, a despawn. */
	PersonDecision,
	/** One VM record. Off unless `SimCopter.Replay.RecordOpcodes` - it is thousands of lines a second. */
	PersonOpcode,
	/** A player-facing line from the mission layer's log. */
	MissionMessage,
	/** An operator bookmark dropped from the replay panel. */
	Bookmark,
};

SIMCOPTERREMAKE_API const TCHAR* GetActorKindName(EReplayActorKind Kind);
SIMCOPTERREMAKE_API const TCHAR* GetEventKindName(EReplayEventKind Kind);

/**
 * One actor's presentation state at one instant.
 *
 * Everything here is what the *renderer* needs, not what the sim needs: the recorder never tries to
 * capture a behaviour program's internal state, because playback never runs a behaviour program.
 */
struct SIMCOPTERREMAKE_API FReplayActorState
{
	/** `ClipId` value meaning "this actor has no privanim pose" (a car, the helicopter). */
	static constexpr uint16 NoClip = 0xffffu;

	enum EFlags : uint8
	{
		FlagNone = 0,
		/** The actor was hidden at this instant (despawned into the pool, riding in a cabin). */
		FlagHidden = 1 << 0,
		/** Helicopter only: the face-type-11 rotor blur disc was on. */
		FlagRotorBlurDisc = 1 << 1,
	};

	FVector3f LocationCm = FVector3f::ZeroVector;
	/** Pitch, Yaw, Roll of the actor's own transform. */
	FVector3f RotationDeg = FVector3f::ZeroVector;
	/**
	 * Pitch, Yaw, Roll of the visual pivot hanging under the actor: the helicopter's `ModelPivot`
	 * attitude, a knocked-down pedestrian's tumble. Separate because the sim's own transform is
	 * upright in both cases and the lean is drawn on top of it.
	 */
	FVector3f VisualRotationDeg = FVector3f::ZeroVector;
	/**
	 * Kind-specific channel.
	 * - Helicopter: main rotor angle in degrees.
	 * - Pedestrian/Vehicle: the visual root's Z offset, which is where the knockdown's drop and
	 *   the water submersion both end up.
	 */
	float AuxA = 0.0f;
	/**
	 * Kind-specific channel.
	 * - Helicopter: tail rotor angle in degrees.
	 * - Pedestrian/Vehicle: unused.
	 */
	float AuxB = 0.0f;
	/** Index into `FReplayClip::ClipMnemonics`, or `NoClip`. */
	uint16 ClipId = NoClip;
	/** Frame within that privanim clip. */
	uint16 ClipFrame = 0;
	uint8 Flags = FlagNone;

	bool IsHidden() const { return (Flags & FlagHidden) != 0; }

	/**
	 * Whether this state is far enough from `Other` to be worth a key. The thresholds are the
	 * point at which the difference is visible from a camera close enough to see it: half a
	 * millimetre and a twentieth of a degree.
	 */
	bool DiffersFrom(const FReplayActorState& Other) const;

	/** Position/rotation lerp for playback between two keys; discrete fields snap to `A`. */
	static FReplayActorState Blend(const FReplayActorState& A, const FReplayActorState& B, float Alpha);
};

/** One stored sample of one actor. */
struct SIMCOPTERREMAKE_API FReplayKey
{
	int32 FrameIndex = 0;
	FReplayActorState State;
};

/**
 * Enough to build a stand-in for an actor that no longer exists.
 *
 * A clip loaded from disk describes a population that despawned hours ago, so playback spawns its
 * own puppets rather than driving the live crowd. These are the four things
 * `ASimCopterGroundAgent::ConfigureAgent` needs to come up looking like the person or car that was
 * recorded - the figure binding in particular, because the privanim figure is chosen from the
 * behaviour class at spawn (`FUN_004c71c0`) and can never be changed afterwards.
 */
struct SIMCOPTERREMAKE_API FReplaySpawnDescriptor
{
	/** Entry in the population mesh table: the vehicle's GEO, or the pedestrian's. */
	FString MeshTableName;
	/** privanim figure name ("Medik", "2DOG", "Coww"), when the agent renders as one. */
	FString FigureName;
	/** Drives the figure binding and the head table. */
	int32 BehaviorClass = 0;
	/** Palette shift into the figure's clothes colours. */
	int32 ClothesOffset = 0;
};

/** Everything one actor did over the clip. */
struct SIMCOPTERREMAKE_API FReplayActorTrack
{
	/** Unique within the clip. Never an engine pointer or name - clips outlive the actors. */
	uint32 ActorId = 0;
	EReplayActorKind Kind = EReplayActorKind::Unknown;
	/** What the panel's actor list shows: "Robber", "Police Car", "Helicopter". */
	FString Label;
	/**
	 * Behaviour state for a person (0 ambient, 3 rioter, 6 medevac patient, 10 robber ...), so the
	 * event track can be filtered the way `SimCopter.People.TraceStates` filters the log.
	 * `INDEX_NONE` for everything else.
	 */
	int32 PersonState = INDEX_NONE;
	/** How playback rebuilds a stand-in for this actor. */
	FReplaySpawnDescriptor Spawn;
	/** Frames this actor existed for; outside them it is hidden. */
	int32 FirstFrame = 0;
	int32 LastFrame = 0;
	TArray<FReplayKey> Keys;

	/**
	 * State at a fractional frame, interpolated between the bracketing keys. Returns false when
	 * the actor did not exist then, which the player turns into "hide it".
	 */
	bool Sample(float Frame, FReplayActorState& OutState) const;

	int64 GetApproximateMemoryBytes() const;
};

struct SIMCOPTERREMAKE_API FReplayEvent
{
	int32 FrameIndex = 0;
	/** The track this belongs to, or 0 for a world event. */
	uint32 ActorId = 0;
	EReplayEventKind Kind = EReplayEventKind::Generic;
	int32 PersonState = INDEX_NONE;
	FVector3f WorldLocationCm = FVector3f::ZeroVector;
	FString Text;
};

/**
 * Interning table for privanim clip mnemonics ("1Wal", "NoMo", "Dead", "Wave"). Eighteen of them
 * ship, so two bytes a key beats a string a key by a very wide margin.
 */
class SIMCOPTERREMAKE_API FReplayMnemonicTable
{
public:
	uint16 Intern(const FString& Mnemonic);
	/** Null when the id is `FReplayActorState::NoClip` or out of range. */
	const FString* Resolve(uint16 Id) const;

	const TArray<FString>& GetNames() const { return Names; }
	void SetNames(TArray<FString>&& InNames);
	void Reset();

private:
	TArray<FString> Names;
	TMap<FString, uint16> Lookup;
};

/** A whole recording. */
struct SIMCOPTERREMAKE_API FReplayClip
{
	FString Name;
	/**
	 * Which level this clip belongs to, and the rule that a clip may only be loaded where it was
	 * recorded: "career:7", or "user:islandtown.sc2". Derived by
	 * `USimCopterReplaySubsystem::ResolveLevelId` from the session, so a clip recorded in a career
	 * city cannot be opened over a user city that happens to share a name.
	 */
	FString LevelId;
	/** The same thing spelled for a human: "Career City 7", "ISLANDTOWN.SC2". */
	FString LevelDisplayName;
	FDateTime RecordedAtUtc = FDateTime(0);
	float FrameIntervalSeconds = SimCopterReplay::FrameIntervalSeconds;
	int32 FrameCount = 0;
	TArray<FString> ClipMnemonics;
	TArray<FReplayActorTrack> Tracks;
	TArray<FReplayEvent> Events;

	float GetDurationSeconds() const;
	int64 GetApproximateMemoryBytes() const;
	void Reset();

	/** Frame index nearest a time in seconds, clamped into the clip. */
	int32 TimeToFrame(float Seconds) const;
	float FrameToTime(float Frame) const;

	/** Reads or writes the whole clip. Returns false with a player-facing reason in `OutError`. */
	bool Serialize(FArchive& Archive, FString& OutError);
};

// ---------------------------------------------------------------------------------------------
// The event funnel
//
// Deliberately a free function over a global rather than a subsystem call: it is reached from the
// `SIMCOPTER_PEOPLE_TRACE` macro, which fires from the innermost loop of the behaviour VM in a
// header that must not drag the subsystem in. `bRecordingEvents` is false whenever a clip is not
// recording, so the whole path costs one predictable branch.
// ---------------------------------------------------------------------------------------------

/** Game thread only. Written by the recorder as it starts and stops. */
extern SIMCOPTERREMAKE_API bool GRecordingEvents;
extern SIMCOPTERREMAKE_API bool GRecordingOpcodeEvents;

inline bool IsRecordingEvents() { return GRecordingEvents; }
inline bool IsRecordingOpcodeEvents() { return GRecordingOpcodeEvents; }

/**
 * Adds one event to the clip being recorded. `Source` is optional; when it is null the event is
 * attributed to whatever `FScopedEventSource` is in scope, which is how the trace macro's
 * hundreds of existing call sites get an actor without being rewritten.
 */
SIMCOPTERREMAKE_API void RecordEvent(
	EReplayEventKind Kind,
	const FString& Text,
	const AActor* Source = nullptr,
	int32 PersonState = INDEX_NONE);

/**
 * Where `RecordEvent` sends what it collects. The recorder installs itself on start and clears
 * itself on stop; null means nothing is listening, which is the state the game is in almost all of
 * the time. A raw function pointer rather than a delegate because this is called from the
 * behaviour VM's innermost loop.
 */
SIMCOPTERREMAKE_API void SetEventSink(
	void (*Sink)(EReplayEventKind Kind, const FString& Text, const AActor* Source, int32 PersonState));

/**
 * Attributes every event emitted inside the scope to one actor. `ASimCopterGroundAgent::Tick`
 * opens one around its behaviour update, so a trace line printed six frames deep in the VM still
 * lands on the right track.
 */
struct SIMCOPTERREMAKE_API FScopedEventSource
{
	explicit FScopedEventSource(const AActor* InSource);
	~FScopedEventSource();

	FScopedEventSource(const FScopedEventSource&) = delete;
	FScopedEventSource& operator=(const FScopedEventSource&) = delete;

private:
	const AActor* Previous = nullptr;
};

/** The actor the innermost `FScopedEventSource` names, or null. */
SIMCOPTERREMAKE_API const AActor* GetCurrentEventSource();
}
