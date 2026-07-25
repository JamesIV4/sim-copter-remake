// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/SimCopterWinch.h"

namespace SimCopterWinch
{
// FUN_00485f50 action 0x0b:
//   if (heli[0x71] == 0)      heli[0x72] = 2;   // harness deployed -> stow it first
//   else if (3 < heli[0x6f])  heli[0x72] = -1;  // otherwise pay the bucket out
int32 ResolveLowerBucketCommand(const FWinchState& State)
{
	if (!State.bHarnessStowed)
	{
		return CommandRaiseHarness;
	}
	return State.NodeCursor > LoweredNode ? CommandLowerBucket : CommandIdle;
}

// FUN_00485f50 action 0x0c:
//   if (heli[0x70] == 0 && heli[0x6f] < 0x11) heli[0x72] = 1;
int32 ResolveRaiseBucketCommand(const FWinchState& State)
{
	return (!State.bBucketStowed && State.NodeCursor < StowedNode) ? CommandRaiseBucket : CommandIdle;
}

// FUN_00485f50 action 0x0e (mirror image of 0x0b).
int32 ResolveLowerHarnessCommand(const FWinchState& State)
{
	if (!State.bBucketStowed)
	{
		return CommandRaiseBucket;
	}
	return State.NodeCursor > LoweredNode ? CommandLowerHarness : CommandIdle;
}

// FUN_00485f50 action 0x0f.
int32 ResolveRaiseHarnessCommand(const FWinchState& State)
{
	return (!State.bHarnessStowed && State.NodeCursor < StowedNode) ? CommandRaiseHarness : CommandIdle;
}

// SCHOOK: WinchStep 0x00487bb0
bool StepWinch(FWinchState& InOutState)
{
	bool bRopeEndChanged = false;

	if (InOutState.Command < CommandIdle)
	{
		// Paying out. The original decrements unconditionally here; the input layer is what
		// stops issuing the command at the lower limit, so clamp defensively.
		if (InOutState.NodeCursor > LoweredNode)
		{
			--InOutState.NodeCursor;
		}

		// Crossing node 0x10 opens the door and selects which rope-end object renders.
		if (InOutState.NodeCursor == SwapNode)
		{
			const ERopeEnd Previous = InOutState.RopeEnd;
			if (InOutState.Command == CommandLowerBucket)
			{
				InOutState.bBucketStowed = false;
				InOutState.RopeEnd = ERopeEnd::Bucket;
			}
			else
			{
				InOutState.bHarnessStowed = false;
				InOutState.RopeEnd = ERopeEnd::Harness;
			}
			bRopeEndChanged = InOutState.RopeEnd != Previous;
		}
	}
	else if (InOutState.Command > CommandIdle)
	{
		if (InOutState.NodeCursor < StowedNode)
		{
			++InOutState.NodeCursor;
		}

		// Reaching node 0x11 hides both nodes and latches the matching stowed flag.
		if (InOutState.NodeCursor == StowedNode)
		{
			if (InOutState.Command == CommandRaiseBucket)
			{
				InOutState.bBucketStowed = true;
			}
			else
			{
				InOutState.bHarnessStowed = true;
			}
		}
	}

	return bRopeEndChanged;
}

int32 GetDeployedNodeCount(const FWinchState& State)
{
	return FMath::Max(0, StowedNode - State.NodeCursor);
}
}
