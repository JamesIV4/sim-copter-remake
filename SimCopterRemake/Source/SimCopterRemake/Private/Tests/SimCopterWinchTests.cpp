// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/SimCopterWinch.h"
#include "Misc/AutomationTest.h"

using namespace SimCopterWinch;

namespace
{
// Runs the winch until it settles, mirroring the original's one-step-per-frame update.
void RunWinch(FWinchState& State, int32 Command, int32 MaxSteps = 64)
{
	State.Command = Command;
	for (int32 Step = 0; Step < MaxSteps; ++Step)
	{
		StepWinch(State);
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterWinchConstantsTest,
	"SimCopter.Winch.Constants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterWinchConstantsTest::RunTest(const FString& Parameters)
{
	// FUN_00485f50 / FUN_00487bb0 node limits and command values.
	TestEqual(TEXT("stowed node is 0x11"), StowedNode, 17);
	TestEqual(TEXT("lowered node is 3"), LoweredNode, 3);
	TestEqual(TEXT("swap node is 0x10"), SwapNode, 16);
	TestEqual(TEXT("rope pays out 14 nodes"), StowedNode - LoweredNode, 14);

	// A fresh helicopter has both attachments stowed and shows the bucket.
	const FWinchState Fresh;
	TestTrue(TEXT("bucket starts stowed"), Fresh.bBucketStowed);
	TestTrue(TEXT("harness starts stowed"), Fresh.bHarnessStowed);
	TestTrue(TEXT("nothing starts deployed"), Fresh.IsFullyStowed());
	TestEqual(TEXT("nothing is paid out"), GetDeployedNodeCount(Fresh), 0);
	TestTrue(TEXT("rope end starts as the bucket"), Fresh.RopeEnd == ERopeEnd::Bucket);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterWinchCommandTest,
	"SimCopter.Winch.CommandResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterWinchCommandTest::RunTest(const FString& Parameters)
{
	FWinchState State;

	// Everything stowed: lowering either attachment pays that one out.
	TestEqual(TEXT("lower bucket from stowed"), ResolveLowerBucketCommand(State), CommandLowerBucket);
	TestEqual(TEXT("lower harness from stowed"), ResolveLowerHarnessCommand(State), CommandLowerHarness);
	// ...and raising does nothing, because nothing is out.
	TestEqual(TEXT("raise bucket from stowed is idle"), ResolveRaiseBucketCommand(State), CommandIdle);
	TestEqual(TEXT("raise harness from stowed is idle"), ResolveRaiseHarnessCommand(State), CommandIdle);

	// Harness deployed: asking for the bucket raises the harness first (the "one attachment
	// at a time" rule) rather than paying out a second rope end.
	State.bHarnessStowed = false;
	State.NodeCursor = LoweredNode;
	State.RopeEnd = ERopeEnd::Harness;
	TestEqual(
		TEXT("lower bucket while the harness is out raises the harness"),
		ResolveLowerBucketCommand(State),
		CommandRaiseHarness);

	// Bucket deployed: the mirror case.
	State = FWinchState();
	State.bBucketStowed = false;
	State.NodeCursor = LoweredNode;
	TestEqual(
		TEXT("lower harness while the bucket is out raises the bucket"),
		ResolveLowerHarnessCommand(State),
		CommandRaiseBucket);
	TestEqual(
		TEXT("raise bucket while it is out and below the top"),
		ResolveRaiseBucketCommand(State),
		CommandRaiseBucket);

	// At the lower limit the original stops issuing the lower command.
	State = FWinchState();
	State.bHarnessStowed = true;
	State.bBucketStowed = false;
	State.NodeCursor = LoweredNode;
	TestEqual(TEXT("lower bucket at the limit is idle"), ResolveLowerBucketCommand(State), CommandIdle);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSimCopterWinchExchangeTest,
	"SimCopter.Winch.RopeEndExchange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCopterWinchExchangeTest::RunTest(const FString& Parameters)
{
	FWinchState State;

	// Lower the bucket all the way.
	RunWinch(State, CommandLowerBucket);
	TestEqual(TEXT("bucket reaches the lower limit"), State.NodeCursor, LoweredNode);
	TestFalse(TEXT("bucket is deployed"), State.bBucketStowed);
	TestTrue(TEXT("harness stays stowed"), State.bHarnessStowed);
	TestTrue(TEXT("rope end shows the bucket"), State.RopeEnd == ERopeEnd::Bucket);
	TestEqual(TEXT("14 nodes are paid out"), GetDeployedNodeCount(State), 14);

	// Raise it back; the stowed flag only latches at the top.
	State.Command = CommandRaiseBucket;
	StepWinch(State);
	TestEqual(TEXT("one raise step moves one node"), State.NodeCursor, LoweredNode + 1);
	TestFalse(TEXT("bucket is still deployed mid-travel"), State.bBucketStowed);

	RunWinch(State, CommandRaiseBucket);
	TestEqual(TEXT("bucket returns to the stowed node"), State.NodeCursor, StowedNode);
	TestTrue(TEXT("bucket latches stowed at the top"), State.bBucketStowed);
	TestTrue(TEXT("everything is stowed again"), State.IsFullyStowed());

	// Now lower the harness: the rope-end object must change exactly once, at node 0x10.
	State.Command = CommandLowerHarness;
	int32 ChangeCount = 0;
	int32 ChangeNode = INDEX_NONE;
	for (int32 Step = 0; Step < 32; ++Step)
	{
		if (StepWinch(State))
		{
			++ChangeCount;
			ChangeNode = State.NodeCursor;
		}
	}
	TestEqual(TEXT("rope end swaps exactly once"), ChangeCount, 1);
	TestEqual(TEXT("rope end swaps at node 0x10"), ChangeNode, SwapNode);
	TestTrue(TEXT("rope end now shows the harness"), State.RopeEnd == ERopeEnd::Harness);
	TestFalse(TEXT("harness is deployed"), State.bHarnessStowed);
	TestTrue(TEXT("bucket is stowed"), State.bBucketStowed);
	TestEqual(TEXT("harness reaches the lower limit"), State.NodeCursor, LoweredNode);

	// The two attachments are never deployed at the same time.
	TestFalse(TEXT("only one attachment is out"), !State.bBucketStowed && !State.bHarnessStowed);

	// Idle does nothing.
	const int32 NodeBefore = State.NodeCursor;
	RunWinch(State, CommandIdle);
	TestEqual(TEXT("idle leaves the cursor alone"), State.NodeCursor, NodeBefore);

	return true;
}
