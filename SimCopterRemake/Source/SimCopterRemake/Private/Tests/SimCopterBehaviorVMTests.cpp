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
	ESimCopterBehaviorStepResult StepResult = ESimCopterBehaviorStepResult::NoTarget;
	int32 StepTowardCalls = 0;
	bool bBoardSelectionResult = false;
	int32 BoardSelectionCalls = 0;
	bool bCanAlight = false;
	bool bTryAlight = false;
	int32 TryAlightCalls = 0;
	bool bHasHiddenState5 = false;
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
	virtual ESimCopterBehaviorStepResult StepTowardSelectedObject(FSimCopterPersonContext&) override
	{
		++StepTowardCalls;
		return StepResult;
	}
	virtual bool BoardSelection(FSimCopterPersonContext&) override
	{
		++BoardSelectionCalls;
		return bBoardSelectionResult;
	}
	virtual bool CanAlightHere() const override { return bCanAlight; }
	virtual bool TryAlightHere() override
	{
		++TryAlightCalls;
		return bTryAlight;
	}
	virtual bool HasHiddenPersonInState(int32 State) const override
	{
		return State == 5 && bHasHiddenState5;
	}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterBehaviorVMActionDelegationTest,
	"SimCopter.Behavior.VM.ActionDelegation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterBehaviorVMActionDelegationTest::RunTest(const FString& Parameters)
{
	auto AddBranchingProgram = [](FPeopleBehaviorModel& Model, int32 ProgramId, uint16 Opcode)
	{
		FBhavProgram Program;
		Program.Id = ProgramId;
		Program.Name = FString::Printf(TEXT("Opcode %d delegation"), int32(Opcode));
		FBhavRecord Action;
		Action.Token = Opcode;
		Action.TrueNext = 1;
		Action.FalseNext = 2;
		Action.Args[0] = 0;
		FBhavRecord TrueWait;
		TrueWait.Token = 0;
		TrueWait.Args[0] = 2;
		FBhavRecord FalseWait = TrueWait;
		Program.Records = {Action, TrueWait, FalseWait};
		Model.ProgramsById.Add(Program.Id, MoveTemp(Program));
	};

	FPeopleBehaviorModel Model;
	AddBranchingProgram(Model, 1217, 17);
	AddBranchingProgram(Model, 1221, 21);
	AddBranchingProgram(Model, 1212, 12);
	AddBranchingProgram(Model, 1273, 73);

	{
		FStubBehaviorWorld World;
		World.bTryAlight = true;
		FSimCopterPersonContext Context;
		Context.Stack.Add({1217, 0, {0, 0, 1}});
		TestEqual(
			TEXT("Opcode 17 follows the action service true edge"),
			int32(FSimCopterBehaviorVM::Tick(Context, Model, World)),
			int32(EBhavStepResult::Ran));
		TestEqual(TEXT("Opcode 17 calls TryAlightHere once"), World.TryAlightCalls, 1);
		TestEqual(TEXT("Opcode 17 true branch"), Context.Stack.Last().RecordIndex, 1);
	}

	{
		FStubBehaviorWorld World;
		World.bCanAlight = false;
		FSimCopterPersonContext Context;
		Context.Stack.Add({1221, 0, {0, 0, 1}});
		FSimCopterBehaviorVM::Tick(Context, Model, World);
		TestEqual(TEXT("Opcode 21 false branch comes from CanAlightHere"), Context.Stack.Last().RecordIndex, 2);
	}

	{
		FStubBehaviorWorld World;
		World.StepResult = ESimCopterBehaviorStepResult::Arrived;
		World.bBoardSelectionResult = true;
		FSimCopterPersonContext Context;
		Context.Stack.Add({1212, 0, {1, 0, 1}});
		Context.bHasSelection = true;
		FSimCopterBehaviorVM::Tick(Context, Model, World);
		TestEqual(TEXT("Opcode 12 asks the world to approach once"), World.StepTowardCalls, 1);
		TestEqual(TEXT("Opcode 12 delegates boarding once"), World.BoardSelectionCalls, 1);
		TestEqual(TEXT("Opcode 12 true branch"), Context.Stack.Last().RecordIndex, 1);
	}

	{
		FStubBehaviorWorld World;
		World.bHasHiddenState5 = true;
		FSimCopterPersonContext Context;
		Context.Stack.Add({1273, 0, {0, 0, 1}});
		FSimCopterBehaviorVM::Tick(Context, Model, World);
		TestEqual(TEXT("Opcode 73 queries live hidden state-5 people"), Context.Stack.Last().RecordIndex, 1);
	}

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

		// Passenger action ordering is a contract with the engine-owned carrier service. Pin the
		// shipped graphs that establish it so a later opcode reinterpretation cannot silently make
		// counters authoritative again.
		if (const FBhavProgram* TransportBoard = Model.FindProgram(291);
			TestNotNull(TEXT("BHAV 291 transport board program"), TransportBoard) &&
			TransportBoard->Records.IsValidIndex(5))
		{
			TestEqual(TEXT("BHAV 291 probes player helicopter"), int32(TransportBoard->Records[2].Token), 15);
			TestEqual(TEXT("BHAV 291 player helicopter class"), int32(TransportBoard->Records[2].Args[0]), 2);
			TestEqual(TEXT("BHAV 291 probe reaches board action"), int32(TransportBoard->Records[2].TrueNext), 5);
			TestEqual(TEXT("BHAV 291 performs walk-and-board"), int32(TransportBoard->Records[5].Token), 12);
		}

		if (const FBhavProgram* TransportAlight = Model.FindProgram(292);
			TestNotNull(TEXT("BHAV 292 transport alight program"), TransportAlight) &&
			TransportAlight->Records.IsValidIndex(6))
		{
			TestEqual(TEXT("BHAV 292 probes mission destination"), int32(TransportAlight->Records[3].Token), 15);
			TestEqual(TEXT("BHAV 292 destination object class"), int32(TransportAlight->Records[3].Args[0]), 0);
			TestEqual(TEXT("BHAV 292 performs alight"), int32(TransportAlight->Records[5].Token), 17);
			TestEqual(TEXT("BHAV 292 alight reaches outcome"), int32(TransportAlight->Records[5].TrueNext), 4);
			TestEqual(TEXT("BHAV 292 posts delivered outcome"), int32(TransportAlight->Records[4].Token), 13);
			TestEqual(TEXT("BHAV 292 delivered outcome code"), int32(TransportAlight->Records[4].Args[0]), 1);
			TestEqual(TEXT("BHAV 292 disappears only after outcome"), int32(TransportAlight->Records[6].Token), 16);
		}

		if (const FBhavProgram* PullPatients = Model.FindProgram(263);
			TestNotNull(TEXT("BHAV 263 hospital unload program"), PullPatients) &&
			PullPatients->Records.IsValidIndex(19))
		{
			TestEqual(TEXT("BHAV 263 first selects aboard patient"), int32(PullPatients->Records[0].Token), 84);
			TestEqual(TEXT("BHAV 263 no-patient edge"), int32(PullPatients->Records[0].FalseNext), 19);
			TestEqual(TEXT("BHAV 263 no-patient edge calls helper ride"), int32(PullPatients->Records[19].Token), 269);
		}

		if (const FBhavProgram* MedicRide = Model.FindProgram(269);
			TestNotNull(TEXT("BHAV 269 medic ride program"), MedicRide) &&
			MedicRide->Records.IsValidIndex(9))
		{
			TestEqual(TEXT("BHAV 269 selects starting vehicle"), int32(MedicRide->Records[1].Token), 62);
			TestEqual(TEXT("BHAV 269 missing vehicle reaches disappear"), int32(MedicRide->Records[1].FalseNext), 9);
			TestEqual(TEXT("BHAV 269 missing vehicle disappears"), int32(MedicRide->Records[9].Token), 40);
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
