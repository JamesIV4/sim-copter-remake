// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Formats/SimCopterPeopleReader.h"
#include "Ground/SimCopterBehaviorVM.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

namespace
{
class FStubBehaviorWorld : public ISimCopterBehaviorWorld
{
public:
	int32 TileClass = 7;
	bool bThreat = false;
	int32 MoveSteps = 0;
	TSet<int32> UnknownOpcodes;

	virtual int32 GetCurrentTileClass() const override { return TileClass; }
	virtual bool IsTileClassAllowedForState(int32 StateIndex, int32 InTileClass) const override
	{
		return FSimCopterBehaviorVM::GetAllowedTileClasses(StateIndex).Contains(InTileClass);
	}
	virtual bool MoveStep(FSimCopterPersonContext&) override { ++MoveSteps; return true; }
	virtual bool IsThreatNearby(const FSimCopterPersonContext&) const override { return bThreat; }
	virtual void OnUnknownOpcode(int32 Opcode) override { UnknownOpcodes.Add(Opcode); }
};
} // namespace

// Runs the shipped people.df programs through the interpreter. Skips without the original data.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterBehaviorVMReferenceTest,
	"SimCopter.Behavior.VM.Reference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterBehaviorVMReferenceTest::RunTest(const FString& Parameters)
{
	const FString RootPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reference/SimCopterOriginalGame")));
	const FString PeoplePath = FSimCopterPeopleReader::ResolvePeoplePath(RootPath);
	if (PeoplePath.IsEmpty())
	{
		AddInfo(TEXT("Original people.df not present; skipping behavior VM validation."));
		return true;
	}

	FPeopleBehaviorModel Model;
	FString Error;
	if (!TestTrue(TEXT("Parses people.df"), FSimCopterPeopleReader::LoadFromFile(PeoplePath, Model, Error)))
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("Program count"), Model.ProgramsById.Num(), 137);

	// Every state-table program id must exist in the shipped file.
	for (const int32 ProgramId : FPeopleBehaviorModel::GetStateProgramIds())
	{
		TestNotNull(*FString::Printf(TEXT("State program %d exists"), ProgramId), Model.FindProgram(ProgramId));
	}

	// 'Idle-40' (id 1103) is [local0 := 40][wait local0]: must yield exactly 40 ticks per pass.
	const FBhavProgram* Idle40 = Model.FindProgram(1103);
	if (TestNotNull(TEXT("Idle-40 program"), Idle40))
	{
		TestEqual(TEXT("Idle-40 name"), Idle40->Name, FString(TEXT("Idle-40")));
		TestEqual(TEXT("Idle-40 record count"), Idle40->Records.Num(), 2);
		TestEqual(TEXT("Idle-40 rec0 opcode"), int32(Idle40->Records[0].Token), 2);
		TestEqual(TEXT("Idle-40 rec0 operator := "), int32(Idle40->Records[0].Args[2]), 5);
		TestEqual(TEXT("Idle-40 rec0 value"), int32(Idle40->Records[0].Args[1]), 40);
		TestEqual(TEXT("Idle-40 rec1 opcode (wait)"), int32(Idle40->Records[1].Token), 0);

		FSimCopterPersonContext Context;
		FStubBehaviorWorld World;
		Context.Stack.Add({1103, 0, {}});
		Context.Attributes[EBhavAttr::State] = 0;
		int32 Yields = 0;
		for (int32 Tick = 0; Tick < 100; ++Tick)
		{
			const EBhavStepResult Result = FSimCopterBehaviorVM::Tick(Context, Model, World);
			if (Result == EBhavStepResult::Completed)
			{
				break;
			}
			TestEqual(TEXT("Idle-40 yields while waiting"), int32(Result), int32(EBhavStepResult::Ran));
			++Yields;
		}
		TestEqual(TEXT("Idle-40 waits 40 ticks"), Yields, 40);
	}

	// The ambient state-0 program (id 600) must run without runaway loops for many ticks.
	{
		FSimCopterPersonContext Context;
		FStubBehaviorWorld World;
		Context.ResetToState(0);
		for (int32 Tick = 0; Tick < 500; ++Tick)
		{
			const EBhavStepResult Result = FSimCopterBehaviorVM::Tick(Context, Model, World);
			if (Result == EBhavStepResult::Failed)
			{
				AddError(FString::Printf(TEXT("Ambient program failed at tick %d."), Tick));
				return false;
			}
			if (Result == EBhavStepResult::Stopped)
			{
				break; // despawn path is a legitimate outcome
			}
		}
		AddInfo(FString::Printf(TEXT("Ambient run: %d move steps; unported opcodes hit: %d."),
			World.MoveSteps, World.UnknownOpcodes.Num()));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
