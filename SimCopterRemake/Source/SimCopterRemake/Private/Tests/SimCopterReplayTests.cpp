// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/InputComponent.h"
#include "Game/SimCopterPlayerController.h"
#include "Misc/AutomationTest.h"
#include "Replay/SimCopterReplaySubsystem.h"
#include "Replay/SimCopterReplayTypes.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

// The replay clip model's pure parts: the sparse-key sampling that playback is built on, the round
// trip through the clip file, the mnemonic table, and the name/level rules that decide which clips
// a city offers. Recording and playback themselves need a live world with a population in it, so
// they are not covered here - AGENTS.md section 7.
//
// NOT a port: the original has no replay, so there is no decompiled behaviour to check against and
// nothing here cites a FUN_004xxxxx.

namespace
{
using namespace SimCopterReplay;

FReplayActorState MakeState(const float X, const float Yaw = 0.0f)
{
	FReplayActorState State;
	State.LocationCm = FVector3f(X, 0.0f, 0.0f);
	State.RotationDeg = FVector3f(0.0f, Yaw, 0.0f);
	return State;
}

void AddKey(FReplayActorTrack& Track, const int32 Frame, const FReplayActorState& State)
{
	FReplayKey& Key = Track.Keys.AddDefaulted_GetRef();
	Key.FrameIndex = Frame;
	Key.State = State;
	Track.LastFrame = FMath::Max(Track.LastFrame, Frame);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterReplaySamplingTest,
	"SimCopter.Replay.Sampling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterReplaySamplingTest::RunTest(const FString& Parameters)
{
	FReplayActorTrack Track;
	Track.FirstFrame = 0;
	Track.LastFrame = 0;
	AddKey(Track, 0, MakeState(0.0f));
	AddKey(Track, 10, MakeState(100.0f));

	FReplayActorState Sampled;

	// Exactly on a key.
	TestTrue(TEXT("Frame 0 samples"), Track.Sample(0.0f, Sampled));
	TestEqual(TEXT("Frame 0 is the first key"), Sampled.LocationCm.X, 0.0f);
	TestTrue(TEXT("Frame 10 samples"), Track.Sample(10.0f, Sampled));
	TestEqual(TEXT("Frame 10 is the second key"), Sampled.LocationCm.X, 100.0f);

	// Between two keys. This is the whole reason a 20 Hz recording plays smoothly at 144 fps and
	// at 0.1x speed, so it is the one behaviour worth pinning exactly.
	TestTrue(TEXT("Frame 5 samples"), Track.Sample(5.0f, Sampled));
	TestEqual(TEXT("Frame 5 is halfway between the keys"), Sampled.LocationCm.X, 50.0f);
	TestTrue(TEXT("Fractional frame samples"), Track.Sample(2.5f, Sampled));
	TestEqual(TEXT("Frame 2.5 is a quarter of the way"), Sampled.LocationCm.X, 25.0f);

	// Before the actor existed and after it despawned, so playback hides it rather than showing it
	// parked at the first or last pose it ever had.
	FReplayActorTrack Late;
	Late.FirstFrame = 20;
	Late.LastFrame = 20;
	AddKey(Late, 20, MakeState(5.0f));
	TestFalse(TEXT("Before the first frame the actor does not exist"), Late.Sample(0.0f, Sampled));
	TestTrue(TEXT("On its own frame it does"), Late.Sample(20.0f, Sampled));
	TestFalse(TEXT("Well after the last frame it does not"), Late.Sample(40.0f, Sampled));

	// An empty track can be asked and must simply say no.
	FReplayActorTrack Empty;
	TestFalse(TEXT("An empty track never samples"), Empty.Sample(0.0f, Sampled));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterReplayBlendTest,
	"SimCopter.Replay.Blend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterReplayBlendTest::RunTest(const FString& Parameters)
{
	// A car turning through north: 350 -> 10 degrees is a 20 degree turn to the right, not a 340
	// degree spin to the left. Interpolating the raw numbers gets this wrong, and gets it wrong
	// most visibly in slow motion - which is the shot a replay tool exists to take.
	const FReplayActorState From = MakeState(0.0f, 350.0f);
	const FReplayActorState To = MakeState(0.0f, 10.0f);
	const FReplayActorState Mid = FReplayActorState::Blend(From, To, 0.5f);
	TestEqual(
		TEXT("A yaw across north takes the short way"),
		static_cast<float>(FRotator::ClampAxis(Mid.RotationDeg.Y)),
		0.0f);

	// The discrete fields are steps, not ramps: half a pose is not a pose.
	FReplayActorState PoseA = MakeState(0.0f);
	PoseA.ClipId = 3;
	PoseA.ClipFrame = 1;
	PoseA.Flags = FReplayActorState::FlagNone;
	FReplayActorState PoseB = MakeState(10.0f);
	PoseB.ClipId = 7;
	PoseB.ClipFrame = 4;
	PoseB.Flags = FReplayActorState::FlagHidden;

	const FReplayActorState Blended = FReplayActorState::Blend(PoseA, PoseB, 0.5f);
	TestEqual(TEXT("Clip id holds until the next key"), static_cast<int32>(Blended.ClipId), 3);
	TestEqual(TEXT("Clip frame holds until the next key"), static_cast<int32>(Blended.ClipFrame), 1);
	TestEqual(TEXT("Flags hold until the next key"), static_cast<int32>(Blended.Flags), 0);
	TestEqual(TEXT("Position still interpolates"), Blended.LocationCm.X, 5.0f);

	// Alpha 1 must land exactly on B's continuous fields, or a key is never quite reached.
	const FReplayActorState End = FReplayActorState::Blend(PoseA, PoseB, 1.0f);
	TestEqual(TEXT("Alpha 1 reaches the second key's position"), End.LocationCm.X, 10.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterReplayDifferenceTest,
	"SimCopter.Replay.KeyThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterReplayDifferenceTest::RunTest(const FString& Parameters)
{
	// The sparse rule is what keeps a clip's memory manageable: several hundred parked cars and
	// idle pedestrians must cost one key each for the whole take, not one per frame.
	const FReplayActorState Base = MakeState(100.0f);

	FReplayActorState Identical = Base;
	TestFalse(TEXT("An unchanged state needs no key"), Identical.DiffersFrom(Base));

	FReplayActorState Jitter = Base;
	Jitter.LocationCm.X += 0.001f;
	TestFalse(TEXT("Sub-threshold movement needs no key"), Jitter.DiffersFrom(Base));

	FReplayActorState Moved = Base;
	Moved.LocationCm.X += 1.0f;
	TestTrue(TEXT("A centimetre of movement needs a key"), Moved.DiffersFrom(Base));

	// A pose swap and a despawn are step changes and must always take a key however still the body
	// is - this is what stops a person who stopped walking from keeping the walk pose.
	FReplayActorState Posed = Base;
	Posed.ClipId = 2;
	TestTrue(TEXT("A pose change always needs a key"), Posed.DiffersFrom(Base));

	FReplayActorState Hidden = Base;
	Hidden.Flags = FReplayActorState::FlagHidden;
	TestTrue(TEXT("A despawn always needs a key"), Hidden.DiffersFrom(Base));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterReplayMnemonicTableTest,
	"SimCopter.Replay.MnemonicTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterReplayMnemonicTableTest::RunTest(const FString& Parameters)
{
	FReplayMnemonicTable Table;

	const uint16 Walk = Table.Intern(TEXT("1Wal"));
	const uint16 Idle = Table.Intern(TEXT("NoMo"));
	TestNotEqual(TEXT("Two clips get two ids"), Walk, Idle);
	TestEqual(TEXT("Interning the same clip twice gives the same id"), Table.Intern(TEXT("1Wal")), Walk);

	const FString* Resolved = Table.Resolve(Walk);
	TestTrue(TEXT("A known id resolves"), Resolved != nullptr && *Resolved == TEXT("1Wal"));
	TestNull(TEXT("The no-clip sentinel resolves to nothing"), Table.Resolve(FReplayActorState::NoClip));

	// An actor with no privanim figure at all - every car in the city - must land on the sentinel
	// rather than adding an empty name to the table.
	TestEqual(TEXT("An empty mnemonic is the sentinel"), Table.Intern(FString()), FReplayActorState::NoClip);

	// Reloading a clip rebuilds the table from the file's list, and the ids stored in its keys have
	// to keep meaning what they meant when they were written.
	FReplayMnemonicTable Reloaded;
	Reloaded.SetNames(TArray<FString>(Table.GetNames()));
	const FString* ReloadedWalk = Reloaded.Resolve(Walk);
	TestTrue(TEXT("A reloaded table resolves the original ids"), ReloadedWalk != nullptr && *ReloadedWalk == TEXT("1Wal"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterReplayClipSerializationTest,
	"SimCopter.Replay.ClipSerialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterReplayClipSerializationTest::RunTest(const FString& Parameters)
{
	FReplayClip Written;
	Written.Name = TEXT("Bridge Pass");
	Written.LevelId = TEXT("career:7");
	Written.LevelDisplayName = TEXT("Career City 7");
	Written.RecordedAtUtc = FDateTime(2026, 8, 12, 9, 30, 0);
	Written.FrameCount = 41;
	Written.ClipMnemonics = { TEXT("1Wal"), TEXT("NoMo") };

	FReplayActorTrack& Track = Written.Tracks.AddDefaulted_GetRef();
	Track.ActorId = 1;
	Track.Kind = EReplayActorKind::Pedestrian;
	Track.Label = TEXT("Robber");
	Track.PersonState = 10;
	Track.Spawn.MeshTableName = TEXT("PERSON1");
	Track.Spawn.FigureName = TEXT("Robbr");
	Track.Spawn.BehaviorClass = 4;
	Track.Spawn.ClothesOffset = 6;
	Track.FirstFrame = 0;
	AddKey(Track, 0, MakeState(0.0f, 90.0f));
	AddKey(Track, 40, MakeState(400.0f, 180.0f));
	Track.Keys.Last().State.ClipId = 0;
	Track.Keys.Last().State.ClipFrame = 3;

	FReplayEvent& Event = Written.Events.AddDefaulted_GetRef();
	Event.FrameIndex = 12;
	Event.ActorId = 1;
	Event.Kind = EReplayEventKind::PersonDecision;
	Event.PersonState = 10;
	Event.WorldLocationCm = FVector3f(1.0f, 2.0f, 3.0f);
	Event.Text = TEXT("SELECT class 5 -> 'CARPOLIC' at 3 tiles");

	// A sound event carries its meaning entirely in its payload and has no text at all, so it is
	// the one kind that would be silently dropped by a text-only round trip.
	FReplayEvent& Sound = Written.Events.AddDefaulted_GetRef();
	Sound.FrameIndex = 20;
	Sound.Kind = EReplayEventKind::SoundStart;
	Sound.PayloadA = 0x1d;   // sound table slot
	Sound.PayloadB = 1;      // play flags
	Sound.WorldLocationCm = FVector3f(400.0f, 800.0f, 120.0f);

	// Particles are spawned by gameplay, and gameplay is frozen during a review - so the creator
	// calls themselves are recorded and re-issued. Losing this track is the difference between a
	// replay with fire, water and rotor wash in it and a replay of a city where nothing happens.
	Written.EffectChannels.Add(TEXT("SimCopterHelicopterPawn.WaterFX"));
	FReplayEffectSpawn& Spawn = Written.EffectSpawns.AddDefaulted_GetRef();
	Spawn.FrameIndex = 7;
	Spawn.Kind = EReplayEffectSpawn::SplashColumn;
	Spawn.ChannelId = 0;
	Spawn.TypeValue = 4;
	Spawn.CellX = 12;
	Spawn.CellY = 34;
	Spawn.PaletteIndex = 0x0c;
	Spawn.Flags = FReplayEffectSpawn::FlagBoolArgument;
	Spawn.LocationCm = FVector3f(10.0f, 20.0f, 30.0f);
	Spawn.VelocityCmPerSec = FVector3f(1.0f, 2.0f, 3.0f);
	Spawn.SizeCm = 40.0f;
	Spawn.LifeSeconds = 1.5f;
	Spawn.GravityCmPerSec2 = -980.0f;
	Spawn.Color = FLinearColor(0.25f, 0.5f, 0.75f, 1.0f);

	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes);
		FString Error;
		TestTrue(TEXT("A clip serializes out"), Written.Serialize(Writer, Error));
		TestTrue(TEXT("Writing reports no error"), Error.IsEmpty());
	}
	TestTrue(TEXT("Something was written"), Bytes.Num() > 0);

	FReplayClip Read;
	{
		FMemoryReader Reader(Bytes);
		FString Error;
		TestTrue(TEXT("A clip serializes back in"), Read.Serialize(Reader, Error));
	}

	TestEqual(TEXT("Name survives"), Read.Name, Written.Name);
	TestEqual(TEXT("Level id survives"), Read.LevelId, Written.LevelId);
	TestEqual(TEXT("Frame count survives"), Read.FrameCount, Written.FrameCount);
	TestEqual(TEXT("Recorded time survives"), Read.RecordedAtUtc, Written.RecordedAtUtc);
	TestEqual(TEXT("Mnemonic list survives"), Read.ClipMnemonics.Num(), 2);
	TestEqual(TEXT("Track count survives"), Read.Tracks.Num(), 1);
	TestEqual(TEXT("Event count survives"), Read.Events.Num(), 2);
	TestEqual(TEXT("Effect channel list survives"), Read.EffectChannels.Num(), 1);
	TestEqual(TEXT("Effect spawn count survives"), Read.EffectSpawns.Num(), 1);

	if (Read.EffectSpawns.Num() == 1)
	{
		const FReplayEffectSpawn& ReadSpawn = Read.EffectSpawns[0];
		TestEqual(TEXT("Spawn kind survives"), static_cast<int32>(ReadSpawn.Kind), static_cast<int32>(EReplayEffectSpawn::SplashColumn));
		TestEqual(TEXT("Spawn frame survives"), ReadSpawn.FrameIndex, 7);
		TestEqual(TEXT("Spawn channel survives"), static_cast<int32>(ReadSpawn.ChannelId), 0);
		TestEqual(TEXT("Spawn type value survives"), ReadSpawn.TypeValue, 4);
		TestEqual(TEXT("Spawn cell survives"), ReadSpawn.CellY, 34);
		TestEqual(TEXT("Spawn palette survives"), static_cast<int32>(ReadSpawn.PaletteIndex), 0x0c);
		TestTrue(TEXT("Spawn bool argument survives"), ReadSpawn.GetBoolArgument());
		TestEqual(TEXT("Spawn position survives"), ReadSpawn.LocationCm.Z, 30.0f);
		TestEqual(TEXT("Spawn size survives"), ReadSpawn.SizeCm, 40.0f);
		TestEqual(TEXT("Spawn gravity survives"), ReadSpawn.GravityCmPerSec2, -980.0f);
		TestEqual(TEXT("Spawn colour survives"), ReadSpawn.Color.G, 0.5f);
	}

	if (Read.Tracks.Num() == 1)
	{
		const FReplayActorTrack& ReadTrack = Read.Tracks[0];
		TestEqual(TEXT("Track kind survives"), static_cast<int32>(ReadTrack.Kind), static_cast<int32>(EReplayActorKind::Pedestrian));
		TestEqual(TEXT("Track label survives"), ReadTrack.Label, FString(TEXT("Robber")));
		TestEqual(TEXT("Person state survives"), ReadTrack.PersonState, 10);
		// The spawn descriptor is how playback rebuilds a stand-in for a person who despawned hours
		// ago; losing the figure name gives the whole crowd the same body.
		TestEqual(TEXT("Figure name survives"), ReadTrack.Spawn.FigureName, FString(TEXT("Robbr")));
		TestEqual(TEXT("Behaviour class survives"), ReadTrack.Spawn.BehaviorClass, 4);
		TestEqual(TEXT("Clothes offset survives"), ReadTrack.Spawn.ClothesOffset, 6);
		TestEqual(TEXT("Key count survives"), ReadTrack.Keys.Num(), 2);
		if (ReadTrack.Keys.Num() == 2)
		{
			TestEqual(TEXT("Key position survives"), ReadTrack.Keys[1].State.LocationCm.X, 400.0f);
			TestEqual(TEXT("Key clip frame survives"), static_cast<int32>(ReadTrack.Keys[1].State.ClipFrame), 3);
		}
	}

	if (Read.Events.Num() == 2)
	{
		TestEqual(TEXT("Event text survives"), Read.Events[0].Text, FString(TEXT("SELECT class 5 -> 'CARPOLIC' at 3 tiles")));
		TestEqual(TEXT("Event kind survives"), static_cast<int32>(Read.Events[0].Kind), static_cast<int32>(EReplayEventKind::PersonDecision));

		const FReplayEvent& ReadSound = Read.Events[1];
		TestTrue(TEXT("A sound event reads back as a sound"), ReadSound.IsSound());
		TestEqual(TEXT("Sound slot id survives"), ReadSound.PayloadA, 0x1d);
		TestEqual(TEXT("Sound flags survive"), ReadSound.PayloadB, 1);
		// The position is what decides Play3D vs Play2D on the way back out, so losing it would
		// collapse the whole city's audio onto the listener.
		TestEqual(TEXT("Sound position survives"), ReadSound.WorldLocationCm.Y, 800.0f);
	}

	// A clip written by a different build must be refused with a message, not read hopefully: a
	// half-understood clip plays back as a city teleporting into the ground, which reads as a
	// physics bug rather than a format one.
	TArray<uint8> Damaged = Bytes;
	if (Damaged.Num() > 8)
	{
		// The version field sits immediately after the four-byte magic.
		Damaged[4] = static_cast<uint8>(ClipFileVersion + 1);
		FMemoryReader Reader(Damaged);
		FReplayClip Refused;
		FString Error;
		TestFalse(TEXT("A future clip version is refused"), Refused.Serialize(Reader, Error));
		TestFalse(TEXT("Refusing gives a reason"), Error.IsEmpty());
	}

	TArray<uint8> NotAClip;
	NotAClip.AddZeroed(64);
	{
		FMemoryReader Reader(NotAClip);
		FReplayClip Refused;
		FString Error;
		TestFalse(TEXT("A file that is not a clip is refused"), Refused.Serialize(Reader, Error));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterReplayTimebaseTest,
	"SimCopter.Replay.Timebase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterReplayTimebaseTest::RunTest(const FString& Parameters)
{
	// 20 Hz is the original's own simulation period, and it has to stay above both the behaviour
	// VM's 12.5 Hz ceiling and the remake's 15 Hz people tick or a decision could fall between two
	// recorded frames. See Docs/memory/simcopter-people-logic-next.md.
	TestEqual(TEXT("The recorder samples at 20 Hz"), FrameIntervalSeconds, 0.05f);

	FReplayClip Clip;
	Clip.FrameCount = 21;
	TestEqual(TEXT("21 frames is one second"), Clip.GetDurationSeconds(), 1.0f);
	TestEqual(TEXT("Time maps back to a frame"), Clip.TimeToFrame(0.5f), 10);
	TestEqual(TEXT("A frame maps back to time"), Clip.FrameToTime(10.0f), 0.5f);

	// Scrubbing past either end must clamp rather than sampling off the end of the track.
	TestEqual(TEXT("Negative time clamps to the start"), Clip.TimeToFrame(-5.0f), 0);
	TestEqual(TEXT("Time past the end clamps to the last frame"), Clip.TimeToFrame(99.0f), 20);

	FReplayClip Empty;
	TestEqual(TEXT("An empty clip has no duration"), Empty.GetDurationSeconds(), 0.0f);
	TestEqual(TEXT("An empty clip clamps every time to frame zero"), Empty.TimeToFrame(3.0f), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterReplayClipNameTest,
	"SimCopter.Replay.ClipNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterReplayClipNameTest::RunTest(const FString& Parameters)
{
	FString Error;

	TestFalse(TEXT("An empty name is refused"), USimCopterReplaySubsystem::IsClipNameValid(FString(), Error));
	TestFalse(TEXT("Refusing gives a reason"), Error.IsEmpty());
	TestFalse(TEXT("Whitespace alone is refused"), USimCopterReplaySubsystem::IsClipNameValid(TEXT("   "), Error));
	TestTrue(TEXT("A real name is accepted"), USimCopterReplaySubsystem::IsClipNameValid(TEXT("Bridge Pass"), Error));

	TestEqual(
		TEXT("Runs of whitespace collapse"),
		USimCopterReplaySubsystem::NormalizeClipName(TEXT("  Bridge   Pass  ")),
		FString(TEXT("Bridge Pass")));
	TestTrue(
		TEXT("A name is capped at the display limit"),
		USimCopterReplaySubsystem::NormalizeClipName(FString::ChrN(200, TEXT('a'))).Len() <= MaxClipNameLength);

	// Two names that differ only in punctuation must not collide on one file, which is what the
	// name hash in the file name is for - the same rule the saved-game slots use.
	const FString FirstFile = USimCopterReplaySubsystem::MakeClipFileName(TEXT("Take 1"));
	const FString SecondFile = USimCopterReplaySubsystem::MakeClipFileName(TEXT("Take-1"));
	TestNotEqual(TEXT("Names differing only in punctuation get different files"), FirstFile, SecondFile);
	TestEqual(
		TEXT("The same name always gives the same file"),
		USimCopterReplaySubsystem::MakeClipFileName(TEXT("Take 1")),
		FirstFile);

	// The file name must be safe to put on disk whatever was typed.
	const FString Awkward = USimCopterReplaySubsystem::MakeClipFileName(TEXT("../../etc/pass wd?*"));
	for (const TCHAR Character : Awkward)
	{
		if (!FChar::IsAlnum(Character) && Character != TEXT('_'))
		{
			AddError(FString::Printf(TEXT("Clip file name contains '%c', which is not path safe."), Character));
			break;
		}
	}
	TestFalse(TEXT("A name of pure punctuation still produces a file name"),
		USimCopterReplaySubsystem::MakeClipFileName(TEXT("???")).IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterReplayInputConsumptionTest,
	"SimCopter.Replay.InputDoesNotConsume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterReplayInputConsumptionTest::RunTest(const FString& Parameters)
{
	// THE regression guard for this feature, and it has earned its place twice.
	//
	// "Controlled pawn gets last dibs on the input stack" (APlayerController::BuildInputStack): the
	// player controller's InputComponent is processed FIRST, and both action and axis bindings
	// consume their keys by default. The replay bindings deliberately reuse the flight axes -
	// W/S, A/D, Space/LeftCtrl, the mouse look, the wheel - plus C and the right mouse button, so
	// any one of them left consuming silently kills that control for the helicopter, everywhere,
	// with the panel closed and nothing on screen to suggest why.
	ASimCopterPlayerController* Controller = NewObject<ASimCopterPlayerController>(GetTransientPackage());
	UInputComponent* Component = NewObject<UInputComponent>(GetTransientPackage());
	if (!TestNotNull(TEXT("Controller constructed"), Controller)
		|| !TestNotNull(TEXT("Input component constructed"), Component))
	{
		return false;
	}

	ASimCopterPlayerController::BindReplayInput(*Component, *Controller);

	TestTrue(TEXT("Replay binds some actions"), Component->GetNumActionBindings() > 0);
	TestTrue(TEXT("Replay binds some axes"), Component->AxisBindings.Num() > 0);

	for (int32 Index = 0; Index < Component->GetNumActionBindings(); ++Index)
	{
		const FInputActionBinding& Binding = Component->GetActionBinding(Index);
		const FString Name = Binding.GetActionName().ToString();

		// Every replay binding has to survive the pause, because reviewing a clip installs one.
		TestTrue(
			FString::Printf(TEXT("Action '%s' executes when paused"), *Name),
			Binding.bExecuteWhenPaused);

		if (ASimCopterPlayerController::IsReplayExclusiveAction(Binding.GetActionName()))
		{
			// Tab and H are the panel's alone and may consume.
			continue;
		}
		TestFalse(
			FString::Printf(TEXT("Action '%s' shares its key with gameplay and must not consume"), *Name),
			Binding.bConsumeInput);
	}

	for (const FInputAxisBinding& Binding : Component->AxisBindings)
	{
		// Every replay axis is a flight axis. There is no such thing as a replay-exclusive one.
		TestFalse(
			FString::Printf(TEXT("Axis '%s' must not consume"), *Binding.AxisName.ToString()),
			Binding.bConsumeInput);
		TestTrue(
			FString::Printf(TEXT("Axis '%s' executes when paused"), *Binding.AxisName.ToString()),
			Binding.bExecuteWhenPaused);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
