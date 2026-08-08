// Sound-table and mixer-arithmetic tests. Everything checked here is pure: the registration
// table transcribed from FUN_00424b70, the people voice table from FUN_004c5210, and the two
// pieces of maths behind 3D playback (FUN_00468220 and FUN_004247c0). No world required.

#include "Audio/SimCopterAudioSubsystem.h"
#include "Audio/SimCopterSoundTable.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterSoundTableTest,
	"SimCopter.Sound.Table",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterSoundTableTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterSound;

	const TArrayView<const FSoundSlot> Table = GetSlotTable();
	TestEqual(TEXT("FUN_00424b70 registers 0x00..0x83"), Table.Num(), NumSlots);
	TestEqual(TEXT("slot count is 132"), NumSlots, 0x84);

	for (int32 Id = 0; Id < NumSlots; ++Id)
	{
		const FSoundSlot* Slot = GetSlot(Id);
		if (!TestNotNull(*FString::Printf(TEXT("slot 0x%02x exists"), Id), Slot))
		{
			return false;
		}
		TestTrue(
			*FString::Printf(TEXT("slot 0x%02x names a file"), Id),
			Slot->Wav != nullptr && FCString::Strlen(Slot->Wav) > 0);
	}
	TestNull(TEXT("no slot below zero"), GetSlot(-1));
	TestNull(TEXT("no slot past the table"), GetSlot(NumSlots));

	// Spot-check the ids the ported call sites name, at the ends and either side of the two
	// places the sequence does something unusual.
	TestEqual(TEXT("0x00 is the rotor loop"), FString(GetSlot(SND_COPLOOP)->Wav), FString(TEXT("COPLOOP")));
	TestEqual(TEXT("0x02 CHOPSTOP"), FString(GetSlot(SND_CHOPSTOP)->Wav), FString(TEXT("CHOPSTOP")));
	TestEqual(TEXT("0x03 CHOPSTAR"), FString(GetSlot(SND_CHOPSTAR)->Wav), FString(TEXT("CHOPSTAR")));
	TestEqual(TEXT("0x15 WINCHLP"), FString(GetSlot(SND_WINCHLP)->Wav), FString(TEXT("WINCHLP")));
	TestEqual(TEXT("0x1e CA_CHING"), FString(GetSlot(SND_CA_CHING)->Wav), FString(TEXT("CA_CHING")));
	TestEqual(TEXT("0x7f BLIP1"), FString(GetSlot(SND_BLIP1)->Wav), FString(TEXT("BLIP1")));
	TestEqual(TEXT("0x80 NOEQUIP"), FString(GetSlot(SND_NOEQUIP)->Wav), FString(TEXT("NOEQUIP")));
	TestEqual(TEXT("0x81 FIRESTAR"), FString(GetSlot(SND_FIRESTAR)->Wav), FString(TEXT("FIRESTAR")));

	// The original registers ambsrn11.wav twice, at 0x11 and 0x20, because a slot is a voice:
	// one is the ambulance driving to a call and the other is the crash-rescue flyby, and they
	// have to be able to sound at the same time. A "fix" that collapses them is a regression.
	TestEqual(
		TEXT("0x11 and 0x20 are deliberately the same file"),
		FString(GetSlot(SND_AMBSRN11)->Wav),
		FString(GetSlot(SND_AMBSRN2)->Wav));
	TestEqual(TEXT("...namely AMBSRN11"), FString(GetSlot(SND_AMBSRN11)->Wav), FString(TEXT("AMBSRN11")));

	// The four numbered voice runs, at both ends.
	TestEqual(TEXT("0x2f D1000"), FString(GetSlot(SND_D1000)->Wav), FString(TEXT("D1000")));
	TestEqual(TEXT("0x41 D1018"), FString(GetSlot(SND_D1018)->Wav), FString(TEXT("D1018")));
	TestEqual(TEXT("0x42 L001"), FString(GetSlot(SND_L001)->Wav), FString(TEXT("L001")));
	TestEqual(TEXT("0x4a L009"), FString(GetSlot(SND_L009)->Wav), FString(TEXT("L009")));
	TestEqual(TEXT("0x4b D2001"), FString(GetSlot(SND_D2001)->Wav), FString(TEXT("D2001")));
	TestEqual(TEXT("0x5e D2020"), FString(GetSlot(SND_D2020)->Wav), FString(TEXT("D2020")));

	// The dispatcher ids the mission system computes must land on the DIS0xx run, not on the
	// D2### one - mapping them to D2001+ is exactly the bug this table replaced.
	TestEqual(TEXT("0x5f DIS053"), FString(GetSlot(SND_DIS053)->Wav), FString(TEXT("DIS053")));
	TestEqual(TEXT("0x63 DIS057"), FString(GetSlot(0x63)->Wav), FString(TEXT("DIS057")));
	TestEqual(TEXT("0x64 DIS058"), FString(GetSlot(0x64)->Wav), FString(TEXT("DIS058")));
	TestEqual(TEXT("0x6e DIS068"), FString(GetSlot(SND_DIS068)->Wav), FString(TEXT("DIS068")));

	// Language routing: the spoken lines resolve against the language folder, the effects do not.
	TestTrue(TEXT("D1000 is a language clip"), GetSlot(SND_D1000)->Dir == ESoundDir::Language);
	TestTrue(TEXT("DIS053 is a language clip"), GetSlot(SND_DIS053)->Dir == ESoundDir::Language);
	TestTrue(TEXT("COPLOOP is not"), GetSlot(SND_COPLOOP)->Dir == ESoundDir::Root);
	TestTrue(TEXT("aDrOpen is not"), GetSlot(SND_ADROPEN)->Dir == ESoundDir::Root);

	// The people bank: fourteen slots, all seeded with xWhoa by the loop that closes
	// FUN_00424b70, and swapped per speaker at runtime.
	TestEqual(TEXT("bank is 0x71..0x7e"), VoiceBankCount, 14);
	for (int32 Id = VoiceBankFirst; Id <= VoiceBankLast; ++Id)
	{
		TestTrue(*FString::Printf(TEXT("0x%02x is bank"), Id), IsVoiceBankSlot(Id));
		TestEqual(
			*FString::Printf(TEXT("0x%02x seeded with xWhoa"), Id),
			FString(GetSlot(Id)->Wav),
			FString(TEXT("xWhoa")));
	}
	TestFalse(TEXT("0x70 is not bank"), IsVoiceBankSlot(VoiceBankFirst - 1));
	TestFalse(TEXT("0x7f is not bank"), IsVoiceBankSlot(VoiceBankLast + 1));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterGameplaySoundMappingsTest,
	"SimCopter.Sound.GameplayMappings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterGameplaySoundMappingsTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterSound;

	TestEqual(TEXT("Firework mortar uses sound 0x17"), FireworkMortarSound, SND_TGSHWH);
	TestEqual(TEXT("Enter copter uses sound 0x25"), EnterCopterSound, SND_DOROPN);
	TestEqual(TEXT("Exit copter uses sound 0x26"), ExitCopterSound, SND_DORCLS);
	TestEqual(TEXT("Level complete uses DIS063 at 0x69"), LevelCompleteVoiceSound, SND_DIS063);
	TestEqual(TEXT("Passenger door changes use people event 60"), VOX_DOOR_OPEN, 60);

	const FSoundSlot* FireworkMortar = GetSlot(FireworkMortarSound);
	const FSoundSlot* EnterCopter = GetSlot(EnterCopterSound);
	const FSoundSlot* ExitCopter = GetSlot(ExitCopterSound);
	const FSoundSlot* LevelComplete = GetSlot(LevelCompleteVoiceSound);
	if (TestNotNull(TEXT("Firework mortar slot exists"), FireworkMortar))
	{
		TestEqual(TEXT("Firework mortar file"), FString(FireworkMortar->Wav), FString(TEXT("TGSHWH")));
	}
	if (TestNotNull(TEXT("Enter copter slot exists"), EnterCopter))
	{
		TestEqual(TEXT("Enter copter file"), FString(EnterCopter->Wav), FString(TEXT("DOROPN")));
	}
	if (TestNotNull(TEXT("Exit copter slot exists"), ExitCopter))
	{
		TestEqual(TEXT("Exit copter file"), FString(ExitCopter->Wav), FString(TEXT("DORCLS")));
	}
	if (TestNotNull(TEXT("Level complete slot exists"), LevelComplete))
	{
		TestEqual(TEXT("Level complete file"), FString(LevelComplete->Wav), FString(TEXT("DIS063")));
	}
	const FVoiceEvent* PassengerDoor = GetVoiceEvent(VOX_DOOR_OPEN);
	if (TestNotNull(TEXT("Passenger door voice event exists"), PassengerDoor))
	{
		TestEqual(TEXT("Passenger door event has no randomized alternatives"), PassengerDoor->Clips.Num(), 1);
		TestEqual(TEXT("Passenger door event file"), FString(PassengerDoor->Clips[0]), FString(TEXT("doropn")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterVoiceEventTableTest,
	"SimCopter.Sound.VoiceEvents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterVoiceEventTableTest::RunTest(const FString& Parameters)
{
	using namespace SimCopterSound;

	const TArrayView<const FVoiceEvent> Events = GetVoiceEventTable();
	TestEqual(TEXT("FUN_004c5210 switches on 62 events"), Events.Num(), 62);

	int32 Previous = 0;
	for (const FVoiceEvent& Event : Events)
	{
		TestTrue(TEXT("events ascend"), Event.Event > Previous);
		Previous = Event.Event;
		TestTrue(
			*FString::Printf(TEXT("event %d names at least one clip"), Event.Event),
			Event.Clips.Num() > 0);
	}
	TestEqual(TEXT("events run 1..62"), Previous, 62);

	// A handful of the picks, including the two shapes the decompile uses (an inner switch and
	// a pair of if/else branches) and the largest list.
	const FVoiceEvent* Assert = GetVoiceEvent(VOX_ASSERT);
	if (TestNotNull(TEXT("event 1 exists"), Assert))
	{
		TestEqual(TEXT("assert has four takes"), Assert->Clips.Num(), 4);
		TestEqual(TEXT("first is assert1"), FString(Assert->Clips[0]), FString(TEXT("assert1")));
	}
	const FVoiceEvent* Query = GetVoiceEvent(VOX_QUERY);
	if (TestNotNull(TEXT("event 4 exists"), Query))
	{
		TestEqual(TEXT("query is the if/else pair"), Query->Clips.Num(), 2);
	}
	const FVoiceEvent* Dying = GetVoiceEvent(VOX_DYING);
	if (TestNotNull(TEXT("event 13 exists"), Dying))
	{
		TestEqual(TEXT("achdie has three takes"), Dying->Clips.Num(), 3);
	}
	TestNull(TEXT("event 0 does not exist"), GetVoiceEvent(0));
	TestNull(TEXT("event 63 does not exist"), GetVoiceEvent(63));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterSoundAttenuationTest,
	"SimCopter.Sound.Attenuation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterSoundAttenuationTest::RunTest(const FString& Parameters)
{
	constexpr int32 One = 0x10000;

	// FUN_00468220: largest component plus a quarter of the other two. Not a length - the
	// audible region it defines is an octagon, and Play3D's cull uses it verbatim.
	TestEqual(TEXT("norm of zero"), USimCopterAudioSubsystem::OctagonalNorm1616(0, 0, 0), 0);
	TestEqual(TEXT("axis-aligned norm is the axis"), USimCopterAudioSubsystem::OctagonalNorm1616(100 * One, 0, 0), 100 * One);
	TestEqual(TEXT("norm ignores sign"), USimCopterAudioSubsystem::OctagonalNorm1616(-100 * One, 0, 0), 100 * One);
	TestEqual(TEXT("z-dominant norm is the axis"), USimCopterAudioSubsystem::OctagonalNorm1616(0, 0, -100 * One), 100 * One);

	// 100,100,0 -> max 100 plus a quarter of (100 + 0). The exact value matters: it is what
	// decides whether a sound diagonally away from the listener is inside the cull radius.
	TestEqual(
		TEXT("diagonal norm is max + (rest >> 2)"),
		USimCopterAudioSubsystem::OctagonalNorm1616(100 * One, 100 * One, 0),
		125 * One);
	TestEqual(
		TEXT("all three axes"),
		USimCopterAudioSubsystem::OctagonalNorm1616(100 * One, 40 * One, 40 * One),
		120 * One);
	// Beta is 1/4 where the best 2D fit would be about 0.4, so the approximation UNDER-estimates
	// the true length off-axis: 125 against 141.4 on the diagonal. The audible region is
	// therefore an octagon that reaches further diagonally than along the axes - a sound is
	// still heard at ~2172 true units on a diagonal but cuts out at 1920 straight ahead.
	const float DiagonalNorm =
		static_cast<float>(USimCopterAudioSubsystem::OctagonalNorm1616(100 * One, 100 * One, 0)) / One;
	TestTrue(TEXT("octagonal norm under-estimates the diagonal"), DiagonalNorm < FMath::Sqrt(2.0f) * 100.0f);
	TestTrue(TEXT("...but never the axis"), DiagonalNorm >= 100.0f);
	TestTrue(
		TEXT("diagonal audibility reaches about 2172 units"),
		FMath::IsNearlyEqual(
			USimCopterAudioSubsystem::AudibleRangeUnits / DiagonalNorm * 100.0f * FMath::Sqrt(2.0f),
			2172.0f,
			1.0f));

	// FUN_004247c0: 10000 at the listener, falling linearly to 6000 at the 1920-unit edge.
	// That is only -40 dB over the entire audible range, which is why distant sounds in the
	// original stay so present right up to the point they vanish.
	TestEqual(TEXT("full volume at the listener"), USimCopterAudioSubsystem::DistanceVolumeIndex(0.0f), 10000);
	TestEqual(
		TEXT("half range is -2000"),
		USimCopterAudioSubsystem::DistanceVolumeIndex(USimCopterAudioSubsystem::AudibleRangeUnits * 0.5f),
		8000);
	TestEqual(
		TEXT("cull edge is 6000, not silence"),
		USimCopterAudioSubsystem::DistanceVolumeIndex(USimCopterAudioSubsystem::AudibleRangeUnits),
		6000);
	TestEqual(TEXT("attenuation span is 4000"), USimCopterAudioSubsystem::DistanceAttenuationSpan, 4000);

	// The index is hundredths of a decibel below unity, DirectSound's own unit.
	TestEqual(TEXT("10000 is unity gain"), USimCopterAudioSubsystem::VolumeIndexToGain(10000), 1.0f);
	TestEqual(TEXT("0 is silence"), USimCopterAudioSubsystem::VolumeIndexToGain(0), 0.0f);
	TestTrue(TEXT("negative index clamps to silence"), USimCopterAudioSubsystem::VolumeIndexToGain(-500) == 0.0f);
	TestTrue(TEXT("over-unity index clamps to unity"), USimCopterAudioSubsystem::VolumeIndexToGain(20000) == 1.0f);
	// -20 dB is a tenth of the amplitude.
	TestTrue(
		TEXT("-2000 index is -20 dB"),
		FMath::IsNearlyEqual(USimCopterAudioSubsystem::VolumeIndexToGain(8000), 0.1f, 1e-4f));
	TestTrue(
		TEXT("gain falls with distance"),
		USimCopterAudioSubsystem::VolumeIndexToGain(
			USimCopterAudioSubsystem::DistanceVolumeIndex(USimCopterAudioSubsystem::AudibleRangeUnits)) <
		USimCopterAudioSubsystem::VolumeIndexToGain(
			USimCopterAudioSubsystem::DistanceVolumeIndex(0.0f)));

	// One original unit is 6.25 cm, so the audible radius is 120 m.
	TestEqual(TEXT("unit scale matches the effect layer"), USimCopterAudioSubsystem::OriginalUnitToCm, 6.25f);
	TestEqual(
		TEXT("audible radius is 12000 cm"),
		USimCopterAudioSubsystem::AudibleRangeUnits * USimCopterAudioSubsystem::OriginalUnitToCm,
		12000.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterRotorSoundLawTest,
	"SimCopter.Sound.RotorLaw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterRotorSoundLawTest::RunTest(const FString& Parameters)
{
	// FUN_00488fd0's two laws, restated so a future edit to the pawn cannot quietly change them.
	//   pitch  : AddFrequency(0, (rpm * 4 - 0x5a0) * 0xf)
	//   volume : SetVolumeAdjust(0, (rpm - 0x168) / 4)
	auto FrequencyDelta = [](int32 Rpm) { return (Rpm * 4 - 0x5a0) * 0xf; };
	auto VolumeAdjust = [](int32 Rpm) { return (Rpm - 0x168) / 4; };

	// 0x5a0 is 4 * 360, so the loop plays at its recorded rate at exactly 360 rpm and nowhere
	// else. Everything below that is pitched down - the rotor's whole audible character.
	TestEqual(TEXT("360 rpm plays at the clip's own rate"), FrequencyDelta(360), 0);
	TestEqual(TEXT("300 rpm is -3600 Hz"), FrequencyDelta(300), -3600);
	TestTrue(TEXT("pitch rises with rpm"), FrequencyDelta(300) < FrequencyDelta(360));

	// Against the 11025 Hz the effects are recorded at, the lift gate lands near 0.67x.
	const float GateRatio = (11025.0f + FrequencyDelta(300)) / 11025.0f;
	TestTrue(TEXT("300 rpm is about 0.67x"), FMath::IsNearlyEqual(GateRatio, 0.6735f, 1e-3f));

	// Volume barely moves across the whole range: about -80 index points, under a decibel.
	TestEqual(TEXT("360 rpm is unity"), VolumeAdjust(360), 0);
	TestEqual(TEXT("300 rpm is -15"), VolumeAdjust(300), -15);
	TestEqual(TEXT("the 30 rpm gate is -82"), VolumeAdjust(30), -82);
	TestTrue(
		TEXT("volume swing over the whole range is under a decibel"),
		FMath::Abs(VolumeAdjust(30) - VolumeAdjust(360)) < 100);

	return true;
}
