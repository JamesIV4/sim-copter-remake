// Copyright Epic Games, Inc. All Rights Reserved.
//
// The seat window and the head a person wears, both decoded from the executable:
//
//   FUN_004c71c0  behavior class -> head image / voice pitch / voice set, applied once at spawn
//   FUN_004c7090  setting state 6 overwrites the head with 10, the bandaged one
//   FUN_0048bff0  seat manifest add: head from person+0x18e, face 1, person id
//   FUN_0048c0e0  seat manifest set-face, which is all people opcode 54 does
//   FUN_00453f70  the blit: column = head + 1, row = the face, cells of 27x33
//   BHAV 264      'Face vs. speed/health' picks that face
//   FUN_004c5210  the voice dispatcher, including the EKG's health-driven rate

#if WITH_DEV_AUTOMATION_TESTS

#include "Audio/SimCopterSoundTable.h"
#include "Engine/Texture2D.h"
#include "Formats/SimCopterPeopleCityRules.h"
#include "Formats/SimCopterPeopleReader.h"
#include "Ground/SimCopterBehaviorVM.h"
#include "Ground/SimCopterGroundAgent.h"
#include "Ground/SimCopterPopulationFigure.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Styling/SlateBrush.h"
#include "UI/SimCopterHangarArt.h"

namespace
{
// Just enough world for BHAV 264: it reads the helicopter speed through opcode 55 and writes the
// seat face through opcode 54. Everything else it touches is an expression on the attributes.
class FStubFaceWorld : public ISimCopterBehaviorWorld
{
public:
	int32 PlayerSpeed = 0;
	int32 LastMood = INDEX_NONE;
	int32 MoodWrites = 0;
	TSet<int32> UnknownOpcodes;

	virtual int32 GetCurrentTileClass() const override { return 7; }
	virtual bool IsTileClassAllowedForState(int32, int32) const override { return true; }
	virtual bool MoveStep(FSimCopterPersonContext&) override { return true; }
	virtual bool IsThreatNearby(const FSimCopterPersonContext&) const override { return false; }
	virtual int32 GetPlayerHelicopterSpeed() const override { return PlayerSpeed; }
	virtual void SetSeatPortraitMood(int32 Mood) override
	{
		LastMood = Mood;
		++MoodWrites;
	}
	// Record 0 of BHAV 264 gates the whole health/speed graph on riding the player.
	virtual bool IsCarrierPlayerHelicopter() const override { return true; }
	virtual void OnUnknownOpcode(int32 Opcode) override { UnknownOpcodes.Add(Opcode); }
};

// Runs 264 until it writes a face, which it always does within a couple of records.
int32 RunFaceProgram(const FPeopleBehaviorModel& Model, int32 Head, int32 Health, int32 Speed, int32 WrittenOff = 0)
{
	FSimCopterPersonContext Context;
	FStubFaceWorld World;
	World.PlayerSpeed = Speed;
	Context.Stack.Add({264, 0, {}});
	Context.Attributes[EBhavAttr::HeadImageIndex] = uint16(Head);
	Context.Attributes[EBhavAttr::MedevacHealth] = uint16(Health);
	Context.Attributes[EBhavAttr::WrittenOff] = uint16(WrittenOff);
	for (int32 Tick = 0; Tick < 40 && World.MoodWrites == 0; ++Tick)
	{
		FSimCopterBehaviorVM::Tick(Context, Model, World);
	}
	return World.LastMood;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterPassengerFallGravityTest,
	"SimCopter.Passengers.FallGravity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterPassengerFallGravityTest::RunTest(const FString& Parameters)
{
	float VerticalVelocity = 0.0f;
	float Height = ASimCopterGroundAgent::IntegratePedestrianGravityStep(
		1000.0f,
		0.5f,
		980.0f,
		VerticalVelocity);
	TestEqual(TEXT("A released passenger accelerates downward"), VerticalVelocity, -490.0f);
	TestEqual(TEXT("The first gravity step lowers the passenger"), Height, 755.0f);

	Height = ASimCopterGroundAgent::IntegratePedestrianGravityStep(
		Height,
		0.5f,
		980.0f,
		VerticalVelocity);
	TestEqual(TEXT("Gravity accumulates while no surface is in probe range"), VerticalVelocity, -980.0f);
	TestEqual(TEXT("A second gravity step keeps lowering the passenger"), Height, 265.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterPassengerPortraitRenderingTest,
	"SimCopter.Passengers.PortraitRendering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterPassengerPortraitRenderingTest::RunTest(const FString& Parameters)
{
	const FString OriginalGameRoot = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame")));
	const FString TexturePath = FPaths::Combine(OriginalGameRoot, TEXT("BMP/PEOPLE1.BMP"));
	if (!FPaths::FileExists(TexturePath))
	{
		AddInfo(TEXT("Original PEOPLE1.BMP not present; skipping passenger portrait rendering validation."));
		return true;
	}

	USimCopterHangarArt* Art = NewObject<USimCopterHangarArt>();
	Art->SetOriginalGameRoot(OriginalGameRoot);
	const FSlateBrush* Brush = Art->GetSubImage(
		TEXT("PEOPLE1.BMP"),
		FIntRect(27, 0, 54, 33),
		/*bColorKeyed=*/true,
		ESimCopterArtRotation::None,
		/*bNearestNeighbor=*/true);
	if (!TestNotNull(TEXT("Passenger portrait brush"), Brush))
	{
		return false;
	}

	TestEqual(TEXT("Passenger portrait width"), Brush->ImageSize.X, 27.0f);
	TestEqual(TEXT("Passenger portrait height"), Brush->ImageSize.Y, 33.0f);
	const UTexture2D* Texture = Cast<UTexture2D>(Brush->GetResourceObject());
	if (TestNotNull(TEXT("Passenger portrait texture"), Texture))
	{
		TestEqual(TEXT("Passenger portrait uses nearest-neighbor sampling"), Texture->Filter, TF_Nearest);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterPassengerHeadImageTest,
	"SimCopter.Passengers.HeadImages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterPassengerHeadImageTest::RunTest(const FString& Parameters)
{
	// FUN_004c71c0's switch, class by class.
	const int32 ExpectedHeads[] = {4, 8, 6, 7, 5, 7, 7, 7, 5, 5, 6, 9, 3, 2, 1, 5, 4, 4, 7, 0, 7, 5};
	for (int32 BehaviorClass = 0; BehaviorClass < UE_ARRAY_COUNT(ExpectedHeads); ++BehaviorClass)
	{
		TestEqual(
			*FString::Printf(TEXT("Behavior class %d head image"), BehaviorClass),
			FSimCopterPeopleCityRules::GetHeadImageIndexForBehaviorClass(BehaviorClass),
			ExpectedHeads[BehaviorClass]);
	}

	// The whole point: head 10 belongs to state 6 and nothing else can reach it, so a bandaged
	// head can only ever appear on a casualty. Rolling one at random is the bug this pins shut.
	for (int32 BehaviorClass = 0; BehaviorClass <= 21; ++BehaviorClass)
	{
		TestNotEqual(
			*FString::Printf(TEXT("Behavior class %d does not claim the casualty head"), BehaviorClass),
			FSimCopterPeopleCityRules::GetHeadImageIndexForBehaviorClass(BehaviorClass),
			FSimCopterPeopleCityRules::MedevacVictimHeadImageIndex);
	}

	// FUN_004c7090's tail.
	{
		FSimCopterPersonContext Context;
		Context.Attributes[EBhavAttr::HeadImageIndex] = 4;
		Context.ResetToState(0);
		TestEqual(TEXT("A non-victim state leaves the head alone"), int32(Context.Attributes[EBhavAttr::HeadImageIndex]), 4);
		Context.ResetToState(6);
		TestEqual(TEXT("State 6 takes the bandaged head"), int32(Context.Attributes[EBhavAttr::HeadImageIndex]), 10);
	}

	// DAT_0058f0e0, the SIM3D.BMP image behind each head index. Eleven of them; 0x43 is the one
	// that only ever reaches a medevac victim.
	const TArray<int32>& HeadTable = FSimCopterPopulationFigure::GetHeadImageTable();
	TestEqual(TEXT("Head table has eleven entries"), HeadTable.Num(), 11);
	if (HeadTable.Num() == 11)
	{
		TestEqual(
			TEXT("Casualty head is SIM3D image 0x43"),
			HeadTable[FSimCopterPeopleCityRules::MedevacVictimHeadImageIndex],
			0x43);
	}

	// FUN_004c71c0's voice pitch column, spot-checked at both extremes and the plain man.
	TestEqual(TEXT("Blonde voice pitch"), FSimCopterPeopleCityRules::GetVoicePitchDeltaForBehaviorClass(0), 500);
	TestEqual(TEXT("5man voice pitch"), FSimCopterPeopleCityRules::GetVoicePitchDeltaForBehaviorClass(4), 0);
	TestEqual(TEXT("Elvis voice pitch"), FSimCopterPeopleCityRules::GetVoicePitchDeltaForBehaviorClass(20), -8000);
	{
		// FUN_004c71c0's footwear rows. The final 1-in-65000 Elvis override is deterministic for
		// these seeds and does not fire, so these check the actual class-to-WAV wiring.
		uint16 BlondeRandom = 0x4242;
		uint16 PlainRandom = 0x4343;
		uint16 PilotRandom = 0x4444;
		TestEqual(
			TEXT("Blonde walks in heels"),
			FSimCopterPeopleCityRules::ChooseVoiceSetForBehaviorClass(0, BlondeRandom),
			int32(SimCopterSound::VOX_FOOTSTEPS_HEELS));
		TestEqual(
			TEXT("Plain pedestrian walks in shoes"),
			FSimCopterPeopleCityRules::ChooseVoiceSetForBehaviorClass(4, PlainRandom),
			int32(SimCopterSound::VOX_FOOTSTEPS_SHOES));
		TestEqual(
			TEXT("Player pilot walks in boots"),
			FSimCopterPeopleCityRules::ChooseVoiceSetForBehaviorClass(19, PilotRandom),
			int32(SimCopterSound::VOX_FOOTSTEPS_BOOTS));
	}

	// The voice set: Elvis and Nessie always speak in the 0x2f..0x36 noises, an ordinary person
	// gets one of the three footstep clips.
	{
		uint16 Random = 0x2a2a;
		for (int32 Attempt = 0; Attempt < 8; ++Attempt)
		{
			const int32 ElvisVoice = FSimCopterPeopleCityRules::ChooseVoiceSetForBehaviorClass(20, Random);
			TestTrue(
				TEXT("Elvis always takes one of the eight Elvis noises"),
				ElvisVoice >= SimCopterSound::VOX_ELVIS_FIRST && ElvisVoice <= SimCopterSound::VOX_ELVIS_LAST);
		}
		int32 Footsteps = 0;
		for (int32 Attempt = 0; Attempt < 64; ++Attempt)
		{
			const int32 Voice = FSimCopterPeopleCityRules::ChooseVoiceSetForBehaviorClass(4, Random);
			if (Voice == SimCopterSound::VOX_FOOTSTEPS_SHOES)
			{
				++Footsteps;
			}
		}
		TestTrue(TEXT("An ordinary man almost always keeps his footsteps"), Footsteps >= 60);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterPassengerVoiceRateTest,
	"SimCopter.Passengers.VoiceRates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterPassengerVoiceRateTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Impact trauma at tier 0"), ASimCopterGroundAgent::ComputeMedevacHealthAfterCabinImpact(100, 0), 99);
	TestEqual(TEXT("Impact trauma at tier 3"), ASimCopterGroundAgent::ComputeMedevacHealthAfterCabinImpact(100, 3), 96);
	TestEqual(TEXT("Impact trauma cannot underflow"), ASimCopterGroundAgent::ComputeMedevacHealthAfterCabinImpact(2, 3), 0);

	// (health * 4 + 0x78) * 0x19, the absolute rate FUN_004c5210 pushes into the buffer while the
	// EKG loops. This is what makes the beep slow down as a patient dies.
	TestEqual(TEXT("EKG at full health"), SimCopterSound::GetEkgFrequencyHz(100), 13000);
	TestEqual(TEXT("EKG at half health"), SimCopterSound::GetEkgFrequencyHz(50), 8000);
	TestEqual(TEXT("EKG at zero"), SimCopterSound::GetEkgFrequencyHz(0), 3000);
	TestEqual(TEXT("EKG clamps health above 100"), SimCopterSound::GetEkgFrequencyHz(500), 13000);
	TestEqual(TEXT("EKG clamps health below 0"), SimCopterSound::GetEkgFrequencyHz(-40), 3000);

	// The opening AddFrequency offset is zero at full health, so the first beat plays at the
	// clip's own rate and only drifts as they fade.
	TestEqual(TEXT("EKG starts at the clip's rate"), SimCopterSound::GetEkgStartPitchDeltaHz(100), 0);
	TestEqual(TEXT("EKG offset at zero health"), SimCopterSound::GetEkgStartPitchDeltaHz(0), -10000);

	TestEqual(TEXT("Footsteps standing still"), SimCopterSound::GetWalkPacedFrequencyHz(0), 84 * 125);
	TestEqual(TEXT("Footstep offset at speed 1"), SimCopterSound::GetWalkPacedPitchDeltaHz(1), 0);

	TestTrue(TEXT("The EKG loops"), SimCopterSound::IsLoopingVoiceEvent(SimCopterSound::VOX_EKG));
	TestFalse(TEXT("A dying cry does not"), SimCopterSound::IsLoopingVoiceEvent(SimCopterSound::VOX_DYING));
	TestFalse(TEXT("The EKG is not walk-paced"), SimCopterSound::IsWalkPacedVoiceEvent(SimCopterSound::VOX_EKG));
	TestTrue(TEXT("Footsteps are"), SimCopterSound::IsWalkPacedVoiceEvent(SimCopterSound::VOX_FOOTSTEPS_BOOTS));
	TestEqual(TEXT("Player footsteps stop after six city tiles"), SimCopterSound::PlayerFootstepMaxRangeCm, 2400.0f);
	TestEqual(TEXT("NPC footsteps use the pedestrian range"), SimCopterSound::PedestrianFootstepMaxRangeCm, 800.0f);
	TestEqual(TEXT("NPC footsteps use full volume"), SimCopterSound::PedestrianFootstepVolumeMultiplier, 1.0f);

	const SimCopterSound::FVoiceEvent* Shoes = SimCopterSound::GetVoiceEvent(SimCopterSound::VOX_FOOTSTEPS_SHOES);
	const SimCopterSound::FVoiceEvent* Heels = SimCopterSound::GetVoiceEvent(SimCopterSound::VOX_FOOTSTEPS_HEELS);
	const SimCopterSound::FVoiceEvent* Boots = SimCopterSound::GetVoiceEvent(SimCopterSound::VOX_FOOTSTEPS_BOOTS);
	if (TestNotNull(TEXT("Shoes event is registered"), Shoes))
	{
		TestEqual(TEXT("Shoes event file"), FString(Shoes->Clips[0]), FString(TEXT("xFtShoes")));
	}
	if (TestNotNull(TEXT("Heels event is registered"), Heels))
	{
		TestEqual(TEXT("Heels event file"), FString(Heels->Clips[0]), FString(TEXT("xFtHeels")));
	}
	if (TestNotNull(TEXT("Boots event is registered"), Boots))
	{
		TestEqual(TEXT("Boots event file"), FString(Boots->Clips[0]), FString(TEXT("xFtBoots")));
	}

	// EKG.wav must be a real voice event, or the medevac beep silently plays nothing.
	const SimCopterSound::FVoiceEvent* Ekg = SimCopterSound::GetVoiceEvent(SimCopterSound::VOX_EKG);
	if (TestNotNull(TEXT("Voice event 58 is registered"), Ekg))
	{
		TestEqual(TEXT("Voice event 58 is EKG"), FString(Ekg->Clips[0]), FString(TEXT("EKG")));
	}

	return true;
}

// Runs the shipped BHAV 264 - the program that actually chooses a passenger's face. Skips without
// the original data.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterPassengerFaceProgramTest,
	"SimCopter.Passengers.FaceProgram",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterPassengerFaceProgramTest::RunTest(const FString& Parameters)
{
	const FString RootPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame")));
	const FString PeoplePath = FSimCopterPeopleReader::ResolvePeoplePath(RootPath);
	if (PeoplePath.IsEmpty())
	{
		AddInfo(TEXT("Original people.df not present; skipping passenger face validation."));
		return true;
	}

	FPeopleBehaviorModel Model;
	FString Error;
	if (!TestTrue(TEXT("Parses people.df"), FSimCopterPeopleReader::LoadFromFile(PeoplePath, Model, Error)))
	{
		AddError(Error);
		return false;
	}

	// The record sites this whole feature rests on. If a later change renumbers an opcode, these
	// fail loudly rather than the seat window quietly going blank.
	if (const FBhavProgram* Faces = Model.FindProgram(264);
		TestNotNull(TEXT("BHAV 264 face program"), Faces) && Faces->Records.IsValidIndex(11))
	{
		TestEqual(TEXT("BHAV 264 name"), Faces->Name, FString(TEXT("Face vs. speed/health")));
		TestEqual(TEXT("BHAV 264 gates on riding the player"), int32(Faces->Records[0].Token), 59);
		TestEqual(TEXT("BHAV 264 tests the head image"), int32(Faces->Records[8].Args[0]), EBhavAttr::HeadImageIndex);
		TestEqual(TEXT("BHAV 264 casualty head is 10"), int32(Faces->Records[8].Args[1]), 10);
		TestEqual(TEXT("BHAV 264 tests written-off"), int32(Faces->Records[9].Args[0]), EBhavAttr::WrittenOff);
		TestEqual(TEXT("BHAV 264 reads health"), int32(Faces->Records[10].Args[0]), EBhavAttr::MedevacHealth);
		TestEqual(TEXT("BHAV 264 dead threshold"), int32(Faces->Records[10].Args[1]), 1);
		TestEqual(TEXT("BHAV 264 hurt threshold"), int32(Faces->Records[11].Args[1]), 50);
		TestEqual(TEXT("BHAV 264 reads the helicopter speed"), int32(Faces->Records[1].Token), 55);
		TestEqual(TEXT("BHAV 264 fast threshold"), int32(Faces->Records[3].Args[1]), 250);
		TestEqual(TEXT("BHAV 264 brisk threshold"), int32(Faces->Records[4].Args[1]), 125);
		TestEqual(TEXT("BHAV 264 writes the seat face"), int32(Faces->Records[2].Token), 54);
	}

	// A medevac victim, whose head is 10: the face tracks their health, not your flying.
	TestEqual(TEXT("Healthy patient shows the calm face"), RunFaceProgram(Model, 10, 100, 0), 0);
	TestEqual(TEXT("Patient at 50 is still calm"), RunFaceProgram(Model, 10, 50, 0), 0);
	TestEqual(TEXT("Failing patient shows the hurt face"), RunFaceProgram(Model, 10, 49, 0), 1);
	TestEqual(TEXT("Patient at 1 is still hurt"), RunFaceProgram(Model, 10, 1, 0), 1);
	TestEqual(TEXT("Dead patient shows the worst face"), RunFaceProgram(Model, 10, 0, 0), 2);
	// Written off - everyone aboard when the helicopter is destroyed - skips the health test.
	TestEqual(TEXT("Written-off patient shows the worst face"), RunFaceProgram(Model, 10, 100, 0, /*WrittenOff*/ 1), 2);

	// Everybody else - a transport fare, an officer - reacts to how hard you are flying instead,
	// and the shipped edges are not monotonic: records [3] and [4] send > 250 to face 2, > 125 to
	// face 0 and everything below that to face 1. So a passenger sits at the middle face while you
	// crawl, brightens once you are making progress, and takes the frightened face when you push
	// past 250 - which a damaged helicopter reaches at a much lower real speed, because opcode 55
	// multiplies by MaxDamage / remaining hit points.
	TestEqual(TEXT("Barely moving leaves the middle face"), RunFaceProgram(Model, 4, 100, 100), 1);
	TestEqual(TEXT("A decent cruise takes face 0"), RunFaceProgram(Model, 4, 100, 126), 0);
	TestEqual(TEXT("Hard flying frightens them"), RunFaceProgram(Model, 4, 100, 251), 2);
	// A non-casualty's health attribute must not leak into their face.
	TestEqual(TEXT("A fare with no health ignores it"), RunFaceProgram(Model, 4, 0, 200), 0);
	TestEqual(TEXT("A fare at full health still follows the speed"), RunFaceProgram(Model, 4, 100, 300), 2);
	TestEqual(TEXT("Impact recovery at idle uses the neutral face"),
		ASimCopterGroundAgent::ComputePassengerPortraitStateFromDamageScaledSpeed(0), 1);
	TestEqual(TEXT("Impact recovery at the lower edge stays neutral"),
		ASimCopterGroundAgent::ComputePassengerPortraitStateFromDamageScaledSpeed(125), 1);
	TestEqual(TEXT("Impact recovery at cruise uses the calm face"),
		ASimCopterGroundAgent::ComputePassengerPortraitStateFromDamageScaledSpeed(126), 0);
	TestEqual(TEXT("Impact recovery at the upper edge stays calm"),
		ASimCopterGroundAgent::ComputePassengerPortraitStateFromDamageScaledSpeed(250), 0);
	TestEqual(TEXT("Impact recovery keeps genuinely hard flying frightened"),
		ASimCopterGroundAgent::ComputePassengerPortraitStateFromDamageScaledSpeed(251), 2);

	// BHAV 800 is what makes the EKG a medevac victim's own voice, which is the only reason
	// FUN_004c5210 re-tunes it from their health instead of restarting it.
	if (const FBhavProgram* MedevacInit = Model.FindProgram(800);
		TestNotNull(TEXT("BHAV 800 medevac initbhav"), MedevacInit) && MedevacInit->Records.IsValidIndex(4))
	{
		TestEqual(TEXT("person state 6 starts BHAV 800"), FPeopleBehaviorModel::GetStateProgramIds()[6], 800);
		TestEqual(TEXT("BHAV 800 assigns a voice set"), int32(MedevacInit->Records[4].Args[0]), EBhavAttr::VoiceSet);
		TestEqual(TEXT("BHAV 800 voice set is the EKG"), int32(MedevacInit->Records[4].Args[1]), SimCopterSound::VOX_EKG);
	}

	// BHAV 302 is the sound loop: a dying cry at 3D, then the EKG 2D so it carries to the cockpit,
	// and opcode 85 - stop talking - the moment the patient is not riding you.
	if (const FBhavProgram* Sounds = Model.FindProgram(302);
		TestNotNull(TEXT("BHAV 302 medevac play sounds"), Sounds) && Sounds->Records.IsValidIndex(9))
	{
		TestEqual(TEXT("BHAV 302 cries out"), int32(Sounds->Records[2].Token), 57);
		TestEqual(TEXT("BHAV 302 cry is achdie"), int32(Sounds->Records[2].Args[0]), SimCopterSound::VOX_DYING);
		TestEqual(TEXT("BHAV 302 cry is positional"), int32(Sounds->Records[2].Args[2]), 0);
		TestEqual(TEXT("BHAV 302 gates the EKG on riding the player"), int32(Sounds->Records[8].Token), 59);
		TestEqual(TEXT("BHAV 302 plays the EKG"), int32(Sounds->Records[7].Token), 57);
		TestEqual(TEXT("BHAV 302 EKG event"), int32(Sounds->Records[7].Args[0]), SimCopterSound::VOX_EKG);
		TestEqual(TEXT("BHAV 302 EKG is non-positional"), int32(Sounds->Records[7].Args[2]), 1);
		TestEqual(TEXT("BHAV 302 falls silent off the helicopter"), int32(Sounds->Records[9].Token), 85);
		TestEqual(TEXT("BHAV 302 silence is the not-riding arm"), int32(Sounds->Records[8].FalseNext), 9);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
