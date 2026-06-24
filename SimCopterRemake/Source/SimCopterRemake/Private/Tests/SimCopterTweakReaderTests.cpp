// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Formats/SimCopterTweakReader.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterTweakParserTest,
	"SimCopter.Formats.Tweak.Parser",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterTweakParserTest::RunTest(const FString& Parameters)
{
	const FString TweakText =
		TEXT("%%%% comment\n")
		TEXT("[Camera Parms]\n")
		TEXT("NumCtrl=3\n")
		TEXT("Ctrl0_Label=Chase Maxdist\n")
		TEXT("Ctrl0_Value=233.1\n")
		TEXT("\n")
		TEXT("[Joystick]\n")
		TEXT("Threshold=20\n");

	FSimCopterTweakFile TweakFile;
	FString Error;
	if (!TestTrue(TEXT("Parses simple tweak text"), FSimCopterTweakReader::ParseTweakText(TweakText, TweakFile, Error)))
	{
		AddError(Error);
		return false;
	}

	const FSimCopterTweakSection* CameraSection = TweakFile.FindSection(TEXT("camera parms"));
	TestNotNull(TEXT("Finds section case-insensitively"), CameraSection);
	if (CameraSection != nullptr)
	{
		TestEqual(TEXT("Reads string value"), CameraSection->GetString(TEXT("Ctrl0_Label")), FString(TEXT("Chase Maxdist")));
		TestTrue(TEXT("Reads float value"), FMath::IsNearlyEqual(CameraSection->GetFloat(TEXT("Ctrl0_Value")), 233.1f, 0.001f));
		TestEqual(TEXT("Reads int value"), CameraSection->GetInt(TEXT("NumCtrl")), 3);
	}

	const FSimCopterTweakSection* JoystickSection = TweakFile.FindSection(TEXT("Joystick"));
	TestNotNull(TEXT("Finds second section"), JoystickSection);
	if (JoystickSection != nullptr)
	{
		TestEqual(TEXT("Reads joystick threshold"), JoystickSection->GetInt(TEXT("Threshold")), 20);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterTweakReferenceHeliTest,
	"SimCopter.Formats.Tweak.ReferenceHeli",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterTweakReferenceHeliTest::RunTest(const FString& Parameters)
{
	const FString HeliTweakPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame/tweak/heli.twk")));
	if (!FPaths::FileExists(HeliTweakPath))
	{
		AddWarning(FString::Printf(TEXT("Skipping optional heli tweak test because '%s' is not present."), *HeliTweakPath));
		return true;
	}

	FSimCopterTweakFile TweakFile;
	FString Error;
	if (!TestTrue(TEXT("Loads heli.twk"), FSimCopterTweakReader::LoadTweakFileFromFile(HeliTweakPath, TweakFile, Error)))
	{
		AddError(Error);
		return false;
	}

	const FSimCopterTweakSection* JetRanger = TweakFile.FindSection(TEXT("Jet Ranger"));
	TestNotNull(TEXT("Jet Ranger section exists"), JetRanger);
	if (JetRanger != nullptr)
	{
		TestEqual(TEXT("Jet Ranger first label"), JetRanger->GetString(TEXT("Ctrl0_Label")), FString(TEXT("MaxBank (10 = 1 deg)")));
		TestTrue(TEXT("Jet Ranger max bank raw value"), FMath::IsNearlyEqual(JetRanger->GetFloat(TEXT("Ctrl0_Value")), 426.7f, 0.001f));
		TestTrue(TEXT("Jet Ranger fuel raw value"), FMath::IsNearlyEqual(JetRanger->GetFloat(TEXT("Ctrl13_Value")), 91.2f, 0.001f));
	}

	const FSimCopterTweakSection* Landing = TweakFile.FindSection(TEXT("Heli Landing"));
	TestNotNull(TEXT("Heli Landing section exists"), Landing);
	if (Landing != nullptr)
	{
		TestEqual(TEXT("Landing speed label"), Landing->GetString(TEXT("Ctrl2_Label")), FString(TEXT("Speed")));
		TestTrue(TEXT("Landing max descent raw value"), FMath::IsNearlyEqual(Landing->GetFloat(TEXT("Ctrl4_Value")), 19.4f, 0.001f));
	}

	const FSimCopterTweakSection* Rope = TweakFile.FindSection(TEXT("Heli Ropestuff"));
	TestNotNull(TEXT("Heli Ropestuff section exists"), Rope);
	if (Rope != nullptr)
	{
		TestTrue(TEXT("Rope tension raw value"), FMath::IsNearlyEqual(Rope->GetFloat(TEXT("Ctrl3_Value")), 0.5f, 0.001f));
	}

	return true;
}

#endif
