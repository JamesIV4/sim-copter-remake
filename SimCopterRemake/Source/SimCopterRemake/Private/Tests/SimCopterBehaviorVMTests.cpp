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
	int32 FileX = 0;
	int32 FileY = 0;
	bool bHasCurrentTileCoordinate = true;
	bool bThreat = false;
	bool bHasPlayerProbe = false;
	FSimCopterBehaviorPlayerTileProbe PlayerProbe;
	int32 MoveSteps = 0;
	TSet<int32> UnknownOpcodes;

	virtual int32 GetCurrentTileClass() const override { return TileClass; }
	virtual bool TryGetCurrentTileCoordinate(int32& OutFileX, int32& OutFileY) const override
	{
		if (!bHasCurrentTileCoordinate)
		{
			return false;
		}
		OutFileX = FileX;
		OutFileY = FileY;
		return true;
	}
	virtual bool IsTileClassAllowedForState(int32 StateIndex, int32 InTileClass) const override
	{
		return FSimCopterBehaviorVM::GetAllowedTileClasses(StateIndex).Contains(InTileClass);
	}
	virtual bool MoveStep(FSimCopterPersonContext&) override { ++MoveSteps; return true; }
	virtual bool IsThreatNearby(const FSimCopterPersonContext&) const override { return bThreat; }
	virtual bool TryGetPlayerTileProbe(
		const FSimCopterPersonContext&,
		FSimCopterBehaviorPlayerTileProbe& OutProbe) const override
	{
		if (!bHasPlayerProbe)
		{
			return false;
		}
		OutProbe = PlayerProbe;
		return true;
	}
	virtual void OnUnknownOpcode(int32 Opcode) override { UnknownOpcodes.Add(Opcode); }
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterBehaviorVMOpcode22Test,
	"SimCopter.Behavior.VM.Opcode22PlayerTileProbe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterBehaviorVMOpcode22Test::RunTest(const FString& Parameters)
{
	FPeopleBehaviorModel Model;
	FBhavProgram Program;
	Program.Id = 2200;
	Program.Name = TEXT("Opcode22 probe");
	FBhavRecord Probe;
	Probe.Token = 22;
	Probe.TrueNext = 1;
	Probe.FalseNext = 2;
	Probe.Args[0] = 0;
	Probe.Args[1] = 1;
	FBhavRecord TrueWait;
	TrueWait.Token = 0;
	TrueWait.Args[0] = 2;
	FBhavRecord FalseWait = TrueWait;
	Program.Records = {Probe, TrueWait, FalseWait};
	Model.ProgramsById.Add(Program.Id, MoveTemp(Program));

	FSimCopterPersonContext Context;
	Context.Stack.Add({2200, 0, {0, 0, 1}});
	FStubBehaviorWorld World;
	World.FileX = 10;
	World.FileY = 12;
	World.bHasPlayerProbe = true;
	World.PlayerProbe.FileX = 10;
	World.PlayerProbe.FileY = 12;
	World.PlayerProbe.Speed = 6;
	World.PlayerProbe.Facing = 3;
	TestEqual(TEXT("Opcode 22 true branch yields on true record"), int32(FSimCopterBehaviorVM::Tick(Context, Model, World)), int32(EBhavStepResult::Ran));
	TestEqual(TEXT("Opcode 22 speed local"), int32(Context.Stack.Last().Locals[0]), 6);
	TestEqual(TEXT("Opcode 22 facing local"), int32(Context.Stack.Last().Locals[1]), 3);
	TestEqual(TEXT("Opcode 22 true branch record"), Context.Stack.Last().RecordIndex, 1);

	FSimCopterPersonContext MissContext;
	MissContext.Stack.Add({2200, 0, {0, 0, 1}});
	World.PlayerProbe.FileY = 13;
	TestEqual(TEXT("Opcode 22 false branch yields on false record"), int32(FSimCopterBehaviorVM::Tick(MissContext, Model, World)), int32(EBhavStepResult::Ran));
	TestEqual(TEXT("Opcode 22 miss leaves speed local"), int32(MissContext.Stack.Last().Locals[0]), 0);
	TestEqual(TEXT("Opcode 22 miss leaves facing local"), int32(MissContext.Stack.Last().Locals[1]), 0);
	TestEqual(TEXT("Opcode 22 false branch record"), MissContext.Stack.Last().RecordIndex, 2);

	return true;
}

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
		TestTrue(TEXT("Ambient program produces original VM move steps"), World.MoveSteps > 0);
		const int32 PortedAmbientOpcodes[] = {4, 7, 15, 18, 22, 23, 24, 27, 28, 29, 31, 34, 36, 57, 59, 70, 85, 86};
		for (const int32 Opcode : PortedAmbientOpcodes)
		{
			TestFalse(
				*FString::Printf(TEXT("Ambient opcode %d is handled by a named path"), Opcode),
				World.UnknownOpcodes.Contains(Opcode));
		}
		TestEqual(TEXT("Ambient run has no unknown opcodes"), World.UnknownOpcodes.Num(), 0);
		TArray<int32> UnknownList = World.UnknownOpcodes.Array();
		UnknownList.Sort();
		FString UnknownText;
		for (const int32 Opcode : UnknownList)
		{
			if (!UnknownText.IsEmpty())
			{
				UnknownText += TEXT(",");
			}
			UnknownText += FString::FromInt(Opcode);
		}
		AddInfo(FString::Printf(TEXT("Ambient run: %d move steps; unported opcodes hit: %d."),
			World.MoveSteps, World.UnknownOpcodes.Num()));
		if (!UnknownText.IsEmpty())
		{
			AddInfo(FString::Printf(TEXT("Ambient unported opcode list: %s."), *UnknownText));
		}
	}

	const int32 AmbientTileClasses[] = {12, 13, 11, 10, 5, 4, 3, 7};
	for (int32 BehaviorClass = 0; BehaviorClass <= 20; ++BehaviorClass)
	{
		for (const int32 TileClass : AmbientTileClasses)
		{
			FSimCopterPersonContext Context;
			FStubBehaviorWorld World;
			World.TileClass = TileClass;
			Context.ResetToState(0);
			Context.Attributes[EBhavAttr::BehaviorClass] = uint16(BehaviorClass);
			for (int32 Tick = 0; Tick < 300; ++Tick)
			{
				const EBhavStepResult Result = FSimCopterBehaviorVM::Tick(Context, Model, World);
				if (Result == EBhavStepResult::Failed)
				{
					AddError(FString::Printf(
						TEXT("Ambient variant failed: behavior class %d, tile class %d, tick %d."),
						BehaviorClass,
						TileClass,
						Tick));
					return false;
				}
				if (Result == EBhavStepResult::Stopped)
				{
					break;
				}
			}
			TestEqual(
				*FString::Printf(TEXT("Ambient variant unknown opcodes class %d tile %d"), BehaviorClass, TileClass),
				World.UnknownOpcodes.Num(),
				0);
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
