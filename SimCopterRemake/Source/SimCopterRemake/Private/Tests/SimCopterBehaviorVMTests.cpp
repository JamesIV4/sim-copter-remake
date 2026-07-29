// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Formats/SimCopterPeopleReader.h"
#include "Ground/SimCopterBehaviorVM.h"
#include "Ground/SimCopterInteraction.h"
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
	int32 BeginFallAndDieCalls = 0;
	bool bHasHiddenState5 = false;
	int32 SelectionRoom = INDEX_NONE;
	bool bBeamStillFlying = false;
	int32 BeamSteps = 0;
	int32 ReactToSourceCalls = 0;
	bool bFaceAwayFromSelection = true;
	bool bHasRiotCrowd = false;
	int32 RiotFacingOctant = 0;
	int32 RiotAverageAgitation = 0;
	int32 RiotCount = 0;
	float BodyRadiusUnits = 3.0f;
	bool bJoinRiotResult = false;
	int32 JoinRiotCalls = 0;
	bool bHasFireNearby = false;
	int32 FireTileDistance = 0;
	int32 ActiveMedevacs = 0;
	int32 CollapseCalls = 0;
	bool bCollapseResult = true;
	int32 TickCounter = 0;
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
	virtual bool BeginFallAndDie(FSimCopterPersonContext&) override
	{
		++BeginFallAndDieCalls;
		return true;
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
	virtual int32 GetSelectionRoomForBoarding(const FSimCopterPersonContext&) const override
	{
		return SelectionRoom;
	}
	virtual bool AdvanceBeamAbduction(FSimCopterPersonContext&) override
	{
		++BeamSteps;
		return bBeamStillFlying;
	}
	virtual void ReactToInteractionSource(FSimCopterPersonContext&) override { ++ReactToSourceCalls; }
	virtual bool FaceAwayFromSelectedObject(FSimCopterPersonContext&) override { return bFaceAwayFromSelection; }
	virtual bool MeasureRiotCrowd(
		const FSimCopterPersonContext&,
		int32,
		int32& OutFacingOctant,
		int32& OutAverageAgitation,
		int32& OutCount) const override
	{
		OutFacingOctant = RiotFacingOctant;
		OutAverageAgitation = RiotAverageAgitation;
		OutCount = RiotCount;
		return bHasRiotCrowd;
	}
	virtual void SetBodyRadiusOriginalUnits(float RadiusUnits) override { BodyRadiusUnits = RadiusUnits; }
	virtual bool JoinLiveRiot(FSimCopterPersonContext&) override
	{
		++JoinRiotCalls;
		return bJoinRiotResult;
	}
	virtual bool FaceNearestFireWithin(FSimCopterPersonContext&, int32, int32& OutTileDistance) override
	{
		OutTileDistance = FireTileDistance;
		return bHasFireNearby;
	}
	virtual int32 GetActiveMedevacMissionCount() const override { return ActiveMedevacs; }
	virtual bool CollapseIntoMedevacVictim(FSimCopterPersonContext&) override
	{
		++CollapseCalls;
		return bCollapseResult;
	}
	virtual int32 GetBehaviorTickCounter() const override { return TickCounter; }
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
	AddBranchingProgram(Model, 1266, 66);

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

	{
		FStubBehaviorWorld World;
		FSimCopterPersonContext Context;
		Context.Stack.Add({1266, 0, {0, 0, 1}});
		TestEqual(
			TEXT("Opcode 66 stops after delegating physical death handling"),
			int32(FSimCopterBehaviorVM::Tick(Context, Model, World)),
			int32(EBhavStepResult::Stopped));
		TestEqual(TEXT("Opcode 66 delegates death exactly once"), World.BeginFallAndDieCalls, 1);
		TestTrue(TEXT("Opcode 66 still requests population cleanup"), Context.bRequestDespawn);
	}

	return true;
}

// The opcodes closed out in the 2026-07-29 decode pass: the room check every officer boards
// through, the UFO abduction flight, the bump conversation, and the ambient stubs behind
// BHAV 600's riot/fire hooks.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterBehaviorVMLateOpcodeTest,
	"SimCopter.Behavior.VM.LateOpcodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCopterBehaviorVMLateOpcodeTest::RunTest(const FString& Parameters)
{
	// One opcode with both edges landing on a wait record, exactly as the delegation test above
	// builds them: the wait parks the walker on record 1 or 2 so the edge it took can be read back.
	constexpr uint16 WaitLocalSlot = 9;
	auto AddProgram = [WaitLocalSlot](FPeopleBehaviorModel& Model, int32 ProgramId, uint16 Opcode, const uint16(&Args)[4])
	{
		FBhavProgram Program;
		Program.Id = ProgramId;
		Program.Name = FString::Printf(TEXT("Opcode %d"), int32(Opcode));
		FBhavRecord Action;
		Action.Token = Opcode;
		Action.TrueNext = 1;
		Action.FalseNext = 2;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Action.Args[Index] = Args[Index];
		}
		FBhavRecord Wait;
		Wait.Token = 0;
		Wait.Args[0] = WaitLocalSlot;
		Program.Records = {Action, Wait, Wait};
		Model.ProgramsById.Add(Program.Id, MoveTemp(Program));
	};

	// The wait counter has to start non-zero or the wait succeeds immediately and walks back onto
	// record 0 (default edge) until the runaway guard trips.
	auto Push = [WaitLocalSlot](FSimCopterPersonContext& Context, int32 ProgramId)
	{
		FSimCopterPersonContext::FFrame Frame;
		Frame.ProgramId = ProgramId;
		Frame.Locals[WaitLocalSlot] = 200;
		Context.Stack.Add(Frame);
	};

	FPeopleBehaviorModel Model;
	AddProgram(Model, 1050, 50, {0, 0, 0, 0});
	AddProgram(Model, 1078, 78, {0, 0, 0, 0});
	AddProgram(Model, 1080, 80, {0, 0, 0, 0});
	AddProgram(Model, 1031, 31, {0, 0, 0, 0});
	AddProgram(Model, 1032, 32, {0, 0, 0, 0});
	AddProgram(Model, 1024, 24, {0, 1, 2, 3});
	AddProgram(Model, 1027, 27, {0, 0, 0, 0});
	AddProgram(Model, 1028, 28, {0, 0, 0, 0});
	AddProgram(Model, 1035, 35, {0, 0, 0, 0});
	AddProgram(Model, 1036, 36, {12, 1, 0, 0});
	AddProgram(Model, 1079, 79, {0, 0, 0, 0});

	// Opcode 50: the free-seat count lands in the local, and an object with no room to report leaves
	// whatever was there (the original writes nothing on that arm). Both take the true edge.
	{
		FStubBehaviorWorld World;
		World.SelectionRoom = 3;
		FSimCopterPersonContext Context;
		Push(Context, 1050);
		Context.Stack.Last().Locals[0] = 77;
		FSimCopterBehaviorVM::Tick(Context, Model, World);
		TestEqual(TEXT("Opcode 50 writes the free seat count"), int32(Context.Stack.Last().Locals[0]), 3);
		TestEqual(TEXT("Opcode 50 always succeeds"), Context.Stack.Last().RecordIndex, 1);

		FStubBehaviorWorld NoRoomWorld;
		NoRoomWorld.SelectionRoom = INDEX_NONE;
		FSimCopterPersonContext NoRoomContext;
		Push(NoRoomContext, 1050);
		NoRoomContext.Stack.Last().Locals[0] = 77;
		FSimCopterBehaviorVM::Tick(NoRoomContext, Model, NoRoomWorld);
		TestEqual(TEXT("Opcode 50 leaves the local alone for anything else"), int32(NoRoomContext.Stack.Last().Locals[0]), 77);
		TestEqual(TEXT("Opcode 50 still succeeds"), NoRoomContext.Stack.Last().RecordIndex, 1);
	}

	// Opcode 78: yields while the flight is still running, true once it is done.
	{
		FStubBehaviorWorld World;
		World.bBeamStillFlying = true;
		FSimCopterPersonContext Context;
		Push(Context, 1078);
		TestEqual(
			TEXT("Opcode 78 yields mid-flight"),
			int32(FSimCopterBehaviorVM::Tick(Context, Model, World)),
			int32(EBhavStepResult::Ran));
		TestEqual(TEXT("Opcode 78 stays on its own record while flying"), Context.Stack.Last().RecordIndex, 0);
		TestEqual(TEXT("Opcode 78 steps once per tick"), World.BeamSteps, 1);

		World.bBeamStillFlying = false;
		FSimCopterBehaviorVM::Tick(Context, Model, World);
		TestEqual(TEXT("Opcode 78 advances once it arrives"), Context.Stack.Last().RecordIndex, 1);
	}

	// Opcode 80 always succeeds and always asks the world for the reaction.
	{
		FStubBehaviorWorld World;
		FSimCopterPersonContext Context;
		Push(Context, 1080);
		FSimCopterBehaviorVM::Tick(Context, Model, World);
		TestEqual(TEXT("Opcode 80 delegates the bump reaction"), World.ReactToSourceCalls, 1);
		TestEqual(TEXT("Opcode 80 always succeeds"), Context.Stack.Last().RecordIndex, 1);
	}

	// Opcode 31 follows the world's answer; opcode 32 with no interaction source is a successful
	// no-op (the interface default), not the random-facing failure the stub used to take.
	{
		FStubBehaviorWorld World;
		World.bFaceAwayFromSelection = false;
		FSimCopterPersonContext Context;
		Push(Context, 1031);
		FSimCopterBehaviorVM::Tick(Context, Model, World);
		TestEqual(TEXT("Opcode 31 fails when there is no bearing"), Context.Stack.Last().RecordIndex, 2);

		FSimCopterPersonContext SourceContext;
		Push(SourceContext, 1032);
		SourceContext.Attributes[EBhavAttr::Facing] = 5;
		FSimCopterBehaviorVM::Tick(SourceContext, Model, World);
		TestEqual(TEXT("Opcode 32 with no source succeeds"), SourceContext.Stack.Last().RecordIndex, 1);
		TestEqual(TEXT("Opcode 32 with no source leaves the facing"), int32(SourceContext.Attributes[EBhavAttr::Facing]), 5);
	}

	// Opcode 24 writes bearing/mean/count into args[1..3] and fails outright without a riot.
	{
		FStubBehaviorWorld World;
		World.bHasRiotCrowd = true;
		World.RiotFacingOctant = 6;
		World.RiotAverageAgitation = 4;
		World.RiotCount = 9;
		FSimCopterPersonContext Context;
		Push(Context, 1024);
		Context.Stack.Last().Locals[0] = 1; // the search radius BHAV 852 passes
		FSimCopterBehaviorVM::Tick(Context, Model, World);
		TestEqual(TEXT("Opcode 24 bearing local"), int32(Context.Stack.Last().Locals[1]), 6);
		TestEqual(TEXT("Opcode 24 mean agitation local"), int32(Context.Stack.Last().Locals[2]), 4);
		TestEqual(TEXT("Opcode 24 head count local"), int32(Context.Stack.Last().Locals[3]), 9);
		TestEqual(TEXT("Opcode 24 true edge"), Context.Stack.Last().RecordIndex, 1);

		FStubBehaviorWorld NoRiotWorld;
		FSimCopterPersonContext NoRiotContext;
		Push(NoRiotContext, 1024);
		NoRiotContext.Stack.Last().Locals[0] = 1;
		FSimCopterBehaviorVM::Tick(NoRiotContext, Model, NoRiotWorld);
		TestEqual(TEXT("Opcode 24 fails with no live riot"), NoRiotContext.Stack.Last().RecordIndex, 2);
	}

	// Opcode 27: 3.0 units normally, 1.5 once agitation passes 5.
	{
		FStubBehaviorWorld World;
		FSimCopterPersonContext Context;
		Push(Context, 1027);
		Context.Attributes[EBhavAttr::Speed] = 5;
		FSimCopterBehaviorVM::Tick(Context, Model, World);
		TestEqual(TEXT("Opcode 27 calm body radius"), World.BodyRadiusUnits, 3.0f);

		FSimCopterPersonContext AgitatedContext;
		Push(AgitatedContext, 1027);
		AgitatedContext.Attributes[EBhavAttr::Speed] = 6;
		FSimCopterBehaviorVM::Tick(AgitatedContext, Model, World);
		TestEqual(TEXT("Opcode 27 agitated body radius"), World.BodyRadiusUnits, 1.5f);
	}

	// Opcode 28 ends the tick when it converts someone - it must not reach the despawn path.
	{
		FStubBehaviorWorld World;
		World.bJoinRiotResult = true;
		FSimCopterPersonContext Context;
		Push(Context, 1028);
		TestEqual(
			TEXT("Opcode 28 yields after joining a riot"),
			int32(FSimCopterBehaviorVM::Tick(Context, Model, World)),
			int32(EBhavStepResult::Ran));
		TestEqual(TEXT("Opcode 28 asks to join once"), World.JoinRiotCalls, 1);
		TestFalse(TEXT("Opcode 28 does not request a despawn"), Context.bRequestDespawn);

		FStubBehaviorWorld NoRiotWorld;
		FSimCopterPersonContext NoRiotContext;
		Push(NoRiotContext, 1028);
		FSimCopterBehaviorVM::Tick(NoRiotContext, Model, NoRiotWorld);
		TestEqual(TEXT("Opcode 28 fails with no live riot"), NoRiotContext.Stack.Last().RecordIndex, 2);
	}

	// Opcode 35 is a collapse, not a despawn: it converts below the medevac cap and does nothing at
	// or above it.
	{
		FStubBehaviorWorld World;
		World.ActiveMedevacs = 0;
		FSimCopterPersonContext Context;
		Push(Context, 1035);
		Context.Stack.Last().Locals[0] = 3; // BHAV 906's difficulty + 2
		TestEqual(
			TEXT("Opcode 35 yields after collapsing"),
			int32(FSimCopterBehaviorVM::Tick(Context, Model, World)),
			int32(EBhavStepResult::Ran));
		TestEqual(TEXT("Opcode 35 collapses once"), World.CollapseCalls, 1);
		TestFalse(TEXT("Opcode 35 never despawns the person"), Context.bRequestDespawn);

		FStubBehaviorWorld BusyWorld;
		BusyWorld.ActiveMedevacs = 3;
		FSimCopterPersonContext BusyContext;
		Push(BusyContext, 1035);
		BusyContext.Stack.Last().Locals[0] = 3;
		FSimCopterBehaviorVM::Tick(BusyContext, Model, BusyWorld);
		TestEqual(TEXT("Opcode 35 leaves them standing at the cap"), BusyContext.Stack.Last().RecordIndex, 2);
		TestEqual(TEXT("Opcode 35 does not collapse at the cap"), BusyWorld.CollapseCalls, 0);
	}

	// Opcode 36 writes the fire distance BHAV 274 branches on; opcode 79 the tick counter BHAV 444
	// subtracts.
	{
		FStubBehaviorWorld World;
		World.bHasFireNearby = true;
		World.FireTileDistance = 5;
		FSimCopterPersonContext Context;
		Push(Context, 1036);
		FSimCopterBehaviorVM::Tick(Context, Model, World);
		TestEqual(TEXT("Opcode 36 writes the fire distance"), int32(Context.Stack.Last().Locals[1]), 5);
		TestEqual(TEXT("Opcode 36 true edge"), Context.Stack.Last().RecordIndex, 1);

		World.TickCounter = 4321;
		FSimCopterPersonContext TickContext;
		Push(TickContext, 1079);
		FSimCopterBehaviorVM::Tick(TickContext, Model, World);
		TestEqual(TEXT("Opcode 79 writes the behaviour tick counter"), int32(TickContext.Stack.Last().Locals[0]), 4321);
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
			PullPatients->Records.IsValidIndex(24))
		{
			TestEqual(TEXT("BHAV 263 first selects aboard patient"), int32(PullPatients->Records[0].Token), 84);
			TestEqual(TEXT("BHAV 263 no-patient edge"), int32(PullPatients->Records[0].FalseNext), 19);
			TestEqual(TEXT("BHAV 263 no-patient edge calls helper ride"), int32(PullPatients->Records[19].Token), 269);
			TestEqual(TEXT("BHAV 263 alights the selected real patient"), int32(PullPatients->Records[3].Token), 47);
			TestEqual(TEXT("BHAV 263 totes that patient"), int32(PullPatients->Records[10].Token), 44);
			TestEqual(TEXT("BHAV 263 gives the patient the slump behavior"), int32(PullPatients->Records[24].Token), 39);
			TestEqual(TEXT("BHAV 263 pushes BHAV 802"), int32(PullPatients->Records[24].Args[0]), 802);
		}

		if (const FBhavProgram* MedicRide = Model.FindProgram(269);
			TestNotNull(TEXT("BHAV 269 medic ride program"), MedicRide) &&
			MedicRide->Records.IsValidIndex(22))
		{
			TestEqual(TEXT("BHAV 269 selects starting vehicle"), int32(MedicRide->Records[1].Token), 62);
			TestEqual(TEXT("BHAV 269 missing vehicle reaches disappear"), int32(MedicRide->Records[1].FalseNext), 9);
			TestEqual(TEXT("BHAV 269 missing vehicle disappears"), int32(MedicRide->Records[9].Token), 40);
			TestEqual(TEXT("BHAV 269 walks and boards the starting vehicle"), int32(MedicRide->Records[6].Token), 12);
			TestEqual(TEXT("BHAV 269 fallback boards its selection"), int32(MedicRide->Records[22].Token), 48);
			TestEqual(TEXT("BHAV 269 messages the starting vehicle after boarding"), int32(MedicRide->Records[15].Token), 61);
		}

		// The original ambulance interaction is data-driven end to end. Pin the exact graph that
		// seeks a state-6 victim, carries them to original object class 10, sets down that same
		// person, and posts pickup/delivery before either actor disappears.
		TestEqual(TEXT("person state 5 starts BHAV 801"), FPeopleBehaviorModel::GetStateProgramIds()[5], 801);
		if (const FBhavProgram* MedicInit = Model.FindProgram(801);
			TestNotNull(TEXT("BHAV 801 ambulance/hospital medic program"), MedicInit) &&
			MedicInit->Records.IsValidIndex(9))
		{
			TestEqual(TEXT("BHAV 801 tests XBLD hospital 209"), int32(MedicInit->Records[1].Token), 25);
			TestEqual(TEXT("BHAV 801 hospital id"), int32(MedicInit->Records[1].Args[0]), 209);
			TestEqual(TEXT("BHAV 801 hospital arm calls unload"), int32(MedicInit->Records[8].Token), 263);
			TestEqual(TEXT("BHAV 801 street arm calls victim search"), int32(MedicInit->Records[9].Token), 262);
		}

		if (const FBhavProgram* StreetMedic = Model.FindProgram(262);
			TestNotNull(TEXT("BHAV 262 street paramedic program"), StreetMedic) &&
			StreetMedic->Records.IsValidIndex(40))
		{
			TestEqual(TEXT("BHAV 262 searches by object class"), int32(StreetMedic->Records[1].Token), 15);
			TestEqual(TEXT("BHAV 262 searches for medevac victims"), int32(StreetMedic->Records[1].Args[0]), 5);
			TestEqual(TEXT("BHAV 262 search radius is eight tiles"), int32(StreetMedic->Records[1].Args[1]), 8);
			TestEqual(TEXT("BHAV 262 picks up the selected victim"), int32(StreetMedic->Records[26].Token), 44);
			TestEqual(TEXT("BHAV 262 selects the nearest emergency vehicle"), int32(StreetMedic->Records[9].Token), 272);
			TestEqual(TEXT("BHAV 262 hands the victim to that vehicle"), int32(StreetMedic->Records[11].Token), 275);
			TestEqual(TEXT("BHAV 262 returns the medic to its starting vehicle"), int32(StreetMedic->Records[40].Token), 269);
		}

		if (const FBhavProgram* NearestEmergencyVehicle = Model.FindProgram(272);
			TestNotNull(TEXT("BHAV 272 nearest emergency vehicle program"), NearestEmergencyVehicle) &&
			NearestEmergencyVehicle->Records.IsValidIndex(2))
		{
			TestEqual(TEXT("BHAV 272 selects an object class"), int32(NearestEmergencyVehicle->Records[2].Token), 15);
			TestEqual(TEXT("BHAV 272 selects original class 10 ambulance"), int32(NearestEmergencyVehicle->Records[2].Args[0]), 10);
		}

		if (const FBhavProgram* PutPatientOnVehicle = Model.FindProgram(275);
			TestNotNull(TEXT("BHAV 275 ambulance handoff program"), PutPatientOnVehicle) &&
			PutPatientOnVehicle->Records.IsValidIndex(6))
		{
			TestEqual(TEXT("BHAV 275 reselects the carried patient"), int32(PutPatientOnVehicle->Records[2].Token), 46);
			TestEqual(TEXT("BHAV 275 sets down that patient"), int32(PutPatientOnVehicle->Records[3].Token), 51);
			TestEqual(TEXT("BHAV 275 pushes completion onto the patient"), int32(PutPatientOnVehicle->Records[6].Token), 39);
			TestEqual(TEXT("BHAV 275 pushes BHAV 285"), int32(PutPatientOnVehicle->Records[6].Args[0]), 285);
		}

		if (const FBhavProgram* AmbulanceOutcome = Model.FindProgram(285);
			TestNotNull(TEXT("BHAV 285 ambulance patient completion"), AmbulanceOutcome) &&
			AmbulanceOutcome->Records.IsValidIndex(2))
		{
			TestEqual(TEXT("BHAV 285 first posts picked up"), int32(AmbulanceOutcome->Records[0].Token), 13);
			TestEqual(TEXT("BHAV 285 picked-up outcome"), int32(AmbulanceOutcome->Records[0].Args[0]), 0);
			TestEqual(TEXT("BHAV 285 then posts delivered"), int32(AmbulanceOutcome->Records[1].Token), 13);
			TestEqual(TEXT("BHAV 285 delivered outcome"), int32(AmbulanceOutcome->Records[1].Args[0]), 1);
			TestEqual(TEXT("BHAV 285 disappears only after both outcomes"), int32(AmbulanceOutcome->Records[2].Token), 40);
		}

		if (const FBhavProgram* PatientAtHospital = Model.FindProgram(282);
			TestNotNull(TEXT("BHAV 282 hospital patient completion"), PatientAtHospital) &&
			PatientAtHospital->Records.IsValidIndex(4))
		{
			TestEqual(TEXT("BHAV 282 tests XBLD hospital"), int32(PatientAtHospital->Records[2].Token), 25);
			TestEqual(TEXT("BHAV 282 hospital id"), int32(PatientAtHospital->Records[2].Args[0]), 209);
			TestEqual(TEXT("BHAV 282 requires a serviceable tile"), int32(PatientAtHospital->Records[3].Token), 56);
			TestEqual(TEXT("BHAV 282 posts delivered"), int32(PatientAtHospital->Records[1].Token), 13);
			TestEqual(TEXT("BHAV 282 delivery outcome"), int32(PatientAtHospital->Records[1].Args[0]), 1);
			TestEqual(TEXT("BHAV 282 leaves the map after delivery"), int32(PatientAtHospital->Records[4].Token), 37);
		}

		// The record sites the 2026-07-29 decode pass closed. These are the whole shipped use of
		// opcodes 50/78/80, so if a later change breaks one of them the behaviour is simply gone.
		if (const FBhavProgram* CopWait = Model.FindProgram(1051);
			TestNotNull(TEXT("BHAV 1051 cop wait at station"), CopWait) && CopWait->Records.IsValidIndex(14))
		{
			TestEqual(TEXT("BHAV 1051 asks the helicopter for room"), int32(CopWait->Records[13].Token), 50);
			TestEqual(TEXT("BHAV 1051 room lands in local 0"), int32(CopWait->Records[13].Args[0]), 0);
			// rec[14] is "local0 > 0": operator 0 with a literal source (scope pair 0x0907).
			TestEqual(TEXT("BHAV 1051 tests the room"), int32(CopWait->Records[14].Token), 2);
			TestEqual(TEXT("BHAV 1051 room comparison is >"), int32(CopWait->Records[14].Args[2]), 0);
			TestEqual(TEXT("BHAV 1051 room threshold is 0"), int32(CopWait->Records[14].Args[1]), 0);
			TestEqual(TEXT("BHAV 1051 free seat reaches the boarding arm"), int32(CopWait->Records[14].TrueNext), 6);
		}

		if (const FBhavProgram* Abduction = Model.FindProgram(666);
			TestNotNull(TEXT("BHAV 666 Porkchop"), Abduction) && Abduction->Records.IsValidIndex(8))
		{
			TestEqual(TEXT("BHAV 666 is the state-16 program"), FPeopleBehaviorModel::GetStateProgramIds()[16], 666);
			TestEqual(TEXT("BHAV 666 name"), Abduction->Name, FString(TEXT("Porkchop")));
			TestEqual(TEXT("BHAV 666 flies to the beam target"), int32(Abduction->Records[4].Token), 78);
			TestEqual(TEXT("BHAV 666 arrival goes invisible"), int32(Abduction->Records[4].TrueNext), 8);
			TestEqual(TEXT("BHAV 666 vanish is an expression"), int32(Abduction->Records[8].Token), 2);
			TestEqual(TEXT("BHAV 666 vanish target is the visible attribute"), int32(Abduction->Records[8].Args[0]), EBhavAttr::Visible);
		}

		if (const FBhavProgram* BumpReaction = Model.FindProgram(914);
			TestNotNull(TEXT("BHAV 914 person-neutral reaction"), BumpReaction) && BumpReaction->Records.IsValidIndex(6))
		{
			TestEqual(
				TEXT("BHAV 914 is the mode-13 reaction"),
				SimCopterInteraction::GetPersonReactionProgram(ESimCopterInteractionMode::PersonNeutral),
				914);
			TestEqual(TEXT("BHAV 914 gabs at the bumper"), int32(BumpReaction->Records[2].Token), 80);
			TestEqual(TEXT("BHAV 914 otherwise turns away from them"), int32(BumpReaction->Records[6].Token), 32);
		}

		if (const FBhavProgram* FireGawk = Model.FindProgram(274);
			TestNotNull(TEXT("BHAV 274 gawk at fire"), FireGawk) && FireGawk->Records.IsValidIndex(1))
		{
			TestEqual(TEXT("BHAV 274 looks for a fire"), int32(FireGawk->Records[0].Token), 36);
			TestEqual(TEXT("BHAV 274 fire search radius"), int32(FireGawk->Records[0].Args[0]), 12);
			TestEqual(TEXT("BHAV 274 distance lands in local 0"), int32(FireGawk->Records[0].Args[1]), 0);
		}

		if (const FBhavProgram* RiotValue = Model.FindProgram(852);
			TestNotNull(TEXT("BHAV 852 riot value"), RiotValue) && RiotValue->Records.IsValidIndex(2))
		{
			TestEqual(TEXT("BHAV 852 measures the crowd"), int32(RiotValue->Records[2].Token), 24);
			TestEqual(TEXT("BHAV 852 crowd radius local"), int32(RiotValue->Records[2].Args[0]), 0);
			TestEqual(TEXT("BHAV 852 no crowd returns false"), int32(RiotValue->Records[2].FalseNext), -1);
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
