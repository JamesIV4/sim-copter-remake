// Copyright Epic Games, Inc. All Rights Reserved.

#include "Replay/SimCopterReplayTypes.h"

#include "GameFramework/Actor.h"
#include "Serialization/Archive.h"

DEFINE_LOG_CATEGORY(LogSimCopterReplay);

namespace SimCopterReplay
{
bool GRecordingEvents = false;
bool GRecordingOpcodeEvents = false;

namespace
{
/** Innermost `FScopedEventSource`. Game thread only, like everything else on this path. */
const AActor* GEventSource = nullptr;

/**
 * The recorder's callback, installed while a clip is recording. A raw function pointer rather than
 * a delegate so the trace path stays a load and a branch.
 */
void (*GEventSink)(EReplayEventKind, const FString&, const AActor*, int32) = nullptr;

/** Half a millimetre and a twentieth of a degree - below what a close camera can resolve. */
constexpr float LocationEpsilonCm = 0.05f;
constexpr float RotationEpsilonDeg = 0.05f;
constexpr float AuxEpsilon = 0.05f;
}

void SetEventSink(void (*Sink)(EReplayEventKind, const FString&, const AActor*, int32))
{
	GEventSink = Sink;
}

const TCHAR* GetActorKindName(const EReplayActorKind Kind)
{
	switch (Kind)
	{
	case EReplayActorKind::Pedestrian:   return TEXT("Person");
	case EReplayActorKind::Vehicle:      return TEXT("Vehicle");
	case EReplayActorKind::Helicopter:   return TEXT("Helicopter");
	case EReplayActorKind::OnFootPlayer: return TEXT("Player");
	default:                             return TEXT("Actor");
	}
}

const TCHAR* GetEventKindName(const EReplayEventKind Kind)
{
	switch (Kind)
	{
	case EReplayEventKind::PersonDecision: return TEXT("DECISION");
	case EReplayEventKind::PersonOpcode:   return TEXT("OPCODE");
	case EReplayEventKind::MissionMessage: return TEXT("MISSION");
	case EReplayEventKind::Bookmark:       return TEXT("MARK");
	default:                               return TEXT("EVENT");
	}
}

void RecordEvent(
	const EReplayEventKind Kind,
	const FString& Text,
	const AActor* Source,
	const int32 PersonState)
{
	if (GEventSink == nullptr)
	{
		return;
	}
	GEventSink(Kind, Text, Source != nullptr ? Source : GEventSource, PersonState);
}

FScopedEventSource::FScopedEventSource(const AActor* InSource)
	: Previous(GEventSource)
{
	GEventSource = InSource;
}

FScopedEventSource::~FScopedEventSource()
{
	GEventSource = Previous;
}

const AActor* GetCurrentEventSource()
{
	return GEventSource;
}

// ---------------------------------------------------------------------------------------------
// FReplayActorState
// ---------------------------------------------------------------------------------------------

bool FReplayActorState::DiffersFrom(const FReplayActorState& Other) const
{
	// The discrete fields first: they are the cheap comparisons and the ones that must never be
	// smoothed away, because a pose swap or a despawn is a step change by definition.
	if (ClipId != Other.ClipId || ClipFrame != Other.ClipFrame || Flags != Other.Flags)
	{
		return true;
	}
	if (!LocationCm.Equals(Other.LocationCm, LocationEpsilonCm))
	{
		return true;
	}
	if (!RotationDeg.Equals(Other.RotationDeg, RotationEpsilonDeg))
	{
		return true;
	}
	if (!VisualRotationDeg.Equals(Other.VisualRotationDeg, RotationEpsilonDeg))
	{
		return true;
	}
	return !FMath::IsNearlyEqual(AuxA, Other.AuxA, AuxEpsilon)
		|| !FMath::IsNearlyEqual(AuxB, Other.AuxB, AuxEpsilon);
}

FReplayActorState FReplayActorState::Blend(
	const FReplayActorState& A,
	const FReplayActorState& B,
	const float Alpha)
{
	// The discrete fields come from A and never blend: half a pose is not a pose, and a body that
	// is hidden on one key and visible on the next should not fade through the floor.
	FReplayActorState Result = A;
	if (Alpha <= 0.0f)
	{
		return Result;
	}
	if (Alpha >= 1.0f)
	{
		Result.LocationCm = B.LocationCm;
		Result.RotationDeg = B.RotationDeg;
		Result.VisualRotationDeg = B.VisualRotationDeg;
		Result.AuxA = B.AuxA;
		Result.AuxB = B.AuxB;
		return Result;
	}

	Result.LocationCm = FMath::Lerp(A.LocationCm, B.LocationCm, Alpha);

	// Angles have to go the short way round or a car turning through north spins 359 degrees the
	// wrong way in slow motion - which is exactly the shot a replay tool exists to take.
	const auto LerpAngles = [Alpha](const FVector3f& From, const FVector3f& To)
	{
		return FVector3f(
			From.X + FMath::UnwindDegrees(To.X - From.X) * Alpha,
			From.Y + FMath::UnwindDegrees(To.Y - From.Y) * Alpha,
			From.Z + FMath::UnwindDegrees(To.Z - From.Z) * Alpha);
	};
	Result.RotationDeg = LerpAngles(A.RotationDeg, B.RotationDeg);
	Result.VisualRotationDeg = LerpAngles(A.VisualRotationDeg, B.VisualRotationDeg);

	// The rotor angle strobes rather than sweeps past lift RPM (FUN_00487740 advances it a fixed
	// 39.1 degrees a step), so unwinding it is right for the same reason it is right for a yaw.
	Result.AuxA = A.AuxA + FMath::UnwindDegrees(B.AuxA - A.AuxA) * Alpha;
	Result.AuxB = A.AuxB + FMath::UnwindDegrees(B.AuxB - A.AuxB) * Alpha;
	return Result;
}

// ---------------------------------------------------------------------------------------------
// FReplayActorTrack
// ---------------------------------------------------------------------------------------------

bool FReplayActorTrack::Sample(const float Frame, FReplayActorState& OutState) const
{
	if (Keys.Num() == 0)
	{
		return false;
	}

	// Outside the actor's life the caller hides it. The bounds are inclusive: an actor recorded on
	// a single frame is still visible on that frame.
	if (Frame < static_cast<float>(FirstFrame) || Frame > static_cast<float>(LastFrame) + 1.0f)
	{
		return false;
	}

	// Upper bound on FrameIndex: the first key strictly after the requested frame.
	int32 Low = 0;
	int32 High = Keys.Num();
	while (Low < High)
	{
		const int32 Mid = Low + (High - Low) / 2;
		if (static_cast<float>(Keys[Mid].FrameIndex) <= Frame)
		{
			Low = Mid + 1;
		}
		else
		{
			High = Mid;
		}
	}

	if (Low == 0)
	{
		OutState = Keys[0].State;
		return true;
	}

	const FReplayKey& Previous = Keys[Low - 1];
	if (Low >= Keys.Num())
	{
		OutState = Previous.State;
		return true;
	}

	const FReplayKey& Next = Keys[Low];
	const float Span = static_cast<float>(Next.FrameIndex - Previous.FrameIndex);
	const float Alpha = Span > 0.0f ? (Frame - static_cast<float>(Previous.FrameIndex)) / Span : 0.0f;
	OutState = FReplayActorState::Blend(Previous.State, Next.State, FMath::Clamp(Alpha, 0.0f, 1.0f));
	return true;
}

int64 FReplayActorTrack::GetApproximateMemoryBytes() const
{
	return static_cast<int64>(sizeof(FReplayActorTrack))
		+ static_cast<int64>(Keys.GetAllocatedSize())
		+ static_cast<int64>(Label.GetAllocatedSize());
}

// ---------------------------------------------------------------------------------------------
// FReplayMnemonicTable
// ---------------------------------------------------------------------------------------------

uint16 FReplayMnemonicTable::Intern(const FString& Mnemonic)
{
	if (Mnemonic.IsEmpty())
	{
		return FReplayActorState::NoClip;
	}
	if (const uint16* Existing = Lookup.Find(Mnemonic))
	{
		return *Existing;
	}
	// NoClip is the sentinel, so it can never be a real index. Eighteen clips ship; anything that
	// pushed this to 65535 would be a bug, not a big city.
	if (Names.Num() >= FReplayActorState::NoClip)
	{
		return FReplayActorState::NoClip;
	}
	const uint16 Id = static_cast<uint16>(Names.Add(Mnemonic));
	Lookup.Add(Mnemonic, Id);
	return Id;
}

const FString* FReplayMnemonicTable::Resolve(const uint16 Id) const
{
	return Names.IsValidIndex(Id) ? &Names[Id] : nullptr;
}

void FReplayMnemonicTable::SetNames(TArray<FString>&& InNames)
{
	Names = MoveTemp(InNames);
	Lookup.Reset();
	for (int32 Index = 0; Index < Names.Num() && Index < FReplayActorState::NoClip; ++Index)
	{
		Lookup.Add(Names[Index], static_cast<uint16>(Index));
	}
}

void FReplayMnemonicTable::Reset()
{
	Names.Reset();
	Lookup.Reset();
}

// ---------------------------------------------------------------------------------------------
// FReplayClip
// ---------------------------------------------------------------------------------------------

float FReplayClip::GetDurationSeconds() const
{
	return static_cast<float>(FMath::Max(FrameCount - 1, 0)) * FrameIntervalSeconds;
}

int64 FReplayClip::GetApproximateMemoryBytes() const
{
	int64 Bytes = static_cast<int64>(sizeof(FReplayClip));
	Bytes += static_cast<int64>(Tracks.GetAllocatedSize());
	for (const FReplayActorTrack& Track : Tracks)
	{
		Bytes += Track.GetApproximateMemoryBytes();
	}
	Bytes += static_cast<int64>(Events.GetAllocatedSize());
	for (const FReplayEvent& Event : Events)
	{
		Bytes += static_cast<int64>(Event.Text.GetAllocatedSize());
	}
	return Bytes;
}

void FReplayClip::Reset()
{
	Name.Reset();
	LevelId.Reset();
	LevelDisplayName.Reset();
	RecordedAtUtc = FDateTime(0);
	FrameIntervalSeconds = SimCopterReplay::FrameIntervalSeconds;
	FrameCount = 0;
	ClipMnemonics.Reset();
	Tracks.Reset();
	Events.Reset();
}

int32 FReplayClip::TimeToFrame(const float Seconds) const
{
	if (FrameIntervalSeconds <= 0.0f)
	{
		return 0;
	}
	return FMath::Clamp(
		FMath::RoundToInt(Seconds / FrameIntervalSeconds),
		0,
		FMath::Max(FrameCount - 1, 0));
}

float FReplayClip::FrameToTime(const float Frame) const
{
	return Frame * FrameIntervalSeconds;
}

bool FReplayClip::Serialize(FArchive& Archive, FString& OutError)
{
	OutError.Reset();

	uint32 Magic = ClipFileMagic;
	int32 Version = ClipFileVersion;
	Archive << Magic;
	Archive << Version;

	if (Archive.IsLoading())
	{
		if (Magic != ClipFileMagic)
		{
			OutError = TEXT("That file is not a SimCopter replay clip.");
			return false;
		}
		if (Version != ClipFileVersion)
		{
			OutError = FString::Printf(
				TEXT("That clip was recorded by a different version of the game (clip format %d, this build reads %d)."),
				Version,
				ClipFileVersion);
			return false;
		}
	}

	Archive << Name;
	Archive << LevelId;
	Archive << LevelDisplayName;

	int64 RecordedTicks = RecordedAtUtc.GetTicks();
	Archive << RecordedTicks;
	if (Archive.IsLoading())
	{
		// A corrupt tick count would throw from the FDateTime constructor, and a clip file is
		// ordinary user data that may well be truncated.
		RecordedAtUtc = (RecordedTicks >= 0 && RecordedTicks <= FDateTime::MaxValue().GetTicks())
			? FDateTime(RecordedTicks)
			: FDateTime(0);
	}

	Archive << FrameIntervalSeconds;
	Archive << FrameCount;
	Archive << ClipMnemonics;

	int32 TrackCount = Tracks.Num();
	Archive << TrackCount;
	if (Archive.IsLoading())
	{
		if (TrackCount < 0 || FrameCount < 0 || FrameIntervalSeconds <= 0.0f)
		{
			OutError = TEXT("That clip file is damaged.");
			return false;
		}
		Tracks.Empty(TrackCount);
		Tracks.SetNum(TrackCount);
	}

	for (FReplayActorTrack& Track : Tracks)
	{
		Archive << Track.ActorId;
		// Written as its underlying byte rather than relying on the archive's enum overload, so a
		// later reordering of the enum is a version bump and not a silent reinterpretation.
		uint8 KindByte = static_cast<uint8>(Track.Kind);
		Archive << KindByte;
		Track.Kind = static_cast<EReplayActorKind>(KindByte);
		Archive << Track.Label;
		Archive << Track.PersonState;
		Archive << Track.Spawn.MeshTableName;
		Archive << Track.Spawn.FigureName;
		Archive << Track.Spawn.BehaviorClass;
		Archive << Track.Spawn.ClothesOffset;
		Archive << Track.FirstFrame;
		Archive << Track.LastFrame;

		int32 KeyCount = Track.Keys.Num();
		Archive << KeyCount;
		if (Archive.IsLoading())
		{
			if (KeyCount < 0)
			{
				OutError = TEXT("That clip file is damaged.");
				return false;
			}
			Track.Keys.Empty(KeyCount);
			Track.Keys.SetNum(KeyCount);
		}
		for (FReplayKey& Key : Track.Keys)
		{
			Archive << Key.FrameIndex;
			Archive << Key.State.LocationCm;
			Archive << Key.State.RotationDeg;
			Archive << Key.State.VisualRotationDeg;
			Archive << Key.State.AuxA;
			Archive << Key.State.AuxB;
			Archive << Key.State.ClipId;
			Archive << Key.State.ClipFrame;
			Archive << Key.State.Flags;
		}
	}

	int32 EventCount = Events.Num();
	Archive << EventCount;
	if (Archive.IsLoading())
	{
		if (EventCount < 0)
		{
			OutError = TEXT("That clip file is damaged.");
			return false;
		}
		Events.Empty(EventCount);
		Events.SetNum(EventCount);
	}
	for (FReplayEvent& Event : Events)
	{
		Archive << Event.FrameIndex;
		Archive << Event.ActorId;
		uint8 EventKindByte = static_cast<uint8>(Event.Kind);
		Archive << EventKindByte;
		Event.Kind = static_cast<EReplayEventKind>(EventKindByte);
		Archive << Event.PersonState;
		Archive << Event.WorldLocationCm;
		Archive << Event.Text;
	}

	if (Archive.IsError())
	{
		OutError = TEXT("That clip file could not be read to the end.");
		return false;
	}
	return true;
}
}
